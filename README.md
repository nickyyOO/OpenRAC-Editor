<h1 align="center">
  <a href="https://openrac.dev/">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="docs/assets/openrac-logo-dark.svg">
      <img src="docs/assets/openrac-logo-light.svg" alt="OpenRAC" width="440">
    </picture>
  </a>
</h1>

<p align="center">
  <strong>OpenRAC Editor</strong><br>
  The level editor for Ratchet &amp; Clank. Open a planet from your own disc, change it, and play it in OpenRAC.
</p>

<p align="center">
  <a href="https://github.com/OpenRAC/OpenRAC/releases/latest"><img src="https://img.shields.io/github/v/release/OpenRAC/OpenRAC?filter=launcher-v*&label=get%20the%20launcher&color=e8a317" alt="Get the OpenRAC launcher"></a>
  <a href="https://github.com/nickyyOO/OpenRAC-Editor/actions/workflows/ai-rules.yml"><img src="https://github.com/nickyyOO/OpenRAC-Editor/actions/workflows/ai-rules.yml/badge.svg" alt="ai-rules"></a>
  <a href="https://openrac.dev/"><img src="https://img.shields.io/badge/website-openrac.dev-e8a317" alt="openrac.dev"></a>
  <a href="https://discord.gg/Sfd2B54PDG"><img src="https://img.shields.io/badge/Discord-join%20us-5865F2?logo=discord&logoColor=white" alt="Join us on Discord"></a>
</p>

<p align="center">
  <a href="#what-you-can-do">What you can do</a> ·
  <a href="#get-started">Get started</a> ·
  <a href="#build-from-source">Build from source</a> ·
  <a href="docs/README.md">Docs</a>
</p>

<p align="center">
  <img src="docs/assets/editor.png" alt="OpenRAC Editor with Kerwan open: the scene tree, the 3D view and the inspector" width="860">
</p>

## What is OpenRAC Editor?

OpenRAC Editor is the level editor of [OpenRAC](https://github.com/OpenRAC/OpenRAC), the native PC
version of the PlayStation 2 Ratchet & Clank games. It opens each planet as it is on your disc,
with its terrain, objects, collision and sky, and saves your changes in a form the game can load.

It is built on [Godot Engine](https://godotengine.org) 4.7 and keeps its 3D tools, made small and
focused on Ratchet & Clank levels.

## What you can do

- **Move, turn and scale objects.** Every moby, tie and shrub of a level is a node you can pick
  in the 3D view or the scene tree.
- **Add and delete objects,** and change what the game stores for each one in the inspector.
- **Edit collision** in the Collision panel: see the ground by surface type and change it, for
  the level and for each kind of object.
- **Work with gameplay data:** the values and settings each level keeps for its objects.
- **Preview a level** by flying around it.
- **Script your own tools** in GDScript.
- **Play your changes:** the launcher packs your edits into a mod and starts the game with them.
  Your disc image is never changed.

<p align="center">
  <img src="docs/assets/collision.png" alt="The Collision panel showing Kerwan's ground by surface type" width="420">
  <img src="docs/assets/inspector.png" alt="A moby picked in the 3D view, with its game data in the inspector" width="420">
</p>

## Get started

1. [Get the OpenRAC launcher](https://github.com/OpenRAC/OpenRAC/releases/latest) and add your disc.
2. In **Mods**, open the **Level editor** tab and press **Get the levels ready**.
3. Pick a planet and open it.

**Guides:** [Level editor](https://github.com/OpenRAC/OpenRAC/blob/develop/editor/README.md) ·
[Map mods](https://github.com/OpenRAC/OpenRAC/blob/develop/editor/MAP_MODDING.md)

## Build from source

You need Python 3, SCons and a C++ compiler.

```sh
git clone https://github.com/nickyyOO/OpenRAC-Editor.git && cd OpenRAC-Editor
scons platform=linuxbsd profile=build/custom.py      # Linux
scons platform=windows profile=build/custom.py       # Windows
scons platform=macos arch=arm64 profile=build/custom.py metal=yes vulkan=no   # macOS
```

**More:** [Building](docs/BUILDING.md) · [Changes from Godot](docs/CHANGES.md) · [Contributing](CONTRIBUTING.md)

## Credits and license

OpenRAC Editor is built on Godot Engine, by Juan Linietsky, Ariel Manzur and the
[Godot Engine contributors](AUTHORS.md), with the support of its [donors](DONORS.md).

**See:** [License](LICENSE) · [Godot's license](LICENSE.txt) · [Third-party notices](COPYRIGHT.txt)

Godot is a trademark of the Godot Foundation; OpenRAC Editor is not made or endorsed by it.
OpenRAC is an unofficial fan project, not affiliated with or endorsed by the owners of
Ratchet & Clank. All trademarks belong to their owners.
