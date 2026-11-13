/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: writes the level's cutscenes each time the level is saved, and draws the game's
 * picture (its frame, the letterbox, the safe areas, the subtitles) over a 3D view that looks
 * through a cutscene's camera. */

#pragma once

#include "editor/plugins/editor_plugin.h"

class MenuButton;
class CutsceneTimeline;
class Cutscene;
class CutsceneCamera;

class CutsceneEditorPlugin : public EditorPlugin {
	GDCLASS(CutsceneEditorPlugin, EditorPlugin);

	enum MenuItem {
		ITEM_FRAME_4_3,
		ITEM_FRAME_16_9,
		ITEM_LETTERBOX,
		ITEM_SAFE_AREAS,
		ITEM_SUBTITLES,
	};

	MenuButton *game_view = nullptr;
	CutsceneTimeline *timeline = nullptr;
	bool wide = true;
	bool letterbox = true;
	bool safe_areas = true;
	bool subtitles = true;
	double last_position = -1.0;

	void _scene_saved(const String &p_path);
	void _selection_changed();
	void _menu(int p_item);
	void _update_menu();
	// The cutscene camera one of the 3D views looks through, if any.
	CutsceneCamera *_previewed() const;

protected:
	void _notification(int p_what);

public:
	String get_plugin_name() const override { return "Cutscene"; }
	void forward_3d_force_draw_over_viewport(Control *p_overlay) override;

	CutsceneEditorPlugin();
};
