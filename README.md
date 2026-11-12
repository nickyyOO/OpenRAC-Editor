# OpenRAC Editor

The level editor for [OpenRAC](https://github.com/OpenRAC/OpenRAC). It is a fork of
[Godot 4.7.2](https://github.com/godotengine/godot) with everything OpenRAC does not use taken out.
Godot's own readme is in [README.godot.md](README.godot.md).

This fork is updated rarely. It does not follow new Godot releases.

## What is different from Godot

- Its own name, icon, splash screen and colours (the OpenRAC launcher's warm coal and gold).
- Desktop only: Windows, Linux and macOS.
- One renderer: Vulkan on Windows and Linux, Metal on macOS (Apple Silicon). No OpenGL or Direct3D 12.
- No project list. The OpenRAC launcher opens each level; started on its own, the editor says so and quits.
- English only: no translations and no fonts for other scripts.
- Removed: export platforms and templates, Asset Store, update check, version control, the 2D and
  Game screens, 2D node editors, GridMap, baked lighting, UI theme editor.
- Unused modules are off: 2D and 3D physics, navigation, XR, networking and TLS, CSG, GridMap,
  visual shaders, video, Ogg/MP3 audio, texture compressors, and image formats other than PNG and SVG.
- The simple text server (no ICU or HarfBuzz).

All build options are in [build/custom.py](build/custom.py). Source changes are marked with
`OpenRAC Editor:` comments.

## Size

Linux x86_64 editor, measured:

| Build | Program | Zip |
|---|---|---|
| Godot 4.7.2 (official) | 146 MB | 78 MB |
| OpenRAC Editor | 67 MB | 25 MB |

## Build

You need Python 3, SCons and a C++ compiler, as for Godot
([Godot's build docs](https://docs.godotengine.org/en/stable/contributing/development/compiling/index.html)).
Run from the repo root:

```sh
# Linux (Clang)
scons platform=linuxbsd profile=build/custom.py use_llvm=yes linker=lld

# Windows (MSVC or MinGW)
scons platform=windows profile=build/custom.py

# macOS (Apple Silicon, Metal)
scons platform=macos arch=arm64 profile=build/custom.py metal=yes vulkan=no
```

The program is written to `bin/`. For macOS, copy `misc/dist/macos_tools.app` to `OpenRAC Editor.app`
and put the program in `Contents/MacOS/OpenRAC Editor`.

## License

MIT, like Godot. Keep [LICENSE.txt](LICENSE.txt) and [COPYRIGHT.txt](COPYRIGHT.txt) with every copy.
"Godot" is a trademark of the Godot Foundation; this fork is not Godot.
