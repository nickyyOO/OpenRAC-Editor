<h1 align="center">
  <a href="https://openrac.dev/">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="docs/assets/openrac-editor-logo-dark.svg">
      <img src="docs/assets/openrac-editor-logo-light.svg" alt="OpenRAC Editor" width="560">
    </picture>
  </a>
</h1>

<p align="center">
  <strong>The level editor for Ratchet &amp; Clank.</strong><br>
  Open a planet from your own disc, change it, and play it in OpenRAC.
</p>

<p align="center">
  <a href="https://github.com/OpenRAC/OpenRAC/releases/latest"><img src="https://img.shields.io/github/v/release/OpenRAC/OpenRAC?filter=launcher-v*&label=get%20the%20launcher&color=e8a317" alt="Get the OpenRAC launcher"></a>
  <a href="https://openrac.dev/"><img src="https://img.shields.io/badge/website-openrac.dev-e8a317" alt="openrac.dev"></a>
  <a href="https://discord.gg/Sfd2B54PDG"><img src="https://img.shields.io/badge/Discord-join%20us-5865F2?logo=discord&logoColor=white" alt="Join us on Discord"></a>
</p>

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#get-started">Get started</a> ·
  <a href="#build-from-source">Build from source</a> ·
  <a href="docs/README.md">Docs</a>
</p>

<p align="center">
  <img src="docs/assets/cutscene-editor.gif" alt="A cutscene on Kerwan in the editor: two Ratchets in their animations, one walking his path, seen through the cutscene camera with letterbox and subtitles, and the Cutscene dock's timeline below" width="100%">
</p>

## What is OpenRAC Editor?

OpenRAC Editor is the level editor of [OpenRAC](https://github.com/OpenRAC/OpenRAC), the native PC
version of the PlayStation 2 Ratchet & Clank games. It opens each planet as it is on your disc, with
its terrain, objects, collision and sky, and saves your changes in a form the game can load.

It is built on [Godot Engine](https://godotengine.org) and keeps its 3D tools, focused on
Ratchet & Clank levels.

## Features

### Make your own cutscenes

Add a Cutscene to a level and direct it: key the camera, place characters, give them lines,
voices and music. The Cutscene dock shows the whole scene on one timeline, and the 3D view shows
the camera's picture framed as the game shows it (4:3 or 16:9), with the letterbox, the safe areas
and the subtitles. The characters play their animations as you scrub, and walk their paths.

Saving the level writes the cutscene beside it, and the cutscene player mod plays it in OpenRAC
when its trigger happens: the level starting, walking into an area, talking to someone, picking
something up, a mission flag, or another cutscene ending. Start skips it, and a skip leaves the
world just as watching does.

### Characters that stand on the ground

Actors are the level's own characters, new ones of any class the level has, a figure in the hero's
look, or your own glTF meshes. They walk along paths, play the game's animations and look at each
other. **Foot IK** keeps their feet on steps and slopes: a scene made on flat ground still stands
right where the ground is not.

<p align="center">
  <img src="docs/assets/foot-ik.gif" alt="Ratchet standing on the edge of a step: without foot IK one foot floats over the grass, with foot IK one foot stands on the grass and the other on the step" width="80%">
</p>

Faces move too: mouths follow the voice lines, eyes blink, and moods (happy, angry, ...) change the
face over time. Animation layers move only some joints (an arm, the head) over the animation under
them, and an animation from another rig (a glTF file) can be retargeted onto a character.

### Subtitles in the disc's languages

Lines are written in each language the disc has (English, French, German, Spanish and Italian on
PAL, English on NTSC-U), one field each, and the game shows the one it is set to. Lines the game's
font cannot write, such as accented letters, are drawn into the file as pictures.

### Events

At any time a cutscene can hide, show or remove objects, set a mission flag, fade, add heat haze,
change the fog and background colour, slow time down, play another cutscene or travel to another
planet.

See [the cutscene format](docs/CUTSCENE_FORMAT.md).

### Every object, in the scene tree

Each moby, tie and shrub of a level is a node. Pick one in the 3D view or the scene tree, then move,
turn or scale it, copy it, or delete it. The inspector shows what the game stores for each moby,
such as its spawn id, group, colour and class variables, and your changes go back into the level.

### Collision and gameplay data

The Collision panel draws the ground the game tests Ratchet against, coloured by surface type. Pick
faces to repaint, move or delete them, or add new ones on the game's grid. Change what each level
starts with too: fog and background colour, the height where Ratchet falls to his death, where the
ship lands, cameras, sounds, volumes, paths and grind rails.

### Play your changes

The OpenRAC launcher packs your edits into a mod and starts the game with them. Your disc image is
never changed. You can also build a whole new world and add it as a ship destination.

### Work with an AI assistant

The editor has a built-in MCP server. Connect Claude or another assistant and it can find
objects, change them, look at the 3D view and undo its steps, as you would. See
[MCP](docs/MCP.md).

### Your own tools

Write your own editor tools and checks in GDScript. The class reference is built in: **Help > Search
Help**.

## Get started

1. [Get the OpenRAC launcher](https://github.com/OpenRAC/OpenRAC/releases/latest) and add your disc.
2. In **Mods**, open the **Level editor** tab and press **Get the levels ready**.
3. Pick a planet and open it.

Edits play in Ratchet & Clank (PAL and NTSC-U). Levels of the later games open to explore.

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
[Godot Engine contributors](AUTHORS.md).
The logo's lettering is set in [Righteous](https://fonts.google.com/specimen/Righteous).

**See:** [License](LICENSE) · [Godot's license](LICENSE.txt) · [Third-party notices](COPYRIGHT.txt)

Godot is a trademark of the Godot Foundation; OpenRAC Editor is not made or endorsed by it.
OpenRAC is an unofficial fan project, not affiliated with or endorsed by the owners of
Ratchet & Clank. All trademarks belong to their owners.
