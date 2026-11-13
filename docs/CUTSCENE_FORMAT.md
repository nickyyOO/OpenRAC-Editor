# The .cutscene format (draft, version 1)

One cutscene is one JSON file, `<name>.cutscene`, written by OpenRAC Editor and played by the
OpenRAC cutscene player (`mods/cutscene-player`). The editor packs it with its level as
`level_NN/cutscenes/<name>.cutscene`; files it names (voices, music, meshes) are paths beside it. It does not depend on the game: anything game-specific (class ids, joint
numbers, sequence numbers, sound ids) is a number the game's own data gives, and the player maps
it through the game's `openrac_api`.

## Rules

- **Time** is in game ticks, 60 a second, for every game and disc (the port runs PAL in its own
  60 Hz mode, `tick_rate()`). The port draws frames between ticks by itself. Animation keys of the
  game's own sequences run at the game's rate (30 Hz in RAC1), so `frame` numbers are the game's.
- **Space** is the game's: units and axes as `openrac_api` gives them (z up). The editor converts
  from Godot (game `(x, y, z)` = Godot `(x, z, -y)`).
- **Angles** are radians; `rotation` is `[x, y, z]` with z the yaw, as `openrac_pose` has it.
- **Actors are found by id, not by node path**, so a level extracted again keeps working: a moby
  of the level by its `spawn_id` (the game's UID).
- **Languages** are the disc's own: the file lists them in `languages`, the first one is the
  fallback. Text and voice are keyed by language code.
- Unknown keys are kept by the editor and ignored by the player, so a newer file still plays its
  known parts.

## Top level

```jsonc
{
  "format": "openrac-cutscene",
  "version": 1,
  "game": "rac1",              // the game's id, as in a mod manifest
  "level": 3,
  "name": "Meet the vendor",
  "length": 720,               // ticks
  "languages": ["en", "fr", "de", "es", "it"],
  "trigger": { ... },
  "options": { ... },
  "hero": { ... },
  "camera": { ... },
  "actors": [ ... ],
  "subtitles": [ ... ],
  "sounds": [ ... ],
  "events": [ ... ],
  "variants": [ ... ]
}
```

## `trigger`

| Key | Meaning |
|---|---|
| `kind` | `volume` (the hero enters an area: `box` `{ "min": [x, y, z], "max": [x, y, z] }` or `sphere` `{ "position": [x, y, z], "radius": r }`; the editor bakes a level volume to a box), `talk` (the hero is within `radius`, default 4, of the moby `spawn_id` and presses Circle), `pickup` (the moby `spawn_id` leaves the level), `level_start` (the level loaded, the player in control), `after` (another cutscene ends, `cutscene` its name), `flag` (the mission flag `flag` becomes set), `manual` (only a mod or another cutscene starts it) |
| `once` | Play once per save (default true); remembered in the mod's save data |
| `flag` | Optional condition: `{ "level": 3, "byte": 4, "is": 1 }` on the mission state |

The player starts a cutscene only when the game would start one of its own: the player in
control, on foot, alive, with no menu and no game scene playing. Until then it waits.

## `options`

| Key | Default | Meaning |
|---|---|---|
| `skippable` | true | Start skips it. On a skip the player applies the end state of every `persistent` event and puts actors where the cutscene leaves them |
| `letterbox` | true | Black bars, as the game's scenes |
| `hide_hud` | true | The game's HUD is hidden while it plays (subtitles stay) |
| `world` | `"run"` | `"run"`: the level goes on; `"hold"`: mobys that are not actors (nor the hero) do not update while it plays |
| `music` | `"duck"` | `"keep"`, `"duck"` (a quarter of its volume), `"stop"` (silent); eased over half a second, and back on the way out |

## `hero`

| Key | Meaning |
|---|---|
| `mode` | `"held"` (he stays, controls locked, `hero_grab`), `"hidden"`, or `"actor"` (a figure in his look is an actor with id `"hero"`) |
| `end` | Where he stands when it ends: `{ "position": [x, y, z], "yaw": r }`, placed on the ground |

## `camera`

```jsonc
"camera": {
  "keys": [
    { "t": 0,   "eye": [260, 147, 52], "look_at": [262, 150, 50], "fov": 52, "ease": "smooth" },
    { "t": 120, "eye": [265, 140, 53], "look_at": [262, 150, 50], "cut": true },
    { "t": 300, "eye": [266, 141, 53], "rotation": [0.1, 0, 2.4], "roll": 0 }
  ],
  "shake": [ { "t": 400, "length": 30, "strength": 0.4 } ],
  "end": "ease"     // "ease" back to the game's camera, or "cut"
}
```

A key gives `eye` and either `look_at` (a point, or `{ "actor": "vendor", "joint": 4 }`) or
`rotation` (`[roll, pitch, yaw]`: yaw 0 looks along +x, a pitch above 0 looks down). A look-at
actor point is the actor's place plus `offset` (default `[0, 0, 1]`). `cut` jumps to the key with no motion from the one before. `ease` is `linear`,
`smooth` (Catmull-Rom through the keys) or `bezier` with `in` and `out` handles. `fov` is the
vertical field of view in degrees (1 to 150), linear between the keys that give one and jumping at a
cut; the game's own is kept when no key gives one. It changes only the picture, never what the
game's logic sees. `{ "actor": "vendor", "joint": 4 }` is that joint where the port last drew it
(the offset then defaults to none); a port that cannot place joints uses the actor's place.
`shake` keys wobble the eye from `t` for `length` ticks (30 if not given), fading out, by
`strength` units (0.3 if not given).

## `actors`

```jsonc
{
  "id": "vendor",
  "source": { "spawn_id": 149 },        // a moby of the level
  // or { "class": 11 }                 // a new moby of a class the level has
  // or { "figure": "hero" }            // a figure in the hero's look
  // or { "prop": "meshes/robot.glb" }  // a mesh beside the .cutscene file
  "missing": "spawn",                   // a level moby that is gone: "spawn" a stand-in, or "skip"
  "track": [
    { "t": 0,   "position": [262, 150, 50], "rotation": [0, 0, 1.2], "scale": 1, "ease": "smooth" },
    { "t": 240, "position": [264, 148, 50] }
  ],
  "path": { "points": [[262, 150, 50], [270, 150, 50]], "speed": 8, "ground": true, "t": 300 },
  "anims": [
    { "t": 0,   "seq": 0, "loop": true },
    { "t": 120, "seq": 3, "frame": 0, "speed": 1, "blend": 4 }
  ],
  "look_at": [ { "t": 0, "target": { "actor": "hero" }, "weight": 1 } ],
  "visible": [ { "t": 0, "on": true } ],
  "held": [ { "t": 0, "class": 1088, "joint": 56 } ],
  "end": "keep"                          // "keep" where it is, "restore" as it was, "remove"
}
```

`track` keys move it; `path` walks it along points on the ground at `speed` units a second (8 if
not given) from tick `t` instead, facing the way it walks. `anims` start the game's sequences of the
actor's class, blending over `blend` ticks.

`look_at` turns the actor's body (its yaw) towards a `target` (a point, or an actor or the hero,
with `joint` and `offset` as the camera's), by `weight` (0 to 1, 1 if not given), easing to it
over `blend` ticks (10 if not given) from the key before. A key with `"target": null` eases the
turn out. A new target turns the body round at most 0.2 radians a tick.

`held` keys say what the actor holds from `t` on: `{ "from": "hero" }` what the hero holds now (a
figure only), `{ "class": 1088, "joint": 56 }` a moby of that class on that joint (`item` and
`slot`, the game's, for a figure), or `{}` nothing. On an actor that is not a figure the held moby
follows the joint's place and the actor's turn.

### Layers (advanced, version 1.1)

```jsonc
"layers": [
  { "name": "face", "joints": [12, 13, 14], "weight": [ { "t": 0, "w": 1 } ],
    "frames": "faces/vendor_face.bin" }   // joint rotations per tick, written by the editor
]
```

Expressions, lip sync and hand-set joint keys all become layers baked to one joint rotation per
tick for the listed joints; the player turns the actor's joints by them, on top of its animation
(the port's joint offsets, as a mod turns the hero's). `frames` is a file beside the .cutscene
file: for each tick from 0, for each joint in `joints`' order, a quaternion (x, y, z, w) as four
little-endian float32s; after its last tick the last holds. `weight` keys (`w` from 0 to 1, linear
between them, 1 if none) blend each rotation in from none. The editor does not write layers yet:
they come from a tool or by hand, and the editor keeps them as written (the actor's `advanced`).

## `subtitles`

```jsonc
{ "t": 30, "end": 150, "text": { "en": "Need a weapon?", "fr": "Besoin d'une arme ?" }, "actor": "vendor" }
```

Drawn in the game's font at the bottom, as the game's scenes do. A language the line lacks shows
the first language of `languages`.

The game's font writes only ASCII letters (accents are dropped). For a line it cannot write
(Japanese, or any letter past Latin), the editor adds a picture of it, drawn with a system font,
which the player draws over the picture instead:

```jsonc
"bitmap": { "ja": { "width": 412, "height": 34, "pixel": 0.5, "bits": "AAAA..." } }
```

`bits` is base64 of rows of 1-bit pixels (the first pixel in each byte's top bit, every row whole
bytes); `pixel` is the size of one in the game's 512 x 448 picture. Up to three rows, broken at
spaces or anywhere in a language without them. The editor needs a font with the letters (Noto
Sans CJK, Yu Gothic, Meiryo, Hiragino); without one it says so and writes no picture.

## `sounds`

```jsonc
{ "t": 30, "kind": "voice", "file": { "en": "audio/vendor_01_en.wav" }, "actor": "vendor", "volume": 1 }
{ "t": 0,  "kind": "music", "file": "audio/theme.ogg", "fade_in": 60, "fade_out": 60 }
{ "t": 90, "kind": "sfx", "game_sound": 26, "actor": "hero" }
```

`file` is beside the .cutscene file (WAV or Ogg Vorbis). `kind` decides the volume group the player
uses. `game_sound` is an entry of the actor's class's sound table (the hero's when `actor` is
`"hero"` or not given), played on that actor where it is.

## `events`

```jsonc
{ "t": 200, "do": "moby_hide", "spawn_id": 210, "on": true, "persistent": true }
{ "t": 260, "do": "moby_kill", "spawn_id": 211, "persistent": true }
{ "t": 300, "do": "mission_flag", "level": 3, "byte": 4, "value": 1, "persistent": true }
{ "t": 600, "do": "fade", "to": [0, 0, 0], "length": 30 }      // to black over half a second
{ "t": 660, "do": "fade", "to": null, "length": 30 }           // and back to the picture
{ "t": 0,   "do": "haze", "position": [260, 150, 52], "radius": 4, "strength": 0.1, "length": 120 }
{ "t": 700, "do": "play", "cutscene": "vendor_part2" }
{ "t": 710, "do": "travel", "level": 4 }
```

`persistent` events are the ones a skip must still apply.

A `fade` goes from the colour before it (none at first) to `to`, `[r, g, b]` or `[r, g, b, a]`
from 0 to 1, over `length` ticks (30 if not given, 0 at once). `to` as `null` or with `a` 0 clears
it. What a fade leaves goes with the way back to the game's camera.

## `variants`

```jsonc
"variants": [ { "if": { "level": 3, "byte": 4, "is": 1 }, "play": "vendor_again" } ]
```

Checked when the cutscene would start, in order: the first that holds plays instead.

## In the editor

A **Cutscene** node in the level (best under its `Game` node, so its children are in the game's
units) is one file. **Set Up Timeline and Camera** adds an AnimationPlayer whose animation is the
cutscene's timeline, and a **CutsceneCamera**. Its children are what the file lists:

| Node | In the file |
|------|-------------|
| CutsceneCamera | `camera`: its place and turn on the timeline, sampled every tenth of a second and at each key; `cuts` (seconds) jump |
| CutsceneActor | `actors`: **Take Selected Moby** makes it a moby of the level; a `sequence` key starts an animation, `visible` keys show and hide it; `advanced` holds `look_at`, `held` and `layers` as the file writes them |
| CutscenePath (a child of an actor) | the actor's `path`: the curve's points, drawn in the 3D view as any Path3D's; `start`, `speed`, `ground` |
| CutsceneShake (a child of the camera) | `camera.shake`: `start`, `duration`, `strength` |
| CutsceneSubtitle | `subtitles` |
| CutsceneSound | `sounds` |
| CutsceneEvent | `events` |
| a Node3D called HeroEnd | `hero.end` |

Saving the level writes every Cutscene to `cutscenes/<name>.cutscene` beside the scene, and the
launcher packs them with the level. **Export .cutscene** writes one now; **Import .cutscene** reads
one back into a new, empty Cutscene (a camera looking at an actor becomes its turn, bezier handles
a smooth curve, and events the editor has no node for are not kept: it says which). From a script, `to_cutscene()`,
`export_cutscene()`, `from_cutscene(data)` and `import_cutscene(path)` do the same.

## What the player does today (version 0.2)

The player is a mod on the port's mod interface (1.14, with `pack_folder` and `game_language`; SDK
1.14.8 adds what a scene needs: `camera_fov`, `hud_hide`, `music_volume`, `moby_set_joints`,
`moby_joint_position`, `moby_sound_play`). It plays all of this format: the triggers, `once` (per
save, in the mod's data folder), variants, the hero held, hidden or an actor, camera keys (linear,
smooth, bezier, cuts, field of view, shake, the way back to the game's camera), actors of all four
sources with `track`, `path`, `anims`, `visible`, `look_at`, `held` and `layers`, joints as
look-at targets, subtitles (the game's font for ASCII, pictures for the rest, the disc's language),
sounds with fades and game sounds on actors, the letterbox, fades, the hidden HUD, the held world,
the lowered music, and the events `moby_hide`, `moby_kill`, `mission_flag`, `haze`, `fade`, `play`
and `travel`. Start skips. The letterbox, fades and subtitle pictures need the port's overlay for
mods (`on_overlay`); holding the world needs the moby update gate, which one mod has at a time.

On a port without a call it needs, the player plays the rest and logs what it skipped, once a
cutscene.

Not played: a sound's place (the port's mod sounds are not placed in the world), and a `blend`
other than 4 ticks between animations (the game's own).

## PCSX2

The player is a mod of OpenRAC's port, which runs the game natively; PCSX2 runs the disc's own
code, which has no mod interface. Playing a cutscene there would need a player written for the
PlayStation 2 itself (MIPS code patched into the game, reading the file from memory), or a program
beside PCSX2 that writes the camera and the actors into the game's memory each frame over PCSX2's
PINE socket. The second is a rough preview at best: PINE is not in step with the game's frames, so
the camera would shake against the game's own, and what the port adds (figures, props, joints, the
overlay) has no counterpart. Neither is built; the editor's preview and the port are the way to
watch a cutscene.
