# Behaviour format

A moby's Inspector in the editor has a **Behaviour** section: a simple movement the moby makes in
the game instead of running its own logic. The level's pack writes them to
`level_NN/behaviours.json`, beside the level's placements, and the behaviour presets mod
(`mods/behaviour-presets`) plays them.

```json
{"format": 1, "mobys": [
  {"spawn_id": 181, "preset": "orbit", "radius": 6.5, "speed": 45, "sequence": -1}
]}
```

- `spawn_id`: the moby's spawn id, as the level placed it (0 to 65534).
- `preset`: one of the presets below. An entry with another one is left out, and its moby keeps
  its own logic.
- `sequence`: the class's animation it plays, from the start; -1 keeps the one it plays.
- The preset's numbers. A missing one takes its default; each is held to its range.

| Preset | Numbers (default) | What it does |
| --- | --- | --- |
| `spin` | `speed` (90) degrees a second | Turns in place; below 0 the other way. |
| `bob` | `height` (1) units, `period` (2) seconds | Goes up and down from where it is. |
| `patrol` | `distance` (8) units, `speed` (4) units a second | Goes out along the way it faces and back, facing the way it goes. |
| `orbit` | `radius` (4) units, `speed` (45) degrees a second | Goes round a centre beside it, starting where it stands: to its left for a positive speed, to its right for a negative one. |
| `face_hero` | `speed` (180) degrees a second | Turns towards the hero. |
| `follow` | `speed` (4) units a second, `distance` (3) units, `range` (30) units | Walks towards the hero, while he is within `range` (0: always), up to `distance` from him. |

The game's axes: z up, the yaw is a rotation about z, a yaw of 0 faces +x. Times are in the
game's ticks, 60 a second. A moby with a behaviour is driven by the mod (its own update does not
run) from the tick it is first seen in a level load; a new load (a death too) starts it again
from its place. It has no collision of its own: it goes through walls and floors. A moby that
another mod drives (a cutscene's actor) keeps that one.
