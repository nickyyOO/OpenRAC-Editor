# SPDX-License-Identifier: MIT
# Copyright (c) 2026 the OpenRAC contributors
"""Shared maps: a planet pack as parts of the player's own files, made back byte for byte."""
import openrac_root  # noqa: F401,I001  (OpenRAC's extractor on the import path)
import json
import os
from pathlib import Path
import random
import tempfile
import unittest
import zipfile

from formats import wad
from wadpack import compress

import shared_map

SECTOR = 2048


def build(parts: list, sources: list[bytes], own: bytes) -> bytes:
    """What PARTS make (the data, copy and wad parts, as OpenRAC's maps.rs makes them)."""
    out = bytearray()
    for part in parts:
        if part[0] == "data":
            out += own[part[1]:part[1] + part[2]]
        elif part[0] == "copy":
            out += sources[part[1]][part[2]:part[2] + part[3]]
        elif part[0] == "wad":
            out += compress(build(part[2], sources, own), build(part[1], sources, own))
        else:
            raise AssertionError(f"unexpected part {part[0]}")
    return bytes(out)


def read_sources(manifest: dict, disc: bytes, levels: Path) -> list[bytes]:
    found: list[bytes] = []
    for spec in manifest["sources"]:
        if "port" in spec:
            data = (levels / spec["port"]).read_bytes()
        elif "disc" in spec:
            first, count = spec["disc"]
            data = disc[first * SECTOR:(first + count) * SECTOR]
        else:
            outer = found[spec["wad"]]
            data = wad(outer[spec["at"]:])
        assert shared_map.sha(data) == spec["sha256"]
        found.append(data)
    return found


class FakeDisc:
    def __init__(self, data: bytes):
        self.data = data

    def sectors(self, first: int, count: int) -> bytes:
        return self.data[first * SECTOR:(first + count) * SECTOR]


def game_bytes(seed: int, size: int) -> bytes:
    """Bytes like a level's: words from a small set, so a WAD stream packs them."""
    rng = random.Random(seed)
    words = [rng.randbytes(4) for _ in range(64)]
    return b"".join(rng.choice(words) for _ in range(size // 4))


class Diff(unittest.TestCase):
    def test_copies_the_players_bytes_and_keeps_the_rest(self):
        base = os.urandom(200_000)
        target = base[:50_000] + b"mine" * 100 + base[60_000:150_000] + os.urandom(3000) + base[150_000:]
        own = bytearray()
        parts = shared_map.diff(target, [(0, base)], own)
        self.assertEqual(build(parts, [base], bytes(own)), target)
        self.assertLess(len(own), 4000)

    def test_runs_across_two_bases(self):
        a, b = os.urandom(40_000), os.urandom(40_000)
        target = a[30_000:] + b[:20_000]
        own = bytearray()
        parts = shared_map.diff(target, [(0, a), (1, b)], own)
        self.assertEqual(build(parts, [a, b], bytes(own)), target)
        self.assertEqual(len(own), 0)

    def test_no_base(self):
        own = bytearray()
        self.assertEqual(build(shared_map.diff(b"abc", [], own), [], bytes(own)), b"abc")
        self.assertEqual(shared_map.diff(b"", [], own), [])


class Make(unittest.TestCase):
    """A planet whose core is the disc's level with one WAD stream changed, and a texture the same
    as an extracted level's: the map carries only what the maker changed."""

    def test_map_holds_only_the_makers_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            unpacked = game_bytes(4, 60_000)
            stream = compress(unpacked)
            head = os.urandom(8 * SECTOR)
            level = head + stream + bytes(-(len(head) + len(stream)) % SECTOR)
            disc = bytes(10 * SECTOR) + level
            levels = root / "levels"
            (levels / "level_11" / "textures").mkdir(parents=True)
            texture = os.urandom(5000)
            (levels / "level_11" / "textures" / "0001.png").write_bytes(texture)
            pack = root / "planet"
            (pack / "level_11" / "textures").mkdir(parents=True)
            mine = b"the maker's own" * 50
            edited = unpacked[:30_000] + mine + unpacked[30_000:]
            (pack / "level_11" / "core_pal.bin").write_bytes(head + compress(edited, stream[:16]))
            (pack / "level_11" / "textures" / "0001.png").write_bytes(texture)
            (pack / "placements.json").write_text('{"mine": true}')
            (pack / "patches.tsv").write_text(f"10\t{len(level) // SECTOR}\tlevel_11/core_pal.bin\n")
            project = root / "project"
            project.mkdir()
            out = root / "test.openrac-map"
            about = {"id": "test", "name": "Test", "author": "Me", "about": "", "credits": [],
                     "made": "2026-11-28", "disc": "pal"}
            shared_map.make(pack, out, about, FakeDisc(disc), {"levels": []}, levels, project)

            with zipfile.ZipFile(out) as archive:
                manifest = json.loads(archive.read("map.json"))
                blobs = {name: archive.read(name) for name in archive.namelist() if name != "map.json"}
            self.assertEqual((manifest["format"], manifest["version"]), ("openrac-map", 2))
            self.assertEqual(manifest["author"], "Me")
            sources = read_sources(manifest, disc, levels)
            for i, entry in enumerate(manifest["files"]):
                made = build(entry["parts"], sources, blobs.get(f"data/{i}.bin", b""))
                self.assertEqual(shared_map.sha(made), entry["sha256"], entry["path"])
                self.assertEqual((pack / entry["path"]).read_bytes(), made)
            carried = b"".join(blobs.values())
            # The core is the disc's level but for the maker's bytes; the texture is only named.
            self.assertIn(mine, carried)
            self.assertLess(len(carried), 4 * len(mine))
            self.assertNotIn(texture[:256], carried)
            self.assertNotIn(head[:256], carried)


class Spans(unittest.TestCase):
    def test_finds_streams_that_pack_again(self):
        stream = compress(game_bytes(6, 30_000))
        data = b"x" * 100 + stream + b"WADnot a stream" + b"y" * 50
        found = shared_map.wad_spans(data)
        self.assertEqual([(s, e) for s, e, _ in found], [(100, 100 + len(stream))])


if __name__ == "__main__":
    unittest.main()
