"""A level project as a new planet: an added ship destination (map_mod.py), packed, exported and
installed in one step, as the launcher's "Add as a new planet" runs it:

    openrac_editor.py planet ISO PROJECT OVERRIDES

PROJECT is one level's project (a project per level, --split). Its planet's name is in its
project.godot ([openrac] planet/name), set in the editor (the level's Properties, Planet Name).
The first export gives the planet an id (planet/id), kept in project.godot, so the next export
updates the same planet instead of adding another. The level is the template: its program and
classes run the new planet, and the original planet stays as it is.
"""

from __future__ import annotations

import openrac_root  # noqa: F401,I001  (OpenRAC's extractor on the import path)
import re
import secrets
import tempfile
from pathlib import Path

import overrides

# project.godot's section and keys (the editor's plugin registers the same: openrac_library/plugin.gd).
SECTION = "openrac"
NAME = "planet/name"
ID = "planet/id"

# The launcher's codes for why a planet was not added (its toolErrors.ts has a sentence for each).
UNNAMED = "planet-unnamed"
INVALID = "planet-invalid"
INCOMPLETE = "pack-incomplete"
NO_PROJECT = "no-project"


class PlanetError(overrides.PackError):
    """Why a planet was not added, with the launcher's code for it."""

    def __init__(self, message: str, code: str = INVALID):
        super().__init__(message)
        self.code = code


def code_of(exc: Exception) -> str:
    """The launcher's code for EXC: its own, else planet-invalid (map_mod.py and overrides.py say why)."""
    return getattr(exc, "code", None) or INVALID


# What the ship computer shows at most (map_mod.py and the port check the rest).
MOST_NAME = 40


def settings(project: Path) -> dict[str, str]:
    """The [openrac] planet/* and map/* settings of PROJECT's project.godot, as strings."""
    found: dict[str, str] = {}
    section = ""
    lines = iter((project / "project.godot").read_text(encoding="utf-8").splitlines())
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1]
        elif "=" in stripped and not stripped.startswith(";"):
            key, value = stripped.split("=", 1)
            # Godot writes a string's new lines as they are: its value goes on to its closing quote.
            while value.startswith('"') and not closed(value):
                value += "\n" + next(lines, '"')
            if section == SECTION and key.strip().startswith(("planet/", "map/")):
                found[key.strip()] = godot_string(value.strip())
    return found


def closed(value: str) -> bool:
    """Whether the quoted VALUE ends with its own closing quote, not an escaped one."""
    if len(value) < 2 or not value.endswith('"'):
        return False
    backslashes = len(value[1:-1]) - len(value[1:-1].rstrip("\\"))
    return backslashes % 2 == 0


def godot_string(value: str) -> str:
    """A project.godot string value ("text", with \\" and \\\\ escaped) as text; anything else as written."""
    if len(value) >= 2 and value[0] == value[-1] == '"':
        return re.sub(r'\\(.)', lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), value[1:-1])
    return value


def quoted(text: str) -> str:
    """TEXT as a project.godot string, one line."""
    escaped = text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")
    return '"' + escaped + '"'


def set_setting(project: Path, key: str, value: str) -> None:
    """Sets [openrac] KEY to the string VALUE in PROJECT's project.godot, the rest kept as it is."""
    path = project / "project.godot"
    lines = path.read_text(encoding="utf-8").splitlines()
    line = f"{key}={quoted(value)}"
    start = next((i for i, text in enumerate(lines) if text.strip() == f"[{SECTION}]"), None)
    if start is None:
        lines += ["", f"[{SECTION}]", "", line]
    else:
        end = next((i for i in range(start + 1, len(lines)) if lines[i].strip().startswith("[")), len(lines))
        at = next((i for i in range(start + 1, end) if lines[i].split("=", 1)[0].strip() == key), None)
        if at is not None:
            # A value Godot wrote over several lines goes with its first.
            value_text = lines[at].split("=", 1)[1].strip()
            while value_text.startswith('"') and not closed(value_text) and at + 1 < len(lines):
                value_text += "\n" + lines.pop(at + 1)
            lines[at] = line
        else:
            # After the section's last setting, before the blank lines that end it.
            last = max((i for i in range(start + 1, end) if lines[i].strip()), default=start)
            lines.insert(last + 1, line)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def carry_settings(old: Path, new: Path) -> None:
    """Copies the planet/* settings of the project OLD (a backup) into the fresh project NEW, so a
    level exported again stays the same planet."""
    if not (old / "project.godot").is_file() or not (new / "project.godot").is_file():
        return
    for key, value in settings(old).items():
        set_setting(new, key, value)


def slug(name: str) -> str:
    """NAME in lower case letters, digits and dashes (My World! -> my-world), or "planet"."""
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")[:40].strip("-") or "planet"


def planet_name(project: Path) -> str:
    """The planet's name, checked: what the ship computer shows."""
    name = settings(project).get(NAME, "").strip()
    if not name:
        raise PlanetError("Name your planet first: in the editor, click the level (the top object in the "
                          "Level list) and type its Planet Name in Properties", UNNAMED)
    if len(name) > MOST_NAME or any(ord(c) < 32 for c in name):
        raise PlanetError(f"The planet's name must be one line of at most {MOST_NAME} characters", UNNAMED)
    # The ship computer draws the name's bytes with the game's font, which has plain ASCII only: a
    # letter such as "ł" or "é" would show as other glyphs.
    odd = sorted({c for c in name if not 32 <= ord(c) < 127})
    if odd:
        raise PlanetError(f"The planet's name can use plain letters A-Z, digits, spaces and punctuation only "
                          f"(the game's font has no {' '.join(odd)})", UNNAMED)
    return name


def planet_id(project: Path, name: str) -> str:
    """The planet's id: the one in project.godot, else a new one written there (its name and a random
    part, so two people's planets of the same name stay two planets)."""
    known = settings(project).get(ID, "").strip()
    if known:
        return known
    made = f"{slug(name)}.{secrets.token_hex(4)}"
    set_setting(project, ID, made)
    return made


def level_of(project: Path) -> int:
    """The one level PROJECT holds (levels/level_NN with its scene)."""
    found = sorted(int(scene.stem[6:]) for scene in project.glob("levels/level_*/level_*.tscn")
                   if re.fullmatch(r"level_\d\d", scene.stem) and scene.parent.name == scene.stem)
    if len(found) != 1:
        raise PlanetError(f"{project} must be one level's project (it holds {len(found)} levels): "
                          "pick the level in the launcher", NO_PROJECT)
    return found[0]


def map_note(project: Path) -> tuple[str, str]:
    """The map's author and its line about it, from the project's settings (map/author, map/about)."""
    found = settings(project)
    return found.get("map/author", "").strip(), found.get("map/about", "").strip()


def make_planet(disc, survey: dict, project: Path, out: Path, keep_problems: bool = False) -> dict:
    """Packs PROJECT's level and exports it as a new planet's pack into the folder OUT: what was
    done, for the report. A level with problems a player would see (map_check.py) is refused,
    unless KEEP_PROBLEMS."""
    from map_mod import export_map
    from pack import pack
    if not (project / "project.godot").is_file():
        raise PlanetError(f"{project} is not a level project (open the level in the editor first)", NO_PROJECT)
    name = planet_name(project)
    level = level_of(project)
    ident = planet_id(project, name)
    with tempfile.TemporaryDirectory(prefix=".openrac-planet-") as temp:
        packed = Path(temp) / "packed"
        packed.mkdir()
        pack(disc, survey, project, packed, {})
        report = (packed / "changes.txt").read_text(encoding="utf-8")
        refused = [line.strip() for line in report.splitlines() if line.lstrip().startswith("not packed:")]
        if refused:
            raise PlanetError("Some edits could not be packed:\n" + "\n".join(f"  {line}" for line in refused), INCOMPLETE)
        try:
            import map_check
        except ImportError:                       # no numpy: not checked (the release brings it)
            keep_problems = True
        if not keep_problems:
            lines = map_check.packed_problems(project, report)
            if lines:
                raise PlanetError("The map has problems a player would see:\n"
                                  + "\n".join(f"  {line}" for line in lines), INCOMPLETE)
        author, about = map_note(project)
        manifest = export_map(packed, project, out, name, level, destination=True, map_id=ident,
                              author=author, description=about)
    return {"name": name, "id": ident, "level": level, "report": report, "author": author, "about": about,
            "spawn": manifest["map"]["spawn"]}


def add_planet(disc, survey: dict, project: Path, into: Path, keep_problems: bool = False) -> dict:
    """Makes PROJECT's planet (make_planet) and installs it in the launcher's packs folder INTO:
    what was done, for the report."""
    with tempfile.TemporaryDirectory(prefix=".openrac-planet-") as temp:
        made = make_planet(disc, survey, project, Path(temp) / "planet", keep_problems)
        installed, _, _ = overrides.add(Path(temp) / "planet", into)
    return {**made, "installed": installed}
