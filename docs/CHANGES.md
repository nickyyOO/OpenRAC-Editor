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

## Changed

- **Name:** OpenRAC Editor (`version.py`). Settings live in their own folder (`openrac_editor`), apart
  from Godot's.
- **Look:** OpenRAC's icon, logo, splash and the launcher's colours: warm coal (#24221e) and gold
  (#f5c040). Themes: OpenRAC and Black (OLED). Fonts: Inter and JetBrains Mono.
- **Rendering:** Vulkan on Windows and Linux, Metal on macOS. One renderer, so no renderer switch.
- **About window:** says what the editor is; Godot's authors and licenses stay.
- **Inspector buttons** (tool buttons) take the inspector's full width, so a row of them lines up.

## Added

- **MCP server** (`editor/mcp/`): AI assistants read and edit the open level, with undo. See
  [MCP.md](MCP.md).
- **Cutscenes** (`modules/openrac_cutscene/`): the Cutscene node and its camera, actors,
  walks (CutscenePath), camera shakes (CutsceneShake), subtitles, sounds and events, keyed on an
  AnimationPlayer. Saving the level writes each one to `cutscenes/<name>.cutscene` beside it; a
  `.cutscene` imports back into nodes. Lines the game's font cannot write (Japanese) are drawn into
  the file as pictures. Tests: `modules/openrac_cutscene/tests/run_tests.sh`.
- **Cutscene player** (`mods/cutscene-player/`): the OpenRAC mod that plays the editor's
  cutscenes. See [CUTSCENE_FORMAT.md](CUTSCENE_FORMAT.md).

## Build options

[build/custom.py](../build/custom.py) holds the rest: size-optimized production build, no
deprecated code, no XR, our splash, no update check.
