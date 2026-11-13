/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: the cutscene nodes and their export to a .cutscene file. */

#include "cutscene.h"

#include "cutscene_math.h"
#include "cutscene_rigs.h"

#include "core/config/project_settings.h"
#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/marshalls.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/animation/animation_player.h"
#include "scene/resources/font.h"
#include "servers/text/text_server.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/gui/editor_file_dialog.h"
#endif

namespace {

using cutscene_math::ticks;

// Subtitles the game's font cannot write (Japanese, and any letter past Latin) go into the file as
// pictures the player draws: 1 bit a pixel, two pixels to one of the game's 512 x 448 picture.
constexpr int SUBTITLE_FONT_SIZE = 26;
constexpr int SUBTITLE_MAX_WIDTH = 880; // 440 of the game's pixels
constexpr int SUBTITLE_MAX_ROWS = 3;

bool needs_picture(const String &p_language, const String &p_text) {
	if (p_language == "ja") {
		return true;
	}
	for (int i = 0; i < p_text.length(); i++) {
		if (p_text[i] > 0x24F) {
			return true;
		}
	}
	return false;
}

// A font with the letters of a language the game's font lacks: the system's (Noto CJK, Yu Gothic,
// Hiragino, ...), found once and let go with the module (cutscene_free_font), before the text server.
Ref<SystemFont> g_subtitle_font;

Ref<SystemFont> subtitle_font() {
	if (g_subtitle_font.is_null()) {
		Ref<SystemFont> font;
		font.instantiate();
		PackedStringArray names;
		for (const char *name : { "Noto Sans CJK JP", "Noto Sans JP", "Yu Gothic", "Meiryo", "MS Gothic",
					 "Hiragino Sans", "Hiragino Kaku Gothic ProN", "Source Han Sans JP", "IPAGothic",
					 "TakaoGothic", "WenQuanYi Zen Hei", "Droid Sans Fallback", "Noto Sans" }) {
			names.push_back(name);
		}
		font->set_font_names(names);
		font->set_antialiasing(TextServer::FONT_ANTIALIASING_GRAY);
		g_subtitle_font = font;
	}
	return g_subtitle_font;
}

// `p_text` drawn as a subtitle picture: { width, height, pixel, bits } (rows of bits, the first pixel
// in each byte's top bit, base64). Broken into up to three rows at spaces, or anywhere in a language
// without them. Empty, with `r_problem` set, when no font has its letters.
Dictionary subtitle_picture(const String &p_text, String &r_problem) {
	const Ref<SystemFont> font = subtitle_font();
	for (int i = 0; i < p_text.length(); i++) {
		if (p_text[i] > 32 && !font->has_char(p_text[i])) {
			r_problem = vformat("no font on this computer has the letter %s (install Noto Sans CJK)", String::chr(p_text[i]));
			return Dictionary();
		}
	}
	TextServer *ts = TextServerManager::get_singleton()->get_primary_interface().ptr();
	const TypedArray<RID> rids = font->get_rids();
	if (ts == nullptr || rids.is_empty()) {
		r_problem = "no text server to draw it";
		return Dictionary();
	}
	const RID shaped = ts->create_shaped_text();
	ts->shaped_text_add_string(shaped, p_text, rids, SUBTITLE_FONT_SIZE);
	const Glyph *glyphs = ts->shaped_text_get_glyphs(shaped);
	const int count = ts->shaped_text_get_glyph_count(shaped);
	const int ascent = int(Math::ceil(ts->shaped_text_get_ascent(shaped)));
	const int line = ascent + int(Math::ceil(ts->shaped_text_get_descent(shaped))) + 2;

	// The rows: glyph ranges, broken at the last space that fits, else at the glyph that does not.
	Vector<Vector2i> rows; // first glyph, past the last
	int first = 0;
	while (first < count && rows.size() < SUBTITLE_MAX_ROWS) {
		float width = 0;
		int end = first, space = -1;
		while (end < count) {
			const float advance = glyphs[end].advance * glyphs[end].repeat;
			if (width + advance > SUBTITLE_MAX_WIDTH && end > first) {
				break;
			}
			if (glyphs[end].start >= 0 && glyphs[end].start < p_text.length() && p_text[glyphs[end].start] == ' ') {
				space = end;
			}
			width += advance;
			end++;
		}
		if (end < count && space > first) {
			end = space + 1;
		}
		rows.push_back(Vector2i(first, end));
		first = end;
	}

	// Each row's width without its spaces at the ends, and the picture's.
	Vector<float> widths;
	float widest = 1;
	for (const Vector2i &row : rows) {
		float w = 0;
		for (int g = row.x; g < row.y; g++) {
			w += glyphs[g].advance * glyphs[g].repeat;
		}
		widths.push_back(w);
		widest = MAX(widest, w);
	}
	const int width = MIN(int(Math::ceil(widest)) + 2, SUBTITLE_MAX_WIDTH + 2);
	const int height = MAX(1, line * rows.size());
	Vector<uint8_t> lit;
	lit.resize_initialized(width * height);

	for (int r = 0; r < rows.size(); r++) {
		float pen = (width - widths[r]) / 2;
		const float baseline = r * line + ascent + 1;
		for (int g = rows[r].x; g < rows[r].y; g++) {
			const Glyph &gl = glyphs[g];
			for (int k = 0; k < gl.repeat; k++) {
				if (gl.font_rid.is_valid() && (gl.flags & TextServer::GRAPHEME_IS_VIRTUAL) == 0) {
					const Vector2i size(gl.font_size, 0);
					ts->font_render_glyph(gl.font_rid, size, gl.index);
					const int tex = ts->font_get_glyph_texture_idx(gl.font_rid, size, gl.index);
					if (tex >= 0) {
						const Ref<Image> atlas = ts->font_get_texture_image(gl.font_rid, size, tex);
						const Rect2 uv = ts->font_get_glyph_uv_rect(gl.font_rid, size, gl.index);
						const Vector2 off = ts->font_get_glyph_offset(gl.font_rid, size, gl.index);
						const int x0 = int(Math::round(pen + gl.x_off + off.x));
						const int y0 = int(Math::round(baseline + gl.y_off + off.y));
						if (atlas.is_valid()) {
							for (int v = 0; v < int(uv.size.y); v++) {
								for (int u = 0; u < int(uv.size.x); u++) {
									const int x = x0 + u, y = y0 + v;
									const int ax = int(uv.position.x) + u, ay = int(uv.position.y) + v;
									if (x < 0 || y < 0 || x >= width || y >= height || ax >= atlas->get_width() || ay >= atlas->get_height()) {
										continue;
									}
									if (atlas->get_pixel(ax, ay).a >= 0.5f) {
										lit.write[y * width + x] = 1;
									}
								}
							}
						}
					}
				}
				pen += gl.advance;
			}
		}
	}
	ts->free_rid(shaped);

	const int row_bytes = (width + 7) / 8;
	Vector<uint8_t> bits;
	bits.resize_initialized(row_bytes * height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (lit[y * width + x]) {
				bits.write[y * row_bytes + x / 8] |= uint8_t(0x80 >> (x % 8));
			}
		}
	}
	Dictionary picture;
	picture["width"] = width;
	picture["height"] = height;
	picture["pixel"] = 0.5;
	picture["bits"] = CryptoCore::b64_encode_str(bits.ptr(), bits.size());
	return picture;
}

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

void cutscene_free_font() {
	g_subtitle_font.unref();
}

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

Rect2 Cutscene::game_frame(const Vector2 &p_size, bool p_wide) {
	const real_t aspect = p_wide ? real_t(16.0 / 9.0) : real_t(4.0 / 3.0);
	Vector2 size = p_size;
	if (size.x > size.y * aspect) {
		size.x = size.y * aspect;
	} else {
		size.y = size.x / aspect;
	}
	return Rect2((p_size - size) / 2, size);
}

double Cutscene::get_timeline_position() const {
	AnimationPlayer *player = get_timeline();
	if (player == nullptr || player->get_assigned_animation() == StringName() || !player->has_animation(player->get_assigned_animation())) {
		return 0.0;
	}
	return player->get_current_animation_position();
}

PackedStringArray Cutscene::subtitles_at(double p_seconds, const String &p_language) const {
	PackedStringArray rows;
	const String language = !p_language.is_empty() ? p_language : (languages.is_empty() ? String("en") : languages[0]);
	for (int i = 0; i < get_child_count(); i++) {
		const CutsceneSubtitle *line = Object::cast_to<CutsceneSubtitle>(get_child(i));
		if (line == nullptr || p_seconds < line->get_start() || p_seconds >= line->get_start() + line->get_duration()) {
			continue;
		}
		const Dictionary text = line->get_text();
		String said = text.get(language, String());
		if (said.is_empty() && !text.is_empty()) {
			said = text.values()[0];
		}
		for (const String &row : cutscene_math::wrap_subtitle(said)) {
			rows.push_back(row);
		}
	}
	return rows;
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

Dictionary Cutscene::to_cutscene(PackedStringArray *r_problems, Dictionary *r_files) const {
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
			const CutsceneActor *carrier = Object::cast_to<CutsceneActor>(cam->get_node_or_null(cam->get_ride_on()));
			if (!cam->get_ride_on().is_empty() && carrier == nullptr) {
				problems.push_back(vformat("%s rides on %s, which is not an actor: it does not ride", cam->get_name(), String(cam->get_ride_on())));
			}
			for (int t : sample_ticks(tl, cam, length_ticks, cuts)) {
				const double time = t / cutscene_math::TICK_RATE;
				const Transform3D at = game_at(tl, cam, time);
				Vector3 rot = cutscene_math::camera_rotation(-at.basis.get_column(2), at.basis.get_column(1));
				Dictionary key;
				key["t"] = t;
				if (carrier != nullptr) {
					// Against the carrier: its place and turn now, the eye in its axes.
					const Transform3D on = game_at(tl, carrier, time);
					const Vector3 facing = on.basis.get_column(0);
					const double yaw = Math::atan2(double(facing.y), double(facing.x));
					Dictionary eye;
					eye["actor"] = carrier->get_effective_id();
					eye["offset"] = vec3((at.origin - on.origin).rotated(Vector3(0, 0, 1), -yaw));
					eye["turn"] = true;
					key["eye"] = eye;
					rot.z = Math::wrapf(double(rot.z) - yaw, -Math::PI, Math::PI);
				} else {
					key["eye"] = vec3(at.origin);
				}
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
			Array shake;
			for (int c = 0; c < cam->get_child_count(); c++) {
				if (const CutsceneShake *one = Object::cast_to<CutsceneShake>(cam->get_child(c))) {
					Dictionary e;
					e["t"] = ticks(one->get_start());
					e["length"] = MAX(1, ticks(one->get_duration()));
					e["strength"] = tidy(one->get_strength());
					shake.push_back(e);
				}
			}
			if (!shake.is_empty()) {
				camera["shake"] = shake;
			}
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
			// A walk: the first CutscenePath child's points, in the game's space.
			for (int c = 0; c < actor->get_child_count(); c++) {
				const CutscenePath *walk = Object::cast_to<CutscenePath>(actor->get_child(c));
				if (walk == nullptr) {
					continue;
				}
				const Ref<Curve3D> curve = walk->get_curve();
				const Transform3D walk_to_game = to_game_space(walk) * (walk->is_inside_tree() ? walk->get_global_transform() : walk->get_transform());
				Array points;
				for (int k = 0; curve.is_valid() && k < curve->get_point_count(); k++) {
					points.push_back(vec3(walk_to_game.xform(curve->get_point_position(k))));
				}
				if (points.size() < 2) {
					problems.push_back(vformat("actor \"%s\": its path has fewer than two points and is left out", id));
					break;
				}
				Dictionary path;
				path["points"] = points;
				path["speed"] = tidy(walk->get_speed());
				path["ground"] = walk->get_ground();
				path["t"] = ticks(walk->get_start());
				a["path"] = path;
				break;
			}
			// What the editor has no node for, as the file writes it.
			const Dictionary advanced = actor->get_advanced();
			for (const char *part : { "look_at", "held", "layers" }) {
				if (advanced.has(part)) {
					a[part] = advanced[part];
				}
			}
			// Its layers: each joint's turn baked per tick, up to the last key.
			Array layers = Array(a.get("layers", Array())).duplicate();
			for (int c = 0; c < actor->get_child_count(); c++) {
				const CutsceneLayer *l = Object::cast_to<CutsceneLayer>(actor->get_child(c));
				if (l == nullptr) {
					continue;
				}
				const PackedInt32Array joints = l->get_joints();
				if (joints.is_empty()) {
					problems.push_back(vformat("actor \"%s\": layer %s turns no joint and is left out", id, l->get_name()));
					continue;
				}
				int last = 0;
				for (int j : joints) {
					const int k = tl.find(l, Animation::TYPE_VALUE, vformat("joint_%d", j));
					if (k >= 0 && tl.anim->track_get_key_count(k) > 0) {
						last = MAX(last, ticks(tl.anim->track_get_key_time(k, tl.anim->track_get_key_count(k) - 1)));
					}
				}
				last = MIN(last, length_ticks);
				PackedByteArray bytes;
				bytes.resize((last + 1) * joints.size() * 16);
				uint8_t *w = bytes.ptrw();
				for (int t = 0; t <= last; t++) {
					for (int j : joints) {
						const Vector3 degrees = value_at(tl, l, vformat("joint_%d", j), t / cutscene_math::TICK_RATE);
						const Quaternion q = Basis::from_euler(degrees * (Math::PI / 180.0)).get_rotation_quaternion();
						for (real_t v : { q.x, q.y, q.z, q.w }) {
							w += encode_float(float(v), w);
						}
					}
				}
				const String file = vformat("layers/%s_%s_%s.bin", get_cutscene_id(), id, String(l->get_name()).to_snake_case());
				Dictionary layer;
				layer["name"] = String(l->get_name()).to_snake_case();
				Array joint_list;
				for (int j : joints) {
					joint_list.push_back(j);
				}
				layer["joints"] = joint_list;
				const int wk = tl.find(l, Animation::TYPE_VALUE, "weight");
				Array weight;
				if (wk >= 0 && tl.anim->track_get_key_count(wk) > 0) {
					for (int i = 0; i < tl.anim->track_get_key_count(wk); i++) {
						Dictionary key;
						key["t"] = ticks(tl.anim->track_get_key_time(wk, i));
						key["w"] = tidy(CLAMP(double(tl.anim->track_get_key_value(wk, i)), 0.0, 1.0));
						weight.push_back(key);
					}
				} else if (l->get_weight() < 1.0) {
					Dictionary key;
					key["t"] = 0;
					key["w"] = tidy(l->get_weight());
					weight.push_back(key);
				}
				if (!weight.is_empty()) {
					layer["weight"] = weight;
				}
				layer["frames"] = file;
				layers.push_back(layer);
				if (r_files != nullptr) {
					(*r_files)[file] = bytes;
				}
			}
			if (!layers.is_empty()) {
				a["layers"] = layers;
			}
			// The face: the rig without its gaits (they go with the walk), the blink, the moods.
			Dictionary rig = actor->get_rig().duplicate(true);
			const Variant gaits = rig.get("gaits", Variant());
			rig.erase("gaits");
			rig.erase("parts");  // the editor's, for layers' masks
			if (a.has("path") && gaits.get_type() == Variant::ARRAY && !Array(gaits).is_empty()) {
				Dictionary path = a["path"];
				path["gaits"] = gaits;
			}
			Array moods;
			for (int c = 0; c < actor->get_child_count(); c++) {
				const CutsceneMood *m = Object::cast_to<CutsceneMood>(actor->get_child(c));
				if (m == nullptr) {
					continue;
				}
				Dictionary key;
				key["t"] = ticks(m->get_start());
				if (!m->get_mood().is_empty()) {
					key["mood"] = m->get_mood();
					if (!Dictionary(rig.get("moods", Dictionary())).has(m->get_mood())) {
						problems.push_back(vformat("actor \"%s\": its rig has no mood \"%s\"", id, m->get_mood()));
					}
				}
				key["weight"] = tidy(m->get_weight());
				key["blend"] = ticks(m->get_blend());
				moods.push_back(key);
			}
			if (!rig.is_empty() || !moods.is_empty()) {
				Dictionary face;
				if (!rig.is_empty()) {
					face["rig"] = rig;
				}
				if (!actor->get_blink()) {
					face["blink"] = false;
				}
				if (!moods.is_empty()) {
					face["moods"] = moods;
				}
				a["face"] = face;
			}
			if (!actor->get_ride_on().is_empty()) {
				const String carrier = actor_id_of(actor, actor->get_ride_on());
				if (carrier.is_empty() || carrier == id) {
					problems.push_back(vformat("actor \"%s\" rides on %s, which is not another actor: it does not ride", id, String(actor->get_ride_on())));
				} else {
					Array ride;
					Dictionary on;
					on["t"] = ticks(actor->get_ride_start());
					on["actor"] = carrier;
					ride.push_back(on);
					if (actor->get_ride_end() > actor->get_ride_start()) {
						Dictionary off;
						off["t"] = ticks(actor->get_ride_end());
						ride.push_back(off);
					}
					a["ride"] = ride;
				}
			}
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
			// The lines the game's font cannot write, as pictures.
			Dictionary pictures;
			for (const Variant *key = text.next(nullptr); key != nullptr; key = text.next(key)) {
				const String code = *key;
				if (!needs_picture(code, text[*key])) {
					continue;
				}
				String problem;
				const Dictionary picture = subtitle_picture(text[*key], problem);
				if (picture.is_empty()) {
					problems.push_back(vformat("%s: the %s line is not drawn: %s", line->get_name(), code, problem));
				} else {
					pictures[code] = picture;
				}
			}
			if (!pictures.is_empty()) {
				s["bitmap"] = pictures;
			}
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
	// Talking: each voice line on an actor whose rig has a jaw opens its mouth as loud as the line,
	// in each language the line has a WAV for.
	for (int i = 0; i < actors.size(); i++) {
		Dictionary a = actors[i];
		Dictionary face = a.get("face", Dictionary());
		if (!Dictionary(face.get("rig", Dictionary())).has("jaw")) {
			continue;
		}
		Array talk;
		for (const Variant &item : sounds) {
			const Dictionary s = item;
			if (String(s.get("kind", "")) != "voice" || s.get("actor", "") != a.get("id", "") || !s.has("file")) {
				continue;
			}
			Dictionary by_language;
			Dictionary files;
			if (s["file"].get_type() == Variant::DICTIONARY) {
				files = s["file"];
			} else {
				files[""] = s["file"];
			}
			for (const Variant &code : files.keys()) {
				const String file = files[code];
				if (file.get_extension().nocasecmp_to("wav") != 0) {
					problems.push_back(vformat("actor \"%s\": %s is not a WAV, so the mouth does not move with it", a.get("id", ""), file));
					continue;
				}
				const String path = dir.path_join("cutscenes").path_join(file);
				String problem;
				const Vector<uint8_t> envelope = cutscene_math::mouth_envelope(FileAccess::get_file_as_bytes(path), problem);
				if (envelope.is_empty()) {
					problems.push_back(vformat("actor \"%s\": %s: %s, so the mouth does not move with it", a.get("id", ""), file, problem.is_empty() ? String("cannot read it") : problem));
					continue;
				}
				by_language[code] = CryptoCore::b64_encode_str(envelope.ptr(), envelope.size());
			}
			if (by_language.is_empty()) {
				continue;
			}
			Dictionary one;
			one["t"] = s.get("t", 0);
			one["envelope"] = by_language.has("") ? by_language[""] : Variant(by_language);
			talk.push_back(one);
		}
		if (!talk.is_empty()) {
			face["talk"] = talk;
		}
	}
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
	Dictionary files;
	const Dictionary cutscene_data = to_cutscene(&problems, &files);
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
	for (const KeyValue<Variant, Variant> &side : files) {
		const String side_path = folder.path_join(String(side.key));
		DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(side_path.get_base_dir()));
		Ref<FileAccess> out = FileAccess::open(side_path, FileAccess::WRITE, &err);
		ERR_FAIL_COND_V_MSG(out.is_null(), err, "Cannot write " + side_path);
		out->store_buffer(PackedByteArray(side.value));
	}
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
	import_from = p_path.get_base_dir();
	const PackedStringArray problems = from_cutscene(read);
	import_from = String();
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
	struct RideWanted {
		CutsceneActor *actor = nullptr;
		String carrier;
	};
	Vector<RideWanted> ride_wanted; // set once every actor is made

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
		// A walk, as a CutscenePath child with the points in its space.
		const Dictionary walk = a.get("path", Dictionary());
		const Array points = walk.get("points", Array());
		if (points.size() >= 2) {
			CutscenePath *path_node = memnew(CutscenePath);
			path_node->set_name("Path");
			actor->add_child(path_node, true);
			path_node->set_owner(owner);
			// A top-level node: its own transform is its place, the Cutscene's.
			path_node->set_transform(is_inside_tree() ? get_global_transform() : get_transform());
			Ref<Curve3D> curve;
			curve.instantiate();
			for (const Variant &point : points) {
				curve->add_point(from_game.xform(vec3_of(point)));
			}
			path_node->set_curve(curve);
			path_node->set_speed(walk.get("speed", 8.0));
			path_node->set_ground(walk.get("ground", true));
			path_node->set_start(seconds(walk.get("t", 0)));
		} else if (a.has("path")) {
			problems.push_back(vformat("actor \"%s\": its path has fewer than two points and is not kept", id));
		}
		Dictionary advanced;
		for (const char *part : { "look_at", "held" }) {
			if (a.has(part)) {
				advanced[part] = a[part];
			}
		}
		// Layers whose frames are beside the file become layer nodes with a key every other tick
		// (the game's 30 Hz); the rest are kept as written.
		Array kept_layers;
		const String files_from = !import_from.is_empty() ? import_from : scene_dir_of(this).path_join("cutscenes");
		for (const Variant &item : Array(a.get("layers", Array()))) {
			const Dictionary def = item;
			const Array joint_list = def.get("joints", Array());
			const String frames_path = def.get("frames", "");
			const PackedByteArray bytes = frames_path.is_empty() || files_from.is_empty() ? PackedByteArray() : FileAccess::get_file_as_bytes(files_from.path_join(frames_path));
			const int stride = joint_list.size() * 16;
			if (stride == 0 || bytes.is_empty() || bytes.size() % stride != 0) {
				kept_layers.push_back(def);
				continue;
			}
			CutsceneLayer *layer = memnew(CutsceneLayer);
			layer->set_name(String(def.get("name", "layer")).to_pascal_case());
			actor->add_child(layer, true);
			layer->set_owner(owner);
			PackedInt32Array joints;
			for (const Variant &j : joint_list) {
				joints.push_back(j);
			}
			layer->set_joints(joints);
			const NodePath layer_path = root->get_path_to(layer);
			const int count = bytes.size() / stride;
			const uint8_t *r = bytes.ptr();
			for (int t = 0; t < count; t++) {
				if (t % 2 != 0 && t != count - 1) {
					continue;
				}
				for (int j = 0; j < joints.size(); j++) {
					const uint8_t *q = r + t * stride + j * 16;
					const Quaternion turn(decode_float(q), decode_float(q + 4), decode_float(q + 8), decode_float(q + 12));
					const Vector3 degrees = Basis(turn.normalized()).get_euler() * (180.0 / Math::PI);
					const String property = vformat("joint_%d", joints[j]);
					if (t == 0) {
						layer->set(property, degrees);
					}
					if (count > 1) {
						anim->track_insert_key(track_for(anim, NodePath(String(layer_path) + ":" + property), Animation::TYPE_VALUE), t / cutscene_math::TICK_RATE, degrees);
					}
				}
			}
			const Array weight = def.get("weight", Array());
			if (weight.size() == 1) {
				layer->set_weight(Dictionary(weight[0]).get("w", 1.0));
			} else {
				for (const Variant &key : weight) {
					anim->track_insert_key(track_for(anim, NodePath(String(layer_path) + ":weight"), Animation::TYPE_VALUE), seconds(Dictionary(key).get("t", 0)), double(Dictionary(key).get("w", 1.0)));
				}
			}
		}
		if (!kept_layers.is_empty()) {
			advanced["layers"] = kept_layers;
		}
		actor->set_advanced(advanced);
		const Dictionary face = a.get("face", Dictionary());
		Dictionary rig = Dictionary(face.get("rig", Dictionary())).duplicate(true);
		const Variant gaits = Dictionary(a.get("path", Dictionary())).get("gaits", Variant());
		if (gaits.get_type() == Variant::ARRAY && !rig.has("gaits")) {
			rig["gaits"] = gaits;
		}
		actor->set_rig(rig);
		actor->set_blink(face.get("blink", true));
		const Array rides = a.get("ride", Array());
		for (int r = 0; r < rides.size(); r++) {
			const Dictionary on = rides[r];
			if (!on.has("actor")) {
				continue;
			}
			ride_wanted.push_back(RideWanted{ actor, String(on["actor"]) });
			actor->set_ride_start(seconds(on.get("t", 0)));
			if (r + 1 < rides.size() && !Dictionary(rides[r + 1]).has("actor")) {
				actor->set_ride_end(seconds(Dictionary(rides[r + 1]).get("t", 0)));
			}
			if (rides.size() > 2) {
				problems.push_back(vformat("actor \"%s\": only its first ride is kept", id));
			}
			break;
		}
		for (const Variant &item : Array(face.get("moods", Array()))) {
			const Dictionary e = item;
			CutsceneMood *m = memnew(CutsceneMood);
			m->set_name("Mood");
			actor->add_child(m, true);
			m->set_owner(owner);
			m->set_start(seconds(e.get("t", 0)));
			m->set_mood(e.get("mood", ""));
			m->set_weight(e.get("weight", 1.0));
			m->set_blend(seconds(e.get("blend", 10)));
		}
	}

	for (const RideWanted &ride : ride_wanted) {
		if (actor_nodes.has(ride.carrier)) {
			ride.actor->set_ride_on(NodePath("../" + actor_nodes[ride.carrier]));
		} else {
			problems.push_back(vformat("actor \"%s\" rides on \"%s\", who is not an actor here: not kept", ride.actor->get_effective_id(), ride.carrier));
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
			// An eye on an actor: placed against where the file's track has the actor at that tick.
			double carried_yaw = 0.0;
			const Variant eye_value = key.get("eye", Variant());
			if (eye_value.get_type() == Variant::DICTIONARY) {
				const Dictionary on = eye_value;
				const String id = on.get("actor", "");
				Vector3 place;
				double yaw = 0.0;
				for (const Variant &item : Array(p_data.get("actors", Array()))) {
					const Dictionary other = item;
					if (String(other.get("id", "")) != id) {
						continue;
					}
					for (const Variant &k : Array(other.get("track", Array()))) {
						const Dictionary tk = k;
						if (int(tk.get("t", 0)) <= t) {
							place = vec3_of(tk.get("position", Variant()), place);
							yaw = vec3_of(tk.get("rotation", Variant()), Vector3(0, 0, float(yaw))).z;
						}
					}
				}
				const bool turn = on.get("turn", false);
				const Vector3 offset = vec3_of(on.get("offset", Variant()), Vector3(0, 0, 1));
				eye = place + (turn ? offset.rotated(Vector3(0, 0, 1), yaw) : offset);
				carried_yaw = turn ? yaw : 0.0;
				if (actor_nodes.has(id)) {
					cam->set_ride_on(NodePath("../" + actor_nodes[id]));
				} else {
					problems.push_back(vformat("camera key at %d: its eye is on \"%s\", who is not an actor here", t, id));
				}
			} else {
				eye = vec3_of(eye_value, eye);
			}
			const Variant look = key.get("look_at", Variant());
			if (look.get_type() == Variant::ARRAY) {
				const Vector3 at = vec3_of(look);
				forward = (at - eye).length() > 1e-6 ? (at - eye).normalized() : forward;
			} else if (key.has("rotation")) {
				Vector3 rot = vec3_of(key["rotation"]);
				rot.z += carried_yaw;
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
		for (const Variant &item : Array(camera.get("shake", Array()))) {
			const Dictionary e = item;
			CutsceneShake *one = memnew(CutsceneShake);
			one->set_name("Shake");
			cam->add_child(one, true);
			one->set_owner(owner);
			one->set_start(seconds(e.get("t", 0)));
			one->set_duration(seconds(e.get("length", 30)));
			one->set_strength(e.get("strength", 0.3));
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
		} else if (what == "air") {
			event->set_action(CutsceneEvent::ACTION_AIR);
			const Dictionary fog = e.get("fog", Variant());
			event->set_air_fog(!fog.is_empty());
			if (!fog.is_empty()) {
				const Array c = fog.get("colour", Array());
				event->set_colour(c.size() >= 3 ? Color(c[0], c[1], c[2]) : Color(0, 0, 0));
				event->set_fog_near(fog.get("near", 0.0));
				event->set_fog_far(fog.get("far", 100.0));
				event->set_fog_thickness(fog.get("thickness", 1.0));
			}
			const Array back = e.get("background", Array());
			event->set_air_background(back.size() >= 3);
			if (back.size() >= 3) {
				event->set_background(Color(back[0], back[1], back[2]));
			}
			event->set_length(seconds(e.get("length", 30)));
		} else if (what == "slow") {
			event->set_action(CutsceneEvent::ACTION_SLOW);
			event->set_stretch(e.get("stretch", 1.0));
			event->set_length(seconds(e.get("length", 0)));
		} else if (what == "tie_hide") {
			event->set_action(bool(e.get("on", true)) ? CutsceneEvent::ACTION_TIE_HIDE : CutsceneEvent::ACTION_TIE_SHOW);
			event->set_tie(e.get("tie", 0));
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
	ClassDB::bind_static_method("Cutscene", D_METHOD("game_frame", "size", "wide"), &Cutscene::game_frame);
	ClassDB::bind_method(D_METHOD("get_timeline_position"), &Cutscene::get_timeline_position);
	ClassDB::bind_method(D_METHOD("subtitles_at", "seconds", "language"), &Cutscene::subtitles_at, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("to_cutscene"), &Cutscene::_to_cutscene_bind);
	ClassDB::bind_method(D_METHOD("export_problems"), &Cutscene::export_problems);
	ClassDB::bind_method(D_METHOD("export_files"), &Cutscene::export_files);
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

Dictionary Cutscene::export_files() const {
	Dictionary files;
	to_cutscene(nullptr, &files);
	return files;
}

PackedStringArray Cutscene::export_problems() const {
	PackedStringArray problems;
	to_cutscene(&problems);
	return problems;
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
void CutsceneCamera::set_ride_on(const NodePath &p_actor) { ride_on = p_actor; }
NodePath CutsceneCamera::get_ride_on() const { return ride_on; }
bool CutsceneCamera::get_export_fov() const { return export_fov; }

void CutsceneCamera::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cuts", "seconds"), &CutsceneCamera::set_cuts);
	ClassDB::bind_method(D_METHOD("get_cuts"), &CutsceneCamera::get_cuts);
	ClassDB::bind_method(D_METHOD("set_ease_back", "ease"), &CutsceneCamera::set_ease_back);
	ClassDB::bind_method(D_METHOD("get_ease_back"), &CutsceneCamera::get_ease_back);
	ClassDB::bind_method(D_METHOD("set_export_fov", "export"), &CutsceneCamera::set_export_fov);
	ClassDB::bind_method(D_METHOD("set_ride_on", "actor"), &CutsceneCamera::set_ride_on);
	ClassDB::bind_method(D_METHOD("get_ride_on"), &CutsceneCamera::get_ride_on);
	ClassDB::bind_method(D_METHOD("get_export_fov"), &CutsceneCamera::get_export_fov);
	ADD_GROUP("Cutscene", "");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "cuts"), "set_cuts", "get_cuts");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ease_back"), "set_ease_back", "get_ease_back");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "export_fov"), "set_export_fov", "get_export_fov");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "ride_on", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "CutsceneActor"), "set_ride_on", "get_ride_on");
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

Callable CutsceneActor::get_use_known_rig_action() {
	return callable_mp(this, &CutsceneActor::_use_known_rig);
}

// The rig the editor knows for the actor's class (the hero's for a hero figure), in place of its own.
void CutsceneActor::_use_known_rig() {
	const Cutscene *cutscene = Object::cast_to<Cutscene>(get_parent());
	const String game = cutscene != nullptr ? cutscene->get_game() : String("rac1");
	const int cls = source == SOURCE_HERO_FIGURE ? -1 : moby_class;
	const Dictionary known = known_rig(game, cls);
	if (known.is_empty()) {
		WARN_PRINT(vformat("No rig is known for %s: add one to res://cutscene_rigs.json as \"%s/%s\".", cls < 0 ? String("the hero") : vformat("class %d", cls), game, cls < 0 ? String("hero") : itos(cls)));
		return;
	}
	set_rig(known);
	notify_property_list_changed();
	print_line(vformat("Actor \"%s\" has the known rig of %s.", get_effective_id(), cls < 0 ? String("the hero") : vformat("class %d", cls)));
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
		if (rig.is_empty()) {
			const Cutscene *owner_cutscene = Object::cast_to<Cutscene>(get_parent());
			set_rig(known_rig(owner_cutscene != nullptr ? owner_cutscene->get_game() : String("rac1"), moby_class));
		}
		print_line(vformat("Actor \"%s\" is %s (spawn id %d).", get_effective_id(), moby->get_name(), spawn_id));
		return;
	}
	WARN_PRINT("Select a moby of the level too (Ctrl+click it in the scene tree), then press again.");
#endif
}

void CutsceneActor::set_advanced(const Dictionary &p_advanced) { advanced = p_advanced; }
void CutsceneActor::set_rig(const Dictionary &p_rig) { rig = p_rig; }
void CutsceneActor::set_ride_on(const NodePath &p_actor) { ride_on = p_actor; }
NodePath CutsceneActor::get_ride_on() const { return ride_on; }
void CutsceneActor::set_ride_start(double p_seconds) { ride_start = MAX(0.0, p_seconds); }
double CutsceneActor::get_ride_start() const { return ride_start; }
void CutsceneActor::set_ride_end(double p_seconds) { ride_end = MAX(0.0, p_seconds); }
double CutsceneActor::get_ride_end() const { return ride_end; }
Dictionary CutsceneActor::get_rig() const { return rig; }
void CutsceneActor::set_blink(bool p_blink) { blink = p_blink; }
bool CutsceneActor::get_blink() const { return blink; }

Dictionary CutsceneActor::known_rig(const String &p_game, int p_class) {
	const String key = vformat("%s/%s", p_game, p_class < 0 ? String("hero") : itos(p_class));
	// A project's own rigs first, then the editor's.
	for (const String &text : { FileAccess::exists("res://cutscene_rigs.json") ? FileAccess::get_file_as_string("res://cutscene_rigs.json") : String(), String::utf8(cutscene_rigs::BUILT_IN) }) {
		if (text.is_empty()) {
			continue;
		}
		const Variant parsed = JSON::parse_string(text);
		if (parsed.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary rigs = parsed;
		if (rigs.has(key) && rigs[key].get_type() == Variant::DICTIONARY) {
			return Dictionary(rigs[key]).duplicate(true);
		}
	}
	return Dictionary();
}
Dictionary CutsceneActor::get_advanced() const { return advanced; }

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
	ClassDB::bind_method(D_METHOD("set_advanced", "advanced"), &CutsceneActor::set_advanced);
	ClassDB::bind_method(D_METHOD("get_advanced"), &CutsceneActor::get_advanced);
	ClassDB::bind_method(D_METHOD("get_use_known_rig_action"), &CutsceneActor::get_use_known_rig_action);
	ClassDB::bind_method(D_METHOD("set_rig", "rig"), &CutsceneActor::set_rig);
	ClassDB::bind_method(D_METHOD("set_ride_on", "actor"), &CutsceneActor::set_ride_on);
	ClassDB::bind_method(D_METHOD("get_ride_on"), &CutsceneActor::get_ride_on);
	ClassDB::bind_method(D_METHOD("set_ride_start", "seconds"), &CutsceneActor::set_ride_start);
	ClassDB::bind_method(D_METHOD("get_ride_start"), &CutsceneActor::get_ride_start);
	ClassDB::bind_method(D_METHOD("set_ride_end", "seconds"), &CutsceneActor::set_ride_end);
	ClassDB::bind_method(D_METHOD("get_ride_end"), &CutsceneActor::get_ride_end);
	ClassDB::bind_method(D_METHOD("get_rig"), &CutsceneActor::get_rig);
	ClassDB::bind_method(D_METHOD("set_blink", "blink"), &CutsceneActor::set_blink);
	ClassDB::bind_method(D_METHOD("get_blink"), &CutsceneActor::get_blink);
	ClassDB::bind_static_method("CutsceneActor", D_METHOD("known_rig", "game", "moby_class"), &CutsceneActor::known_rig);

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
	ADD_GROUP("Ride", "ride_");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "ride_on", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "CutsceneActor"), "set_ride_on", "get_ride_on");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ride_start", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"), "set_ride_start", "get_ride_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ride_end", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"), "set_ride_end", "get_ride_end");
	ADD_GROUP("Face", "");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "use_known_rig", PROPERTY_HINT_TOOL_BUTTON, "Use Known Rig,Skeleton3D", PROPERTY_USAGE_EDITOR), "", "get_use_known_rig_action");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "rig", PROPERTY_HINT_DICTIONARY_TYPE, "String;Variant"), "set_rig", "get_rig");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "blink"), "set_blink", "get_blink");
	ADD_GROUP("Advanced", "");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "advanced", PROPERTY_HINT_DICTIONARY_TYPE, "String;Variant"), "set_advanced", "get_advanced");

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
// CutscenePath

CutscenePath::CutscenePath() {
	set_as_top_level(true);
}

void CutscenePath::set_start(double p_seconds) { start = MAX(0.0, p_seconds); }
double CutscenePath::get_start() const { return start; }
void CutscenePath::set_speed(double p_speed) { speed = MAX(0.01, p_speed); }
double CutscenePath::get_speed() const { return speed; }
void CutscenePath::set_ground(bool p_ground) { ground = p_ground; }
bool CutscenePath::get_ground() const { return ground; }

PackedStringArray CutscenePath::get_configuration_warnings() const {
	PackedStringArray warnings = Path3D::get_configuration_warnings();
	if (Object::cast_to<CutsceneActor>(get_parent()) == nullptr) {
		warnings.push_back(RTR("A path is walked only as a child of a CutsceneActor."));
	}
	if (get_curve().is_null() || get_curve()->get_point_count() < 2) {
		warnings.push_back(RTR("Add at least two points (select the path and click in the 3D view)."));
	}
	return warnings;
}

void CutscenePath::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_start", "seconds"), &CutscenePath::set_start);
	ClassDB::bind_method(D_METHOD("get_start"), &CutscenePath::get_start);
	ClassDB::bind_method(D_METHOD("set_speed", "speed"), &CutscenePath::set_speed);
	ClassDB::bind_method(D_METHOD("get_speed"), &CutscenePath::get_speed);
	ClassDB::bind_method(D_METHOD("set_ground", "ground"), &CutscenePath::set_ground);
	ClassDB::bind_method(D_METHOD("get_ground"), &CutscenePath::get_ground);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"), "set_start", "get_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed", PROPERTY_HINT_RANGE, "0.01,40,0.01,or_greater,suffix:u/s"), "set_speed", "get_speed");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ground"), "set_ground", "get_ground");
}

// ------------------------------------------------------------------------------------------------
// CutsceneShake

void CutsceneShake::set_start(double p_seconds) { start = MAX(0.0, p_seconds); }
double CutsceneShake::get_start() const { return start; }
void CutsceneShake::set_duration(double p_seconds) { duration = MAX(0.0, p_seconds); }
double CutsceneShake::get_duration() const { return duration; }
void CutsceneShake::set_strength(double p_strength) { strength = MAX(0.0, p_strength); }
double CutsceneShake::get_strength() const { return strength; }

PackedStringArray CutsceneShake::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();
	if (Object::cast_to<CutsceneCamera>(get_parent()) == nullptr) {
		warnings.push_back(RTR("A shake moves the camera only as a child of a CutsceneCamera."));
	}
	return warnings;
}

void CutsceneShake::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_start", "seconds"), &CutsceneShake::set_start);
	ClassDB::bind_method(D_METHOD("get_start"), &CutsceneShake::get_start);
	ClassDB::bind_method(D_METHOD("set_duration", "seconds"), &CutsceneShake::set_duration);
	ClassDB::bind_method(D_METHOD("get_duration"), &CutsceneShake::get_duration);
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &CutsceneShake::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &CutsceneShake::get_strength);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"), "set_start", "get_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_duration", "get_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater"), "set_strength", "get_strength");
}

// ------------------------------------------------------------------------------------------------
// CutsceneMood

void CutsceneLayer::set_joints(const PackedInt32Array &p_joints) {
	PackedInt32Array kept;
	for (int j : p_joints) {
		if (j >= 0 && j < 256 && !kept.has(j)) {
			kept.push_back(j);
		}
	}
	joints = kept;
	notify_property_list_changed();
	update_configuration_warnings();
}
PackedInt32Array CutsceneLayer::get_joints() const { return joints; }
void CutsceneLayer::set_weight(double p_weight) { weight = CLAMP(p_weight, 0.0, 1.0); }
double CutsceneLayer::get_weight() const { return weight; }
void CutsceneLayer::set_turn(int p_joint, const Vector3 &p_degrees) { turns[p_joint] = p_degrees; }
Vector3 CutsceneLayer::get_turn(int p_joint) const {
	const Vector3 *found = turns.getptr(p_joint);
	return found != nullptr ? *found : Vector3();
}

void CutsceneLayer::use_part(const String &p_part) {
	const CutsceneActor *actor = Object::cast_to<CutsceneActor>(get_parent());
	ERR_FAIL_NULL_MSG(actor, "A layer takes a part of its actor's rig: put it under a CutsceneActor.");
	const Array range = Dictionary(actor->get_rig().get("parts", Dictionary())).get(p_part, Array());
	ERR_FAIL_COND_MSG(range.size() != 2, vformat("The actor's rig has no part \"%s\".", p_part));
	PackedInt32Array part;
	for (int j = int(range[0]); j <= int(range[1]); j++) {
		part.push_back(j);
	}
	set_joints(part);
}

bool CutsceneLayer::_set(const StringName &p_name, const Variant &p_value) {
	const String name = p_name;
	if (name.begins_with("joint_") && name.substr(6).is_valid_int()) {
		turns[name.substr(6).to_int()] = p_value;
		return true;
	}
	if (name == "use_part" && !String(p_value).is_empty()) {
		use_part(p_value);
		return true;
	}
	return false;
}

bool CutsceneLayer::_get(const StringName &p_name, Variant &r_ret) const {
	const String name = p_name;
	if (name.begins_with("joint_") && name.substr(6).is_valid_int()) {
		r_ret = get_turn(name.substr(6).to_int());
		return true;
	}
	if (name == "use_part") {
		r_ret = String();
		return true;
	}
	return false;
}

void CutsceneLayer::_get_property_list(List<PropertyInfo> *p_list) const {
	// A part of the actor's rig to take as the mask (editor only, not stored).
	if (const CutsceneActor *actor = Object::cast_to<CutsceneActor>(get_parent())) {
		const Dictionary parts = actor->get_rig().get("parts", Dictionary());
		if (!parts.is_empty()) {
			p_list->push_back(PropertyInfo(Variant::STRING, "use_part", PROPERTY_HINT_ENUM, String(",").join(PackedStringArray(Variant(parts.keys()))), PROPERTY_USAGE_EDITOR));
		}
	}
	// Each joint of the mask, keyable on the timeline (the key icon beside it).
	p_list->push_back(PropertyInfo(Variant::NIL, "Joints", PROPERTY_HINT_NONE, "joint_", PROPERTY_USAGE_GROUP));
	for (int j : joints) {
		p_list->push_back(PropertyInfo(Variant::VECTOR3, vformat("joint_%d", j), PROPERTY_HINT_RANGE, "-180,180,0.1,suffix:°"));
	}
}

PackedStringArray CutsceneLayer::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();
	if (Object::cast_to<CutsceneActor>(get_parent()) == nullptr) {
		warnings.push_back(RTR("A layer turns joints only as a child of a CutsceneActor."));
	}
	if (joints.is_empty()) {
		warnings.push_back(RTR("List the joints this layer turns (its mask), or pick a Use Part."));
	}
	return warnings;
}

void CutsceneLayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_joints", "joints"), &CutsceneLayer::set_joints);
	ClassDB::bind_method(D_METHOD("get_joints"), &CutsceneLayer::get_joints);
	ClassDB::bind_method(D_METHOD("set_weight", "weight"), &CutsceneLayer::set_weight);
	ClassDB::bind_method(D_METHOD("get_weight"), &CutsceneLayer::get_weight);
	ClassDB::bind_method(D_METHOD("set_turn", "joint", "degrees"), &CutsceneLayer::set_turn);
	ClassDB::bind_method(D_METHOD("get_turn", "joint"), &CutsceneLayer::get_turn);
	ClassDB::bind_method(D_METHOD("use_part", "part"), &CutsceneLayer::use_part);
	ClassDB::bind_method(D_METHOD("get_warnings"), &CutsceneLayer::get_configuration_warnings);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "joints"), "set_joints", "get_joints");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weight", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_weight", "get_weight");
}

void CutsceneMood::set_start(double p_seconds) { start = MAX(0.0, p_seconds); }
double CutsceneMood::get_start() const { return start; }
void CutsceneMood::set_mood(const String &p_mood) {
	mood = p_mood;
	update_configuration_warnings();
}
String CutsceneMood::get_mood() const { return mood; }
void CutsceneMood::set_weight(double p_weight) { weight = CLAMP(p_weight, 0.0, 1.0); }
double CutsceneMood::get_weight() const { return weight; }
void CutsceneMood::set_blend(double p_seconds) { blend = MAX(0.0, p_seconds); }
double CutsceneMood::get_blend() const { return blend; }

PackedStringArray CutsceneMood::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();
	const CutsceneActor *actor = Object::cast_to<CutsceneActor>(get_parent());
	if (actor == nullptr) {
		warnings.push_back(RTR("A mood shows only as a child of a CutsceneActor."));
	} else if (!mood.is_empty() && !Dictionary(actor->get_rig().get("moods", Dictionary())).has(mood)) {
		warnings.push_back(RTR("The actor's rig has no such mood: add its joints to the rig's \"moods\"."));
	}
	return warnings;
}

void CutsceneMood::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_warnings"), &CutsceneMood::get_configuration_warnings);
	ClassDB::bind_method(D_METHOD("set_start", "seconds"), &CutsceneMood::set_start);
	ClassDB::bind_method(D_METHOD("get_start"), &CutsceneMood::get_start);
	ClassDB::bind_method(D_METHOD("set_mood", "mood"), &CutsceneMood::set_mood);
	ClassDB::bind_method(D_METHOD("get_mood"), &CutsceneMood::get_mood);
	ClassDB::bind_method(D_METHOD("set_weight", "weight"), &CutsceneMood::set_weight);
	ClassDB::bind_method(D_METHOD("get_weight"), &CutsceneMood::get_weight);
	ClassDB::bind_method(D_METHOD("set_blend", "seconds"), &CutsceneMood::set_blend);
	ClassDB::bind_method(D_METHOD("get_blend"), &CutsceneMood::get_blend);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"), "set_start", "get_start");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "mood", PROPERTY_HINT_PLACEHOLDER_TEXT, "happy, angry, ... (empty: the face's own)"), "set_mood", "get_mood");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weight", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_weight", "get_weight");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s"), "set_blend", "get_blend");
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
void CutsceneEvent::set_air_fog(bool p_on) { air_fog = p_on; }
bool CutsceneEvent::get_air_fog() const { return air_fog; }
void CutsceneEvent::set_fog_near(float p_near) { fog_near = MAX(0.0f, p_near); }
float CutsceneEvent::get_fog_near() const { return fog_near; }
void CutsceneEvent::set_fog_far(float p_far) { fog_far = MAX(0.0f, p_far); }
float CutsceneEvent::get_fog_far() const { return fog_far; }
void CutsceneEvent::set_fog_thickness(float p_thickness) { fog_thickness = CLAMP(p_thickness, 0.0f, 1.0f); }
float CutsceneEvent::get_fog_thickness() const { return fog_thickness; }
void CutsceneEvent::set_air_background(bool p_on) { air_background = p_on; }
bool CutsceneEvent::get_air_background() const { return air_background; }
void CutsceneEvent::set_background(const Color &p_colour) { background = p_colour; }
Color CutsceneEvent::get_background() const { return background; }
void CutsceneEvent::set_stretch(float p_stretch) { stretch = CLAMP(p_stretch, 1.0f, 8.0f); }
float CutsceneEvent::get_stretch() const { return stretch; }
void CutsceneEvent::set_tie(int p_tie) { tie = CLAMP(p_tie, 0, 0xFFFF); }
int CutsceneEvent::get_tie() const { return tie; }

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
		case ACTION_AIR: {
			// Neither fog nor background: back to the game's.
			e["do"] = "air";
			if (air_fog) {
				Dictionary fog;
				Array c;
				c.push_back(tidy(colour.r));
				c.push_back(tidy(colour.g));
				c.push_back(tidy(colour.b));
				fog["colour"] = c;
				fog["near"] = tidy(fog_near);
				fog["far"] = tidy(MAX(fog_far, fog_near + 0.01f));
				fog["thickness"] = tidy(fog_thickness);
				e["fog"] = fog;
			}
			if (air_background) {
				Array b;
				b.push_back(tidy(background.r));
				b.push_back(tidy(background.g));
				b.push_back(tidy(background.b));
				e["background"] = b;
			}
			e["length"] = ticks(length);
		} break;
		case ACTION_SLOW:
			e["do"] = "slow";
			e["stretch"] = tidy(stretch);
			e["length"] = ticks(length);
			break;
		case ACTION_TIE_HIDE:
		case ACTION_TIE_SHOW:
			e["do"] = "tie_hide";
			e["tie"] = tie;
			e["on"] = action == ACTION_TIE_HIDE;
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
	const bool timed = action == ACTION_HAZE || action == ACTION_FADE || action == ACTION_AIR || action == ACTION_SLOW;
	const bool fog = action == ACTION_AIR && air_fog;
	if ((name == "air_fog" || name == "air_background") && action != ACTION_AIR) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if ((name == "fog_near" || name == "fog_far" || name == "fog_thickness") && !fog) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if ((name == "background" && !(action == ACTION_AIR && air_background)) || (name == "stretch" && action != ACTION_SLOW) ||
			(name == "tie" && action != ACTION_TIE_HIDE && action != ACTION_TIE_SHOW)) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if ((name == "spawn_id" && !moby) || (name == "level" && action != ACTION_MISSION_FLAG && action != ACTION_TRAVEL) ||
			((name == "flag_byte" || name == "flag_value") && action != ACTION_MISSION_FLAG) ||
			((name == "position" || name == "radius" || name == "strength") && action != ACTION_HAZE) ||
			(name == "length" && !timed) || (name == "colour" && action != ACTION_FADE && !fog) ||
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
	ClassDB::bind_method(D_METHOD("set_air_fog", "on"), &CutsceneEvent::set_air_fog);
	ClassDB::bind_method(D_METHOD("get_air_fog"), &CutsceneEvent::get_air_fog);
	ClassDB::bind_method(D_METHOD("set_fog_near", "near"), &CutsceneEvent::set_fog_near);
	ClassDB::bind_method(D_METHOD("get_fog_near"), &CutsceneEvent::get_fog_near);
	ClassDB::bind_method(D_METHOD("set_fog_far", "far"), &CutsceneEvent::set_fog_far);
	ClassDB::bind_method(D_METHOD("get_fog_far"), &CutsceneEvent::get_fog_far);
	ClassDB::bind_method(D_METHOD("set_fog_thickness", "thickness"), &CutsceneEvent::set_fog_thickness);
	ClassDB::bind_method(D_METHOD("get_fog_thickness"), &CutsceneEvent::get_fog_thickness);
	ClassDB::bind_method(D_METHOD("set_air_background", "on"), &CutsceneEvent::set_air_background);
	ClassDB::bind_method(D_METHOD("get_air_background"), &CutsceneEvent::get_air_background);
	ClassDB::bind_method(D_METHOD("set_background", "colour"), &CutsceneEvent::set_background);
	ClassDB::bind_method(D_METHOD("get_background"), &CutsceneEvent::get_background);
	ClassDB::bind_method(D_METHOD("set_stretch", "stretch"), &CutsceneEvent::set_stretch);
	ClassDB::bind_method(D_METHOD("get_stretch"), &CutsceneEvent::get_stretch);
	ClassDB::bind_method(D_METHOD("set_tie", "tie"), &CutsceneEvent::set_tie);
	ClassDB::bind_method(D_METHOD("get_tie"), &CutsceneEvent::get_tie);
	ClassDB::bind_method(D_METHOD("to_event"), &CutsceneEvent::to_event);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start", PROPERTY_HINT_RANGE, "0,600,0.01,or_greater,suffix:s"), "set_start", "get_start");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "action", PROPERTY_HINT_ENUM, "Hide Moby,Show Moby,Delete Moby,Mission Flag,Heat Haze,Fade,Play Cutscene,Travel,Fog and Background,Slow Motion,Hide Tie,Show Tie"), "set_action", "get_action");
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
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "air_fog", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), "set_air_fog", "get_air_fog");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fog_near", PROPERTY_HINT_RANGE, "0,500,0.1,or_greater"), "set_fog_near", "get_fog_near");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fog_far", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater"), "set_fog_far", "get_fog_far");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fog_thickness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_fog_thickness", "get_fog_thickness");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "air_background", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), "set_air_background", "get_air_background");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "background", PROPERTY_HINT_COLOR_NO_ALPHA), "set_background", "get_background");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stretch", PROPERTY_HINT_RANGE, "1,8,0.1"), "set_stretch", "get_stretch");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tie", PROPERTY_HINT_RANGE, "0,65535,1"), "set_tie", "get_tie");

	BIND_ENUM_CONSTANT(ACTION_MOBY_HIDE);
	BIND_ENUM_CONSTANT(ACTION_MOBY_SHOW);
	BIND_ENUM_CONSTANT(ACTION_MOBY_KILL);
	BIND_ENUM_CONSTANT(ACTION_MISSION_FLAG);
	BIND_ENUM_CONSTANT(ACTION_HAZE);
	BIND_ENUM_CONSTANT(ACTION_FADE);
	BIND_ENUM_CONSTANT(ACTION_PLAY);
	BIND_ENUM_CONSTANT(ACTION_TRAVEL);
	BIND_ENUM_CONSTANT(ACTION_AIR);
	BIND_ENUM_CONSTANT(ACTION_SLOW);
	BIND_ENUM_CONSTANT(ACTION_TIE_HIDE);
	BIND_ENUM_CONSTANT(ACTION_TIE_SHOW);
}
