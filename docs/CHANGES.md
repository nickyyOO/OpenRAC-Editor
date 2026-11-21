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
- **Drivers:** OpenGL, Direct3D 12, EGL, SDL input, AccessKit, XAudio2, MIDI. Linux has no
  text-to-speech.
- **Editor:** the project list, Asset Store, version control, the 2D node editors, the TileMap,
  GridMap MeshLibrary, baked lighting (LightmapGI, VoxelGI), SDF collision, physical bone, sprite
  frames, virtual joystick and UI theme editors.
- **Libraries** in `thirdparty/` that only those used, the built-in TLS certificates and the
  fallback fonts for other scripts. `COPYRIGHT.txt` lists only what is left.
- **Translations:** all editor and class reference translations. The editor is in English.
- **Godot's project files:** CI workflows, issue templates, unit tests, doc tools, changelog,
  donors list, Godot's logos, Visual Studio project generation, clang settings, Linux and Windows
  packaging, export templates.

## Hidden in the editor

- Started without a level, the editor says to open one from the OpenRAC launcher and quits.
- No update check, export or export templates, build profiles or feature profiles.
- No 2D and Game screens: levels are 3D and the game runs in OpenRAC.
- The Help menu links to OpenRAC's level editor guide and bug tracker.
- Only the 3D screen. The Script and Asset Library screens, the run bar and the profiler are hidden.
- Docks a level maker does not need are hidden: Files, Import, Signals, Groups, Debugger, Audio,
  Search Results, Shader Editor, Animation Tree and Preloader (`_is_openrac_hidden_dock`).
- The 3D toolbar has no sun and sky preview, group, list select, trackball or keep-parts buttons.
- The Keys panel (Godot's Animation panel) has no play backwards, autoplay, onion skin, pin,
  libraries, transitions or properties items.
- Properties (Godot's Inspector) shows only what the game uses: position, rotation, size,
  visibility, a camera's zoom, a path's curve, the game values, OpenRAC's own sections and the
  cutscene types. Godot's node settings (process, physics, script) are hidden, and so are the game's
  bookkeeping values (indexes, ids, raw bits).
- The Level list has no script, signal, group or unique-name buttons.
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

## Added

- **MCP server** (`editor/mcp/`): AI assistants read and edit the open level, with undo. See
  [MCP.md](MCP.md).
- **Cutscenes** (`modules/openrac_cutscene/`): the Cutscene node and its camera, actors,
  walks (CutscenePath), camera shakes (CutsceneShake), subtitles, sounds and events, keyed on an
  AnimationPlayer. Saving the level writes each one to `cutscenes/<name>.cutscene` beside it; a
  `.cutscene` imports back into nodes. Lines the game's font cannot write (Japanese) are drawn into
  the file as pictures. Tests: `modules/openrac_cutscene/tests/run_tests.sh`.
- **editor-runtime** (`mods/editor-runtime/`): the OpenRAC mod that plays what the editor packs
  with a level, which such a level needs on: its cutscenes ([CUTSCENE_FORMAT.md](CUTSCENE_FORMAT.md))
  and the behaviours set in a moby's Inspector, spin, bob, patrol, orbit, face or follow the hero
  ([BEHAVIOUR_FORMAT.md](BEHAVIOUR_FORMAT.md)).

## Build options

[build/custom.py](../build/custom.py) holds the rest: size-optimized production build, no
deprecated code, no XR, our splash, no update check.
