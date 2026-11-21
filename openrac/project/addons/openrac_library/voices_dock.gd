@tool
extends EditorDock
## The Voices dock: the characters' lines spoken again by an AI voice model, from your own disc.
## Four steps, one tab each, and nothing to type but names and words:
## 1. Model: this computer's preset and the models ranked for it (models.json); install one.
## 2. Characters: read the disc, listen to it (Whisper, a speaker model), and name each voice once.
## 3. Lines: any line's words, changed if you like, spoken in its character's voice; pick the take.
## 4. Cutscene: a subtitle of a cutscene of this level spoken in a character's voice, as its voice
##    line (the actor's jaw follows it in the game).
## "Put the voices in your mods" makes the overrides pack the game plays (Play with your edits).
## The work is openrac/tools/voice_lab.py's (docs/VOICES.md), run by voices_lab.gd.

const Lab = preload("voices_lab.gd")
const Play = preload("play.gd")

## Editor settings the dock keeps: the chosen model, and each server model's address.
const MODEL_SETTING: String = "openrac/voices/model"
const SERVER_SETTING: String = "openrac/voices/server/"

const GOOD_COLOUR: Color = Color(0.55, 0.9, 0.55)
const WARN_COLOUR: Color = Color(1.0, 0.8, 0.45)
const MUTED_COLOUR: Color = Color(0.7, 0.7, 0.7)
const SMALL_VOICE: int = 3
const FOLD_AFTER: int = 12

## The languages' names, for the lists.
const LANGUAGE_NAMES: Dictionary = {"en": "English", "fr": "French", "de": "German", "es": "Spanish",
	"it": "Italian", "ja": "Japanese", "ko": "Korean", "?": "Unknown"}

## Where the voice tools are and what they work on (the launcher's editor-open names them).
var python: String = ""
var tools: String = ""
var data: String = ""
var overrides: String = ""
var game: String = ""
var version: String = ""
## The disc the level's projects were read from (.openrac/disc.json), if any.
var known_disc: String = ""

var lab: Lab = Lab.new()
var started: bool = false

## What the tools last said.
var catalogue: Dictionary = {}
var summary: Dictionary = {}
var voice_rows: Array = []
var line_rows: Array = []
var selected_line: Dictionary = {}

# Header
var status: Label
var progress: ProgressBar
var stop_button: Button
var tabs: TabContainer

# 1. Model
var hardware_label: Label
var model_list: ItemList
var model_info: RichTextLabel
var install_button: Button
var remove_button: Button
var use_button: Button
var server_edit: LineEdit
var listen_label: Label
var listen_button: Button

# 2. Characters
var disc_label: Label
var read_button: Button
var disc_dialog: EditorFileDialog
var language_pick: OptionButton
var listen_run_button: Button
var voice_tree: Tree
var sample_button: Button
var name_edit: LineEdit
var name_button: Button
var known_pick: OptionButton
var voice_box: VBoxContainer
var voice_hint: Label

# 3. Lines
var who_pick: OptionButton
var line_language_pick: OptionButton
var search_edit: LineEdit
var line_tree: Tree
var line_info: Label
var words_edit: TextEdit
var original_button: Button
var save_words_button: Button
var disc_words_button: Button
var speak_button: Button
var take_pick: OptionButton
var take_button: Button
var speak_all_button: Button
var line_box: VBoxContainer
var line_hint: Label
var no_lines_hint: Label

# 4. Cutscene
var cutscene_info: Label
var cutscene_language: OptionButton
var cutscene_who: OptionButton
var cutscene_words: TextEdit
var cutscene_button: Button
var subtitle: Node = null

# Footer
var pack_button: Button
var pack_label: Label

var player: AudioStreamPlayer
## Buttons that start work, off while the tools are busy.
var actions: Array[Button] = []


func _init() -> void:
	title = "Voices"
	default_slot = EditorDock.DOCK_SLOT_RIGHT_UL  # a tab beside Properties, so Properties keeps its full height
	custom_minimum_size.x = px(260.0)
	# Scrolls rather than asks for its full height, so it never pushes the editor's other docks off.
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(scroll)
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.add_child(box)

	var top := HBoxContainer.new()
	box.add_child(top)
	status = Label.new()
	status.visible = false
	status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(status)
	stop_button = Button.new()
	stop_button.text = "Stop"
	stop_button.tooltip_text = "Stops now; what's done is kept."
	stop_button.visible = false
	stop_button.pressed.connect(stop_work)
	top.add_child(stop_button)
	progress = ProgressBar.new()
	progress.visible = false
	progress.custom_minimum_size.y = px(6.0)
	progress.show_percentage = false
	box.add_child(progress)

	tabs = TabContainer.new()
	# Four tabs never push the dock wider than it is: their names shorten instead.
	tabs.clip_tabs = true
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(tabs)
	tabs.add_child(build_model_tab())
	tabs.add_child(build_characters_tab())
	tabs.add_child(build_lines_tab())
	tabs.add_child(build_cutscene_tab())

	pack_button = action_button("Add to Mods",
		"Adds your kept voice lines to your mods. Turn it on in the launcher's Mods page.")
	pack_button.pressed.connect(make_pack)
	box.add_child(pack_button)
	pack_label = Label.new()
	pack_label.visible = false
	pack_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	pack_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.add_child(pack_label)

	player = AudioStreamPlayer.new()
	add_child(player)


static func px(value: float) -> float:
	return value * EditorInterface.get_editor_scale() if Engine.is_editor_hint() else value


## A button that starts work. Its words stay on one line: a button that wraps is squeezed to
## nothing in a row.
func action_button(text: String, tip: String) -> Button:
	var button := Button.new()
	button.text = text
	button.tooltip_text = tip
	actions.append(button)
	return button


func wrapped_label(text: String = "") -> Label:
	var label := Label.new()
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	return label


func build_model_tab() -> Control:
	var tab := VBoxContainer.new()
	tab.name = "Voice"
	hardware_label = wrapped_label()
	tab.add_child(hardware_label)
	model_list = ItemList.new()
	# Fits the catalogue's models, so what is said about the picked one sits right under them.
	model_list.custom_minimum_size.y = px(170.0)
	model_list.item_selected.connect(func(_index: int) -> void: show_model())
	tab.add_child(model_list)
	model_info = RichTextLabel.new()
	model_info.fit_content = true
	model_info.bbcode_enabled = true
	model_info.selection_enabled = true
	tab.add_child(model_info)
	server_edit = LineEdit.new()
	server_edit.placeholder_text = "Its web address"
	server_edit.visible = false
	server_edit.text_changed.connect(func(text: String) -> void:
		var row: Dictionary = picked_model()
		if not row.is_empty():
			set_setting(SERVER_SETTING + String(row["id"]), text.strip_edges()))
	tab.add_child(server_edit)
	var row := HFlowContainer.new()
	tab.add_child(row)
	use_button = action_button("Use This Voice", "Lines are spoken with this voice.")
	use_button.pressed.connect(use_model)
	row.add_child(use_button)
	install_button = action_button("Install", "Downloads what this voice needs.")
	install_button.pressed.connect(install_model)
	row.add_child(install_button)
	remove_button = action_button("Remove", "Frees disc space. You can install it again.")
	remove_button.pressed.connect(remove_model)
	row.add_child(remove_button)
	tab.add_child(HSeparator.new())
	listen_label = wrapped_label()
	tab.add_child(listen_label)
	listen_button = action_button("Install Listening", "Needed to find who speaks each line (Characters).")
	listen_button.pressed.connect(func() -> void: run("install", {"model": "listen"}, "Installing listening", refresh_models))
	tab.add_child(listen_button)
	return tab


func build_characters_tab() -> Control:
	var tab := VBoxContainer.new()
	tab.name = "Characters"
	disc_label = wrapped_label()
	tab.add_child(disc_label)
	read_button = action_button("Read the Disc", "Finds every spoken line on your disc (nothing leaves your computer).")
	read_button.pressed.connect(read_disc)
	tab.add_child(read_button)
	var row := HBoxContainer.new()
	tab.add_child(row)
	language_pick = OptionButton.new()
	language_pick.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(language_pick)
	listen_run_button = action_button("Listen", "Writes down each line and groups lines by voice. Slow the first time.")
	listen_run_button.pressed.connect(listen)
	row.add_child(listen_run_button)
	voice_tree = Tree.new()
	voice_tree.columns = 3
	voice_tree.column_titles_visible = true
	voice_tree.set_column_title(0, "Voice")
	voice_tree.set_column_title(1, "Lines")
	voice_tree.set_column_title(2, "Who")
	voice_tree.set_column_expand(1, false)
	voice_tree.set_column_custom_minimum_width(1, int(px(48.0)))
	voice_tree.hide_root = true
	voice_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	voice_tree.custom_minimum_size.y = px(160.0)
	voice_tree.item_selected.connect(show_voice)
	tab.add_child(voice_tree)
	voice_hint = wrapped_label("Pick a voice to hear it and say who it is.")
	voice_hint.modulate = MUTED_COLOUR
	tab.add_child(voice_hint)
	# Shown once a voice is picked.
	voice_box = VBoxContainer.new()
	voice_box.visible = false
	tab.add_child(voice_box)
	sample_button = Button.new()
	sample_button.text = "Hear Voice"
	sample_button.pressed.connect(hear_voice)
	voice_box.add_child(sample_button)
	var naming := HBoxContainer.new()
	voice_box.add_child(naming)
	name_edit = LineEdit.new()
	name_edit.placeholder_text = "Who is it? (Clank)"
	name_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_edit.text_submitted.connect(func(_text: String) -> void: name_voice())
	naming.add_child(name_edit)
	known_pick = OptionButton.new()
	# As wide as "Names...", not as the longest name in it.
	known_pick.fit_to_longest_item = false
	known_pick.tooltip_text = "The game's characters"
	known_pick.item_selected.connect(func(index: int) -> void:
		if index > 0:
			name_edit.text = known_pick.get_item_text(index)
		known_pick.select(0))
	naming.add_child(known_pick)
	name_button = action_button("Name It", "Gives every line of this voice to this character.")
	name_button.pressed.connect(name_voice)
	naming.add_child(name_button)
	return tab


func build_lines_tab() -> Control:
	var tab := VBoxContainer.new()
	tab.name = "Lines"
	no_lines_hint = wrapped_label("Read your disc first, in Characters: its lines are listed here.")
	no_lines_hint.modulate = MUTED_COLOUR
	tab.add_child(no_lines_hint)
	var row := HBoxContainer.new()
	tab.add_child(row)
	who_pick = OptionButton.new()
	who_pick.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	who_pick.item_selected.connect(func(_index: int) -> void: refresh_lines())
	row.add_child(who_pick)
	line_language_pick = OptionButton.new()
	line_language_pick.item_selected.connect(func(_index: int) -> void: refresh_lines())
	row.add_child(line_language_pick)
	search_edit = LineEdit.new()
	search_edit.placeholder_text = "Words or cutscene"
	search_edit.clear_button_enabled = true
	search_edit.text_submitted.connect(func(_text: String) -> void: refresh_lines())
	tab.add_child(search_edit)
	line_tree = Tree.new()
	line_tree.columns = 3
	line_tree.column_titles_visible = true
	line_tree.set_column_title(0, "Who")
	line_tree.set_column_title(1, "Words")
	line_tree.set_column_title(2, "New")
	line_tree.set_column_expand(0, false)
	line_tree.set_column_custom_minimum_width(0, int(px(80.0)))
	line_tree.set_column_expand(2, false)
	line_tree.set_column_custom_minimum_width(2, int(px(32.0)))
	line_tree.hide_root = true
	line_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	line_tree.custom_minimum_size.y = px(160.0)
	line_tree.item_selected.connect(show_line)
	tab.add_child(line_tree)
	line_info = wrapped_label()
	line_info.modulate = MUTED_COLOUR
	line_info.visible = false
	tab.add_child(line_info)
	line_hint = wrapped_label("Pick a line to change its words, hear it or speak it again.")
	line_hint.modulate = MUTED_COLOUR
	tab.add_child(line_hint)
	# Shown once a line is picked.
	line_box = VBoxContainer.new()
	line_box.visible = false
	tab.add_child(line_box)
	words_edit = TextEdit.new()
	words_edit.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	words_edit.custom_minimum_size.y = px(60.0)
	words_edit.placeholder_text = "What the character says"
	line_box.add_child(words_edit)
	var buttons := HFlowContainer.new()
	line_box.add_child(buttons)
	original_button = Button.new()
	original_button.text = "Hear Original"
	original_button.pressed.connect(func() -> void: hear(String(selected_line.get("snippet", ""))))
	buttons.add_child(original_button)
	save_words_button = action_button("Keep Words", "The words the next take says. Subtitles don't change.")
	save_words_button.pressed.connect(save_words)
	buttons.add_child(save_words_button)
	disc_words_button = action_button("Original Words", "Back to the disc's words.")
	disc_words_button.pressed.connect(func() -> void:
		words_edit.text = String(selected_line.get("disc_text", ""))
		save_words())
	buttons.add_child(disc_words_button)
	speak_button = action_button("Speak It", "Makes a new take of this line in the character's voice.")
	speak_button.pressed.connect(speak_line)
	buttons.add_child(speak_button)
	var takes := HBoxContainer.new()
	line_box.add_child(takes)
	take_pick = OptionButton.new()
	take_pick.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	take_pick.tooltip_text = "The take the game plays for this line."
	take_pick.item_selected.connect(choose_take)
	takes.add_child(take_pick)
	take_button = Button.new()
	take_button.text = "Hear"
	take_button.pressed.connect(hear_take)
	takes.add_child(take_button)
	speak_all_button = action_button("Speak All Shown", "Makes a take of every line in the list.")
	speak_all_button.pressed.connect(speak_all)
	tab.add_child(speak_all_button)
	return tab


func build_cutscene_tab() -> Control:
	var tab := VBoxContainer.new()
	tab.name = "Cutscene"
	cutscene_info = wrapped_label("Pick a subtitle in a cutscene in the Level list.")
	tab.add_child(cutscene_info)
	var row := HBoxContainer.new()
	tab.add_child(row)
	cutscene_who = OptionButton.new()
	cutscene_who.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cutscene_who.tooltip_text = "Who says it."
	row.add_child(cutscene_who)
	cutscene_language = OptionButton.new()
	cutscene_language.item_selected.connect(func(_index: int) -> void: show_subtitle())
	row.add_child(cutscene_language)
	cutscene_words = TextEdit.new()
	cutscene_words.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	cutscene_words.custom_minimum_size.y = px(60.0)
	cutscene_words.placeholder_text = "The subtitle's words"
	tab.add_child(cutscene_words)
	cutscene_button = action_button("Speak Subtitle",
		"Speaks the subtitle's words in the character's voice and adds the sound to the cutscene.")
	cutscene_button.pressed.connect(speak_subtitle)
	tab.add_child(cutscene_button)
	return tab


func _ready() -> void:
	if not Engine.is_editor_hint():
		return
	# What the launcher named on the editor's command line (editor.json's editor-open), unless set
	# before (tests).
	if python == "":
		python = Play.user_arg("--openrac-python")
		tools = Play.user_arg("--openrac-tools")
		data = Play.user_arg("--openrac-data")
		overrides = Play.user_arg("--openrac-overrides")
	if game == "":
		var level: Dictionary = level_of_project()
		game = String(level.get("game", ""))
		version = String(level.get("version", ""))
	known_disc = remembered_disc()
	visibility_changed.connect(func() -> void:
		if is_visible_in_tree() and not started:
			begin())
	EditorInterface.get_selection().selection_changed.connect(selection_changed)
	update_buttons()

	if is_visible_in_tree():
		begin()


func _exit_tree() -> void:
	if not Engine.is_editor_hint():
		return
	player.stop()
	lab.stop()


## This project's game and disc (its level.json), or {}.
static func level_of_project() -> Dictionary:
	if not DirAccess.dir_exists_absolute("res://levels"):
		return {}

	for folder in DirAccess.get_directories_at("res://levels"):
		var stats: Variant = quiet_json(FileAccess.get_file_as_string("res://levels/%s/level.json" % folder))

		if stats is Dictionary:
			return {"game": String(stats.get("game", "rac1")), "version": String(stats.get("version", "pal")),
				"folder": "res://levels/" + folder}

	return {}


## The disc the projects were read from (openrac_editor.py remember_disc()), or "".
static func remembered_disc() -> String:
	var project: String = ProjectSettings.globalize_path("res://").trim_suffix("/")

	for folder in [project, project.get_base_dir()]:
		var note: Variant = quiet_json(FileAccess.get_file_as_string(folder.path_join(".openrac/disc.json")))

		if note is Dictionary and FileAccess.file_exists(String(note.get("disc", ""))):
			return String(note["disc"])

	return ""


## Starts the tools and asks them what they know.
func begin() -> void:
	started = true

	if game not in ["rac1", "rac2"]:
		say("AI voices work in Ratchet & Clank and Going Commando levels.", WARN_COLOUR)
		return

	var problem: String = ""

	if data == "":
		problem = "Open the level from the launcher (Mods, Level editor, Open) first."
	else:
		problem = lab.start(python, Lab.arguments(tools, data, overrides, game, version))

	if problem != "":
		say(problem, WARN_COLOUR)
		return

	say("Checking this computer")
	refresh_models()
	refresh_summary()


func _process(_delta: float) -> void:
	if not started:
		return

	for said in lab.poll():
		if said["kind"] == "progress":
			show_progress(said)
		elif said["kind"] == "stopped":
			say("The voice tools stopped. " + String(said.get("text", "")).get_slice("\n", 0), WARN_COLOUR)
			update_buttons()


func say(text: String, colour: Color = Color(1, 1, 1)) -> void:
	status.text = text
	status.visible = text != ""
	status.modulate = colour


func show_progress(said: Dictionary) -> void:
	var total: int = int(said["total"])
	progress.visible = total > 0
	progress.max_value = max(total, 1)
	progress.value = int(said["done"])
	var stage: String = String(said["stage"])
	var words: Dictionary = {"cut": "Cutting the lines", "words": "Writing down the words", "voices": "Telling the voices apart",
		"install": "Installing", "load": "Loading the model", "speak": "Speaking", "pack": "Making the pack",
		"read": "Reading the disc"}
	var text: String = String(words.get(stage, stage.capitalize()))

	if total > 1:
		text += " (%d of %d)" % [int(said["done"]), total]

	if String(said["text"]) != "" and stage in ["install", "load", "speak"]:
		text += ": " + String(said["text"])

	say(text + "...")


## Runs COMMAND; THEN gets its result once it succeeded. TITLE says what is under way.
func run(command: String, args: Dictionary, title: String, then: Callable = Callable()) -> void:
	if not lab.running():
		begin()

		if not lab.running():
			return

	say(title + "...")
	lab.send(command, args, func(ok: bool, result: Variant, error: String) -> void:
		progress.visible = false
		if ok:
			say("")
			if then.is_valid():
				then.call(result)
		else:
			say(error.strip_edges(), WARN_COLOUR)
		update_buttons())
	update_buttons()


func stop_work() -> void:
	lab.stop()
	progress.visible = false
	say("Stopped. What was done is kept.")
	started = false
	begin()


func update_buttons() -> void:
	var busy: bool = lab.busy()
	stop_button.visible = busy

	for button in actions:
		button.disabled = busy

	if not busy:
		var row: Dictionary = picked_model()
		install_button.disabled = row.is_empty() or row.get("kind") != "python" or bool(row.get("installed"))
		remove_button.disabled = row.is_empty() or row.get("kind") != "python" or not bool(row.get("installed"))
		use_button.disabled = row.is_empty()
		listen_button.visible = not bool(catalogue.get("listen", false))
		var has_disc: bool = not summary.is_empty()
		listen_run_button.disabled = not has_disc or not bool(catalogue.get("listen", false))
		name_button.disabled = voice_tree.get_selected() == null
		var line_ready: bool = not selected_line.is_empty()
		for button in [save_words_button, disc_words_button, speak_button]:
			button.disabled = not line_ready
		speak_all_button.disabled = line_rows.is_empty()
		cutscene_button.disabled = subtitle == null or cutscene_who.item_count == 0
		pack_button.disabled = not has_disc

	# What a pick shows is there only once something is picked; the hint says so before.
	var voice_picked: bool = voice_tree.get_selected() != null
	voice_tree.visible = not voice_rows.is_empty()
	voice_box.visible = voice_picked
	voice_hint.visible = not voice_picked and not voice_rows.is_empty()
	line_box.visible = not selected_line.is_empty()
	line_hint.visible = selected_line.is_empty() and not line_rows.is_empty()
	line_info.visible = line_info.text != ""
	# Nothing to pick from before the disc is read: no empty lists.
	var read: bool = not summary.is_empty()
	language_pick.get_parent().visible = read
	no_lines_hint.visible = not read
	for part in [who_pick.get_parent(), search_edit, line_tree, speak_all_button]:
		part.visible = read
	cutscene_who.visible = cutscene_who.item_count > 0
	cutscene_language.visible = cutscene_language.item_count > 0
	pack_label.visible = pack_label.text != ""


# ---- 1. Model ---------------------------------------------------------------------------------------


func refresh_models(_result: Variant = null) -> void:
	var languages: String = ",".join(PackedStringArray(summary.get("languages", {"en": 1}).keys())).replace("?", "")
	run("models", {"languages": languages if languages != "" else "en"}, "Looking at this computer", show_models)


func show_models(result: Variant) -> void:
	catalogue = result
	var hw: Dictionary = catalogue["hardware"]
	var best: Variant = hw.get("best")
	var card: String = "no graphics card the voices can use"

	if best is Dictionary:
		card = "%s, %s GB" % [best.get("name", "a graphics card"), str(best.get("vram_gb", 0))]

	hardware_label.text = "This computer: %s. Best choice: %s." % [card, catalogue["preset"]["name"]]
	var chosen: String = chosen_model()
	model_list.clear()

	for row in catalogue["models"]:
		var marks: PackedStringArray = []

		if row["recommended"]:
			marks.append("best here")

		if row["installed"] and row["kind"] == "python":
			marks.append("installed")

		if row["kind"] == "server":
			marks.append("runs elsewhere")

		if not row["fits"]:
			marks.append("too big here")

		var text: String = ("● " if row["id"] == chosen else "") + String(row["name"])

		if not marks.is_empty():
			text += "  (" + ", ".join(marks) + ")"

		var index: int = model_list.add_item(text)
		model_list.set_item_metadata(index, row)

		if not row["fits"]:
			model_list.set_item_custom_fg_color(index, MUTED_COLOUR)
		elif row["recommended"]:
			model_list.set_item_custom_fg_color(index, GOOD_COLOUR)

		if row["id"] == chosen:
			model_list.select(index)

	if chosen == "" and model_list.item_count > 0:
		for index in model_list.item_count:
			if model_list.get_item_metadata(index)["recommended"]:
				model_list.select(index)

	listen_label.text = ("Listening is installed." if catalogue.get("listen") else
		"Listening isn't installed yet.")
	show_model()
	update_buttons()


func picked_model() -> Dictionary:
	var picked: PackedInt32Array = model_list.get_selected_items()
	return model_list.get_item_metadata(picked[0]) if not picked.is_empty() else {}


func show_model() -> void:
	var row: Dictionary = picked_model()
	server_edit.visible = not row.is_empty() and row["kind"] == "server"

	if row.is_empty():
		model_info.text = ""
		return

	var lines: PackedStringArray = [String(row["notes"])]

	if not row["fits"]:
		lines.append("[color=#ffcc73]%s[/color]" % row["why"])

	if not row["missing"].is_empty():
		lines.append("[color=#ffcc73]No voice for: %s.[/color]" % ", ".join(PackedStringArray(row["missing"].map(language_name))))

	var size: String = "about %s GB to download" % str(row["download_gb"]) if row["kind"] == "python" else "runs elsewhere"
	lines.append("Licence: %s%s. Uses %s; %s." % [row["license"], "" if row["commercial"] else " (free for non-commercial use)",
		{"cuda": "the graphics card", "mps": "the Mac's graphics", "cpu": "the processor (slow)", "server": "another computer"}.get(row["device"], row["device"]), size])
	model_info.text = "\n".join(lines)

	if row["kind"] == "server":
		server_edit.text = setting(SERVER_SETTING + String(row["id"]), "")

	update_buttons()


func chosen_model() -> String:
	return setting(MODEL_SETTING, "")


func use_model() -> void:
	var row: Dictionary = picked_model()

	if row.is_empty():
		return

	if row["kind"] == "python" and not row["installed"]:
		install_model()

	set_setting(MODEL_SETTING, String(row["id"]))
	show_models(catalogue)
	say("Lines will be spoken with %s." % row["name"], GOOD_COLOUR)


func install_model() -> void:
	var row: Dictionary = picked_model()

	if not row.is_empty():
		run("install", {"model": row["id"]}, "Installing " + String(row["name"]), refresh_models)


func remove_model() -> void:
	var row: Dictionary = picked_model()

	if not row.is_empty():
		run("remove", {"model": row["id"]}, "Removing " + String(row["name"]), refresh_models)


## The model lines are spoken with, and its server's address: what speak and say take.
func speaking_args() -> Dictionary:
	var model: String = chosen_model()

	if model == "":
		for row in catalogue.get("models", []):
			if row["recommended"]:
				model = row["id"]

	var args: Dictionary = {"model": model}
	var server: String = setting(SERVER_SETTING + model, "")

	if server != "":
		args["server"] = server

	return args


# ---- 2. Characters -----------------------------------------------------------------------------------


func refresh_summary(_result: Variant = null) -> void:
	lab.send("summary", {}, func(ok: bool, result: Variant, _error: String) -> void:
		if ok:
			show_summary(result)
		else:
			summary = {}
			disc_label.text = "Read your disc first: the voices come from its lines."
			if known_disc == "":
				disc_label.text += " Choose its disc image (.iso)."
			update_buttons())


func show_summary(result: Variant) -> void:
	summary = result
	disc_label.text = "%s: %d lines in %d recordings." % [summary["title"], int(summary["lines"]), int(summary["clips"])]

	if not summary["listened"]:
		disc_label.text += " Listen to them to find the voices."

	var languages: Array = summary["languages"].keys()
	languages.sort_custom(func(a: String, b: String) -> bool: return summary["languages"][a] > summary["languages"][b])

	for pick in [language_pick, line_language_pick, cutscene_language]:
		var was: String = pick.get_item_metadata(pick.selected) if pick.selected >= 0 else ""
		pick.clear()

		for code in languages:
			if code == "?" and pick != line_language_pick:
				continue

			pick.add_item(language_name(code))
			pick.set_item_metadata(pick.item_count - 1, code)

			if code == was:
				pick.select(pick.item_count - 1)

	known_pick.clear()
	known_pick.add_item("Names")

	for known in summary["known"]:
		known_pick.add_item(known)

	fill_people()
	refresh_voices()
	refresh_lines()
	show_subtitle()


func fill_people() -> void:
	var was: String = String(who_pick.get_item_metadata(who_pick.selected)) if who_pick.selected >= 0 else "*"
	who_pick.clear()
	who_pick.add_item("Everyone")
	who_pick.set_item_metadata(0, "*")
	who_pick.add_item("Not named yet")
	who_pick.set_item_metadata(1, "")
	cutscene_who.clear()

	for person in summary.get("characters", []):
		who_pick.add_item(person)
		who_pick.set_item_metadata(who_pick.item_count - 1, person)
		cutscene_who.add_item(person)

	for index in who_pick.item_count:
		if String(who_pick.get_item_metadata(index)) == was:
			who_pick.select(index)


func read_disc() -> void:
	if known_disc != "":
		run("read", {"disc": known_disc}, "Reading the disc", show_summary)
		return

	if disc_dialog == null:
		disc_dialog = EditorFileDialog.new()
		disc_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
		disc_dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
		disc_dialog.add_filter("*.iso", "Disc image")
		disc_dialog.title = "Your disc's image"
		disc_dialog.file_selected.connect(func(path: String) -> void:
			known_disc = path
			read_disc())
		add_child(disc_dialog)

	disc_dialog.popup_file_dialog()


func listen() -> void:
	var code: Variant = language_pick.get_item_metadata(language_pick.selected) if language_pick.selected >= 0 else "en"
	run("listen", {"languages": String(code)}, "Listening", refresh_summary)


func refresh_voices() -> void:
	lab.send("voices", {}, func(ok: bool, result: Variant, _error: String) -> void:
		if not ok:
			return
		voice_rows = result["voices"]
		voice_tree.clear()
		var root: TreeItem = voice_tree.create_item()
		# A disc has a long tail of voices with a line or two: fold them, to be named in Lines.
		var small: TreeItem = null
		var numbers: Dictionary = {}
		for row in voice_rows:
			var language: String = String(row["language"])
			numbers[language] = int(numbers.get(language, 0)) + 1
			var parent: TreeItem = root
			if voice_rows.size() > FOLD_AFTER and int(row["lines"]) < SMALL_VOICE and row["character"] == null:
				if small == null:
					small = voice_tree.create_item(root)
					small.set_text(0, "Voices with few lines (name them in Lines)")
					small.set_custom_color(0, MUTED_COLOUR)
					small.set_selectable(0, false)
					small.set_selectable(1, false)
					small.set_selectable(2, false)
					small.collapsed = true
				parent = small
			var item: TreeItem = voice_tree.create_item(parent)
			item.set_text(0, "%s voice %d" % [language_name(language), numbers[language]])
			item.set_text(1, str(int(row["lines"])))
			item.set_text(2, String(row["character"]) if row["character"] != null else "?")
			if row["character"] == null:
				item.set_custom_color(2, MUTED_COLOUR)
			item.set_metadata(0, row)
		update_buttons())


func show_voice() -> void:
	var item: TreeItem = voice_tree.get_selected()

	if item == null:
		return

	var row: Dictionary = item.get_metadata(0)
	name_edit.text = String(row["character"]) if row["character"] != null else ""
	sample_button.set_meta("sample", 0)
	update_buttons()
	hear_voice()


func hear_voice() -> void:
	var item: TreeItem = voice_tree.get_selected()

	if item == null:
		return

	var samples: Array = item.get_metadata(0)["samples"]

	if samples.is_empty():
		return

	# Each press the next of its samples.
	var at: int = int(sample_button.get_meta("sample", 0)) % samples.size()
	sample_button.set_meta("sample", at + 1)
	hear(String(samples[at]))


func name_voice() -> void:
	var item: TreeItem = voice_tree.get_selected()

	if item == null:
		return

	var who: String = name_edit.text.strip_edges()
	run("name", {"voice": item.get_metadata(0)["voice"], "character": who}, "Naming", func(result: Variant) -> void:
		var spread: Array = result.get("spread", [])
		say("%s: %d lines%s." % [who if who != "" else "Not named", int(result["named"]),
			" (and their voice in %d other languages)" % spread.size() if not spread.is_empty() else ""], GOOD_COLOUR)
		refresh_summary())


# ---- 3. Lines ----------------------------------------------------------------------------------------


func refresh_lines() -> void:
	if summary.is_empty():
		return

	var args: Dictionary = {"limit": 400}
	var who: String = String(who_pick.get_item_metadata(who_pick.selected)) if who_pick.selected >= 0 else "*"

	if who != "*":
		args["character"] = who

	if line_language_pick.selected >= 0:
		args["language"] = String(line_language_pick.get_item_metadata(line_language_pick.selected))

	if search_edit.text.strip_edges() != "":
		args["search"] = search_edit.text.strip_edges()

	var keep: String = String(selected_line.get("id", ""))
	lab.send("lines", args, func(ok: bool, result: Variant, _error: String) -> void:
		if not ok:
			return
		line_rows = result["lines"]
		line_tree.clear()
		var root: TreeItem = line_tree.create_item()
		for row in line_rows:
			var item: TreeItem = line_tree.create_item(root)
			item.set_text(0, String(row["character"]) if row["character"] != null else "?")
			item.set_text(1, String(row["text"]) if row["text"] != "" else "(no words yet: press Listen)")
			item.set_tooltip_text(1, "%s, %s" % [row["clip"], row["group"]])
			item.set_text(2, "✓" if row["chosen"] != null else ("…" if not row["takes"].is_empty() else ""))
			if row["edited"]:
				item.set_custom_color(1, WARN_COLOUR)
			item.set_metadata(0, row)
			if row["id"] == keep:
				item.select(0)
		line_info.text = ""
		if int(result["total"]) > line_rows.size():
			line_info.text = "Showing %d of %d lines. Search to see the others." % [line_rows.size(), int(result["total"])]
		update_buttons())


func show_line() -> void:
	var item: TreeItem = line_tree.get_selected()

	if item == null:
		return

	selected_line = item.get_metadata(0)
	words_edit.text = String(selected_line["text"])
	line_info.text = "%s (%s), %.1f s%s" % [selected_line["clip"], selected_line["group"],
		float(selected_line["end"]) - float(selected_line["start"]), ", your words" if selected_line["edited"] else ""]
	take_pick.clear()
	take_pick.add_item("Original line")
	take_pick.set_item_metadata(0, -1)

	for take in selected_line["takes"]:
		var heard: String = ""

		if take.get("missed") != null:
			heard = ", heard %d%%" % int(round(100.0 * (1.0 - float(take["missed"]))))

		take_pick.add_item("Take %d (%.1f s%s)" % [int(take["take"]), float(take.get("seconds", 0.0)), heard])
		take_pick.set_item_metadata(take_pick.item_count - 1, int(take["take"]))

		if selected_line["chosen"] != null and int(selected_line["chosen"]) == int(take["take"]):
			take_pick.select(take_pick.item_count - 1)

	update_buttons()


func save_words() -> void:
	if selected_line.is_empty():
		return

	var words: String = words_edit.text.strip_edges()
	var same: bool = words == String(selected_line["disc_text"])
	run("text", {"line": selected_line["id"], "text": "" if same else words}, "Keeping the words", func(_result: Variant) -> void:
		refresh_lines())


func speak_line() -> void:
	if selected_line.is_empty():
		return

	var words: String = words_edit.text.strip_edges()
	var args: Dictionary = speaking_args()
	args["line"] = selected_line["id"]

	if words != String(selected_line["text"]):
		# The words typed are kept for the line too, so the list and the next take say them.
		lab.send("text", {"line": selected_line["id"], "text": "" if words == String(selected_line["disc_text"]) else words},
			func(_ok: bool, _result: Variant, _error: String) -> void: pass)
		args["text"] = words

	run("speak", args, "Speaking", func(result: Variant) -> void:
		report_takes(result)
		var takes: Array = result["takes"]
		if not takes.is_empty():
			hear(String(takes[0]["file"]))
		refresh_lines_then_show())


func speak_all() -> void:
	var ids: Array = line_rows.filter(func(row: Dictionary) -> bool: return row["character"] != null).map(func(row: Dictionary) -> String: return row["id"])

	if ids.is_empty():
		say("Name who speaks these lines first (Characters).", WARN_COLOUR)
		return

	var args: Dictionary = speaking_args()
	args["line"] = ids
	run("speak", args, "Speaking %d lines" % ids.size(), func(result: Variant) -> void:
		report_takes(result)
		refresh_lines_then_show())


func report_takes(result: Dictionary) -> void:
	var failed: Array = result.get("failed", [])

	if failed.is_empty():
		say("%d new take%s." % [result["takes"].size(), "" if result["takes"].size() == 1 else "s"], GOOD_COLOUR)
	else:
		say("%d new, %d failed: %s" % [result["takes"].size(), failed.size(), failed[0]["error"]], WARN_COLOUR)


func refresh_lines_then_show() -> void:
	refresh_lines()
	# The list's answer arrives later: the selected line's takes are shown again then.
	lab.send("summary", {}, func(_ok: bool, _result: Variant, _error: String) -> void: show_line())


func choose_take(index: int) -> void:
	if selected_line.is_empty():
		return

	var take: int = int(take_pick.get_item_metadata(index))
	run("choose", {"line": selected_line["id"], "take": "none" if take < 0 else str(take)}, "Keeping the take",
		func(_result: Variant) -> void: refresh_lines())


func hear_take() -> void:
	var take: int = int(take_pick.get_item_metadata(take_pick.selected)) if take_pick.selected >= 0 else -1

	if take < 0:
		hear(String(selected_line.get("snippet", "")))
		return

	for row in selected_line.get("takes", []):
		if int(row["take"]) == take:
			hear(String(row["file"]))


# ---- 4. Cutscene -------------------------------------------------------------------------------------


func selection_changed() -> void:
	var nodes: Array[Node] = EditorInterface.get_selection().get_selected_nodes()
	subtitle = nodes[0] if nodes.size() == 1 and nodes[0].get_class() == "CutsceneSubtitle" else null
	show_subtitle()

	if subtitle != null and is_visible_in_tree():
		tabs.current_tab = 3


func cutscene_code() -> String:
	return String(cutscene_language.get_item_metadata(cutscene_language.selected)) if cutscene_language.selected >= 0 else "en"


func show_subtitle() -> void:
	if subtitle == null or not is_instance_valid(subtitle):
		subtitle = null
		cutscene_info.text = "Pick a subtitle in a cutscene in the Level list."
		cutscene_words.text = ""
		update_buttons()
		return

	var text: Dictionary = subtitle.get("text")
	var code: String = cutscene_code()
	cutscene_words.text = String(text.get(code, text.get("", "")))
	var speaker: Node = subtitle.get_node_or_null(subtitle.get("speaker"))
	cutscene_info.text = "%s at %.1f s%s." % [subtitle.name, float(subtitle.get("start")),
		", said by " + String(speaker.name) if speaker != null else ""]

	# The character whose name is the speaker's, if any.
	if speaker != null:
		for index in cutscene_who.item_count:
			if cutscene_who.get_item_text(index).to_lower().replace(" ", "") == String(speaker.name).to_lower().replace("_", ""):
				cutscene_who.select(index)

	update_buttons()


## Where a subtitle's voice line goes: res://levels/level_NN/cutscenes/voices/, as the cutscene's
## files are beside it (cutscene.cpp scene_dir_of()).
static func voice_file_of(line: Node, code: String) -> Dictionary:
	var owner_node: Node = line.owner if line.owner != null else line
	var dir: String = owner_node.scene_file_path.get_base_dir()
	var cutscene: Node = line.get_parent()
	var base: String = ("%s_%s_%s" % [cutscene.name if cutscene != null else "cutscene", line.name, code]).to_snake_case().validate_filename()
	var relative: String = "voices/%s.wav" % base
	return {"relative": relative, "absolute": ProjectSettings.globalize_path(dir.path_join("cutscenes").path_join(relative))}


func speak_subtitle() -> void:
	if subtitle == null or cutscene_who.item_count == 0:
		return

	var line: Node = subtitle
	var code: String = cutscene_code()
	var words: String = cutscene_words.text.strip_edges()
	var file: Dictionary = voice_file_of(line, code)
	var undo: EditorUndoRedoManager = EditorInterface.get_editor_undo_redo()
	var text: Dictionary = line.get("text").duplicate()
	text[code] = words
	undo.create_action("Subtitle words")
	undo.add_do_property(line, "text", text)
	undo.add_undo_property(line, "text", line.get("text"))
	undo.commit_action()
	var args: Dictionary = speaking_args()
	args.merge({"character": cutscene_who.get_item_text(cutscene_who.selected), "language": code, "text": words,
		"out": file["absolute"]})
	run("say", args, "Speaking the subtitle", func(result: Variant) -> void:
		if not is_instance_valid(line):
			return
		add_voice_line(line, code, String(file["relative"]))
		hear(String(result["file"]))
		say("The subtitle is spoken: %s." % file["relative"], GOOD_COLOUR))


## The subtitle's voice line: a CutsceneSound beside it (made, or the one made before), starting
## with it and coming from its speaker, with this language's file.
static func add_voice_line(line: Node, code: String, relative: String) -> Node:
	var parent: Node = line.get_parent()
	var voice_name: String = String(line.name) + "Voice"
	var sound: Node = parent.get_node_or_null(voice_name)
	var undo: EditorUndoRedoManager = EditorInterface.get_editor_undo_redo() if Engine.is_editor_hint() else null
	var made: bool = sound == null

	if made:
		sound = ClassDB.instantiate("CutsceneSound")
		sound.name = voice_name

	var files: Dictionary = sound.get("files").duplicate()
	var single: String = String(sound.get("file"))

	if single != "" and files.is_empty():
		files[""] = single

	files.erase("")
	files[code] = relative

	if undo != null:
		undo.create_action("Subtitle voice line")

		if made:
			undo.add_do_method(parent, "add_child", sound, true)
			undo.add_do_method(sound, "set_owner", line.owner)
			undo.add_do_reference(sound)
			undo.add_undo_method(parent, "remove_child", sound)

		undo.add_do_property(sound, "kind", 0)
		undo.add_do_property(sound, "start", line.get("start"))
		undo.add_do_property(sound, "files", files)
		undo.add_do_property(sound, "file", "")
		undo.add_do_property(sound, "source", line.get("speaker"))

		if not made:
			for property in ["kind", "start", "files", "file", "source"]:
				undo.add_undo_property(sound, property, sound.get(property))

		undo.commit_action()
	else:
		if made:
			parent.add_child(sound, true)
			sound.owner = line.owner
		sound.set("kind", 0)
		sound.set("start", line.get("start"))
		sound.set("files", files)
		sound.set("file", "")
		sound.set("source", line.get("speaker"))

	return sound


# ---- the pack ----------------------------------------------------------------------------------------


func make_pack() -> void:
	run("pack", {}, "Making the pack", func(result: Variant) -> void:
		var notes: Array = result.get("notes", [])
		if result.get("pack") == null:
			pack_label.text = "No take kept yet, so the disc's lines play."
		else:
			pack_label.text = "%d lines in %d recordings are in the AI voices mod. Turn it on in the launcher's Mods page." % [
				int(result["lines"]), int(result["clips"])]
		if not notes.is_empty():
			pack_label.text += "\n" + "\n".join(PackedStringArray(notes.slice(0, 5))))


# ---- small things ------------------------------------------------------------------------------------


func hear(path: String) -> void:
	if path == "" or not FileAccess.file_exists(path):
		return

	var stream: AudioStreamWAV = AudioStreamWAV.load_from_file(path)

	if stream == null:
		say("Couldn't play " + path.get_file(), WARN_COLOUR)
		return

	player.stream = stream
	player.play()


## TEXT as JSON, or null, without an error in the editor's log for a file that is missing or not JSON.
static func quiet_json(text: String) -> Variant:
	var json := JSON.new()
	return json.data if text != "" and json.parse(text) == OK else null


static func language_name(code: String) -> String:
	return String(LANGUAGE_NAMES.get(code, code))


static func setting(key: String, fallback: String) -> String:
	if not Engine.is_editor_hint():
		return fallback

	var settings: EditorSettings = EditorInterface.get_editor_settings()
	return String(settings.get_setting(key)) if settings.has_setting(key) else fallback


static func set_setting(key: String, value: String) -> void:
	if Engine.is_editor_hint():
		EditorInterface.get_editor_settings().set_setting(key, value)
