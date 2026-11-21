/**************************************************************************/
/*  editor_property_name_processor.cpp                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "editor_property_name_processor.h"

#include "core/string/translation_server.h"
#include "editor/settings/editor_settings.h"

EditorPropertyNameProcessor *EditorPropertyNameProcessor::singleton = nullptr;

EditorPropertyNameProcessor::Style EditorPropertyNameProcessor::get_default_inspector_style() {
	if (!EditorSettings::get_singleton()) {
		return STYLE_CAPITALIZED;
	}
	const Style style = (Style)EDITOR_GET("interface/inspector/default_property_name_style").operator int();
	if (style == STYLE_LOCALIZED && !is_localization_available()) {
		return STYLE_CAPITALIZED;
	}
	return style;
}

EditorPropertyNameProcessor::Style EditorPropertyNameProcessor::get_settings_style() {
	if (!EditorSettings::get_singleton()) {
		return STYLE_LOCALIZED;
	}
	const bool translate = EDITOR_GET("interface/editor/localization/localize_settings");
	return translate ? STYLE_LOCALIZED : STYLE_CAPITALIZED;
}

EditorPropertyNameProcessor::Style EditorPropertyNameProcessor::get_tooltip_style(Style p_style) {
	return p_style == STYLE_LOCALIZED ? STYLE_CAPITALIZED : STYLE_LOCALIZED;
}

bool EditorPropertyNameProcessor::is_localization_available() {
	return EditorSettings::get_singleton() && EditorSettings::get_singleton()->get_language() != "en";
}

String EditorPropertyNameProcessor::_capitalize_name(const String &p_name) const {
	HashMap<String, String>::ConstIterator cached = capitalize_string_cache.find(p_name);
	if (cached) {
		return cached->value;
	}

	Vector<String> parts = p_name.split("_", false);
	for (int i = 0; i < parts.size(); i++) {
		// Articles/conjunctions/prepositions which should only be capitalized when not at beginning and end.
		if (i > 0 && i + 1 < parts.size() && stop_words.has(parts[i])) {
			continue;
		}
		HashMap<String, String>::ConstIterator remap = capitalize_string_remaps.find(parts[i]);
		if (remap) {
			parts.write[i] = remap->value;
		} else {
			parts.write[i] = parts[i].capitalize();
		}
	}
	const String capitalized = String(" ").join(parts);

	capitalize_string_cache[p_name] = capitalized;
	return capitalized;
}

StringName EditorPropertyNameProcessor::_get_context(const String &p_name, const String &p_property, const StringName &p_class) const {
	if (p_property.is_empty() && p_class == StringName()) {
		return StringName();
	}
	const HashMap<String, StringName> *context_map = translation_contexts.getptr(p_name);
	if (context_map == nullptr) {
		return StringName();
	}
	// It's expected that full property path is enough to distinguish between usages.
	// In case a class name is needed, all usages should be prefixed with the class name.
	const StringName *context = context_map->getptr(p_property);
	if (context == nullptr && p_class != StringName()) {
		context = context_map->getptr(String(p_class) + "::" + p_property);
	}
	if (context == nullptr) {
		return StringName();
	}
	return *context;
}

// OpenRAC Editor: a plain label for a game property, if it has one. Only the last part of a path gets it.
bool EditorPropertyNameProcessor::_openrac_label(const String &p_name, const String &p_property, const StringName &p_class, String &r_label) const {
	if (p_property.is_empty()) {
		return false;
	}
	if (p_name == "metadata" && p_property.begins_with("metadata/")) {
		r_label = "Game Settings";
		return true;
	}
	if (!p_property.ends_with(p_name)) {
		return false;
	}
	if (p_class != StringName()) {
		const String *found = openrac_labels.getptr(String(p_class) + "::" + p_property);
		if (found) {
			r_label = *found;
			return true;
		}
	}
	const String *found = openrac_labels.getptr(p_property);
	if (found) {
		r_label = *found;
		return true;
	}
	if (p_property.begins_with("metadata/rc1_")) {
		// Other game values: drop the rc1_ (and pv_) prefix, so "rc1_pv_spring_k" reads "Spring K".
		String rest = p_property.trim_prefix("metadata/rc1_").trim_prefix("pv_");
		r_label = _capitalize_name(rest);
		return true;
	}
	return false;
}

String EditorPropertyNameProcessor::process_name(const String &p_name, Style p_style, const String &p_property, const StringName &p_class) const {
	String openrac_label;
	if (p_style != STYLE_RAW && _openrac_label(p_name, p_property, p_class, openrac_label)) {
		return openrac_label;
	}
	switch (p_style) {
		case STYLE_RAW: {
			return p_name;
		} break;

		case STYLE_CAPITALIZED: {
			return _capitalize_name(p_name);
		} break;

		case STYLE_LOCALIZED: {
			const String capitalized = _capitalize_name(p_name);
			if (TranslationServer::get_singleton()) {
				return TranslationServer::get_singleton()->get_property_domain()->translate(capitalized, _get_context(p_name, p_property, p_class));
			}
			return capitalized;
		} break;
	}
	ERR_FAIL_V_MSG(p_name, "Unexpected property name style.");
}

String EditorPropertyNameProcessor::translate_group_name(const String &p_name) const {
	if (TranslationServer::get_singleton()) {
		return TranslationServer::get_singleton()->get_property_domain()->translate(p_name, StringName());
	}
	return p_name;
}

EditorPropertyNameProcessor::EditorPropertyNameProcessor() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = this;

	// The following initialization is parsed by the l10n extraction script with a regex.
	// The map name and value definition format should be kept synced with the regex.
	// https://github.com/godotengine/godot-editor-l10n/blob/main/scripts/common.py
	// OpenRAC Editor: plain labels for the game's properties.
	openrac_labels["Cutscene::export_now"] = "Save Cutscene File";
	openrac_labels["Cutscene::import_file"] = "Load Cutscene File";
	openrac_labels["Cutscene::set_up"] = "Set Up";
	openrac_labels["Cutscene::file_name"] = "File Name";
	openrac_labels["Cutscene::game"] = "Game";
	openrac_labels["Cutscene::length"] = "Length";
	openrac_labels["Cutscene::languages"] = "Disc Languages";
	openrac_labels["Cutscene::translations"] = "Subtitle Languages";
	openrac_labels["Cutscene::trigger_kind"] = "Starts When";
	openrac_labels["Cutscene::trigger_once"] = "Play Only Once";
	openrac_labels["Cutscene::trigger_radius"] = "Area Size";
	openrac_labels["Cutscene::trigger_box"] = "Area Box";
	openrac_labels["Cutscene::trigger_spawn_id"] = "Object ID";
	openrac_labels["Cutscene::trigger_after"] = "After Cutscene";
	openrac_labels["Cutscene::trigger_flag_enabled"] = "Use Mission Flag";
	openrac_labels["Cutscene::skippable"] = "Can Skip";
	openrac_labels["Cutscene::letterbox"] = "Black Bars";
	openrac_labels["Cutscene::hide_hud"] = "Hide the HUD";
	openrac_labels["Cutscene::world"] = "Level During Cutscene";
	openrac_labels["Cutscene::music"] = "Music";
	openrac_labels["Cutscene::hero_mode"] = "Ratchet During Cutscene";
	openrac_labels["Cutscene::hero_sequence"] = "Ratchet's Move";
	openrac_labels["Cutscene::variants"] = "Other Versions";
	openrac_labels["CutsceneCamera::cuts"] = "Camera Cuts";
	openrac_labels["CutsceneCamera::ease_back"] = "Ease Back to Game Camera";
	openrac_labels["CutsceneCamera::export_fov"] = "Save Zoom";
	openrac_labels["CutsceneCamera::fov"] = "Zoom";
	openrac_labels["CutsceneActor::take_selected_moby"] = "Use Selected Object";
	openrac_labels["CutsceneActor::actor_id"] = "Actor Name";
	openrac_labels["CutsceneActor::source"] = "Who Acts";
	openrac_labels["CutsceneActor::spawn_id"] = "Object ID";
	openrac_labels["CutsceneActor::moby_class"] = "Object Type";
	openrac_labels["CutsceneActor::prop_path"] = "Prop Model";
	openrac_labels["CutsceneActor::spawn_if_missing"] = "Add If Missing";
	openrac_labels["CutsceneActor::end"] = "At the End";
	openrac_labels["CutsceneActor::sequence"] = "Move";
	openrac_labels["CutsceneActor::anim_speed"] = "Move Speed";
	openrac_labels["CutsceneActor::blend"] = "Blend Time";
	openrac_labels["CutsceneActor::ride_on"] = "Ride On";
	openrac_labels["CutsceneActor::ride_start"] = "Ride From";
	openrac_labels["CutsceneActor::ride_end"] = "Ride To";
	openrac_labels["CutsceneActor::use_known_rig"] = "Use Known Face";
	openrac_labels["CutsceneActor::rig"] = "Face Joints";
	openrac_labels["CutsceneActor::pose_file"] = "Pose File";
	openrac_labels["CutsceneActor::pose_joints"] = "Pose Joints";
	openrac_labels["CutsceneActor::pose_source"] = "Pose From Model";
	openrac_labels["CutsceneActor::pose_animation"] = "Move Name";
	openrac_labels["CutsceneActor::pose_map"] = "Joint Matching";
	openrac_labels["CutsceneActor::pose_start"] = "Pose Start";
	openrac_labels["CutsceneActor::pose_bake"] = "Make Pose";
	openrac_labels["CutscenePath::speed"] = "Speed";
	openrac_labels["CutscenePath::ground"] = "Stick to Ground";
	openrac_labels["CutsceneLayer::use_part"] = "Body Part";
	openrac_labels["CutsceneLayer::weight"] = "Amount";
	openrac_labels["CutsceneLayer::retarget_source"] = "Model File";
	openrac_labels["CutsceneLayer::retarget_animation"] = "Move Name";
	openrac_labels["CutsceneLayer::retarget_map"] = "Joint Matching";
	openrac_labels["CutsceneLayer::retarget_bake"] = "Copy Moves";
	openrac_labels["CutsceneMood::mood"] = "Mood";
	openrac_labels["CutsceneMood::weight"] = "Amount";
	openrac_labels["CutsceneMood::blend"] = "Blend Time";
	openrac_labels["CutsceneSound::kind"] = "Kind";
	openrac_labels["CutsceneSound::files"] = "Files per Language";
	openrac_labels["CutsceneSound::game_sound"] = "Game Sound";
	openrac_labels["CutsceneSound::source"] = "Comes From";
	openrac_labels["CutsceneSound::volume"] = "Volume";
	openrac_labels["CutsceneSound::fade_in"] = "Fade In";
	openrac_labels["CutsceneSound::fade_out"] = "Fade Out";
	openrac_labels["CutsceneSound::stop"] = "Stop At";
	openrac_labels["CutsceneEvent::action"] = "Action";
	openrac_labels["CutsceneEvent::persistent"] = "Stays After";
	openrac_labels["CutsceneEvent::spawn_id"] = "Object ID";
	openrac_labels["CutsceneEvent::level"] = "Level";
	openrac_labels["CutsceneEvent::flag_byte"] = "Flag Number";
	openrac_labels["CutsceneEvent::flag_value"] = "Flag Value";
	openrac_labels["CutsceneEvent::radius"] = "Size";
	openrac_labels["CutsceneEvent::colour"] = "Color";
	openrac_labels["CutsceneEvent::cutscene"] = "Cutscene";
	openrac_labels["CutsceneEvent::air_fog"] = "Change Fog";
	openrac_labels["CutsceneEvent::fog_near"] = "Fog Start";
	openrac_labels["CutsceneEvent::fog_far"] = "Fog End";
	openrac_labels["CutsceneEvent::fog_thickness"] = "Fog Thickness";
	openrac_labels["CutsceneEvent::air_background"] = "Change Sky Color";
	openrac_labels["CutsceneEvent::background"] = "Sky Color";
	openrac_labels["CutsceneEvent::stretch"] = "Slow Down";
	openrac_labels["CutsceneEvent::tie"] = "Big Scenery";
	openrac_labels["scale"] = "Size";
	openrac_labels["metadata/rc1_draw_distance"] = "Visible Up To";
	openrac_labels["metadata/rc1_update_distance"] = "Active Up To";
	openrac_labels["metadata/rc1_colour"] = "Brightness";
	openrac_labels["metadata/rc1_spawn_id"] = "ID";
	openrac_labels["metadata/rc1_occlusion"] = "Hidden Behind Walls";
	openrac_labels["metadata/rc1_class"] = "Type";
	openrac_labels["metadata/rc1_class_name"] = "Type Name";
	openrac_labels["metadata/rc1_class_scale"] = "Type Size";
	openrac_labels["metadata/rc1_light"] = "Light";
	openrac_labels["metadata/rc1_directional_lights"] = "Lights";
	openrac_labels["metadata/rc1_pvar"] = "Settings";
	openrac_labels["metadata/rc1_spawn_flags"] = "Start Options";
	openrac_labels["metadata/rc1_group"] = "Team";
	openrac_labels["metadata/rc1_rooted_distance"] = "Stays Put Up To";
	openrac_labels["metadata/rc1_surface"] = "Ground Type";
	openrac_labels["metadata/rc1_collision_kind"] = "Collision Kind";
	openrac_labels["metadata/rc1_range"] = "Range";
	openrac_labels["metadata/rc1_joints"] = "Joints";
	openrac_labels["metadata/rc1_joint"] = "Joint";
	openrac_labels["metadata/rc1_static_collision"] = "Solid";
	openrac_labels["metadata/rc1_skin_animations"] = "Animations";
	openrac_labels["metadata/rc1_skin"] = "Skin";
	openrac_labels["metadata/rc1_foreign_level"] = "From Level";
	openrac_labels["metadata/rc1_behaviour"] = "Simple Move";
	openrac_labels["metadata/rc1_ship_path"] = "Ship Path";
	openrac_labels["metadata/rc1_ship_camera_cuboids"] = "Ship Camera Areas";

	capitalize_string_remaps["2d"] = "2D";
	capitalize_string_remaps["3d"] = "3D";
	capitalize_string_remaps["4d"] = "4D";
	capitalize_string_remaps["aa"] = "AA";
	capitalize_string_remaps["aabb"] = "AABB";
	capitalize_string_remaps["adb"] = "ADB";
	capitalize_string_remaps["actool"] = "actool";
	capitalize_string_remaps["ao"] = "AO";
	capitalize_string_remaps["androidxr"] = "AndroidXR";
	capitalize_string_remaps["api"] = "API";
	capitalize_string_remaps["apk"] = "APK";
	capitalize_string_remaps["arm32"] = "arm32";
	capitalize_string_remaps["arm64"] = "arm64";
	capitalize_string_remaps["arm64-v8a"] = "arm64-v8a";
	capitalize_string_remaps["armeabi-v7a"] = "armeabi-v7a";
	capitalize_string_remaps["arvr"] = "ARVR";
	capitalize_string_remaps["astc"] = "ASTC";
	capitalize_string_remaps["bbcode"] = "BBCode";
	capitalize_string_remaps["bg"] = "BG";
	capitalize_string_remaps["bidi"] = "BiDi";
	capitalize_string_remaps["bp"] = "BP";
	capitalize_string_remaps["bpc"] = "BPC";
	capitalize_string_remaps["bpm"] = "BPM";
	capitalize_string_remaps["bptc"] = "BPTC";
	capitalize_string_remaps["bvh"] = "BVH";
	capitalize_string_remaps["ca"] = "CA";
	capitalize_string_remaps["colr"] = "COLR";
	capitalize_string_remaps["ccdik"] = "CCDIK";
	capitalize_string_remaps["cd"] = "CD";
	capitalize_string_remaps["cpu"] = "CPU";
	capitalize_string_remaps["csg"] = "CSG";
	capitalize_string_remaps["d3d12"] = "D3D12";
	capitalize_string_remaps["db"] = "dB";
	capitalize_string_remaps["dof"] = "DoF";
	capitalize_string_remaps["dpi"] = "DPI";
	capitalize_string_remaps["dtls"] = "DTLS";
	capitalize_string_remaps["eol"] = "EOL";
	capitalize_string_remaps["erp"] = "ERP";
	capitalize_string_remaps["etc2"] = "ETC2";
	capitalize_string_remaps["fabrik"] = "FABRIK";
	capitalize_string_remaps["fbx"] = "FBX";
	capitalize_string_remaps["fbx2gltf"] = "FBX2glTF";
	capitalize_string_remaps["fft"] = "FFT";
	capitalize_string_remaps["fg"] = "FG";
	capitalize_string_remaps["filesystem"] = "FileSystem";
	capitalize_string_remaps["fov"] = "FOV";
	capitalize_string_remaps["fps"] = "FPS";
	capitalize_string_remaps["fs"] = "FS";
	capitalize_string_remaps["fsr"] = "FSR";
	capitalize_string_remaps["fxaa"] = "FXAA";
	capitalize_string_remaps["gdscript"] = "GDScript";
	capitalize_string_remaps["ggx"] = "GGX";
	capitalize_string_remaps["gi"] = "GI";
	capitalize_string_remaps["gl"] = "GL";
	capitalize_string_remaps["glb"] = "GLB";
	capitalize_string_remaps["gles"] = "GLES";
	capitalize_string_remaps["gles2"] = "GLES2";
	capitalize_string_remaps["gles3"] = "GLES3";
	capitalize_string_remaps["gltf"] = "glTF";
	capitalize_string_remaps["gridmap"] = "GridMap";
	capitalize_string_remaps["gpu"] = "GPU";
	capitalize_string_remaps["gui"] = "GUI";
	capitalize_string_remaps["guid"] = "GUID";
	capitalize_string_remaps["hdr"] = "HDR";
	capitalize_string_remaps["hidpi"] = "hiDPI";
	capitalize_string_remaps["hipass"] = "High-pass";
	capitalize_string_remaps["hl"] = "HL";
	capitalize_string_remaps["hsv"] = "HSV";
	capitalize_string_remaps["html"] = "HTML";
	capitalize_string_remaps["http"] = "HTTP";
	capitalize_string_remaps["id"] = "ID";
	capitalize_string_remaps["ids"] = "IDs";
	capitalize_string_remaps["igd"] = "IGD";
	capitalize_string_remaps["ik"] = "IK";
	capitalize_string_remaps["image@2x"] = "Image @2x";
	capitalize_string_remaps["image@3x"] = "Image @3x";
	capitalize_string_remaps["ime"] = "IME";
	capitalize_string_remaps["iod"] = "IOD";
	capitalize_string_remaps["ios"] = "iOS";
	capitalize_string_remaps["ip"] = "IP";
	capitalize_string_remaps["ipad"] = "iPad";
	capitalize_string_remaps["iphone"] = "iPhone";
	capitalize_string_remaps["ipv6"] = "IPv6";
	capitalize_string_remaps["ir"] = "IR";
	capitalize_string_remaps["itunes"] = "iTunes";
	capitalize_string_remaps["jit"] = "JIT";
	capitalize_string_remaps["k1"] = "K1";
	capitalize_string_remaps["k2"] = "K2";
	capitalize_string_remaps["kb"] = "(KB)"; // Unit.
	capitalize_string_remaps["lcd"] = "LCD";
	capitalize_string_remaps["ldr"] = "LDR";
	capitalize_string_remaps["linuxbsd"] = "Linux/*BSD";
	capitalize_string_remaps["lod"] = "LOD";
	capitalize_string_remaps["lods"] = "LODs";
	capitalize_string_remaps["loongarch64"] = "loongarch64";
	capitalize_string_remaps["lowpass"] = "Low-pass";
	capitalize_string_remaps["macos"] = "macOS";
	capitalize_string_remaps["mb"] = "(MB)"; // Unit.
	capitalize_string_remaps["mjpeg"] = "MJPEG";
	capitalize_string_remaps["mms"] = "MMS";
	capitalize_string_remaps["ms"] = "(ms)"; // Unit
	capitalize_string_remaps["msaa"] = "MSAA";
	capitalize_string_remaps["msdf"] = "MSDF";
	// Not used for now as AudioEffectReverb has a `msec` property.
	//capitalize_string_remaps["msec"] = "(msec)"; // Unit.
	capitalize_string_remaps["navmesh"] = "NavMesh";
	capitalize_string_remaps["nfc"] = "NFC";
	capitalize_string_remaps["ogv"] = "OGV";
	capitalize_string_remaps["oidn"] = "OIDN";
	capitalize_string_remaps["ok"] = "OK";
	capitalize_string_remaps["opengl"] = "OpenGL";
	capitalize_string_remaps["opengl3"] = "OpenGL 3";
	capitalize_string_remaps["opentype"] = "OpenType";
	capitalize_string_remaps["openxr"] = "OpenXR";
	capitalize_string_remaps["osslsigncode"] = "osslsigncode";
	capitalize_string_remaps["path3d"] = "Path3D";
	capitalize_string_remaps["pck"] = "PCK";
	capitalize_string_remaps["png"] = "PNG";
	capitalize_string_remaps["po2"] = "(Power of 2)"; // Unit.
	capitalize_string_remaps["ppc64"] = "ppc64";
	capitalize_string_remaps["pvrtc"] = "PVRTC";
	capitalize_string_remaps["pvs"] = "PVS";
	capitalize_string_remaps["rcodesign"] = "rcodesign";
	capitalize_string_remaps["rdo"] = "RDO";
	capitalize_string_remaps["rgb"] = "RGB";
	capitalize_string_remaps["rid"] = "RID";
	capitalize_string_remaps["rmb"] = "RMB";
	capitalize_string_remaps["rpc"] = "RPC";
	capitalize_string_remaps["rv64"] = "rv64";
	capitalize_string_remaps["s3tc"] = "S3TC";
	capitalize_string_remaps["scp"] = "SCP";
	capitalize_string_remaps["scrcpy"] = "scrcpy";
	capitalize_string_remaps["sdf"] = "SDF";
	capitalize_string_remaps["sdfgi"] = "SDFGI";
	capitalize_string_remaps["sdk"] = "SDK";
	capitalize_string_remaps["sec"] = "(sec)"; // Unit.
	capitalize_string_remaps["signtool"] = "signtool";
	capitalize_string_remaps["smaa"] = "SMAA";
	capitalize_string_remaps["sms"] = "SMS";
	capitalize_string_remaps["srgb"] = "sRGB";
	capitalize_string_remaps["ssao"] = "SSAO";
	capitalize_string_remaps["ssh"] = "SSH";
	capitalize_string_remaps["ssil"] = "SSIL";
	capitalize_string_remaps["ssl"] = "SSL";
	capitalize_string_remaps["sss"] = "SSS";
	capitalize_string_remaps["stderr"] = "stderr";
	capitalize_string_remaps["stdout"] = "stdout";
	capitalize_string_remaps["sv"] = "SV";
	capitalize_string_remaps["svg"] = "SVG";
	capitalize_string_remaps["taa"] = "TAA";
	capitalize_string_remaps["tcp"] = "TCP";
	capitalize_string_remaps["textfile"] = "TextFile";
	capitalize_string_remaps["tls"] = "TLS";
	capitalize_string_remaps["tv"] = "TV";
	capitalize_string_remaps["tvos"] = "tvOS";
	capitalize_string_remaps["uastc"] = "UASTC";
	capitalize_string_remaps["ui"] = "UI";
	capitalize_string_remaps["uri"] = "URI";
	capitalize_string_remaps["url"] = "URL";
	capitalize_string_remaps["urls"] = "URLs";
	capitalize_string_remaps["us"] = U"(µs)"; // Unit.
	capitalize_string_remaps["usb"] = "USB";
	capitalize_string_remaps["usec"] = U"(µsec)"; // Unit.
	capitalize_string_remaps["uid"] = "UID";
	capitalize_string_remaps["uuid"] = "UUID";
	capitalize_string_remaps["uv"] = "UV";
	capitalize_string_remaps["uv1"] = "UV1";
	capitalize_string_remaps["uv2"] = "UV2";
	capitalize_string_remaps["vector2"] = "Vector2";
	capitalize_string_remaps["visionos"] = "visionOS";
	capitalize_string_remaps["vpn"] = "VPN";
	capitalize_string_remaps["vram"] = "VRAM";
	capitalize_string_remaps["vrs"] = "VRS";
	capitalize_string_remaps["vsync"] = "V-Sync";
	capitalize_string_remaps["wap"] = "WAP";
	capitalize_string_remaps["webp"] = "WebP";
	capitalize_string_remaps["webrtc"] = "WebRTC";
	capitalize_string_remaps["websocket"] = "WebSocket";
	capitalize_string_remaps["wintab"] = "WinTab";
	capitalize_string_remaps["winink"] = "Windows Ink";
	capitalize_string_remaps["wifi"] = "Wi-Fi";
	capitalize_string_remaps["x86"] = "x86";
	capitalize_string_remaps["x86_32"] = "x86_32";
	capitalize_string_remaps["x86_64"] = "x86_64";
	capitalize_string_remaps["xr"] = "XR";
	capitalize_string_remaps["xray"] = "X-Ray";
	capitalize_string_remaps["xy"] = "XY";
	capitalize_string_remaps["xz"] = "XZ";
	capitalize_string_remaps["yz"] = "YZ";

	// Articles, conjunctions, prepositions.
	// The following initialization is parsed in https://github.com/godotengine/godot-editor-l10n/blob/main/scripts/common.py
	// with a regex. The word definition format should be kept synced with the regex.
	stop_words = LocalVector<String>({
			"a",
			"an",
			"and",
			"as",
			"at",
			"by",
			"for",
			"in",
			"not",
			"of",
			"on",
			"or",
			"over",
			"per",
			"the",
			"then",
			"to",
			"with",
	});

	// Translation context associated with a name.
	// The second key is either:
	// - `full/property/path`
	// - `Class::full/property/path`
	// In case a class name is needed to distinguish between usages, all usages should use the second format.
	//
	// The following initialization is parsed in https://github.com/godotengine/godot-editor-l10n/blob/main/scripts/common.py
	// with a regex. The map name and value definition format should be kept synced with the regex.
	translation_contexts["force"]["constant_force"] = "Physics";
	translation_contexts["force"]["force/8_bit"] = "Enforce";
	translation_contexts["force"]["force/mono"] = "Enforce";
	translation_contexts["force"]["force/max_rate"] = "Enforce";
	translation_contexts["force"]["force/max_rate_hz"] = "Enforce";
	translation_contexts["normal"]["theme_override_styles/normal"] = "Ordinary";
	translation_contexts["normal"]["TextureButton::texture_normal"] = "Ordinary";
	translation_contexts["normal"]["Decal::texture_normal"] = "Geometry";
	translation_contexts["normal"]["detail_normal"] = "Geometry";
	translation_contexts["normal"]["normal"] = "Geometry";
}

EditorPropertyNameProcessor::~EditorPropertyNameProcessor() {
	singleton = nullptr;
}
