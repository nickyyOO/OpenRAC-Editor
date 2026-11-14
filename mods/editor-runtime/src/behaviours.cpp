// SPDX-License-Identifier: MIT
// The behaviour presets of editor-runtime: simple behaviours given in OpenRAC Editor to mobys of a level (spin,
// bob, patrol, orbit, face the hero, follow the hero), played through the port's mod interface only.
// It reads <pack>/level_NN/behaviours.json when a level loads (docs/BEHAVIOUR_FORMAT.md), drives
// each moby named there (its own update no longer runs) and poses it every tick.
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "openrac/mod.hpp"
#include "presets.hpp"

namespace {

namespace fs = std::filesystem;
using namespace behaviour;

const openrac_api* g_api = nullptr;
openrac_mod* g_mod = nullptr;

#define API_HAS(f) (g_api->size >= offsetof(openrac_api, f) + sizeof(g_api->f) && g_api->f != nullptr)

void log(int level, const std::string& text) { g_api->log(g_mod, level, text.c_str()); }

fs::path utf8_path(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

// A moby given a behaviour, and how it goes this load.
struct Run {
    Behaviour def;
    int slot = -1;        // its slot while driven
    bool driven = false;
    bool gone = false;    // killed, or another mod drives it: left alone until the level loads again
    bool seq_set = false;
    std::uint32_t ticks = 0;
    State state;
};

struct Level {
    int number = -1;
    std::uint32_t loads = ~0u;
    std::vector<Run> runs;
};
Level g_level;

std::string pack_folder() {
    char buf[4096];
    return g_api->pack_folder(buf, sizeof(buf)) > 0 ? std::string(buf) : std::string();
}

std::vector<Behaviour> read_level(int level) {
    std::vector<Behaviour> out;
    const std::string pack = pack_folder();
    if (pack.empty() || level < 0) return out;
    const fs::path file = utf8_path(pack) / std::format("level_{:02d}", level) / "behaviours.json";
    std::error_code e;
    if (!fs::is_regular_file(file, e)) return out;
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    std::string error;
    const Value doc = Value::parse(text.str(), error);
    if (!error.empty() || doc["format"].integer() != 1 || !doc["mobys"].is_array()) {
        log(OPENRAC_LOG_WARN, std::format("level_{:02d}/behaviours.json: {}", level,
                                          error.empty() ? "not format 1 with a mobys array" : error));
        return out;
    }
    for (const Value& entry : doc["mobys"].items()) {
        if (auto b = behaviour_of(entry)) {
            out.push_back(*b);
        } else {
            log(OPENRAC_LOG_WARN, std::format("level_{:02d}: a behaviour with no spawn id or an unknown preset \"{}\", left out",
                                              level, entry["preset"].string()));
        }
    }
    log(OPENRAC_LOG_INFO, std::format("level_{:02d}: {} behaviours", level, out.size()));
    return out;
}

std::vector<openrac_moby> g_list(4096);

// The level's mobys now, and each placed one's slot by its spawn id.
int list_mobys(std::map<int, int>& by_uid) {
    for (openrac_moby& m : g_list) m.size = sizeof(openrac_moby);
    const int n = std::min(g_api->moby_list(g_list.data(), static_cast<int>(g_list.size())), static_cast<int>(g_list.size()));
    for (int i = 0; i < n; ++i) {
        if (g_list[i].key < 0 || (g_list[i].flags & OPENRAC_MOBY_DEAD)) continue;
        const int id = API_HAS(moby_uid) ? g_api->moby_uid(g_list[i].slot) : g_list[i].key;
        if (id >= 0) by_uid.emplace(id, i);
    }
    return n;
}

V3 v3(const float f[3]) { return {f[0], f[1], f[2]}; }

void tick() {
    std::map<int, int> by_uid;
    list_mobys(by_uid);
    openrac_pose hero{};
    hero.size = sizeof(hero);
    const bool has_hero = g_api->hero_pose(&hero) == OPENRAC_HOOK_OK;
    const V3 hero_at = v3(hero.position);
    for (Run& r : g_level.runs) {
        if (r.gone) continue;
        const auto it = by_uid.find(r.def.spawn_id);
        if (it == by_uid.end()) {
            // Not there (yet, or no more): a driven one that went is let go.
            if (r.driven) r.gone = true;
            continue;
        }
        const openrac_moby& m = g_list[static_cast<std::size_t>(it->second)];
        if (!r.driven) {
            const int got = g_api->moby_drive(g_mod, m.slot, 1);
            if (got != OPENRAC_HOOK_OK) {
                if (got == OPENRAC_HOOK_TAKEN) {
                    log(OPENRAC_LOG_WARN, std::format("moby {}: another mod drives it, so it keeps that one", r.def.spawn_id));
                }
                r.gone = true;
                continue;
            }
            r.driven = true;
            r.slot = m.slot;
            r.state = start(v3(m.position), v3(m.rotation));
        } else if (m.slot != r.slot) {
            r.gone = true;
            continue;
        }
        const double dt = 1.0 / kTicksPerSecond;
        step(r.def, r.state, r.ticks * dt, dt, has_hero ? &hero_at : nullptr);
        ++r.ticks;
        openrac_moby pose = m;
        pose.size = sizeof(pose);
        std::memcpy(pose.position, &r.state.position, sizeof(float) * 3);
        std::memcpy(pose.rotation, &r.state.rotation, sizeof(float) * 3);
        pose.scale = 0;  // its own
        // Its animation goes on as it plays, or starts the sequence it was given.
        pose.anim_speed = 1;
        if (r.def.sequence >= 0 && !r.seq_set) {
            pose.seq = static_cast<std::uint32_t>(r.def.sequence);
            pose.frame = pose.next_frame = 0;
            pose.anim_time = 0;
            r.seq_set = true;
        }
        g_api->moby_pose(g_mod, r.slot, &pose);
    }
}

void frame() {
    openrac_play_status s{};
    s.size = sizeof(s);
    if (g_api->play_status(&s) != OPENRAC_HOOK_OK) return;
    if (s.loads != g_level.loads || s.level != g_level.number) {
        // A new load: every drive of the last one is gone, the mobys start again from their places.
        std::vector<Behaviour> defs;
        if (s.level == g_level.number) {
            for (const Run& r : g_level.runs) defs.push_back(r.def);
        } else {
            defs = read_level(s.level);
        }
        g_level = {s.level, s.loads, {}};
        for (const Behaviour& b : defs) {
            Run r;
            r.def = b;
            g_level.runs.push_back(r);
        }
    }
    if (s.phase == OPENRAC_PLAY_PLAYING && !g_level.runs.empty()) tick();
}

void load(openrac::Mod& mod) {
    g_api = &mod.api();
    g_mod = mod.handle();
    if (g_api->api_minor < 15 || !API_HAS(pack_folder) || !API_HAS(moby_drive) || !API_HAS(moby_pose)) {
        throw openrac::Error("behaviour presets need a newer OpenRAC (mod interface 1.15 with pack_folder)");
    }
    mod.on_frame(frame);
}

}  // namespace

namespace editor_runtime {
void behaviours_load(openrac::Mod& mod) { load(mod); }
}  // namespace editor_runtime
