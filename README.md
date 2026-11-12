# OpenRAC Editor

The level editor for [OpenRAC](https://github.com/OpenRAC/OpenRAC). It is a fork of
[Godot 4.7.2](https://github.com/godotengine/godot) with everything OpenRAC does not use taken out.
Godot's own readme is in [README.godot.md](README.godot.md).

This fork is updated rarely. It does not follow new Godot releases.

## What is different from Godot

- Desktop only: Windows, Linux and macOS.
- Vulkan only, like the game. macOS uses MoltenVK. No OpenGL, Direct3D 12 or Metal.
- No export platforms. OpenRAC packs levels with its own tools.
- Editor text in English and Polish only. No translated class reference.
- Unused modules are off: 2D and 3D physics, navigation, XR, networking, CSG, GridMap,
  visual shaders, video, Ogg/MP3 audio, texture compressors, and image formats other than PNG, SVG and TGA.
- The simple text server (no ICU or HarfBuzz), so right-to-left and complex scripts do not shape.

All build options are in [build/custom.py](build/custom.py). Source changes are marked with
`OpenRAC Editor:` comments.

## Size

Linux x86_64 editor, measured:

| Build | Program | Zip |
|---|---|---|
| Godot 4.7.2 (official) | 146 MB | 78 MB |
| OpenRAC Editor | 73 MB | 29 MB |

## Build

You need Python 3, SCons and a C++ compiler, as for Godot
([Godot's build docs](https://docs.godotengine.org/en/stable/contributing/development/compiling/index.html)).
Run from the repo root:

```sh
# Linux (Clang)
scons platform=linuxbsd profile=build/custom.py use_llvm=yes linker=lld

# Windows (MSVC or MinGW)
scons platform=windows profile=build/custom.py

# macOS (needs the Vulkan SDK with MoltenVK)
scons platform=macos arch=arm64 profile=build/custom.py vulkan_sdk_path=<path to the Vulkan SDK>
```

The program is written to `bin/`.

## License

MIT, like Godot. Keep [LICENSE.txt](LICENSE.txt) and [COPYRIGHT.txt](COPYRIGHT.txt) with every copy.
"Godot" is a trademark of the Godot Foundation; this fork is not Godot.
