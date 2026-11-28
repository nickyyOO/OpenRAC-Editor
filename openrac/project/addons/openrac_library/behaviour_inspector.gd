@tool
extends EditorInspectorPlugin
## The Behaviour section in the Inspector of a moby under Game/Mobys: a preset (spin, bob, patrol,
## orbit, face the hero, follow the hero, wander), its numbers and the animations it plays standing
## and moving, kept as rc1_behaviour metadata, which
## editor/behaviours.py packs and the game's map player plays. Undoable.

## Each preset's numbers, their defaults and steps (editor/behaviours.py PRESETS), and a hint.
const PRESETS: Dictionary = {
	"spin": {"speed": [90.0, -3600.0, 3600.0, "degrees a second"]},
	"bob": {"height": [1.0, -100.0, 100.0, "units"], "period": [2.0, 0.05, 600.0, "seconds"]},
	"patrol": {"distance": [8.0, 0.0, 1000.0, "units"], "speed": [4.0, 0.0, 200.0, "units a second"]},
	"orbit": {"radius": [4.0, 0.0, 1000.0, "units"], "speed": [45.0, -3600.0, 3600.0, "degrees a second"]},
	"face_hero": {"speed": [180.0, 0.0, 3600.0, "degrees a second"]},
	"follow": {"speed": [4.0, 0.0, 200.0, "units a second"], "distance": [3.0, 0.0, 1000.0, "units"],
		"range": [30.0, 0.0, 10000.0, "units; 0 means always"]},
	"wander": {"radius": [8.0, 0.0, 1000.0, "units"], "speed": [3.0, 0.0, 200.0, "units a second"],
		"pause": [2.0, 0.0, 600.0, "seconds"]},
}

const LABELS: Dictionary = {"spin": "Spin", "bob": "Bob", "patrol": "Patrol", "orbit": "Orbit",
	"face_hero": "Face Ratchet", "follow": "Follow Ratchet", "wander": "Wander"}

## The animation metadata: the one it plays standing, and the one while it moves.
const ANIMATIONS: Array = [["sequence", "Standing animation", "Its own"], ["walk_sequence", "Moving animation", "Same as standing"]]

var undo_redo: EditorUndoRedoManager


func _can_handle(object: Object) -> bool:
	return is_moby(object as Node)


## Whether NODE is a moby of a level scene: a child of Game/Mobys.
static func is_moby(node: Node) -> bool:
	if node == null or not node is Node3D:
		return false

	var group: Node = node.get_parent()
	return group != null and group.name == "Mobys" and group.get_parent() != null and group.get_parent().name == "Game"


func _parse_begin(object: Object) -> void:
	add_custom_control(section(object as Node3D))


## The section for NODE.
func section(node: Node3D) -> Control:
	var box := VBoxContainer.new()
	var title := Label.new()
	title.text = "Simple Move"
	title.tooltip_text = "A simple move the object plays in the game, in place of its own."
	title.mouse_filter = Control.MOUSE_FILTER_PASS
	box.add_child(title)

	var choice := OptionButton.new()
	choice.add_item("None")
	choice.set_item_metadata(0, "")
	var preset: String = String(node.get_meta("rc1_behaviour", ""))

	for key: String in PRESETS:
		choice.add_item(LABELS[key])
		choice.set_item_metadata(choice.item_count - 1, key)

		if key == preset:
			choice.select(choice.item_count - 1)

	choice.item_selected.connect(func(index: int) -> void: set_preset(node, String(choice.get_item_metadata(index))))
	box.add_child(choice)

	if PRESETS.has(preset):
		var numbers: Dictionary = PRESETS[preset]

		for key: String in numbers:
			var spec: Array = numbers[key]
			box.add_child(number_row(node, key, "%s (%s)" % [key.capitalize(), spec[3]],
				float(node.get_meta("rc1_behaviour_" + key, spec[0])), spec[1], spec[2], 0.05))

		var sequences: Array = class_sequences(node)

		for row: Array in ANIMATIONS:
			var value: int = int(node.get_meta("rc1_behaviour_" + row[0], -1))

			if sequences.is_empty():
				box.add_child(number_row(node, row[0], "%s (-1: %s)" % [row[1], String(row[2]).to_lower()],
					float(value), -1.0, 255.0, 1.0))
			else:
				box.add_child(animation_row(node, row[0], row[1], row[2], sequences, value))

	return box


func number_row(node: Node3D, key: String, label: String, value: float, low: float, high: float, step: float) -> Control:
	var row := HBoxContainer.new()
	var name := Label.new()
	name.text = label
	name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name.clip_text = true
	name.tooltip_text = label
	name.mouse_filter = Control.MOUSE_FILTER_PASS
	row.add_child(name)
	var spin := SpinBox.new()
	spin.min_value = low
	spin.max_value = high
	spin.step = step
	spin.value = value
	spin.custom_minimum_size.x = 90.0 * EditorInterface.get_editor_scale()
	spin.value_changed.connect(func(number: float) -> void: set_number(node, key, number))
	row.add_child(spin)
	return row


## The animations of NODE's class, from its model's AnimationPlayer: [[sequence number, name]].
static func class_sequences(node: Node) -> Array:
	var out: Array = []
	var number := RegEx.create_from_string("seq_(\\d+)")

	for player: Node in node.find_children("*", "AnimationPlayer", true, false):
		for name: StringName in (player as AnimationPlayer).get_animation_list():
			var found: RegExMatch = number.search(String(name))

			if found != null and not out.any(func(a: Array) -> bool: return a[0] == found.get_string(1).to_int()):
				out.append([found.get_string(1).to_int(), String(name)])

	out.sort_custom(func(a: Array, b: Array) -> bool: return a[0] < b[0])
	return out


## A row to pick one of SEQUENCES (or NONE) for KEY.
func animation_row(node: Node3D, key: String, label: String, none: String, sequences: Array, value: int) -> Control:
	var row := HBoxContainer.new()
	var name := Label.new()
	name.text = label
	name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name.clip_text = true
	name.tooltip_text = label
	name.mouse_filter = Control.MOUSE_FILTER_PASS
	row.add_child(name)
	var pick := OptionButton.new()
	pick.fit_to_longest_item = false
	pick.clip_text = true
	pick.custom_minimum_size.x = 90.0 * EditorInterface.get_editor_scale()
	pick.add_item(none)
	pick.set_item_metadata(0, -1)

	for sequence: Array in sequences:
		pick.add_item("Animation %d" % sequence[0])
		pick.set_item_metadata(pick.item_count - 1, sequence[0])
		pick.set_item_tooltip(pick.item_count - 1, sequence[1])

		if sequence[0] == value:
			pick.select(pick.item_count - 1)

	pick.item_selected.connect(func(index: int) -> void: set_number(node, key, float(pick.get_item_metadata(index))))
	row.add_child(pick)
	return row


## Gives NODE the preset PRESET ("" for none), its numbers kept when they are the new preset's too.
func set_preset(node: Node3D, preset: String) -> void:
	var before: Dictionary = behaviour_meta(node)
	var after: Dictionary = {}

	if not preset.is_empty():
		after["rc1_behaviour"] = preset

		for key: String in PRESETS[preset]:
			after["rc1_behaviour_" + key] = before.get("rc1_behaviour_" + key, PRESETS[preset][key][0])

		after["rc1_behaviour_sequence"] = before.get("rc1_behaviour_sequence", -1)
		after["rc1_behaviour_walk_sequence"] = before.get("rc1_behaviour_walk_sequence", -1)

	commit(node, "Set the behaviour of %s" % node.name, before, after, true)


func set_number(node: Node3D, key: String, value: float) -> void:
	var before: Dictionary = behaviour_meta(node)
	var after: Dictionary = before.duplicate()
	after["rc1_behaviour_" + key] = int(value) if key.ends_with("sequence") else value
	commit(node, "Set %s of %s's behaviour" % [key, node.name], before, after, false)


## NODE's rc1_behaviour metadata.
static func behaviour_meta(node: Node) -> Dictionary:
	var out: Dictionary = {}

	for key: StringName in node.get_meta_list():
		if String(key) == "rc1_behaviour" or String(key).begins_with("rc1_behaviour_"):
			out[String(key)] = node.get_meta(key)

	return out


## Replaces NODE's behaviour metadata BEFORE by AFTER, undoably (or at once without an editor);
## REBUILD: the section shows other numbers after it (an undo always rebuilds it).
func commit(node: Node3D, action: String, before: Dictionary, after: Dictionary, rebuild: bool) -> void:
	if undo_redo == null:
		replace_meta(node, after)
		return

	undo_redo.create_action(action, UndoRedo.MERGE_ENDS, node)
	undo_redo.add_do_method(self, "replace_meta", node, after)
	undo_redo.add_undo_method(self, "replace_meta", node, before)

	if rebuild:
		undo_redo.add_do_method(self, "refresh", node)

	undo_redo.add_undo_method(self, "refresh", node)
	undo_redo.commit_action()


static func replace_meta(node: Node, meta: Dictionary) -> void:
	for key: String in behaviour_meta(node):
		node.remove_meta(key)

	for key: String in meta:
		node.set_meta(key, meta[key])


## Shows NODE's section again (a preset brings other numbers).
func refresh(node: Node) -> void:
	node.notify_property_list_changed()
