@tool
extends EditorDock
## The Collision dock: one panel to see and edit a level's collision and a moby class's own. It
## shows what the plugin (plugin.gd) tells it and passes each button to it; the edits themselves
## are in level_faces.gd and class_collision.gd.

const Surfaces = preload("surfaces.gd")
const Help = preload("help.gd")
const ClassCollision = preload("class_collision.gd")

# =============================================================================
# VARIABLES
# =============================================================================

## The plugin that does what the buttons ask.
var plugin: EditorPlugin

## What the open scene is, in a line.
var status_label: Label

## The level's tools (see, select, edit, add), hidden in a scene without the level's collision.
var level_box: VBoxContainer

## Shows the collision over the level.
var show_toggle: CheckButton

## The surfaces of the level, a row each.
var legend: VBoxContainer

## Clicks in the 3D view pick collision faces while on.
var pick_toggle: CheckButton

## What is selected, in words.
var selection_label: Label

## The surface id a repaint gives.
var surface_option: OptionButton

## The footstep sound class a repaint gives.
var sound_spin: SpinBox

## Whether a repaint keeps the faces out of some queries.
var excluded_check: CheckBox

## What is known of the surface id chosen.
var surface_meaning: Label

## The offset a move applies, X, Y and Z.
var move_spins: Array[SpinBox] = []

## The size of an added quad or box, X, Y and Z.
var size_spins: Array[SpinBox] = []

## Where faces are added.
var place_label: Label

## The buttons that need a selection, a level or a place, by what they need.
var needs: Dictionary = {"selection": [], "level": [], "place": [], "mesh": [], "object": [], "part": []}

## The results of the last check or edit.
var report_label: Label

## The selected object's collision, in words.
var object_label: Label

## The primitive editor: radius, height, position X, Y, Z, queries, surface.
var part_spins: Dictionary = {}

## The mode "Make class collision" uses.
var mode_option: OptionButton

## The help page.
var help_text: RichTextLabel

# =============================================================================
# CONSTANTS
# =============================================================================

## The finest step a position takes across (the leaves' grid), in game units.
const STEP_ACROSS: float = 0.0625

## The size of the colour swatch in the legend, at a display scale of 100%.
const SWATCH: Vector2 = Vector2(18, 14)

## The width a wrapped label keeps, at a display scale of 100%.
const TEXT_WIDTH: float = 220.0

# =============================================================================
# METHODS
# =============================================================================

func _init() -> void:
	title = "Collision"
	default_slot = EditorDock.DOCK_SLOT_RIGHT_UL
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(scroll)
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(box)

	status_label = wrapped(box, "")
	button(box, "How to Edit Collision", handle_help_pressed)
	help_text = RichTextLabel.new()
	help_text.bbcode_enabled = true
	help_text.fit_content = true
	help_text.text = Help.TEXT
	help_text.visible = false
	help_text.custom_minimum_size.x = px(TEXT_WIDTH)
	box.add_child(help_text)

	level_box = VBoxContainer.new()
	box.add_child(level_box)
	build_view(level_box)
	build_selection(level_box)
	build_edit(level_box)
	build_add(level_box)
	# The report is outside the level's tools: a class scene's edits report there too.
	report_label = wrapped(box, "")
	build_object(box)
	show_level(null)
	show_object({})


# --- Building ---

## VALUE pixels at the editor's scale (Editor Settings, Display Scale), so the dock looks the same
## on a 4K screen as on a small one.
static func px(value: float) -> float:
	return value * EditorInterface.get_editor_scale()


## The show toggle and the legend.
func build_view(box: VBoxContainer) -> void:
	header(box, "Look")
	show_toggle = CheckButton.new()
	show_toggle.text = "Show Collision"
	show_toggle.tooltip_text = "Colour the level's collision by ground type."
	show_toggle.toggled.connect(handle_show_toggled)
	box.add_child(show_toggle)
	needs.level.append(show_toggle)
	legend = VBoxContainer.new()
	box.add_child(legend)
	wrapped(box, "Each colour is a ground type. Yellow walls block only Ratchet.")


## The pick toggle and the selection tools.
func build_selection(box: VBoxContainer) -> void:
	header(box, "Pick")
	pick_toggle = CheckButton.new()
	pick_toggle.text = "Pick Faces"
	pick_toggle.toggled.connect(handle_pick_toggled)
	box.add_child(pick_toggle)
	needs.level.append(pick_toggle)
	wrapped(box, "Click a face to pick it. Shift-click adds, Alt-click picks the whole patch.")
	var row := HBoxContainer.new()
	box.add_child(row)
	needs.selection.append(button(row, "Pick Whole Patch", handle_connected_pressed))
	needs.selection.append(button(row, "Clear", handle_clear_pressed))
	selection_label = wrapped(box, "")


## The repaint, delete and move tools.
func build_edit(box: VBoxContainer) -> void:
	header(box, "Edit the selection")
	var grid := GridContainer.new()
	grid.columns = 2
	box.add_child(grid)
	var surface_label := Label.new()
	surface_label.text = "Ground type"
	grid.add_child(surface_label)
	surface_option = OptionButton.new()
	surface_option.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	for surface in 32:
		var known: String = Surfaces.describe(surface)
		surface_option.add_item(known, surface)
	surface_option.select(surface_option.get_item_index(Surfaces.DEFAULT_SURFACE))
	surface_option.item_selected.connect(func(_index: int) -> void: handle_surface_changed(surface_option.get_selected_id()))
	grid.add_child(surface_option)
	sound_spin = spin(grid, "Footstep sound", 0, 3, 1, 0)
	excluded_check = CheckBox.new()
	excluded_check.text = "Ignore for shots"
	box.add_child(excluded_check)
	surface_meaning = wrapped(box, "")
	handle_surface_changed(surface_option.get_selected_id())
	needs.selection.append(button(box, "Repaint", handle_repaint_pressed))
	needs.selection.append(button(box, "Delete", handle_delete_pressed))
	var row := HBoxContainer.new()
	box.add_child(row)

	for axis in ["X", "Y", "Z"]:
		move_spins.append(spin(row, axis, -1024, 1024, STEP_ACROSS, 0.0))

	needs.selection.append(button(box, "Move", handle_move_pressed))
	wrapped(box, "Z is up. Faces snap to the game's grid.")


## The add tools.
func build_add(box: VBoxContainer) -> void:
	header(box, "Add collision")
	var row := HBoxContainer.new()
	box.add_child(row)

	for axis in ["X", "Y", "Z"]:
		size_spins.append(spin(row, axis, STEP_ACROSS, 64, STEP_ACROSS, 2.0))

	place_label = wrapped(box, "")
	needs.place.append(button(box, "Add Floor", handle_quad_pressed))
	needs.place.append(button(box, "Add Box", handle_box_pressed))
	needs.mesh.append(button(box, "Collision From Model", handle_mesh_pressed))
	needs.level.append(button(box, "Add Model", handle_static_model_pressed))
	needs.level.append(button(box, "Add Spinning Model", handle_animated_model_pressed))
	wrapped(box, "New faces use the ground type and sound above.")
	needs.level.append(button(box, "Check Limits", handle_check_pressed))


## The object (moby class) tools.
func build_object(box: VBoxContainer) -> void:
	header(box, "Object collision")
	object_label = wrapped(box, "")
	needs.object.append(button(box, "Edit Its Type", handle_open_class_pressed))
	var grid := GridContainer.new()
	grid.columns = 2
	box.add_child(grid)
	part_spins.radius = spin(grid, "Radius", 0.001, 4096, 0.001, 1.0)
	part_spins.height = spin(grid, "Height", 0.001, 4096, 0.001, 1.0)
	part_spins.x = spin(grid, "Position X", -4096, 4096, 0.001, 0.0)
	part_spins.y = spin(grid, "Position Y", -4096, 4096, 0.001, 0.0)
	part_spins.z = spin(grid, "Position Z", -4096, 4096, 0.001, 0.0)
	part_spins.queries = spin(grid, "Hit by", 0, 3, 1, 3)
	part_spins.surface = spin(grid, "Ground type", 0, 255, 1, Surfaces.DEFAULT_SURFACE)

	for key in part_spins:
		needs.part.append(part_spins[key])

	needs.part.append(button(box, "Apply", handle_apply_part_pressed))
	var row := HBoxContainer.new()
	box.add_child(row)
	mode_option = OptionButton.new()

	for mode in ClassCollision.MODES:
		mode_option.add_item(mode.capitalize())
		mode_option.set_item_metadata(mode_option.item_count - 1, mode)

	row.add_child(mode_option)
	needs.object.append(button(row, "Make From Model", handle_make_class_pressed))
	wrapped(box, "Sizes are in the model's own units.")


## A bold caption between sections.
func header(box: VBoxContainer, text: String) -> void:
	box.add_child(HSeparator.new())
	var label := Label.new()
	label.text = text
	label.theme_type_variation = &"HeaderSmall"
	box.add_child(label)


## A label that wraps at the dock's width.
func wrapped(box: Control, text: String) -> Label:
	var label := Label.new()
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.custom_minimum_size.x = px(TEXT_WIDTH)
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.add_child(label)
	return label


## A button calling handler when pressed.
func button(box: Control, text: String, handler: Callable) -> Button:
	var item := Button.new()
	item.text = text
	item.pressed.connect(handler)
	box.add_child(item)
	return item


## A captioned spin box.
func spin(box: Control, caption: String, low: float, high: float, step: float, value: float) -> SpinBox:
	var label := Label.new()
	label.text = caption
	box.add_child(label)
	var item := SpinBox.new()
	item.min_value = low
	item.max_value = high
	item.step = step
	item.value = value
	item.allow_greater = high > 1000
	item.allow_lesser = low < -1000
	item.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.add_child(item)
	return item


# --- What the plugin shows ---

## The level's collision: the status line and the legend (null: the scene has none).
func show_level(faces) -> void:
	for row in legend.get_children():
		row.queue_free()

	# A scene without the level's editable collision: say why, and hide the level tools.
	if faces == null:
		status_label.text = "This file isn't a level. Open a level to edit its collision."
		level_box.visible = false
		enable("level", false)
		enable("selection", false)
		enable("place", false)
		enable("mesh", false)
		return

	level_box.visible = true
	var counts: Dictionary = faces.kinds_present()
	var by_surface: Dictionary = {}

	for kind in counts:
		var surface: int = -1 if kind < 0 else Surfaces.surface_of(kind)
		by_surface[surface] = by_surface.get(surface, 0) + counts[kind]

	var ids: Array = by_surface.keys()
	ids.sort()

	for surface in ids:
		var colour: Color = Surfaces.HERO_COLOUR if surface < 0 else Surfaces.colour(surface)
		var text: String = ("Yellow walls: %d faces" % by_surface[surface] if surface < 0
			else "%s: %d faces" % [Surfaces.describe(surface), by_surface[surface]])
		legend_row(colour, text)

	var edited: String = " Edited: %d removed, %d added." % [
		faces.removed.size() + faces.hero_removed.size(), faces.added.size()] if faces.has_edits() else ""
	status_label.text = "%d collision faces.%s" % [faces.face_count(), edited]

	# Nodes the packer cannot read either are worth knowing about.
	if not faces.unread.is_empty():
		status_label.text += " Skipped (not a model the editor can read): %s." % [
			", ".join(faces.unread)]

	enable("level", true)


## One legend row: a swatch and its text.
func legend_row(colour: Color, text: String) -> void:
	var row := HBoxContainer.new()
	var swatch := ColorRect.new()
	swatch.color = colour
	swatch.custom_minimum_size = SWATCH * px(1.0)
	swatch.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	row.add_child(swatch)
	var label := Label.new()
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.custom_minimum_size.x = px(TEXT_WIDTH - SWATCH.x)
	row.add_child(label)
	legend.add_child(row)


## The selection: its faces in words (empty faces: nothing selected).
func show_selection(faces, selected: PackedInt32Array) -> void:
	enable("selection", not selected.is_empty())

	# Nothing selected: say how to select.
	if selected.is_empty():
		selection_label.text = "Nothing picked." if pick_toggle.button_pressed else (
			"Nothing picked. Turn on Pick Faces, then click a face.")
		return

	var surfaces: Dictionary = {}
	var sounds: Dictionary = {}
	var excluded: int = 0
	var heroes: int = 0
	var added: int = 0

	for face in selected:
		# Hero faces have no type byte to describe.
		if faces.is_hero(face):
			heroes += 1
			continue

		var kind: int = faces.face_kind(face)
		surfaces[Surfaces.surface_of(kind)] = surfaces.get(Surfaces.surface_of(kind), 0) + 1
		sounds[Surfaces.sound_of(kind)] = true
		excluded += 1 if Surfaces.is_excluded(kind) else 0
		added += 1 if faces.origins[face] < 0 else 0

	var parts: PackedStringArray = PackedStringArray()

	for surface in surfaces:
		parts.append("%s (%d)" % [Surfaces.describe(surface), surfaces[surface]])

	var text: String = "%d faces picked" % selected.size()
	text += (": %s." % ", ".join(parts)) if not parts.is_empty() else "."

	# Sound classes and exclusion only for faces with a type byte.
	if not sounds.is_empty():
		var classes: Array = sounds.keys()
		classes.sort()
		text += " Footstep sound %s; %d ignored for shots." % [
			", ".join(classes.map(func(c: int) -> String: return str(c))), excluded]
	# Hero faces and added faces are worth a word each.
	if heroes > 0:
		text += " %d on yellow walls (delete only)." % heroes
	if added > 0:
		text += " %d added with this dock." % added

	selection_label.text = text

	# A single face's byte becomes the repaint's starting point.
	if selected.size() == 1 and heroes == 0:
		var kind: int = faces.face_kind(selected[0])
		surface_option.select(surface_option.get_item_index(Surfaces.surface_of(kind)))
		sound_spin.value = Surfaces.sound_of(kind)
		excluded_check.button_pressed = Surfaces.is_excluded(kind)


## Where added faces go (null: nowhere yet).
func show_place(place) -> void:
	enable("place", place != null)
	place_label.text = ("Click a face to choose where." if place == null
		else "Here: the last point you clicked.")


## Whether a mesh outside the collision is selected, for "Make collision from the selected mesh".
func show_mesh(mesh_name: String) -> void:
	enable("mesh", not mesh_name.is_empty())


## Text from the last check or edit.
func show_report(text: String) -> void:
	report_label.text = text


## The selected object's collision: {} for none, else "text", "can_open" (a moby of a level),
## "can_make" (the class scene is open), "part" (a selected part's values, or {}).
func show_object(info: Dictionary) -> void:
	object_label.text = info.get("text", "Select an object to see its collision.")
	needs.object[0].disabled = not info.get("can_open", false)
	needs.object[1].disabled = not info.get("can_make", false)
	var part: Dictionary = info.get("part", {})
	enable("part", not part.is_empty())

	# A selected part fills the editor with its values.
	if not part.is_empty():
		part_spins.radius.value = part.radius
		part_spins.height.value = part.height
		part_spins.x.value = part.position.x
		part_spins.y.value = part.position.y
		part_spins.z.value = part.position.z
		part_spins.queries.value = part.queries
		part_spins.surface.value = part.surface


## Turns a group of controls on or off.
func enable(group: String, on: bool) -> void:
	for control in needs[group]:
		# Buttons and check buttons have disabled; spin boxes are editable.
		if control is BaseButton:
			(control as BaseButton).disabled = not on
		elif control is SpinBox:
			(control as SpinBox).editable = on


## The type byte the controls give.
func chosen_kind() -> int:
	return Surfaces.type_byte(surface_option.get_selected_id(), int(sound_spin.value), excluded_check.button_pressed)


## The vector of three spin boxes.
func vector_of(spins: Array[SpinBox]) -> Vector3:
	return Vector3(spins[0].value, spins[1].value, spins[2].value)


# --- Handlers ---

## Shows or hides the help page.
func handle_help_pressed() -> void:
	help_text.visible = not help_text.visible


## Shows or hides the collision.
func handle_show_toggled(on: bool) -> void:
	plugin.set_showing(on)


## Starts or stops picking faces in the 3D view.
func handle_pick_toggled(on: bool) -> void:
	plugin.set_picking(on)


## Selects the faces connected to the selection.
func handle_connected_pressed() -> void:
	plugin.select_connected()


## Clears the selection.
func handle_clear_pressed() -> void:
	plugin.clear_selection()


## Says what the chosen surface id is known for.
func handle_surface_changed(value: float) -> void:
	surface_meaning.text = "Ground type %d." % int(value)


## Repaints the selection.
func handle_repaint_pressed() -> void:
	plugin.repaint(chosen_kind())


## Deletes the selection.
func handle_delete_pressed() -> void:
	plugin.delete_selection()


## Moves the selection.
func handle_move_pressed() -> void:
	plugin.move_selection(vector_of(move_spins))


## Adds a floor quad.
func handle_quad_pressed() -> void:
	var extent: Vector3 = vector_of(size_spins)
	plugin.add_quad(Vector2(extent.x, extent.y), chosen_kind())


## Adds a box.
func handle_box_pressed() -> void:
	plugin.add_box(vector_of(size_spins), chosen_kind())


## Makes collision from the selected mesh.
func handle_mesh_pressed() -> void:
	plugin.make_from_mesh(chosen_kind())


## Adds a project GLB as static map scenery.
func handle_static_model_pressed() -> void:
	plugin.add_static_model()


## Adds a spinning visual model that the mod renderer animates independently of native mobys.
func handle_animated_model_pressed() -> void:
	plugin.add_animated_model()


## Estimates what the packer would refuse.
func handle_check_pressed() -> void:
	plugin.check_limits()


## Opens the selected moby's class scene.
func handle_open_class_pressed() -> void:
	plugin.open_class_scene()


## Applies the part editor's values to the selected part.
func handle_apply_part_pressed() -> void:
	var values: Dictionary = {}

	for key in part_spins:
		values[key] = part_spins[key].value

	plugin.apply_part(values)


## Makes the class's collision from its model.
func handle_make_class_pressed() -> void:
	plugin.make_class_collision(String(mode_option.get_item_metadata(mode_option.selected)), chosen_kind())
