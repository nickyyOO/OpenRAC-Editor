// SPDX-License-Identifier: MIT
// OpenRAC cutscene player: the sums of a cutscene's timeline, apart from the port so they can be
// tested alone (tests/timeline_test.cpp). Everything here reads a parsed .cutscene
// (docs/CUTSCENE_FORMAT.md) and a tick, and gives a place, a turn, a picture or a line of text.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "json.hpp"

namespace cutscene {

using json::Value;

constexpr double kTickRate = 60.0;  // cutscene ticks a second, every game and disc
constexpr float kPi = 3.14159265f;

// ---------------------------------------------------------------------------------------------
// Small vector maths

struct V3 {
    float x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normal(V3 a, V3 fallback) {
    const float l = length(a);
    return l > 1e-6f && std::isfinite(l) ? a * (1.0f / l) : fallback;
}
inline V3 lerp(V3 a, V3 b, float u) { return a + (b - a) * u; }
// Catmull-Rom through p1 (u 0) and p2 (u 1).
inline V3 catmull(V3 p0, V3 p1, V3 p2, V3 p3, float u) {
    const float u2 = u * u, u3 = u2 * u;
    return (p1 * 2.0f + (p2 - p0) * u + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * u2
            + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * u3)
           * 0.5f;
}
inline V3 bezier(V3 a, V3 b, V3 c, V3 d, float u) {
    const float v = 1.0f - u;
    return a * (v * v * v) + b * (3 * v * v * u) + c * (3 * v * u * u) + d * (u * u * u);
}
// From angle a to b the short way round.
inline float angle_lerp(float a, float b, float u) {
    float d = std::fmod(b - a, 2 * kPi);
    if (d > kPi) d -= 2 * kPi;
    if (d < -kPi) d += 2 * kPi;
    return a + d * u;
}
// [x, y, z] as a vector; anything else (or a part not a number) is the fallback.
inline V3 vec(const Value& v, V3 fallback = {}) {
    if (!v.is_array() || v.size() < 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number()) {
        return fallback;
    }
    return {static_cast<float>(v[0].number()), static_cast<float>(v[1].number()),
            static_cast<float>(v[2].number())};
}
// A direction from a rotation [x, y, z]: z the yaw (0 along +x), y the pitch (above 0 looks down).
inline V3 direction(V3 rotation) {
    const float cp = std::cos(rotation.y);
    return {std::cos(rotation.z) * cp, std::sin(rotation.z) * cp, -std::sin(rotation.y)};
}
// Euler [x, y, z] (z the yaw, applied last) as a quaternion (x, y, z, w).
inline void quaternion(V3 r, float out[4]) {
    const float cx = std::cos(r.x / 2), sx = std::sin(r.x / 2);
    const float cy = std::cos(r.y / 2), sy = std::sin(r.y / 2);
    const float cz = std::cos(r.z / 2), sz = std::sin(r.z / 2);
    out[0] = sx * cy * cz - cx * sy * sz;
    out[1] = cx * sy * cz + sx * cy * sz;
    out[2] = cx * cy * sz - sx * sy * cz;
    out[3] = cx * cy * cz + sx * sy * sz;
}

// The order of `keys` by their "t", ties kept in the file's order: a file written by hand need
// not list its keys in time.
inline std::vector<std::size_t> by_time(const std::vector<Value>& keys) {
    std::vector<std::size_t> order(keys.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return keys[a]["t"].integer() < keys[b]["t"].integer(); });
    return order;
}

// ---------------------------------------------------------------------------------------------
// Actors

// The track's place, rotation and scale at tick t. Each key gives what it has, the rest carries
// from the key before; what no key gives keeps the value passed in. Before the first key it is
// the first, after the last the last; a "cut" key jumps.
inline void track_at(const Value& track, int t, V3& pos, V3& rot, float& scale) {
    const auto& all = track.items();
    if (all.empty()) return;
    const std::vector<std::size_t> order = by_time(all);
    const std::size_t n = order.size();
    std::vector<V3> P(n), R(n);
    std::vector<float> S(n);
    std::vector<int> T(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Value& k = all[order[i]];
        P[i] = vec(k["position"], i ? P[i - 1] : pos);
        R[i] = vec(k["rotation"], i ? R[i - 1] : rot);
        S[i] = static_cast<float>(k["scale"].number(i ? S[i - 1] : scale));
        T[i] = k["t"].integer();
    }
    if (t <= T[0]) {
        pos = P[0], rot = R[0], scale = S[0];
        return;
    }
    std::size_t i = 0;
    while (i + 1 < n && T[i + 1] <= t) ++i;
    if (i + 1 >= n || all[order[i + 1]]["cut"].boolean()) {
        pos = P[i], rot = R[i], scale = S[i];
        return;
    }
    const float u = T[i + 1] > T[i] ? static_cast<float>(t - T[i]) / static_cast<float>(T[i + 1] - T[i]) : 1.0f;
    if (all[order[i]]["ease"].string_or("linear") == "smooth") {
        pos = catmull(P[i ? i - 1 : i], P[i], P[i + 1], P[std::min(i + 2, n - 1)], u);
    } else {
        pos = lerp(P[i], P[i + 1], u);
    }
    rot = {angle_lerp(R[i].x, R[i + 1].x, u), angle_lerp(R[i].y, R[i + 1].y, u), angle_lerp(R[i].z, R[i + 1].z, u)};
    scale = S[i] + (S[i + 1] - S[i]) * u;
}

// Walks `path` from its start tick at its speed (units a second, 8 if not given): the place along
// its points and the yaw of the stretch it is on. Once at the end it stays there. False before it
// starts, or with fewer than two points.
inline bool path_at(const Value& path, int t, V3& pos, V3& rot) {
    const auto& pts = path["points"].items();
    const int start = path["t"].integer();
    if (pts.size() < 2 || t < start) return false;
    float left = static_cast<float>((t - start) / kTickRate * std::max(0.0, path["speed"].number(8)));
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const V3 a = vec(pts[i]), b = vec(pts[i + 1]);
        const float len = length(b - a);
        // A stretch of no length has no direction: the yaw stays the last one's.
        if (len > 1e-6f) {
            rot.z = std::atan2(b.y - a.y, b.x - a.x);
        }
        if (left <= len || i + 2 == pts.size()) {
            pos = len > 1e-6f ? lerp(a, b, std::min(1.0f, left / len)) : b;
            return true;
        }
        left -= len;
    }
    return true;
}

// The last "visible" key at or before t (keys in time order); true before any.
inline bool visible_at(const Value& keys, int t) {
    bool visible = true;
    int at = -1;
    for (const Value& k : keys.items()) {
        const int kt = k["t"].integer();
        if (kt <= t && kt >= at) {
            visible = k["on"].boolean(true);
            at = kt;
        }
    }
    return visible;
}

// The anims entry that plays at t: the latest one started at or before it, -1 before any.
inline int anim_at(const Value& anims, int t) {
    int found = -1, at = -1;
    const auto& list = anims.items();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const int kt = list[i]["t"].integer();
        if (kt <= t && kt >= at) {
            found = static_cast<int>(i);
            at = kt;
        }
    }
    return found;
}

// ---------------------------------------------------------------------------------------------
// Camera

struct CamKey {
    int t = 0;
    V3 eye;
    bool has_target = false;
    V3 target;  // look_at
    V3 forward;
    float roll = 0;
    bool cut = false;
    std::string ease;
    V3 in, out;  // bezier handles, as offsets from eye
};

// A key from the file: `target` is its look_at point, when it has one (the player finds actors);
// `eye` carries from the key before when the key has none.
inline CamKey camera_key(const Value& k, const CamKey* before, const V3* target) {
    CamKey c;
    c.t = k["t"].integer();
    c.eye = vec(k["eye"], before ? before->eye : V3{});
    c.has_target = target != nullptr;
    if (target) c.target = *target;
    const bool has_rot = k.has("rotation");
    const V3 rot = vec(k["rotation"]);
    // A key with no rotation, or looking at its own eye, keeps the key before's direction.
    const V3 fallback = before ? before->forward : V3{1, 0, 0};
    if (c.has_target) {
        c.forward = normal(c.target - c.eye, has_rot ? direction(rot) : fallback);
    } else {
        c.forward = has_rot ? direction(rot) : fallback;
    }
    c.roll = static_cast<float>(k["roll"].number(has_rot ? rot.x : (before ? before->roll : 0.0f)));
    c.cut = k["cut"].boolean();
    c.ease = k["ease"].string_or("smooth");
    c.in = vec(k["in"]);
    c.out = vec(k["out"]);
    return c;
}

struct CameraPose {
    V3 eye;
    V3 forward{1, 0, 0};
    float roll = 0;
};

// The camera at t from its keys (in time order): held before the first and after the last key, and
// up to a cut; between two keys by the first's ease ("smooth", "linear" or "bezier").
inline bool camera_pose(const std::vector<CamKey>& keys, int t, CameraPose& out) {
    if (keys.empty()) return false;
    std::size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].t <= t) ++i;
    if (t <= keys[0].t || i + 1 >= keys.size() || keys[i + 1].cut) {
        const CamKey& k = t <= keys[0].t ? keys[0] : keys[i];
        out = {k.eye, k.forward, k.roll};
        return true;
    }
    const CamKey &a = keys[i], &b = keys[i + 1];
    const float u = b.t > a.t ? static_cast<float>(t - a.t) / static_cast<float>(b.t - a.t) : 1.0f;
    // Neighbours for the smooth curve, not across a cut.
    const CamKey& before = (i > 0 && !a.cut) ? keys[i - 1] : a;
    const CamKey& after = (i + 2 < keys.size() && !keys[i + 2].cut) ? keys[i + 2] : b;
    if (a.ease == "linear") {
        out.eye = lerp(a.eye, b.eye, u);
    } else if (a.ease == "bezier") {
        out.eye = bezier(a.eye, a.eye + a.out, b.eye + b.in, b.eye, u);
    } else {
        out.eye = catmull(before.eye, a.eye, b.eye, after.eye, u);
    }
    if (a.has_target && b.has_target) {
        const V3 target = a.ease == "linear"
                              ? lerp(a.target, b.target, u)
                              : catmull(before.has_target ? before.target : a.target, a.target, b.target,
                                        after.has_target ? after.target : b.target, u);
        out.forward = normal(target - out.eye, a.forward);
    } else {
        out.forward = normal(lerp(a.forward, b.forward, u), a.forward);
    }
    out.roll = a.roll + (b.roll - a.roll) * u;
    return true;
}

// The camera's shake at t: a wobble of the eye, fading over each shake's length.
inline V3 shake_at(const Value& shakes, int t) {
    V3 sum;
    for (const Value& s : shakes.items()) {
        const int st = s["t"].integer(), len = std::max(1, s["length"].integer(30));
        if (t < st || t >= st + len) continue;
        const float k = static_cast<float>(s["strength"].number(0.3)) * (1.0f - float(t - st) / float(len));
        const float ft = static_cast<float>(t);
        sum = sum + V3{std::sin(ft * 1.7f), std::sin(ft * 2.3f + 1.0f), std::sin(ft * 2.9f + 2.0f)} * k;
    }
    return sum;
}

// A camera's axes as the game has them (z up): left = up x forward, so yaw 0 looks along +x with
// +y on the left. The roll turns left towards up.
struct CameraAxes {
    V3 forward, left, up;
};
inline CameraAxes camera_axes(V3 forward, float roll) {
    CameraAxes a;
    a.forward = normal(forward, {1, 0, 0});
    a.left = normal(cross({0, 0, 1}, a.forward), {0, 1, 0});
    a.up = normal(cross(a.forward, a.left), {0, 0, 1});
    if (roll != 0) {
        const float c = std::cos(roll), s = std::sin(roll);
        const V3 l2 = a.left * c + a.up * s, u2 = a.up * c - a.left * s;
        a.left = l2, a.up = u2;
    }
    return a;
}

// ---------------------------------------------------------------------------------------------
// Text

// The printable ASCII of a UTF-8 text (the HUD font has nothing else).
inline std::string ascii(const std::string& utf8) {
    std::string out;
    for (unsigned char c : utf8) {
        if (c >= 32 && c < 127) out += static_cast<char>(c);
    }
    return out;
}

// A subtitle as rows of up to `width` letters, broken at spaces (a longer word broken where it
// must), at most `rows`; the last row ends in "..." when words were left out.
inline std::vector<std::string> wrap(const std::string& text, std::size_t width = 40, std::size_t rows = 3) {
    std::vector<std::string> out;
    if (width == 0 || rows == 0) return out;
    std::vector<std::string> words;
    std::string w;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!w.empty()) words.push_back(w), w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) words.push_back(w);
    bool cut = false;
    for (std::string word : words) {
        while (!word.empty()) {
            if (!out.empty() && out.back().size() + 1 + word.size() <= width) {
                out.back() += " " + word;
                word.clear();
                break;
            }
            if (out.size() == rows) {
                cut = true;
                break;
            }
            out.push_back(word.substr(0, width));
            word.erase(0, std::min(width, word.size()));
        }
        if (cut) break;
    }
    if (cut && !out.empty()) {
        std::string& last = out.back();
        if (last.size() + 3 > width) last.resize(width >= 3 ? width - 3 : 0);
        last += "...";
    }
    return out;
}

// Which language of a by-language text to show: the game's, else the cutscene's first, else
// English, else the first the text has. Empty when it has none.
inline std::string pick_language(const Value& by_language, const Value& languages, const std::string& game) {
    if (!by_language.is_object()) return std::string();
    if (by_language.has(game)) return game;
    if (languages.is_array() && languages.size() > 0 && by_language.has(languages[0].string())) {
        return languages[0].string();
    }
    if (by_language.has("en")) return "en";
    return by_language.members().empty() ? std::string() : by_language.members().begin()->first;
}

// ---------------------------------------------------------------------------------------------
// Sounds

// A playing sound's volume at t (0 to 1 of its own): up over fade_in from its start, down over
// fade_out to its end (-1: none).
inline float fade_gain(int start, int fade_in, int fade_out, int end, int t) {
    float k = 1;
    if (fade_in > 0 && t - start < fade_in) k = std::max(0.0f, float(t - start) / float(fade_in));
    if (end >= 0 && fade_out > 0 && t > end - fade_out) {
        k = std::min(k, std::max(0.0f, float(end - t) / float(fade_out)));
    }
    return k;
}

// ---------------------------------------------------------------------------------------------
// Over the picture: the letterbox and fades

constexpr int kLetterboxTicks = 15;  // ticks the bars take to come in

// How far the letterbox's bars are in at tick t of the cutscene (0 to 1, eased).
inline float letterbox_at(int t) {
    const float u = std::clamp(static_cast<float>(t) / kLetterboxTicks, 0.0f, 1.0f);
    return u * u * (3 - 2 * u);
}

struct Rgba {
    float r = 0, g = 0, b = 0, a = 0;
};

// The colour over the picture at t from the "fade" events: each goes from the colour before it to
// its "to" ([r, g, b] or [r, g, b, a], 0 to 1) over its length (30 ticks if not given); "to"
// null clears the colour. Clear before any.
inline Rgba fade_at(const Value& events, int t) {
    Rgba now;
    const auto& list = events.items();
    for (std::size_t i : by_time(list)) {
        const Value& e = list[i];
        if (e["do"].string() != "fade") continue;
        const int t0 = e["t"].integer();
        if (t < t0) break;
        const Value& to = e["to"];
        auto part = [&](int k, float fallback) { return std::clamp(static_cast<float>(to[k].number(fallback)), 0.0f, 1.0f); };
        const Rgba target = to.is_array() ? Rgba{part(0, 0), part(1, 0), part(2, 0), part(3, 1)}
                                          : Rgba{now.r, now.g, now.b, 0};
        const int len = std::max(0, e["length"].integer(30));
        const float u = len > 0 ? std::min(1.0f, static_cast<float>(t - t0) / static_cast<float>(len)) : 1.0f;
        now = {now.r + (target.r - now.r) * u, now.g + (target.g - now.g) * u, now.b + (target.b - now.b) * u,
               now.a + (target.a - now.a) * u};
    }
    return now;
}

// A colour as the overlay takes it, 0xRRGGBBAA.
inline std::uint32_t pack_rgba(Rgba c) {
    // Held to 0 to 1, NaN as 0.
    auto byte = [](float v) { return static_cast<std::uint32_t>(std::lround((v > 0 ? std::min(v, 1.0f) : 0.0f) * 255.0f)); };
    return byte(c.r) << 24 | byte(c.g) << 16 | byte(c.b) << 8 | byte(c.a);
}

// ---------------------------------------------------------------------------------------------
// Triggers

// Whether `at` is in a volume trigger's box (min to max, either way round) or sphere.
inline bool inside(const Value& trigger, V3 at) {
    if (trigger["box"].is_object()) {
        const V3 a = vec(trigger["box"]["min"]), b = vec(trigger["box"]["max"]);
        const V3 lo{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
        const V3 hi{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
        return at.x >= lo.x && at.y >= lo.y && at.z >= lo.z && at.x <= hi.x && at.y <= hi.y && at.z <= hi.z;
    }
    if (trigger["sphere"].is_object()) {
        return length(at - vec(trigger["sphere"]["position"])) <= trigger["sphere"]["radius"].number(4);
    }
    return false;
}

}  // namespace cutscene
