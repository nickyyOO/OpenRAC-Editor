"""Behaviours: simple movements given to a level's mobys, which the game's map player plays.

A moby under Game/Mobys with rc1_behaviour metadata (a preset's name) and its numbers is driven by
the map player built into OpenRAC (port/game/rac1/editor_runtime): its own update stops and it moves as
the preset says, from where it is placed. Pack writes them beside the level's placements:

    <out>/level_NN/behaviours.json   {"format": 1, "mobys": [{"spawn_id", "preset", <numbers>}]}

A moby of another level's class that has a move is not placed: the mod makes it, so its entry has
"class", "position", "rotation" (radians) and "scale" in place of "spawn_id", and the class comes
for its look and animations only (its code never runs). One with no move stands still.

The presets and their numbers (metadata rc1_behaviour_<name>; a missing one takes its default):

    spin       speed (degrees a second; below 0 the other way)
    bob        height (units), period (seconds for one up and down)
    patrol     distance (units out along the way it faces, then back), speed (units a second)
    orbit      radius (units), speed (degrees a second; above 0 to its left, below to its right)
    face_hero  speed (degrees a second it turns)
    follow     speed (units a second), distance (how near the hero it stops), range (how near the
               hero must be; 0: always)
    wander     radius (units round where it stands), speed (units a second), pause (seconds it
               stands at each place)

and for each, sequence: the class's animation it plays standing (-1: its own), and
walk_sequence: the one it plays while it moves (-1: the standing one). The mod clamps each number.
"""

import openrac_root  # noqa: F401,I001  (OpenRAC's extractor on the import path)
import json
from pathlib import Path

from pack import scene_mobys, sharing

# Each preset's numbers and their defaults (as the mod's presets.hpp has them).
PRESETS: dict[str, dict[str, float]] = {
    "spin": {"speed": 90.0},
    "bob": {"height": 1.0, "period": 2.0},
    "patrol": {"distance": 8.0, "speed": 4.0},
    "orbit": {"radius": 4.0, "speed": 45.0},
    "face_hero": {"speed": 180.0},
    "follow": {"speed": 4.0, "distance": 3.0, "range": 30.0},
    "wander": {"radius": 8.0, "speed": 3.0, "pause": 2.0},
}

# A moby the mod makes with no move given: a spin of no speed, its own standing animation.
STAND = {"preset": "spin", "speed": 0.0, "sequence": -1}


def text(value) -> str:
    return str(value).strip().strip('"')


def given(text_of_scene: str) -> list[dict]:
    """The scene's mobys with a behaviour."""
    return [node for node in scene_mobys(text_of_scene) if "behaviour" in node["meta"]]


def entry(node: dict) -> dict:
    """NODE's behaviours.json entry, without its spawn id; ValueError when it cannot be one."""
    preset = text(node["meta"]["behaviour"])
    if preset not in PRESETS:
        raise ValueError(f"no preset {preset!r} (there are {', '.join(PRESETS)})")
    out = {"preset": preset}
    for key, default in PRESETS[preset].items():
        value = node["meta"].get(f"behaviour_{key}", default)
        try:
            number = float(value)
        except (TypeError, ValueError):
            raise ValueError(f"rc1_behaviour_{key} must be a number, not {value!r}") from None
        if number != number or number in (float("inf"), float("-inf")):
            raise ValueError(f"rc1_behaviour_{key} must be a number, not {value!r}")
        out[key] = number
    for key in ("sequence", "walk_sequence"):
        value = node["meta"].get(f"behaviour_{key}", -1)
        try:
            out[key] = int(float(value))
        except (TypeError, ValueError):
            raise ValueError(f"rc1_behaviour_{key} must be a number, not {value!r}") from None
    if out["walk_sequence"] < 0:
        del out["walk_sequence"]    # none: the standing one all the time
    return out


def write_behaviours(text_of_scene: str, out: Path, spawn_ids: dict[str, int],
                     made: dict[str, dict] | None = None) -> tuple[list[str], list[str]]:
    """Writes the scene's behaviours into OUT (the packed level's folder): what was done and what not.
    MADE: the mobys the mod makes (another level's class brought for its look), by node name: their
    class, position, rotation and scale, written in place of a spawn id."""
    changes, problems, entries = [], [], []
    made = made or {}
    for node in given(text_of_scene):
        name = node["name"]
        if name in made:
            try:
                entries.append({**made[name], **entry(node)})
            except ValueError as error:
                problems.append(f"{name}: behaviour not used: {error}")
                continue
            changes.append(f"{name}: made by the mod, behaviour {entries[-1]['preset']}")
            continue
        spawn_id = spawn_ids.get(name, -1)
        if not 0 <= spawn_id < 0xFFFF:
            problems.append(f"{name}: no spawn id, so its behaviour is not used")
            continue
        try:
            entries.append({"spawn_id": spawn_id, **entry(node)})
        except ValueError as error:
            problems.append(f"{name}: behaviour not used: {error}")
            continue
        changes.append(f"{name}: behaviour {entries[-1]['preset']}" + sharing(name, spawn_id, spawn_ids, "behaviour"))
    for name in sorted(made.keys() - {node["name"] for node in given(text_of_scene)}):
        # No simple move: it stands where it is placed, playing its own animation (its code never runs).
        entries.append({**made[name], **STAND})
        changes.append(f"{name}: made by the mod, standing (give it a simple move to make it move)")
    if entries:
        out.mkdir(parents=True, exist_ok=True)
        (out / "behaviours.json").write_text(json.dumps({"format": 1, "mobys": entries}, indent=1) + "\n", encoding="utf-8", newline="\n")
    elif (out / "behaviours.json").exists():
        (out / "behaviours.json").unlink()
    return changes, problems
