// SPDX-License-Identifier: MIT
// OpenRAC cutscene player: plays the .cutscene files OpenRAC Editor packs with a level
// (docs/CUTSCENE_FORMAT.md), through the port's mod interface only. It reads
// <pack>/level_NN/cutscenes/*.cutscene when a level loads, starts each one when its trigger
// happens, and drives the camera, the actors, the subtitles, the sounds and the events tick by tick.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "openrac/mod.hpp"
#include "timeline.hpp"

namespace {

namespace fs = std::filesystem;
using namespace cutscene;

constexpr int kCameraEaseOut = 30;  // ticks from the last key back to the game's camera
constexpr int kMaxVariantHops = 8;  // variants that lead to variants: a loop stops here

// ---------------------------------------------------------------------------------------------
// The port

const openrac_api* g_api = nullptr;
openrac_mod* g_mod = nullptr;

// A UTF-8 path as the file system's.
fs::path utf8_path(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

void log(int level, const std::string& text) { g_api->log(g_mod, level, text.c_str()); }

// ---------------------------------------------------------------------------------------------
// The level's cutscenes

struct Cutscene {
    std::string name;  // the file's name without .cutscene: what "after" and "play" name
    std::string pack_dir;  // "level_03/cutscenes", for "pack:" paths of its sounds and meshes
    Value doc;
    int length = 0;
    // Trigger state.
    bool inside = false;  // volume: the hero was in it last tick
    bool present = false;  // pickup: the moby was there last tick
    bool flag_was = true;  // flag: last tick's value (true at load, so a flag set before is no start)
};

struct Level {
    int number = -1;
    std::uint32_t loads = 0xFFFFFFFFu;
    std::vector<Cutscene> scenes;
    bool started = false;  // level_start triggers ran

    Cutscene* find(const std::string& name) {
        for (Cutscene& c : scenes) {
            if (c.name == name) return &c;
        }
        return nullptr;
    }
};
Level g_level;

std::string pack_folder() {
    char buf[4096];
    return g_api->pack_folder(buf, sizeof(buf)) > 0 ? std::string(buf) : std::string();
}

void load_level(int level) {
    g_level.scenes.clear();
    g_level.number = level;
    g_level.started = false;
    const std::string pack = pack_folder();
    if (pack.empty() || level < 0) return;
    const std::string rel = std::format("level_{:02d}/cutscenes", level);
    const fs::path dir = utf8_path(pack) / rel;
    std::error_code e;
    if (!fs::is_directory(dir, e)) return;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir, e)) {
        if (entry.path().extension() == ".cutscene") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        std::string error;
        Value doc = Value::parse(text.str(), error);
        if (!error.empty() || doc["format"].string() != "openrac-cutscene") {
            log(OPENRAC_LOG_WARN, std::format("{}: not a cutscene ({})", file.filename().string(),
                                              error.empty() ? "no \"format\"" : error));
            continue;
        }
        if (doc["version"].integer() > 1) {
            log(OPENRAC_LOG_INFO, std::format("{}: version {}, playing the parts this player knows",
                                              file.filename().string(), doc["version"].integer()));
        }
        if (doc["game"].is_string() && doc["game"].string() != g_api->game) continue;
        Cutscene c;
        c.name = file.stem().string();
        c.pack_dir = rel;
        c.length = std::max(1, doc["length"].integer(1));
        c.doc = std::move(doc);
        g_level.scenes.push_back(std::move(c));
    }
    if (!g_level.scenes.empty()) {
        log(OPENRAC_LOG_INFO, std::format("level {}: {} cutscene(s)", level, g_level.scenes.size()));
    }
}

// ---------------------------------------------------------------------------------------------
// Cutscenes played once per save, in the mod's data folder

class Seen {
public:
    bool has(const std::string& key) {
        sync();
        return m_keys.count(key) != 0;
    }
    void add(const std::string& key) {
        sync();
        m_keys.insert(key);
        save();
    }

private:
    // The save in play decides the file; a game not saved yet keeps its list in memory until it is.
    void sync() {
        const int slot = g_api->save_slot_current();
        if (slot == m_slot) return;
        const bool was_unsaved = m_slot == -1;
        std::set<std::string> carried = was_unsaved ? m_keys : std::set<std::string>{};
        m_slot = slot;
        m_keys.clear();
        if (slot >= 0) {
            std::ifstream in(file());
            for (std::string line; std::getline(in, line);) {
                if (!line.empty()) m_keys.insert(line);
            }
            m_keys.insert(carried.begin(), carried.end());
            if (!carried.empty()) save();
        }
    }
    fs::path file() const {
        const char* dir = g_api->data_dir(g_mod);
        return dir == nullptr ? fs::path() : utf8_path(dir) / std::format("seen-slot{}.txt", m_slot);
    }
    void save() const {
        if (m_slot < 0) return;
        const fs::path f = file();
        if (f.empty()) return;
        std::ofstream out(f, std::ios::trunc);
        for (const std::string& k : m_keys) out << k << '\n';
    }

    int m_slot = -2;
    std::set<std::string> m_keys;
};
Seen g_seen;

std::string seen_key(const Cutscene& c) { return std::format("{}/{}", g_level.number, c.name); }

bool flag_holds(const Value& cond) {
    if (!cond.is_object()) return true;
    std::uint8_t state[OPENRAC_MISSION_STATE_SIZE] = {};
    const int level = cond["level"].integer(g_level.number);
    if (g_api->mission_state_read(level, state) != OPENRAC_HOOK_OK) return false;
    const int byte = cond["byte"].integer();
    if (byte < 0 || byte >= OPENRAC_MISSION_STATE_SIZE) return false;
    return state[byte] == cond["is"].integer(1);
}

// ---------------------------------------------------------------------------------------------
// The level's mobys, read once a tick while a cutscene plays

std::vector<openrac_moby> g_mobys;
const openrac_moby* moby_at(int slot) {
    for (const openrac_moby& m : g_mobys) {
        if (m.slot == slot) return &m;
    }
    return nullptr;
}
void read_mobys() {
    g_mobys.resize(1024);
    for (openrac_moby& m : g_mobys) m.size = sizeof(openrac_moby);
    int n = g_api->moby_list(g_mobys.data(), static_cast<int>(g_mobys.size()));
    if (n > static_cast<int>(g_mobys.size())) {
        g_mobys.resize(static_cast<std::size_t>(n));
        for (openrac_moby& m : g_mobys) m.size = sizeof(openrac_moby);
        n = g_api->moby_list(g_mobys.data(), n);
    }
    g_mobys.resize(static_cast<std::size_t>(std::max(0, std::min(n, static_cast<int>(g_mobys.size())))));
}

bool hero_pose(openrac_pose& out) {
    out = {};
    out.size = sizeof(openrac_pose);
    return g_api->hero_pose(&out) == OPENRAC_HOOK_OK;
}

// ---------------------------------------------------------------------------------------------
// Pad: Start skips; Circle talks. Buttons are active-low in the report's first two bytes.

std::uint16_t g_pressed = 0;  // newly pressed since the last tick
std::uint16_t g_held = 0;
bool g_swallow_start = false;  // Start is the player's, not the game's pause: while a cutscene
                               // plays, and after it until the press that skipped it is let go
bool g_cutscene_on = false;
bool g_talk_ready = false;  // a talk trigger is in reach: Circle is the player's
bool g_talk_pressed = false;

void on_pad(int port, std::uint8_t* report, int size) {
    if (port != 0 || report == nullptr || size < 2) return;
    const std::uint16_t held = static_cast<std::uint16_t>(~(report[0] | (report[1] << 8)));
    g_pressed |= static_cast<std::uint16_t>(held & ~g_held);
    g_held = held;
    if (g_swallow_start) {
        if (held & OPENRAC_PAD_START) {
            report[0] |= static_cast<std::uint8_t>(OPENRAC_PAD_START);
        } else if (!g_cutscene_on) {
            // The press that skipped is over: Start is the game's again.
            g_swallow_start = false;
        }
    }
    if (g_talk_ready && (held & OPENRAC_PAD_CIRCLE)) {
        g_talk_pressed = true;
        report[1] |= static_cast<std::uint8_t>(OPENRAC_PAD_CIRCLE >> 8);
    }
}

// ---------------------------------------------------------------------------------------------
// Playing one cutscene

struct ActorRun {
    const Value* def = nullptr;
    std::string id;
    enum class Kind { None, Moby, Spawned, Figure, Prop } kind = Kind::None;
    int slot = -1;
    int figure = -1;
    int prop = -1;
    openrac_moby original{};  // a level moby as it was, for "end": "restore"
    int anim = -1;  // the anims entry playing
    int visible = -1;  // last visibility asked
    V3 position;  // where it is this tick, for look_at and sounds
    V3 rotation;
    float scale = 1;
    float base_scale = 0;  // its own size, which a track's scale multiplies (0: not read yet)
};

struct Voice {
    std::uint32_t voice = 0;
    int start = 0;
    int fade_in = 0, fade_out = 0, end = -1;
    float volume = 1;
};

struct Play {
    Cutscene* scene = nullptr;
    const Value* doc = nullptr;
    int t = 0;
    bool started = false;  // the hero is held and the first tick ran
    bool skipped = false;
    int ease_out = -1;  // ticks into the camera's way back, -1 before
    std::vector<ActorRun> actors;
    std::vector<char> events_done;
    std::vector<char> sounds_done;
    std::vector<Voice> voices;
    struct Haze {
        openrac_haze patch;
        int until;
    };
    std::vector<Haze> haze;
    std::string after;  // a cutscene an event asked to play next
    int travel = -1;
    std::set<std::string> warned;
    CameraPose last_camera;  // the last picture of the cutscene's camera, for the way back
    bool has_camera = false;
    bool hero_hidden = false;
};
std::unique_ptr<Play> g_play;
// What the overlay draws, set on the game's thread and read on the window's.
std::atomic<float> g_bars{0};  // the letterbox, 0 to 1
std::atomic<std::uint32_t> g_fade{0};  // the colour over the picture, 0xRRGGBBAA
bool g_has_overlay = false;  // the port draws for mods
std::string g_language;
bool g_subtitles = true;  // the "subtitles" setting
std::map<std::string, int> g_sounds;  // pack path -> sound id (kept until the program ends)

void warn_once(Play& p, const std::string& what) {
    if (p.warned.insert(what).second) {
        log(OPENRAC_LOG_INFO, std::format("{}: {} is not played by this version", p.scene->name, what));
    }
}

ActorRun* actor(Play& p, const std::string& id) {
    for (ActorRun& a : p.actors) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

// A point the camera or an actor looks at: [x, y, z], or { "actor": id, "offset": [x, y, z] }.
bool target_point(Play& p, const Value& v, V3& out) {
    if (v.is_array()) {
        out = vec(v);
        return true;
    }
    if (!v.is_object()) return false;
    const std::string id = v["actor"].string();
    const V3 offset = vec(v["offset"], {0, 0, 1});
    if (v.has("joint")) warn_once(p, "a joint as a look-at target");
    if (ActorRun* a = actor(p, id)) {
        out = a->position + offset;
        return true;
    }
    openrac_pose hero;
    if (id == "hero" && hero_pose(hero)) {
        out = V3{hero.position[0], hero.position[1], hero.position[2]} + offset;
        return true;
    }
    return false;
}

std::string pick_language(const Value& by_language, const Value& languages) {
    return cutscene::pick_language(by_language, languages, g_language);
}

int sound_id(Play& p, const std::string& file) {
    const std::string path = "pack:" + p.scene->pack_dir + "/" + file;
    auto it = g_sounds.find(path);
    if (it != g_sounds.end()) return it->second;
    const int id = g_api->sound_load(g_mod, path.c_str());
    if (id < 0) log(OPENRAC_LOG_WARN, std::format("{}: cannot load {}", p.scene->name, file));
    g_sounds[path] = id;
    return id;
}

// --- Actors

void actors_begin(Play& p) {
    const Value& list = (*p.doc)["actors"];
    for (const Value& def : list.items()) {
        ActorRun a;
        a.def = &def;
        a.id = def["id"].string();
        const Value& src = def["source"];
        // A first pose for actors made here: the track's first key.
        openrac_moby first{};
        first.size = sizeof(openrac_moby);
        const Value& key0 = def["track"][0];
        const V3 pos = vec(key0["position"]), rot = vec(key0["rotation"]);
        std::memcpy(first.position, &pos, sizeof(float) * 3);
        std::memcpy(first.rotation, &rot, sizeof(float) * 3);
        first.scale = 0;  // the class's own size; the track's scale multiplies it from the first tick
        if (src.has("spawn_id")) {
            a.slot = g_api->moby_slot(src["spawn_id"].integer());
            if (a.slot >= 0) {
                if (const openrac_moby* m = moby_at(a.slot)) a.original = *m;
                a.kind = g_api->moby_drive(g_mod, a.slot, 1) == OPENRAC_HOOK_OK ? ActorRun::Kind::Moby
                                                                                 : ActorRun::Kind::None;
            } else if (def["missing"].string_or("spawn") == "spawn" && src.has("class")) {
                a.slot = g_api->moby_spawn(g_mod, src["class"].integer(), &first);
                a.kind = a.slot >= 0 ? ActorRun::Kind::Spawned : ActorRun::Kind::None;
            }
        } else if (src.has("class")) {
            a.slot = g_api->moby_spawn(g_mod, src["class"].integer(), &first);
            a.kind = a.slot >= 0 ? ActorRun::Kind::Spawned : ActorRun::Kind::None;
        } else if (src.has("figure")) {
            a.figure = g_api->figure_create(g_mod);
            a.kind = a.figure >= 0 ? ActorRun::Kind::Figure : ActorRun::Kind::None;
        } else if (src.has("prop")) {
            const std::string path = "pack:" + p.scene->pack_dir + "/" + src["prop"].string();
            a.prop = g_api->prop_create(g_mod, path.c_str());
            a.kind = a.prop >= 0 ? ActorRun::Kind::Prop : ActorRun::Kind::None;
        }
        if (a.kind == ActorRun::Kind::None) {
            log(OPENRAC_LOG_WARN, std::format("{}: actor \"{}\" is missing, skipped", p.scene->name, a.id));
        }
        for (const char* part : {"look_at", "held", "layers"}) {
            if (def.has(part)) warn_once(p, std::format("actor \"{}\"", part));
        }
        p.actors.push_back(std::move(a));
    }
}

void actor_tick(Play& p, ActorRun& a, int t) {
    if (a.kind == ActorRun::Kind::None) return;
    const Value& def = *a.def;
    track_at(def["track"], t, a.position, a.rotation, a.scale);
    path_at(def["path"], t, a.position, a.rotation);

    const int visible = visible_at(def["visible"], t) ? 1 : 0;

    if (a.kind == ActorRun::Kind::Prop) {
        openrac_prop_pose pose{};
        pose.size = sizeof(pose);
        std::memcpy(pose.position, &a.position, sizeof(float) * 3);
        quaternion(a.rotation, pose.rotation);
        pose.scale = a.scale;
        g_api->prop_pose(g_mod, a.prop, &pose);
        if (visible != a.visible) g_api->prop_hide(g_mod, a.prop, !visible);
        a.visible = visible;
        return;
    }

    // Animation: a new anims entry starts its sequence at its frame; after that the game plays it
    // on by itself, so the pose hands back what the moby has (no pull back to a key).
    const auto& anims = def["anims"].items();
    const int anim = anim_at(def["anims"], t);
    const int slot = a.kind == ActorRun::Kind::Figure ? g_api->figure_slot(g_mod, a.figure) : a.slot;
    const openrac_moby* now = moby_at(slot);
    if (a.base_scale <= 0) {
        // Scale 1 is the actor's own size: a level moby's, a new moby's, the hero's for a figure.
        openrac_pose hero;
        if (a.kind == ActorRun::Kind::Moby && a.original.scale > 0) {
            a.base_scale = a.original.scale;
        } else if (a.kind == ActorRun::Kind::Figure && hero_pose(hero) && hero.scale > 0) {
            a.base_scale = hero.scale;
        } else if (now != nullptr && now->scale > 0) {
            a.base_scale = now->scale;
        }
    }
    const float size = a.scale * (a.base_scale > 0 ? a.base_scale : 1.0f);
    std::uint32_t seq = now ? now->seq : 0, frame = now ? now->frame : 0;
    std::uint32_t next = now ? now->next_frame : 0;
    float anim_time = now ? now->anim_time : 0, speed = now ? now->anim_speed : 1;
    if (anim >= 0) {
        const Value& e = anims[static_cast<std::size_t>(anim)];
        speed = static_cast<float>(e["speed"].number(1));
        if (anim != a.anim) {
            seq = static_cast<std::uint32_t>(e["seq"].integer());
            frame = static_cast<std::uint32_t>(e["frame"].integer());
            next = frame + 1;
            anim_time = 0;
            if (e.has("blend") && e["blend"].integer() != 4) warn_once(p, "a blend other than 4 ticks");
        }
    }
    a.anim = anim;

    if (a.kind == ActorRun::Kind::Figure) {
        openrac_pose pose{};
        pose.size = sizeof(pose);
        pose.level = g_level.number;
        std::memcpy(pose.position, &a.position, sizeof(float) * 3);
        std::memcpy(pose.rotation, &a.rotation, sizeof(float) * 3);
        pose.scale = size;
        pose.item = -1;
        pose.seq = seq, pose.frame = frame, pose.next_frame = next;
        pose.anim_time = anim_time, pose.anim_speed = speed;
        pose.flags = OPENRAC_POSE_ON_GROUND;
        g_api->figure_pose(g_mod, a.figure, &pose);
    } else {
        openrac_moby pose{};
        pose.size = sizeof(pose);
        pose.slot = a.slot;
        std::memcpy(pose.position, &a.position, sizeof(float) * 3);
        std::memcpy(pose.rotation, &a.rotation, sizeof(float) * 3);
        pose.scale = size;
        pose.seq = seq, pose.frame = frame, pose.next_frame = next;
        pose.anim_time = anim_time, pose.anim_speed = speed;
        g_api->moby_pose(g_mod, a.slot, &pose);
    }
    if (visible != a.visible && slot >= 0) g_api->moby_hide(g_mod, slot, !visible);
    a.visible = visible;
}

void actors_end(Play& p) {
    for (ActorRun& a : p.actors) {
        const std::string end = (*a.def)["end"].string_or("keep");
        // A skip puts every actor where the cutscene leaves it.
        if (p.skipped) actor_tick(p, a, p.scene->length);
        switch (a.kind) {
            case ActorRun::Kind::Moby:
                if (end == "remove") {
                    g_api->moby_kill(g_mod, a.slot);
                    break;
                }
                if (end == "restore") {
                    openrac_moby back = a.original;
                    back.size = sizeof(back);
                    g_api->moby_pose(g_mod, a.slot, &back);
                    if (!(a.original.flags & OPENRAC_MOBY_HIDDEN)) g_api->moby_hide(g_mod, a.slot, 0);
                }
                g_api->moby_drive(g_mod, a.slot, 0);
                break;
            case ActorRun::Kind::Spawned:
                // A moby made for the cutscene has no update of the game's to go back to: it stays
                // as a still pose ("keep") or goes.
                if (end != "keep") g_api->moby_kill(g_mod, a.slot);
                break;
            case ActorRun::Kind::Figure:
                if (end != "keep") g_api->figure_remove(g_mod, a.figure);
                break;
            case ActorRun::Kind::Prop:
                if (end != "keep") g_api->prop_remove(g_mod, a.prop);
                break;
            case ActorRun::Kind::None: break;
        }
    }
}

// --- Camera

std::vector<CamKey> camera_keys(Play& p) {
    const auto& all = (*p.doc)["camera"]["keys"].items();
    std::vector<CamKey> keys;
    for (std::size_t i : by_time(all)) {
        const Value& k = all[i];
        V3 target;
        const bool has_target = target_point(p, k["look_at"], target);
        keys.push_back(camera_key(k, keys.empty() ? nullptr : &keys.back(), has_target ? &target : nullptr));
        if (k.has("fov")) warn_once(p, "a camera field of view");
    }
    return keys;
}

openrac_camera make_camera(V3 eye, V3 forward, float roll) {
    const CameraAxes axes = camera_axes(forward, roll);
    openrac_camera cam{};
    cam.size = sizeof(cam);
    std::memcpy(cam.eye, &eye, sizeof(float) * 3);
    std::memcpy(cam.forward, &axes.forward, sizeof(float) * 3);
    std::memcpy(cam.left, &axes.left, sizeof(float) * 3);
    std::memcpy(cam.up, &axes.up, sizeof(float) * 3);
    return cam;
}

bool camera_at(Play& p, int t, CameraPose& out) {
    if (!camera_pose(camera_keys(p), t, out)) return false;
    out.eye = out.eye + shake_at((*p.doc)["camera"]["shake"], t);
    return true;
}

// --- Over the picture

// The letterbox and the fade at t, `k` of the way in (less on the way back to the game).
void overlay_tick(Play& p, int t, float k) {
    if (!g_has_overlay) return;
    g_bars = (*p.doc)["options"]["letterbox"].boolean(true) ? letterbox_at(t) * k : 0.0f;
    Rgba fade = fade_at((*p.doc)["events"], t);
    fade.a *= k;
    g_fade = pack_rgba(fade);
}

void draw_overlay(void*, const openrac_overlay* o) {
    constexpr float kWidth = 512, kHeight = 448, kBar = kHeight * 0.12f;
    const float bars = g_bars.load();
    if (bars > 0) {
        o->rect(o->context, 0, 0, kWidth, kBar * bars, 0x000000FFu);
        o->rect(o->context, 0, kHeight - kBar * bars, kWidth, kBar * bars, 0x000000FFu);
    }
    const std::uint32_t fade = g_fade.load();
    if ((fade & 0xFFu) != 0) o->rect(o->context, 0, 0, kWidth, kHeight, fade);
}

// --- Subtitles

void subtitles_tick(Play& p, int t) {
    std::vector<openrac_hud_text> lines;
    if (!g_subtitles) return;
    for (const Value& s : (*p.doc)["subtitles"].items()) {
        if (t < s["t"].integer() || t >= s["end"].integer(s["t"].integer() + 120)) continue;
        const Value& text = s["text"];
        const std::string lang = text.is_string() ? std::string() : pick_language(text, (*p.doc)["languages"]);
        if (lang == "ja") {
            warn_once(p, "Japanese subtitles (the HUD font has only ASCII)");
            continue;
        }
        // Lines of up to 40 letters, broken at spaces, at most three.
        const std::vector<std::string> rows = wrap(ascii(text.is_string() ? text.string() : text[lang].string()));
        for (std::size_t r = 0; r < rows.size(); ++r) {
            openrac_hud_text line{};
            std::snprintf(line.text, sizeof(line.text), "%s", rows[r].c_str());
            line.x = 0.5f;
            line.y = 0.80f + 0.055f * static_cast<float>(r) - 0.0275f * static_cast<float>(rows.size() - 1);
            line.scale = 0.75f;
            line.rgba[0] = line.rgba[1] = line.rgba[2] = 128;
            line.rgba[3] = 255;
            line.flags = OPENRAC_HUD_SHADOW;
            lines.push_back(line);
        }
    }
    if (lines.size() > 8) lines.resize(8);
    g_api->hud_text_set(g_mod, lines.empty() ? nullptr : lines.data(), static_cast<int>(lines.size()));
}

// --- Sounds

void sounds_tick(Play& p, int t) {
    const auto& list = (*p.doc)["sounds"].items();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Value& s = list[i];
        if (p.sounds_done[i] || t < s["t"].integer()) continue;
        p.sounds_done[i] = 1;
        if (p.skipped) continue;
        const float volume = static_cast<float>(s["volume"].number(1));
        if (s.has("game_sound")) {
            if (s["actor"].string_or("hero") == "hero") {
                g_api->hero_sound_play(g_mod, s["game_sound"].integer(), 0);
            } else {
                warn_once(p, "a game sound on an actor other than the hero");
            }
            continue;
        }
        const Value& file = s["file"];
        std::string path = file.is_string() ? file.string() : std::string();
        if (file.is_object()) {
            const std::string lang = pick_language(file, (*p.doc)["languages"]);
            path = file[lang].string();
        }
        if (path.empty()) continue;
        const int id = sound_id(p, path);
        if (id < 0) continue;
        Voice v;
        v.fade_in = s["fade_in"].integer();
        v.fade_out = s["fade_out"].integer();
        v.start = t;
        v.end = s.has("end") ? s["end"].integer() : (s["kind"].string() == "music" ? p.scene->length : -1);
        v.volume = volume;
        v.voice = g_api->sound_play(g_mod, id, v.fade_in > 0 ? 0.0f : volume, 1.0f, 0);
        if (v.voice != 0) p.voices.push_back(v);
    }
    // Fades.
    for (Voice& v : p.voices) {
        if (v.end >= 0 && t >= v.end) {
            g_api->sound_stop(g_mod, v.voice);
            v.voice = 0;
            continue;
        }
        if (v.fade_in > 0 || v.fade_out > 0) {
            g_api->sound_set(g_mod, v.voice, v.volume * fade_gain(v.start, v.fade_in, v.fade_out, v.end, t), 1.0f);
        }
    }
    std::erase_if(p.voices, [](const Voice& v) { return v.voice == 0; });
}

// --- Events

void event_run(Play& p, const Value& e) {
    const std::string what = e["do"].string();
    const int level = e["level"].integer(g_level.number);
    if (what == "moby_hide" || what == "moby_kill") {
        const int slot = g_api->moby_slot(e["spawn_id"].integer());
        if (slot < 0) return;
        if (what == "moby_kill") {
            g_api->moby_kill(g_mod, slot);
        } else {
            g_api->moby_hide(g_mod, slot, e["on"].boolean(true) ? 1 : 0);
        }
    } else if (what == "mission_flag") {
        std::uint8_t flags[OPENRAC_MISSION_STATE_SIZE] = {};
        const int byte = e["byte"].integer();
        if (byte < 0 || byte >= OPENRAC_MISSION_STATE_SIZE) return;
        const int value = e["value"].integer(1);
        if (value == 0) {
            flags[byte] = 1;
            g_api->mission_state_clear(level, flags);
        } else {
            g_api->mission_state_read(level, flags);
            flags[byte] = static_cast<std::uint8_t>(value);
            g_api->mission_state_write(level, flags);
        }
    } else if (what == "haze") {
        Play::Haze h{};
        const V3 pos = vec(e["position"]);
        std::memcpy(h.patch.position, &pos, sizeof(float) * 3);
        h.patch.radius = static_cast<float>(e["radius"].number(4));
        h.patch.strength = static_cast<float>(e["strength"].number(0.1));
        h.until = e["t"].integer() + e["length"].integer(60);
        p.haze.push_back(h);
    } else if (what == "fade") {
        // Drawn from the events each tick (overlay_tick); without the port's overlay, not at all.
        if (!g_has_overlay) warn_once(p, "a fade (needs the port's overlay)");
    } else if (what == "play") {
        p.after = e["cutscene"].string();
    } else if (what == "travel") {
        p.travel = level;
    } else {
        warn_once(p, std::format("the event \"{}\"", what));
    }
}

void events_tick(Play& p, int t) {
    const auto& list = (*p.doc)["events"].items();
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (p.events_done[i] || t < list[i]["t"].integer()) continue;
        p.events_done[i] = 1;
        // A skip runs only what must last: persistent events, a next cutscene, a travel.
        const std::string what = list[i]["do"].string();
        if (p.skipped && !list[i]["persistent"].boolean() && what != "play" && what != "travel") continue;
        event_run(p, list[i]);
    }
    std::erase_if(p.haze, [t](const Play::Haze& h) { return t >= h.until; });
    std::vector<openrac_haze> patches;
    for (const Play::Haze& h : p.haze) patches.push_back(h.patch);
    g_api->haze_set(g_mod, patches.empty() ? nullptr : patches.data(), static_cast<int>(patches.size()));
}

// --- Start and end

void start(Cutscene& c, int hops = 0) {
    // A variant whose condition holds plays instead (not round in a loop).
    for (const Value& v : c.doc["variants"].items()) {
        if (hops >= kMaxVariantHops || !flag_holds(v["if"])) continue;
        if (Cutscene* other = g_level.find(v["play"].string()); other != nullptr && other != &c) {
            if (c.doc["trigger"]["once"].boolean(true)) g_seen.add(seen_key(c));
            start(*other, hops + 1);
            return;
        }
    }
    auto p = std::make_unique<Play>();
    p->scene = &c;
    p->doc = &c.doc;
    p->events_done.assign(c.doc["events"].size(), 0);
    p->sounds_done.assign(c.doc["sounds"].size(), 0);
    const Value& opts = c.doc["options"];
    if (opts["letterbox"].boolean(true) && !g_has_overlay) warn_once(*p, "the letterbox (needs the port's overlay)");
    if (opts["world"].string_or("run") != "run") warn_once(*p, "holding the world");
    if (opts["music"].string_or("duck") != "keep") warn_once(*p, "lowering the game's music");
    if (c.doc["trigger"]["once"].boolean(true)) g_seen.add(seen_key(c));
    g_api->hero_grab(g_mod, -1, 0);
    g_play = std::move(p);
    g_swallow_start = true;
    g_cutscene_on = true;
    log(OPENRAC_LOG_INFO, std::format("playing {}", c.name));
}

// Ends the cutscene: what it leaves stays, the rest goes back. With `level_gone` the level it played
// in was reloaded or left, and its mobys with it: only the port's state is put back.
void finish(bool level_gone) {
    Play& p = *g_play;
    if (!level_gone && p.skipped) {
        // Everything still to come that lasts.
        std::fill(p.sounds_done.begin(), p.sounds_done.end(), 1);
        events_tick(p, p.scene->length);
    }
    for (Voice& v : p.voices) g_api->sound_stop(g_mod, v.voice);
    p.voices.clear();
    g_api->hud_text_set(g_mod, nullptr, 0);
    g_api->haze_set(g_mod, nullptr, 0);
    g_api->camera_set(g_mod, nullptr);
    g_api->hero_release(g_mod);
    if (!level_gone) {
        actors_end(p);
        if (p.hero_hidden) g_api->moby_hide(g_mod, g_api->figure_slot(g_mod, -1), 0);
        const Value& end = (*p.doc)["hero"]["end"];
        if (end.is_object()) {
            const V3 pos = vec(end["position"]);
            const float at[3] = {pos.x, pos.y, pos.z};
            g_api->hero_place(g_mod, at, static_cast<float>(end["yaw"].number()), OPENRAC_PLACE_GROUND);
        }
    }
    const std::string done = p.scene->name, next = p.after;
    const int travel = p.travel;
    g_play.reset();
    g_cutscene_on = false;
    g_bars = 0;
    g_fade = 0;
    log(OPENRAC_LOG_INFO, std::format("{} ended", done));
    if (level_gone) return;
    if (travel >= 0) {
        g_api->play_travel(g_mod, travel);
        return;
    }
    if (!next.empty()) {
        if (Cutscene* c = g_level.find(next)) {
            start(*c);
            return;
        }
    }
    // Cutscenes that start after this one.
    for (Cutscene& c : g_level.scenes) {
        const Value& tr = c.doc["trigger"];
        if (tr["kind"].string() == "after" && tr["cutscene"].string() == done && flag_holds(tr["flag"])
            && !(tr["once"].boolean(true) && g_seen.has(seen_key(c)))) {
            start(c);
            return;
        }
    }
}

void play_tick() {
    Play& p = *g_play;
    read_mobys();
    if (!p.started) {
        // The first tick: the hero hidden if asked, the actors taken or made.
        p.started = true;
        const std::string mode = (*p.doc)["hero"]["mode"].string_or("held");
        if (mode == "hidden" || mode == "actor") {
            p.hero_hidden = g_api->moby_hide(g_mod, g_api->figure_slot(g_mod, -1), 1) == OPENRAC_HOOK_OK;
        }
        actors_begin(p);
    }
    if (p.ease_out >= 0) {
        // Back to the game's camera, then the end.
        openrac_camera game{};
        game.size = sizeof(game);
        const float u = float(p.ease_out) / float(kCameraEaseOut);
        const float s = u * u * (3 - 2 * u);
        if (p.has_camera && g_api->camera_view(&game) == OPENRAC_HOOK_OK) {
            const V3 eye = lerp(p.last_camera.eye, V3{game.eye[0], game.eye[1], game.eye[2]}, s);
            const V3 f = lerp(p.last_camera.forward, V3{game.forward[0], game.forward[1], game.forward[2]}, s);
            openrac_camera cam = make_camera(eye, normal(f, p.last_camera.forward), p.last_camera.roll * (1 - s));
            g_api->camera_set(g_mod, &cam);
        }
        overlay_tick(p, p.scene->length, 1 - s);
        if (++p.ease_out > kCameraEaseOut) finish(false);
        return;
    }

    const bool skippable = (*p.doc)["options"]["skippable"].boolean(true);
    if (skippable && (g_pressed & OPENRAC_PAD_START)) p.skipped = true;
    if (p.skipped) {
        finish(false);
        return;
    }

    const int t = p.t;
    for (ActorRun& a : p.actors) actor_tick(p, a, t);
    CameraPose pose;
    if (camera_at(p, t, pose)) {
        const openrac_camera cam = make_camera(pose.eye, pose.forward, pose.roll);
        g_api->camera_set(g_mod, &cam);
        p.last_camera = pose;
        p.has_camera = true;
    }
    subtitles_tick(p, t);
    sounds_tick(p, t);
    events_tick(p, t);
    overlay_tick(p, t, 1);
    if (p.travel >= 0) {
        finish(false);
        return;
    }
    if (++p.t > p.scene->length) {
        g_api->hud_text_set(g_mod, nullptr, 0);
        if (p.has_camera && (*p.doc)["camera"]["end"].string_or("ease") == "ease") {
            g_api->hero_release(g_mod);
            p.ease_out = 0;
        } else {
            finish(false);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Triggers

// The game would start a scene of its own now: the player in control, on foot, alive.
bool can_start(const openrac_play_status& s, const openrac_pose& hero) {
    return s.phase == OPENRAC_PLAY_PLAYING && g_api->scene_playing(nullptr) < 0 && hero.health > 0
           && !(hero.flags & OPENRAC_POSE_AIRBORNE);
}

void triggers_tick(const openrac_play_status& s) {
    openrac_pose hero;
    const bool has_hero = hero_pose(hero);
    const V3 at = has_hero ? V3{hero.position[0], hero.position[1], hero.position[2]} : V3{};
    const bool ready = has_hero && can_start(s, hero);
    bool talk_ready = false;
    bool mobys_read = false;
    Cutscene* fire = nullptr;
    for (Cutscene& c : g_level.scenes) {
        const Value& tr = c.doc["trigger"];
        const std::string kind = tr["kind"].string();
        if (tr["once"].boolean(true) && g_seen.has(seen_key(c))) continue;
        bool now = false;
        if (kind == "level_start") {
            now = !g_level.started;
        } else if (kind == "volume") {
            // Entering fires; entered while the player is not in control, it fires once he is.
            const bool in = has_hero && inside(tr, at);
            now = in && !c.inside;
            if (!now || ready) c.inside = in;
        } else if (kind == "talk") {
            const int slot = g_api->moby_slot(tr["spawn_id"].integer(-1));
            if (!mobys_read) read_mobys(), mobys_read = true;
            if (const openrac_moby* m = moby_at(slot); m && has_hero) {
                const V3 mp{m->position[0], m->position[1], m->position[2]};
                if (length(mp - at) <= tr["radius"].number(4)) {
                    talk_ready = ready;
                    now = g_talk_pressed;
                }
            }
        } else if (kind == "pickup") {
            const bool here = g_api->moby_slot(tr["spawn_id"].integer(-1)) >= 0;
            now = c.present && !here;
            if (!now || ready) c.present = here;
        } else if (kind == "flag") {
            const bool holds = flag_holds(tr["flag"]);
            now = holds && !c.flag_was;
            if (!now || ready) c.flag_was = holds;
            if (now) {
                if (ready && !fire) fire = &c;
                continue;
            }
        }
        if (now && kind != "flag" && !flag_holds(tr["flag"])) now = false;
        if (now && ready && !fire) fire = &c;
    }
    g_talk_ready = talk_ready;
    g_talk_pressed = false;
    if (ready) g_level.started = true;
    if (fire) start(*fire);
}

// ---------------------------------------------------------------------------------------------

void frame() {
    openrac_play_status s{};
    s.size = sizeof(s);
    if (g_api->play_status(&s) != OPENRAC_HOOK_OK) return;
    if (s.loads != g_level.loads || s.level != g_level.number) {
        if (g_play) finish(true);  // everything of it went with the level
        g_level.loads = s.loads;
        if (s.level != g_level.number) {
            load_level(s.level);
        } else {
            g_level.started = false;
        }
        for (Cutscene& c : g_level.scenes) {
            c.inside = false;
            c.present = g_api->moby_slot(c.doc["trigger"]["spawn_id"].integer(-1)) >= 0;
            c.flag_was = flag_holds(c.doc["trigger"]["flag"]);
        }
        char code[8] = "en";
        g_api->game_language(code, sizeof(code));
        g_language = code;
    }
    if (g_play) {
        // A menu over the cutscene pauses it.
        if (s.phase == OPENRAC_PLAY_PLAYING) play_tick();
    } else if (!g_level.scenes.empty()) {
        triggers_tick(s);
    }
    g_pressed = 0;
}

void load(openrac::Mod& mod) {
    g_api = &mod.api();
    g_mod = mod.handle();
    if (g_api->api_minor < 14 || g_api->size < offsetof(openrac_api, game_language) + sizeof(void*)
        || g_api->pack_folder == nullptr || g_api->game_language == nullptr) {
        throw openrac::Error("this player needs a newer OpenRAC (mod interface 1.14 with pack_folder)");
    }
    g_subtitles = mod.setting<bool>("subtitles");
    g_has_overlay = mod.on_overlay(draw_overlay);
    mod.on_frame(frame);
    mod.on_pad(on_pad);
    mod.info("ready");
}

}  // namespace

OPENRAC_MOD_CPP("cutscene-player", "0.1.0", load)
