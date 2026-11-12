/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: writes the level's cutscenes each time the level is saved. */

#pragma once

#include "editor/plugins/editor_plugin.h"

class CutsceneEditorPlugin : public EditorPlugin {
	GDCLASS(CutsceneEditorPlugin, EditorPlugin);

	void _scene_saved(const String &p_path);

protected:
	void _notification(int p_what);

public:
	String get_plugin_name() const override { return "Cutscene"; }

};
