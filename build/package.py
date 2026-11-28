#!/usr/bin/env python3
"""The release archive of OpenRAC Editor for one system, as OpenRAC's launcher downloads it
(launcher/core/src/editor_install.rs): the editor's program where openrac/editor.json says, the
editor's Python tools and project files (openrac/, tests left out), the guide and the licenses.

  python3 build/package.py PROGRAM linux|windows|macos VERSION OUT.zip

PROGRAM is the build's (bin/godot.<platform>.editor.<arch>[.exe]); on macOS it goes into the
application bundle misc/dist/macos_tools.app.
"""

import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

REPO = Path(__file__).resolve().parents[1]
MANIFEST = REPO / "openrac" / "editor.json"

# numpy for the terrain tools (terrain_tools.py), for the Python the launcher brings (3.12), so
# nothing has to be installed: unpacked into openrac/tools/vendor, which terrain_tools.py reads first.
NUMPY = "numpy==2.2.6"
PYTHON = "3.12"
PLATFORMS = {
    "linux": ["manylinux_2_17_x86_64", "manylinux2014_x86_64"],
    "windows": ["win_amd64"],
    "macos": ["macosx_11_0_arm64", "macosx_14_0_arm64"],
}


def wanted(rel: Path) -> bool:
    """Whether openrac/REL goes into the archive: everything but tests and caches."""
    if "__pycache__" in rel.parts or rel.suffix == ".pyc":
        return False
    return not rel.name.startswith("test_") and rel.name != "run_tests.sh"


def put(archive: zipfile.ZipFile, name: str, data: bytes, executable: bool = False) -> None:
    info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = (0o100755 if executable else 0o100644) << 16
    archive.writestr(info, data)


def plist_version(data: bytes, version: str) -> bytes:
    """Info.plist with the editor's version (v0.2.0 -> 0.2.0, dev -> 0.0.0)."""
    numbers = re.findall(r"\d+", version)[:3]
    plain = ".".join(numbers + ["0"] * (3 - len(numbers))).encode()
    return re.sub(rb"(<key>CFBundle(?:ShortVersionString|Version)</key>\s*<string>)[^<]*", rb"\g<1>" + plain, data)


def vendored(system: str) -> list[tuple[str, bytes]]:
    """numpy's wheel for SYSTEM's launcher Python, as (path in the wheel, bytes)."""
    with tempfile.TemporaryDirectory() as folder:
        command = [sys.executable, "-m", "pip", "download", NUMPY, "--only-binary=:all:", "--no-deps",
                   "--python-version", PYTHON, "--implementation", "cp", "--abi", "cp" + PYTHON.replace(".", ""),
                   "-d", folder, "-q"]
        for platform in PLATFORMS[system]:
            command += ["--platform", platform]
        subprocess.run(command, check=True)
        wheel = next(Path(folder).glob("*.whl"))
        with zipfile.ZipFile(wheel) as source:
            # numpy's own tests are left out, as the editor's are.
            return [(name, source.read(name)) for name in source.namelist()
                    if not name.endswith("/") and "tests" not in name.split("/")]


def package(program: Path, system: str, version: str, out: Path) -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    manifest["version"] = version
    at = manifest["program"][system]
    with zipfile.ZipFile(out, "w") as archive:
        if system == "macos":
            bundle = REPO / "misc/dist/macos_tools.app"
            top = at.split("/Contents/")[0]
            for path in sorted(p for p in bundle.rglob("*") if p.is_file()):
                data = path.read_bytes()
                if path.name == "Info.plist":
                    data = plist_version(data, version)
                put(archive, f"{top}/{path.relative_to(bundle).as_posix()}", data)
        put(archive, at, program.read_bytes(), executable=True)
        for path in sorted(p for p in (REPO / "openrac").rglob("*") if p.is_file()):
            rel = path.relative_to(REPO / "openrac")
            if rel.as_posix() == "editor.json" or not wanted(rel):
                continue
            put(archive, f"openrac/{rel.as_posix()}", path.read_bytes())
        put(archive, "openrac/editor.json", (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode())
        for name, data in vendored(system):
            put(archive, f"openrac/tools/vendor/{name}", data,
                executable=name.endswith((".so", ".dylib", ".pyd", ".dll")))
        # The guides the editor's MCP server serves to AI assistants (mcp_knowledge.gd) come too.
        for doc in ("docs/LEVEL_EDITOR.md", "docs/AI_GUIDE.md", "docs/MCP.md", "docs/CUTSCENE_FORMAT.md",
                    "docs/MAP_MODDING.md", "docs/LEVEL_MODDING.md", "docs/BEHAVIOUR_FORMAT.md",
                    "LICENSE.txt", "COPYRIGHT.txt"):
            if (REPO / doc).is_file():
                put(archive, doc, (REPO / doc).read_bytes())


def main() -> int:
    if len(sys.argv) != 5 or sys.argv[2] not in ("linux", "windows", "macos"):
        print(__doc__, file=sys.stderr)
        return 2
    package(Path(sys.argv[1]), sys.argv[2], sys.argv[3], Path(sys.argv[4]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
