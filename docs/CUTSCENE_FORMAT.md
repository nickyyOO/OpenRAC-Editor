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
| `hide_hud` | true | |
| `world` | `"run"` | `"run"`: the level goes on; `"hold"`: mobys that are not actors are held while it plays |
| `music` | `"duck"` | `"keep"`, `"duck"` (lower), `"stop"` |

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
vertical field of view in degrees; the game's own is kept when no key gives one.

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

`track` keys move it; `path` walks it along points on the ground at a speed instead (the editor
picks the walk or run sequence when `anims` gives none). `anims` start the game's sequences of the
actor's class, blending over `blend` ticks.

### Layers (advanced, version 1.1)

```jsonc
"layers": [
  { "name": "face", "joints": [12, 13, 14], "weight": [ { "t": 0, "w": 1 } ],
    "frames": "faces/vendor_face.bin" }   // joint rotations per tick, written by the editor
]
```

Expressions, lip sync and hand-set joint keys all become layers the editor bakes to one joint
rotation per tick for the listed joints; the player applies them as the game's joint modifiers.

## `subtitles`

```jsonc
{ "t": 30, "end": 150, "text": { "en": "Need a weapon?", "fr": "Besoin d'une arme ?" }, "actor": "vendor" }
```

Drawn in the game's font at the bottom, as the game's scenes do. A language the line lacks shows
the first language of `languages`.

## `sounds`

```jsonc
{ "t": 30, "kind": "voice", "file": { "en": "audio/vendor_01_en.wav" }, "actor": "vendor", "volume": 1 }
{ "t": 0,  "kind": "music", "file": "audio/theme.ogg", "fade_in": 60, "fade_out": 60 }
{ "t": 90, "kind": "sfx", "game_sound": 26, "actor": "hero" }
```

`file` is beside the .cutscene file (WAV or Ogg Vorbis). `kind` decides the volume group the player
uses. With `actor` the sound comes from that actor's place.

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
| CutsceneActor | `actors`: **Take Selected Moby** makes it a moby of the level; a `sequence` key starts an animation, `visible` keys show and hide it |
| CutsceneSubtitle | `subtitles` |
| CutsceneSound | `sounds` |
| CutsceneEvent | `events` |
| a Node3D called HeroEnd | `hero.end` |

Saving the level writes every Cutscene to `cutscenes/<name>.cutscene` beside the scene, and the
launcher packs them with the level. **Export .cutscene** writes one now; **Import .cutscene** reads
one back into a new, empty Cutscene (a `path`, `look_at` on actors, shake, bezier handles and
events the editor has no node for are not kept: it says which). From a script, `to_cutscene()`,
`export_cutscene()`, `from_cutscene(data)` and `import_cutscene(path)` do the same.

## What the player does today (version 0.1)

The player is a mod on the port's mod interface (1.14, with `pack_folder` and `game_language`). It
plays: the triggers, `once` (per save, in the mod's data folder), variants, the hero held or
hidden, camera keys (linear, smooth, bezier, cuts, shake, the way back to the game's camera),
actors of all four sources with `track`, `path`, `anims` and `visible`, subtitles (ASCII letters,
the disc's language, the first language as fallback), sounds with fades, the letterbox, and the
events `moby_hide`, `moby_kill`, `mission_flag`, `haze`, `fade`, `play` and `travel`. Start skips.
The letterbox and fades need the port's overlay for mods (`on_overlay`).

Not yet, for want of a call in the mod interface: `fov`, hiding the HUD,
holding the world, lowering the music, `look_at` and `held` on actors, joints as look-at targets,
`layers`, a game sound on an actor other than the hero, and Japanese subtitles (the HUD's text is
ASCII). The player logs each one it skips, once a cutscene.
