// SPDX-License-Identifier: MIT
// The behaviour presets' sums, apart from the port so they can be tested: where a moby is and which
// way it faces at a time, from where it started (and where the hero is). The game's axes: z up,
// rotation[2] the yaw in radians, a yaw of 0 facing +x.
#pragma once

#include <cmath>
#include <optional>
#include <string>

#include "json.hpp"

namespace behaviour {

using json::Value;

constexpr double kPi = 3.14159265358979323846;
constexpr double kTicksPerSecond = 60.0;  // the port runs every disc at 60 ticks a second

struct V3 {
    float x = 0, y = 0, z = 0;
};

enum class Preset { Spin, Bob, Patrol, Orbit, FaceHero, Follow };

struct Behaviour {
    int spawn_id = -1;
    Preset preset = Preset::Spin;
    double speed = 0;     // degrees a second (spin, orbit, face_hero) or units a second (patrol, follow)
    double height = 0;    // bob: how far up and down
    double period = 0;    // bob: seconds for one up and down
    double distance = 0;  // patrol: how far it goes; follow: how near the hero it stops
    double radius = 0;    // orbit
    double range = 0;     // follow: how near the hero must be for it to follow (0: always)
    int sequence = -1;    // the class's sequence it plays, -1 its own
};

inline double radians(double degrees) { return degrees * kPi / 180.0; }

// An angle in -pi..pi.
inline double wrap(double a) {
    a = std::fmod(a + kPi, 2.0 * kPi);
    if (a < 0) a += 2.0 * kPi;
    return a - kPi;
}

// `from` turned towards `to` by at most `step` radians, the short way.
inline double turn(double from, double to, double step) {
    const double d = wrap(to - from);
    if (std::fabs(d) <= step) return wrap(to);
    return wrap(from + (d > 0 ? step : -step));
}

inline std::optional<Preset> preset_of(const std::string& name) {
    if (name == "spin") return Preset::Spin;
    if (name == "bob") return Preset::Bob;
    if (name == "patrol") return Preset::Patrol;
    if (name == "orbit") return Preset::Orbit;
    if (name == "face_hero") return Preset::FaceHero;
    if (name == "follow") return Preset::Follow;
    return std::nullopt;
}

// A number of `v`, `fallback` when it is missing or not finite, held to lo..hi.
inline double field(const Value& v, double fallback, double lo, double hi) {
    double n = v.number(fallback);
    if (!std::isfinite(n)) n = fallback;
    return n < lo ? lo : n > hi ? hi : n;
}

// One entry of behaviours.json' "mobys", or nothing (no spawn id, a preset this mod does not know).
// What it leaves out takes the preset's own default.
inline std::optional<Behaviour> behaviour_of(const Value& v) {
    const auto preset = preset_of(v["preset"].string());
    const int id = v["spawn_id"].integer(-1);
    if (!preset || id < 0 || id >= 0xFFFF) return std::nullopt;
    Behaviour b;
    b.spawn_id = id;
    b.preset = *preset;
    const bool turns = *preset == Preset::Spin || *preset == Preset::Orbit || *preset == Preset::FaceHero;
    const double speed = *preset == Preset::Spin ? 90 : *preset == Preset::Orbit ? 45 : *preset == Preset::FaceHero ? 180 : 4;
    b.speed = field(v["speed"], speed, turns ? -3600 : 0, turns ? 3600 : 200);
    b.height = field(v["height"], 1, -100, 100);
    b.period = field(v["period"], 2, 0.05, 600);
    b.distance = field(v["distance"], *preset == Preset::Follow ? 3 : 8, 0, 1000);
    b.radius = field(v["radius"], 4, 0, 1000);
    b.range = field(v["range"], 30, 0, 10000);
    b.sequence = v["sequence"].integer(-1);
    if (b.sequence < -1 || b.sequence > 255) b.sequence = -1;
    return b;
}

// Where a moby is: its start, and where it is now (face_hero and follow go on from there).
struct State {
    V3 start_position, start_rotation;
    V3 position, rotation;
};

inline State start(const V3& position, const V3& rotation) { return {position, rotation, position, rotation}; }

// Moves `s` to time `t` (seconds since the behaviour began; `dt` since the last step). `hero`: where
// the hero is, or null for none.
inline void step(const Behaviour& b, State& s, double t, double dt, const V3* hero) {
    const V3& p0 = s.start_position;
    const double yaw0 = s.start_rotation.z;
    switch (b.preset) {
        case Preset::Spin:
            s.rotation.z = static_cast<float>(wrap(yaw0 + radians(b.speed) * t));
            break;
        case Preset::Bob:
            s.position.z = static_cast<float>(p0.z + b.height * std::sin(2.0 * kPi * t / b.period));
            break;
        case Preset::Patrol: {
            // Out along the way it faced, then back, facing the way it goes.
            if (b.speed <= 0 || b.distance <= 0) break;
            const double leg = b.distance / b.speed;
            const double phase = std::fmod(t, 2.0 * leg);
            const bool out = phase < leg;
            const double along = (out ? phase : 2.0 * leg - phase) * b.speed;
            s.position.x = static_cast<float>(p0.x + std::cos(yaw0) * along);
            s.position.y = static_cast<float>(p0.y + std::sin(yaw0) * along);
            s.rotation.z = static_cast<float>(out ? wrap(yaw0) : wrap(yaw0 + kPi));
            break;
        }
        case Preset::Orbit: {
            // Round a centre beside it, starting where it stands and facing the way it goes (to its
            // left for a positive speed, to its right for a negative one).
            const double side = b.speed >= 0 ? kPi / 2 : -kPi / 2;
            const double from = yaw0 - side;  // the angle of it seen from the centre
            const double cx = p0.x - b.radius * std::cos(from);
            const double cy = p0.y - b.radius * std::sin(from);
            const double a = from + radians(b.speed) * t;
            s.position.x = static_cast<float>(cx + b.radius * std::cos(a));
            s.position.y = static_cast<float>(cy + b.radius * std::sin(a));
            s.rotation.z = static_cast<float>(wrap(a + side));
            break;
        }
        case Preset::FaceHero:
            if (hero != nullptr) {
                const double to = std::atan2(hero->y - s.position.y, hero->x - s.position.x);
                s.rotation.z = static_cast<float>(turn(s.rotation.z, to, radians(std::fabs(b.speed)) * dt));
            }
            break;
        case Preset::Follow: {
            if (hero == nullptr) break;
            const double dx = hero->x - s.position.x, dy = hero->y - s.position.y;
            const double far = std::sqrt(dx * dx + dy * dy);
            if (far < 1e-4 || (b.range > 0 && far > b.range)) break;
            s.rotation.z = static_cast<float>(turn(s.rotation.z, std::atan2(dy, dx), radians(360.0) * dt));
            const double go = std::fmin(b.speed * dt, far - b.distance);
            if (go > 0) {
                s.position.x = static_cast<float>(s.position.x + dx / far * go);
                s.position.y = static_cast<float>(s.position.y + dy / far * go);
            }
            break;
        }
    }
}

}  // namespace behaviour
