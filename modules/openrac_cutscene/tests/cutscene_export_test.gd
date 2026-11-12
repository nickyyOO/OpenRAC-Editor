# The cutscene nodes' export (docs/CUTSCENE_FORMAT.md), run by run_tests.sh in the editor without a
# window: a level with the game's frame, a cutscene with a timeline, a camera, actors, lines,
# sounds and events, and what each becomes in the file.
extends SceneTree

var failures := 0
# The full cutscene's export and file, for the round trip.
var full_export: Dictionary
var full_path: String


func check(ok: bool, what: String) -> void:
	if not ok:
		failures += 1
		printerr("FAILED: ", what)


func near(a: float, b: float, eps := 0.001) -> bool:
	return absf(a - b) <= eps


func near3(a: Array, b: Vector3, eps := 0.001) -> bool:
	return a.size() == 3 and near(a[0], b.x, eps) and near(a[1], b.y, eps) and near(a[2], b.z, eps)


# Whether two exports are the same, numbers to `eps` (a file read back has floats for ints).
func same(a, b, eps := 0.002) -> bool:
	if (a is int or a is float) and (b is int or b is float):
		return absf(float(a) - float(b)) <= eps
	if a is Dictionary and b is Dictionary:
		if a.size() != b.size():
			return false
		for k in a:
			if not b.has(k) or not same(a[k], b[k], eps):
				return false
		return true
	if (a is Array or a is PackedStringArray) and (b is Array or b is PackedStringArray):
		if a.size() != b.size():
			return false
		for i in a.size():
			if not same(a[i], b[i], eps):
				return false
		return true
	return a == b


func write(path: String, text: String) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(text)
	f.close()


# A level as the extractor writes it: the Game node turns the game's axes (z up) into Godot's.
func make_level(dir: String, level_json: Dictionary) -> Node3D:
	DirAccess.make_dir_recursive_absolute(dir)
	var f := FileAccess.open(dir.path_join("level.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(level_json))
	f.close()
	var root := Node3D.new()
	root.name = "Level"
	root.scene_file_path = dir.path_join("level.tscn")
	var game := Node3D.new()
	game.name = "Game"
	game.transform = Transform3D(Basis(Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0)), Vector3())
	root.add_child(game)
	game.owner = root
	get_root().add_child(root)
	return root


func add(parent: Node, node: Node, owner: Node) -> Node:
	parent.add_child(node)
	node.owner = owner
	return node


func timeline(cutscene: Node, length: float) -> Animation:
	var player := AnimationPlayer.new()
	player.name = "Timeline"
	add(cutscene, player, cutscene.owner)
	var lib := AnimationLibrary.new()
	var anim := Animation.new()
	anim.length = length
	lib.add_animation(cutscene.get_cutscene_id(), anim)
	player.add_animation_library("", lib)
	return anim


func test_full_cutscene() -> void:
	var root := make_level("user://t/level_07", {"level": 7, "version": "pal"})
	var game := root.get_node("Game")
	var cs = add(game, Cutscene.new(), root)
	cs.name = "Meet Vendor"
	var anim := timeline(cs, 2.0)

	# The camera: from (0, 0, 10) looking along +x to (6, 0, 10) looking along +y, a cut at 1 s.
	var cam = add(cs, CutsceneCamera.new(), root)
	cam.name = "Camera"
	cam.cuts = PackedFloat32Array([1.0])
	var t := anim.add_track(Animation.TYPE_POSITION_3D)
	anim.track_set_path(t, "Camera")
	anim.position_track_insert_key(t, 0.0, Vector3(0, 0, 10))
	anim.position_track_insert_key(t, 2.0, Vector3(6, 0, 10))
	var r := anim.add_track(Animation.TYPE_ROTATION_3D)
	anim.track_set_path(r, "Camera")
	anim.rotation_track_insert_key(r, 0.0, Basis.looking_at(Vector3(1, 0, 0), Vector3(0, 0, 1)).get_rotation_quaternion())
	anim.rotation_track_insert_key(r, 2.0, Basis.looking_at(Vector3(0, 1, 0), Vector3(0, 0, 1)).get_rotation_quaternion())

	# An actor of the level, turning, with two animations and hidden for a while.
	var vendor = add(cs, CutsceneActor.new(), root)
	vendor.name = "Vendor"
	vendor.spawn_id = 149
	vendor.moby_class = 11
	vendor.end = CutsceneActor.END_RESTORE
	vendor.position = Vector3(260, 147, 50)
	var s := anim.add_track(Animation.TYPE_VALUE)
	anim.track_set_path(s, "Vendor:sequence")
	anim.track_insert_key(s, 0.0, 3)
	anim.track_insert_key(s, 1.0, 5)
	var sp := anim.add_track(Animation.TYPE_VALUE)
	anim.track_set_path(sp, "Vendor:anim_speed")
	anim.track_insert_key(sp, 0.0, 1.0)
	anim.track_insert_key(sp, 1.0, 0.5)
	anim.value_track_set_update_mode(sp, Animation.UPDATE_DISCRETE)
	var v := anim.add_track(Animation.TYPE_VALUE)
	anim.track_set_path(v, "Vendor:visible")
	anim.track_insert_key(v, 0.5, false)
	anim.track_insert_key(v, 1.5, true)

	# A second actor with the same id is left out; a figure of the hero.
	var twin = add(cs, CutsceneActor.new(), root)
	twin.name = "Twin"
	twin.actor_id = "vendor"
	twin.spawn_id = 150
	var figure = add(cs, CutsceneActor.new(), root)
	figure.name = "HeroDouble"
	figure.source = CutsceneActor.SOURCE_HERO_FIGURE
	figure.scale = Vector3(2, 2, 2)
	figure.visible = false

	var line = add(cs, CutsceneSubtitle.new(), root)
	line.name = "Line1"
	line.start = 0.5
	line.duration = 1.0
	line.text = {"en": " Need a weapon? ", "fr": "", "de": "Brauchst du eine Waffe?"}
	line.speaker = NodePath("../Vendor")
	var empty_line = add(cs, CutsceneSubtitle.new(), root)
	empty_line.name = "Empty"
	empty_line.text = {"en": "  "}

	var sound = add(cs, CutsceneSound.new(), root)
	sound.name = "Jump"
	sound.start = 0.25
	sound.kind = CutsceneSound.KIND_SFX
	sound.game_sound = 26
	var music = add(cs, CutsceneSound.new(), root)
	music.name = "Music"
	music.kind = CutsceneSound.KIND_MUSIC
	music.file = "audio/theme.ogg"
	music.fade_in = 1.0
	music.stop = 2.0
	var silent = add(cs, CutsceneSound.new(), root)
	silent.name = "Silent"

	var flag = add(cs, CutsceneEvent.new(), root)
	flag.name = "Flag"
	flag.start = 1.0
	flag.action = CutsceneEvent.ACTION_MISSION_FLAG
	flag.flag_byte = 4
	var show = add(cs, CutsceneEvent.new(), root)
	show.name = "Show"
	show.action = CutsceneEvent.ACTION_MOBY_SHOW
	show.spawn_id = 210
	show.persistent = false

	var end := Node3D.new()
	end.name = "HeroEnd"
	add(cs, end, root)
	end.position = Vector3(1, 2, 3)
	end.rotation = Vector3(0, 0, PI / 2)

	var d: Dictionary = cs.to_cutscene()
	check(d["format"] == "openrac-cutscene" and d["version"] == 1, "format and version")
	check(d["level"] == 7, "level from level.json")
	check(d["name"] == "Meet Vendor", "name is the node's")
	check(cs.get_cutscene_id() == "meet_vendor", "id is the name in snake_case")
	check(d["length"] == 120, "length from the timeline: 2 s = 120 ticks, got %s" % d["length"])
	check(Array(d["languages"]) == ["en", "fr", "de", "es", "it"], "a PAL level's five languages")

	var keys: Array = d["camera"]["keys"]
	var ticks := keys.map(func(k): return k["t"])
	check(ticks[0] == 0 and ticks[-1] == 120, "camera keys from 0 to the end: %s" % [ticks])
	check(ticks.has(59) and ticks.has(60), "a key before the cut and at it")
	for k in keys:
		check(k["ease"] == "linear", "sampled keys are linear")
		check(k.has("cut") == (k["t"] == 60), "only the cut's key cuts (t %d)" % k["t"])
	check(near3(keys[0]["eye"], Vector3(0, 0, 10)), "first eye in game space: %s" % [keys[0]["eye"]])
	check(near3(keys[0]["rotation"], Vector3(0, 0, 0)), "looking along +x: no roll, pitch or yaw: %s" % [keys[0]["rotation"]])
	check(near3(keys[-1]["eye"], Vector3(6, 0, 10)), "last eye")
	check(near(keys[-1]["rotation"][2], PI / 2), "looking along +y: yaw 90 degrees: %s" % [keys[-1]["rotation"]])
	check(d["camera"]["end"] == "ease", "the camera eases back by default")

	var actors: Array = d["actors"]
	check(actors.size() == 2, "the twin with the same id is left out: %d" % actors.size())
	var a: Dictionary = actors[0]
	check(a["id"] == "vendor" and a["source"] == {"spawn_id": 149, "class": 11}, "level moby source: %s" % [a["source"]])
	check(a["missing"] == "spawn" and a["end"] == "restore", "missing and end")
	check(a["track"].size() == 1 and near3(a["track"][0]["position"], Vector3(260, 147, 50)), "an actor not moved has one key at its place")
	check(near(a["track"][0]["scale"], 1.0), "scale 1 is its own size")
	check(a["anims"].size() == 2 and a["anims"][0]["t"] == 0 and a["anims"][0]["seq"] == 3 and a["anims"][1]["t"] == 60 and a["anims"][1]["seq"] == 5, "anims from the sequence keys: %s" % [a["anims"]])
	check(near(a["anims"][1]["speed"], 0.5), "speed at the key's time")
	check(a["visible"] == [{"t": 30, "on": false}, {"t": 90, "on": true}], "visible keys: %s" % [a["visible"]])
	var fig: Dictionary = actors[1]
	check(fig["source"] == {"figure": "hero"}, "a hero figure")
	check(near(fig["track"][0]["scale"], 2.0), "the figure's scale")
	check(fig["visible"] == [{"t": 0, "on": false}], "a hidden node starts hidden")
	check(not fig.has("anims"), "no animation asked: none written")

	var subs: Array = d["subtitles"]
	check(subs.size() == 1, "an empty line is left out")
	check(subs[0]["t"] == 30 and subs[0]["end"] == 90, "line times")
	check(subs[0]["text"] == {"en": "Need a weapon?", "de": "Brauchst du eine Waffe?"}, "empty languages dropped, spaces trimmed: %s" % [subs[0]["text"]])
	check(subs[0]["actor"] == "vendor", "the speaker's id")

	var sounds: Array = d["sounds"]
	check(sounds.size() == 2, "a sound with no file is left out")
	check(sounds[0]["game_sound"] == 26 and sounds[0]["kind"] == "sfx" and sounds[0]["t"] == 15, "game sound")
	check(sounds[1]["file"] == "audio/theme.ogg" and sounds[1]["fade_in"] == 60 and sounds[1]["end"] == 120, "music: %s" % [sounds[1]])

	var events: Array = d["events"]
	check(events[0] == {"t": 60, "do": "mission_flag", "level": 7, "byte": 4, "value": 1, "persistent": true}, "a flag of this level: %s" % [events[0]])
	check(events[1] == {"t": 0, "do": "moby_hide", "spawn_id": 210, "on": false}, "show a moby: %s" % [events[1]])

	check(near3(d["hero"]["end"]["position"], Vector3(1, 2, 3)) and near(d["hero"]["end"]["yaw"], PI / 2), "the hero's end: %s" % [d["hero"]])

	# Written beside the level, read back the same.
	var path: String = cs.export_cutscene()
	check(path == "user://t/level_07/cutscenes/meet_vendor.cutscene", "written to %s" % path)
	var back = JSON.parse_string(FileAccess.get_file_as_string(path))
	check(back is Dictionary and same(back, d), "read back the same")
	full_export = d
	full_path = path
	root.queue_free()


func test_triggers_and_defaults() -> void:
	var root := make_level("user://t/level_03", {"level": 3, "version": "ntsc"})
	var game := root.get_node("Game")
	var cs = add(game, Cutscene.new(), root)
	cs.name = "Area"
	cs.position = Vector3(10, 20, 30) # game axes, under Game
	cs.trigger_radius = 2.5
	var d: Dictionary = cs.to_cutscene()
	check(Array(d["languages"]) == ["en"], "an NTSC-U level: English")
	check(d["length"] == 1, "no timeline and no length: one tick")
	check(d["trigger"]["kind"] == "volume" and near3(d["trigger"]["sphere"]["position"], Vector3(10, 20, 30)) and near(d["trigger"]["sphere"]["radius"], 2.5), "area as a sphere at the cutscene: %s" % [d["trigger"]])
	cs.trigger_box = Vector3(4, 2, -6)
	d = cs.to_cutscene()
	check(near3(d["trigger"]["box"]["min"], Vector3(8, 19, 27)) and near3(d["trigger"]["box"]["max"], Vector3(12, 21, 33)), "area as a box (sizes made positive): %s" % [d["trigger"]])
	cs.trigger_kind = Cutscene.TRIGGER_TALK
	cs.trigger_spawn_id = 149
	d = cs.to_cutscene()
	check(d["trigger"]["kind"] == "talk" and d["trigger"]["spawn_id"] == 149 and not d["trigger"].has("box"), "talk")
	cs.trigger_kind = Cutscene.TRIGGER_FLAG
	cs.trigger_flag = Vector3i(-1, 2, 1)
	d = cs.to_cutscene()
	check(d["trigger"]["flag"] == {"level": 3, "byte": 2, "is": 1}, "a flag of this level: %s" % [d["trigger"]])
	cs.trigger_kind = Cutscene.TRIGGER_LEVEL_START
	cs.trigger_flag_enabled = false
	d = cs.to_cutscene()
	check(not d["trigger"].has("flag"), "no flag unless asked")
	cs.languages = PackedStringArray(["ja"])
	cs.length = 1.5
	cs.title = "A title"
	cs.file_name = " my/scene "
	d = cs.to_cutscene()
	check(Array(d["languages"]) == ["ja"] and d["length"] == 90 and d["name"] == "A title", "set languages, length and title")
	check(not cs.get_cutscene_id().contains("/"), "the file name is made safe: %s" % cs.get_cutscene_id())
	check(d["camera"] == {}, "no camera: the game's stays")
	check(d["options"] == {"skippable": true, "letterbox": true, "hide_hud": true, "world": "run", "music": "duck"}, "default options")
	# Set Up: a timeline as long as the cutscene and a camera behind it, a little above, level.
	cs.get_set_up_action().call()
	check(cs.get_timeline() != null and near(cs.get_timeline_animation().length, 1.5), "a timeline of the cutscene's length")
	d = cs.to_cutscene()
	var key: Dictionary = d["camera"]["keys"][0]
	check(near3(key["eye"], Vector3(4, 20, 32)), "the camera 6 behind and 2 above: %s" % [key["eye"]])
	check(near3(key["rotation"], Vector3(0, atan2(2, 6), 0)), "looking at the cutscene, no roll: %s" % [key["rotation"]])
	cs.get_set_up_action().call()
	check(cs.get_child_count() == 2, "set up twice adds nothing: %d" % cs.get_child_count())
	root.queue_free()


func test_no_game_node() -> void:
	# A cutscene in a scene without the Game node: game (x, y, z) = Godot (x, z, -y).
	var root := Node3D.new()
	root.scene_file_path = "user://t/loose/loose.tscn"
	get_root().add_child(root)
	var cs = add(root, Cutscene.new(), root)
	cs.position = Vector3(1, 2, 3)
	var d: Dictionary = cs.to_cutscene()
	check(near3(d["trigger"]["sphere"]["position"], Vector3(1, -3, 2)), "Godot (1, 2, 3) is game (1, -3, 2): %s" % [d["trigger"]])
	check(d["level"] == -1, "no level known")
	root.queue_free()


# Export, import into a new cutscene, export again: the same file.
func test_round_trip() -> void:
	var root := make_level("user://t/level_07b", {"level": 7, "version": "pal"})
	var cs = add(root.get_node("Game"), Cutscene.new(), root)
	cs.name = "Copy"
	check(cs.import_cutscene(full_path) == OK, "import the full cutscene")
	check(cs.get_cutscene_id() == "meet_vendor", "the file's name is kept: %s" % cs.get_cutscene_id())
	var d: Dictionary = cs.to_cutscene()
	for part in full_export:
		check(same(d[part], full_export[part]), "the round trip keeps %s:\n  %s\n  %s" % [part, d[part], full_export[part]])
	check(cs.import_cutscene(full_path) == ERR_ALREADY_IN_USE, "no import into a cutscene with nodes")
	root.queue_free()


# A file written by hand: what is kept, what is said not to be.
func test_import_by_hand() -> void:
	var root := make_level("user://t/level_05", {"level": 5, "version": "ntsc"})
	var cs = add(root.get_node("Game"), Cutscene.new(), root)
	cs.name = "Hand"
	var problems: PackedStringArray = cs.from_cutscene({
		"format": "openrac-cutscene", "version": 1, "level": 5, "name": "By hand", "length": 300,
		"trigger": {"kind": "volume", "once": false, "box": {"min": [14, 10, 0], "max": [10, 12, 2]}, "flag": {"level": 5, "byte": 1, "is": 2}},
		"options": {"skippable": false, "world": "hold", "music": "stop"},
		"camera": {"end": "cut", "shake": [{"t": 0}], "keys": [
			{"t": 0, "eye": [0, 0, 5], "look_at": [10, 0, 5], "ease": "linear"},
			{"t": 100, "eye": [0, 0, 5], "look_at": {"actor": "hero"}, "ease": "linear"},
			{"t": 200, "eye": [5, 5, 5], "rotation": [0, 0, 1.5707963], "cut": true, "fov": 40}]},
		"actors": [{"id": "crate", "source": {"prop": "meshes/crate.glb"}, "end": "remove", "path": {"points": []}}, "not an actor"],
		"subtitles": [{"t": 10, "text": "Plain text", "actor": "crate"}],
		"sounds": [{"t": 3, "file": {"en": "en.ogg", "fr": "fr.ogg"}, "actor": "nobody"}],
		"events": [{"t": 0, "do": "fade", "to": [1, 1, 1, 0.5], "length": 20}, {"t": 5, "do": "dance"}, {"t": 9, "do": "travel", "level": 5}],
		"hero": {"mode": "hidden"},
		"variants": [{"if": {"byte": 3}, "play": "other"}, 7],
	})
	var said := "\n".join(problems)
	for what in ["shake", "\"path\"", "looking at an actor", "\"dance\""]:
		check(said.contains(what), "says %s is not kept: %s" % [what, said])
	check(cs.position.is_equal_approx(Vector3(12, 11, 1)), "placed at the box's centre: %s" % cs.position)
	check(cs.trigger_box.is_equal_approx(Vector3(4, 2, 2)) and not cs.trigger_once, "the box, min and max either way: %s" % cs.trigger_box)
	check(cs.trigger_flag == Vector3i(-1, 1, 2) and cs.trigger_flag_enabled, "the flag of this level: %s" % cs.trigger_flag)
	check(cs.title == "By hand" and near(cs.length, 5.0) and cs.hero_mode == Cutscene.HERO_HIDDEN, "title, length, hero")
	check(not cs.skippable and cs.world == Cutscene.WORLD_HOLD and cs.music == Cutscene.MUSIC_STOP, "options")
	check(cs.variants.size() == 1, "a variant that is not one is dropped")
	var cam = cs.get_node("Camera")
	check(cam.cuts.size() == 1 and near(cam.cuts[0], 200 / 60.0) and cam.export_fov and not cam.ease_back, "cuts, fov, no way back")

	var d: Dictionary = cs.to_cutscene()
	check(d["trigger"]["kind"] == "volume" and near3(d["trigger"]["box"]["min"], Vector3(10, 10, 0)), "the box again: %s" % [d["trigger"]])
	var keys: Array = d["camera"]["keys"]
	var at := {}
	for k in keys:
		at[int(k["t"])] = k
	check(near3(at[0]["rotation"], Vector3(0, 0, 0)), "looking at a point along +x: %s" % [at[0]["rotation"]])
	check(near3(at[199]["eye"], Vector3(0, 0, 5)), "the picture holds up to the cut: %s" % [at[199]])
	check(at[200].get("cut", false) and near3(at[200]["eye"], Vector3(5, 5, 5)) and near(at[200]["rotation"][2], PI / 2), "the cut's picture: %s" % [at[200]])
	check(near(at[200]["fov"], 40), "the fov")
	check(d["actors"].size() == 1 and d["actors"][0]["source"] == {"prop": "meshes/crate.glb"} and d["actors"][0]["end"] == "remove", "the prop: %s" % [d["actors"]])
	check(d["subtitles"][0]["text"] == {"en": "Plain text"} and d["subtitles"][0]["end"] == 130 and d["subtitles"][0]["actor"] == "crate", "a plain line: %s" % [d["subtitles"]])
	check(d["sounds"][0]["file"] == {"en": "en.ogg", "fr": "fr.ogg"} and not d["sounds"][0].has("actor"), "a sound by language, its unknown actor dropped: %s" % [d["sounds"]])
	var events: Array = d["events"]
	check(events.size() == 2, "an unknown event is dropped")
	check(same(events[0], {"t": 0, "do": "fade", "to": [1, 1, 1, 0.5], "length": 20}), "a fade with alpha: %s" % [events[0]])
	check(events[1] == {"t": 9, "do": "travel", "level": 5}, "a travel keeps its level: %s" % [events[1]])
	root.queue_free()


func test_import_errors() -> void:
	var root := make_level("user://t/level_06", {"level": 6})
	var cs = add(root.get_node("Game"), Cutscene.new(), root)
	check(cs.import_cutscene("user://t/none.cutscene") == ERR_FILE_NOT_FOUND, "no file")
	write("user://t/bad.cutscene", "{ not json")
	check(cs.import_cutscene("user://t/bad.cutscene") == ERR_PARSE_ERROR, "not JSON")
	write("user://t/other.cutscene", "{\"format\": \"something else\"}")
	check(cs.import_cutscene("user://t/other.cutscene") == ERR_INVALID_DATA, "not a cutscene")
	check(cs.get_child_count() == 0, "a failed import adds nothing")
	# The least a file can be: nothing but its format.
	check(cs.from_cutscene({"format": "openrac-cutscene"}).is_empty(), "an empty cutscene imports quietly")
	var d: Dictionary = cs.to_cutscene()
	check(d["length"] == 1 and d["camera"] == {} and d["actors"] == [], "and exports empty: %s" % [d])
	root.queue_free()


# Run on the first frame: before it the root is not in the tree yet, so neither are the levels.
func _process(_delta: float) -> bool:
	test_full_cutscene()
	test_triggers_and_defaults()
	test_no_game_node()
	test_round_trip()
	test_import_by_hand()
	test_import_errors()
	print("cutscene export: ", "%d check(s) failed" % failures if failures else "all checks passed")
	quit(1 if failures else 0)
	return true
