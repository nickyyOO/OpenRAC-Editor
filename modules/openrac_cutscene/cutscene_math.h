/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: the small sums of a cutscene's export, kept apart so they can be read alone. */

#pragma once

#include "core/math/math_funcs.h"
#include "core/math/vector3.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

#include <cstdint>
#include <cstring>

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

// A subtitle as the player shows it in the game's font: rows of up to `p_width` letters, broken at
// spaces (a longer word where it must), at most `p_rows`, the last ending in "..." when words were
// left out (the player's wrap() in timeline.hpp).
inline Vector<String> wrap_subtitle(const String &p_text, int p_width = 40, int p_rows = 3) {
	Vector<String> out;
	bool cut = false;
	for (String word : p_text.split_spaces()) {
		while (!word.is_empty()) {
			if (!out.is_empty() && out[out.size() - 1].length() + 1 + word.length() <= p_width) {
				out.write[out.size() - 1] += " " + word;
				word = String();
				break;
			}
			if (out.size() == p_rows) {
				cut = true;
				break;
			}
			out.push_back(word.substr(0, p_width));
			word = word.substr(MIN(p_width, word.length()));
		}
		if (cut) {
			break;
		}
	}
	if (cut && !out.is_empty()) {
		String last = out[out.size() - 1];
		if (last.length() + 3 > p_width) {
			last = last.substr(0, MAX(0, p_width - 3));
		}
		out.write[out.size() - 1] = last + "...";
	}
	return out;
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

// How open a speaker's mouth is, from a voice line's WAV: a byte every thirtieth of a second (two
// ticks), 0 shut to 255 wide, from the loudness of that stretch against the line's loud parts.
// Reads PCM 8, 16, 24 and 32 bit and 32-bit float, any number of channels. Empty, with
// `r_problem` set, for a file that is not such a WAV.
inline Vector<uint8_t> mouth_envelope(const Vector<uint8_t> &p_wav, String &r_problem) {
	Vector<uint8_t> out;
	const uint8_t *b = p_wav.ptr();
	const int64_t size = p_wav.size();
	const auto u16 = [&](int64_t at) { return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8; };
	const auto u32 = [&](int64_t at) { return u16(at) | u16(at + 2) << 16; };
	if (size < 12 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) {
		r_problem = "not a WAV file";
		return out;
	}
	int format = 0, channels = 0, rate = 0, bits = 0;
	int64_t data = -1, data_size = 0;
	for (int64_t at = 12; at + 8 <= size;) {
		const uint32_t chunk = u32(at + 4);
		if (memcmp(b + at, "fmt ", 4) == 0 && chunk >= 16 && at + 8 + 16 <= size) {
			format = u16(at + 8);
			channels = u16(at + 10);
			rate = int(u32(at + 12));
			bits = u16(at + 22);
			if (format == 0xFFFE && chunk >= 40 && at + 8 + 26 <= size) {
				format = u16(at + 8 + 24); // WAVE_FORMAT_EXTENSIBLE: the sub-format's first word
			}
		} else if (memcmp(b + at, "data", 4) == 0) {
			data = at + 8;
			data_size = MIN(int64_t(chunk), size - data);
			break;
		}
		at += 8 + int64_t(chunk) + (chunk & 1);
	}
	const bool pcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
	const bool flt = format == 3 && bits == 32;
	if (data < 0 || channels <= 0 || channels > 16 || rate < 1000 || rate > 384000 || !(pcm || flt)) {
		r_problem = "a WAV this reader does not know (PCM or 32-bit float)";
		return out;
	}
	const int frame = channels * bits / 8;
	const int64_t frames = data_size / frame;
	const auto sample = [&](int64_t at) -> double {
		switch (bits) {
			case 8: return (double(b[at]) - 128.0) / 128.0;
			case 16: return double(int16_t(u16(at))) / 32768.0;
			case 24: return double(int32_t(u32(at - 1) & 0xFFFFFF00u)) / 2147483648.0;
			default: {
				if (flt) {
					const uint32_t v = u32(at);
					float f;
					memcpy(&f, &v, sizeof(f));
					return Math::is_finite(f) ? double(f) : 0.0;
				}
				return double(int32_t(u32(at))) / 2147483648.0;
			}
		}
	};
	// The loudness (RMS) of each thirtieth of a second.
	const double step = double(rate) / 30.0;
	Vector<double> loud;
	for (int64_t i = 0; double(i) * step < double(frames); i++) {
		const int64_t from = int64_t(double(i) * step), to = MIN(frames, int64_t(double(i + 1) * step));
		double sum = 0.0;
		for (int64_t f = from; f < to; f++) {
			for (int c = 0; c < channels; c++) {
				const double v = sample(data + f * frame + int64_t(c) * (bits / 8));
				sum += v * v;
			}
		}
		const int64_t n = (to - from) * channels;
		loud.push_back(n > 0 ? Math::sqrt(sum / double(n)) : 0.0);
	}
	if (loud.is_empty()) {
		r_problem = "a WAV with no sound in it";
		return out;
	}
	// Against the loud parts (the 95th of a hundred), so one shout does not shut the rest; a
	// whisper's quiet below a twentieth of that is shut.
	Vector<double> sorted = loud;
	sorted.sort();
	const double top = MAX(sorted[MIN(sorted.size() - 1, int(sorted.size() * 0.95))], 1e-4);
	for (double l : loud) {
		const double open = CLAMP((l / top - 0.05) / 0.95, 0.0, 1.0);
		out.push_back(uint8_t(Math::round(open * 255.0)));
	}
	return out;
}

} // namespace cutscene_math
