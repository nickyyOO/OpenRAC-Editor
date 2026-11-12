/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: the small sums of a cutscene's export, kept apart so they can be read alone. */

#pragma once

#include "core/math/math_funcs.h"
#include "core/math/vector3.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace cutscene_math {

// A cutscene's time is in game ticks, 60 a second on every disc (the port runs PAL at 60 Hz too).
constexpr double TICK_RATE = 60.0;
// The camera and actors are written every tenth of a second, and at each of their keys.
constexpr int SAMPLE_TICKS = 6;

inline int ticks(double p_seconds) {
	return int(Math::round(p_seconds * TICK_RATE));
}

inline Vector<int> sorted_unique(Vector<int> p_values) {
	p_values.sort();
	Vector<int> out;
	for (int v : p_values) {
		if (out.is_empty() || out[out.size() - 1] != v) {
			out.push_back(v);
		}
	}
	return out;
}

// A camera's turn as the format has it, [roll, pitch, yaw] in the game's axes (z up): yaw 0 looks
// along +x, a pitch above 0 looks down, the roll turns the picture about the forward direction
// (the player's make_camera turns left towards up by it).
inline Vector3 camera_rotation(const Vector3 &p_forward, const Vector3 &p_up) {
	const Vector3 f = p_forward.normalized();
	const double yaw = Math::atan2(double(f.y), double(f.x));
	const double pitch = Math::asin(CLAMP(-double(f.z), -1.0, 1.0));
	Vector3 left = Vector3(0, 0, 1).cross(f);
	left = left.length() > 1e-6 ? left.normalized() : Vector3(0, 1, 0);
	const Vector3 level_up = f.cross(left);
	const Vector3 up = p_up.normalized();
	const double roll = Math::atan2(-double(up.dot(left)), double(up.dot(level_up)));
	return Vector3(roll, pitch, yaw);
}

// The languages of a disc, by the version its levels were read from: the PAL discs' five, the
// Japanese disc's one, else English.
inline Vector<String> disc_languages(const String &p_version) {
	Vector<String> out;
	const String v = p_version.to_lower();
	if (v.begins_with("pal")) {
		out.push_back("en");
		out.push_back("fr");
		out.push_back("de");
		out.push_back("es");
		out.push_back("it");
	} else if (v.contains("jp") || v.contains("ntsc-j") || v == "ja") {
		out.push_back("ja");
	} else {
		out.push_back("en");
	}
	return out;
}

// The class of a moby by its class scene (".../moby_11.tscn" is 11), or -1.
inline int class_from_scene(const String &p_path) {
	const String file = p_path.get_file().get_basename();
	if (!file.begins_with("moby_")) {
		return -1;
	}
	const String number = file.substr(5);
	return number.is_valid_int() ? number.to_int() : -1;
}

} // namespace cutscene_math
