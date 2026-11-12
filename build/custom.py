# OpenRAC Editor build options: desktop editor, one renderer.
# Use from the repo root: scons platform=<linuxbsd|windows|macos> profile=build/custom.py

target = "editor"
production = "yes"  # also turns on LTO
optimize = "size"
debug_symbols = "no"
deprecated = "no"
tests = "no"

# Rendering: Vulkan on Windows and Linux. macOS builds pass metal=yes vulkan=no (README).
vulkan = "yes"
opengl3 = "no"
d3d12 = "no"
metal = "no"

# OpenRAC look and name: our splash, no update check against godotengine.org.
no_editor_splash = "no"
engine_update_check = "no"

# Desktop editor: no screen reader kit, no gamepad driver, no XR.
accesskit = "no"
sdl = "no"
disable_xr = "yes"

# Text: the simple text server (no ICU or HarfBuzz). It is off by default, so switch it on.
module_text_server_fb_enabled = "yes"

# Modules that OpenRAC does not use are deleted from modules/, so there is nothing more to switch off.
