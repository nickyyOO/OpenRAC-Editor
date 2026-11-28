# Changes from Godot

OpenRAC Editor starts from the Godot 4.7.2-stable source. This page lists everything it changed.
Every change to Godot's own code has an `OpenRAC Editor:` comment beside it.

## Taken out

These are deleted from the source:

- **Platforms:** Android, iOS, visionOS and Web, and the export code of every platform.
- **Modules:** text_server_adv, msdfgen, astcenc, basis_universal, bcdec, betsy, bmp, camera, csg,
  cvtt, dds, enet, etcpak, fbx, godot_physics_2d, godot_physics_3d, gridmap, hdr,
  interactive_music, jolt_physics, jpg, jsonrpc, ktx, mbedtls, lightmapper_rd, mobile_vr, mono, mp3,
  multiplayer, navigation_2d, navigation_3d, noise, objectdb_profiler, ogg, openxr, raycast, tga,
  theora, tinyexr, upnp, vhacd, visual_shader, vorbis, webp, webrtc, websocket, webxr,
  xatlas_unwrap. GDScript has no language server.
- **Drivers:** OpenGL, Direct3D 12, EGL, AccessKit, XAudio2, MIDI. Linux has no
  text-to-speech.
- **Editor:** the project list, Asset Store, version control, the 2D screen and the 2D node
  editors, the TileMap, GridMap MeshLibrary, baked lighting (LightmapGI, VoxelGI), SDF collision,
  physical bone, sprite frames, virtual joystick and UI theme editors.
- **Editor parts a level maker never uses:** the Script screen and script editor, the shader
  editors, the class reference page, the run bar and Game screen, the debugger and profilers,
  export (presets, templates, the `--export-*` options, Android build template), project settings
  and autoloads, the Godot 3 project converter, the Files, Import, Signals, Groups and Audio docks,
  the Find in Files and orphan resource windows, the move/copy folder windows, new script and
  shader pickers, the FileSystem tooltips, and the editor plugin calls that only served them
  (tool menu items, autoloads, export and debugger plugins, 2D canvas hooks).
- **Libraries** in `thirdparty/` that only those used, the built-in TLS certificates and the
  fallback fonts for other scripts. `COPYRIGHT.txt` lists only what is left.
- **Translations:** all editor and class reference translations. The editor is in English.
- **Godot's project files:** CI workflows, issue templates, unit tests, doc tools, changelog,
  donors list, Godot's logos, Visual Studio project generation, clang settings, Linux and Windows
  packaging, export templates.

## Hidden in the editor

- Started without a level, the editor says to open one from the OpenRAC launcher and quits.
- No update check, build profiles or feature profiles.
- The Help menu links to OpenRAC's level editor guide and bug tracker.
- The 3D toolbar has no sun and sky preview, group, list select, trackball or keep-parts buttons.
- The Keys panel (Godot's Animation panel) has no play backwards, autoplay, onion skin, pin,
  libraries, transitions or properties items.
- Properties (Godot's Inspector) shows only what the game uses: position, rotation, size,
  visibility, a camera's zoom, a path's curve, the game values, OpenRAC's own sections and the
  cutscene types. Godot's node settings (process, physics, script) are hidden, and so are the game's
  bookkeeping values (indexes, ids, raw bits).
- Settings shows a short list: editor size and look, saving, and how the 3D view moves.

## Changed

- **Name:** OpenRAC Editor (`version.py`). Settings live in their own folder (`openrac_editor`), apart
  from Godot's.
- **Look:** OpenRAC's icon, logo, splash and the launcher's colours: warm coal (#24221e) and gold
  (#f5c040). Themes: OpenRAC and Black (OLED). Fonts: Inter and JetBrains Mono.
- **Rendering:** Vulkan on Windows and Linux, Metal on macOS. One renderer, so no renderer switch.
  The default rendering method is Mobile, not Forward+: the levels draw unshaded, and Mobile draws
  them faster with less video memory. The directional shadow atlas is 2048, not 4096, and sky
  reflections use no texture arrays (as on mobile), which saves about 100 MB of video memory.
- **Light mode** (Editor Settings > Interface > Editor > Display > Light Mode): for slow machines.
  The editor updates at most 60 times a second, 3D views on HiDPI screens draw at half resolution,
  the level's effects stand still unless a cutscene moves them, and ties and shrubs are drawn only
  as far as the game draws them. Auto turns it on with 8 GB of memory or less, or with an
  integrated GPU and 16 GB or less (Apple Silicon counts as integrated).
- **About window:** says what the editor is; Godot's authors and licenses stay.
- **Its own name in texts:** menus, window titles, the system's app names (X11 class, Wayland app id,
  Windows app id), messages and the tools' output say OpenRAC Editor or "the engine" instead of
  Godot. Kept: credits, licenses, the class reference, and Godot version numbers in migration
  messages (Godot 3 to 4, glTF naming versions). The tools' `godot` command is now `projects`;
  `godot` still works.
- **Inspector buttons** (tool buttons) take the inspector's full width, so a row of them lines up.
- **Version:** the editor shows its own version, `dev` for a local build or the release tag (such as
  `v0.2.0`), not Godot's 4.7.2. The engine keeps 4.7.2 inside, for compatibility.
- **Plain words:** every text the editor shows is written for someone who has never used a game
  engine: short, plain English, the same words everywhere (object, not node; Properties, not
  Inspector; Level list, not Scene). Docks: Scene is **Level**, Inspector is **Properties**, Output
  is **Messages**, Animation is **Keys**, History is **Undo History**. The menus are **Level**,
  **View** and **Help**. Game values show plain labels (`EditorPropertyNameProcessor`), and the
  level's fixed parts read Objects, Big Scenery, Small Scenery and Ground in the Level list.
- **Level list groups:** Objects, Big Scenery and Small Scenery show their copies grouped by model,
  one row each with its name and count ("Mountain Wall ×24"), and each copy reads as that name and
  a number. Clicking a group selects all its copies; its lock makes them unclickable in the 3D view.
  Only the list changes (`SceneTreeEditor::_openrac_*`); names come from the Assets dock's tables
  through EditorInterface's `openrac_outline_label` meta.

## Added

- **MCP server** (`editor/mcp/`): AI assistants read and edit the open level, with undo. See
  [MCP.md](MCP.md).
- **Cutscenes** (`modules/openrac_cutscene/`): the Cutscene node and its camera, actors,
  walks (CutscenePath), camera shakes (CutsceneShake), subtitles, sounds and events, keyed on an
  AnimationPlayer. Saving the level writes each one to `cutscenes/<name>.cutscene` beside it; a
  `.cutscene` imports back into nodes. Lines the game's font cannot write (Japanese) are drawn into
  the file as pictures. Tests: `modules/openrac_cutscene/tests/run_tests.sh`.
- **The map player** (built into OpenRAC, `port/game/rac1/editor_runtime`): plays what the editor
  packs with a level: its cutscenes ([CUTSCENE_FORMAT.md](CUTSCENE_FORMAT.md))
  and the behaviours set in a moby's Inspector, spin, bob, patrol, orbit, face or follow the hero
  ([BEHAVIOUR_FORMAT.md](BEHAVIOUR_FORMAT.md)).

- **Where things are** (`editor/editor_node.cpp`, `editor/gui/editor_first_steps.cpp`): left, the Level
  list (with Undo History) over the Assets to add from; right, Properties at full height over
  Collision and Voices; the bottom panel (Cutscene, Messages, Keys) stays closed until needed.
  Levels saved with the old arrangement take the new one once. The bar at the bottom says what the
  mouse and keys do where the pointer is (Settings > Editor > Hints), a first steps card shows how
  to make a level until "Got It", Properties says how to pick something, and Move, Turn and Size
  are named on the toolbar. Properties lost Godot's file buttons and name styles.

- **Motion** (`editor/gui/editor_motion.cpp`): an intro when the editor opens (the README's logo over a
  gold line, with what is loading; any key, click or pad button skips it), tab pages fade
  and slide in, menus and windows fade in, lists scroll smoothly (`ScrollBar::wheel`), the pad bar
  slides up and away, and a gold frame glides after the pad's focus. Settings > Editor > Animations
  and Intro turn them off.

- **Gamepad** (`editor/gui/editor_gamepad.cpp`): every panel, list, menu and field works with a pad
  (Steam Deck, Steam Controller, any pad). The pad acts as the arrow keys, Enter and Escape; LB/RB
  switch tabs, View goes to the next panel, Start opens the menus, Y is the right-click menu, X is
  undo, the right stick scrolls. In the 3D view the sticks fly and look, A picks under a cross,
  holding X carries the picked objects, LB/RB turn them. A bar at the bottom shows each button.
  The Steam Deck's two touchpads (SDL's Steam Deck driver now sends them, `thirdparty/sdl/patches`):
  the right one is the mouse, the left one scrolls (looks in 3D); pressing them clicks. On the Deck
  the editor opens in one window and text fields bring up the Steam keyboard. A on a number lets
  the d-pad change it (LT fine, RT big steps, B puts it back); right on a list row steps onto its
  buttons (the eye); menus leave out their keyboard shortcuts while the pad is used.
  Tests: `openrac/tools/test_editor_gamepad.py`.
- **Play Level** is one big gold button on the toolbar; Check Level is gone (Play checks the same
  edits first and lists the ones the game can't take).

## Build options

[build/custom.py](../build/custom.py) holds the rest: size-optimized production build, no
deprecated code, no XR, our splash, no update check.
