@tool
extends HBoxContainer
## Check level and Play level, on the editor's toolbar (plugin.gd adds them). Both save the scenes
## and run openrac/tools/openrac_editor.py on this level's project, off the editor's thread:
## - Check packs every level of the projects folder into a folder that is thrown away and lists each
##   edit the packer refuses; a click on one selects its node.
## - Play packs into the launcher's packed levels folder and starts the game on this level.
## The launcher names what they need on the editor's command line, after -- (editor.json's
## editor-open): --openrac-python, --openrac-tools, --openrac-packed, and for Play --openrac-data,
## --openrac-levels and --openrac-states (the game program sits beside the states program).

var check_button: Button
var play_button: Button
var dialog: AcceptDialog
var problems: ItemList
var summary: Label
var worker: Thread


func _init() -> void:
	name = "OpenRACPlay"
	check_button = Button.new()
	check_button.text = "Check Level"
	check_button.flat = true
	check_button.tooltip_text = "Saves and lists each edit the game can't take. Click one to pick it."
	check_button.pressed.connect(start.bind("check"))
	add_child(check_button)
	play_button = Button.new()
	play_button.text = "Play Level"
	play_button.flat = true
	play_button.tooltip_text = "Saves, builds your edits and starts the game here."
	play_button.pressed.connect(start.bind("play"))
	add_child(play_button)
	dialog = AcceptDialog.new()
	dialog.title = "Check Level"
	var box := VBoxContainer.new()
	summary = Label.new()
	summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(summary)
	problems = ItemList.new()
	problems.size_flags_vertical = Control.SIZE_EXPAND_FILL
	problems.custom_minimum_size = Vector2(0, 160)
	problems.item_activated.connect(select_problem)
	problems.item_clicked.connect(func(index: int, _at: Vector2, _button: int) -> void: select_problem(index))
	box.add_child(problems)
	dialog.add_child(box)
	add_child(dialog)


func _exit_tree() -> void:
	if worker != null and worker.is_started():
		worker.wait_to_finish()


## The value after NAME on the editor's command line (after --), or "".
static func user_arg(name: String) -> String:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	var at: int = args.find(name)
	return args[at + 1] if at >= 0 and at + 1 < args.size() else ""


## This level project's folder, and its disc version (level.json), or {} when it is not one.
static func this_level() -> Dictionary:
	for folder in DirAccess.get_directories_at("res://levels"):
		var stats: Variant = JSON.parse_string(FileAccess.get_file_as_string("res://levels/%s/level.json" % folder))

		# Ratchet & Clank's levels pack back; another game's do not yet.
		if stats is Dictionary and String(stats.get("game", "rac1")) == "rac1":
			return {"project": ProjectSettings.globalize_path("res://").trim_suffix("/"),
				"version": String(stats.get("version", "pal"))}

	return {}


## The command for KIND ("check" or "play"), or a sentence saying what is missing.
static func command_for(kind: String, level: Dictionary) -> Variant:
	var python: String = user_arg("--openrac-python")
	var tools: String = user_arg("--openrac-tools")

	if level.is_empty():
		return "This isn't a Ratchet & Clank level, so there is nothing to build."

	if python == "" or tools == "":
		return "Open the level from the launcher (Mods, Level editor, Open) first."

	var args: PackedStringArray = [tools.path_join("openrac_editor.py"), kind, level["project"]]

	if kind == "play":
		var states: String = user_arg("--openrac-states")
		var game: String = states.replace("-states", "")

		if user_arg("--openrac-packed") == "" or user_arg("--openrac-data") == "" or game == "":
			return "Open the level from the launcher (Mods, Level editor, Open) to play it."

		args.append_array(["--packed", user_arg("--openrac-packed"), "--game", game,
			"--data", user_arg("--openrac-data"), "--levels", user_arg("--openrac-levels")])

	return {"program": python, "args": args}


## Saves the scenes and runs KIND in the background; the result shows in the dialog.
func start(kind: String) -> void:
	if worker != null and worker.is_started():
		return

	var command: Variant = command_for(kind, this_level())

	if command is String:
		show_result(kind, 1, PackedStringArray([command]))
		return

	EditorInterface.save_all_scenes()
	check_button.disabled = true
	play_button.disabled = true
	check_button.text = "Checking" if kind == "check" else "Check Level"
	play_button.text = "Building" if kind == "play" else "Play Level"
	worker = Thread.new()
	worker.start(run.bind(kind, command))


## On the worker's thread: runs the command and hands its lines back to the editor's thread.
func run(kind: String, command: Dictionary) -> void:
	var output: Array = []
	var code: int = OS.execute(command["program"], command["args"], output, true)
	var lines := PackedStringArray()

	for chunk in output:
		lines.append_array(String(chunk).split("\n", false))

	finish.call_deferred(kind, code, lines)


func finish(kind: String, code: int, lines: PackedStringArray) -> void:
	worker.wait_to_finish()
	worker = null
	check_button.disabled = false
	play_button.disabled = false
	check_button.text = "Check Level"
	play_button.text = "Play Level"
	show_result(kind, code, lines)


## The result: each refused edit as a row to click, else what was done.
func show_result(kind: String, code: int, lines: PackedStringArray) -> void:
	problems.clear()
	var said := PackedStringArray()

	for line in lines:
		if line.begins_with("openrac-problem: "):
			problems.add_item(line.trim_prefix("openrac-problem: "))
		elif line.begins_with("openrac_editor: "):
			said.append(line.trim_prefix("openrac_editor: "))
		elif not line.begins_with("openrac-"):
			said.append(line)

	dialog.title = "Check Level" if kind == "check" else "Play Level"

	if problems.item_count > 0:
		summary.text = "%d edits can't be built. Click one, fix it, then check again." % problems.item_count
	elif code == 0:
		summary.text = "\n".join(said) if not said.is_empty() else "Done."
	else:
		summary.text = "\n".join(said) if not said.is_empty() else "Couldn't check the level. Try again."

	problems.visible = problems.item_count > 0
	# Sized for the editor's display scale, never past most of its window.
	dialog.popup_centered_clamped(Vector2(560, 320) * EditorInterface.get_editor_scale(), 0.8)


## Selects the node a problem names ("<node>: <why>"), when the edited scene has it.
func select_problem(index: int) -> void:
	var text: String = problems.get_item_text(index)
	var root: Node = EditorInterface.get_edited_scene_root()

	if root == null or not ":" in text:
		return

	var found: Node = root.find_child(text.get_slice(":", 0).strip_edges(), true, false)

	if found == null:
		return

	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(found)
	EditorInterface.edit_node(found)
