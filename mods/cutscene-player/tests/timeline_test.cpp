// SPDX-License-Identifier: MIT
// Tests of the cutscene player's JSON reader and timeline sums, without the port:
//   cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
#include <clocale>
#include <cmath>
#include <cstdio>
#include <string>

#include "json.hpp"
#include "timeline.hpp"

using namespace cutscene;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
    }
}
#define CHECK(x) check((x), #x, __LINE__)

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool near3(V3 a, V3 b, float eps = 1e-3f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

Value parse(const std::string& text) {
    std::string error;
    Value v = Value::parse(text, error);
    if (!error.empty()) std::fprintf(stderr, "parse error: %s in %s\n", error.c_str(), text.c_str());
    return v;
}
bool fails(const std::string& text) {
    std::string error;
    Value v = Value::parse(text, error);
    return !error.empty() && v.is_null();
}

// --- JSON

void json_values() {
    const Value v = parse(R"({"a": 1, "b": [1, 2.5, -3e2], "c": "x\ny", "d": true, "e": false, "f": null})");
    CHECK(v.is_object());
    CHECK(v["a"].integer() == 1);
    CHECK(v["b"].size() == 3);
    CHECK(v["b"][1].number() == 2.5);
    CHECK(v["b"][2].number() == -300);
    CHECK(v["c"].string() == "x\ny");
    CHECK(v["d"].boolean() && !v["e"].boolean(true));
    CHECK(v["f"].is_null() && v.has("f"));
    // Missing parts read as null and chain.
    CHECK(v["nope"]["deeper"][3].is_null());
    CHECK(v["nope"].integer(7) == 7);
    CHECK(v["b"][-1].is_null() && v["b"][99].is_null());
    CHECK(v["a"].string_or("s") == "s");
    CHECK(v["c"].number(4) == 4);
    // A comment, as the format's examples have.
    CHECK(parse("// a comment\n{\"x\": 1 // after\n}")["x"].integer() == 1);
    CHECK(parse("  [ ]  ").is_array());
    CHECK(parse("{}").is_object());
}

void json_numbers() {
    CHECK(parse("0").number() == 0);
    CHECK(parse("-0.5").number() == -0.5);
    CHECK(parse("1E3").number() == 1000);
    CHECK(parse("2e-2").number() == 0.02);
    CHECK(fails("+1"));
    CHECK(fails("01"));
    CHECK(fails("1."));
    CHECK(fails(".5"));
    CHECK(fails("1e"));
    CHECK(fails("nan"));
    CHECK(fails("inf"));
    CHECK(fails("0x10"));
    CHECK(fails("-"));
    // Past a double: infinite or zero; the integer of it held to int's range.
    CHECK(std::isinf(parse("1e999").number()) && parse("1e999").number() > 0);
    CHECK(parse("-1e999").number() < 0);
    CHECK(parse("1e-999").number() == 0);
    CHECK(parse("1e999").integer() == 2147483647);
    CHECK(parse("-1e999").integer() == -2147483647 - 1);
    CHECK(parse("3000000000").integer() == 2147483647);
    CHECK(parse("-2.9").integer() == -2);
    // The same in a locale whose decimal mark is a comma.
    if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") || std::setlocale(LC_NUMERIC, "fr_FR.UTF-8")) {
        CHECK(parse("1.5").number() == 1.5);
        std::setlocale(LC_NUMERIC, "C");
    }
}

// A JSON string of `body`, with "~" for a backslash (so the escapes stay escapes in this file).
std::string quoted(std::string body) {
    for (char& c : body) {
        if (c == '~') c = '\\';
    }
    return "\"" + body + "\"";
}

void json_strings() {
    CHECK(parse(quoted("a~\"b~~c~/d~t")).string() == "a\"b\\c/d\t");
    CHECK(parse(quoted("~u00e9")).string() == "\xC3\xA9");
    CHECK(parse(quoted("~u3042")).string() == "\xE3\x81\x82");
    CHECK(parse(quoted("~ud83d~ude00")).string() == "\xF0\x9F\x98\x80");
    CHECK(parse(quoted("~u0041~u004a")).string() == "AJ");
    // UTF-8 as it is passes through.
    CHECK(parse(quoted("\xC3\xA9")).string() == "\xC3\xA9");
    // Half a pair, or a pair whose second half is not one: the replacement character.
    CHECK(parse(quoted("~ud83d")).string() == "\xEF\xBF\xBD");
    CHECK(parse(quoted("~ude00")).string() == "\xEF\xBF\xBD");
    CHECK(parse(quoted("~ud83dA")).string() == "\xEF\xBF\xBD" "A");
    CHECK(parse(quoted("~ud83d~u0041")).string() == "\xEF\xBF\xBD" "A");
    CHECK(parse(quoted("~ud83d~ud83d")).string() == "\xEF\xBF\xBD\xEF\xBF\xBD");
    CHECK(fails(quoted("~u12")));
    CHECK(fails(quoted("~u12zz")));
    CHECK(fails(quoted("~u+123")));
    CHECK(fails(R"("open)"));
    CHECK(fails("\"ends in a backslash\\"));
}

void json_broken() {
    CHECK(fails(""));
    CHECK(fails("   "));
    CHECK(fails("{"));
    CHECK(fails("[1, 2"));
    CHECK(fails("[1 2]"));
    CHECK(fails("{\"a\" 1}"));
    CHECK(fails("{a: 1}"));
    CHECK(fails("{\"a\": 1,}"));
    CHECK(fails("[1,]"));
    CHECK(fails("tru"));
    CHECK(fails("truex"));
    CHECK(fails("{} {}"));
    std::string deep(100, '[');
    deep += std::string(100, ']');
    CHECK(fails(deep));
    std::string fine(60, '[');
    fine += std::string(60, ']');
    CHECK(!fails(fine));
}

// --- Maths

void maths() {
    CHECK(near(angle_lerp(0.1f, 6.2f, 0.5f), 0.1f + (6.2f - 2 * kPi - 0.1f) * 0.5f));
    CHECK(near(angle_lerp(3.0f, -3.0f, 1.0f), 3.0f + (2 * kPi - 6.0f)));
    CHECK(near(angle_lerp(1.0f, 2.0f, 0.25f), 1.25f));
    CHECK(near3(normal({0, 0, 0}, {1, 2, 3}), {1, 2, 3}));
    CHECK(near3(normal({0, 0, NAN}, {1, 0, 0}), {1, 0, 0}));
    CHECK(near3(normal({0, 3, 4}, {}), {0, 0.6f, 0.8f}));
    CHECK(near3(direction({0, 0, 0}), {1, 0, 0}));
    CHECK(near3(direction({0, 0, kPi / 2}), {0, 1, 0}));
    CHECK(near3(direction({0, kPi / 2, 0}), {0, 0, -1}));  // pitch above 0 looks down
    // The curves go through their ends.
    const V3 a{0, 0, 0}, b{1, 2, 3}, c{4, 4, 4}, d{9, 0, 1};
    CHECK(near3(catmull(a, b, c, d, 0), b) && near3(catmull(a, b, c, d, 1), c));
    CHECK(near3(bezier(a, b, c, d, 0), a) && near3(bezier(a, b, c, d, 1), d));
    float q[4];
    quaternion({0, 0, 0}, q);
    CHECK(near(q[3], 1) && near(q[0], 0) && near(q[1], 0) && near(q[2], 0));
    quaternion({0, 0, kPi}, q);
    CHECK(near(q[2], 1) && near(q[3], 0));
    CHECK(near3(vec(parse("[1, 2]"), {7, 7, 7}), {7, 7, 7}));
    CHECK(near3(vec(parse("[1, \"2\", 3]"), {7, 7, 7}), {7, 7, 7}));
    CHECK(near3(vec(parse("[1, 2, 3, 4]")), {1, 2, 3}));
}

// --- Tracks

void tracks() {
    const Value track = parse(R"([
        {"t": 0, "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": 1},
        {"t": 60, "position": [6, 0, 0], "scale": 3},
        {"t": 120, "position": [6, 6, 0], "rotation": [0, 0, 3], "cut": true},
        {"t": 180, "rotation": [0, 0, -3]}
    ])");
    V3 pos, rot;
    float scale = 1;
    track_at(track, -10, pos, rot, scale);
    CHECK(near3(pos, {0, 0, 0}) && near(scale, 1));
    track_at(track, 30, pos, rot, scale);
    CHECK(near3(pos, {3, 0, 0}) && near(scale, 2));
    track_at(track, 60, pos, rot, scale);
    CHECK(near3(pos, {6, 0, 0}) && near(scale, 3));
    // Up to a cut it holds, then jumps.
    track_at(track, 119, pos, rot, scale);
    CHECK(near3(pos, {6, 0, 0}) && near(rot.z, 0));
    track_at(track, 120, pos, rot, scale);
    CHECK(near3(pos, {6, 6, 0}) && near(rot.z, 3));
    // A key with no position keeps the one before; the yaw goes the short way (3 to -3 over pi).
    track_at(track, 150, pos, rot, scale);
    CHECK(near3(pos, {6, 6, 0}));
    CHECK(near(rot.z, 3 + (2 * kPi - 6) * 0.5f));
    track_at(track, 1000, pos, rot, scale);
    CHECK(near(rot.z, -3));

    // Keys out of order are read in time; keys at the same tick: the later in the file wins after.
    const Value shuffled = parse(R"([{"t": 60, "position": [6, 0, 0]}, {"t": 0, "position": [0, 0, 0]}])");
    track_at(shuffled, 30, pos, rot, scale);
    CHECK(near3(pos, {3, 0, 0}));
    const Value same = parse(R"([{"t": 0, "position": [0, 0, 0]}, {"t": 0, "position": [5, 0, 0]}])");
    track_at(same, 0, pos, rot, scale);
    CHECK(near3(pos, {0, 0, 0}));
    track_at(same, 1, pos, rot, scale);
    CHECK(near3(pos, {5, 0, 0}));

    // No keys: nothing changes. One key: always it.
    pos = {1, 1, 1};
    track_at(parse("[]"), 10, pos, rot, scale);
    CHECK(near3(pos, {1, 1, 1}));
    track_at(parse("null"), 10, pos, rot, scale);
    CHECK(near3(pos, {1, 1, 1}));
    track_at(parse(R"([{"t": 50, "position": [2, 2, 2]}])"), 10, pos, rot, scale);
    CHECK(near3(pos, {2, 2, 2}));
    // A first key with only a place keeps the rotation and scale passed in.
    rot = {0, 0, 1}, scale = 4;
    track_at(parse(R"([{"t": 0, "position": [0, 0, 0]}])"), 0, pos, rot, scale);
    CHECK(near(rot.z, 1) && near(scale, 4));

    // Smooth keys pass through every key.
    const Value smooth = parse(R"([
        {"t": 0, "position": [0, 0, 0], "ease": "smooth"},
        {"t": 10, "position": [10, 5, 0], "ease": "smooth"},
        {"t": 20, "position": [20, 0, 0], "ease": "smooth"}
    ])");
    track_at(smooth, 10, pos, rot, scale);
    CHECK(near3(pos, {10, 5, 0}));
    track_at(smooth, 5, pos, rot, scale);
    CHECK(pos.y > 2.5f);  // the curve bulges past the straight line towards the middle key
}

void paths() {
    V3 pos, rot;
    const Value path = parse(R"({"t": 60, "speed": 6, "points": [[0, 0, 0], [6, 0, 0], [6, 6, 0]]})");
    CHECK(!path_at(path, 59, pos, rot));
    CHECK(path_at(path, 60, pos, rot) && near3(pos, {0, 0, 0}) && near(rot.z, 0));
    CHECK(path_at(path, 90, pos, rot) && near3(pos, {3, 0, 0}));
    CHECK(path_at(path, 150, pos, rot) && near3(pos, {6, 3, 0}) && near(rot.z, kPi / 2));
    // At the end it stays there.
    CHECK(path_at(path, 6000, pos, rot) && near3(pos, {6, 6, 0}));
    // A point twice keeps the yaw of the stretch before it.
    const Value twice = parse(R"({"speed": 60, "points": [[0, 0, 0], [0, 5, 0], [0, 5, 0]]})");
    CHECK(path_at(twice, 600, pos, rot) && near3(pos, {0, 5, 0}) && near(rot.z, kPi / 2));
    // No speed is 8; a speed below 0 stands still; one point is no path.
    CHECK(path_at(parse(R"({"points": [[0, 0, 0], [100, 0, 0]]})"), 60, pos, rot) && near3(pos, {8, 0, 0}));
    CHECK(path_at(parse(R"({"speed": -5, "points": [[1, 0, 0], [100, 0, 0]]})"), 60, pos, rot)
          && near3(pos, {1, 0, 0}));
    CHECK(!path_at(parse(R"({"points": [[0, 0, 0]]})"), 60, pos, rot));
    CHECK(!path_at(parse("null"), 60, pos, rot));
}

void visibility_and_anims() {
    const Value keys = parse(R"([{"t": 30, "on": false}, {"t": 90, "on": true}])");
    CHECK(visible_at(keys, 0) && !visible_at(keys, 30) && !visible_at(keys, 89) && visible_at(keys, 90));
    CHECK(visible_at(parse("null"), 5));
    // Out of order: still the latest at or before t.
    const Value shuffled = parse(R"([{"t": 90, "on": true}, {"t": 30, "on": false}])");
    CHECK(!visible_at(shuffled, 60) && visible_at(shuffled, 100));
    const Value anims = parse(R"([{"t": 0, "seq": 3}, {"t": 60, "seq": 5}])");
    CHECK(anim_at(anims, -1) == -1 && anim_at(anims, 0) == 0 && anim_at(anims, 59) == 0 && anim_at(anims, 60) == 1);
    CHECK(anim_at(parse("[]"), 10) == -1);
}

// --- Camera

std::vector<CamKey> keys_of(const std::string& text) {
    const Value v = parse(text);
    std::vector<CamKey> keys;
    for (std::size_t i : by_time(v.items())) {
        const Value& k = v[i];
        V3 target = vec(k["look_at"]);
        keys.push_back(camera_key(k, keys.empty() ? nullptr : &keys.back(), k["look_at"].is_array() ? &target : nullptr));
    }
    return keys;
}

void camera() {
    CameraPose pose;
    CHECK(!camera_pose({}, 0, pose));
    const auto keys = keys_of(R"([
        {"t": 0, "eye": [0, 0, 10], "rotation": [0, 0, 0], "ease": "linear"},
        {"t": 60, "eye": [6, 0, 10], "rotation": [0, 0, 0]},
        {"t": 61, "eye": [0, 50, 10], "rotation": [0, 0, 1.5707963], "cut": true},
        {"t": 120, "eye": [0, 50, 10], "look_at": [0, 50, 0]}
    ])");
    CHECK(camera_pose(keys, -5, pose) && near3(pose.eye, {0, 0, 10}) && near3(pose.forward, {1, 0, 0}));
    CHECK(camera_pose(keys, 30, pose) && near3(pose.eye, {3, 0, 10}));
    // Held up to the cut, then the new view.
    CHECK(camera_pose(keys, 60, pose) && near3(pose.eye, {6, 0, 10}));
    CHECK(camera_pose(keys, 61, pose) && near3(pose.eye, {0, 50, 10}) && near3(pose.forward, {0, 1, 0}));
    // Looking at a point: straight down.
    CHECK(camera_pose(keys, 500, pose) && near3(pose.forward, {0, 0, -1}));
    // Roll from "rotation"'s first part, or "roll".
    const auto rolled = keys_of(R"([{"t": 0, "eye": [0, 0, 0], "rotation": [0.5, 0, 0]},
                                    {"t": 10, "eye": [0, 0, 0], "rotation": [0, 0, 0], "roll": -0.5}])");
    CHECK(camera_pose(rolled, 0, pose) && near(pose.roll, 0.5f));
    CHECK(camera_pose(rolled, 5, pose) && near(pose.roll, 0));
    // Looking at its own eye keeps the direction it had; a key without eye keeps the one before.
    const auto blind = keys_of(R"([{"t": 0, "eye": [1, 1, 1], "rotation": [0, 0, 1.5707963]},
                                   {"t": 10, "look_at": [1, 1, 1]}])");
    CHECK(near3(blind[1].eye, {1, 1, 1}) && near3(blind[1].forward, {0, 1, 0}));
    // Two keys at the same tick: no division by zero.
    const auto same = keys_of(R"([{"t": 5, "eye": [0, 0, 0], "rotation": [0, 0, 0], "ease": "linear"},
                                  {"t": 5, "eye": [9, 0, 0], "rotation": [0, 0, 0]}])");
    CHECK(camera_pose(same, 5, pose) && std::isfinite(pose.eye.x));
    // Opposite directions between keys: still a direction, not NaN.
    const auto turn = keys_of(R"([{"t": 0, "eye": [0, 0, 0], "rotation": [0, 0, 0], "ease": "linear"},
                                  {"t": 10, "eye": [0, 0, 0], "rotation": [0, 0, 3.14159265]}])");
    CHECK(camera_pose(turn, 5, pose) && std::isfinite(pose.forward.x) && near(length(pose.forward), 1));
    // Bezier: handles bend the way; the ends are the keys.
    const auto bez = keys_of(R"([{"t": 0, "eye": [0, 0, 0], "rotation": [0, 0, 0], "ease": "bezier", "out": [0, 10, 0]},
                                 {"t": 10, "eye": [10, 0, 0], "rotation": [0, 0, 0], "in": [0, 10, 0]}])");
    CHECK(camera_pose(bez, 5, pose) && pose.eye.y > 5);
    CHECK(camera_pose(bez, 10, pose) && near3(pose.eye, {10, 0, 0}));
}

void camera_axes_and_shake() {
    CameraAxes a = camera_axes({1, 0, 0}, 0);
    CHECK(near3(a.left, {0, 1, 0}) && near3(a.up, {0, 0, 1}));
    a = camera_axes({0, 0, -1}, 0);  // straight down: no NaN
    CHECK(near(length(a.left), 1) && near(length(a.up), 1) && near(dot(a.left, a.forward), 0));
    a = camera_axes({1, 0, 0}, kPi / 2);  // the roll turns left towards up
    CHECK(near3(a.left, {0, 0, 1}) && near3(a.up, {0, -1, 0}));
    a = camera_axes({0, 0, 0}, 0);
    CHECK(near3(a.forward, {1, 0, 0}));
    const Value shake = parse(R"([{"t": 10, "length": 20, "strength": 1}])");
    CHECK(near3(shake_at(shake, 9), {}) && near3(shake_at(shake, 30), {}));
    CHECK(length(shake_at(shake, 12)) > 0);
    CHECK(length(shake_at(parse(R"([{"t": 0, "length": 0}])"), 0)) >= 0);  // a length 0 is 1
}

// --- Text, sounds, triggers

void text() {
    CHECK(ascii("caf\xC3\xA9 ok\x01") == "caf ok");
    auto rows = wrap("one two three", 40, 3);
    CHECK(rows.size() == 1 && rows[0] == "one two three");
    rows = wrap("aaaa bbbb cccc", 9, 3);
    CHECK(rows.size() == 2 && rows[0] == "aaaa bbbb" && rows[1] == "cccc");
    rows = wrap("  spaced   out\nlines  ", 40, 3);
    CHECK(rows.size() == 1 && rows[0] == "spaced out lines");
    CHECK(wrap("", 40, 3).empty() && wrap("    ", 40, 3).empty());
    // A word longer than a row is broken.
    rows = wrap("abcdefghij", 4, 3);
    CHECK(rows.size() == 3 && rows[0] == "abcd" && rows[1] == "efgh" && rows[2] == "ij");
    // Too much text: three rows, the last ending in "...".
    rows = wrap("a b c d e f g h", 3, 3);
    CHECK(rows.size() == 3 && rows[2] == "...");
    rows = wrap("word word word word word word", 10, 2);
    CHECK(rows.size() == 2 && rows[1].size() <= 10 && rows[1].ends_with("..."));
    CHECK(wrap("x", 0, 3).empty() && wrap("x", 3, 0).empty());

    const Value text = parse(R"({"en": "Hi", "fr": "Salut"})");
    CHECK(pick_language(text, parse(R"(["fr", "en"])"), "fr") == "fr");
    CHECK(pick_language(text, parse(R"(["fr", "en"])"), "de") == "fr");
    CHECK(pick_language(text, parse(R"(["de"])"), "it") == "en");
    CHECK(pick_language(parse(R"({"es": "Hola"})"), parse("null"), "it") == "es");
    CHECK(pick_language(parse("{}"), parse("null"), "en").empty());
    CHECK(pick_language(parse("\"plain\""), parse("null"), "en").empty());
}

void fades() {
    CHECK(near(fade_gain(0, 0, 0, -1, 50), 1));
    CHECK(near(fade_gain(10, 20, 0, -1, 20), 0.5f));
    CHECK(near(fade_gain(10, 20, 0, -1, 5), 0));  // before its start: silent, not below 0
    CHECK(near(fade_gain(0, 0, 10, 100, 95), 0.5f));
    CHECK(near(fade_gain(0, 0, 10, 100, 100), 0));
    CHECK(near(fade_gain(0, 0, 10, -1, 1000), 1));  // no end: no fade out
    // Fading in and out at once: the quieter.
    CHECK(near(fade_gain(0, 100, 100, 100, 50), 0.5f));
    CHECK(near(fade_gain(0, 100, 10, 100, 95), 0.5f));
}

void volumes() {
    const Value box = parse(R"({"box": {"min": [0, 0, 0], "max": [2, 2, 2]}})");
    CHECK(inside(box, {1, 1, 1}) && inside(box, {0, 0, 0}) && inside(box, {2, 2, 2}));
    CHECK(!inside(box, {3, 1, 1}) && !inside(box, {1, 1, -0.1f}));
    // min and max the wrong way round still make the box.
    const Value flipped = parse(R"({"box": {"min": [2, 2, 2], "max": [0, 0, 0]}})");
    CHECK(inside(flipped, {1, 1, 1}));
    const Value sphere = parse(R"({"sphere": {"position": [0, 0, 0], "radius": 2}})");
    CHECK(inside(sphere, {0, 2, 0}) && !inside(sphere, {0, 2.01f, 0}));
    const Value plain = parse(R"({"sphere": {"position": [0, 0, 0]}})");  // radius 4
    CHECK(inside(plain, {0, 0, 3.9f}) && !inside(plain, {0, 0, 4.1f}));
    CHECK(!inside(parse("{}"), {0, 0, 0}) && !inside(parse("null"), {0, 0, 0}));
}

void picture() {
    CHECK(near(letterbox_at(-5), 0) && near(letterbox_at(0), 0) && near(letterbox_at(kLetterboxTicks), 1));
    CHECK(near(letterbox_at(kLetterboxTicks / 2), 0.5f, 0.1f) && near(letterbox_at(9999), 1));
    // To black over 30 ticks, then back to clear over 10.
    const Value events = parse(R"([
        {"t": 100, "do": "fade", "to": null, "length": 10},
        {"t": 0, "do": "moby_hide", "spawn_id": 3},
        {"t": 60, "do": "fade", "to": [0, 0, 0], "length": 30}
    ])");
    CHECK(near(fade_at(events, 59).a, 0));
    CHECK(near(fade_at(events, 75).a, 0.5f));
    CHECK(near(fade_at(events, 95).a, 1));
    CHECK(near(fade_at(events, 105).a, 0.5f));
    CHECK(near(fade_at(events, 200).a, 0));
    // A colour with its own alpha, no length (30), and length 0 (at once); parts held to 0 to 1.
    const Value white = parse(R"([{"t": 0, "do": "fade", "to": [1, 1, 1, 0.5]}])");
    CHECK(near(fade_at(white, 15).a, 0.25f) && near(fade_at(white, 15).r, 0.5f));
    const Value now = parse(R"([{"t": 10, "do": "fade", "to": [2, -1, 0.5], "length": 0}])");
    const Rgba c = fade_at(now, 10);
    CHECK(near(c.a, 1) && near(c.r, 1) && near(c.g, 0) && near(c.b, 0.5f));
    CHECK(near(fade_at(parse("null"), 10).a, 0));
    CHECK(pack_rgba({1, 0, 0, 1}) == 0xFF0000FFu);
    CHECK(pack_rgba({0, 0.5f, 1, 0}) == 0x0080FF00u);
    CHECK(pack_rgba({-1, 2, 0, NAN}) == 0x00FF0000u);
}

}  // namespace

int main() {
    json_values();
    json_numbers();
    json_strings();
    json_broken();
    maths();
    tracks();
    paths();
    visibility_and_anims();
    camera();
    camera_axes_and_shake();
    text();
    fades();
    volumes();
    picture();
    std::printf("timeline: %d check(s), %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
