@tool
extends EditorInspectorPlugin
## The Planet section in the Properties of a level (the top object in the Level list): the name the
## ship's menu shows when the level is added as a new planet (openrac/tools/planet.py). It is kept in
## project.godot ([openrac] planet/name), as the editor has no Project Settings window.
## Share Map writes the level as one .openrac-map file players add in OpenRAC and play
## (openrac/tools/shared_map.py): the maker's own bytes only, the game's come from each player's disc.

const Play = preload("play.gd")

const NAME_SETTING: String = "openrac/planet/name"
const ID_SETTING: String = "openrac/planet/id"
const AUTHOR_SETTING: String = "openrac/map/author"

## The export running in the background, and its result window.
static var worker: Thread
static var result: AcceptDialog

## What the ship's menu shows at most (planet.py MOST_NAME).
const MOST_NAME: int = 40


func _can_handle(object: Object) -> bool:
	return is_level(object as Node)


## Whether NODE is a level: the root of a level scene (it has rc1_level).
static func is_level(node: Node) -> bool:
	if node == null or not node.is_inside_tree():
		return false

	return node.has_meta("rc1_level") and node == node.get_tree().edited_scene_root


func _parse_begin(object: Object) -> void:
	add_custom_control(section())


## Why NAME can't be a planet's name, or "" when it can (planet.py planet_name checks the same).
static func name_problem(name: String) -> String:
	if name.is_empty() or name.length() > MOST_NAME:
		return "A planet's name is 1 to %d characters." % MOST_NAME

	for i in name.length():
		var c: int = name.unicode_at(i)

		if c < 32 or c > 126:
			return "The game's font has plain letters A-Z, digits, spaces and punctuation only: \"%s\" is not one." % name[i]

	return ""


## Saves NAME as the planet's name; why not, or "".
static func save_name(name: String) -> String:
	name = name.strip_edges()
	var problem: String = name_problem(name)

	if not problem.is_empty():
		return problem

	sync_id()
	ProjectSettings.set_setting(NAME_SETTING, name)
	return "" if ProjectSettings.save() == OK else "project.godot could not be saved."


## Takes the planet's id from project.godot on disk: planet.py writes it there when the planet is
## first added, while the editor is open. Without this, the editor's next save of its settings would
## write the old empty id back, and the next add would make a second planet.
static func sync_id() -> void:
	var file := ConfigFile.new()

	if file.load("res://project.godot") != OK:
		return

	var id: String = String(file.get_value("openrac", "planet/id", ""))

	if id != String(ProjectSettings.get_setting(ID_SETTING, "")):
		ProjectSettings.set_setting(ID_SETTING, id)


func section() -> Control:
	var box := VBoxContainer.new()
	var title := Label.new()
	title.text = "Planet Name"
	title.tooltip_text = "The name the ship's menu shows when you add this level as a new planet."
	title.mouse_filter = Control.MOUSE_FILTER_PASS
	box.add_child(title)

	var field := LineEdit.new()
	field.text = String(ProjectSettings.get_setting(NAME_SETTING, ""))
	field.placeholder_text = "Letters, digits and spaces, %d at most" % MOST_NAME
	field.max_length = MOST_NAME
	box.add_child(field)

	var note := Label.new()
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.add_theme_color_override("font_color", EditorInterface.get_editor_theme().get_color("error_color", "Editor"))
	note.visible = false
	box.add_child(note)

	var apply := func() -> void:
		if field.text.strip_edges() == String(ProjectSettings.get_setting(NAME_SETTING, "")):
			note.visible = false
			return

		note.text = save_name(field.text)
		note.visible = not note.text.is_empty()

	field.text_submitted.connect(func(_text: String) -> void: apply.call())
	field.focus_exited.connect(apply)

	var made_by := Label.new()
	made_by.text = "Made By"
	made_by.tooltip_text = "Your name, shown to players who import your map."
	box.add_child(made_by)
	var author := LineEdit.new()
	author.text = String(ProjectSettings.get_setting(AUTHOR_SETTING, ""))
	author.placeholder_text = "Your name (optional)"
	author.max_length = 80
	author.text_submitted.connect(func(_text: String) -> void: save_author(author.text))
	author.focus_exited.connect(func() -> void: save_author(author.text))
	box.add_child(author)

	var share := Button.new()
	share.text = "Share Map"
	share.tooltip_text = "Saves this level as one .openrac-map file. It holds your edits and your own models, no game data: players add it in OpenRAC and play it on their own disc."
	share.pressed.connect(func() -> void:
		save_author(author.text)
		apply.call()
		pick_file())
	box.add_child(share)
	return box


static func save_author(text: String) -> void:
	text = text.strip_edges()

	if text != String(ProjectSettings.get_setting(AUTHOR_SETTING, "")):
		sync_id()
		ProjectSettings.set_setting(AUTHOR_SETTING, text)
		ProjectSettings.save()


## Asks where the map file goes, then writes it there.
static func pick_file() -> void:
	if worker != null and worker.is_started():
		return

	var picker := EditorFileDialog.new()
	picker.file_mode = EditorFileDialog.FILE_MODE_SAVE_FILE
	picker.access = EditorFileDialog.ACCESS_FILESYSTEM
	picker.add_filter("*.openrac-map", "OpenRAC map")
	picker.title = "Save your map"
	var name: String = String(ProjectSettings.get_setting(NAME_SETTING, "")).strip_edges()
	picker.current_file = (name.to_lower().replace(" ", "-") if name != "" else "my-map") + ".openrac-map"
	picker.file_selected.connect(func(path: String) -> void:
		picker.queue_free()
		export_to(path))
	picker.canceled.connect(picker.queue_free)
	EditorInterface.get_base_control().add_child(picker)
	picker.popup_file_dialog()


## Saves the scenes and writes the map to PATH off the editor's thread (it packs the level, a few
## minutes).
static func export_to(path: String) -> void:
	var python: String = Play.user_arg("--openrac-python")
	var tools: String = Play.user_arg("--openrac-tools")
	var level: Dictionary = Play.this_level()

	if level.is_empty() or python == "" or tools == "":
		show_result(1, PackedStringArray(["Open the level from the launcher (Mods, Level editor, Open) first."]))
		return

	if String(ProjectSettings.get_setting(NAME_SETTING, "")).strip_edges() == "":
		show_result(1, PackedStringArray(["Type the map's name in Planet Name first."]))
		return

	EditorInterface.save_all_scenes()
	var args := PackedStringArray([tools.path_join("openrac_editor.py"), "map", "export", level["project"], path])
	var levels: String = Play.user_arg("--openrac-levels")

	if levels != "":
		args.append_array(["--levels", levels])

	var author: String = String(ProjectSettings.get_setting(AUTHOR_SETTING, "")).strip_edges()

	if author != "":
		args.append_array(["--author", author])

	worker = Thread.new()
	worker.start(func() -> void:
		var output: Array = []
		var code: int = OS.execute(python, args, output, true)
		var lines := PackedStringArray()
		for chunk in output:
			lines.append_array(String(chunk).split("\n", false))
		finish.call_deferred(code, lines))


static func finish(code: int, lines: PackedStringArray) -> void:
	worker.wait_to_finish()
	worker = null
	show_result(code, lines)


static func show_result(code: int, lines: PackedStringArray) -> void:
	var said := PackedStringArray()

	for line in lines:
		if line.begins_with("map: "):
			said.append(line.trim_prefix("map: "))
		elif not line.begins_with("openrac-"):
			said.append(line)

	if result == null or not is_instance_valid(result):
		result = AcceptDialog.new()
		result.title = "Share Map"
		EditorInterface.get_base_control().add_child(result)

	if code == 0:
		result.dialog_text = "Your map is saved. Send the file to players: they add it in OpenRAC's Mods and play.\n\n" + "\n".join(said)
	else:
		result.dialog_text = "\n".join(said) if not said.is_empty() else "Couldn't save the map. Try again."

	result.popup_centered_clamped(Vector2(560, 300) * EditorInterface.get_editor_scale(), 0.8)
