/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: the cutscene nodes (docs/CUTSCENE_FORMAT.md). */

#include "register_types.h"

#include "cutscene.h"

#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor_plugin/cutscene_editor_plugin.h"
#include "editor/plugins/editor_plugin.h"
#endif

void initialize_openrac_cutscene_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(Cutscene);
		GDREGISTER_CLASS(CutsceneCamera);
		GDREGISTER_CLASS(CutsceneActor);
		GDREGISTER_CLASS(CutsceneSubtitle);
		GDREGISTER_CLASS(CutsceneSound);
		GDREGISTER_CLASS(CutsceneEvent);
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::add_by_type<CutsceneEditorPlugin>();
	}
#endif
}

void uninitialize_openrac_cutscene_module(ModuleInitializationLevel p_level) {
}
