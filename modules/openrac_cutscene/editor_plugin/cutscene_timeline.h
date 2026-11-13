/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: the Cutscene dock, the cutscene's timeline at a glance: a row for the camera,
 * each actor (and its walk, moods and layers), the subtitles, the sounds and the events, their keys
 * and spans on one time scale in the game's ticks. Click to go to a time or pick a node, drag to
 * move what has a start. */

#pragma once

#include "editor/docks/editor_dock.h"

class Button;
class Cutscene;
class HScrollBar;
class Label;

class CutsceneTimelineView : public Control {
	GDCLASS(CutsceneTimelineView, Control);

public:
	struct Item {
		double start = 0.0;
		double end = -1.0; // -1: a point (a key, an event)
		ObjectID node;
		String text;
		bool movable = false; // has a `start` that a drag moves
	};
	struct Row {
		String label;
		int depth = 0;
		ObjectID node;
		Vector<Item> items;
	};

private:
	ObjectID cutscene;
	Vector<Row> rows;
	double length = 1.0;
	double zoom = 120.0; // pixels a second
	double scroll = 0.0; // seconds at the left edge
	bool fit = false; // the whole cutscene in view (at any width) until zoomed by hand
	// A drag in progress: seeking, or moving an item's start.
	bool seeking = false;
	ObjectID dragged;
	double drag_from = 0.0;
	double drag_start = 0.0;
	double drag_offset = 0.0;

	real_t _label_width() const;
	real_t _row_height() const;
	real_t _ruler_height() const;
	double _time_at(real_t p_x) const;
	real_t _x_of(double p_seconds) const;
	const Item *_item_at(const Point2 &p_point) const;

protected:
	static void _bind_methods();
	void _notification(int p_what);
	void gui_input(const Ref<InputEvent> &p_event) override;

public:
	// Gathers the rows from the cutscene's nodes and timeline (null: none).
	void set_cutscene(Cutscene *p_cutscene);
	Cutscene *get_cutscene() const;
	void refresh();
	void seek(double p_seconds);
	double get_length() const { return length; }
	void set_zoom(double p_zoom);
	double get_zoom() const { return zoom; }
	// Keeps the whole cutscene in view, as the dock's width changes, until zoomed by hand.
	void fit_to_length();
	CursorShape get_cursor_shape(const Point2 &p_pos = Point2i()) const override;
	void set_scroll(double p_seconds);
	double get_scroll() const { return scroll; }
	double visible_seconds() const;
	const Vector<Row> &get_rows() const { return rows; }

	Size2 get_minimum_size() const override;
};

class CutsceneTimeline : public EditorDock {
	GDCLASS(CutsceneTimeline, EditorDock);

	CutsceneTimelineView *view = nullptr;
	HScrollBar *scroll = nullptr;
	Button *play = nullptr;
	Button *rewind = nullptr;
	Button *zoom_out = nullptr;
	Button *zoom_fit = nullptr;
	Button *zoom_in = nullptr;
	Label *time = nullptr;
	double last_position = -1.0;
	bool updating_scroll = false;
	int frames = 0;

	void _play_pressed();
	void _rewind_pressed();
	void _zoom_by(double p_factor);
	void _zoom_fit();
	void _scrolled(double p_value);
	void _update_scroll();

protected:
	void _notification(int p_what);

public:
	void set_cutscene(Cutscene *p_cutscene);
	Cutscene *get_cutscene() const;
	CutsceneTimelineView *get_view() const { return view; }

	CutsceneTimeline();
};
