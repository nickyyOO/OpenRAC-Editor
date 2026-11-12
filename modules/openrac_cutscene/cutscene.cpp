/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: the cutscene nodes and their export to a .cutscene file. */

#include "cutscene.h"

#include "cutscene_math.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/animation/animation_player.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/gui/editor_file_dialog.h"
#endif

namespace {

using cutscene_math::ticks;

// A number as the file keeps it: four decimals are a tenth of a millimetre in the game's units.
double tidy(double p_value) {
	return Math::round(p_value * 10000.0) / 10000.0;
}

Array vec3(const Vector3 &p_v) {
	Array a;
	a.push_back(tidy(p_v.x));
	a.push_back(tidy(p_v.y));
	a.push_back(tidy(p_v.z));
	return a;
}

// Godot's space to the game's: the level's Game node holds the game's frame (the extractor's
// conversion); without one, game (x, y, z) = Godot (x, z, -y).
Transform3D to_game_space(const Node *p_node) {
	for (const Node *n = p_node; n != nullptr; n = n->get_parent()) {
		const Node3D *game = Object::cast_to<Node3D>(n);
		if (game != nullptr && n->get_name() == StringName("Game") && game->is_inside_tree()) {
			return game->get_global_transform().affine_inverse();
		}
	}
	return Transform3D(Basis(1, 0, 0, 0, 0, -1, 0, 1, 0), Vector3());
}

// The timeline's animation and the node its tracks are relative to.
struct Timeline {
	Ref<Animation> anim;
	const Node *root = nullptr;

	int find(const Node *p_node, Animation::TrackType p_type, const String &p_property = String()) const {
		if (anim.is_null() || root == nullptr) {
			return -1;
		}
		String path = String(root->get_path_to(p_node));
		if (!p_property.is_empty()) {
			path += ":" + p_property;
		}
		return anim->find_track(NodePath(path), p_type);
	}

	// The tracks that move the node or change its picture: its place, turn and scale, and a
	// camera's field of view (not its animation or visibility, which are written as keys).
	Vector<int> tracks_of(const Node *p_node) const {
		Vector<int> out;
		if (anim.is_null() || root == nullptr) {
			return out;
		}
		const String own = String(root->get_path_to(p_node));
		for (int i = 0; i < anim->get_track_count(); i++) {
			const String path = String(anim->track_get_path(i));
			const Animation::TrackType type = anim->track_get_type(i);
			if (path == own && (type == Animation::TYPE_POSITION_3D || type == Animation::TYPE_ROTATION_3D || type == Animation::TYPE_SCALE_3D)) {
				out.push_back(i);
			} else if (type == Animation::TYPE_VALUE && path.begins_with(own + ":")) {
				const String property = path.substr(own.length() + 1);
				// The properties local_at reads.
				if (property == "position" || property == "rotation" || property == "quaternion" || property == "scale" || property == "fov") {
					out.push_back(i);
				}
			}
		}
		return out;
	}
};

// The node's own transform at `p_time` on the timeline: its keyed place, turn and scale, else its
// own.
Transform3D local_at(const Timeline &p_tl, const Node3D *p_node, double p_time) {
	Vector3 position = p_node->get_position();
	Quaternion rotation = p_node->get_quaternion();
	Vector3 scale = p_node->get_scale();
	int k = -1;
	if ((k = p_tl.find(p_node, Animation::TYPE_POSITION_3D)) >= 0) {
		position = p_tl.anim->position_track_interpolate(k, p_time);
	} else if ((k = p_tl.find(p_node, Animation::TYPE_VALUE, "position")) >= 0) {
		position = p_tl.anim->value_track_interpolate(k, p_time);
	}
	if ((k = p_tl.find(p_node, Animation::TYPE_ROTATION_3D)) >= 0) {
		rotation = p_tl.anim->rotation_track_interpolate(k, p_time);
	} else if ((k = p_tl.find(p_node, Animation::TYPE_VALUE, "quaternion")) >= 0) {
		rotation = p_tl.anim->value_track_interpolate(k, p_time);
	} else if ((k = p_tl.find(p_node, Animation::TYPE_VALUE, "rotation")) >= 0) {
		rotation = Basis::from_euler(p_tl.anim->value_track_interpolate(k, p_time), p_node->get_rotation_order()).get_rotation_quaternion();
	}
	if ((k = p_tl.find(p_node, Animation::TYPE_SCALE_3D)) >= 0) {
		scale = p_tl.anim->scale_track_interpolate(k, p_time);
	} else if ((k = p_tl.find(p_node, Animation::TYPE_VALUE, "scale")) >= 0) {
		scale = p_tl.anim->value_track_interpolate(k, p_time);
	}
	return Transform3D(Basis(rotation, scale), position);
}

// The node in the game's space at `p_time`: its parent's place now, its own keyed.
Transform3D game_at(const Timeline &p_tl, const Node3D *p_node, double p_time) {
	const Node3D *parent = p_node->get_parent_node_3d();
	const Transform3D parent_global = parent != nullptr && parent->is_inside_tree() ? parent->get_global_transform() : Transform3D();
	return to_game_space(p_node) * parent_global * local_at(p_tl, p_node, p_time);
}

// A property's value at `p_time`: keyed, else the node's own.
Variant value_at(const Timeline &p_tl, const Node *p_node, const String &p_property, double p_time) {
	const int k = p_tl.find(p_node, Animation::TYPE_VALUE, p_property);
	return k >= 0 ? p_tl.anim->value_track_interpolate(k, p_time) : p_node->get(p_property);
}

// The ticks to write keys at for a node: every tenth of a second, each key of its tracks, the end,
// and around each cut (the tick before it and its own).
Vector<int> sample_ticks(const Timeline &p_tl, const Node *p_node, int p_length, const Vector<int> &p_cuts) {
	const Vector<int> tracks = p_tl.tracks_of(p_node);
	Vector<int> out;
	if (tracks.is_empty()) {
		out.push_back(0);
		return out;
	}
	for (int t = 0; t < p_length; t += cutscene_math::SAMPLE_TICKS) {
		out.push_back(t);
	}
	out.push_back(p_length);
	for (int track : tracks) {
		for (int i = 0; i < p_tl.anim->track_get_key_count(track); i++) {
			out.push_back(CLAMP(ticks(p_tl.anim->track_get_key_time(track, i)), 0, p_length));
		}
	}
	for (int cut : p_cuts) {
		if (cut > 0 && cut <= p_length) {
			out.push_back(cut - 1);
			out.push_back(cut);
		}
	}
	return cutscene_math::sorted_unique(out);
}

String actor_id_of(const Node *p_from, const NodePath &p_path) {
	if (p_path.is_empty() || p_from == nullptr) {
		return String();
	}
	const CutsceneActor *actor = Object::cast_to<CutsceneActor>(p_from->get_node_or_null(p_path));
	return actor != nullptr ? actor->get_effective_id() : String();
}

// The level scene's folder and its level.json (number, disc version), when there is one.
String scene_dir_of(const Node *p_node) {
	const Node *root = p_node->get_owner() != nullptr ? p_node->get_owner() : p_node;
	const String path = root->get_scene_file_path();
	return path.is_empty() ? String() : path.get_base_dir();
}

Dictionary level_json(const String &p_dir) {
	if (p_dir.is_empty()) {
		return Dictionary();
	}
	// A scene of no level, or a level.json that is not one, is no error: the level is unknown.
	const String file = p_dir.path_join("level.json");
	if (!FileAccess::exists(file)) {
		return Dictionary();
	}
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(FileAccess::get_file_as_string(file)) != OK) {
		return Dictionary();
	}
	const Variant parsed = json->get_data();
	return parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
}

const char *const TRIGGER_NAMES[] = { "level_start", "volume", "talk", "pickup", "after", "flag", "manual" };
const char *const HERO_NAMES[] = { "held", "hidden", "actor" };
const char *const END_NAMES[] = { "keep", "restore", "remove" };
const char *const KIND_NAMES[] = { "voice", "music", "sfx" };

} // namespace

// ------------------------------------------------------------------------------------------------
// Cutscene

void Cutscene::set_file_name(const String &p_name) {
	file_name = p_name;
	update_configuration_warnings();
}
String Cutscene::get_file_name() const { return file_name; }
void Cutscene::set_title(const String &p_title) { title = p_title; }
String Cutscene::get_title() const { return title; }
void Cutscene::set_game(const String &p_game) { game = p_game; }
String Cutscene::get_game() const { return game; }
void Cutscene::set_length(double p_seconds) { length = MAX(0.0, p_seconds); }
double Cutscene::get_length() const { return length; }
void Cutscene::set_languages(const PackedStringArray &p_languages) { languages = p_languages; }
PackedStringArray Cutscene::get_languages() const { return languages; }
void Cutscene::set_trigger_kind(TriggerKind p_kind) {
	trigger_kind = p_kind;
	notify_property_list_changed();
	update_configuration_warnings();
}
Cutscene::TriggerKind Cutscene::get_trigger_kind() const { return trigger_kind; }
void Cutscene::set_trigger_once(bool p_once) { trigger_once = p_once; }
bool Cutscene::get_trigger_once() const { return trigger_once; }
void Cutscene::set_trigger_radius(float p_radius) { trigger_radius = MAX(0.0f, p_radius); }
float Cutscene::get_trigger_radius() const { return trigger_radius; }
void Cutscene::set_trigger_box(const Vector3 &p_size) { trigger_box = p_size.abs(); }
Vector3 Cutscene::get_trigger_box() const { return trigger_box; }
void Cutscene::set_trigger_spawn_id(int p_id) {
	trigger_spawn_id = p_id;
	update_configuration_warnings();
}
int Cutscene::get_trigger_spawn_id() const { return trigger_spawn_id; }
void Cutscene::set_trigger_after(const String &p_name) {
	trigger_after = p_name;
	update_configuration_warnings();
}
String Cutscene::get_trigger_after() const { return trigger_after; }
void Cutscene::set_trigger_flag_enabled(bool p_enabled) {
	trigger_flag_enabled = p_enabled;
	notify_property_list_changed();
}
bool Cutscene::get_trigger_flag_enabled() const { return trigger_flag_enabled; }
void Cutscene::set_trigger_flag(const Vector3i &p_flag) { trigger_flag = p_flag; }
Vector3i Cutscene::get_trigger_flag() const { return trigger_flag; }
void Cutscene::set_skippable(bool p_on) { skippable = p_on; }
bool Cutscene::get_skippable() const { return skippable; }
void Cutscene::set_letterbox(bool p_on) { letterbox = p_on; }
bool Cutscene::get_letterbox() const { return letterbox; }
void Cutscene::set_hide_hud(bool p_on) { hide_hud = p_on; }
bool Cutscene::get_hide_hud() const { return hide_hud; }
void Cutscene::set_world(World p_world) { world = p_world; }
Cutscene::World Cutscene::get_world() const { return world; }
void Cutscene::set_music(Music p_music) { music = p_music; }
Cutscene::Music Cutscene::get_music() const { return music; }
void Cutscene::set_hero_mode(HeroMode p_mode) { hero_mode = p_mode; }
Cutscene::HeroMode Cutscene::get_hero_mode() const { return hero_mode; }
void Cutscene::set_variants(const TypedArray<Dictionary> &p_variants) { variants = p_variants; }
TypedArray<Dictionary> Cutscene::get_variants() const { return variants; }

Callable Cutscene::get_set_up_action() {
	return callable_mp(this, &Cutscene::_set_up_timeline);
}

// Adds what a new cutscene needs: a timeline (an AnimationPlayer with an animation as long as the
// cutscene, 10 seconds when no length is set) and a camera looking at the cutscene's place.
void Cutscene::_set_up_timeline() {
	Node *owner = get_owner() != nullptr ? get_owner() : this;
	AnimationPlayer *player = get_timeline();
	if (player == nullptr) {
		player = memnew(AnimationPlayer);
		player->set_name("Timeline");
		add_child(player, true);
		player->set_owner(owner);
	}
	if (get_timeline_animation().is_null()) {
		Ref<AnimationLibrary> library;
		if (player->has_animation_library(StringName())) {
			library = player->get_animation_library(StringName());
		} else {
			library.instantiate();
			player->add_animation_library(StringName(), library);
		}
		Ref<Animation> anim;
		anim.instantiate();
		anim->set_length(length > 0.0 ? length : 10.0);
		library->add_animation(get_cutscene_id(), anim);
		player->set_assigned_animation(get_cutscene_id());
	}
	bool has_camera = false;
	for (int i = 0; i < get_child_count(); i++) {
		has_camera = has_camera || Object::cast_to<CutsceneCamera>(get_child(i)) != nullptr;
	}
	if (!has_camera) {
		CutsceneCamera *camera = memnew(CutsceneCamera);
		camera->set_name("Camera");
		add_child(camera, true);
		camera->set_owner(owner);
		// Behind the cutscene's place and above it, looking at it level with the game's ground
		// (z up in the game, whatever frame the level is in).
		const Basis from_game = to_game_space(this).basis.inverse();
		if (is_inside_tree()) {
			const Vector3 centre = get_global_position();
			camera->look_at_from_position(centre + from_game.xform(Vector3(-6, 0, 2)), centre, from_game.xform(Vector3(0, 0, 1)).normalized());
		} else {
			camera->set_position(Vector3(0, 2, 6));
		}
	}
	update_configuration_warnings();
}

Callable Cutscene::get_export_action() {
	return callable_mp(this, &Cutscene::_export_pressed);
}

void Cutscene::_export_pressed() {
	String path;
	const Error err = export_cutscene(&path);
	if (err == OK) {
		print_line(vformat("Cutscene \"%s\" written to %s", get_cutscene_id(), path));
	}
}

String Cutscene::get_cutscene_id() const {
	const String id = file_name.strip_edges();
	return (id.is_empty() ? String(get_name()).to_snake_case() : id).validate_filename();
}

AnimationPlayer *Cutscene::get_timeline() const {
	for (int i = 0; i < get_child_count(); i++) {
		if (AnimationPlayer *player = Object::cast_to<AnimationPlayer>(get_child(i))) {
			return player;
		}
	}
	return nullptr;
}

Ref<Animation> Cutscene::get_timeline_animation() const {
	AnimationPlayer *player = get_timeline();
	if (player == nullptr) {
		return Ref<Animation>();
	}
	const StringName own = get_cutscene_id();
	if (player->has_animation(own)) {
		return player->get_animation(own);
	}
	LocalVector<StringName> names;
	player->get_animation_list(&names);
	for (const StringName &name : names) {
		if (name != StringName("RESET")) {
			return player->get_animation(name);
		}
	}
	return Ref<Animation>();
}

Dictionary Cutscene::to_cutscene(PackedStringArray *r_problems) const {
	PackedStringArray problems;
	Timeline tl;
	tl.anim = get_timeline_animation();
	if (AnimationPlayer *player = get_timeline()) {
		tl.root = player->get_node_or_null(player->get_root_node());
	}
	const double seconds = length > 0.0 ? length : (tl.anim.is_valid() ? double(tl.anim->get_length()) : 0.0);
	const int length_ticks = MAX(1, ticks(seconds));
	const String dir = scene_dir_of(this);
	const Dictionary level_info = level_json(dir);
	const Transform3D to_game = to_game_space(this);
	const Transform3D here = to_game * (is_inside_tree() ? get_global_transform() : get_transform());

	Dictionary out;
	out["format"] = "openrac-cutscene";
	out["version"] = 1;
	out["game"] = game;
	int level = level_info.get("level", -1);
	if (level < 0) {
		// A project per level keeps it in the folder's name, level_NN.
		const String folder = dir.get_file();
		level = folder.begins_with("level_") ? folder.substr(6).to_int() : -1;
	}
	out["level"] = level;
	out["name"] = title.is_empty() ? String(get_name()) : title;
	out["length"] = length_ticks;
	PackedStringArray langs = languages;
	if (langs.is_empty()) {
		for (const String &code : cutscene_math::disc_languages(level_info.get("version", ""))) {
			langs.push_back(code);
		}
	}
	out["languages"] = langs;

	// The trigger.
	Dictionary trigger;
	trigger["kind"] = TRIGGER_NAMES[trigger_kind];
	trigger["once"] = trigger_once;
	switch (trigger_kind) {
		case TRIGGER_AREA:
			if (trigger_box != Vector3()) {
				Dictionary box;
				box["min"] = vec3(here.origin - trigger_box * 0.5);
				box["max"] = vec3(here.origin + trigger_box * 0.5);
				trigger["box"] = box;
			} else {
				Dictionary sphere;
				sphere["position"] = vec3(here.origin);
				sphere["radius"] = tidy(trigger_radius);
				trigger["sphere"] = sphere;
			}
			break;
		case TRIGGER_TALK:
			trigger["spawn_id"] = trigger_spawn_id;
			trigger["radius"] = tidy(trigger_radius);
			if (trigger_spawn_id < 0) {
				problems.push_back("the talk trigger has no spawn id");
			}
			break;
		case TRIGGER_PICKUP:
			trigger["spawn_id"] = trigger_spawn_id;
			if (trigger_spawn_id < 0) {
				problems.push_back("the pickup trigger has no spawn id");
			}
			break;
		case TRIGGER_AFTER:
			trigger["cutscene"] = trigger_after;
			if (trigger_after.is_empty()) {
				problems.push_back("the after trigger names no cutscene");
			}
			break;
		default:
			break;
	}
	if (trigger_flag_enabled || trigger_kind == TRIGGER_FLAG) {
		Dictionary flag;
		flag["level"] = trigger_flag.x < 0 ? level : trigger_flag.x;
		flag["byte"] = trigger_flag.y;
		flag["is"] = trigger_flag.z;
		trigger["flag"] = flag;
	}
	out["trigger"] = trigger;

	Dictionary options;
	options["skippable"] = skippable;
	options["letterbox"] = letterbox;
	options["hide_hud"] = hide_hud;
	options["world"] = world == WORLD_HOLD ? "hold" : "run";
	options["music"] = music == MUSIC_KEEP ? "keep" : (music == MUSIC_STOP ? "stop" : "duck");
	out["options"] = options;

	Dictionary hero;
	hero["mode"] = HERO_NAMES[hero_mode];
	if (const Node3D *end = Object::cast_to<Node3D>(get_node_or_null(NodePath("HeroEnd")))) {
		const Transform3D at = to_game * end->get_global_transform();
		Dictionary place;
		place["position"] = vec3(at.origin);
		const Vector3 facing = at.basis.get_column(0);
		place["yaw"] = tidy(Math::atan2(facing.y, facing.x));
		hero["end"] = place;
	}
	out["hero"] = hero;

	// The camera, the actors, the lines, the sounds and the events: children in their order.
	Array actors;
	Array subtitles;
	Array sounds;
	Array events;
	Dictionary camera;
	HashSet<String> ids;
	for (int i = 0; i < get_child_count(); i++) {
		Node *child = get_child(i);
		if (const CutsceneCamera *cam = Object::cast_to<CutsceneCamera>(child)) {
			if (camera.has("keys")) {
				problems.push_back(vformat("%s: a second camera is left out", cam->get_name()));
				continue;
			}
			Vector<int> cuts;
			for (float c : cam->get_cuts()) {
				cuts.push_back(ticks(c));
			}
			cuts = cutscene_math::sorted_unique(cuts);
			Array keys;
			for (int t : sample_ticks(tl, cam, length_ticks, cuts)) {
				const double time = t / cutscene_math::TICK_RATE;
				const Transform3D at = game_at(tl, cam, time);
				const Vector3 rot = cutscene_math::camera_rotation(-at.basis.get_column(2), at.basis.get_column(1));
				Dictionary key;
				key["t"] = t;
				key["eye"] = vec3(at.origin);
				key["rotation"] = vec3(rot);
				key["ease"] = "linear";
				if (cuts.has(t)) {
					key["cut"] = true;
				}
				if (cam->get_export_fov()) {
					key["fov"] = tidy(double(value_at(tl, cam, "fov", time)));
				}
				keys.push_back(key);
			}
			camera["keys"] = keys;
			camera["end"] = cam->get_ease_back() ? "ease" : "cut";
		} else if (const CutsceneActor *actor = Object::cast_to<CutsceneActor>(child)) {
			const String id = actor->get_effective_id();
			if (ids.has(id)) {
				problems.push_back(vformat("two actors are called \"%s\": the second is left out", id));
				continue;
			}
			ids.insert(id);
			Dictionary a;
			a["id"] = id;
			Dictionary source;
			switch (actor->get_source()) {
				case CutsceneActor::SOURCE_LEVEL_MOBY:
					source["spawn_id"] = actor->get_spawn_id();
					if (actor->get_moby_class() >= 0) {
						source["class"] = actor->get_moby_class();
					}
					if (actor->get_spawn_id() < 0) {
						problems.push_back(vformat("actor \"%s\" names no moby (spawn id)", id));
					}
					a["missing"] = actor->get_spawn_if_missing() ? "spawn" : "skip";
					break;
				case CutsceneActor::SOURCE_NEW_MOBY:
					source["class"] = actor->get_moby_class();
					if (actor->get_moby_class() < 0) {
						problems.push_back(vformat("actor \"%s\" names no moby class", id));
					}
					break;
				case CutsceneActor::SOURCE_HERO_FIGURE:
					source["figure"] = "hero";
					break;
				case CutsceneActor::SOURCE_PROP:
					source["prop"] = actor->get_prop_path();
					if (actor->get_prop_path().is_empty()) {
						problems.push_back(vformat("actor \"%s\" has no mesh", id));
					}
					break;
			}
			a["source"] = source;
			Array track;
			for (int t : sample_ticks(tl, actor, length_ticks, Vector<int>())) {
				const Transform3D at = game_at(tl, actor, t / cutscene_math::TICK_RATE);
				Dictionary key;
				key["t"] = t;
				key["position"] = vec3(at.origin);
				key["rotation"] = vec3(at.basis.orthonormalized().get_euler(EulerOrder::XYZ));
				const Vector3 s = at.basis.get_scale();
				key["scale"] = tidy((s.x + s.y + s.z) / 3.0);
				track.push_back(key);
			}
			a["track"] = track;
			// Animations: each key of `sequence` starts one, at the speed `anim_speed` has then.
			Array anims;
			const int seq_track = tl.find(actor, Animation::TYPE_VALUE, "sequence");
			if (seq_track >= 0) {
				for (int k = 0; k < tl.anim->track_get_key_count(seq_track); k++) {
					const double time = tl.anim->track_get_key_time(seq_track, k);
					const int seq = tl.anim->track_get_key_value(seq_track, k);
					if (seq < 0) {
						continue;
					}
					Dictionary e;
					e["t"] = CLAMP(ticks(time), 0, length_ticks);
					e["seq"] = seq;
					e["speed"] = tidy(double(value_at(tl, actor, "anim_speed", time)));
					e["blend"] = actor->get_blend();
					anims.push_back(e);
				}
			} else if (actor->get_sequence() >= 0) {
				Dictionary e;
				e["t"] = 0;
				e["seq"] = actor->get_sequence();
				e["speed"] = tidy(actor->get_anim_speed());
				e["blend"] = actor->get_blend();
				anims.push_back(e);
			}
			if (!anims.is_empty()) {
				a["anims"] = anims;
			}
			Array visible;
			const int vis_track = tl.find(actor, Animation::TYPE_VALUE, "visible");
			if (vis_track >= 0) {
				for (int k = 0; k < tl.anim->track_get_key_count(vis_track); k++) {
					Dictionary e;
					e["t"] = CLAMP(ticks(tl.anim->track_get_key_time(vis_track, k)), 0, length_ticks);
					e["on"] = bool(tl.anim->track_get_key_value(vis_track, k));
					visible.push_back(e);
				}
			} else if (!actor->is_visible()) {
				Dictionary e;
				e["t"] = 0;
				e["on"] = false;
				visible.push_back(e);
			}
			if (!visible.is_empty()) {
				a["visible"] = visible;
			}
			a["end"] = END_NAMES[actor->get_end()];
			actors.push_back(a);
		} else if (const CutsceneSubtitle *line = Object::cast_to<CutsceneSubtitle>(child)) {
			Dictionary s;
			s["t"] = ticks(line->get_start());
			s["end"] = ticks(line->get_start() + line->get_duration());
			Dictionary text;
			const Dictionary given = line->get_text();
			for (const Variant *key = given.next(nullptr); key != nullptr; key = given.next(key)) {
				const String words = String(given[*key]).strip_edges();
				if (!words.is_empty()) {
					text[String(*key)] = words;
				}
			}
			if (text.is_empty()) {
				problems.push_back(vformat("%s: a subtitle with no text is left out", line->get_name()));
				continue;
			}
			if (int(s["end"]) <= int(s["t"])) {
				problems.push_back(vformat("%s: a subtitle with no duration is never shown", line->get_name()));
			}
			for (const String &code : langs) {
				if (!text.has(code)) {
					problems.push_back(vformat("%s: no %s text (the first language shows)", line->get_name(), code));
				}
			}
			s["text"] = text;
			const String speaker = actor_id_of(line, line->get_speaker());
			if (!speaker.is_empty()) {
				s["actor"] = speaker;
			}
			subtitles.push_back(s);
		} else if (const CutsceneSound *sound = Object::cast_to<CutsceneSound>(child)) {
			Dictionary s;
			s["t"] = ticks(sound->get_start());
			s["kind"] = KIND_NAMES[sound->get_kind()];
			if (sound->get_game_sound() >= 0) {
				s["game_sound"] = sound->get_game_sound();
			} else if (!sound->get_files().is_empty()) {
				s["file"] = sound->get_files();
			} else if (!sound->get_file().is_empty()) {
				s["file"] = sound->get_file();
			} else {
				problems.push_back(vformat("%s: a sound with no file is left out", sound->get_name()));
				continue;
			}
			const String from = actor_id_of(sound, sound->get_source());
			if (!from.is_empty()) {
				s["actor"] = from;
			}
			s["volume"] = tidy(sound->get_volume());
			if (sound->get_fade_in() > 0.0) {
				s["fade_in"] = ticks(sound->get_fade_in());
			}
			if (sound->get_fade_out() > 0.0) {
				s["fade_out"] = ticks(sound->get_fade_out());
			}
			if (sound->get_stop() > 0.0) {
				s["end"] = ticks(sound->get_stop());
			}
			sounds.push_back(s);
		} else if (const CutsceneEvent *event = Object::cast_to<CutsceneEvent>(child)) {
			Dictionary e = event->to_event();
			if (e.has("level") && int(e["level"]) < 0) {
				e["level"] = level;
			}
			events.push_back(e);
		}
	}
	out["camera"] = camera;
	out["actors"] = actors;
	out["subtitles"] = subtitles;
	out["sounds"] = sounds;
	out["events"] = events;
	if (!variants.is_empty()) {
		out["variants"] = variants;
	}
	if (tl.anim.is_null()) {
		problems.push_back("no timeline (an AnimationPlayer child): nothing moves");
	}
	if (r_problems != nullptr) {
		*r_problems = problems;
	}
	return out;
}

Error Cutscene::export_cutscene(String *r_path) const {
	const String dir = scene_dir_of(this);
	ERR_FAIL_COND_V_MSG(dir.is_empty(), ERR_UNCONFIGURED, "Save the level first: the cutscene is written beside its scene.");
	PackedStringArray problems;
	const Dictionary cutscene_data = to_cutscene(&problems);
	for (const String &problem : problems) {
		WARN_PRINT(vformat("Cutscene \"%s\": %s", get_cutscene_id(), problem));
	}
	const String folder = dir.path_join("cutscenes");
	const Error made = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(folder));
	ERR_FAIL_COND_V_MSG(made != OK && made != ERR_ALREADY_EXISTS, made, "Cannot make " + folder);
	const String path = folder.path_join(get_cutscene_id() + ".cutscene");
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(file.is_null(), err, "Cannot write " + path);
	file->store_string(JSON::stringify(cutscene_data, "  ", false) + "\n");
	if (r_path != nullptr) {
		*r_path = path;
	}
	return OK;
}

// ------------------------------------------------------------------------------------------------
// Import: a .cutscene as nodes

namespace {

int name_index(const char *const *p_names, int p_count, const String &p_name, int p_fallback) {
	for (int i = 0; i < p_count; i++) {
		if (p_name == p_names[i]) {
			return i;
		}
	}
	return p_fallback;
}

// [x, y, z] from the file, else the fallback.
Vector3 vec3_of(const Variant &p_value, const Vector3 &p_fallback = Vector3()) {
	if (p_value.get_type() != Variant::ARRAY) {
		return p_fallback;
	}
	const Array a = p_value;
	if (a.size() < 3) {
		return p_fallback;
	}
	return Vector3(a[0], a[1], a[2]);
}

// A camera key's picture as a Camera3D's basis in the game's space: [roll, pitch, yaw] (or a
// point to look at), the camera looking along -z with y up, as the export reads it back.
Basis camera_basis(const Vector3 &p_forward, double p_roll) {
	const Vector3 f = p_forward.normalized();
	Vector3 left = Vector3(0, 0, 1).cross(f);
	left = left.length() > 1e-6 ? left.normalized() : Vector3(0, 1, 0);
	Vector3 up = f.cross(left).normalized();
	const double c = Math::cos(p_roll), s = Math::sin(p_roll);
	const Vector3 l2 = left * c + up * s;
	up = up * c - left * s;
	left = l2;
	return Basis(-left, up, -f);
}

Vector3 forward_of(const Vector3 &p_rotation) {
	const double cp = Math::cos(p_rotation.y);
	return Vector3(Math::cos(p_rotation.z) * cp, Math::sin(p_rotation.z) * cp, -Math::sin(p_rotation.y));
}

// The animation's track of `p_type` for `p_path`, made if it is not there.
int track_for(const Ref<Animation> &p_anim, const NodePath &p_path, Animation::TrackType p_type) {
	int track = p_anim->find_track(p_path, p_type);
	if (track < 0) {
		track = p_anim->add_track(p_type);
		p_anim->track_set_path(track, p_path);
	}
	return track;
}

int discrete_track(const Ref<Animation> &p_anim, const NodePath &p_path) {
	const int track = track_for(p_anim, p_path, Animation::TYPE_VALUE);
	p_anim->value_track_set_update_mode(track, Animation::UPDATE_DISCRETE);
	return track;
}

} // namespace

Callable Cutscene::get_import_action() {
	return callable_mp(this, &Cutscene::_import_pressed);
}

void Cutscene::_import_pressed() {
#ifdef TOOLS_ENABLED
	EditorFileDialog *dialog = memnew(EditorFileDialog);
	dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	dialog->add_filter("*.cutscene", "Cutscene");
	dialog->set_title("Import a .cutscene into this cutscene");
	const String dir = scene_dir_of(this);
	if (!dir.is_empty()) {
		dialog->set_current_dir(ProjectSettings::get_singleton()->globalize_path(dir.path_join("cutscenes")));
	}
	ObjectID self = get_instance_id();
	dialog->connect("file_selected", callable_mp_static(+[](ObjectID p_self, const String &p_path) {
		Cutscene *cutscene = ObjectDB::get_instance<Cutscene>(p_self);
		if (cutscene == nullptr) {
			return;
		}
		PackedStringArray problems;
		if (cutscene->import_cutscene(p_path, &problems) == OK) {
			print_line(vformat("Cutscene \"%s\" imported from %s.", cutscene->get_cutscene_id(), p_path));
		}
		for (const String &problem : problems) {
			WARN_PRINT(vformat("Import of %s: %s", p_path.get_file(), problem));
		}
	}).bind(self));
	dialog->connect("visibility_changed", callable_mp_static(+[](EditorFileDialog *p_dialog) {
		if (!p_dialog->is_visible()) {
			p_dialog->queue_free();
		}
	}).bind(dialog));
	EditorInterface::get_singleton()->popup_dialog_centered_ratio(dialog, 0.6);
#endif
}

Error Cutscene::import_cutscene(const String &p_path, PackedStringArray *r_problems) {
	ERR_FAIL_COND_V_MSG(!FileAccess::exists(p_path), ERR_FILE_NOT_FOUND, "No file " + p_path);
	Ref<JSON> json;
	json.instantiate();
	const Error parsed = json->parse(FileAccess::get_file_as_string(p_path));
	ERR_FAIL_COND_V_MSG(parsed != OK, ERR_PARSE_ERROR, vformat("%s is not JSON: %s (line %d)", p_path, json->get_error_message(), json->get_error_line() + 1));
	const Variant read = json->get_data();
	ERR_FAIL_COND_V_MSG(read.get_type() != Variant::DICTIONARY || Dictionary(read).get("format", "") != "openrac-cutscene", ERR_INVALID_DATA, p_path + " is not a .cutscene");
	for (int i = 0; i < get_child_count(); i++) {
		Node *child = get_child(i);
		const bool taken = Object::cast_to<CutsceneCamera>(child) || Object::cast_to<CutsceneActor>(child) || Object::cast_to<CutsceneSubtitle>(child) ||
				Object::cast_to<CutsceneSound>(child) || Object::cast_to<CutsceneEvent>(child);
		ERR_FAIL_COND_V_MSG(taken, ERR_ALREADY_IN_USE, "Import into a new, empty cutscene: this one has a camera, actors, lines, sounds or events already.");
	}
	if (file_name.is_empty()) {
		set_file_name(p_path.get_file().get_basename());
	}
	const PackedStringArray problems = from_cutscene(read);
	if (r_problems != nullptr) {
		*r_problems = problems;
	}
	return OK;
}

PackedStringArray Cutscene::from_cutscene(const Dictionary &p_data) {
	PackedStringArray problems;
	Node *owner = get_owner() != nullptr ? get_owner() : this;
	const int file_level = p_data.get("level", -1);

	// What the cutscene is and how it starts.
	const String name = p_data.get("name", "");
	if (!name.is_empty() && name != String(get_name())) {
		set_title(name);
	}
	if (p_data.get("game", Variant()).get_type() == Variant::STRING) {
		set_game(p_data["game"]);
	}
	const int length_ticks = MAX(1, int(p_data.get("length", 1)));
	set_length(length_ticks / cutscene_math::TICK_RATE);
	if (p_data.get("languages", Variant()).get_type() == Variant::ARRAY) {
		const PackedStringArray codes = p_data["languages"];
		set_languages(codes);
	}
	const Dictionary trigger = p_data.get("trigger", Dictionary());
	set_trigger_kind(TriggerKind(name_index(TRIGGER_NAMES, std::size(TRIGGER_NAMES), trigger.get("kind", ""), TRIGGER_MANUAL)));
	set_trigger_once(trigger.get("once", true));
	// A volume's place is the cutscene's own.
	bool placed = false;
	Vector3 centre;
	if (trigger.get("box", Variant()).get_type() == Variant::DICTIONARY) {
		const Dictionary box = trigger["box"];
		const Vector3 a = vec3_of(box.get("min", Variant())), b = vec3_of(box.get("max", Variant()));
		centre = (a + b) * 0.5;
		set_trigger_box((b - a).abs());
		placed = true;
	} else if (trigger.get("sphere", Variant()).get_type() == Variant::DICTIONARY) {
		const Dictionary sphere = trigger["sphere"];
		centre = vec3_of(sphere.get("position", Variant()));
		set_trigger_radius(sphere.get("radius", 4.0));
		placed = true;
	}
	if (trigger.has("radius")) {
		set_trigger_radius(trigger["radius"]);
	}
	if (trigger.has("spawn_id")) {
		set_trigger_spawn_id(trigger["spawn_id"]);
	}
	if (trigger.has("cutscene")) {
		set_trigger_after(trigger["cutscene"]);
	}
	if (trigger.get("flag", Variant()).get_type() == Variant::DICTIONARY) {
		const Dictionary flag = trigger["flag"];
		const int level = flag.get("level", -1);
		set_trigger_flag(Vector3i(level == file_level ? -1 : level, flag.get("byte", 0), flag.get("is", 1)));
		set_trigger_flag_enabled(true);
	}
	if (placed) {
		const Node3D *parent = get_parent_node_3d();
		const Transform3D parent_game = to_game_space(this) * (parent != nullptr && parent->is_inside_tree() ? parent->get_global_transform() : Transform3D());
		set_position(parent_game.affine_inverse().xform(centre));
	}

	const Dictionary options = p_data.get("options", Dictionary());
	set_skippable(options.get("skippable", true));
	set_letterbox(options.get("letterbox", true));
	set_hide_hud(options.get("hide_hud", true));
	set_world(String(options.get("world", "run")) == "hold" ? WORLD_HOLD : WORLD_RUN);
	const String music_name = options.get("music", "duck");
	set_music(music_name == "keep" ? MUSIC_KEEP : (music_name == "stop" ? MUSIC_STOP : MUSIC_DUCK));
	const Dictionary hero = p_data.get("hero", Dictionary());
	set_hero_mode(HeroMode(name_index(HERO_NAMES, std::size(HERO_NAMES), hero.get("mode", "held"), HERO_HELD)));
	if (p_data.get("variants", Variant()).get_type() == Variant::ARRAY) {
		TypedArray<Dictionary> list;
		for (const Variant &v : Array(p_data["variants"])) {
			if (v.get_type() == Variant::DICTIONARY) {
				list.push_back(v);
			}
		}
		set_variants(list);
	}

	// The timeline, as long as the cutscene.
	AnimationPlayer *player = get_timeline();
	if (player == nullptr) {
		player = memnew(AnimationPlayer);
		player->set_name("Timeline");
		add_child(player, true);
		player->set_owner(owner);
	}
	Ref<Animation> anim = get_timeline_animation();
	if (anim.is_null()) {
		Ref<AnimationLibrary> library;
		if (player->has_animation_library(StringName())) {
			library = player->get_animation_library(StringName());
		} else {
			library.instantiate();
			player->add_animation_library(StringName(), library);
		}
		anim.instantiate();
		library->add_animation(get_cutscene_id(), anim);
		player->set_assigned_animation(get_cutscene_id());
	}
	anim->set_length(length_ticks / cutscene_math::TICK_RATE);
	const Node *root = player->get_node_or_null(player->get_root_node());
	if (root == nullptr) {
		root = this;
	}
	// The game's space to this node's children's.
	const Transform3D from_game = (to_game_space(this) * (is_inside_tree() ? get_global_transform() : get_transform())).affine_inverse();
	auto add = [&](Node *p_node, const String &p_name) {
		p_node->set_name(p_name.validate_node_name());
		add_child(p_node, true);
		p_node->set_owner(owner);
	};
	auto seconds = [](const Variant &p_ticks) { return int(p_ticks) / cutscene_math::TICK_RATE; };
	HashMap<String, String> actor_nodes; // actor id -> node name

	// The actors first, so the camera, lines and sounds can name them.
	for (const Variant &item : Array(p_data.get("actors", Array()))) {
		if (item.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary a = item;
		const String id = a.get("id", "");
		CutsceneActor *actor = memnew(CutsceneActor);
		actor->set_actor_id(id);
		const Dictionary source = a.get("source", Dictionary());
		if (source.has("spawn_id")) {
			actor->set_source(CutsceneActor::SOURCE_LEVEL_MOBY);
			actor->set_spawn_id(source["spawn_id"]);
			actor->set_spawn_if_missing(String(a.get("missing", "spawn")) == "spawn");
		} else if (source.has("class")) {
			actor->set_source(CutsceneActor::SOURCE_NEW_MOBY);
		} else if (source.has("figure")) {
			actor->set_source(CutsceneActor::SOURCE_HERO_FIGURE);
		} else if (source.has("prop")) {
			actor->set_source(CutsceneActor::SOURCE_PROP);
			actor->set_prop_path(source["prop"]);
		}
		if (source.has("class")) {
			actor->set_moby_class(source["class"]);
		}
		actor->set_end(CutsceneActor::End(name_index(END_NAMES, std::size(END_NAMES), a.get("end", "keep"), CutsceneActor::END_KEEP)));
		add(actor, id.is_empty() ? String("Actor") : id.to_pascal_case());
		actor_nodes[id] = actor->get_name();
		const NodePath path = root->get_path_to(actor);

		// Its place: one key is where it stands, more are its track.
		const Array track = a.get("track", Array());
		Vector3 pos, rot;
		double scale = 1.0;
		for (int k = 0; k < track.size(); k++) {
			const Dictionary key = track[k];
			pos = vec3_of(key.get("position", Variant()), pos);
			rot = vec3_of(key.get("rotation", Variant()), rot);
			scale = key.get("scale", scale);
			const Transform3D local = from_game * Transform3D(Basis::from_euler(rot, EulerOrder::XYZ).scaled(Vector3(scale, scale, scale)), pos);
			if (k == 0) {
				actor->set_transform(local);
			}
			if (track.size() > 1) {
				const double time = seconds(key.get("t", 0));
				anim->position_track_insert_key(track_for(anim, path, Animation::TYPE_POSITION_3D), time, local.origin);
				anim->rotation_track_insert_key(track_for(anim, path, Animation::TYPE_ROTATION_3D), time, local.basis.get_rotation_quaternion());
				anim->scale_track_insert_key(track_for(anim, path, Animation::TYPE_SCALE_3D), time, local.basis.get_scale());
			}
		}
		const Array anims = a.get("anims", Array());
		for (int k = 0; k < anims.size(); k++) {
			const Dictionary e = anims[k];
			const double time = seconds(e.get("t", 0));
			if (anims.size() == 1 && int(e.get("t", 0)) == 0) {
				actor->set_sequence(e.get("seq", -1));
				actor->set_anim_speed(e.get("speed", 1.0));
			} else {
				anim->track_insert_key(discrete_track(anim, NodePath(String(path) + ":sequence")), time, int(e.get("seq", -1)));
				anim->track_insert_key(discrete_track(anim, NodePath(String(path) + ":anim_speed")), time, double(e.get("speed", 1.0)));
			}
			actor->set_blend(e.get("blend", 4));
			if (e.has("frame") && int(e["frame"]) != 0) {
				problems.push_back(vformat("actor \"%s\": an animation's start frame is not kept", id));
			}
		}
		const Array visible = a.get("visible", Array());
		if (visible.size() == 1 && int(Dictionary(visible[0]).get("t", 0)) == 0) {
			actor->set_visible(Dictionary(visible[0]).get("on", true));
		} else {
			for (const Variant &v : visible) {
				const Dictionary e = v;
				anim->track_insert_key(discrete_track(anim, NodePath(String(path) + ":visible")), seconds(e.get("t", 0)), bool(e.get("on", true)));
			}
		}
		for (const char *part : { "path", "look_at", "held", "layers" }) {
			if (a.has(part)) {
				problems.push_back(vformat("actor \"%s\": its \"%s\" is not kept", id, part));
			}
		}
	}

	// The camera: its keys as its place and turn on the timeline, its cuts.
	const Dictionary camera = p_data.get("camera", Dictionary());
	const Array keys = camera.get("keys", Array());
	if (!keys.is_empty()) {
		CutsceneCamera *cam = memnew(CutsceneCamera);
		cam->set_ease_back(String(camera.get("end", "ease")) != "cut");
		add(cam, "Camera");
		const NodePath path = root->get_path_to(cam);
		const int pos_track = track_for(anim, path, Animation::TYPE_POSITION_3D);
		const int rot_track = track_for(anim, path, Animation::TYPE_ROTATION_3D);
		PackedFloat32Array cuts;
		Vector3 eye, forward(1, 0, 0);
		double roll = 0.0;
		Transform3D before;
		int before_t = -1;
		bool any_fov = false, smooth = false;
		for (int k = 0; k < keys.size(); k++) {
			const Dictionary key = keys[k];
			const int t = key.get("t", 0);
			eye = vec3_of(key.get("eye", Variant()), eye);
			const Variant look = key.get("look_at", Variant());
			if (look.get_type() == Variant::ARRAY) {
				const Vector3 at = vec3_of(look);
				forward = (at - eye).length() > 1e-6 ? (at - eye).normalized() : forward;
			} else if (key.has("rotation")) {
				const Vector3 rot = vec3_of(key["rotation"]);
				forward = forward_of(rot);
				roll = rot.x;
			}
			if (look.get_type() == Variant::DICTIONARY) {
				problems.push_back(vformat("camera key at %d: looking at an actor is kept as the key's turn", t));
			}
			roll = key.get("roll", roll);
			const Transform3D local = from_game * Transform3D(camera_basis(forward, roll), eye);
			if (bool(key.get("cut", false))) {
				cuts.push_back(t / cutscene_math::TICK_RATE);
				// Up to a cut the picture holds.
				if (before_t >= 0 && t - 1 > before_t) {
					anim->position_track_insert_key(pos_track, (t - 1) / cutscene_math::TICK_RATE, before.origin);
					anim->rotation_track_insert_key(rot_track, (t - 1) / cutscene_math::TICK_RATE, before.basis.get_rotation_quaternion());
				}
			}
			const String ease = key.get("ease", "smooth");
			smooth = smooth || ease == "smooth";
			if (ease == "bezier") {
				problems.push_back(vformat("camera key at %d: bezier handles are kept as a smooth curve", t));
			}
			anim->position_track_insert_key(pos_track, t / cutscene_math::TICK_RATE, local.origin);
			anim->rotation_track_insert_key(rot_track, t / cutscene_math::TICK_RATE, local.basis.get_rotation_quaternion());
			if (key.has("fov")) {
				any_fov = true;
				anim->track_insert_key(track_for(anim, NodePath(String(path) + ":fov"), Animation::TYPE_VALUE), t / cutscene_math::TICK_RATE, double(key["fov"]));
			}
			if (k == 0) {
				cam->set_transform(local);
			}
			before = local;
			before_t = t;
		}
		if (smooth) {
			anim->track_set_interpolation_type(pos_track, Animation::INTERPOLATION_CUBIC);
		}
		cam->set_cuts(cuts);
		cam->set_export_fov(any_fov);
		if (camera.has("shake")) {
			problems.push_back("the camera's shake is not kept");
		}
	}

	// The lines, sounds and events.
	auto actor_path = [&](const Variant &p_id) {
		const String id = p_id;
		return actor_nodes.has(id) ? NodePath("../" + actor_nodes[id]) : NodePath();
	};
	int count = 0;
	for (const Variant &item : Array(p_data.get("subtitles", Array()))) {
		const Dictionary s = item;
		CutsceneSubtitle *line = memnew(CutsceneSubtitle);
		const int t = s.get("t", 0);
		line->set_start(t / cutscene_math::TICK_RATE);
		line->set_duration((int(s.get("end", t + 120)) - t) / cutscene_math::TICK_RATE);
		const Variant text = s.get("text", Variant());
		if (text.get_type() == Variant::DICTIONARY) {
			line->set_text(text);
		} else {
			Dictionary one;
			one[languages.is_empty() ? String("en") : languages[0]] = String(text);
			line->set_text(one);
		}
		add(line, vformat("Line%d", ++count));
		line->set_speaker(actor_path(s.get("actor", "")));
	}
	count = 0;
	for (const Variant &item : Array(p_data.get("sounds", Array()))) {
		const Dictionary s = item;
		CutsceneSound *sound = memnew(CutsceneSound);
		sound->set_start(seconds(s.get("t", 0)));
		sound->set_kind(CutsceneSound::Kind(name_index(KIND_NAMES, std::size(KIND_NAMES), s.get("kind", "voice"), CutsceneSound::KIND_VOICE)));
		if (s.has("game_sound")) {
			sound->set_game_sound(s["game_sound"]);
		}
		const Variant file = s.get("file", Variant());
		if (file.get_type() == Variant::DICTIONARY) {
			sound->set_files(file);
		} else if (file.get_type() == Variant::STRING) {
			sound->set_file(file);
		}
		sound->set_volume(s.get("volume", 1.0));
		sound->set_fade_in(seconds(s.get("fade_in", 0)));
		sound->set_fade_out(seconds(s.get("fade_out", 0)));
		sound->set_stop(seconds(s.get("end", 0)));
		add(sound, vformat("Sound%d", ++count));
		sound->set_source(actor_path(s.get("actor", "")));
	}
	count = 0;
	for (const Variant &item : Array(p_data.get("events", Array()))) {
		const Dictionary e = item;
		const String what = e.get("do", "");
		CutsceneEvent *event = memnew(CutsceneEvent);
		event->set_start(seconds(e.get("t", 0)));
		event->set_persistent(e.get("persistent", false));
		if (what == "moby_hide") {
			event->set_action(bool(e.get("on", true)) ? CutsceneEvent::ACTION_MOBY_HIDE : CutsceneEvent::ACTION_MOBY_SHOW);
		} else if (what == "moby_kill") {
			event->set_action(CutsceneEvent::ACTION_MOBY_KILL);
		} else if (what == "mission_flag") {
			event->set_action(CutsceneEvent::ACTION_MISSION_FLAG);
			event->set_flag_byte(e.get("byte", 0));
			event->set_flag_value(e.get("value", 1));
		} else if (what == "haze") {
			event->set_action(CutsceneEvent::ACTION_HAZE);
			event->set_position(vec3_of(e.get("position", Variant())));
			event->set_radius(e.get("radius", 4.0));
			event->set_strength(e.get("strength", 0.1));
			event->set_length(seconds(e.get("length", 60)));
		} else if (what == "fade") {
			event->set_action(CutsceneEvent::ACTION_FADE);
			const Array to = e.get("to", Array());
			event->set_colour(to.size() >= 3 ? Color(to[0], to[1], to[2], to.size() >= 4 ? float(to[3]) : 1.0f) : Color(0, 0, 0, 0));
			event->set_length(seconds(e.get("length", 30)));
		} else if (what == "play") {
			event->set_action(CutsceneEvent::ACTION_PLAY);
			event->set_cutscene(e.get("cutscene", ""));
		} else if (what == "travel") {
			event->set_action(CutsceneEvent::ACTION_TRAVEL);
		} else {
			problems.push_back(vformat("the event \"%s\" is not kept", what));
			memdelete(event);
			continue;
		}
		if (e.has("spawn_id")) {
			event->set_spawn_id(e["spawn_id"]);
		}
		if (e.has("level")) {
			event->set_level(int(e["level"]) == file_level && what != "travel" ? -1 : int(e["level"]));
		}
		add(event, vformat("%s%d", what.to_pascal_case(), ++count));
	}

	// Where the hero ends.
	const Dictionary end = hero.get("end", Dictionary());
	if (!end.is_empty()) {
		Node3D *marker = memnew(Node3D);
		add(marker, "HeroEnd");
		marker->set_transform(from_game * Transform3D(Basis(Vector3(0, 0, 1), double(end.get("yaw", 0.0))), vec3_of(end.get("position", Variant()))));
	}
	update_configuration_warnings();
	return problems;
}

PackedStringArray Cutscene::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (get_timeline() == nullptr) {
		warnings.push_back(RTR("Add an AnimationPlayer child: its animation is the cutscene's timeline."));
	}
	bool has_camera = false;
	for (int i = 0; i < get_child_count(); i++) {
		has_camera = has_camera || Object::cast_to<CutsceneCamera>(get_child(i)) != nullptr;
	}
	if (!has_camera) {
		warnings.push_back(RTR("No CutsceneCamera child: the game's own camera stays."));
	}
	if ((trigger_kind == TRIGGER_TALK || trigger_kind == TRIGGER_PICKUP) && trigger_spawn_id < 0) {
		warnings.push_back(RTR("This trigger needs the spawn id of a moby."));
	}
	if (trigger_kind == TRIGGER_AFTER && trigger_after.is_empty()) {
		warnings.push_back(RTR("Name the cutscene this one plays after."));
	}
	return warnings;
}

void Cutscene::_validate_property(PropertyInfo &p_property) const {
	const String &name = p_property.name;
	const bool area = trigger_kind == TRIGGER_AREA;
	const bool talk = trigger_kind == TRIGGER_TALK;
	if ((name == "trigger_radius" && !area && !talk) || (name == "trigger_box" && !area) ||
			(name == "trigger_spawn_id" && !talk && trigger_kind != TRIGGER_PICKUP) ||
			(name == "trigger_after" && trigger_kind != TRIGGER_AFTER) ||
			(name == "trigger_flag" && !trigger_flag_enabled && trigger_kind != TRIGGER_FLAG)) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void Cutscene::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_file_name", "name"), &Cutscene::set_file_name);
	ClassDB::bind_method(D_METHOD("get_file_name"), &Cutscene::get_file_name);
	ClassDB::bind_method(D_METHOD("set_title", "title"), &Cutscene::set_title);
	ClassDB::bind_method(D_METHOD("get_title"), &Cutscene::get_title);
	ClassDB::bind_method(D_METHOD("set_game", "game"), &Cutscene::set_game);
	ClassDB::bind_method(D_METHOD("get_game"), &Cutscene::get_game);
	ClassDB::bind_method(D_METHOD("set_length", "seconds"), &Cutscene::set_length);
	ClassDB::bind_method(D_METHOD("get_length"), &Cutscene::get_length);
	ClassDB::bind_method(D_METHOD("set_languages", "languages"), &Cutscene::set_languages);
	ClassDB::bind_method(D_METHOD("get_languages"), &Cutscene::get_languages);
	ClassDB::bind_method(D_METHOD("set_trigger_kind", "kind"), &Cutscene::set_trigger_kind);
	ClassDB::bind_method(D_METHOD("get_trigger_kind"), &Cutscene::get_trigger_kind);
	ClassDB::bind_method(D_METHOD("set_trigger_once", "once"), &Cutscene::set_trigger_once);
	ClassDB::bind_method(D_METHOD("get_trigger_once"), &Cutscene::get_trigger_once);
	ClassDB::bind_method(D_METHOD("set_trigger_radius", "radius"), &Cutscene::set_trigger_radius);
	ClassDB::bind_method(D_METHOD("get_trigger_radius"), &Cutscene::get_trigger_radius);
	ClassDB::bind_method(D_METHOD("set_trigger_box", "size"), &Cutscene::set_trigger_box);
	ClassDB::bind_method(D_METHOD("get_trigger_box"), &Cutscene::get_trigger_box);
	ClassDB::bind_method(D_METHOD("set_trigger_spawn_id", "id"), &Cutscene::set_trigger_spawn_id);
	ClassDB::bind_method(D_METHOD("get_trigger_spawn_id"), &Cutscene::get_trigger_spawn_id);
	ClassDB::bind_method(D_METHOD("set_trigger_after", "name"), &Cutscene::set_trigger_after);
	ClassDB::bind_method(D_METHOD("get_trigger_after"), &Cutscene::get_trigger_after);
	ClassDB::bind_method(D_METHOD("set_trigger_flag_enabled", "enabled"), &Cutscene::set_trigger_flag_enabled);
	ClassDB::bind_method(D_METHOD("get_trigger_flag_enabled"), &Cutscene::get_trigger_flag_enabled);
	ClassDB::bind_method(D_METHOD("set_trigger_flag", "flag"), &Cutscene::set_trigger_flag);
	ClassDB::bind_method(D_METHOD("get_trigger_flag"), &Cutscene::get_trigger_flag);
	ClassDB::bind_method(D_METHOD("set_skippable", "on"), &Cutscene::set_skippable);
	ClassDB::bind_method(D_METHOD("get_skippable"), &Cutscene::get_skippable);
	ClassDB::bind_method(D_METHOD("set_letterbox", "on"), &Cutscene::set_letterbox);
	ClassDB::bind_method(D_METHOD("get_letterbox"), &Cutscene::get_letterbox);
	ClassDB::bind_method(D_METHOD("set_hide_hud", "on"), &Cutscene::set_hide_hud);
	ClassDB::bind_method(D_METHOD("get_hide_hud"), &Cutscene::get_hide_hud);
	ClassDB::bind_method(D_METHOD("set_world", "world"), &Cutscene::set_world);
	ClassDB::bind_method(D_METHOD("get_world"), &Cutscene::get_world);
	ClassDB::bind_method(D_METHOD("set_music", "music"), &Cutscene::set_music);
	ClassDB::bind_method(D_METHOD("get_music"), &Cutscene::get_music);
	ClassDB::bind_method(D_METHOD("set_hero_mode", "mode"), &Cutscene::set_hero_mode);
	ClassDB::bind_method(D_METHOD("get_hero_mode"), &Cutscene::get_hero_mode);
	ClassDB::bind_method(D_METHOD("set_variants", "variants"), &Cutscene::set_variants);
	ClassDB::bind_method(D_METHOD("get_variants"), &Cutscene::get_variants);
	ClassDB::bind_method(D_METHOD("get_export_action"), &Cutscene::get_export_action);
	ClassDB::bind_method(D_METHOD("get_set_up_action"), &Cutscene::get_set_up_action);
	ClassDB::bind_method(D_METHOD("get_cutscene_id"), &Cutscene::get_cutscene_id);
	ClassDB::bind_method(D_METHOD("get_timeline"), &Cutscene::get_timeline);
	ClassDB::bind_method(D_METHOD("get_timeline_animation"), &Cutscene::get_timeline_animation);
	ClassDB::bind_method(D_METHOD("to_cutscene"), &Cutscene::_to_cutscene_bind);
	ClassDB::bind_method(D_METHOD("export_cutscene"), &Cutscene::_export_cutscene_bind);
	ClassDB::bind_method(D_METHOD("get_import_action"), &Cutscene::get_import_action);
	ClassDB::bind_method(D_METHOD("from_cutscene", "data"), &Cutscene::from_cutscene);
	ClassDB::bind_method(D_METHOD("import_cutscene", "path"), &Cutscene::_import_cutscene_bind);

	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "set_up", PROPERTY_HINT_TOOL_BUTTON, "Set Up Timeline and Camera,AnimationPlayer", PROPERTY_USAGE_EDITOR), "", "get_set_up_action");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "export_now", PROPERTY_HINT_TOOL_BUTTON, "Export .cutscene,Save", PROPERTY_USAGE_EDITOR), "", "get_export_action");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "import_file", PROPERTY_HINT_TOOL_BUTTON, "Import .cutscene,Load", PROPERTY_USAGE_EDITOR), "", "get_import_action");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "file_name", PROPERTY_HINT_PLACEHOLDER_TEXT, "the node's name"), "set_file_name", "get_file_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "title"), "set_title", "get_title");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "game"), "set_game", "get_game");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "length", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"), "set_length", "get_length");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "languages"), "set_languages", "get_languages");

	ADD_GROUP("Trigger", "trigger_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "trigger_kind", PROPERTY_HINT_ENUM, "Level Start,Area,Talk,Pickup,After,Flag,Manual"), "set_trigger_kind", "get_trigger_kind");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "trigger_once"), "set_trigger_once", "get_trigger_once");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "trigger_radius", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater"), "set_trigger_radius", "get_trigger_radius");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "trigger_box"), "set_trigger_box", "get_trigger_box");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "trigger_spawn_id"), "set_trigger_spawn_id", "get_trigger_spawn_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "trigger_after"), "set_trigger_after", "get_trigger_after");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "trigger_flag_enabled"), "set_trigger_flag_enabled", "get_trigger_flag_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3I, "trigger_flag"), "set_trigger_flag", "get_trigger_flag");

	ADD_GROUP("Options", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "skippable"), "set_skippable", "get_skippable");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "letterbox"), "set_letterbox", "get_letterbox");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hide_hud"), "set_hide_hud", "get_hide_hud");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "world", PROPERTY_HINT_ENUM, "Run,Hold"), "set_world", "get_world");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "music", PROPERTY_HINT_ENUM, "Duck,Keep,Stop"), "set_music", "get_music");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "hero_mode", PROPERTY_HINT_ENUM, "Held,Hidden,Actor"), "set_hero_mode", "get_hero_mode");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "variants", PROPERTY_HINT_ARRAY_TYPE, "Dictionary"), "set_variants", "get_variants");

	BIND_ENUM_CONSTANT(TRIGGER_LEVEL_START);
	BIND_ENUM_CONSTANT(TRIGGER_AREA);
	BIND_ENUM_CONSTANT(TRIGGER_TALK);
	BIND_ENUM_CONSTANT(TRIGGER_PICKUP);
	BIND_ENUM_CONSTANT(TRIGGER_AFTER);
	BIND_ENUM_CONSTANT(TRIGGER_FLAG);
	BIND_ENUM_CONSTANT(TRIGGER_MANUAL);
	BIND_ENUM_CONSTANT(HERO_HELD);
	BIND_ENUM_CONSTANT(HERO_HIDDEN);
	BIND_ENUM_CONSTANT(HERO_ACTOR);
	BIND_ENUM_CONSTANT(WORLD_RUN);
	BIND_ENUM_CONSTANT(WORLD_HOLD);
	BIND_ENUM_CONSTANT(MUSIC_DUCK);
	BIND_ENUM_CONSTANT(MUSIC_KEEP);
	BIND_ENUM_CONSTANT(MUSIC_STOP);
}

Dictionary Cutscene::_to_cutscene_bind() const {
	return to_cutscene();
}

Error Cutscene::_import_cutscene_bind(const String &p_path) {
	PackedStringArray problems;
	const Error err = import_cutscene(p_path, &problems);
	for (const String &problem : problems) {
		WARN_PRINT(vformat("Import of %s: %s", p_path.get_file(), problem));
	}
	return err;
}

String Cutscene::_export_cutscene_bind() const {
	String path;
	return export_cutscene(&path) == OK ? path : String();
}

// ------------------------------------------------------------------------------------------------
// CutsceneCamera

void CutsceneCamera::set_cuts(const PackedFloat32Array &p_cuts) { cuts = p_cuts; }
PackedFloat32Array CutsceneCamera::get_cuts() const { return cuts; }
void CutsceneCamera::set_ease_back(bool p_ease) { ease_back = p_ease; }
bool CutsceneCamera::get_ease_back() const { return ease_back; }
void CutsceneCamera::set_export_fov(bool p_export) { export_fov = p_export; }
bool CutsceneCamera::get_export_fov() const { return export_fov; }

void CutsceneCamera::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cuts", "seconds"), &CutsceneCamera::set_cuts);
	ClassDB::bind_method(D_METHOD("get_cuts"), &CutsceneCamera::get_cuts);
	ClassDB::bind_method(D_METHOD("set_ease_back", "ease"), &CutsceneCamera::set_ease_back);
	ClassDB::bind_method(D_METHOD("get_ease_back"), &CutsceneCamera::get_ease_back);
	ClassDB::bind_method(D_METHOD("set_export_fov", "export"), &CutsceneCamera::set_export_fov);
	ClassDB::bind_method(D_METHOD("get_export_fov"), &CutsceneCamera::get_export_fov);
	ADD_GROUP("Cutscene", "");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "cuts"), "set_cuts", "get_cuts");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ease_back"), "set_ease_back", "get_ease_back");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "export_fov"), "set_export_fov", "get_export_fov");
}

// ------------------------------------------------------------------------------------------------
// CutsceneActor

void CutsceneActor::set_actor_id(const String &p_id) { actor_id = p_id; }
String CutsceneActor::get_actor_id() const { return actor_id; }
String CutsceneActor::get_effective_id() const {
	const String id = actor_id.strip_edges();
	return id.is_empty() ? String(get_name()).to_snake_case() : id;
}
void CutsceneActor::set_source(Source p_source) {
	source = p_source;
	notify_property_list_changed();
	update_configuration_warnings();
}
CutsceneActor::Source CutsceneActor::get_source() const { return source; }
void CutsceneActor::set_spawn_id(int p_id) {
	spawn_id = p_id;
	update_configuration_warnings();
}
int CutsceneActor::get_spawn_id() const { return spawn_id; }
void CutsceneActor::set_moby_class(int p_class) {
	moby_class = p_class;
	update_configuration_warnings();
}
int CutsceneActor::get_moby_class() const { return moby_class; }
void CutsceneActor::set_prop_path(const String &p_path) {
	prop_path = p_path;
	update_configuration_warnings();
}
String CutsceneActor::get_prop_path() const { return prop_path; }
void CutsceneActor::set_spawn_if_missing(bool p_spawn) { spawn_if_missing = p_spawn; }
bool CutsceneActor::get_spawn_if_missing() const { return spawn_if_missing; }
void CutsceneActor::set_end(End p_end) { end = p_end; }
CutsceneActor::End CutsceneActor::get_end() const { return end; }
void CutsceneActor::set_sequence(int p_sequence) { sequence = p_sequence; }
int CutsceneActor::get_sequence() const { return sequence; }
void CutsceneActor::set_anim_speed(float p_speed) { anim_speed = p_speed; }
float CutsceneActor::get_anim_speed() const { return anim_speed; }
void CutsceneActor::set_blend(int p_ticks) { blend = MAX(0, p_ticks); }
int CutsceneActor::get_blend() const { return blend; }

Callable CutsceneActor::get_take_selected_action() {
	return callable_mp(this, &CutsceneActor::_take_selected_moby);
}

// Makes the actor the moby selected in the scene beside it: its spawn id, its class and its place.
void CutsceneActor::_take_selected_moby() {
#ifdef TOOLS_ENABLED
	EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
	for (Node *node : selection->get_top_selected_node_list()) {
		Node3D *moby = Object::cast_to<Node3D>(node);
		if (moby == nullptr || moby == this || !moby->has_meta("rc1_spawn_id")) {
			continue;
		}
		set_source(SOURCE_LEVEL_MOBY);
		set_spawn_id(moby->get_meta("rc1_spawn_id"));
		set_moby_class(cutscene_math::class_from_scene(moby->get_scene_file_path()));
		if (actor_id.is_empty()) {
			set_actor_id(String(moby->get_name()).to_snake_case());
		}
		set_global_transform(moby->get_global_transform());
		print_line(vformat("Actor \"%s\" is %s (spawn id %d).", get_effective_id(), moby->get_name(), spawn_id));
		return;
	}
	WARN_PRINT("Select a moby of the level too (Ctrl+click it in the scene tree), then press again.");
#endif
}

PackedStringArray CutsceneActor::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (source == SOURCE_LEVEL_MOBY && spawn_id < 0) {
		warnings.push_back(RTR("Set the moby's spawn id, or select a moby with this actor and press Take Selected Moby."));
	}
	if (source == SOURCE_NEW_MOBY && moby_class < 0) {
		warnings.push_back(RTR("Set the class of the moby to make."));
	}
	if (source == SOURCE_PROP && prop_path.is_empty()) {
		warnings.push_back(RTR("Set the mesh (a .glb beside the cutscene file)."));
	}
	if (Object::cast_to<Cutscene>(get_parent()) == nullptr) {
		warnings.push_back(RTR("An actor plays only as a child of a Cutscene."));
	}
	return warnings;
}

void CutsceneActor::_validate_property(PropertyInfo &p_property) const {
	const String &name = p_property.name;
	if ((name == "spawn_id" && source != SOURCE_LEVEL_MOBY) || (name == "spawn_if_missing" && source != SOURCE_LEVEL_MOBY) ||
			(name == "moby_class" && source != SOURCE_LEVEL_MOBY && source != SOURCE_NEW_MOBY) ||
			(name == "prop_path" && source != SOURCE_PROP)) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void CutsceneActor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_actor_id", "id"), &CutsceneActor::set_actor_id);
	ClassDB::bind_method(D_METHOD("get_actor_id"), &CutsceneActor::get_actor_id);
	ClassDB::bind_method(D_METHOD("get_effective_id"), &CutsceneActor::get_effective_id);
	ClassDB::bind_method(D_METHOD("set_source", "source"), &CutsceneActor::set_source);
	ClassDB::bind_method(D_METHOD("get_source"), &CutsceneActor::get_source);
	ClassDB::bind_method(D_METHOD("set_spawn_id", "id"), &CutsceneActor::set_spawn_id);
	ClassDB::bind_method(D_METHOD("get_spawn_id"), &CutsceneActor::get_spawn_id);
	ClassDB::bind_method(D_METHOD("set_moby_class", "class"), &CutsceneActor::set_moby_class);
	ClassDB::bind_method(D_METHOD("get_moby_class"), &CutsceneActor::get_moby_class);
	ClassDB::bind_method(D_METHOD("set_prop_path", "path"), &CutsceneActor::set_prop_path);
	ClassDB::bind_method(D_METHOD("get_prop_path"), &CutsceneActor::get_prop_path);
	ClassDB::bind_method(D_METHOD("set_spawn_if_missing", "spawn"), &CutsceneActor::set_spawn_if_missing);
	ClassDB::bind_method(D_METHOD("get_spawn_if_missing"), &CutsceneActor::get_spawn_if_missing);
	ClassDB::bind_method(D_METHOD("set_end", "end"), &CutsceneActor::set_end);
	ClassDB::bind_method(D_METHOD("get_end"), &CutsceneActor::get_end);
	ClassDB::bind_method(D_METHOD("set_sequence", "sequence"), &CutsceneActor::set_sequence);
	ClassDB::bind_method(D_METHOD("get_sequence"), &CutsceneActor::get_sequence);
	ClassDB::bind_method(D_METHOD("set_anim_speed", "speed"), &CutsceneActor::set_anim_speed);
	ClassDB::bind_method(D_METHOD("get_anim_speed"), &CutsceneActor::get_anim_speed);
	ClassDB::bind_method(D_METHOD("set_blend", "ticks"), &CutsceneActor::set_blend);
	ClassDB::bind_method(D_METHOD("get_blend"), &CutsceneActor::get_blend);
	ClassDB::bind_method(D_METHOD("get_take_selected_action"), &CutsceneActor::get_take_selected_action);

	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "take_selected_moby", PROPERTY_HINT_TOOL_BUTTON, "Take Selected Moby,Node3D", PROPERTY_USAGE_EDITOR), "", "get_take_selected_action");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "actor_id", PROPERTY_HINT_PLACEHOLDER_TEXT, "the node's name"), "set_actor_id", "get_actor_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "source", PROPERTY_HINT_ENUM, "Level Moby,New Moby,Hero Figure,Prop"), "set_source", "get_source");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "spawn_id"), "set_spawn_id", "get_spawn_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "moby_class"), "set_moby_class", "get_moby_class");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "prop_path"), "set_prop_path", "get_prop_path");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "spawn_if_missing"), "set_spawn_if_missing", "get_spawn_if_missing");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "end", PROPERTY_HINT_ENUM, "Keep,Restore,Remove"), "set_end", "get_end");
	ADD_GROUP("Animation", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sequence", PROPERTY_HINT_RANGE, "-1,255,1"), "set_sequence", "get_sequence");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "anim_speed", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_anim_speed", "get_anim_speed");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend", PROPERTY_HINT_RANGE, "0,60,1,suffix:ticks"), "set_blend", "get_blend");

	BIND_ENUM_CONSTANT(SOURCE_LEVEL_MOBY);
	BIND_ENUM_CONSTANT(SOURCE_NEW_MOBY);
	BIND_ENUM_CONSTANT(SOURCE_HERO_FIGURE);
	BIND_ENUM_CONSTANT(SOURCE_PROP);
	BIND_ENUM_CONSTANT(END_KEEP);
	BIND_ENUM_CONSTANT(END_RESTORE);
	BIND_ENUM_CONSTANT(END_REMOVE);
}

// ------------------------------------------------------------------------------------------------
// CutsceneSubtitle

void CutsceneSubtitle::set_start(double p_seconds) { start = MAX(0.0, p_seconds); }
double CutsceneSubtitle::get_start() const { return start; }
void CutsceneSubtitle::set_duration(double p_seconds) { duration = MAX(0.0, p_seconds); }
double CutsceneSubtitle::get_duration() const { return duration; }
void CutsceneSubtitle::set_text(const Dictionary &p_text) { text = p_text; }
Dictionary CutsceneSubtitle::get_text() const { return text; }
void CutsceneSubtitle::set_speaker(const NodePath &p_actor) { speaker = p_actor; }
NodePath CutsceneSubtitle::get_speaker() const { return speaker; }

void CutsceneSubtitle::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_start", "seconds"), &CutsceneSubtitle::set_start);
	ClassDB::bind_method(D_METHOD("get_start"), &CutsceneSubtitle::get_start);
	ClassDB::bind_method(D_METHOD("set_duration", "seconds"), &CutsceneSubtitle::set_duration);
	ClassDB::bind_method(D_METHOD("get_duration"), &CutsceneSubtitle::get_duration);
	ClassDB::bind_method(D_METHOD("set_text", "text"), &CutsceneSubtitle::set_text);
	ClassDB::bind_method(D_METHOD("get_text"), &CutsceneSubtitle::get_text);
	ClassDB::bind_method(D_METHOD("set_speaker", "actor"), &CutsceneSubtitle::set_speaker);
	ClassDB::bind_method(D_METHOD("get_speaker"), &CutsceneSubtitle::get_speaker);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"), "set_start", "get_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater,suffix:s"), "set_duration", "get_duration");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "text", PROPERTY_HINT_DICTIONARY_TYPE, "String;String"), "set_text", "get_text");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "speaker", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "CutsceneActor"), "set_speaker", "get_speaker");
}

// ------------------------------------------------------------------------------------------------
// CutsceneSound

void CutsceneSound::set_start(double p_seconds) { start = MAX(0.0, p_seconds); }
double CutsceneSound::get_start() const { return start; }
void CutsceneSound::set_kind(Kind p_kind) { kind = p_kind; }
CutsceneSound::Kind CutsceneSound::get_kind() const { return kind; }
void CutsceneSound::set_file(const String &p_file) { file = p_file; }
String CutsceneSound::get_file() const { return file; }
void CutsceneSound::set_files(const Dictionary &p_files) { files = p_files; }
Dictionary CutsceneSound::get_files() const { return files; }
void CutsceneSound::set_game_sound(int p_sound) { game_sound = p_sound; }
int CutsceneSound::get_game_sound() const { return game_sound; }
void CutsceneSound::set_source(const NodePath &p_actor) { source = p_actor; }
NodePath CutsceneSound::get_source() const { return source; }
void CutsceneSound::set_volume(float p_volume) { volume = MAX(0.0f, p_volume); }
float CutsceneSound::get_volume() const { return volume; }
void CutsceneSound::set_fade_in(double p_seconds) { fade_in = MAX(0.0, p_seconds); }
double CutsceneSound::get_fade_in() const { return fade_in; }
void CutsceneSound::set_fade_out(double p_seconds) { fade_out = MAX(0.0, p_seconds); }
double CutsceneSound::get_fade_out() const { return fade_out; }
void CutsceneSound::set_stop(double p_seconds) { stop = MAX(0.0, p_seconds); }
double CutsceneSound::get_stop() const { return stop; }

void CutsceneSound::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_start", "seconds"), &CutsceneSound::set_start);
	ClassDB::bind_method(D_METHOD("get_start"), &CutsceneSound::get_start);
	ClassDB::bind_method(D_METHOD("set_kind", "kind"), &CutsceneSound::set_kind);
	ClassDB::bind_method(D_METHOD("get_kind"), &CutsceneSound::get_kind);
	ClassDB::bind_method(D_METHOD("set_file", "file"), &CutsceneSound::set_file);
	ClassDB::bind_method(D_METHOD("get_file"), &CutsceneSound::get_file);
	ClassDB::bind_method(D_METHOD("set_files", "files"), &CutsceneSound::set_files);
	ClassDB::bind_method(D_METHOD("get_files"), &CutsceneSound::get_files);
	ClassDB::bind_method(D_METHOD("set_game_sound", "sound"), &CutsceneSound::set_game_sound);
	ClassDB::bind_method(D_METHOD("get_game_sound"), &CutsceneSound::get_game_sound);
	ClassDB::bind_method(D_METHOD("set_source", "actor"), &CutsceneSound::set_source);
	ClassDB::bind_method(D_METHOD("get_source"), &CutsceneSound::get_source);
	ClassDB::bind_method(D_METHOD("set_volume", "volume"), &CutsceneSound::set_volume);
	ClassDB::bind_method(D_METHOD("get_volume"), &CutsceneSound::get_volume);
	ClassDB::bind_method(D_METHOD("set_fade_in", "seconds"), &CutsceneSound::set_fade_in);
	ClassDB::bind_method(D_METHOD("get_fade_in"), &CutsceneSound::get_fade_in);
	ClassDB::bind_method(D_METHOD("set_fade_out", "seconds"), &CutsceneSound::set_fade_out);
	ClassDB::bind_method(D_METHOD("get_fade_out"), &CutsceneSound::get_fade_out);
	ClassDB::bind_method(D_METHOD("set_stop", "seconds"), &CutsceneSound::set_stop);
	ClassDB::bind_method(D_METHOD("get_stop"), &CutsceneSound::get_stop);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"), "set_start", "get_start");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "kind", PROPERTY_HINT_ENUM, "Voice,Music,Sfx"), "set_kind", "get_kind");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "file", PROPERTY_HINT_PLACEHOLDER_TEXT, "audio/line.ogg, beside the .cutscene"), "set_file", "get_file");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "files", PROPERTY_HINT_DICTIONARY_TYPE, "String;String"), "set_files", "get_files");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "game_sound", PROPERTY_HINT_RANGE, "-1,255,1"), "set_game_sound", "get_game_sound");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "source", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "CutsceneActor"), "set_source", "get_source");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_volume", "get_volume");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_in", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:s"), "set_fade_in", "get_fade_in");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_out", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:s"), "set_fade_out", "get_fade_out");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stop", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"), "set_stop", "get_stop");

	BIND_ENUM_CONSTANT(KIND_VOICE);
	BIND_ENUM_CONSTANT(KIND_MUSIC);
	BIND_ENUM_CONSTANT(KIND_SFX);
}

// ------------------------------------------------------------------------------------------------
// CutsceneEvent

void CutsceneEvent::set_start(double p_seconds) { start = MAX(0.0, p_seconds); }
double CutsceneEvent::get_start() const { return start; }
void CutsceneEvent::set_action(Action p_action) {
	action = p_action;
	notify_property_list_changed();
}
CutsceneEvent::Action CutsceneEvent::get_action() const { return action; }
void CutsceneEvent::set_persistent(bool p_persistent) { persistent = p_persistent; }
bool CutsceneEvent::get_persistent() const { return persistent; }
void CutsceneEvent::set_spawn_id(int p_id) { spawn_id = p_id; }
int CutsceneEvent::get_spawn_id() const { return spawn_id; }
void CutsceneEvent::set_level(int p_level) { level = p_level; }
int CutsceneEvent::get_level() const { return level; }
void CutsceneEvent::set_flag_byte(int p_byte) { flag_byte = CLAMP(p_byte, 0, 15); }
int CutsceneEvent::get_flag_byte() const { return flag_byte; }
void CutsceneEvent::set_flag_value(int p_value) { flag_value = CLAMP(p_value, 0, 255); }
int CutsceneEvent::get_flag_value() const { return flag_value; }
void CutsceneEvent::set_position(const Vector3 &p_position) { position = p_position; }
Vector3 CutsceneEvent::get_position() const { return position; }
void CutsceneEvent::set_radius(float p_radius) { radius = MAX(0.0f, p_radius); }
float CutsceneEvent::get_radius() const { return radius; }
void CutsceneEvent::set_strength(float p_strength) { strength = MAX(0.0f, p_strength); }
float CutsceneEvent::get_strength() const { return strength; }
void CutsceneEvent::set_length(double p_seconds) { length = MAX(0.0, p_seconds); }
double CutsceneEvent::get_length() const { return length; }
void CutsceneEvent::set_colour(const Color &p_colour) { colour = p_colour; }
Color CutsceneEvent::get_colour() const { return colour; }
void CutsceneEvent::set_cutscene(const String &p_name) { cutscene = p_name; }
String CutsceneEvent::get_cutscene() const { return cutscene; }

Dictionary CutsceneEvent::to_event() const {
	Dictionary e;
	e["t"] = ticks(start);
	switch (action) {
		case ACTION_MOBY_HIDE:
		case ACTION_MOBY_SHOW:
			e["do"] = "moby_hide";
			e["spawn_id"] = spawn_id;
			e["on"] = action == ACTION_MOBY_HIDE;
			break;
		case ACTION_MOBY_KILL:
			e["do"] = "moby_kill";
			e["spawn_id"] = spawn_id;
			break;
		case ACTION_MISSION_FLAG:
			e["do"] = "mission_flag";
			e["level"] = level;
			e["byte"] = flag_byte;
			e["value"] = flag_value;
			break;
		case ACTION_HAZE:
			e["do"] = "haze";
			e["position"] = vec3(position);
			e["radius"] = tidy(radius);
			e["strength"] = tidy(strength);
			e["length"] = ticks(length);
			break;
		case ACTION_FADE: {
			e["do"] = "fade";
			Array to;
			to.push_back(tidy(colour.r));
			to.push_back(tidy(colour.g));
			to.push_back(tidy(colour.b));
			if (colour.a < 1.0f) {
				to.push_back(tidy(colour.a)); // alpha 0: back to a clear picture
			}
			e["to"] = to;
			e["length"] = ticks(length);
		} break;
		case ACTION_PLAY:
			e["do"] = "play";
			e["cutscene"] = cutscene;
			break;
		case ACTION_TRAVEL:
			e["do"] = "travel";
			e["level"] = level;
			break;
	}
	if (persistent) {
		e["persistent"] = true;
	}
	return e;
}

void CutsceneEvent::_validate_property(PropertyInfo &p_property) const {
	const String &name = p_property.name;
	const bool moby = action == ACTION_MOBY_HIDE || action == ACTION_MOBY_SHOW || action == ACTION_MOBY_KILL;
	const bool timed = action == ACTION_HAZE || action == ACTION_FADE;
	if ((name == "spawn_id" && !moby) || (name == "level" && action != ACTION_MISSION_FLAG && action != ACTION_TRAVEL) ||
			((name == "flag_byte" || name == "flag_value") && action != ACTION_MISSION_FLAG) ||
			((name == "position" || name == "radius" || name == "strength") && action != ACTION_HAZE) ||
			(name == "length" && !timed) || (name == "colour" && action != ACTION_FADE) ||
			(name == "cutscene" && action != ACTION_PLAY)) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void CutsceneEvent::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_start", "seconds"), &CutsceneEvent::set_start);
	ClassDB::bind_method(D_METHOD("get_start"), &CutsceneEvent::get_start);
	ClassDB::bind_method(D_METHOD("set_action", "action"), &CutsceneEvent::set_action);
	ClassDB::bind_method(D_METHOD("get_action"), &CutsceneEvent::get_action);
	ClassDB::bind_method(D_METHOD("set_persistent", "persistent"), &CutsceneEvent::set_persistent);
	ClassDB::bind_method(D_METHOD("get_persistent"), &CutsceneEvent::get_persistent);
	ClassDB::bind_method(D_METHOD("set_spawn_id", "id"), &CutsceneEvent::set_spawn_id);
	ClassDB::bind_method(D_METHOD("get_spawn_id"), &CutsceneEvent::get_spawn_id);
	ClassDB::bind_method(D_METHOD("set_level", "level"), &CutsceneEvent::set_level);
	ClassDB::bind_method(D_METHOD("get_level"), &CutsceneEvent::get_level);
	ClassDB::bind_method(D_METHOD("set_flag_byte", "byte"), &CutsceneEvent::set_flag_byte);
	ClassDB::bind_method(D_METHOD("get_flag_byte"), &CutsceneEvent::get_flag_byte);
	ClassDB::bind_method(D_METHOD("set_flag_value", "value"), &CutsceneEvent::set_flag_value);
	ClassDB::bind_method(D_METHOD("get_flag_value"), &CutsceneEvent::get_flag_value);
	ClassDB::bind_method(D_METHOD("set_position", "position"), &CutsceneEvent::set_position);
	ClassDB::bind_method(D_METHOD("get_position"), &CutsceneEvent::get_position);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &CutsceneEvent::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &CutsceneEvent::get_radius);
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &CutsceneEvent::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &CutsceneEvent::get_strength);
	ClassDB::bind_method(D_METHOD("set_length", "seconds"), &CutsceneEvent::set_length);
	ClassDB::bind_method(D_METHOD("get_length"), &CutsceneEvent::get_length);
	ClassDB::bind_method(D_METHOD("set_colour", "colour"), &CutsceneEvent::set_colour);
	ClassDB::bind_method(D_METHOD("get_colour"), &CutsceneEvent::get_colour);
	ClassDB::bind_method(D_METHOD("set_cutscene", "name"), &CutsceneEvent::set_cutscene);
	ClassDB::bind_method(D_METHOD("get_cutscene"), &CutsceneEvent::get_cutscene);
	ClassDB::bind_method(D_METHOD("to_event"), &CutsceneEvent::to_event);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"), "set_start", "get_start");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "action", PROPERTY_HINT_ENUM, "Hide Moby,Show Moby,Delete Moby,Mission Flag,Heat Haze,Fade,Play Cutscene,Travel"), "set_action", "get_action");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "persistent"), "set_persistent", "get_persistent");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "spawn_id"), "set_spawn_id", "get_spawn_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "level", PROPERTY_HINT_RANGE, "-1,255,1"), "set_level", "get_level");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "flag_byte", PROPERTY_HINT_RANGE, "0,15,1"), "set_flag_byte", "get_flag_byte");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "flag_value", PROPERTY_HINT_RANGE, "0,255,1"), "set_flag_value", "get_flag_value");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "position"), "set_position", "get_position");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,50,0.1,or_greater"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "length", PROPERTY_HINT_RANGE, "0,60,0.01,suffix:s"), "set_length", "get_length");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "colour"), "set_colour", "get_colour");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "cutscene"), "set_cutscene", "get_cutscene");

	BIND_ENUM_CONSTANT(ACTION_MOBY_HIDE);
	BIND_ENUM_CONSTANT(ACTION_MOBY_SHOW);
	BIND_ENUM_CONSTANT(ACTION_MOBY_KILL);
	BIND_ENUM_CONSTANT(ACTION_MISSION_FLAG);
	BIND_ENUM_CONSTANT(ACTION_HAZE);
	BIND_ENUM_CONSTANT(ACTION_FADE);
	BIND_ENUM_CONSTANT(ACTION_PLAY);
	BIND_ENUM_CONSTANT(ACTION_TRAVEL);
}
