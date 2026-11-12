# Building

OpenRAC Editor builds like Godot 4.7.2, with one build profile:
[build/custom.py](../build/custom.py). Run every command from the repo root.

## What you need

- Python 3 and SCons (`pip install scons`).
- A C++ compiler: Clang or GCC on Linux, MSVC or MinGW on Windows, Xcode on macOS.

Godot's guides list the system packages per platform:
[compiling Godot](https://docs.godotengine.org/en/4.7/contributing/development/compiling/index.html).

## Linux

```sh
scons platform=linuxbsd profile=build/custom.py use_llvm=yes linker=lld
```

Output: `bin/godot.linuxbsd.editor.x86_64.llvm`. Without `wayland-scanner` the build has X11 only.

## Windows

```sh
scons platform=windows profile=build/custom.py
```

Output: `bin/godot.windows.editor.x86_64.exe`.

## macOS

Apple Silicon only, on Metal:

```sh
scons platform=macos arch=arm64 profile=build/custom.py metal=yes vulkan=no
```

Then make the app: copy `misc/dist/macos_tools.app` to `OpenRAC Editor.app` and put the program in
`Contents/MacOS/OpenRAC Editor`.

## Check it

- `bin/<program> --version` prints `4.7.2.stable.custom_build`.
- Started with no level, the editor says to open a level from the OpenRAC launcher and quits.
- `bin/<program> --editor --path <level folder>` opens a level the launcher made.

Builds take a while: a clean Linux build is about 17 minutes on 4 cores.
