# Changes from Godot

OpenRAC Editor starts from the Godot 4.7.2-stable source. This page lists everything it changed.
Every change to Godot's own code has an `OpenRAC Editor:` comment beside it.

## Taken out

These folders are deleted:

- **Platforms:** Android, iOS, visionOS and Web, and the export code of every platform.
- **Modules:** text_server_adv, msdfgen, astcenc, basis_universal, bcdec, betsy, bmp, camera, csg,
  cvtt, dds, enet, etcpak, fbx, godot_physics_2d, godot_physics_3d, gridmap, hdr,
  interactive_music, jolt_physics, jpg, jsonrpc, ktx, mbedtls, lightmapper_rd, mobile_vr, mono, mp3,
  multiplayer, navigation_2d, navigation_3d, noise, objectdb_profiler, ogg, openxr, raycast, tga,
  theora, tinyexr, upnp, vhacd, visual_shader, vorbis, webp, webrtc, websocket, webxr,
  xatlas_unwrap. GDScript has no language server.
- **Drivers:** OpenGL, Direct3D 12, EGL, SDL input, AccessKit, XAudio2.
- **Libraries** in `thirdparty/` that only those used.
- **Translations:** all editor and class reference translations. The editor is in English.
- **Godot's project files:** CI workflows, issue templates, unit tests, doc tools, changelog,
  Godot's logos, Linux and Windows packaging, export templates.

## Hidden or cut in the editor

- No project list. The OpenRAC launcher opens each level; started without one, the editor says so
  and quits.
- No Asset Store, update check, version control, export, export templates, Android build template,
  build profiles or feature profiles.
- No 2D and Game screens: levels are 3D and the game runs in OpenRAC.
- No editors for 2D nodes, GridMap, baked lighting (LightmapGI, VoxelGI), physical bones, sprite
  frames or UI themes.
- Fonts: Inter and JetBrains Mono only, no fallback fonts for other scripts.
- The Help menu links to OpenRAC's level editor guide and bug tracker.

## Changed

- **Name:** OpenRAC Editor (`version.py`). Settings live in their own folder (`openrac_editor`), apart
  from Godot's.
- **Look:** OpenRAC's icon, logo, splash and the launcher's colours: warm coal (#24221e) and gold
  (#f5c040). Themes: OpenRAC and Black (OLED).
- **Rendering:** Vulkan on Windows and Linux, Metal on macOS. One renderer, so no renderer switch.
- **About window:** says what the editor is; Godot's authors, donors and licenses stay.

## Build options

[build/custom.py](../build/custom.py) holds the rest: size-optimized production build, no
deprecated code, no XR, our splash, no update check.
