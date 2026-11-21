@tool
extends EditorPlugin
## The OpenRAC assets plugin: the Assets dock (dock.gd) and placing what it gives. Editor tooling
## only. library.gd finds the assets and brings them into the project; this file puts them where
## the packer reads them:
## - a class this level has goes under Game/Mobys, Game/Ties or Game/Shrubs, as the game's own;
## - anything else goes under Game/StaticAssets as static scenery (openrac/tools/static_assets.py),
##   with the class scale its own scene gives it and collision on for mobys and ties.
## Godot's own drop (from the dock into the 3D view or the Scene dock) may put the node anywhere:
## it is moved to its group right after, keeping where it was dropped.
## behaviour_inspector.gd adds the Behaviour section to a moby's Inspector, foreign_inspector.gd the
## Imported class section to a moby that came with its own logic from another level. play.gd puts
## Check level and Play level on the toolbar.
## voices_dock.gd is the Voices dock: the characters' lines spoken again by AI voice models.
## draw_distance.gd draws ties and shrubs only as far as the game does in the editor's light mode.

const Dock = preload("dock.gd")
const Library = preload("library.gd")
const BehaviourInspector = preload("behaviour_inspector.gd")
const ForeignInspector = preload("foreign_inspector.gd")
const Play = preload("play.gd")
const VoicesDock = preload("voices_dock.gd")
const DrawDistance = preload("draw_distance.gd")

## The groups under Game, by kind, for an asset the level has.
const GROUPS: Dictionary = {"mobys": "Mobys", "ties": "Ties", "shrubs": "Shrubs"}

## Static scenery (openrac/tools/static_assets.py GROUP).
const STATIC_GROUP: String = "StaticAssets"

## How far in front of the 3D view's camera Add places an asset, in the game's units.
const ADD_DISTANCE: float = 12.0

var dock: Dock
var behaviour_inspector: BehaviourInspector
var foreign_inspector: ForeignInspector
## Check level and Play level, on the toolbar (play.gd).
var play: Play
## The Voices dock (voices_dock.gd): the characters' lines in AI voices.
var voices: VoicesDock

## What placed files are, by res:// path: the asset placed, until its node arrives.
var pending: Dictionary = {}

## Set while an asset waits for the editor to import it (ready_to_place()).
var waiting_for_import: bool = false

## Nodes added this frame, to put where they belong once the drop has finished.
var arrived: Array[Node] = []

## Set while Add commits its own edit: what it adds is placed already, no drop to settle.
var placing: bool = false

## Nodes settle() moves, by instance id: moving one adds it to the tree again, which is no drop.
var settling: Dictionary = {}

## The MCP job (mcp_tools.gd) the add under way finishes, the node it added and its result.
var active_job: String = ""
var last_added: Node = null
var last_result: Dictionary = {}

## The level's tools for AI assistants (MCP), when the editor has its MCP server.
var mcp_tools: Array[RefCounted] = []


func _enter_tree() -> void:
	dock = Dock.new()
	dock.plugin = self
	add_dock(dock)
	voices = VoicesDock.new()
	add_dock(voices)
	behaviour_inspector = BehaviourInspector.new()
	behaviour_inspector.undo_redo = get_undo_redo()
	add_inspector_plugin(behaviour_inspector)
	foreign_inspector = ForeignInspector.new()
	foreign_inspector.level_name = level_name
	foreign_inspector.undo_redo = get_undo_redo()
	add_inspector_plugin(foreign_inspector)
	scene_changed.connect(handle_scene_changed)
	get_tree().node_added.connect(handle_node_added)
	handle_scene_changed(EditorInterface.get_edited_scene_root())
	EditorInterface.get_editor_settings().settings_changed.connect(handle_settings_changed)
	add_planet_settings()
	play = Play.new()
	add_control_to_container(CONTAINER_TOOLBAR, play)
	register_mcp()


## Gives the editor's MCP server (docs/MCP.md) the level's tools, cutscene tools, guides and prompts.
func register_mcp() -> void:
	var mcp: Object = mcp_server()

	if mcp == null:
		return

	var level_tools: RefCounted = preload("mcp_tools.gd").new()
	level_tools.register(mcp, self)
	var cutscene_tools: RefCounted = preload("mcp_cutscene_tools.gd").new()
	cutscene_tools.register(mcp, self)
	var knowledge: RefCounted = preload("mcp_knowledge.gd").new()
	knowledge.register(mcp, level_tools)
	mcp_tools = [level_tools, cutscene_tools, knowledge]


## The level as a new planet (openrac/tools/planet.py, the launcher's "Add as a new planet"): its
## name in the ship's menu, and its id, which the first add sets so later ones update the same
## planet. In Project Settings, OpenRAC > Planet.
static func add_planet_settings() -> void:
	for entry in [["openrac/planet/name", "Name in the ship's menu (letters, digits, spaces; 40 at most)"],
			["openrac/planet/id", "Set when the planet is first added."]]:
		if not ProjectSettings.has_setting(entry[0]):
			ProjectSettings.set_setting(entry[0], "")
		ProjectSettings.set_initial_value(entry[0], "")
		ProjectSettings.set_as_basic(entry[0], true)
		ProjectSettings.add_property_info({"name": entry[0], "type": TYPE_STRING, "hint": PROPERTY_HINT_PLACEHOLDER_TEXT,
			"hint_string": entry[1]})


func _exit_tree() -> void:
	for part in mcp_tools:
		part.unregister()

	mcp_tools.clear()

	if get_tree().node_added.is_connected(handle_node_added):
		get_tree().node_added.disconnect(handle_node_added)

	scene_changed.disconnect(handle_scene_changed)
	EditorInterface.get_editor_settings().settings_changed.disconnect(handle_settings_changed)
	remove_control_from_container(CONTAINER_TOOLBAR, play)
	play.queue_free()
	play = null
	remove_inspector_plugin(behaviour_inspector)
	behaviour_inspector = null
	remove_inspector_plugin(foreign_inspector)
	foreign_inspector = null
	remove_dock(dock)
	dock.queue_free()
	dock = null
	remove_dock(voices)
	voices.queue_free()
	voices = null


## Whether PATH is imported and can be placed now. When the editor is still bringing it in (a new
## folder takes a scan of its own), says so and calls AGAIN once it is imported (up to a minute).
func ready_to_place(path: String, again: Callable) -> bool:
	if waiting_for_import:
		report("Still bringing the last asset in; try again in a moment.", true)
		return false

	if Library.imported(path) or not FileAccess.file_exists(path):
		return true

	waiting_for_import = true
	report("Loading %s; it's placed when ready." % path.get_file(), false, false)
	wait_for_import(path, again)
	return false


## Has the editor import PATH, a file just brought in for a drag, and says when it can be dragged.
func wait_until_imported(path: String, name: String) -> void:
	if waiting_for_import:
		return

	waiting_for_import = true
	wait_for_import(path, func() -> void: report("%s is ready: drag it in." % name, false, false))


## Waits for PATH's import, then calls AGAIN (ready_to_place()).
func wait_for_import(path: String, again: Callable) -> void:
	var filesystem: EditorFileSystem = EditorInterface.get_resource_filesystem()

	if not filesystem.is_scanning():
		filesystem.scan()

	for _i in range(240):
		await get_tree().create_timer(0.25).timeout

		if dock == null:    # the plugin was switched off meanwhile
			waiting_for_import = false
			return

		if Library.imported(path) and not filesystem.is_scanning():
			waiting_for_import = false
			again.call()
			return

	waiting_for_import = false
	report("Couldn't load %s." % path, true)


## The name of LEVEL of the edited level's disc, as the dock read it, or "".
func level_name(level: int) -> String:
	var at: Dictionary = here()

	for entry in dock.levels if dock != null else []:
		if entry["game"] == at.get("game") and entry["disc"] == at.get("disc") and int(entry["level"]) == level:
			return String(entry["name"])

	return ""


## Says TEXT in the dock; a FINAL one also ends the add under way (its MCP job, last_result).
func report(text: String, failed: bool = false, final: bool = true) -> void:
	if dock != null:
		dock.show_report(text, failed)

	if not final:
		return

	last_result = {"ok": not failed, "message": text}
	var root: Node = EditorInterface.get_edited_scene_root()

	if failed:
		last_result["error"] = text
	elif last_added != null and is_instance_valid(last_added) and root != null and root.is_ancestor_of(last_added):
		last_result["node"] = String(root.get_path_to(last_added))

	if not active_job.is_empty():
		var mcp: Object = mcp_server()

		if mcp != null:
			mcp.job_finish(active_job, last_result, failed)

		active_job = ""


## The editor's MCP server (EditorMCPServer), or null in an editor without one.
static func mcp_server() -> Object:
	if not ClassDB.class_exists("EditorMCPServer"):
		return null

	return ClassDB.class_call_static("EditorMCPServer", "get_singleton")


## Where an add puts NODE: PLACE ({"position": game coordinates, "yaw": degrees about the game's
## up}) when given, else AT in front of the 3D view's camera, facing it.
func placement(game: Node3D, asset: Dictionary, node: Node3D, camera: Camera3D, at: Vector3, place: Variant) -> Transform3D:
	if place is Dictionary:
		var position: Vector3 = place.get("position", Vector3.ZERO)
		var turn := Basis(Vector3(0, 0, 1), deg_to_rad(float(place.get("yaw", 0.0))))
		return game.global_transform * Transform3D(turn * node.basis, position)

	if camera != null:
		return Transform3D(facing(game.global_basis, at, camera.global_position, Library.front_for(asset)) * node.basis, at)

	return Transform3D(game.global_basis * node.basis, at)


func handle_scene_changed(root: Node) -> void:
	if dock != null:
		dock.set_here(Library.here_of(root.scene_file_path) if root != null else {})

	if root != null:
		show_skins(root)
		stand_up_lying.call_deferred()
		Callable(DrawDistance, "apply_edited").call_deferred()


## The light mode may have changed: the edited level's draw distances follow it (draw_distance.gd).
## Deferred, so the editor has set its light mode first.
func handle_settings_changed() -> void:
	Callable(DrawDistance, "apply_edited").call_deferred()


## Set on a scene whose lying assets were stood up (stand_up_lying()), so it is done once.
const STOOD_UP: StringName = &"openrac_stood_up"


## Assets dragged in before drops stood up were saved as Godot dropped them: lying on their side,
## their up on the game's y. Stands each one up once, keeping where its front points and its size,
## as one undoable edit. Only the editor's own nodes (the extracted ones carry rc1_index).
func stand_up_lying(root: Node = null) -> void:
	# Deferred, so the scene may be closed by then: the edited one, whatever it is now.
	if root == null:
		root = EditorInterface.get_edited_scene_root()

	if root == null or root != EditorInterface.get_edited_scene_root() or root.has_meta(STOOD_UP):
		return

	var game: Node3D = root.get_node_or_null("Game") as Node3D

	if game == null:
		return

	var nodes: Array[Node3D] = []
	var bases: Array[Basis] = []

	for group_name in GROUPS.values() + [STATIC_GROUP]:
		var group: Node = game.get_node_or_null(NodePath(group_name))

		for node in group.get_children() if group != null else []:
			if not node is Node3D or node.has_meta("rc1_index"):
				continue

			var upright: Variant = stood_up(game.global_basis, (node as Node3D).global_basis)

			if upright != null:
				nodes.append(node)
				bases.append(upright)

	if nodes.is_empty():
		return

	root.set_meta(STOOD_UP, true)
	var undo_redo := get_undo_redo()
	undo_redo.create_action("Stand up assets placed lying down", UndoRedo.MERGE_DISABLE, root)

	for i in range(nodes.size()):
		undo_redo.add_do_property(nodes[i], "global_basis", bases[i])
		undo_redo.add_undo_property(nodes[i], "global_basis", nodes[i].global_basis)

	undo_redo.commit_action()

	if dock != null:
		report("Stood up %d asset%s placed lying down. Ctrl+Z puts them back." % [
			nodes.size(), "" if nodes.size() == 1 else "s"])


## The world basis WORLD (of a node under Game, whose world basis is GAME) stood up, when it lies
## as Godot's drop left it (its y on the game's up, its z level); else null.
static func stood_up(game: Basis, world: Basis) -> Variant:
	var local: Basis = game.inverse() * world
	var up := Vector3(0, 0, 1)

	if local.y.normalized().dot(up) < 0.99 or absf(local.z.normalized().dot(up)) > 0.05:
		return null

	var side: Vector3 = local.x.normalized()
	var upright := Basis(side * local.x.length(), up.cross(side) * local.y.length(), up * local.z.length())
	return game * upright


## Each moby with a skin shows the skin, not its class's own model (which the game does not draw):
## a "SkinLook" child read from the skin's file, which the scene does not save.
func show_skins(root: Node) -> void:
	var mobys: Node = root.get_node_or_null("Game/Mobys")

	if mobys == null:
		return

	for moby in mobys.get_children():
		if not moby.has_meta("rc1_skin") or moby.has_node("SkinLook"):
			continue

		var look: Node3D = Library.scene_of(String(moby.get_meta("rc1_skin")))

		if look == null:
			continue

		var model: Node3D = moby.get_node_or_null("Model") as Node3D

		if model != null:
			model.visible = false

		var scale: float = float(moby.get_meta("rc1_skin_class_scale", 1.0))
		look.name = "SkinLook"
		look.transform = Transform3D(Basis.from_scale(Vector3(scale, scale, scale)), Vector3.ZERO)
		moby.add_child(look)


## The level the edited scene is (library.gd here_of()).
func here() -> Dictionary:
	var root: Node = EditorInterface.get_edited_scene_root()
	return Library.here_of(root.scene_file_path) if root != null else {}


## Brings ASSET in for a drag: the res:// file Godot's drop will place, or empty (the dock shows
## why).
func prepare(asset: Dictionary) -> String:
	var error: Array = []
	dock.importing = true
	var path: String = Library.prepare(asset, here(), error)
	dock.importing = false

	if path.is_empty():
		report(" ".join(error), true)
		return ""

	pending[path] = asset
	return path


# =============================================================================
# PLACING
# =============================================================================

## Adds ASSET in front of the 3D view's camera, as an undoable edit.
func add_in_view(asset: Dictionary, place: Variant = null, job: String = "") -> void:
	active_job = job
	last_added = null
	var root: Node = EditorInterface.get_edited_scene_root()
	var game: Node3D = root.get_node_or_null("Game") as Node3D if root != null else null

	if game == null:
		report("Open a level first.", true)
		return

	var path: String = prepare(asset)

	if path.is_empty():
		return

	if not ready_to_place(path, add_in_view.bind(asset, place, job)):
		return

	var scene := ResourceLoader.load(path) as PackedScene

	if scene == null:
		report("Couldn't load %s." % path, true)
		return

	var node := scene.instantiate() as Node3D
	var camera: Camera3D = EditorInterface.get_editor_viewport_3d(0).get_camera_3d()
	var at: Vector3 = camera.global_position - camera.global_basis.z * ADD_DISTANCE if camera != null else game.global_position
	var group: Node3D = group_for(game, asset)
	var make_group: bool = group.get_parent() == null
	node.name = free_name(group, base_name(asset))
	var undo_redo := get_undo_redo()
	undo_redo.create_action("Add %s from the Assets dock" % asset["name"], UndoRedo.MERGE_DISABLE, root)

	if make_group:
		undo_redo.add_do_method(game, "add_child", group)
		undo_redo.add_do_property(group, "owner", root)
		undo_redo.add_do_reference(group)

	undo_redo.add_do_method(group, "add_child", node)
	undo_redo.add_do_property(node, "owner", root)
	undo_redo.add_do_property(node, "global_transform", placement(game, asset, node, camera, at, place))
	add_metadata(undo_redo, node, asset, path)
	undo_redo.add_do_reference(node)
	undo_redo.add_undo_method(group, "remove_child", node)

	if make_group:
		undo_redo.add_undo_method(game, "remove_child", group)

	placing = true
	undo_redo.commit_action()
	placing = false
	pending.erase(path)
	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(node)
	last_added = node
	report(placed_report(asset, node))


## Adds ASSET's model, as it is (animated), on the logic of HOST_CLASS, a moby class this level
## has: a moby of that class under Game/Mobys, which the game draws as ASSET's model (a skin:
## editor/skins.py packs it, the port draws it). Undoable.
func add_as(asset: Dictionary, host_class: int, place: Variant = null, job: String = "") -> void:
	active_job = job
	last_added = null
	var root: Node = EditorInterface.get_edited_scene_root()
	var game: Node3D = root.get_node_or_null("Game") as Node3D if root != null else null
	var level: Dictionary = here()

	if game == null or level.is_empty():
		report("Open a level first.", true)
		return

	var host_path: String = String(level["classes"]["mobys"].get(host_class, ""))
	var error: Array = []
	dock.importing = true
	var skin_path: String = Library.prepare_skin(asset, error)
	dock.importing = false

	if host_path.is_empty() or skin_path.is_empty():
		report(" ".join(error) if not error.is_empty() else "This level has no type %d." % host_class, true)
		return

	var host_scene := ResourceLoader.load(host_path) as PackedScene

	if host_scene == null:
		report("Couldn't load %s." % host_path, true)
		return

	var host: Dictionary = {"name": "Class %d" % host_class, "scale": 1.0}
	Library.read_class_scene(host, FileAccess.get_file_as_string(host_path))
	var node := host_scene.instantiate() as Node3D
	var scale: float = float(asset["scale"])
	var camera: Camera3D = EditorInterface.get_editor_viewport_3d(0).get_camera_3d()
	var at: Vector3 = camera.global_position - camera.global_basis.z * ADD_DISTANCE if camera != null else game.global_position
	var group: Node3D = game.get_node_or_null(GROUPS["mobys"]) as Node3D
	var make_group: bool = group == null

	if make_group:
		group = Node3D.new()
		group.name = GROUPS["mobys"]

	var label: String = "%s_as_%s" % [String(host["name"]).capitalize().replace(" ", ""),
		String(asset["name"]).capitalize().replace(" ", "")]
	node.name = free_name(group, label.validate_node_name())
	var undo_redo := get_undo_redo()
	undo_redo.create_action("Add %s acting as %s" % [asset["name"], host["name"]], UndoRedo.MERGE_DISABLE, root)

	if make_group:
		undo_redo.add_do_method(game, "add_child", group)
		undo_redo.add_do_property(group, "owner", root)
		undo_redo.add_do_reference(group)

	undo_redo.add_do_method(group, "add_child", node)
	undo_redo.add_do_property(node, "owner", root)
	undo_redo.add_do_property(node, "global_transform", placement(game, asset, node, camera, at, place))
	undo_redo.add_do_method(node, "set_meta", "rc1_skin", skin_path)
	undo_redo.add_do_method(node, "set_meta", "rc1_skin_asset", "%s/%s/level_%02d/%s/%d" % [asset["game"],
		asset["disc"], int(asset["level"]), asset["kind"], int(asset["class"])])
	# The game scales the moby by its own class's scale: the skin's is that times this.
	undo_redo.add_do_method(node, "set_meta", "rc1_skin_scale", scale / maxf(float(host["scale"]), 0.0001))
	undo_redo.add_do_method(node, "set_meta", "rc1_skin_class_scale", scale)
	undo_redo.add_do_method(node, "set_meta", "rc1_skin_animations", [])
	undo_redo.add_do_method(self, "show_skins", root)
	undo_redo.add_do_reference(node)
	undo_redo.add_undo_method(group, "remove_child", node)

	if make_group:
		undo_redo.add_undo_method(game, "remove_child", group)

	placing = true
	undo_redo.commit_action()
	placing = false
	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(node)
	last_added = node
	report(("Added %s: a %s of this level that looks like %s from %s. "
		+ "Set its animations in Properties, then Play Level.") % [node.name,
		host["name"], asset["name"], asset["level_name"] if not String(asset["level_name"]).is_empty() else "its level"])


## Adds ASSET, a moby of another level of this disc, with its own class and logic: a moby of its
## class under Game/Mobys with rc1_foreign_level, which editor/foreign_classes.py packs and the
## port runs in its own level's program. Undoable.
func add_foreign(asset: Dictionary, place: Variant = null, job: String = "") -> void:
	active_job = job
	last_added = null
	var root: Node = EditorInterface.get_edited_scene_root()
	var game: Node3D = root.get_node_or_null("Game") as Node3D if root != null else null

	if game == null:
		report("Open a level first.", true)
		return

	var refusal: String = Library.import_refusal(asset, here())

	if not refusal.is_empty():
		report(refusal, true)
		return

	var error: Array = []
	dock.importing = true
	var path: String = Library.prepare(asset, here(), error)
	dock.importing = false

	if not path.is_empty() and not ready_to_place(path, add_foreign.bind(asset, place, job)):
		return

	var scene := ResourceLoader.load(path) as PackedScene if not path.is_empty() else null

	if scene == null:
		report(" ".join(error) if not error.is_empty() else "Couldn't load %s." % path, true)
		return

	var node := scene.instantiate() as Node3D
	var camera: Camera3D = EditorInterface.get_editor_viewport_3d(0).get_camera_3d()
	var at: Vector3 = camera.global_position - camera.global_basis.z * ADD_DISTANCE if camera != null else game.global_position
	var group: Node3D = game.get_node_or_null(GROUPS["mobys"]) as Node3D
	var make_group: bool = group == null

	if make_group:
		group = Node3D.new()
		group.name = GROUPS["mobys"]

	node.name = free_name(group, base_name(asset))
	var undo_redo := get_undo_redo()
	undo_redo.create_action("Add %s with its actions" % asset["name"], UndoRedo.MERGE_DISABLE, root)

	if make_group:
		undo_redo.add_do_method(game, "add_child", group)
		undo_redo.add_do_property(group, "owner", root)
		undo_redo.add_do_reference(group)

	undo_redo.add_do_method(group, "add_child", node)
	undo_redo.add_do_property(node, "owner", root)
	undo_redo.add_do_property(node, "global_transform", placement(game, asset, node, camera, at, place))
	# Drawn at its class's size, as in the game: the packed moby's own scale is this over that.
	var scale: float = float(asset["scale"])
	undo_redo.add_do_property(node, "scale", Vector3(scale, scale, scale))
	undo_redo.add_do_method(node, "set_meta", "rc1_foreign_level", int(asset["level"]))
	undo_redo.add_do_method(node, "set_meta", "rc1_foreign_class_scale", scale)
	undo_redo.add_do_reference(node)
	undo_redo.add_undo_method(group, "remove_child", node)

	if make_group:
		undo_redo.add_undo_method(game, "remove_child", group)

	placing = true
	undo_redo.commit_action()
	placing = false
	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(node)
	last_added = node
	report("Added %s with its actions from %s. Then Play Level." % [
		node.name, asset["level_name"] if not String(asset["level_name"]).is_empty() else "level %02d" % int(asset["level"])])


## The group ASSET goes in under GAME; a new, parentless Node3D for a static group not made yet.
func group_for(game: Node3D, asset: Dictionary) -> Node3D:
	var full: bool = Library.status(asset, here()) == Library.FULL
	var name: String = GROUPS[asset["kind"]] if full else STATIC_GROUP
	var group: Node3D = game.get_node_or_null(name) as Node3D

	if group == null:
		group = Node3D.new()
		group.name = name

	return group


## A node name of the asset: Moby_0012-like for the game's own (pack.py reads the class from the
## scene, not the name), else its name in the library ("Sentrybot_rac1_pal_L03").
func base_name(asset: Dictionary) -> String:
	var name: String = String(asset["name"]).capitalize().replace(" ", "").validate_node_name()

	if Library.status(asset, here()) == Library.FULL:
		return name

	return "%s_%s_%s_L%02d" % [name, asset["game"], asset["disc"], int(asset["level"])]


func free_name(group: Node, base: String) -> String:
	var name: String = base
	var suffix: int = 2

	while group.has_node(NodePath(name)):
		name = "%s_%d" % [base, suffix]
		suffix += 1

	return name


## What a static asset carries: its source, the class scale and whether it is solid.
func add_metadata(undo_redo: EditorUndoRedoManager, node: Node3D, asset: Dictionary, path: String) -> void:
	if not path.begins_with(Library.LIBRARY_DIR + "/"):
		return

	var scale: float = float(asset["scale"])
	undo_redo.add_do_property(node, "scale", Vector3(scale, scale, scale))
	undo_redo.add_do_method(node, "set_meta", "rc1_static_collision", asset["kind"] != "shrubs")
	undo_redo.add_do_method(node, "set_meta", "rc1_asset", "%s/%s/level_%02d/%s/%d" % [asset["game"], asset["disc"],
		int(asset["level"]), asset["kind"], int(asset["class"])])


func placed_report(asset: Dictionary, node: Node) -> String:
	if Library.status(asset, here()) == Library.FULL:
		return "Added %s, a %s of this level. Move it, then Play Level." % [node.name, asset["kind"].trim_suffix("s")]

	return ("Added %s as scenery from %s. "
		+ "Collision is %s.") % [node.name,
		asset["level_name"] if not String(asset["level_name"]).is_empty() else "its level",
		"on" if asset["kind"] != "shrubs" else "off"]


# =============================================================================
# GODOT'S DROP
# =============================================================================

## The EditorInterface meta the editor's 3D view reads while one of the dock's assets is dragged in: the
## model's axes in the world, so the drop stands up as in the game, its front to the camera.
const DROP_BASIS: StringName = &"openrac_drop_basis"
## And the model's front in its own axes (Library.front_for()).
const DROP_FRONT: StringName = &"openrac_drop_front"


## The game's models are z-up with their front on x, under Game's turn to Godot's y-up: a drop
## stands up as in the game, facing the camera (Godot's own drop would lie it on its side).
func turn_drops_upright(front: Vector3 = Vector3(1, 0, 0)) -> void:
	var root: Node = EditorInterface.get_edited_scene_root()
	var game: Node3D = root.get_node_or_null("Game") as Node3D if root != null else null

	if game != null:
		EditorInterface.set_meta(DROP_BASIS, game.global_basis.orthonormalized())
		EditorInterface.set_meta(DROP_FRONT, front)


func stop_turning_drops() -> void:
	for meta in [DROP_BASIS, DROP_FRONT]:
		if EditorInterface.has_meta(meta):
			EditorInterface.remove_meta(meta)


## The world basis of a model at AT under Game (whose world basis is GAME), standing up, turned
## about the game's up so its SIDE (in its own axes: Library.front_for()) faces a camera at EYE.
static func facing(game: Basis, at: Vector3, eye: Vector3, side: Vector3 = Vector3(1, 0, 0)) -> Basis:
	var model: Basis = game.orthonormalized()
	var world_front: Vector3 = model * side
	var front: Vector3 = Vector3(world_front.x, 0, world_front.z)
	var to_eye: Vector3 = Vector3(eye.x - at.x, 0, eye.z - at.z)

	if front.is_zero_approx() or to_eye.is_zero_approx():
		return model

	return Basis(Vector3.UP, front.normalized().signed_angle_to(to_eye.normalized(), Vector3.UP)) * model

## Where Add puts ASSET: ADD_DISTANCE in front of the 3D view's camera (or AT), standing, its
## front to the camera, at SIZE; at Game's origin when there is no camera.
func in_view(game: Node3D, asset: Dictionary, size: Vector3 = Vector3.ONE, at: Variant = null) -> Transform3D:
	var camera: Camera3D = EditorInterface.get_editor_viewport_3d(0).get_camera_3d()

	if camera == null:
		return Transform3D(game.global_basis.orthonormalized().scaled_local(size), game.global_position if at == null else at)

	if at == null:
		at = camera.global_position - camera.global_basis.z * ADD_DISTANCE

	return Transform3D(facing(game.global_basis, at, camera.global_position, Library.front_for(asset)).scaled_local(size), at)


## A node of a file the dock prepared: put it where it belongs once the drop has finished.
func handle_node_added(node: Node) -> void:
	if placing or pending.is_empty() or not node.scene_file_path in pending or settling.has(node.get_instance_id()):
		return

	var root: Node = EditorInterface.get_edited_scene_root()

	if root == null or not root.is_ancestor_of(node):
		return

	# Only a new edit (Godot's drop): an undo or a redo puts back what was.
	var history: UndoRedo = get_undo_redo().get_history_undo_redo(get_undo_redo().get_object_history_id(root))

	if not history.is_committing_action():
		return

	arrived.append(node)

	if arrived.size() == 1:
		settle.call_deferred()


## Whether NODE, added while the edited scene took an edit, is a drop settle() has work on: not
## in its group yet, at its parent's origin, lying or without its metadata. A copy of a placed asset
## (duplicate, paste) is left as Godot made it.
func needs_settling(node: Node, game: Node3D) -> bool:
	if not is_instance_valid(node) or not node is Node3D or not node.is_inside_tree() or node.has_meta("rc1_index"):
		return false

	var asset: Dictionary = pending.get(node.scene_file_path, {})

	if asset.is_empty():
		return false

	var full: bool = Library.status(asset, here()) == Library.FULL
	var group: Node = game.get_node_or_null(NodePath(GROUPS[asset["kind"]] if full else STATIC_GROUP))
	return node.get_parent() != group or (node as Node3D).position.is_zero_approx() \
		or stood_up(game.global_basis, (node as Node3D).global_basis) != null \
		or (node.scene_file_path.begins_with(Library.LIBRARY_DIR + "/") and not node.has_meta("rc1_asset"))


## Moves each dropped node to its group under Game, keeping where it was dropped, as one undoable
## edit after Godot's own.
func settle() -> void:
	var nodes: Array[Node] = arrived.duplicate()
	arrived.clear()
	var root: Node = EditorInterface.get_edited_scene_root()
	var game: Node3D = root.get_node_or_null("Game") as Node3D if root != null else null

	if game == null:
		return

	nodes = nodes.filter(func(node: Node) -> bool: return needs_settling(node, game))

	if nodes.is_empty():
		return

	var undo_redo := get_undo_redo()
	undo_redo.create_action("Place assets from the Assets dock", UndoRedo.MERGE_DISABLE, root)
	var made: Dictionary = {}
	var moved: Array[Node] = []

	for node in nodes:
		var asset: Dictionary = pending.get(node.scene_file_path, {})

		var group: Node3D = made.get(GROUPS.get(asset["kind"], "") if Library.status(asset, here()) == Library.FULL
			else STATIC_GROUP, null)

		if group == null:
			group = group_for(game, asset)

			if group.get_parent() == null:
				undo_redo.add_do_method(game, "add_child", group)
				undo_redo.add_do_property(group, "owner", root)
				undo_redo.add_do_reference(group)
				undo_redo.add_undo_method(game, "remove_child", group)

			made[String(group.name)] = group

		var old_parent: Node = node.get_parent()
		var was: Transform3D = (node as Node3D).global_transform
		var at: Transform3D = was

		# Dropped on the Scene dock, not the 3D view: Godot leaves it at its parent's origin (far off,
		# and lying when that parent is not under Game). Put in front of the camera, standing.
		if (node as Node3D).position.is_zero_approx():
			at = in_view(game, asset, at.basis.get_scale())
		elif stood_up(game.global_basis, at.basis) != null:
			# A Godot without the fork's drop turn lays it on its side: stand it where it was dropped.
			at = Transform3D(in_view(game, asset, at.basis.get_scale(), at.origin).basis, at.origin)

		if old_parent != group:
			var name: String = free_name(group, base_name(asset))
			# Renamed only inside the tree: the editor follows a rename by the node's path.
			undo_redo.add_do_method(old_parent, "remove_child", node)
			undo_redo.add_do_method(group, "add_child", node)
			undo_redo.add_do_property(node, "name", name)
			undo_redo.add_do_property(node, "owner", root)
			undo_redo.add_do_property(node, "global_transform", at)
			undo_redo.add_undo_method(group, "remove_child", node)
			undo_redo.add_undo_method(old_parent, "add_child", node)
			undo_redo.add_undo_property(node, "name", node.name)
			undo_redo.add_undo_property(node, "owner", root)
			undo_redo.add_undo_property(node, "global_transform", was)
		else:
			if not String(node.name).begins_with(base_name(asset)):
				# Dropped next to a selected asset, so already in its group: only its name is Godot's.
				undo_redo.add_do_property(node, "name", free_name(group, base_name(asset)))
				undo_redo.add_undo_property(node, "name", node.name)

			if at != was:
				undo_redo.add_do_property(node, "global_transform", at)
				undo_redo.add_undo_property(node, "global_transform", was)

		add_metadata(undo_redo, node, asset, node.scene_file_path)
		moved.append(node)
		settling[node.get_instance_id()] = true

	undo_redo.commit_action()
	settling.clear()

	if not moved.is_empty():
		var asset: Dictionary = pending.get(moved[0].scene_file_path, {})
		report(placed_report(asset, moved[0]))
