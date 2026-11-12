/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: writes the level's cutscenes each time the level is saved. */

#include "cutscene_editor_plugin.h"

#include "../cutscene.h"

#include "core/object/callable_mp.h"
#include "core/templates/hash_set.h"
#include "editor/editor_node.h"

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

void CutsceneEditorPlugin::_notification(int p_what) {
	// Not in the constructor: the plugin's signals are there only once the editor has made it.
	if (p_what == NOTIFICATION_ENTER_TREE) {
		connect("scene_saved", callable_mp(this, &CutsceneEditorPlugin::_scene_saved));
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		disconnect("scene_saved", callable_mp(this, &CutsceneEditorPlugin::_scene_saved));
	}
}
