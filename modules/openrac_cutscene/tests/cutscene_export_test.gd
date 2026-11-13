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
	for what in ["fewer than two points", "looking at an actor", "\"dance\""]:
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


# A walk, a shake, the parts the editor keeps as written, and a Japanese line drawn as a picture:
# exported, and imported back the same.
func test_walk_shake_and_pictures() -> void:
	var root := make_level("user://t/level_08", {"level": 8, "version": "pal"})
	var cs = add(root.get_node("Game"), Cutscene.new(), root)
	cs.name = "Walk"
	timeline(cs, 2.0)
	var cam = add(cs, CutsceneCamera.new(), root)
	cam.name = "Camera"
	var shake = add(cam, CutsceneShake.new(), root)
	shake.start = 0.5
	shake.duration = 0.25
	shake.strength = 0.4
	var walker = add(cs, CutsceneActor.new(), root)
	walker.name = "Walker"
	walker.source = CutsceneActor.SOURCE_HERO_FIGURE
	walker.position = Vector3(5, 0, 0)
	walker.advanced = {"look_at": [{"t": 0, "target": {"actor": "hero"}, "weight": 1}], "held": [{"t": 0, "from": "hero"}], "other": 1}
	var walk = add(walker, CutscenePath.new(), root)
	walk.name = "Path"
	walk.speed = 3.0
	walk.start = 0.5
	walk.ground = false
	var curve := Curve3D.new()
	curve.add_point(Vector3(1, 0, 0))
	curve.add_point(Vector3(1, 0, -4))  # Godot's -z is the game's +y
	walk.curve = curve
	check(walk.top_level, "a path stays put when its actor moves")
	var line = add(cs, CutsceneSubtitle.new(), root)
	line.name = "Line"
	line.duration = 1.0
	line.text = {"en": "Need a weapon?", "ja": "武器が必要か？いい物があるよ。"}

	var d: Dictionary = cs.to_cutscene()
	check(d["camera"].get("shake", []) == [{"t": 30, "length": 15, "strength": 0.4}], "the shake: %s" % [d["camera"].get("shake")])
	var a: Dictionary = d["actors"][0]
	check(a.has("path"), "the walk is written: %s" % [a])
	if a.has("path"):
		check(same(a["path"]["points"], [[1, 0, 0], [1, 4, 0]]), "its points in the game's space: %s" % [a["path"]["points"]])
		check(near(a["path"]["speed"], 3) and a["path"]["t"] == 30 and a["path"]["ground"] == false, "its speed, start and ground: %s" % [a["path"]])
	check(a.get("look_at") == walker.advanced["look_at"] and a.get("held") == walker.advanced["held"], "look_at and held as written")
	check(not a.has("other"), "only the parts of the format")

	# The Japanese line as a picture (when this computer has a font for it); the English one not.
	var sub: Dictionary = d["subtitles"][0]
	var font := SystemFont.new()
	font.font_names = PackedStringArray(["Noto Sans CJK JP", "Noto Sans JP", "Yu Gothic", "Meiryo", "MS Gothic", "Hiragino Sans", "WenQuanYi Zen Hei", "Droid Sans Fallback"])
	if font.has_char(0x6B66):
		check(sub.has("bitmap") and sub["bitmap"].has("ja") and not sub["bitmap"].has("en"), "a picture for ja only: %s" % [sub.keys()])
		if sub.has("bitmap") and sub["bitmap"].has("ja"):
			var pic: Dictionary = sub["bitmap"]["ja"]
			var w: int = pic["width"]
			var h: int = pic["height"]
			var bits := Marshalls.base64_to_raw(pic["bits"])
			check(w > 50 and w <= 882 and h >= 20 and h <= 120 and near(pic["pixel"], 0.5), "its size: %d x %d" % [w, h])
			check(bits.size() == ((w + 7) / 8) * h, "whole rows of bits: %d" % bits.size())
			var lit := 0
			for b in bits:
				for k in 8:
					lit += (b >> k) & 1
			check(lit > 100 and lit < w * h / 2, "letters drawn, not a block: %d of %d lit" % [lit, w * h])
	else:
		print("  (no Japanese font on this computer: the picture is not checked)")
	# A long line breaks into rows, three at most.
	line.text = {"ja": "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん".repeat(3)}
	d = cs.to_cutscene()
	if font.has_char(0x3042) and d["subtitles"][0].has("bitmap"):
		var pic: Dictionary = d["subtitles"][0]["bitmap"]["ja"]
		check(pic["width"] <= 882 and pic["height"] > 60 and pic["height"] < 130, "three rows at most: %d x %d" % [pic["width"], pic["height"]])

	# Imported back: the same walk, shake and advanced parts.
	var path := "user://t/walk.cutscene"
	write(path, JSON.stringify(cs.to_cutscene()))
	var copy = add(root.get_node("Game"), Cutscene.new(), root)
	copy.name = "WalkCopy"
	var problems: PackedStringArray = copy.from_cutscene(JSON.parse_string(FileAccess.get_file_as_string(path)))
	var said := "\n".join(problems)
	check(not said.contains("shake") and not said.contains("path") and not said.contains("look_at"), "nothing said lost: %s" % said)
	var again: Dictionary = copy.to_cutscene()
	check(same(again["camera"].get("shake"), d["camera"]["shake"]), "the shake again: %s" % [again["camera"].get("shake")])
	check(same(again["actors"][0].get("path"), d["actors"][0]["path"]), "the walk again: %s" % [again["actors"][0].get("path")])
	check(same(again["actors"][0].get("held"), d["actors"][0]["held"]), "held again")
	var copy_walk = copy.get_node_or_null("Walker/Path")
	check(copy_walk is CutscenePath and copy_walk.curve.point_count == 2, "a CutscenePath with its points")
	# A path of one point is no walk.
	walk.curve.remove_point(1)
	check(not cs.to_cutscene()["actors"][0].has("path"), "one point: no walk")
	root.queue_free()


# A WAV of 16-bit mono at 8000 Hz: `parts` of [seconds, loudness 0 to 1] one after the other.
func wav(path: String, parts: Array) -> void:
	var samples := PackedByteArray()
	for part in parts:
		var n := int(part[0] * 8000.0)
		for i in n:
			var v := int(sin(i * 0.3) * 30000.0 * part[1])
			samples.append(v & 0xFF)
			samples.append((v >> 8) & 0xFF)
	var head := PackedByteArray()
	head.append_array("RIFF".to_ascii_buffer())
	head.resize(head.size() + 4)
	head.encode_u32(4, 36 + samples.size())
	head.append_array("WAVEfmt ".to_ascii_buffer())
	var fmt := PackedByteArray()
	fmt.resize(20)
	fmt.encode_u32(0, 16)
	fmt.encode_u16(4, 1)
	fmt.encode_u16(6, 1)
	fmt.encode_u32(8, 8000)
	fmt.encode_u32(12, 16000)
	fmt.encode_u16(16, 2)
	fmt.encode_u16(18, 16)
	head.append_array(fmt)
	head.append_array("data".to_ascii_buffer())
	var size := PackedByteArray()
	size.resize(4)
	size.encode_u32(0, samples.size())
	head.append_array(size)
	head.append_array(samples)
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_buffer(head)
	f.close()


func test_faces() -> void:
	var root := make_level("user://t/level_09", {"level": 9, "version": "pal"})
	var cs = add(root.get_node("Game"), Cutscene.new(), root)
	cs.name = "Faces"
	timeline(cs, 3.0)
	var talker = add(cs, CutsceneActor.new(), root)
	talker.name = "Talker"
	talker.source = CutsceneActor.SOURCE_HERO_FIGURE
	var rig := {
		"jaw": {"joint": 10, "axis": [1, 0, 0], "open": 20},
		"lids": {"joints": [22, 23], "axis": [1, 0, 0], "close": 35},
		"head": {"joint": 8, "axis": [0, 0, 1], "limit": 50},
		"moods": {"happy": [{"joint": 25, "rotation": [0.1, 0, 0, 0.995]}]},
		"gaits": [{"seq": 0, "speed": 0}, {"seq": 2, "speed": 2}],
	}
	talker.rig = rig
	talker.blink = false
	var walk = add(talker, CutscenePath.new(), root)
	walk.name = "Path"
	var curve := Curve3D.new()
	curve.add_point(Vector3(0, 0, 0))
	curve.add_point(Vector3(3, 0, 0))
	walk.curve = curve
	var happy = add(talker, CutsceneMood.new(), root)
	happy.name = "Happy"
	happy.start = 0.5
	happy.blend = 0.25
	var grumpy = add(talker, CutsceneMood.new(), root)
	grumpy.name = "Grumpy"
	grumpy.start = 1.0
	grumpy.mood = "grumpy"
	grumpy.weight = 0.5
	happy.mood = "happy"
	check(happy.get_warnings().is_empty(), "a mood the rig has: no warning")
	check(not grumpy.get_warnings().is_empty(), "a mood the rig lacks is warned of")
	# A line: quiet, loud, quiet (0.1 s, 0.2 s, 0.1 s): three bytes shut, six open, three shut.
	wav("user://t/level_09/cutscenes/audio/hi_en.wav", [[0.1, 0.0], [0.2, 1.0], [0.1, 0.0]])
	write("user://t/level_09/cutscenes/audio/hi_fr.ogg", "not a wav")
	var line = add(cs, CutsceneSound.new(), root)
	line.name = "Line"
	line.start = 1.0
	line.files = {"en": "audio/hi_en.wav", "fr": "audio/hi_fr.ogg"}
	line.source = NodePath("../Talker")
	var other = add(cs, CutsceneSound.new(), root)
	other.name = "Music"
	other.kind = CutsceneSound.KIND_MUSIC
	other.file = "audio/hi_en.wav"
	other.source = NodePath("../Talker")

	var d: Dictionary = cs.to_cutscene()
	var a: Dictionary = d["actors"][0]
	check(a.has("face"), "a face is written: %s" % [a.keys()])
	var face: Dictionary = a.get("face", {})
	var want_rig := rig.duplicate(true)
	want_rig.erase("gaits")
	check(same(face.get("rig"), want_rig), "the rig without its gaits: %s" % [face.get("rig")])
	check(same(a["path"].get("gaits"), rig["gaits"]), "the gaits go with the walk: %s" % [a["path"]])
	check(face.get("blink") == false, "no blink, said")
	check(same(face.get("moods"), [{"t": 30, "mood": "happy", "weight": 1, "blend": 15}, {"t": 60, "mood": "grumpy", "weight": 0.5, "blend": 10}]), "the moods: %s" % [face.get("moods")])
	var talk: Array = face.get("talk", [])
	check(talk.size() == 1, "one talk, the voice line's (music is no talk): %s" % [talk])
	if talk.size() == 1:
		check(talk[0]["t"] == 60, "at the line's start")
		var env = talk[0]["envelope"]
		check(env is Dictionary and env.keys() == ["en"], "by language, only the WAV's: %s" % [env])
		if env is Dictionary and env.has("en"):
			var bytes := Marshalls.base64_to_raw(env["en"])
			check(bytes.size() == 12, "a byte a thirtieth of a second: %d" % bytes.size())
			if bytes.size() == 12:
				check(bytes[0] < 10 and bytes[1] < 10 and bytes[11] < 10, "shut while quiet: %s" % [bytes])
				check(bytes[4] > 200 and bytes[5] > 200 and bytes[6] > 200, "open while loud: %s" % [bytes])
	var problems: PackedStringArray = cs.export_problems()
	var said := "\n".join(problems)
	check(said.contains("grumpy"), "a mood the rig lacks is said: %s" % said)
	check(said.contains("hi_fr.ogg") and said.contains("not a WAV"), "a line that is no WAV is said: %s" % said)

	# One file, not by language: the envelope is one string.
	line.files = {}
	line.file = "audio/hi_en.wav"
	var one = cs.to_cutscene()["actors"][0]["face"]["talk"][0]["envelope"]
	check(one is String and Marshalls.base64_to_raw(one).size() == 12, "one file, one envelope: %s" % [one])
	# A broken WAV is said, not written.
	write("user://t/level_09/cutscenes/audio/bad.wav", "RIFF....WAVEjunk")
	line.file = "audio/bad.wav"
	check(not cs.to_cutscene()["actors"][0]["face"].has("talk"), "a broken WAV gives no talk")
	check("\n".join(cs.export_problems()).contains("bad.wav"), "and is said")
	line.file = "audio/hi_en.wav"

	# No jaw: no talk; no rig and no moods: no face.
	var no_jaw := rig.duplicate(true)
	no_jaw.erase("jaw")
	talker.rig = no_jaw
	check(not cs.to_cutscene()["actors"][0]["face"].has("talk"), "no jaw, no talk")
	talker.rig = {}
	happy.free()
	grumpy.free()
	check(not cs.to_cutscene()["actors"][0].has("face"), "no rig and no moods: no face")
	talker.rig = rig

	# Imported back: the rig with its gaits, the blink, the moods.
	var mood2 = add(talker, CutsceneMood.new(), root)
	mood2.name = "Happy"
	mood2.mood = "happy"
	var path := "user://t/faces.cutscene"
	write(path, JSON.stringify(cs.to_cutscene()))
	var copy = add(root.get_node("Game"), Cutscene.new(), root)
	copy.name = "FacesCopy"
	copy.from_cutscene(JSON.parse_string(FileAccess.get_file_as_string(path)))
	var back = copy.get_node_or_null("Talker")
	check(back is CutsceneActor, "the actor comes back")
	if back is CutsceneActor:
		check(same(back.rig, rig), "its rig, gaits too: %s" % [back.rig])
		check(back.blink == false, "its blink")
		var moods := back.get_children().filter(func(n): return n is CutsceneMood)
		check(moods.size() == 1 and moods[0].mood == "happy", "its mood: %s" % [moods])
	# The rigs the editor knows: none for a class it has no rig for, a project's own first.
	check(CutsceneActor.known_rig("rac1", 99999).is_empty(), "no rig for an unknown class")
	var ratchet := CutsceneActor.known_rig("rac1", -1)
	check(ratchet.get("jaw", {}).get("joint") == 10 and ratchet.get("head", {}).get("joint") == 8, "Ratchet's rig: %s" % [ratchet.keys()])
	check(ratchet.has("moods") and ratchet["moods"].has("happy") and ratchet.has("gaits"), "with moods and gaits")
	var figure = add(cs, CutsceneActor.new(), root)
	figure.name = "Figure"
	figure.source = CutsceneActor.SOURCE_HERO_FIGURE
	figure.get_use_known_rig_action().call()
	check(same(figure.rig, ratchet), "Use Known Rig gives a hero figure Ratchet's rig")
	write("res://cutscene_rigs.json", JSON.stringify({"rac1/77": {"jaw": {"joint": 3}}}))
	check(same(CutsceneActor.known_rig("rac1", 77), {"jaw": {"joint": 3}}), "a project's own rig")
	DirAccess.remove_absolute(ProjectSettings.globalize_path("res://cutscene_rigs.json"))
	root.queue_free()


func test_rides() -> void:
	var root := make_level("user://t/level_10", {"level": 10, "version": "pal"})
	var cs = add(root.get_node("Game"), Cutscene.new(), root)
	cs.name = "Rides"
	timeline(cs, 2.0)
	var ship = add(cs, CutsceneActor.new(), root)
	ship.name = "Ship"
	ship.source = CutsceneActor.SOURCE_NEW_MOBY
	ship.moby_class = 500
	ship.position = Vector3(10, 0, 0)  # the game's (10, 0, 0)
	ship.rotation = Vector3(0, 0, PI / 2)  # under Game: turned about the game's z, facing its +y
	var rider = add(cs, CutsceneActor.new(), root)
	rider.name = "Rider"
	rider.source = CutsceneActor.SOURCE_HERO_FIGURE
	rider.ride_on = NodePath("../Ship")
	rider.ride_start = 0.5
	rider.ride_end = 1.5
	var cam = add(cs, CutsceneCamera.new(), root)
	cam.name = "Camera"
	cam.position = Vector3(10, -2, 1)  # under Game: the game's own units
	cam.ride_on = NodePath("../Ship")
	cam.rotation = Vector3(PI / 2, 0, 0)  # looking along the game's +y, not straight down

	var d: Dictionary = cs.to_cutscene()
	var r: Dictionary = d["actors"][1]
	check(same(r.get("ride"), [{"t": 30, "actor": "ship"}, {"t": 90}]), "the ride: %s" % [r.get("ride")])
	var key: Dictionary = d["camera"]["keys"][0]
	var eye = key["eye"]
	check(eye is Dictionary and eye.get("actor") == "ship" and eye.get("turn") == true, "the eye on the ship: %s" % [eye])
	if eye is Dictionary:
		check(near3(eye["offset"], Vector3(-2, 0, 1), 0.01), "in the ship's axes: %s" % [eye["offset"]])
	# Imported back: the camera rides again, where it was.
	var path := "user://t/rides.cutscene"
	write(path, JSON.stringify(d))
	var copy = add(root.get_node("Game"), Cutscene.new(), root)
	copy.name = "RidesCopy"
	var problems: PackedStringArray = copy.from_cutscene(JSON.parse_string(FileAccess.get_file_as_string(path)))
	check(problems.is_empty(), "nothing lost: %s" % [problems])
	var back_rider = copy.get_node_or_null("Rider")
	check(back_rider != null and back_rider.ride_on == NodePath("../Ship") and near(back_rider.ride_start, 0.5) and near(back_rider.ride_end, 1.5), "the rider rides again")
	var again: Dictionary = copy.to_cutscene()
	check(same(again["camera"]["keys"][0]["eye"], eye, 0.01), "the camera's eye again: %s" % [again["camera"]["keys"][0]["eye"]])
	check(same(again["camera"]["keys"][0]["rotation"], key["rotation"], 0.01), "and its turn: %s vs %s" % [again["camera"]["keys"][0]["rotation"], key["rotation"]])
	# Riding on itself, or on nothing that is an actor: said, not written.
	rider.ride_on = NodePath(".")
	check(not cs.to_cutscene()["actors"][1].has("ride"), "no ride on itself")
	check("\n".join(cs.export_problems()).contains("rides on"), "and said")
	root.queue_free()


func test_air_slow_ties() -> void:
	var root := make_level("user://t/level_11", {"level": 11, "version": "pal"})
	var cs = add(root.get_node("Game"), Cutscene.new(), root)
	cs.name = "Air"
	timeline(cs, 3.0)
	var air = add(cs, CutsceneEvent.new(), root)
	air.name = "Air"
	air.action = CutsceneEvent.ACTION_AIR
	air.persistent = false
	air.colour = Color(1, 0.5, 0)
	air.fog_near = 5
	air.fog_far = 40
	air.fog_thickness = 0.5
	air.air_background = true
	air.background = Color(0, 0, 1)
	air.length = 0.5
	var back = add(cs, CutsceneEvent.new(), root)
	back.name = "Back"
	back.action = CutsceneEvent.ACTION_AIR
	back.persistent = false
	back.start = 2.0
	back.air_fog = false
	var slow = add(cs, CutsceneEvent.new(), root)
	slow.name = "Slow"
	slow.action = CutsceneEvent.ACTION_SLOW
	slow.persistent = false
	slow.start = 1.0
	slow.stretch = 20  # up to 8
	slow.length = 0.25
	var tie = add(cs, CutsceneEvent.new(), root)
	tie.name = "Tie"
	tie.action = CutsceneEvent.ACTION_TIE_HIDE
	tie.tie = 42

	var d: Dictionary = cs.to_cutscene()
	var ev: Array = d["events"]
	var by := {}
	for e in ev:
		by[e["do"] + str(e["t"])] = e
	check(same(by.get("air0"), {"t": 0, "do": "air", "fog": {"colour": [1, 0.5, 0], "near": 5, "far": 40, "thickness": 0.5}, "background": [0, 0, 1], "length": 30}), "the air: %s" % [by.get("air0")])
	check(same(by.get("air120"), {"t": 120, "do": "air", "length": 60}), "back to the game's: %s" % [by.get("air120")])
	check(same(by.get("slow60"), {"t": 60, "do": "slow", "stretch": 8, "length": 15}), "slow: %s" % [by.get("slow60")])
	check(same(by.get("tie_hide0"), {"t": 0, "do": "tie_hide", "tie": 42, "on": true, "persistent": true}), "tie: %s" % [by.get("tie_hide0")])
	var copy = add(root.get_node("Game"), Cutscene.new(), root)
	copy.name = "AirCopy"
	var problems: PackedStringArray = copy.from_cutscene(d)
	check(problems.is_empty(), "nothing lost: %s" % [problems])
	check(same(copy.to_cutscene()["events"], ev), "the same again: %s" % [copy.to_cutscene()["events"]])
	root.queue_free()


func test_layers() -> void:
	var root := make_level("user://t/level_13", {"level": 13, "version": "pal"})
	var cs = add(root.get_node("Game"), Cutscene.new(), root)
	cs.name = "Wave"
	var anim := timeline(cs, 1.0)
	var actor = add(cs, CutsceneActor.new(), root)
	actor.name = "Waver"
	actor.source = CutsceneActor.SOURCE_HERO_FIGURE
	actor.rig = {"parts": {"head": [8, 10]}}
	var layer = add(actor, CutsceneLayer.new(), root)
	layer.name = "Arm Wave"
	check(not layer.get_warnings().is_empty(), "no joints: warned")
	layer.set("use_part", "head")
	check(layer.joints == PackedInt32Array([8, 9, 10]), "a part as the mask: %s" % [layer.joints])
	layer.joints = PackedInt32Array([52, 53, 52, -1])
	check(layer.joints == PackedInt32Array([52, 53]), "kept once each, in range: %s" % [layer.joints])
	check(layer.get_warnings().is_empty(), "joints: no warning")
	# Joint 52 turns 90 degrees about x over the first half second; 53 stays at 10 about y.
	var path := String(cs.get_path_to(layer))
	var k := anim.add_track(Animation.TYPE_VALUE)
	anim.track_set_path(k, NodePath(path + ":joint_52"))
	anim.track_insert_key(k, 0.0, Vector3.ZERO)
	anim.track_insert_key(k, 0.5, Vector3(90, 0, 0))
	layer.set("joint_53", Vector3(0, 10, 0))
	var w := anim.add_track(Animation.TYPE_VALUE)
	anim.track_set_path(w, NodePath(path + ":weight"))
	anim.track_insert_key(w, 0.0, 0.0)
	anim.track_insert_key(w, 0.25, 1.0)
	var d: Dictionary = cs.to_cutscene()
	var l: Dictionary = d["actors"][0]["layers"][0]
	check(l["name"] == "arm_wave" and same(l["joints"], [52, 53]), "the layer: %s" % [l])
	check(same(l["weight"], [{"t": 0, "w": 0}, {"t": 15, "w": 1}]), "its weight: %s" % [l.get("weight")])
	check(l["frames"] == "layers/wave_waver_arm_wave.bin", "its frames: %s" % [l["frames"]])
	var files: Dictionary = cs.export_files()
	var bytes: PackedByteArray = files.get(l["frames"], PackedByteArray())
	check(bytes.size() == 31 * 2 * 16, "a turn per joint per tick up to the last key: %d" % bytes.size())
	if bytes.size() == 31 * 2 * 16:
		var last := Quaternion(bytes.decode_float(30 * 32), bytes.decode_float(30 * 32 + 4), bytes.decode_float(30 * 32 + 8), bytes.decode_float(30 * 32 + 12))
		check(last.is_equal_approx(Quaternion(Vector3(1, 0, 0), PI / 2)), "52 at the end: %s" % [last])
		var half := Quaternion(bytes.decode_float(15 * 32), bytes.decode_float(15 * 32 + 4), bytes.decode_float(15 * 32 + 8), bytes.decode_float(15 * 32 + 12))
		check(half.is_equal_approx(Quaternion(Vector3(1, 0, 0), PI / 4)), "and halfway: %s" % [half])
		var other := Quaternion(bytes.decode_float(16), bytes.decode_float(20), bytes.decode_float(24), bytes.decode_float(28))
		check(other.is_equal_approx(Quaternion(Vector3(0, 1, 0), deg_to_rad(10))), "53 held: %s" % [other])
	# Written beside the .cutscene and read back as a layer with keys.
	root.scene_file_path = "user://t/level_13/level.tscn"
	check(cs.export_cutscene() != "", "exported")
	check(FileAccess.file_exists("user://t/level_13/cutscenes/layers/wave_waver_arm_wave.bin"), "the frames written")
	var copy = add(root.get_node("Game"), Cutscene.new(), root)
	copy.name = "WaveCopy"
	timeline(copy, 1.0)
	var problems: PackedStringArray = copy.from_cutscene(d)
	check(problems.is_empty(), "nothing lost: %s" % [problems])
	var back = copy.get_node_or_null("Waver/ArmWave")
	check(back != null and back.joints == PackedInt32Array([52, 53]), "the layer again")
	if back != null:
		var again: Dictionary = copy.to_cutscene()
		var l2: Dictionary = again["actors"][0]["layers"][0]
		check(same(l2["weight"], l["weight"]), "its weight again: %s" % [l2.get("weight")])
		var bytes2: PackedByteArray = copy.export_files().get(l2["frames"], PackedByteArray())
		check(bytes2.size() == bytes.size(), "as many frames again: %d" % bytes2.size())
		var off := 0.0
		for i in range(0, mini(bytes.size(), bytes2.size()), 4):
			off = maxf(off, absf(bytes.decode_float(i) - bytes2.decode_float(i)))
		check(off < 0.01, "the same turns, within the 30 Hz keys: %f" % off)
	root.queue_free()


func test_game_view() -> void:
	# The game's picture in a view: as large as fits, centred.
	check(Cutscene.game_frame(Vector2(1600, 900), false) == Rect2(200, 0, 1200, 900), "4:3 in a wide view")
	check(Cutscene.game_frame(Vector2(1600, 900), true) == Rect2(0, 0, 1600, 900), "16:9 fills it")
	check(Cutscene.game_frame(Vector2(800, 900), true) == Rect2(0, 225, 800, 450), "16:9 in a tall view")
	var root := make_level("user://t/level_12", {"level": 12, "version": "pal"})
	var cs = add(root.get_node("Game"), Cutscene.new(), root)
	cs.name = "View"
	cs.languages = PackedStringArray(["en", "fr"])
	timeline(cs, 4.0)
	check(cs.get_timeline_position() == 0.0, "no place on the timeline before one is chosen")
	var line = add(cs, CutsceneSubtitle.new(), root)
	line.start = 1.0
	line.duration = 1.0
	line.text = {"en": "This line is long enough that the game's font must break it in two", "fr": "Bonjour"}
	check(cs.subtitles_at(0.5).is_empty(), "nothing before the line")
	var rows: PackedStringArray = cs.subtitles_at(1.5)
	check(rows.size() == 2 and rows[0].length() <= 40, "wrapped as the player does: %s" % [rows])
	check(cs.subtitles_at(1.5, "fr") == PackedStringArray(["Bonjour"]), "by language")
	check(cs.subtitles_at(2.0).is_empty(), "gone at its end")
	var player: AnimationPlayer = cs.get_timeline()
	player.assigned_animation = cs.get_cutscene_id()
	player.seek(1.5, true)
	check(near(cs.get_timeline_position(), 1.5), "the timeline's place: %s" % [cs.get_timeline_position()])
	var fresh := CutsceneCamera.new()
	check(near(fresh.fov, 51.0), "a new camera sees as the game's")
	fresh.free()
	root.queue_free()


func test_retarget() -> void:
	var root := make_level("user://t/level_14", {"level": 14, "version": "pal"})
	var cs = add(root.get_node("Game"), Cutscene.new(), root)
	cs.name = "Mocap"
	var anim := timeline(cs, 2.0)
	var actor = add(cs, CutsceneActor.new(), root)
	actor.name = "Dancer"
	actor.source = CutsceneActor.SOURCE_HERO_FIGURE
	var layer = add(actor, CutsceneLayer.new(), root)
	layer.name = "Arms"
	# A source skeleton: an upper arm resting turned 30 degrees about z, raised 90 about x in a second.
	var skeleton := Skeleton3D.new()
	skeleton.add_bone("UpperArm")
	skeleton.add_bone("Hand")
	var rest := Basis(Vector3(0, 0, 1), deg_to_rad(30))
	skeleton.set_bone_rest(0, Transform3D(rest, Vector3.ZERO))
	var source := Animation.new()
	source.length = 1.0
	var r := source.add_track(Animation.TYPE_ROTATION_3D)
	source.track_set_path(r, NodePath("Skeleton3D:UpperArm"))
	source.rotation_track_insert_key(r, 0.0, rest.get_rotation_quaternion())
	source.rotation_track_insert_key(r, 1.0, (rest * Basis(Vector3(1, 0, 0), PI / 2)).get_rotation_quaternion())
	var problems: String = layer.retarget(source, skeleton, {"UpperArm": 52, "Hand": {"joint": 60, "axes": Basis()}, "Tail": 3}, 0.5)
	check(problems.contains("no bone \"Tail\"") and problems.contains("does not turn bone \"Hand\""), "what it could not do: %s" % problems)
	check(layer.joints == PackedInt32Array([52, 60]), "the mapped joints in the mask: %s" % [layer.joints])
	var k := anim.find_track(NodePath(String(cs.get_path_to(layer)) + ":joint_52"), Animation.TYPE_VALUE)
	check(k >= 0 and anim.track_get_key_count(k) == 31, "a key each 30 Hz frame: %d" % (anim.track_get_key_count(k) if k >= 0 else -1))
	if k >= 0:
		check(near(anim.track_get_key_time(k, 0), 0.5), "from the start given")
		var last: Vector3 = anim.track_get_key_value(k, anim.track_get_key_count(k) - 1)
		check(last.is_equal_approx(Vector3(90, 0, 0)), "the turn from the rest, not the rest: %s" % [last])
	# Axes: a bone whose x is the joint's y turns the joint about y.
	var turned = add(actor, CutsceneLayer.new(), root)
	turned.name = "Turned"
	turned.retarget(source, skeleton, {"UpperArm": {"joint": 70, "axes": Basis(Vector3(0, 1, 0), Vector3(-1, 0, 0), Vector3(0, 0, 1))}}, 0.0)
	var k2 := anim.find_track(NodePath(String(cs.get_path_to(turned)) + ":joint_70"), Animation.TYPE_VALUE)
	if k2 >= 0:
		var end: Vector3 = anim.track_get_key_value(k2, anim.track_get_key_count(k2) - 1)
		check(Basis.from_euler(end * PI / 180).is_equal_approx(Basis(Vector3(0, 1, 0), PI / 2)), "about the joint's own axis: %s" % [end])
	else:
		check(false, "the turned layer's track")
	# Another actor of the same skeleton plays it too, a second later.
	var other = add(cs, CutsceneActor.new(), root)
	other.name = "Twin"
	other.source = CutsceneActor.SOURCE_HERO_FIGURE
	var copy = add(other, CutsceneLayer.new(), root)
	copy.name = "Arms"
	check(copy.copy_keys_from(layer, 1.0) == "", "copied")
	check(copy.joints == layer.joints, "the same mask")
	var k3 := anim.find_track(NodePath(String(cs.get_path_to(copy)) + ":joint_52"), Animation.TYPE_VALUE)
	check(k3 >= 0 and near(anim.track_get_key_time(k3, 0), 1.5), "its keys a second later")
	check(copy.copy_keys_from(copy, 0.0) != "", "not from itself")
	# Feet on the ground: the hero's legs from the known rig; a moby with no legs is told.
	actor.feet_on_ground = true
	var mob = add(cs, CutsceneActor.new(), root)
	mob.name = "Mob"
	mob.source = CutsceneActor.SOURCE_NEW_MOBY
	mob.moby_class = 500
	mob.feet_on_ground = true
	var problems2 := PackedStringArray()
	var d: Dictionary = cs.to_cutscene()
	var dancer: Dictionary = d["actors"][0]
	check(same(dancer.get("feet", {}).get("legs", []), [[92, 93, 94], [98, 99, 100]]), "the hero's legs: %s" % [dancer.get("feet")])
	check(not d["actors"][2].has("feet"), "no legs, no feet")
	var back = add(root.get_node("Game"), Cutscene.new(), root)
	back.name = "MocapBack"
	timeline(back, 2.0)
	back.from_cutscene(d)
	var again = back.get_node_or_null("Dancer")
	check(again != null and again.feet_on_ground, "feet on the ground again")
	# A bake again over the same time replaces its keys, not adds to them.
	layer.retarget(source, skeleton, {"UpperArm": 52}, 0.5)
	check(anim.track_get_key_count(k) == 31, "baked again: %d keys" % anim.track_get_key_count(k))
	# A turn all the way round: each key near the one before (no jump from 179 to -179).
	var spin := Animation.new()
	spin.length = 1.0
	var sr := spin.add_track(Animation.TYPE_ROTATION_3D)
	spin.track_set_path(sr, NodePath("Skeleton3D:UpperArm"))
	for i in 9:
		spin.rotation_track_insert_key(sr, i / 8.0, (rest * Basis(Vector3(0, 1, 0).rotated(Vector3(1, 0, 0), 0.3), i * PI / 4)).get_rotation_quaternion())
	var spun = add(actor, CutsceneLayer.new(), root)
	spun.name = "Spun"
	spun.retarget(spin, skeleton, {"UpperArm": 80}, 0.0)
	var ks := anim.find_track(NodePath(String(cs.get_path_to(spun)) + ":joint_80"), Animation.TYPE_VALUE)
	var worst := 0.0
	for i in range(1, anim.track_get_key_count(ks)):
		var step: Vector3 = anim.track_get_key_value(ks, i) - anim.track_get_key_value(ks, i - 1)
		worst = max(worst, max(abs(step.x), max(abs(step.y), abs(step.z))))
	check(worst < 30.0, "a whole turn in small steps: worst %.1f degrees" % worst)
	# Two layers whose names are one file name: two files.
	var twin_layer = add(actor, CutsceneLayer.new(), root)
	twin_layer.name = "turned"
	twin_layer.joints = PackedInt32Array([5])
	var files: Dictionary = {}
	for l in cs.to_cutscene()["actors"][0].get("layers", []):
		files[l["frames"]] = true
	check(files.size() == 4, "one frames file a layer: %s" % [files.keys()])
	skeleton.free()
	root.queue_free()


# Run on the first frame: before it the root is not in the tree yet, so neither are the levels.
func _process(_delta: float) -> bool:
	test_full_cutscene()
	test_triggers_and_defaults()
	test_no_game_node()
	test_round_trip()
	test_import_by_hand()
	test_import_errors()
	test_walk_shake_and_pictures()
	test_faces()
	test_rides()
	test_air_slow_ties()
	test_game_view()
	test_layers()
	test_retarget()
	print("cutscene export: ", "%d check(s) failed" % failures if failures else "all checks passed")
	quit(1 if failures else 0)
	return true
