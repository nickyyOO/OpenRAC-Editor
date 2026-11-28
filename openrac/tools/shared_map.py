# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the OpenRAC contributors
"""Share a map made in OpenRAC Editor as one file that players add in OpenRAC and play:

    python3 openrac/tools/openrac_editor.py map export PROJECT OUT.openrac-map [--levels LEVELS]

The map (.openrac-map, version 2: OpenRAC's docs/port/SHARED_MAPS.md) is the planet's pack, what
Add as Planet installs, with every byte of the game named by the player's own files instead of
carried. OpenRAC's launcher makes the pack again from the player's disc and extracted levels in a
few seconds (launcher/core/src/maps.rs); no editor is needed.

Each file of the pack is parts, in order: the map's own bytes, ranges of the player's files (an
extracted level's file, disc sectors, or a WAD stream on the disc unpacked), WAD streams packed
again, and a disc model placed in the level (its positions and normals moved, its light slots and
indices as the editor's bake writes them). What no part of the player's files gives is the maker's
own: their terrain, collision, textures and placements.
"""

from __future__ import annotations
import openrac_root  # noqa: F401,I001  (OpenRAC's extractor on the import path)

import argparse
import hashlib
import json
import struct
import sys
import tempfile
import time
import zipfile
from pathlib import Path

from formats import FormatError, wad
from wadpack import compress

import planet

FORMAT = "openrac-map"
VERSION = 2
SUFFIX = ".openrac-map"
MANIFEST = "map.json"
AUTHOR = "map/author"  # project.godot [openrac], the editor's Made By field
BLOCK = 32          # bytes compared at once when looking for the player's bytes in a file
LEAST_RUN = 64      # a run of the player's bytes shorter than this stays the map's own


class MapError(ValueError):
    pass


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def matrix_hex(m) -> str:
    return "".join(struct.pack(">d", float(v)).hex() for v in m)


class Sources:
    """The player's files a map names, each once."""

    def __init__(self):
        self.list: list[dict] = []
        self.data: list[bytes] = []
        self.by_key: dict[str, int] = {}

    def add(self, spec: dict, data: bytes) -> int:
        key = json.dumps(spec, sort_keys=True)
        if key not in self.by_key:
            self.by_key[key] = len(self.list)
            self.list.append({**spec, "sha256": sha(data)})
            self.data.append(data)
        return self.by_key[key]


def diff(target: bytes, bases: list[tuple[int, bytes]], own: bytearray) -> list[list]:
    """TARGET as parts: runs found in BASES (source index, bytes) are copies of them; the rest is
    added to OWN (the file's own bytes) and named there."""
    if not bases or not target:
        return [["data", *_own(own, target)]] if target else []
    joined = b"".join(b for _, b in bases)
    starts, at = [], 0
    for _, b in bases:
        starts.append(at)
        at += len(b)
    index: dict[bytes, int] = {}
    for at in range(0, len(joined) - BLOCK + 1, BLOCK):
        index.setdefault(joined[at:at + BLOCK], at)
    parts: list[list] = []
    pending = bytearray()
    p, last_end = 0, -1

    def flush():
        if pending:
            parts.append(["data", *_own(own, bytes(pending))])
            pending.clear()

    def copy(start: int, length: int):
        # A run may cross from one base into the next: one copy for each.
        while length:
            which = max(i for i, s in enumerate(starts) if s <= start)
            source, data = bases[which]
            off = start - starts[which]
            take = min(length, len(data) - off)
            if parts and parts[-1][0] == "copy" and parts[-1][1] == source and parts[-1][2] + parts[-1][3] == off:
                parts[-1][3] += take
            else:
                parts.append(["copy", source, off, take])
            start += take
            length -= take

    n = len(target)
    while p < n:
        s = None
        if 0 <= last_end and last_end + BLOCK <= len(joined) and joined[last_end:last_end + BLOCK] == target[p:p + BLOCK]:
            s = last_end
        elif p + BLOCK <= n:
            s = index.get(target[p:p + BLOCK])
        if s is None:
            # Nothing of the player's starts here: the next BLOCK bytes are looked up one by one.
            pending.append(target[p])
            p += 1
            continue
        length = BLOCK
        most = min(len(joined) - s, n - p)
        while length < most and joined[s + length] == target[p + length]:
            step = min(4096, most - length)
            if joined[s + length:s + length + step] == target[p + length:p + length + step]:
                length += step
                continue
            while length < most and joined[s + length] == target[p + length]:
                length += 1
            break
        back = 0
        while back < len(pending) and s - back > 0 and joined[s - back - 1] == pending[-1 - back]:
            back += 1
        if length + back < LEAST_RUN:
            pending.extend(target[p:p + length])
            p += length
            last_end = s + length
            continue
        if back:
            del pending[len(pending) - back:]
        flush()
        copy(s - back, length + back)
        p += length
        last_end = s + length
    flush()
    return parts


def _own(own: bytearray, data: bytes) -> tuple[int, int]:
    at = len(own)
    own.extend(data)
    return at, len(data)


def wad_spans(data: bytes) -> list[tuple[int, int, bytes]]:
    """(start, end, unpacked) of each WAD stream in DATA that packs again to the same bytes."""
    found, i = [], data.find(b"WAD")
    while i >= 0:
        try:
            end = i + struct.unpack_from("<I", data, i + 3)[0]
            unpacked = wad(data[i:end]) if end <= len(data) else None
        except (FormatError, struct.error):
            unpacked = None
        if unpacked is not None and compress(unpacked, data[i:i + 16]) == data[i:end]:
            found.append((i, end, unpacked))
            i = data.find(b"WAD", end)
        else:
            i = data.find(b"WAD", i + 1)
    return found


# =============================================================================
# DISC MODELS PLACED IN THE LEVEL (static imports baked into terrain.glb)
# =============================================================================

def _glb(data: bytes) -> tuple[dict, int]:
    """A GLB's JSON and where its binary chunk's bytes start in the file."""
    size, = struct.unpack_from("<I", data, 12)
    doc = json.loads(data[20:20 + size])
    return doc, 20 + size + 8


def _accessor_span(doc: dict, bin_at: int, index: int) -> tuple[int, int, int, int]:
    """(file offset, count, components, component bytes) of a tightly packed accessor."""
    a = doc["accessors"][index]
    view = doc["bufferViews"][a["bufferView"]]
    width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
    size = {5126: 4, 5125: 4, 5123: 2, 5121: 1}[a["componentType"]]
    if view.get("byteStride", width * size) != width * size:
        raise MapError("a disc model's vertices are interleaved")
    return bin_at + view.get("byteOffset", 0) + a.get("byteOffset", 0), a["count"], width, size


def static_parts(project: Path, levels: Path, sources: Sources) -> list[tuple[bytes, list]]:
    """For each disc model placed in PROJECT's level (a static import from a level of the disc):
    (the bytes the bake writes, the part that makes them from the player's model), for each of
    its positions, normals, light slots and indices."""
    import gltf_read
    import static_assets
    found = []
    for scene in sorted(project.glob("levels/level_*/level_*.tscn")):
        text = scene.read_text(encoding="utf-8")
        for name, source, matrix, solid in static_assets.placements(project, text):
            about = source.parent / "source.json"
            info = json.loads(about.read_text(encoding="utf-8")) if about.is_file() else {}
            if not info.get("source_level"):
                continue                       # the maker's own model: theirs to share
            if solid:
                raise MapError(f"{name}: a model from the disc with collision on cannot be shared in a map "
                               "yet: turn its collision off")
            rel = f"{info['source_level']}/{info['kind']}/{info['source_model']}"
            port = levels / rel
            if not port.is_file():
                raise MapError(f"{name}: your extracted levels have no {rel}: extract the levels again")
            port_bytes = port.read_bytes()
            src = sources.add({"port": rel}, port_bytes)
            port_doc, port_bin = _glb(port_bytes)
            document = static_assets.read_model(source, project)
            # The port model's accessors by their bytes: the editor's copy holds the same vertices.
            by_bytes = {}
            for i in range(len(port_doc["accessors"])):
                try:
                    at, count, width, size = _accessor_span(port_doc, port_bin, i)
                except (KeyError, MapError):
                    continue
                by_bytes.setdefault(port_bytes[at:at + count * width * size], (at, count, width, size))
            # triangles() gives each node's primitives in this order: the same walk names their world.
            walk = []
            for index, node in enumerate(document.list("nodes")):
                if "mesh" not in node:
                    continue
                world = gltf_read.multiply(matrix, document.node_world(index))
                flip = 1 if static_assets.determinant(world) < 0 else 0
                for primitive in document.list("meshes")[node["mesh"]]["primitives"]:
                    walk.append((world, gltf_read.invert(world), flip, primitive))
            for (world, inverse, flip, primitive), (attrs, order, _) in zip(walk, static_assets.triangles(document, matrix)):
                raw = {k: _raw(document, v) for k, v in primitive["attributes"].items()}
                for key, kind, m in (("POSITION", "point", world), ("NORMAL", "normal", inverse)):
                    if key in attrs and raw.get(key) in by_bytes:
                        at, count, _, _ = by_bytes[raw[key]]
                        made = struct.pack(f"<{count * 3}f", *(x for v in attrs[key] for x in v))
                        found.append((made, [kind, src, at, count, matrix_hex(m)]))
                if raw.get("_LIGHT_SLOT") in by_bytes and "TEXCOORD_1" in attrs:
                    at, count, _, _ = by_bytes[raw["_LIGHT_SLOT"]]
                    made = struct.pack(f"<{count * 2}f", *(x for v in attrs["TEXCOORD_1"] for x in v))
                    found.append((made, ["slot", src, at, count]))
                if "indices" in primitive and _raw(document, primitive["indices"]) in by_bytes:
                    at, count, _, size = by_bytes[_raw(document, primitive["indices"])]
                    found.append((struct.pack(f"<{len(order)}I", *order), ["index", src, at, count, size, flip]))
    return found


def _raw(document, index: int) -> bytes:
    a = document.list("accessors")[index]
    data, _ = document.view_bytes(a["bufferView"])
    width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
    size = {5126: 4, 5125: 4, 5123: 2, 5121: 1}[a["componentType"]]
    at = a.get("byteOffset", 0)
    return bytes(data[at:at + a["count"] * width * size])


# =============================================================================
# THE MAP
# =============================================================================

def make(planet: Path, out: Path, about: dict, disc, survey: dict, levels: Path, project: Path) -> dict:
    """Writes OUT, the map of the planet pack PLANET (Add as Planet's), whose game bytes come from
    DISC (an extractor Disc) and the extracted LEVELS. ABOUT: id, name, author, about, credits, made,
    disc. What went in, for the report."""
    sources = Sources()
    port: dict[int, list[Path]] = {}
    for path in levels.rglob("*"):
        if path.is_file():
            port.setdefault(path.stat().st_size, []).append(path)

    def same_in_levels(data: bytes) -> str | None:
        for path in port.get(len(data), []):
            if path.read_bytes() == data:
                return path.relative_to(levels).as_posix()
        return None

    # The disc sectors each packed file stands in for: patches.tsv (first, count, file) and
    # virtual.tsv (first, count, file, level, ..., the disc's first, the disc's count).
    replaces: dict[str, tuple[int, int]] = {}
    for name, columns in (("patches.tsv", (0, 1)), ("virtual.tsv", (5, 6))):
        table = planet / name
        if not table.is_file():
            continue
        for line in table.read_text(encoding="utf-8").splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            cells = line.split("\t")
            replaces[cells[2]] = (int(cells[columns[0]]), int(cells[columns[1]]))
    donors = _donor_cores(disc, survey, project, sources)
    statics = static_parts(project, levels, sources)
    files, blobs, report = [], {}, {"files": 0, "own_bytes": 0}
    for path in sorted(p for p in planet.rglob("*") if p.is_file()):
        rel = path.relative_to(planet).as_posix()
        data = path.read_bytes()
        own = bytearray()
        named = same_in_levels(data) if len(data) else None
        if named is not None:
            parts = [["copy", sources.add({"port": named}, data), 0, len(data)]]
        else:
            parts = _file_parts(rel, data, replaces, disc, sources, levels, donors, statics, own)
        i = len(files)
        files.append({"path": rel, "sha256": sha(data), "size": len(data), "parts": parts})
        if own:
            blobs[f"data/{i}.bin"] = bytes(own)
        report["files"] += 1
        report["own_bytes"] += len(own)
    manifest = {"format": FORMAT, "version": VERSION, **about, "game": "rac1",
                "sources": sources.list, "files": files}
    partial = out.with_name(out.name + ".part")
    with zipfile.ZipFile(partial, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(MANIFEST, json.dumps(manifest, separators=(",", ":")) + "\n")
        for blob, data in blobs.items():
            archive.writestr(blob, data)
    partial.replace(out)
    return report


def _donor_cores(disc, survey: dict, project: Path, sources: Sources) -> list[tuple[int, bytes]]:
    """The unpacked cores of the disc's levels the project brings things from (imported classes),
    which a packed core may copy."""
    import re
    wanted = set()
    for scene in project.glob("levels/level_*/level_*.tscn"):
        wanted |= {int(n) for n in re.findall(r"res://levels/level_(\d\d)/", scene.read_text(encoding="utf-8"))}
    here = {int(p.name[6:]) for p in project.glob("levels/level_*") if p.name[6:].isdigit()}
    out = []
    for info in survey["levels"]:
        if info["id"] not in wanted - here:
            continue
        place = info["ranges"]["data"]
        data = disc.sectors(place["lba"], place["sectors"])
        core_at, = struct.unpack_from("<i", data, 10 * 8)
        outer = sources.add({"disc": [place["lba"], place["sectors"]]}, data)
        core = wad(data[core_at:])
        out.append((sources.add({"wad": outer, "at": core_at}, core), core))
    return out


def _table(data: bytes) -> list[int]:
    """The first 24 words of a level's file: (offset, size) of each of its parts."""
    return list(struct.unpack_from("<24i", data)) if len(data) >= 96 else []


def _streams(data: bytes) -> list[tuple[int, bytes]]:
    """(start, unpacked) of each WAD stream in DATA that holds something."""
    found, i = [], data.find(b"WAD")
    while i >= 0:
        try:
            end = i + struct.unpack_from("<I", data, i + 3)[0]
            unpacked = wad(data[i:end]) if 16 < end - i and end <= len(data) else b""
        except (FormatError, struct.error):
            unpacked = b""
        if unpacked:
            found.append((i, unpacked))
            i = data.find(b"WAD", end)
        else:
            i = data.find(b"WAD", i + 1)
    return found


def _file_parts(rel, data, replaces, disc, sources, levels, donors, statics, own) -> list[list]:
    bases: list[tuple[int, bytes]] = []
    spans: list[tuple[int, int, list]] = []
    if rel in replaces:
        first, count = replaces[rel]
        original = disc.sectors(first, count)
        outer = sources.add({"disc": [first, count]}, original)
        bases.append((outer, original))
        was_streams = _streams(original)
        table, was_table = _table(data), _table(original)
        for start, end, unpacked in wad_spans(data):
            # The stream it replaces: the one the original's table of parts (its first words)
            # names in the same slot, as a pack moves a part that grew to the end; else the
            # original's at the same place.
            slot = next((j for j in range(0, min(len(table), len(was_table)), 2) if table[j] == start), None)
            places = ([was_table[slot]] if slot is not None else []) + [start]
            was = next(((at, u) for place in places for at, u in was_streams if at == place), None)
            inner_bases = list(donors)
            if was is not None:
                inner_bases.insert(0, (sources.add({"wad": outer, "at": was[0]}, was[1]), was[1]))
            header = diff(data[start:start + 16], [(outer, original)], own)
            spans.append((start, end, [["wad", header, diff(unpacked, inner_bases, own)]]))
        if rel.split("/")[-2:-1] == ["classes"]:
            bases += donors                 # an imported class's file is in the core it comes from
    same = levels / rel
    if same.is_file():
        base = same.read_bytes()
        bases.append((sources.add({"port": rel}, base), base))
    if rel.endswith(".glb"):
        at = 0
        for made, part in statics:
            where = data.find(made, at)
            if where < 0:
                where = data.find(made)
            if where >= 0 and not any(s <= where < e for s, e, _ in spans):
                spans.append((where, where + len(made), [part]))
                at = where + len(made)
        for _, part in statics:
            source = sources.data[part[1]]
            if not any(b is source for _, b in bases):
                bases.append((part[1], source))
    spans.sort()
    parts, at = [], 0
    for start, end, made in spans:
        if start < at:
            continue
        parts += diff(data[at:start], bases, own)
        parts += made
        at = end
    parts += diff(data[at:], bases, own)
    return parts


# =============================================================================
# EXPORT
# =============================================================================

def default_levels(version: str) -> Path:
    """The extracted levels of OpenRAC beside the editor, for VERSION."""
    return openrac_root.ROOT / "assets" / "port" / "rac1" / version


def export(project: Path, out: Path, levels: Path | None = None, author: str = "", about: str = "",
           credits: list[str] | None = None, disc_path: Path | None = None, keep_problems: bool = False) -> dict:
    """Writes PROJECT's map to OUT, its game bytes named by the extracted LEVELS: what went in, for
    the report. A map with problems a player would see (map_check.py) is refused, unless KEEP_PROBLEMS."""
    import openrac_editor
    from disc import Disc
    project = project.resolve()
    if not (project / "project.godot").is_file():
        raise MapError(f"{project} is not a level project")
    disc_path = disc_path or openrac_editor.remembered_disc(project)
    with Disc(disc_path) as disc, tempfile.TemporaryDirectory(prefix=".openrac-map-") as temp:
        version = disc.version.key
        levels = (levels or default_levels(version)).resolve()
        if not levels.is_dir():
            raise MapError(f"no extracted levels at {levels}: give --levels, the levels OpenRAC plays")
        survey = disc.survey()
        made = planet.make_planet(disc, survey, project, Path(temp) / "planet", keep_problems)
        note = {"id": made["id"], "name": made["name"],
                "author": author or made["author"] or planet.settings(project).get(AUTHOR, "").strip(),
                "about": about or made["about"], "credits": list(credits or []),
                "made": time.strftime("%Y-%m-%d", time.gmtime()), "disc": version}
        out = out.resolve()
        if out.suffix != SUFFIX:
            out = out.with_name(out.name + SUFFIX)
        out.parent.mkdir(parents=True, exist_ok=True)
        report = make(Path(temp) / "planet", out, note, disc, survey, levels, project)
    return {**report, **note, "out": out, "level": made["level"]}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="openrac_editor.py map", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    out = commands.add_parser("export", help="write a level project's map as one .openrac-map file")
    out.add_argument("project", type=Path, help="one level's project")
    out.add_argument("out", type=Path, help="the map file, or a folder to write <id>.openrac-map in")
    out.add_argument("--levels", type=Path, help="the extracted levels OpenRAC plays "
                     "(default: OpenRAC's assets/port/rac1/<disc>)")
    out.add_argument("--author", default="", help="who made the map")
    out.add_argument("--about", default="", help="one line about the map")
    out.add_argument("--credit", action="append", default=[], help="credit for a model or texture you used (repeat)")
    out.add_argument("--disc", type=Path, help="the disc (default: the one the levels were read from)")
    out.add_argument("--anywhere", action="store_true", help="accepted for the launcher")
    out.add_argument("--keep-problems", action="store_true",
                     help="export even when the map has problems a player would see (check-map)")
    args = parser.parse_args(argv)
    import errors
    import overrides
    try:
        target = args.out
        if target.is_dir() or target.suffix != SUFFIX:
            target.mkdir(parents=True, exist_ok=True)
            name = planet.planet_name(args.project.resolve())
            target = target / (planet.planet_id(args.project.resolve(), name) + SUFFIX)
        done = export(args.project, target, args.levels, args.author, args.about, args.credit, args.disc,
                      args.keep_problems)
        print(f"Map {done['name']} (level {done['level']:02}, {done['disc'].upper()} disc): "
              f"{done['out'].stat().st_size / 1e6:.1f} MB, only your own bytes, no game data.")
        print("Players add the file in OpenRAC (Mods): their own disc and levels give the game's bytes back.")
        print(f"openrac-output: {done['out'].parent}")
        return 0
    except (MapError, planet.PlanetError, overrides.PackError, FormatError, OSError, struct.error) as exc:
        errors.say(exc)
        print(f"map: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
