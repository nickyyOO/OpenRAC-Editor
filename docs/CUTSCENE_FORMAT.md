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

A `once` cutscene counts as seen when it ends or is skipped, so a game closed in the middle of one
plays it again. The player's **Play cutscenes** setting off skips each one as it starts (what it leaves
in the world still happens, as with a skip); in a party the host's setting counts for every game,
and each game plays the party's cutscenes on its own triggers.

The player starts a cutscene only when the game would start one of its own: the player in
control, on foot, alive, with no menu and no game scene playing (on a port that says so, also
standing steady: not hurt, not on a ride, a lift or in water). Until then it waits, and a trigger
that fired in the meantime still plays. Two that fire together play one after the other, in the
order of their file names. A chain of `play` events and `after` triggers does not play a cutscene
twice: a loop stops there.

## `options`

| Key | Default | Meaning |
|---|---|---|
| `skippable` | true | Start skips it. On a skip the player applies the end state of every `persistent` event (and every `moby_kill` and `mission_flag`, which cannot be undone) and puts actors where the cutscene leaves them |
| `letterbox` | true | Black bars, as the game's scenes |
| `hide_hud` | true | The game's HUD is hidden while it plays (subtitles stay) |
| `world` | `"run"` | `"run"`: the level goes on; `"hold"`: mobys that are not actors (nor the hero) do not update while it plays |
| `music` | `"duck"` | `"keep"`, `"duck"` (a quarter of its volume), `"stop"` (silent); eased over half a second, and back on the way out |

## `hero`

| Key | Meaning |
|---|---|
| `mode` | `"held"` (he stays, controls locked, `hero_grab`), `"hidden"`, or `"actor"` (a figure in his look is an actor with id `"hero"`) |
| `seq` | The sequence he plays while held, 0 (standing) if not given (version 1.4) |
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

An `eye` can be on an actor too: `{ "actor": "ship", "joint": 4, "offset": [-2, 0, 1], "turn": true }`
puts the camera where that actor (or its joint, or the hero) is, plus `offset`; with `turn` the offset
is in the actor's own axes (x ahead) and the key's `rotation` turns with the actor, so the camera
rides on it. A `look_at` point takes `turn` the same way. The editor writes such keys for a
CutsceneCamera whose **Ride On** is an actor.

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
actor's class, blending over `blend` ticks. On a prop (a skinned `.glb`) `seq` is the file's
animation (its index, 0 up), played from the entry's `t` at `speed` (and `frame`, a thirtieth of a
second, into it), looped unless `"loop": false`; the mesh's joints are its skin's, numbered by their
`joint_NNN` names or else their order (version 1.4).

`"feet": { "legs": [[92, 93, 94, 1], [98, 99, 100, 1]] }` keeps the actor's feet on the level's ground
(version 1.4): each leg's hip, knee and ankle, then the hip's parent joint (0, the root, if left
out; the player turns the hip in its parent's axes, as the game's joint modifiers do). Each ankle goes up or down by how far the ground under
it is from the height the actor stands at (half a unit at most, eased in), the body lowers for the
lower foot, the legs bend to reach and each foot keeps its tilt. On steps and slopes, a scene made
on flat ground then stands on them.

`look_at` turns the actor's body (its yaw) towards a `target` (a point, or an actor or the hero,
with `joint` and `offset` as the camera's), by `weight` (0 to 1, 1 if not given), easing to it
over `blend` ticks (10 if not given) from the key before. A key with `"target": null` eases the
turn out. A new target turns the body round at most 0.2 radians a tick.

`ride` keys put the actor on another: `[{ "t": 30, "actor": "ship" }, { "t": 90 }]` from tick 30 it
keeps its own place and turn as they were against `ship` (an actor, the hero, or with `joint` a joint
of one) when the key began, so it goes and turns with it; a key with no `actor` gets off. A rider moves
after every carrier each tick. The editor writes it from an actor's **Ride On**, **Ride Start** and
**Ride End**.

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
between them, 1 if none) blend each rotation in from none. In the editor a layer is a
**CutsceneLayer** under the actor: `joints` is its mask (**Use Part** takes a part of the actor's
rig, such as `head` or `left_arm`), each joint a keyable `joint_<n>` turn in degrees, and `weight`
keyable too. The export bakes them to `layers/<cutscene>_<actor>_<layer>.bin`, one turn per tick up
to the last key; an import turns such a file back into a layer with a key every other tick (the
game's 30 Hz). A layer whose frames are not beside the file is kept as written (the actor's
`advanced`).

A layer's **Retarget** group bakes an animation made elsewhere (Blender, mocap) onto it: **Retarget
Source** is a scene with a Skeleton3D and an AnimationPlayer (a `.glb`), **Retarget Animation** its
animation (the first if empty), **Retarget Map** which bone turns which joint (`{"UpperArm.L": 52}`,
or `{"joint": 52, "axes": Basis}` when the bone's axes are not the joint's: its x, y and z as the
joint's), **Retarget Start** where it starts on the timeline. **Retarget** keys each mapped joint
every 30 Hz frame with the bone's turn from its rest, adds the joints to the mask and says what it
could not do (a bone not in the skeleton or not moved). Only turns go across, not the bones'
lengths or the body's travel. From a script, `retarget(animation, skeleton, map, start)` does the
same, and `copy_keys_from(layer, offset)` copies another layer's mask, weight and keys onto this one,
later by `offset` seconds: a move made once, played by another actor of the same skeleton. The
editor warns on a prop actor whose `.glb` moves its face by blend shapes (the game has none: a face
is turned on bones, as the hero's rig does) or has no skeleton.

### Pose (version 1.5)

```jsonc
"pose": { "frames": "poses/game_scene_04_0.bin", "joints": 111 }
```

The actor's whole animation, every joint per 30 Hz frame, as the game's own scenes have it
(OpenRAC's `editor/game_scenes.py` writes one for each actor of a scene it imports). `frames` is a
file beside the .cutscene file: for each frame from 0 (frame f is tick 2f), for each of `joints`
joints, 20 bytes, little-endian: the joint's turn from its parent as int16 x, y, z, w over 32767
(its columns are the joint's axes in its parent's, as glTF has it), its place in its parent as
three int16 in the model's units times 1024, and its scale as three uint16 over 4096. Between two
frames the turn and place blend; before the first and after the last, those hold.

The pose replaces the actor's animation: the root joint carries its turn, so `track` and `path`
move it but do not turn it, and `anims`, `look_at`, `layers`, `face` and `feet` do nothing on it. In
the editor the actor's model shows the pose; the player sets every joint's turn and place in place
of the class's animation (the port's `moby_set_joint_poses`; the first 128 joints, the most the port
takes). The scale is shown in the editor only.

### Face and gaits (version 1.2)

```jsonc
"face": {
  "rig": {
    "jaw":  { "joint": 11, "axis": [1, 0, 0], "open": 20 },            // degrees wide open
    "lids": { "joints": [26, 27], "axis": [1, 0, 0], "close": 35 },    // degrees shut
    "head": { "joint": 8, "axis": [0, 0, 1], "limit": 60 },            // degrees it turns at most
    "moods": { "happy": [ { "joint": 12, "rotation": [0, 0, 0.13, 0.99] } ] }
  },
  "talk":  [ { "t": 30, "envelope": { "en": "AAA0f/+A..." } } ],       // written by the editor
  "blink": true,
  "moods": [ { "t": 0, "mood": "happy", "weight": 1, "blend": 10 } ]
}
"path": { "points": [...], "speed": 3, "t": 300,
          "gaits": [ { "seq": 0, "speed": 0 }, { "seq": 2, "speed": 2 }, { "seq": 4, "speed": 6 } ] }
```

The `rig` says which of the actor's joints are its jaw, eyelids and head, and the joint turns of its
moods (quaternions, x y z w, on top of its animation). The editor fills it from the rigs it knows
for the actor's class (`modules/openrac_cutscene/cutscene_rigs.h`, and a project's
`res://cutscene_rigs.json`, an object by `"<game>/<class>"` or `"<game>/hero"`), and it can be set
by hand on the actor. A known rig may also list `parts` (a name and its first and last joint, such
as `"head": [8, 51]`) for layers' masks, and `feet` (each leg's hip, knee, ankle and the hip's parent) for an actor's
**Feet On Ground**: the editor keeps them, and writes `feet` as the actor's own (the hero's legs for a
hero figure when its rig has none).

- `talk`: the jaw opens by `open` degrees about `axis` as loud as the line: `envelope` is a byte a
  thirtieth of a second (0 shut, 255 wide), in base64, by language like the line's files (one string
  when the line has one file). The editor writes it on export from each voice line's WAV on an actor
  whose rig has a jaw.
- `blink` (true if not given, when the rig has `lids`): the lids shut by `close` degrees every two
  to six seconds, the same way each time the cutscene plays.
- `moods` keys fade a mood's pose in over `blend` ticks (10 if not given) while the one before
  fades out; a key with no `mood` fades back to the face's own.
- `head`: `look_at` turns the head first, up to `limit` degrees (60 if not given), and the body
  only for the rest.

On a walk, `gaits` pick the actor's sequence by the walk's speed: the gait with the highest `speed`
not above it while walking, the slowest once it has arrived. An `anims` entry that starts after the
walk did plays instead. The editor writes the rig's `gaits` on the walk.

## `subtitles`

```jsonc
{ "t": 30, "end": 150, "text": { "en": "Need a weapon?", "fr": "Besoin d'une arme ?" }, "actor": "vendor" }
```

Drawn in the game's font at the bottom, as the game's scenes do. A language the line lacks shows
the first language of `languages`.

The game's font writes only ASCII letters (and not `& * = [ ] { } ~`). For a line it cannot write
(Japanese, French, German, Spanish or Italian with an accent), the editor adds a picture of it, drawn with a system font, which
the player draws over the picture instead. A line with no picture is written in ASCII, an accented
letter as the letter without its accent:

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
{ "t": 0,   "do": "air", "fog": { "colour": [0.6, 0.7, 0.8], "near": 10, "far": 80, "thickness": 0.8 },
  "background": [0.2, 0.3, 0.5], "length": 60 }                  // version 1.3
{ "t": 400, "do": "air", "length": 60 }                        // back to the game's
{ "t": 100, "do": "slow", "stretch": 3, "length": 15 }         // a third of the speed
{ "t": 160, "do": "slow", "stretch": 1, "length": 15 }
{ "t": 0,   "do": "tie_hide", "tie": 42, "on": true }
```

`persistent` events are the ones a skip must still apply. A `moby_hide` or `tie_hide` that is
not `persistent` lasts only for the scene: what it hid shows again, and what it showed hides
again, when the scene ends, so a skip leaves the level as watching does. While the scene plays, a
`moby_hide` holds: the player sets it again each tick, so a class whose own code hides or shows
the moby (an amoeboid until Ratchet comes near) does not undo it. `spawn_id` is the record's uid
(the editor's `rc1_spawn_id`), not its place in the level; the player maps one to the other with
the port's `moby_uid` (SDK 1.14.9; an older port takes it as the place). A `moby_kill` and a
`mission_flag` always last.

A `fade` goes from the colour before it (none at first) to `to`, `[r, g, b]` or `[r, g, b, a]`
from 0 to 1, over `length` ticks (30 if not given, 0 at once). `to` as `null` or with `a` 0 clears
it. What a fade leaves goes with the way back to the game's camera.

`air` (version 1.3) eases over `length` ticks (30 if not given) to its `fog` (`colour` 0 to 1,
`near` and `far` in game units, `thickness` 0 to 1, 1 if not given) and `background` (`[r, g, b]`
0 to 1, the colour behind the level). A key with only a background clears the scene's fog; one with
neither goes back to the game's. It changes the picture only, and eases out with the camera.

`slow` (version 1.3) eases over `length` ticks (0 if not given) to `stretch`, how many times longer
a tick lasts (1 the game's speed, up to 8). The game runs the same ticks, only slower, so it plays
out as it would at full speed. The scene's timeline is in ticks too, so it slows with it.

`tie_hide` (version 1.3) hides (`on` true, the default) or shows a tie, by its index in the level's
tie list, in the picture only. One that is not `persistent` shows again when the scene ends.

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
| CutsceneEvent | `events` (also fog and background, slow motion, hiding ties) |
| a Node3D called HeroEnd | `hero.end` |

**Game View** in the 3D view's menu draws the game's picture over a view that looks through a
CutsceneCamera (the camera's **Preview**): the frame at 4:3 or 16:9 (the field of view is vertical,
so a wide picture shows more at the sides), the letterbox, the title-safe area and the subtitles'
band, and the subtitles at the timeline's place, wrapped as the player wraps them. A new
CutsceneCamera starts at the game's field of view (51 degrees).

The **Cutscene** dock (beside Animation, at the bottom) shows the selected node's cutscene at a
glance: a row for the camera and its shakes, each actor and its walk, moods and layers, the
subtitles, the sounds and the events, with their keys and spans on one scale. A click goes to that
time (on a tick) or picks the node; a drag moves what has a start; Play runs the timeline in the 3D
view. Keys themselves are set as in any Godot animation, with the key icons in the inspector and
the Animation dock.

Saving the level writes every Cutscene to `cutscenes/<name>.cutscene` beside the scene, and the
launcher packs them with the level. Packing checks each one first (OpenRAC's `editor/pack.py`, `check_cutscene`) and
leaves out one that is for another level, has no length or one over ten minutes, has two actors of
one name, spawns a moby class the level does not load, or asks for more than the port has (8
figures, 256 meshes, 128 joints an actor, 64 MiB of sounds). What the player would only skip is
reported and packed: a file not beside it, a moby not in the level, an actor that is not one. **Export .cutscene** writes one now; **Import .cutscene** reads
one back into a new, empty Cutscene (a camera looking at an actor becomes its turn, bezier handles
a smooth curve, and events the editor has no node for are not kept: it says which). From a script, `to_cutscene()`,
`export_cutscene()`, `from_cutscene(data)` and `import_cutscene(path)` do the same.

The game's own in-engine scenes can be a start: OpenRAC's `editor/game_scenes.py DISC.iso --level 3
--out DIR` writes each of a level's scenes as `game_scene_KK.cutscene`, with its speech as WAV per
language. It keeps the camera (a key a tick, its cuts and field of view), each actor's class, place
and size, the subtitles and the speech; the first hero actor becomes a figure and the hero is hidden.
Not kept: the actors' own joint animation, so they stand in their class's first animation.

## What the player does today (version 0.3)

The player is a mod on the port's mod interface (1.14, with `pack_folder` and `game_language`; SDK
1.14.8 adds what a scene needs: `camera_fov`, `hud_hide`, `music_volume`, `moby_set_joints`,
`moby_joint_position`, `moby_sound_play`, `scene_air`, `time_stretch`, `tie_hide`, `prop_animate`,
`moby_joint_frame`, `moby_set_joint_poses`). It plays all of this format: the triggers, `once` (per
save, in the mod's data folder), variants, the hero held, hidden or an actor, camera keys (linear,
smooth, bezier, cuts, field of view, shake, the way back to the game's camera), actors of all four
sources with `track`, `path` (with `gaits`), `anims`, `visible`, `look_at`, `held`, `layers`, `face`
(talk, blink, moods, the head's share of a look), `ride`, `feet` and `pose` (without its scale), animated props, cameras riding on actors, joints as
look-at targets, subtitles (the game's font for ASCII, pictures for the rest, the disc's language),
sounds with fades and game sounds on actors, the letterbox, fades, the hidden HUD, the held world,
the lowered music, and the events `moby_hide`, `moby_kill`, `mission_flag`, `haze`, `fade`, `play`,
`travel`, `air`, `slow` and `tie_hide` (these three need `scene_air`, `time_stretch` and `tie_hide`). Start skips. The letterbox, fades and subtitle pictures need the port's overlay for
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
