// SPDX-License-Identifier: MIT
// editor-runtime: the OpenRAC mod that plays what OpenRAC Editor packs with a level beside its
// placements: cutscenes (cutscenes.cpp) and behaviours (behaviours.cpp). A pack made in the editor
// needs it on.
#include "openrac/mod.hpp"

namespace editor_runtime {
void cutscenes_load(openrac::Mod& mod);
void behaviours_load(openrac::Mod& mod);
}  // namespace editor_runtime

namespace {

void load(openrac::Mod& mod) {
    editor_runtime::cutscenes_load(mod);
    editor_runtime::behaviours_load(mod);
    mod.info("ready");
}

}  // namespace

OPENRAC_MOD_CPP("editor-runtime", "0.4.0", load)
