/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: the nodes a cutscene is made of, and its export to a .cutscene file
 * (docs/CUTSCENE_FORMAT.md). A Cutscene is placed in the level; its children are the camera, the
 * actors, the subtitles, the sounds and the events; an AnimationPlayer child holds the timeline. */

#pragma once

#include "core/variant/typed_array.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/path_3d.h"

class AnimationPlayer;
class Animation;

// The cutscene: where it is in the level (the centre of its trigger area), how it starts and what
// it does to the game while it plays.
class Cutscene : public Node3D {
	GDCLASS(Cutscene, Node3D);

public:
	enum TriggerKind {
		TRIGGER_LEVEL_START,
		TRIGGER_AREA,
		TRIGGER_TALK,
		TRIGGER_PICKUP,
		TRIGGER_AFTER,
		TRIGGER_FLAG,
		TRIGGER_MANUAL,
	};
	enum HeroMode {
		HERO_HELD,
		HERO_HIDDEN,
		HERO_ACTOR,
	};
	enum World {
		WORLD_RUN,
		WORLD_HOLD,
	};
	enum Music {
		MUSIC_DUCK,
		MUSIC_KEEP,
		MUSIC_STOP,
	};

private:
	String file_name;
	String title;
	String game = "rac1";
	double length = 0.0;
	PackedStringArray languages;

	TriggerKind trigger_kind = TRIGGER_AREA;
	bool trigger_once = true;
	float trigger_radius = 4.0f;
	Vector3 trigger_box;
	int trigger_spawn_id = -1;
	String trigger_after;
	Vector3i trigger_flag = Vector3i(-1, 0, 1); // level, byte, value; level -1: none
	bool trigger_flag_enabled = false;

	bool skippable = true;
	bool letterbox = true;
	bool hide_hud = true;
	World world = WORLD_RUN;
	Music music = MUSIC_DUCK;
	HeroMode hero_mode = HERO_HELD;
	int hero_sequence = 0;
	TypedArray<Dictionary> variants;

	// The editor's view of the held hero: the level's hero marker stands in its rest pose, so a copy
	// of it plays the held hero's sequence in its place (not saved), the marker hidden meanwhile.
	Node3D *hero_preview = nullptr;
	ObjectID hero_marker;
	uint64_t hero_marker_looked = 0;
	void _update_hero_preview();
	void _show_hero_marker(bool p_show);

	void _export_pressed();
	void _import_pressed();
	void _set_up_timeline();
	Dictionary _to_cutscene_bind() const;
	String import_from;  // the folder of the .cutscene being imported, for the files it names

public:
	// What an export leaves out or cannot do, said in words (an export prints them too).
	PackedStringArray export_problems() const;
	// The files an export writes beside the .cutscene besides it, by path.
	Dictionary export_files() const;

private:
	String _export_cutscene_bind() const;
	Error _import_cutscene_bind(const String &p_path);

protected:
	static void _bind_methods();
	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_file_name(const String &p_name);
	String get_file_name() const;
	void set_title(const String &p_title);
	String get_title() const;
	void set_game(const String &p_game);
	String get_game() const;
	void set_length(double p_seconds);
	double get_length() const;
	void set_languages(const PackedStringArray &p_languages);
	PackedStringArray get_languages() const;
	// The languages as the inspector's tick boxes (bits of LANGUAGE_CODES); none: the disc's own.
	void set_translations(int p_bits);
	int get_translations() const;
	// The languages the lines are written in: the set ones, else the disc's (from level.json).
	PackedStringArray text_languages() const;
	static constexpr const char *LANGUAGE_CODES[] = { "en", "fr", "de", "es", "it", "ja" };
	static constexpr const char *LANGUAGE_NAMES[] = { "English", "French", "German", "Spanish", "Italian", "Japanese" };
	static String language_name(const String &p_code);
	static String language_code(const String &p_name);
	void set_trigger_kind(TriggerKind p_kind);
	TriggerKind get_trigger_kind() const;
	void set_trigger_once(bool p_once);
	bool get_trigger_once() const;
	void set_trigger_radius(float p_radius);
	float get_trigger_radius() const;
	void set_trigger_box(const Vector3 &p_size);
	Vector3 get_trigger_box() const;
	void set_trigger_spawn_id(int p_id);
	int get_trigger_spawn_id() const;
	void set_trigger_after(const String &p_name);
	String get_trigger_after() const;
	void set_trigger_flag_enabled(bool p_enabled);
	bool get_trigger_flag_enabled() const;
	void set_trigger_flag(const Vector3i &p_flag);
	Vector3i get_trigger_flag() const;
	void set_skippable(bool p_on);
	bool get_skippable() const;
	void set_letterbox(bool p_on);
	bool get_letterbox() const;
	void set_hide_hud(bool p_on);
	bool get_hide_hud() const;
	void set_world(World p_world);
	World get_world() const;
	void set_music(Music p_music);
	Music get_music() const;
	void set_hero_mode(HeroMode p_mode);
	HeroMode get_hero_mode() const;
	void set_hero_sequence(int p_sequence);
	int get_hero_sequence() const;
	void set_variants(const TypedArray<Dictionary> &p_variants);
	TypedArray<Dictionary> get_variants() const;
	Callable get_export_action();
	Callable get_import_action();
	Callable get_set_up_action();

	// The file's name without .cutscene: the property, else the node's name in snake_case.
	String get_cutscene_id() const;
	// The timeline: the first AnimationPlayer child and its animation (the one named like the
	// cutscene, else its first). Null when there is none.
	AnimationPlayer *get_timeline() const;
	Ref<Animation> get_timeline_animation() const;
	// The .cutscene as a Dictionary (docs/CUTSCENE_FORMAT.md), and written beside the level's
	// scene, in cutscenes/. `r_problems` gets what could not be exported, in words.
	// `r_files` gets the files the .cutscene names that the editor makes (a layer's frames), by
	// their path beside it.
	Dictionary to_cutscene(PackedStringArray *r_problems = nullptr, Dictionary *r_files = nullptr) const;
	Error export_cutscene(String *r_path = nullptr) const;
	// The other way: a .cutscene's camera, actors, lines, sounds and events as children and timeline
	// tracks of this cutscene, which must have none yet. Returns what could not be kept, in words.
	PackedStringArray from_cutscene(const Dictionary &p_data);
	Error import_cutscene(const String &p_path, PackedStringArray *r_problems = nullptr);

	PackedStringArray get_configuration_warnings() const override;

	// The game's picture in an editor view of `p_size` looking through the cutscene's camera: 4:3,
	// or 16:9 with `p_wide`, as large as fits, centred (the field of view is vertical both ways).
	static Rect2 game_frame(const Vector2 &p_size, bool p_wide);
	// Where the timeline is now, in seconds (0 with none).
	double get_timeline_position() const;
	// The subtitle rows shown at `p_seconds`, in `p_language` (empty: the cutscene's first), as the
	// player wraps them for the game's font.
	PackedStringArray subtitles_at(double p_seconds, const String &p_language = String()) const;
};

// The camera of the cutscene: its place, turn and roll are keyed on the timeline as any Camera3D's.
// `cuts` are the times (seconds) where the picture jumps instead of moving.
class CutsceneCamera : public Camera3D {
	GDCLASS(CutsceneCamera, Camera3D);

	PackedFloat32Array cuts;
	bool ease_back = true;
	bool export_fov = false;
	NodePath ride_on;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	// The game's own vertical field of view, about (the player's kGameFov), so a new camera sees
	// what the game's would.
	static constexpr float GAME_FOV = 51.0f;
	CutsceneCamera() { set_fov(GAME_FOV); }

	void set_cuts(const PackedFloat32Array &p_cuts);
	PackedFloat32Array get_cuts() const;
	void set_ease_back(bool p_ease);
	bool get_ease_back() const;
	void set_export_fov(bool p_export);
	bool get_export_fov() const;
	// An actor the camera rides on: its keys are written against the actor's place and turn, so in
	// the game the camera goes where the actor goes.
	void set_ride_on(const NodePath &p_actor);
	NodePath get_ride_on() const;
};

// One character or object of the cutscene: a moby of the level (by its spawn id), a new moby of a
// class, a figure in the hero's look, or a mesh. Its place is keyed on the timeline; `sequence`
// and `anim_speed` keys start the game's animations, `visible` keys show and hide it.
class CutsceneActor : public Node3D {
	GDCLASS(CutsceneActor, Node3D);

public:
	enum Source {
		SOURCE_LEVEL_MOBY,
		SOURCE_NEW_MOBY,
		SOURCE_HERO_FIGURE,
		SOURCE_PROP,
	};
	enum End {
		END_KEEP,
		END_RESTORE,
		END_REMOVE,
	};

private:
	String actor_id;
	Source source = SOURCE_LEVEL_MOBY;
	int spawn_id = -1;
	int moby_class = -1;
	String prop_path;
	bool spawn_if_missing = true;
	End end = END_KEEP;
	int sequence = -1;
	float anim_speed = 1.0f;
	int blend = 4;
	Dictionary advanced;
	Dictionary rig;
	bool blink = true;
	bool feet_on_ground = false;
	NodePath ride_on;
	double ride_start = 0.0;
	double ride_end = 0.0;

	void _take_selected_moby();
	void _use_known_rig();

	// In the editor: the actor's model (its moby class's scene, or the prop), not saved, posed in
	// the game's sequence at the timeline's time, so the 3D view shows what the game will play.
	Node3D *preview = nullptr;
	String preview_from;
	String _preview_scene() const;
	void _update_preview();

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;
	void _notification(int p_what);

public:
	void set_actor_id(const String &p_id);
	String get_actor_id() const;
	// The id the file uses: actor_id, else the node's name in snake_case.
	String get_effective_id() const;
	void set_source(Source p_source);
	Source get_source() const;
	void set_spawn_id(int p_id);
	int get_spawn_id() const;
	void set_moby_class(int p_class);
	int get_moby_class() const;
	void set_prop_path(const String &p_path);
	String get_prop_path() const;
	void set_spawn_if_missing(bool p_spawn);
	bool get_spawn_if_missing() const;
	void set_end(End p_end);
	End get_end() const;
	void set_sequence(int p_sequence);
	int get_sequence() const;
	void set_anim_speed(float p_speed);
	float get_anim_speed() const;
	void set_blend(int p_ticks);
	int get_blend() const;
	Callable get_take_selected_action();
	Callable get_use_known_rig_action();
	// The actor's rig (docs/CUTSCENE_FORMAT.md, `face.rig`): its jaw, eyelids, head, moods and
	// gaits. Take Selected Moby fills it from the rigs the editor knows (cutscene_rigs.h, and a
	// project's res://cutscene_rigs.json) when it is empty.
	void set_rig(const Dictionary &p_rig);
	Dictionary get_rig() const;
	void set_blink(bool p_blink);
	bool get_blink() const;
	// Keeps the feet on the level's ground in the game (steps, slopes): the legs the rig's "feet"
	// names bend to reach it, and the body lowers for the lower foot.
	void set_feet_on_ground(bool p_on);
	bool get_feet_on_ground() const;
	// Another actor (or the hero's figure) this one rides on from `ride_start` to `ride_end` (0: to the
	// end): in the game it keeps its place against the carrier from then on.
	void set_ride_on(const NodePath &p_actor);
	NodePath get_ride_on() const;
	void set_ride_start(double p_seconds);
	double get_ride_start() const;
	void set_ride_end(double p_seconds);
	double get_ride_end() const;
	// The rig the editor knows for a moby class of `p_game` (-1 for the hero), empty for none.
	static Dictionary known_rig(const String &p_game, int p_class);
	// "look_at", "held" and "layers" as the file writes them (docs/CUTSCENE_FORMAT.md), kept as
	// they are: what the editor has no node for yet.
	void set_advanced(const Dictionary &p_advanced);
	Dictionary get_advanced() const;

	PackedStringArray get_configuration_warnings() const override;
};

// Where an actor walks (the file's `path`), as a child of the CutsceneActor: the curve's points on
// the ground, from `start` at `speed` units a second. Its points are drawn and moved in the 3D view
// as any Path3D's; it stays put when the timeline moves the actor (a top-level node).
class CutscenePath : public Path3D {
	GDCLASS(CutscenePath, Path3D);

	double start = 0.0;
	double speed = 8.0;
	bool ground = true;

protected:
	static void _bind_methods();

public:
	CutscenePath();
	void set_start(double p_seconds);
	double get_start() const;
	void set_speed(double p_speed);
	double get_speed() const;
	void set_ground(bool p_ground);
	bool get_ground() const;

	PackedStringArray get_configuration_warnings() const override;
};

// A shake of the camera (the file's `camera.shake`), as a child of the CutsceneCamera: from
// `start`, fading out over `duration`.
class CutsceneShake : public Node {
	GDCLASS(CutsceneShake, Node);

	double start = 0.0;
	double duration = 0.5;
	double strength = 0.3;

protected:
	static void _bind_methods();

public:
	void set_start(double p_seconds);
	double get_start() const;
	void set_duration(double p_seconds);
	double get_duration() const;
	void set_strength(double p_strength);
	double get_strength() const;

	PackedStringArray get_configuration_warnings() const override;
};

// A mood on an actor's face from `start`, faded in over `blend` (a child of a CutsceneActor whose
// rig has the mood; an empty mood fades back to the face's own).
class CutsceneMood : public Node {
	GDCLASS(CutsceneMood, Node);

	double start = 0.0;
	String mood;
	double weight = 1.0;
	double blend = 1.0 / 6.0;

protected:
	static void _bind_methods();

public:
	void set_start(double p_seconds);
	double get_start() const;
	void set_mood(const String &p_mood);
	String get_mood() const;
	void set_weight(double p_weight);
	double get_weight() const;
	void set_blend(double p_seconds);
	double get_blend() const;

	PackedStringArray get_configuration_warnings() const override;
};

// Hand-set joint turns over the actor's animation (a child of a CutsceneActor): `joints` is its
// mask, each listed joint a keyable `joint_<n>` turn (degrees about the joint's own axes, Godot's
// YXZ order), `weight` a keyable blend from none (0) to all (1). Exported as the file's `layers`,
// one turn per joint per tick in a .bin beside the .cutscene.
class CutsceneLayer : public Node {
	GDCLASS(CutsceneLayer, Node);

	PackedInt32Array joints;
	double weight = 1.0;
	HashMap<int, Vector3> turns;
	// Retarget (editor): a scene with a Skeleton3D and an animation (a .glb from Blender, mocap),
	// its animation's name (empty: the first), which of its bones turns which joint, and when it
	// starts on the timeline.
	String retarget_source;
	String retarget_animation;
	Dictionary retarget_map;
	double retarget_start = 0.0;

	void _retarget_from_source();

protected:
	static void _bind_methods();
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

public:
	void set_joints(const PackedInt32Array &p_joints);
	PackedInt32Array get_joints() const;
	void set_weight(double p_weight);
	double get_weight() const;
	void set_turn(int p_joint, const Vector3 &p_degrees);
	Vector3 get_turn(int p_joint) const;
	// The joints of a part of the actor's rig ("head", "left_arm", ... as the rig's "parts" list
	// them), set as the mask.
	void use_part(const String &p_part);

	// Bakes `p_source`'s turns of the skeleton's bones onto this layer's joints, a key every 30 Hz
	// frame from `p_start` (seconds): each bone's turn from its rest, in the joint's own axes.
	// `p_map` maps a bone's name to a joint (an int) or to {"joint": int, "axes": Basis} when the
	// bone's axes are not the joint's (the bone's x, y, z as the joint's). The mapped joints join
	// the mask. Returns what it could not do, one line each; empty when it did all.
	String retarget(const Ref<Animation> &p_source, Node *p_skeleton, const Dictionary &p_map, double p_start);
	// Copies another layer's joints, weight and keys (moved by `p_offset` seconds) onto this one:
	// a pose or a move made once, played by another actor of the same skeleton.
	String copy_keys_from(Node *p_layer, double p_offset);
	void set_retarget_source(const String &p_path);
	String get_retarget_source() const;
	void set_retarget_animation(const String &p_name);
	String get_retarget_animation() const;
	void set_retarget_map(const Dictionary &p_map);
	Dictionary get_retarget_map() const;
	void set_retarget_start(double p_seconds);
	double get_retarget_start() const;
	Callable get_retarget_action();

	PackedStringArray get_configuration_warnings() const override;
};

// A line of text at the bottom of the screen, in each of the disc's languages.
class CutsceneSubtitle : public Node {
	GDCLASS(CutsceneSubtitle, Node);

	double start = 0.0;
	double duration = 2.0;
	Dictionary text;
	NodePath speaker;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

public:
	void set_start(double p_seconds);
	double get_start() const;
	void set_duration(double p_seconds);
	double get_duration() const;
	void set_text(const Dictionary &p_text);
	Dictionary get_text() const;
	void set_speaker(const NodePath &p_actor);
	NodePath get_speaker() const;
};

// A voice line, music or a sound effect. `file` is beside the .cutscene (a path, or one per
// language); `game_sound` plays one of the hero's own instead.
class CutsceneSound : public Node {
	GDCLASS(CutsceneSound, Node);

public:
	enum Kind {
		KIND_VOICE,
		KIND_MUSIC,
		KIND_SFX,
	};

private:
	double start = 0.0;
	Kind kind = KIND_VOICE;
	String file;
	Dictionary files;
	int game_sound = -1;
	NodePath source;
	float volume = 1.0f;
	double fade_in = 0.0;
	double fade_out = 0.0;
	double stop = 0.0;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

public:
	void set_start(double p_seconds);
	double get_start() const;
	void set_kind(Kind p_kind);
	Kind get_kind() const;
	void set_file(const String &p_file);
	String get_file() const;
	void set_files(const Dictionary &p_files);
	Dictionary get_files() const;
	void set_game_sound(int p_sound);
	int get_game_sound() const;
	void set_source(const NodePath &p_actor);
	NodePath get_source() const;
	void set_volume(float p_volume);
	float get_volume() const;
	void set_fade_in(double p_seconds);
	double get_fade_in() const;
	void set_fade_out(double p_seconds);
	double get_fade_out() const;
	void set_stop(double p_seconds);
	double get_stop() const;
};

// Something the cutscene does to the level at a time: hide or delete a moby, set a mission flag,
// heat haze, another cutscene, a travel. `persistent` ones are done even when it is skipped.
class CutsceneEvent : public Node {
	GDCLASS(CutsceneEvent, Node);

public:
	enum Action {
		ACTION_MOBY_HIDE,
		ACTION_MOBY_SHOW,
		ACTION_MOBY_KILL,
		ACTION_MISSION_FLAG,
		ACTION_HAZE,
		ACTION_FADE,
		ACTION_PLAY,
		ACTION_TRAVEL,
		ACTION_AIR,
		ACTION_SLOW,
		ACTION_TIE_HIDE,
		ACTION_TIE_SHOW,
	};

private:
	double start = 0.0;
	Action action = ACTION_MOBY_HIDE;
	bool persistent = true;
	int spawn_id = -1;
	int level = -1;
	int flag_byte = 0;
	int flag_value = 1;
	Vector3 position;
	float radius = 4.0f;
	float strength = 0.1f;
	double length = 1.0;
	Color colour = Color(0, 0, 0);
	String cutscene;
	bool air_fog = true;
	float fog_near = 10.0f;
	float fog_far = 80.0f;
	float fog_thickness = 1.0f;
	bool air_background = false;
	Color background = Color(0, 0, 0);
	float stretch = 2.0f;
	int tie = 0;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_start(double p_seconds);
	double get_start() const;
	void set_action(Action p_action);
	Action get_action() const;
	void set_persistent(bool p_persistent);
	bool get_persistent() const;
	void set_spawn_id(int p_id);
	int get_spawn_id() const;
	void set_level(int p_level);
	int get_level() const;
	void set_flag_byte(int p_byte);
	int get_flag_byte() const;
	void set_flag_value(int p_value);
	int get_flag_value() const;
	void set_position(const Vector3 &p_position);
	Vector3 get_position() const;
	void set_radius(float p_radius);
	float get_radius() const;
	void set_strength(float p_strength);
	float get_strength() const;
	void set_length(double p_seconds);
	double get_length() const;
	void set_colour(const Color &p_colour);
	Color get_colour() const;
	void set_cutscene(const String &p_name);
	String get_cutscene() const;
	void set_air_fog(bool p_on);
	bool get_air_fog() const;
	void set_fog_near(float p_near);
	float get_fog_near() const;
	void set_fog_far(float p_far);
	float get_fog_far() const;
	void set_fog_thickness(float p_thickness);
	float get_fog_thickness() const;
	void set_air_background(bool p_on);
	bool get_air_background() const;
	void set_background(const Color &p_colour);
	Color get_background() const;
	void set_stretch(float p_stretch);
	float get_stretch() const;
	void set_tie(int p_tie);
	int get_tie() const;

	// The event as the file has it (`{"t": ..., "do": ...}`), at its tick.
	Dictionary to_event() const;
};

VARIANT_ENUM_CAST(Cutscene::TriggerKind);
VARIANT_ENUM_CAST(Cutscene::HeroMode);
VARIANT_ENUM_CAST(Cutscene::World);
// Lets go of the subtitle pictures' font: the module calls it as it closes.
void cutscene_free_font();

VARIANT_ENUM_CAST(Cutscene::Music);
VARIANT_ENUM_CAST(CutsceneActor::Source);
VARIANT_ENUM_CAST(CutsceneActor::End);
VARIANT_ENUM_CAST(CutsceneSound::Kind);
VARIANT_ENUM_CAST(CutsceneEvent::Action);
