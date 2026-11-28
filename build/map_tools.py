#!/usr/bin/env python3
"""The part of OpenRAC Editor that plays shared maps, for OpenRAC's launcher to carry: a player adds a
.openrac-map without the editor. OpenRAC's tools/map_tools.py runs it at the commit its
launcher/map-tools.lock pins, with the mod it built. Its Python tools and project
files (openrac/, tests and the editor's own program left out) and the editor's mod built for the
system (mods/editor-runtime: a map's moving objects and cutscenes need it), as
openrac-map-tools-<system>.zip with map-tools.json saying its version and the extractor API it needs.

  python3 build/map_tools.py SYSTEM VERSION OUT.zip [--mod BUILT_MOD_FOLDER]

SYSTEM is linux, windows, macos or android. No numpy: adding a map does not need it.
"""

import argparse
import json
from pathlib import Path
import re
import sys
import zipfile

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from package import put, wanted  # noqa: E402

SYSTEMS = ("linux", "windows", "macos", "android")


def extractor_api() -> list[int]:
    """The extractor API versions the tools work with (openrac_root.py API_VERSIONS)."""
    text = (REPO / "openrac/tools/openrac_root.py").read_text(encoding="utf-8")
    return [int(v) for v in re.findall(r"\d+", re.search(r"^API_VERSIONS = \(([^)]*)\)", text, re.M).group(1))]


def pack(system: str, version: str, out: Path, mod: Path | None) -> None:
    if mod is not None and not (mod / "openrac-mod.json").is_file():
        raise SystemExit(f"{mod}: no built mod there (no openrac-mod.json)")
    with zipfile.ZipFile(out, "w") as archive:
        for top in ("tools", "project"):
            for path in sorted(p for p in (REPO / "openrac" / top).rglob("*") if p.is_file()):
                rel = path.relative_to(REPO / "openrac")
                if not wanted(rel) or "vendor" in rel.parts:
                    continue
                put(archive, f"openrac/{rel.as_posix()}", path.read_bytes())
        if mod is not None:
            for path in sorted(p for p in mod.rglob("*") if p.is_file()):
                put(archive, f"openrac/mods/editor-runtime/{path.relative_to(mod).as_posix()}", path.read_bytes())
        about = {"version": version, "system": system, "extractorApi": extractor_api(), "runtime": mod is not None}
        put(archive, "map-tools.json", (json.dumps(about, indent=1) + "\n").encode())
        for doc in ("LICENSE.txt", "COPYRIGHT.txt"):
            if (REPO / doc).is_file():
                put(archive, doc, (REPO / doc).read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("system", choices=SYSTEMS)
    parser.add_argument("version")
    parser.add_argument("out", type=Path)
    parser.add_argument("--mod", type=Path, help="the editor's mod built for SYSTEM (its package folder)")
    args = parser.parse_args()
    pack(args.system, args.version, args.out, args.mod)
    return 0


if __name__ == "__main__":
    sys.exit(main())
