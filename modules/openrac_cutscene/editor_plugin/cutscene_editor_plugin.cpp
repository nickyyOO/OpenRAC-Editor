/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: writes the level's cutscenes each time the level is saved, and draws the game's
 * picture over a 3D view that looks through a cutscene's camera. */

#include "cutscene_editor_plugin.h"

#include "../cutscene.h"
#include "cutscene_timeline.h"

#include "core/object/callable_mp.h"
#include "core/templates/hash_set.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/resources/font.h"

// Each Cutscene of the saved scene goes to <scene folder>/cutscenes/<name>.cutscene, which the
// level's packing carries into the pack the port reads.
void CutsceneEditorPlugin::_scene_saved(const String &p_path) {
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	if (root == nullptr || root->get_scene_file_path() != p_path) {
		return;
	}
	TypedArray<Node> found = root->find_children("*", "Cutscene", true, false);
	if (Cutscene *own = Object::cast_to<Cutscene>(root)) {
		found.push_front(own);
	}
	HashSet<String> written;
	for (int i = 0; i < found.size(); i++) {
		Cutscene *cutscene = Object::cast_to<Cutscene>(found[i]);
		if (cutscene == nullptr) {
			continue;
		}
		// Two cutscenes of one name would write one file: the first keeps it.
		const String id = cutscene->get_cutscene_id();
		if (written.has(id)) {
			WARN_PRINT(vformat("Two cutscenes are called \"%s\": %s is not written. Give it another file name.", id, root->get_path_to(cutscene)));
			continue;
		}
		written.insert(id);
		String path;
		if (cutscene->export_cutscene(&path) == OK) {
			print_line(vformat("Cutscene \"%s\" written to %s", id, path));
		}
	}
}

// The Cutscene dock follows the selection: the cutscene of the node picked, if any.
void CutsceneEditorPlugin::_selection_changed() {
	const TypedArray<Node> selected = EditorInterface::get_singleton()->get_selection()->get_selected_nodes();
	for (int i = 0; i < selected.size(); i++) {
		for (Node *n = Object::cast_to<Node>(selected[i]); n != nullptr; n = n->get_parent()) {
			if (Cutscene *cutscene = Object::cast_to<Cutscene>(n)) {
				timeline->set_cutscene(cutscene);
				return;
			}
		}
	}
}

CutsceneCamera *CutsceneEditorPlugin::_previewed() const {
	Node3DEditor *editor = Node3DEditor::get_singleton();
	if (editor == nullptr) {
		return nullptr;
	}
	for (uint32_t i = 0; i < Node3DEditor::VIEWPORTS_COUNT; i++) {
		Node3DEditorViewport *view = editor->get_editor_viewport(i);
		if (view != nullptr && view->is_visible()) {
			if (CutsceneCamera *camera = Object::cast_to<CutsceneCamera>(view->get_previewing_camera())) {
				return camera;
			}
		}
	}
	return nullptr;
}

void CutsceneEditorPlugin::forward_3d_force_draw_over_viewport(Control *p_overlay) {
	Node3DEditorViewport *view = Object::cast_to<Node3DEditorViewport>(p_overlay->get_parent());
	CutsceneCamera *camera = view != nullptr ? Object::cast_to<CutsceneCamera>(view->get_previewing_camera()) : nullptr;
	if (camera == nullptr) {
		return;
	}
	Cutscene *cutscene = nullptr;
	for (Node *n = camera->get_parent(); n != nullptr && cutscene == nullptr; n = n->get_parent()) {
		cutscene = Object::cast_to<Cutscene>(n);
	}
	const Vector2 size = p_overlay->get_size();
	const Rect2 frame = Cutscene::game_frame(size, wide);
	// Outside the game's picture: dimmed.
	const Color dim(0, 0, 0, 0.6);
	p_overlay->draw_rect(Rect2(0, 0, frame.position.x, size.y), dim);
	p_overlay->draw_rect(Rect2(frame.get_end().x, 0, size.x - frame.get_end().x, size.y), dim);
	p_overlay->draw_rect(Rect2(frame.position.x, 0, frame.size.x, frame.position.y), dim);
	p_overlay->draw_rect(Rect2(frame.position.x, frame.get_end().y, frame.size.x, size.y - frame.get_end().y), dim);
	p_overlay->draw_rect(frame, Color(1, 1, 1, 0.5), false, 1.0);
	// The letterbox as the player draws it: 12% of the height, top and bottom.
	if (letterbox && cutscene != nullptr && cutscene->get_letterbox()) {
		const real_t bar = frame.size.y * 0.12f;
		p_overlay->draw_rect(Rect2(frame.position, Vector2(frame.size.x, bar)), Color(0, 0, 0, 0.85));
		p_overlay->draw_rect(Rect2(frame.position.x, frame.get_end().y - bar, frame.size.x, bar), Color(0, 0, 0, 0.85));
	}
	// The HUD is 4:3 in the middle of a wide picture: the subtitles stay within it.
	const Rect2 hud = Cutscene::game_frame(frame.size, false);
	const Rect2 hud_rect(frame.position + hud.position, hud.size);
	if (safe_areas) {
		// Title safe (90%) and the subtitles' band: their three rows around 80% of the height.
		const Rect2 title = hud_rect.grow_individual(-hud_rect.size.x * 0.05f, -hud_rect.size.y * 0.05f, -hud_rect.size.x * 0.05f, -hud_rect.size.y * 0.05f);
		p_overlay->draw_rect(title, Color(1, 0.8, 0.2, 0.6), false, 1.0);
		const Rect2 band(hud_rect.position.x + hud_rect.size.x * 0.1f, hud_rect.position.y + hud_rect.size.y * (0.80f - 0.0825f - 0.03f),
				hud_rect.size.x * 0.8f, hud_rect.size.y * (0.165f + 0.06f));
		p_overlay->draw_rect(band, Color(1, 1, 1, 0.35), false, 1.0);
	}
	if (subtitles && cutscene != nullptr) {
		const PackedStringArray rows = cutscene->subtitles_at(cutscene->get_timeline_position());
		const Ref<Font> font = p_overlay->get_theme_font(SNAME("main"), EditorStringName(EditorFonts));
		const int font_size = MAX(8, int(hud_rect.size.y * 0.04f));
		const int n = MIN(rows.size(), 6);
		for (int r = 0; r < n; r++) {
			const real_t y = hud_rect.position.y + hud_rect.size.y * (0.80f + 0.055f * r - 0.0275f * (n - 1));
			const Vector2 at(hud_rect.position.x, y + font_size * 0.35f);
			p_overlay->draw_string_outline(font, at, rows[r], HORIZONTAL_ALIGNMENT_CENTER, hud_rect.size.x, font_size, 4, Color(0, 0, 0, 0.75));
			p_overlay->draw_string(font, at, rows[r], HORIZONTAL_ALIGNMENT_CENTER, hud_rect.size.x, font_size, Color(0.94, 0.94, 0.94));
		}
	}
}

void CutsceneEditorPlugin::_menu(int p_item) {
	switch (p_item) {
		case ITEM_FRAME_4_3:
			wide = false;
			break;
		case ITEM_FRAME_16_9:
			wide = true;
			break;
		case ITEM_LETTERBOX:
			letterbox = !letterbox;
			break;
		case ITEM_SAFE_AREAS:
			safe_areas = !safe_areas;
			break;
		case ITEM_SUBTITLES:
			subtitles = !subtitles;
			break;
	}
	_update_menu();
	update_overlays();
}

void CutsceneEditorPlugin::_update_menu() {
	PopupMenu *popup = game_view->get_popup();
	popup->set_item_checked(popup->get_item_index(ITEM_FRAME_4_3), !wide);
	popup->set_item_checked(popup->get_item_index(ITEM_FRAME_16_9), wide);
	popup->set_item_checked(popup->get_item_index(ITEM_LETTERBOX), letterbox);
	popup->set_item_checked(popup->get_item_index(ITEM_SAFE_AREAS), safe_areas);
	popup->set_item_checked(popup->get_item_index(ITEM_SUBTITLES), subtitles);
}

void CutsceneEditorPlugin::_notification(int p_what) {
	// Not in the constructor: the plugin's signals are there only once the editor has made it.
	if (p_what == NOTIFICATION_ENTER_TREE) {
		connect("scene_saved", callable_mp(this, &CutsceneEditorPlugin::_scene_saved));
		if (!is_force_draw_over_forwarding_enabled()) {
			set_force_draw_over_forwarding_enabled();
		}
		add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, game_view);
		set_process(true);
		timeline = memnew(CutsceneTimeline);
		add_dock(timeline);
		EditorInterface::get_singleton()->get_selection()->connect("selection_changed", callable_mp(this, &CutsceneEditorPlugin::_selection_changed));
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		disconnect("scene_saved", callable_mp(this, &CutsceneEditorPlugin::_scene_saved));
		remove_control_from_container(CONTAINER_SPATIAL_EDITOR_MENU, game_view);
		set_process(false);
		EditorInterface::get_singleton()->get_selection()->disconnect("selection_changed", callable_mp(this, &CutsceneEditorPlugin::_selection_changed));
		remove_dock(timeline);
		memdelete(timeline);
		timeline = nullptr;
	} else if (p_what == NOTIFICATION_PROCESS) {
		// The subtitles follow the timeline: drawn again when it moves.
		double position = -1.0;
		if (CutsceneCamera *camera = _previewed()) {
			for (Node *n = camera->get_parent(); n != nullptr; n = n->get_parent()) {
				if (Cutscene *cutscene = Object::cast_to<Cutscene>(n)) {
					position = cutscene->get_timeline_position();
					break;
				}
			}
		}
		if (position != last_position) {
			last_position = position;
			update_overlays();
		}
	}
}

CutsceneEditorPlugin::CutsceneEditorPlugin() {
	game_view = memnew(MenuButton);
	game_view->set_text(TTRC("Game View"));
	game_view->set_tooltip_text(TTRC("What the game shows over a view that looks through a cutscene's camera (the camera's Preview)."));
	game_view->set_flat(false);
	game_view->set_theme_type_variation("FlatMenuButton");
	PopupMenu *popup = game_view->get_popup();
	popup->add_radio_check_item(TTRC("Frame 4:3"), ITEM_FRAME_4_3);
	popup->add_radio_check_item(TTRC("Frame 16:9"), ITEM_FRAME_16_9);
	popup->add_separator();
	popup->add_check_item(TTRC("Letterbox"), ITEM_LETTERBOX);
	popup->add_check_item(TTRC("Safe Areas"), ITEM_SAFE_AREAS);
	popup->add_check_item(TTRC("Subtitles"), ITEM_SUBTITLES);
	popup->connect(SceneStringName(id_pressed), callable_mp(this, &CutsceneEditorPlugin::_menu));
	popup->set_hide_on_checkable_item_selection(false);
	_update_menu();
}
