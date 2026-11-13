/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: the Cutscene dock (see cutscene_timeline.h). */

#include "cutscene_timeline.h"

#include "../cutscene.h"
#include "../cutscene_math.h"

#include "core/input/input_event.h"
#include "core/object/class_db.h"
#include "core/object/callable_mp.h"
#include "editor/editor_interface.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/themes/editor_scale.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/scroll_container.h"
#include "scene/resources/animation.h"
#include "scene/resources/font.h"

namespace {

// The game's ticks: every time the dock gives or takes is on one.
double snap(double p_seconds) {
	return Math::round(MAX(0.0, p_seconds) * cutscene_math::TICK_RATE) / cutscene_math::TICK_RATE;
}

const char *const EVENT_NAMES[] = { "Hide Moby", "Show Moby", "Delete Moby", "Mission Flag", "Heat Haze", "Fade", "Play Cutscene", "Travel", "Fog and Background", "Slow Motion", "Hide Tie", "Show Tie" };

} // namespace

// ---------------------------------------------------------------------------------------------
// The view

real_t CutsceneTimelineView::_label_width() const { return 170 * EDSCALE; }
real_t CutsceneTimelineView::_row_height() const { return 22 * EDSCALE; }
real_t CutsceneTimelineView::_ruler_height() const { return 24 * EDSCALE; }

double CutsceneTimelineView::_time_at(real_t p_x) const {
	return scroll + double(p_x - _label_width()) / zoom;
}

real_t CutsceneTimelineView::_x_of(double p_seconds) const {
	return _label_width() + real_t((p_seconds - scroll) * zoom);
}

double CutsceneTimelineView::visible_seconds() const {
	return MAX(0.0, double(get_size().x - _label_width()) / zoom);
}

Cutscene *CutsceneTimelineView::get_cutscene() const {
	return Object::cast_to<Cutscene>(ObjectDB::get_instance(cutscene));
}

void CutsceneTimelineView::set_cutscene(Cutscene *p_cutscene) {
	const ObjectID id = p_cutscene != nullptr ? p_cutscene->get_instance_id() : ObjectID();
	const bool other = id != cutscene;
	cutscene = id;
	refresh();
	if (other) {
		scroll = 0.0;
		fit = true; // a new cutscene opens whole, and stays whole until zoomed
		queue_redraw();
	}
}

void CutsceneTimelineView::fit_to_length() {
	fit = true;
	queue_redraw();
}

Control::CursorShape CutsceneTimelineView::get_cursor_shape(const Point2 &p_pos) const {
	const Item *item = _item_at(p_pos);
	if (dragged.is_valid() || (item != nullptr && item->movable && p_pos.x >= _label_width())) {
		return CURSOR_HSIZE;
	}
	return Control::get_cursor_shape(p_pos);
}

void CutsceneTimelineView::refresh() {
	rows.clear();
	Cutscene *cs = get_cutscene();
	if (cs == nullptr) {
		update_minimum_size();
		queue_redraw();
		return;
	}
	const Ref<Animation> anim = cs->get_timeline_animation();
	AnimationPlayer *player = cs->get_timeline();
	const Node *root = player != nullptr ? player->get_node_or_null(player->get_root_node()) : nullptr;
	length = cs->get_length() > 0.0 ? cs->get_length() : (anim.is_valid() ? double(anim->get_length()) : 1.0);
	length = MAX(length, 1.0 / cutscene_math::TICK_RATE);

	// The times of the keys of the tracks on `p_node` (its own properties, not its children's).
	const auto keys_of = [&](const Node *p_node, Vector<Item> &r_items) {
		if (anim.is_null() || root == nullptr) {
			return;
		}
		const String own = String(root->get_path_to(p_node));
		HashSet<int> seen;
		for (int i = 0; i < anim->get_track_count(); i++) {
			const NodePath path = anim->track_get_path(i);
			if (String(path.get_concatenated_names()) != own) {
				continue;
			}
			for (int k = 0; k < anim->track_get_key_count(i); k++) {
				const double t = anim->track_get_key_time(i, k);
				const int tick = int(Math::round(t * cutscene_math::TICK_RATE));
				if (seen.has(tick)) {
					continue;
				}
				seen.insert(tick);
				Item item;
				item.start = t;
				item.node = p_node->get_instance_id();
				r_items.push_back(item);
			}
		}
	};
	const auto point = [](const Node *p_node, double p_start, const String &p_text) {
		Item item;
		item.start = p_start;
		item.node = p_node->get_instance_id();
		item.text = p_text;
		item.movable = true;
		return item;
	};

	Row subtitles, sounds, events;
	subtitles.label = TTR("Subtitles");
	sounds.label = TTR("Sounds");
	events.label = TTR("Events");
	for (int i = 0; i < cs->get_child_count(); i++) {
		Node *child = cs->get_child(i);
		if (CutsceneCamera *cam = Object::cast_to<CutsceneCamera>(child)) {
			Row row;
			row.label = String(cam->get_name());
			row.node = cam->get_instance_id();
			keys_of(cam, row.items);
			for (float cut : cam->get_cuts()) {
				Item item;
				item.start = cut;
				item.node = cam->get_instance_id();
				item.text = TTR("cut");
				row.items.push_back(item);
			}
			rows.push_back(row);
			for (int c = 0; c < cam->get_child_count(); c++) {
				if (CutsceneShake *shake = Object::cast_to<CutsceneShake>(cam->get_child(c))) {
					Row sub;
					sub.label = String(shake->get_name());
					sub.depth = 1;
					sub.node = shake->get_instance_id();
					Item item = point(shake, shake->get_start(), TTR("shake"));
					item.end = shake->get_start() + shake->get_duration();
					sub.items.push_back(item);
					rows.push_back(sub);
				}
			}
		} else if (CutsceneActor *actor = Object::cast_to<CutsceneActor>(child)) {
			Row row;
			row.label = String(actor->get_name());
			row.node = actor->get_instance_id();
			keys_of(actor, row.items);
			rows.push_back(row);
			for (int c = 0; c < actor->get_child_count(); c++) {
				Node *part = actor->get_child(c);
				Row sub;
				sub.label = String(part->get_name());
				sub.depth = 1;
				sub.node = part->get_instance_id();
				if (CutscenePath *walk = Object::cast_to<CutscenePath>(part)) {
					sub.items.push_back(point(walk, walk->get_start(), TTR("walk")));
				} else if (CutsceneMood *mood = Object::cast_to<CutsceneMood>(part)) {
					Item item = point(mood, mood->get_start(), mood->get_mood().is_empty() ? TTR("own face") : mood->get_mood());
					item.end = mood->get_start() + mood->get_blend();
					sub.items.push_back(item);
				} else if (CutsceneLayer *layer = Object::cast_to<CutsceneLayer>(part)) {
					keys_of(layer, sub.items);
				} else {
					continue;
				}
				rows.push_back(sub);
			}
		} else if (CutsceneSubtitle *line = Object::cast_to<CutsceneSubtitle>(child)) {
			const Dictionary text = line->get_text();
			Item item = point(line, line->get_start(), text.is_empty() ? String(line->get_name()) : String(text.values()[0]));
			item.end = line->get_start() + line->get_duration();
			subtitles.items.push_back(item);
		} else if (CutsceneSound *sound = Object::cast_to<CutsceneSound>(child)) {
			const String file = sound->get_file();
			Item item = point(sound, sound->get_start(), file.is_empty() ? String(sound->get_name()) : file.get_file());
			if (sound->get_stop() > sound->get_start()) {
				item.end = sound->get_stop();
			}
			sounds.items.push_back(item);
		} else if (CutsceneEvent *event = Object::cast_to<CutsceneEvent>(child)) {
			const int action = int(event->get_action());
			events.items.push_back(point(event, event->get_start(), action >= 0 && action < int(std::size(EVENT_NAMES)) ? String(EVENT_NAMES[action]) : String()));
		}
	}
	for (Row *row : { &subtitles, &sounds, &events }) {
		rows.push_back(*row);
	}
	update_minimum_size();
	queue_redraw();
}

void CutsceneTimelineView::_bind_methods() {
	ClassDB::bind_method(D_METHOD("refresh"), &CutsceneTimelineView::refresh);
	ClassDB::bind_method(D_METHOD("seek", "seconds"), &CutsceneTimelineView::seek);
}

Size2 CutsceneTimelineView::get_minimum_size() const {
	return Size2(_label_width() + 100 * EDSCALE, _ruler_height() + _row_height() * MAX(1, rows.size()));
}

void CutsceneTimelineView::set_zoom(double p_zoom) {
	fit = false; // zoomed by hand
	zoom = CLAMP(p_zoom, 10.0, 2000.0);
	queue_redraw();
}

void CutsceneTimelineView::set_scroll(double p_seconds) {
	scroll = CLAMP(p_seconds, 0.0, MAX(0.0, length - visible_seconds() * 0.5));
	queue_redraw();
}

void CutsceneTimelineView::seek(double p_seconds) {
	Cutscene *cs = get_cutscene();
	AnimationPlayer *player = cs != nullptr ? cs->get_timeline() : nullptr;
	const Ref<Animation> anim = cs != nullptr ? cs->get_timeline_animation() : Ref<Animation>();
	if (player == nullptr || anim.is_null()) {
		return;
	}
	if (player->get_assigned_animation() == StringName() || player->get_animation(player->get_assigned_animation()) != anim) {
		LocalVector<StringName> names;
		player->get_animation_list(&names);
		for (const StringName &name : names) {
			if (player->get_animation(name) == anim) {
				player->set_assigned_animation(name);
				break;
			}
		}
	}
	player->seek(CLAMP(snap(p_seconds), 0.0, length), true);
	queue_redraw();
}

const CutsceneTimelineView::Item *CutsceneTimelineView::_item_at(const Point2 &p_point) const {
	const int r = int((p_point.y - _ruler_height()) / _row_height());
	if (p_point.y < _ruler_height() || r < 0 || r >= rows.size()) {
		return nullptr;
	}
	const real_t reach = 5 * EDSCALE;
	const Item *best = nullptr;
	for (const Item &item : rows[r].items) {
		const real_t x0 = _x_of(item.start);
		const real_t x1 = item.end >= 0.0 ? _x_of(item.end) : x0;
		if (p_point.x >= x0 - reach && p_point.x <= x1 + reach) {
			best = &item;
			if (item.movable) {
				break;
			}
		}
	}
	return best;
}

void CutsceneTimelineView::gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed()) {
		if (mb->get_button_index() == MouseButton::WHEEL_UP || mb->get_button_index() == MouseButton::WHEEL_DOWN) {
			const double sign = mb->get_button_index() == MouseButton::WHEEL_UP ? 1.0 : -1.0;
			if (mb->is_command_or_control_pressed()) {
				// Zoom about the time under the pointer.
				const double at = _time_at(mb->get_position().x);
				set_zoom(zoom * (sign > 0 ? 1.2 : 1.0 / 1.2));
				set_scroll(at - double(mb->get_position().x - _label_width()) / zoom);
				accept_event();
			} else if (mb->is_shift_pressed()) {
				fit = false;
				set_scroll(scroll - sign * visible_seconds() * 0.1);
				accept_event();
			}
			return;
		}
		if (mb->get_button_index() != MouseButton::LEFT || mb->get_position().x < _label_width()) {
			if (mb->get_button_index() == MouseButton::LEFT) {
				// A row's name: its node.
				const int r = int((mb->get_position().y - _ruler_height()) / _row_height());
				if (r >= 0 && r < rows.size()) {
					if (Node *node = Object::cast_to<Node>(ObjectDB::get_instance(rows[r].node))) {
						EditorInterface::get_singleton()->edit_node(node);
					}
				}
			}
			return;
		}
		const Item *item = _item_at(mb->get_position());
		if (item != nullptr) {
			Node *node = Object::cast_to<Node>(ObjectDB::get_instance(item->node));
			if (node != nullptr) {
				EditorInterface::get_singleton()->edit_node(node);
			}
			seek(item->start);
			if (item->movable && node != nullptr) {
				dragged = item->node;
				drag_from = double(node->get("start"));
				drag_start = drag_from;
				drag_offset = _time_at(mb->get_position().x) - drag_from;
			}
		} else {
			seeking = true;
			seek(_time_at(mb->get_position().x));
		}
		accept_event();
		return;
	}
	if (mb.is_valid() && !mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		seeking = false;
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(dragged));
		dragged = ObjectID();
		if (node != nullptr && drag_start != drag_from) {
			const double moved_to = drag_start;
			node->set("start", drag_from);
			EditorUndoRedoManager *undo = EditorUndoRedoManager::get_singleton();
			undo->create_action(TTR("Move in the Cutscene"));
			undo->add_do_property(node, "start", moved_to);
			undo->add_undo_property(node, "start", drag_from);
			undo->add_do_method(this, "refresh");
			undo->add_undo_method(this, "refresh");
			undo->commit_action();
		}
		return;
	}
	const Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && mm->get_button_mask().has_flag(MouseButtonMask::LEFT)) {
		if (seeking) {
			seek(_time_at(mm->get_position().x));
		} else if (Node *node = Object::cast_to<Node>(ObjectDB::get_instance(dragged))) {
			drag_start = snap(_time_at(mm->get_position().x) - drag_offset);
			node->set("start", drag_start);
			refresh();
		}
		accept_event();
	}
}

void CutsceneTimelineView::_notification(int p_what) {
	if (p_what != NOTIFICATION_DRAW) {
		return;
	}
	if (fit) {
		const double room = double(get_size().x - _label_width()) - 40.0 * EDSCALE;
		if (room > 0.0) {
			zoom = CLAMP(room / MAX(length, 0.5), 10.0, 2000.0);
			scroll = 0.0;
		}
	}
	const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
	const int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Label"));
	const Color text = get_theme_color(SceneStringName(font_color), SNAME("Label"));
	const Color dim = text * Color(1, 1, 1, 0.45);
	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Size2 size = get_size();
	const real_t lw = _label_width(), rh = _row_height(), ruler = _ruler_height();
	const real_t end_x = _x_of(length);

	draw_rect(Rect2(Point2(), size), get_theme_color(SNAME("dark_color_2"), EditorStringName(Editor)));
	if (get_cutscene() == nullptr) {
		draw_string(font, Point2(8 * EDSCALE, ruler), TTR("Select a Cutscene, or a node in one, to see its timeline."), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, dim);
		return;
	}
	// Past the end: shaded, with a line where it ends.
	if (end_x < size.x) {
		draw_rect(Rect2(MAX(lw, end_x), 0, size.x - MAX(lw, end_x), size.y), Color(0, 0, 0, 0.45));
	}
	// The ruler: seconds, and tenths when there is room.
	const double step = zoom >= 300.0 ? 0.1 : (zoom >= 60.0 ? 0.5 : (zoom >= 20.0 ? 1.0 : 5.0));
	for (double t = Math::floor(scroll / step) * step; _x_of(t) < size.x; t += step) {
		const real_t x = _x_of(t);
		if (x < lw) {
			continue;
		}
		const bool whole = Math::is_equal_approx(t, Math::round(t));
		draw_line(Point2(x, whole ? 0 : ruler * 0.5f), Point2(x, size.y), whole ? dim : dim * Color(1, 1, 1, 0.4));
		if (whole) {
			draw_string(font, Point2(x + 3 * EDSCALE, ruler - 6 * EDSCALE), vformat("%ds", int(Math::round(t))), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.85, dim);
		}
	}
	draw_line(Point2(0, ruler), Point2(size.x, ruler), dim);
	// The rows.
	for (int r = 0; r < rows.size(); r++) {
		const Row &row = rows[r];
		const real_t y = ruler + rh * r;
		if (r % 2 == 1) {
			draw_rect(Rect2(0, y, size.x, rh), Color(1, 1, 1, 0.03));
		}
		draw_string(font, Point2(6 * EDSCALE + row.depth * 14 * EDSCALE, y + rh * 0.7f), row.label, HORIZONTAL_ALIGNMENT_LEFT, lw - 10 * EDSCALE - row.depth * 14 * EDSCALE, font_size, row.depth > 0 ? dim : text);
		for (const Item &item : row.items) {
			const real_t x0 = _x_of(item.start);
			const Color colour = item.movable ? accent : text;
			if (item.end >= 0.0) {
				const real_t x1 = MAX(_x_of(item.end), x0 + 2);
				if (x1 < lw || x0 > size.x) {
					continue;
				}
				const Rect2 bar(MAX(x0, lw), y + 3 * EDSCALE, x1 - MAX(x0, lw), rh - 6 * EDSCALE);
				draw_rect(bar, colour * Color(1, 1, 1, 0.35));
				draw_rect(bar, colour, false, 1.0);
				if (!item.text.is_empty() && bar.size.x > 20 * EDSCALE) {
					draw_string(font, Point2(bar.position.x + 4 * EDSCALE, y + rh * 0.7f), item.text, HORIZONTAL_ALIGNMENT_LEFT, bar.size.x - 8 * EDSCALE, font_size * 0.85, text);
				}
			} else {
				if (x0 < lw || x0 > size.x) {
					continue;
				}
				const real_t h = rh * 0.28f;
				const Vector2 c(x0, y + rh * 0.5f);
				const Vector<Point2> diamond = { c + Vector2(0, -h), c + Vector2(h, 0), c + Vector2(0, h), c + Vector2(-h, 0) };
				draw_colored_polygon(diamond, colour);
				if (!item.text.is_empty()) {
					draw_string(font, Point2(x0 + h + 3 * EDSCALE, y + rh * 0.7f), item.text, HORIZONTAL_ALIGNMENT_LEFT, 160 * EDSCALE, font_size * 0.85, dim);
				}
			}
		}
	}
	draw_line(Point2(lw, 0), Point2(lw, size.y), dim);
	if (end_x >= lw && end_x <= size.x) {
		draw_line(Point2(end_x, 0), Point2(end_x, size.y), text * Color(1, 1, 1, 0.7), 1.0);
		const String end_text = TTR("End");
		const real_t w = font->get_string_size(end_text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.85).x;
		const real_t at = end_x + 3 * EDSCALE + w <= size.x ? end_x + 3 * EDSCALE : end_x - 3 * EDSCALE - w;
		draw_string(font, Point2(at, ruler - 6 * EDSCALE), end_text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.85, text);
	}
	// Where the timeline is.
	const real_t now = _x_of(get_cutscene()->get_timeline_position());
	if (now >= lw && now <= size.x) {
		draw_line(Point2(now, 0), Point2(now, size.y), accent, 2.0);
	}
}

// ---------------------------------------------------------------------------------------------
// The dock

void CutsceneTimeline::set_cutscene(Cutscene *p_cutscene) {
	view->set_cutscene(p_cutscene);
	_update_scroll();
}

Cutscene *CutsceneTimeline::get_cutscene() const {
	return view->get_cutscene();
}

void CutsceneTimeline::_play_pressed() {
	Cutscene *cs = get_cutscene();
	AnimationPlayer *player = cs != nullptr ? cs->get_timeline() : nullptr;
	if (player == nullptr) {
		return;
	}
	if (player->is_playing()) {
		player->pause();
		return;
	}
	view->seek(cs->get_timeline_position() >= view->get_length() ? 0.0 : cs->get_timeline_position());
	player->play(player->get_assigned_animation());
}

void CutsceneTimeline::_zoom_by(double p_factor) {
	// About the time where the timeline is when it is in sight, else the left edge.
	const double now = get_cutscene() != nullptr ? get_cutscene()->get_timeline_position() : 0.0;
	const double seen = view->visible_seconds();
	const bool in_sight = now >= view->get_scroll() && now <= view->get_scroll() + seen;
	const double anchor = in_sight ? now : view->get_scroll();
	const double offset = anchor - view->get_scroll();
	const double before = view->get_zoom();
	view->set_zoom(before * p_factor);
	view->set_scroll(anchor - offset * before / view->get_zoom());
	_update_scroll();
}

void CutsceneTimeline::_zoom_fit() {
	view->fit_to_length();
	_update_scroll();
}

void CutsceneTimeline::_rewind_pressed() {
	view->seek(0.0);
}

void CutsceneTimeline::_scrolled(double p_value) {
	if (!updating_scroll) {
		view->set_scroll(p_value);
	}
}

void CutsceneTimeline::_update_scroll() {
	updating_scroll = true;
	scroll->set_max(view->get_length());
	scroll->set_page(MIN(view->visible_seconds(), view->get_length()));
	scroll->set_value(view->get_scroll());
	updating_scroll = false;
}

void CutsceneTimeline::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			play->set_button_icon(get_editor_theme_icon(SNAME("Play")));
			rewind->set_button_icon(get_editor_theme_icon(SNAME("PlayStartBackwards")));
			zoom_out->set_button_icon(get_editor_theme_icon(SNAME("ZoomLess")));
			zoom_fit->set_button_icon(get_editor_theme_icon(SNAME("AnimationAutoFit")));
			zoom_in->set_button_icon(get_editor_theme_icon(SNAME("ZoomMore")));
		} break;
		case NOTIFICATION_PROCESS: {
			Cutscene *cs = get_cutscene();
			const double position = cs != nullptr ? cs->get_timeline_position() : -1.0;
			AnimationPlayer *player = cs != nullptr ? cs->get_timeline() : nullptr;
			play->set_button_icon(get_editor_theme_icon(player != nullptr && player->is_playing() ? SNAME("Pause") : SNAME("Play")));
			// The nodes change under it (added, renamed, keyed): gathered again twice a second.
			if (++frames % 30 == 0) {
				view->refresh();
			}
			if (position != last_position) {
				last_position = position;
				time->set_text(position < 0.0 ? String() : vformat(TTR("%.2f s  |  tick %d of %d"), position, int(Math::round(position * cutscene_math::TICK_RATE)), int(Math::round(view->get_length() * cutscene_math::TICK_RATE))));
				view->queue_redraw();
			}
			_update_scroll();
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			set_process(is_visible_in_tree());
			if (is_visible_in_tree()) {
				view->refresh();
			}
		} break;
	}
}

CutsceneTimeline::CutsceneTimeline() {
	set_name(TTRC("Cutscene"));
	set_icon_name("Animation");
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);

	VBoxContainer *box = memnew(VBoxContainer);
	add_child(box);
	HBoxContainer *bar = memnew(HBoxContainer);
	box->add_child(bar);
	rewind = memnew(Button);
	rewind->set_flat(true);
	rewind->set_tooltip_text(TTRC("To the start"));
	rewind->connect(SceneStringName(pressed), callable_mp(this, &CutsceneTimeline::_rewind_pressed));
	bar->add_child(rewind);
	play = memnew(Button);
	play->set_flat(true);
	play->set_tooltip_text(TTRC("Play or pause the cutscene in the 3D view (at the game's 60 ticks a second)"));
	play->connect(SceneStringName(pressed), callable_mp(this, &CutsceneTimeline::_play_pressed));
	bar->add_child(play);
	time = memnew(Label);
	time->set_h_size_flags(SIZE_EXPAND_FILL);
	bar->add_child(time);
	zoom_out = memnew(Button);
	zoom_out->set_flat(true);
	zoom_out->set_tooltip_text(TTRC("Zoom out (Ctrl+wheel)"));
	zoom_out->connect(SceneStringName(pressed), callable_mp(this, &CutsceneTimeline::_zoom_by).bind(1.0 / 1.5));
	bar->add_child(zoom_out);
	zoom_fit = memnew(Button);
	zoom_fit->set_flat(true);
	zoom_fit->set_tooltip_text(TTRC("Show the whole cutscene"));
	zoom_fit->connect(SceneStringName(pressed), callable_mp(this, &CutsceneTimeline::_zoom_fit));
	bar->add_child(zoom_fit);
	zoom_in = memnew(Button);
	zoom_in->set_flat(true);
	zoom_in->set_tooltip_text(TTRC("Zoom in (Ctrl+wheel)"));
	zoom_in->connect(SceneStringName(pressed), callable_mp(this, &CutsceneTimeline::_zoom_by).bind(1.5));
	bar->add_child(zoom_in);

	ScrollContainer *rows = memnew(ScrollContainer);
	rows->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	rows->set_v_size_flags(SIZE_EXPAND_FILL);
	rows->set_custom_minimum_size(Size2(0, 160 * EDSCALE));
	box->add_child(rows);
	view = memnew(CutsceneTimelineView);
	view->set_h_size_flags(SIZE_EXPAND_FILL);
	view->set_v_size_flags(SIZE_EXPAND_FILL);
	view->set_tooltip_text(TTRC("Click: go to that time, or pick what is there.\nDrag a highlighted start to move it.\nCtrl+wheel: zoom.  Shift+wheel: scroll."));
	rows->add_child(view);
	scroll = memnew(HScrollBar);
	scroll->set_step(0);
	scroll->connect(SceneStringName(value_changed), callable_mp(this, &CutsceneTimeline::_scrolled));
	box->add_child(scroll);
}
