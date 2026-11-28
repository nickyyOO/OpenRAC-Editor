"""What breaks on one system and not another: text read in the system's code page (Windows) or
written with its line ends, file names Windows or a case-blind disk (Windows, macOS) cannot hold,
paths too long for Windows, the release archives the launcher looks for, the program's place and
execute bit in each archive, and starting the editor on its own on each system.

    python3 -m unittest test_platforms
"""

import openrac_root  # noqa: F401,I001  (OpenRAC's extractor on the import path)
import ast
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

import openrac_editor as oe

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
OPENRAC = REPO / "openrac"
MANIFEST = json.loads((OPENRAC / "editor.json").read_text(encoding="utf-8"))
SYSTEMS = ("linux", "windows", "macos")
# Names Windows keeps for devices, whatever the extension.
RESERVED = {"con", "prn", "aux", "nul", *(f"com{i}" for i in range(1, 10)), *(f"lpt{i}" for i in range(1, 10))}
# The longest folder the editor goes into on Windows, roughly:
# C:\Users\<a long name>\AppData\Local\openrac\editor\ (the launcher's data folder).
INSTALL_PREFIX = len("C:\\Users\\" + "x" * 32 + "\\AppData\\Local\\openrac\\editor\\")
WINDOWS_MAX_PATH = 259


def folder_name(name: str) -> str:
    """NAME for a folder, or its ASCII spelling where this system cannot name files so (a Linux
    run in a Windows code page, which the CI uses to stand in for Windows' text)."""
    try:
        name.encode(sys.getfilesystemencoding())
        return name
    except UnicodeEncodeError:
        return name.encode("ascii", "replace").decode().replace("?", "_")


def tool_sources() -> list[Path]:
    """The editor's Python that runs on the player's computer (not its tests)."""
    return sorted([p for p in HERE.glob("*.py") if not p.name.startswith("test_")] + list((REPO / "build").glob("*.py")))


def load_package():
    spec = importlib.util.spec_from_file_location("package", REPO / "build" / "package.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Text(unittest.TestCase):
    def test_every_text_read_and_write_names_utf8(self):
        """Without it Python reads and writes in the system's code page: cp1252 on most Windows,
        so a scene with a node named "Działko" breaks there and nowhere else."""
        missing = []
        for path in tool_sources():
            tree = ast.parse(path.read_text(encoding="utf-8"))
            for node in ast.walk(tree):
                if not isinstance(node, ast.Call):
                    continue
                func = node.func
                keywords = {k.arg for k in node.keywords}
                if isinstance(func, ast.Attribute) and func.attr in ("read_text", "write_text"):
                    if "encoding" not in keywords:
                        missing.append(f"{path.name}:{node.lineno} {func.attr} without encoding")
                    if func.attr == "write_text" and "newline" not in keywords:
                        missing.append(f"{path.name}:{node.lineno} write_text without newline=\"\\n\"")
                elif isinstance(func, ast.Name) and func.id == "open":
                    mode = node.args[1] if len(node.args) > 1 else next(
                        (k.value for k in node.keywords if k.arg == "mode"), ast.Constant("r"))
                    if isinstance(mode, ast.Constant) and "b" not in mode.value and "encoding" not in keywords:
                        missing.append(f"{path.name}:{node.lineno} open() in text mode without encoding")
        self.assertEqual(missing, [], "\n".join(missing))

    def test_written_text_has_the_same_bytes_everywhere(self):
        """A file the tools write is the same on Windows (no \\r\\n), so packs and digests match."""
        import godot
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp) / "level_00"
            godot.write_project(project, [0])
            for path in (project / "project.godot", project / godot.PROJECT_STAMP):
                self.assertNotIn(b"\r\n", path.read_bytes(), path.name)

    def test_checkouts_keep_lf(self):
        """The release is packed from a Windows checkout too: with \\r\\n there, its project files
        would differ from Linux's and every project would be brought up to date again."""
        attributes = (REPO / ".gitattributes").read_text(encoding="utf-8")
        self.assertRegex(attributes, r"(?m)^\* text=auto eol=lf$")
        for path in OPENRAC.rglob("*"):
            if path.is_file() and path.suffix in (".py", ".gd", ".json", ".tsv", ".md", ".cfg", ".tscn"):
                self.assertNotIn(b"\r\n", path.read_bytes(), path.relative_to(REPO).as_posix())

    def test_a_project_stamp_with_windows_line_ends_is_still_current(self):
        import godot
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp) / "level_00"
            godot.write_project(project, [0])
            (project / godot.PROJECT_STAMP).write_bytes(godot.project_files().encode() + b"\r\n")
            self.assertFalse(oe.sync_project(project))

    def test_a_scene_with_non_ascii_names_reads_back(self):
        """A player's own node names, in a project under a folder with spaces and letters past ASCII,
        read back as the pack reads them (and as a CRLF scene from a Windows editor would be)."""
        import pack
        scene_text = ('[gd_scene format=3]\n\n'
                      '[ext_resource type="PackedScene" path="res://levels/level_03/mobys/moby_11.glb" id="v"]\n\n'
                      '[node name="Działko_żółw" parent="Game/Mobys" instance=ExtResource("v")]\n'
                      'transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 2, 3, 4)\n')
        with tempfile.TemporaryDirectory() as tmp:
            scene = Path(tmp) / folder_name("Mateusz Kłysz") / "level 03.tscn"
            scene.parent.mkdir()
            for ends in ("\n", "\r\n"):
                scene.write_bytes(scene_text.replace("\n", ends).encode("utf-8"))
                nodes = pack.scene_mobys(scene.read_text(encoding="utf-8"))
                self.assertEqual([(n["name"], n["class_id"]) for n in nodes], [("Działko_żółw", 11)], repr(ends))
                self.assertEqual(nodes[0]["transform"][-3:], (2.0, 3.0, 4.0))


class Names(unittest.TestCase):
    def files(self) -> list[Path]:
        return [p.relative_to(REPO) for p in OPENRAC.rglob("*") if "__pycache__" not in p.parts]

    def test_every_name_is_one_windows_can_hold(self):
        bad = []
        for rel in self.files():
            for part in rel.parts:
                stem = part.split(".")[0].lower()
                if (re.search(r'[<>:"\\|?*\x00-\x1f]', part) or part.endswith((".", " ")) or stem in RESERVED):
                    bad.append(rel.as_posix())
        self.assertEqual(bad, [])

    def test_no_two_names_differ_only_in_case(self):
        """Windows and macOS disks are blind to case: one would overwrite the other."""
        seen: dict[str, str] = {}
        clashes = []
        for rel in self.files():
            key = rel.as_posix().lower()
            if key in seen:
                clashes.append(f"{seen[key]} / {rel.as_posix()}")
            seen[key] = rel.as_posix()
        self.assertEqual(clashes, [])

    def test_installed_paths_fit_windows(self):
        """The editor's files and a level project's under the launcher's data folder stay short of
        Windows' 260 characters."""
        longest = max([*(len(rel.as_posix()) for rel in self.files()), *(len(p) for p in MANIFEST["program"].values())])
        self.assertLessEqual(INSTALL_PREFIX + longest, WINDOWS_MAX_PATH, f"the longest is {longest} characters")
        project_files = [p.relative_to(OPENRAC / "project") for p in (OPENRAC / "project").rglob("*") if p.is_file()]
        level = len("C:\\Users\\" + "x" * 32 + "\\AppData\\Local\\openrac\\projects\\rac1\\pal\\level_00\\")
        longest = max(len(rel.as_posix()) for rel in project_files)
        self.assertLessEqual(level + longest, WINDOWS_MAX_PATH, f"the longest project file is {longest} characters")


class Release(unittest.TestCase):
    def test_programs_are_where_each_system_runs_them(self):
        program = MANIFEST["program"]
        self.assertEqual(set(program), set(SYSTEMS))
        self.assertTrue(program["windows"].endswith(".exe"))
        self.assertFalse(program["linux"].endswith(".exe"))
        self.assertRegex(program["macos"], r"^[^/]+\.app/Contents/MacOS/[^/]+$")
        for path in program.values():
            self.assertNotIn("\\", path, "the launcher splits the manifest's paths at /")
            self.assertFalse(path.startswith("/"), path)

    def test_the_mac_bundle_starts_the_program_the_manifest_names(self):
        """Finder starts Info.plist's CFBundleExecutable; the launcher starts the manifest's path."""
        import plistlib
        plist = REPO / "misc" / "dist" / "macos_tools.app" / "Contents" / "Info.plist"
        if not plist.is_file():
            self.skipTest("no macos_tools.app")
        info = plistlib.loads(plist.read_bytes())
        bundle, _, _, name = MANIFEST["program"]["macos"].split("/")
        self.assertEqual(info["CFBundleExecutable"], name)
        self.assertEqual(bundle, f"{info['CFBundleName']}.app")

    def test_release_archives_are_the_ones_the_launcher_downloads(self):
        workflow = (REPO / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
        assets = dict(re.findall(r"- system: (\w+)\n(?:\s+\w+: .*\n)*?\s+asset: (\S+)", workflow))
        self.assertEqual(set(assets), set(SYSTEMS))
        launcher = openrac_root.ROOT / "launcher" / "core" / "src" / "editor_install.rs"
        if not launcher.is_file():
            self.skipTest("an OpenRAC without the launcher's source")
        source = launcher.read_text(encoding="utf-8")
        wants = dict(re.findall(r"Platform::(Linux|Windows|Macos) => Some\(\"([^\"]+\.zip)\"\)", source))
        self.assertEqual({k.lower(): v for k, v in wants.items()}, assets)

    def test_each_archive_holds_a_runnable_program_and_the_tools(self):
        package = load_package()
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / "godot.editor"
            program.write_bytes(b"\x7fELF")
            for system in SYSTEMS:
                with self.subTest(system=system):
                    out = Path(tmp) / f"{system}.zip"
                    if system == "macos" and not (REPO / "misc/dist/macos_tools.app").is_dir():
                        self.skipTest("no macos_tools.app")
                    package.package(program, system, "v9.9.9", out)
                    with zipfile.ZipFile(out) as archive:
                        names = archive.namelist()
                        at = MANIFEST["program"][system]
                        self.assertIn(at, names)
                        self.assertEqual(archive.getinfo(at).external_attr >> 16 & 0o111, 0o111, "can run")
                        for name in names:
                            self.assertNotIn("\\", name)
                            self.assertFalse(name.startswith("/") or ".." in name.split("/"), name)
                            self.assertNotIn("__pycache__", name)
                            self.assertFalse(name.rsplit("/", 1)[-1].startswith("test_"), name)
                        self.assertEqual(len(names), len({n.lower() for n in names}), "a name only in case")
                        self.assertIn("openrac/tools/openrac_editor.py", names)
                        self.assertIn("openrac/tools/openrac_root.py", names)
                        self.assertIn("openrac/tools/vendor/numpy/__init__.py", names)     # terrain tools
                        manifest = json.loads(archive.read("openrac/editor.json"))
                        self.assertEqual(manifest["version"], "v9.9.9")
                        if system == "macos":
                            bundle = at.split("/Contents/")[0]
                            self.assertIn(f"{bundle}/Contents/Info.plist", names)


class MapTools(unittest.TestCase):
    def test_map_tools_archive_plays_a_map_without_the_editor(self):
        """What OpenRAC's launcher carries (launcher/map-tools.lock): the tools, the project files
        and the editor's mod, no program, no numpy, no tests."""
        spec = importlib.util.spec_from_file_location("map_tools", REPO / "build" / "map_tools.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as tmp:
            mod = Path(tmp) / "mod"
            mod.mkdir()
            (mod / "openrac-mod.json").write_text("{}\n", encoding="utf-8", newline="\n")
            out = Path(tmp) / "map-tools.zip"
            module.pack("android", "v9.9.9", out, mod)
            with zipfile.ZipFile(out) as archive:
                names = archive.namelist()
                about = json.loads(archive.read("map-tools.json"))
            self.assertEqual(about["version"], "v9.9.9")
            self.assertEqual(about["extractorApi"], list(openrac_root.API_VERSIONS))
            for name in ("openrac/tools/openrac_editor.py", "openrac/tools/shared_map.py",
                         "openrac/project/project_format.json", "openrac/mods/editor-runtime/openrac-mod.json"):
                self.assertIn(name, names)
            for name in names:
                self.assertNotIn("/vendor/", name)
                self.assertFalse(name.rsplit("/", 1)[-1].startswith("test_"), name)
                self.assertNotIn("__pycache__", name)


class Starting(unittest.TestCase):
    def test_starts_on_its_own_on_each_system(self):
        """Detached, so closing the launcher leaves the editor open: a new session off Windows,
        a process of its own on Windows."""
        flags = {"DETACHED_PROCESS": 0x8, "CREATE_NEW_PROCESS_GROUP": 0x200}
        for platform, want in (("linux", {"start_new_session": True}), ("darwin", {"start_new_session": True}),
                               ("win32", {"creationflags": 0x208})):
            with self.subTest(platform=platform), mock.patch.object(oe.sys, "platform", platform), \
                    mock.patch.multiple(oe.subprocess, create=True, **flags), \
                    mock.patch.object(oe.subprocess, "Popen") as popen:
                self.assertEqual(oe.open_editor(["C:\\Program Files\\Ed\\ed.exe", "--editor"]), 0)
                for key, value in want.items():
                    self.assertEqual(popen.call_args.kwargs[key], value)
                self.assertEqual(popen.call_args.kwargs["stdin"], subprocess.DEVNULL)

    def test_paths_with_spaces_and_letters_past_ascii_pass_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(oe.subprocess, "Popen") as popen:
            project = Path(tmp) / folder_name("Mateusz Kłysz") / "level 03"
            program = "/Applications/OpenRAC Editor.app/Contents/MacOS/OpenRAC Editor"
            argv = [program, "--path", str(project), "res://levels/level_03/level_03.tscn"]
            self.assertEqual(oe.open_editor(argv), 0)
            self.assertEqual(popen.call_args.args[0], argv, "one argument each, never split at spaces")

    def test_path_as_the_last_argument_does_not_break_it(self):
        with mock.patch.object(oe.subprocess, "Popen") as popen:
            self.assertEqual(oe.open_editor(["ed", "--path"]), 0)
            popen.assert_called_once()


class Workers(unittest.TestCase):
    """The levels are exported in processes of their own. Linux starts them with fork, a copy of
    this one; Windows and macOS with spawn, a fresh Python that imports the tools again and gets
    the function by name, so a function only this process has breaks there alone. Android has no
    process pools: extract.SubprocessPool runs openrac_editor.py --worker instead."""

    CHILD = """
import multiprocessing, sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
import openrac_editor as oe
import extract
if __name__ == "__main__":
    kind = sys.argv[1]
    if kind == "spawn":
        pool = ProcessPoolExecutor(1, mp_context=multiprocessing.get_context("spawn"))
    else:
        pool = extract.SubprocessPool(1, Path(oe.__file__).resolve())
    with pool:
        for function, args in ((oe.export_level, (Path("no disc.iso"), {}, Path(sys.argv[2]), 0)),
                               (oe.export_wad_level, (Path("no disc.iso"), 3, Path(sys.argv[2]), 0))):
            try:
                pool.submit(function, *args).result(timeout=120)
                print("ran")
            except Exception as error:
                print(type(error).__name__, str(error)[:200])
"""

    def run_child(self, kind: str) -> tuple[list[str], str]:
        with tempfile.TemporaryDirectory() as tmp:
            done = subprocess.run([sys.executable, "-c", self.CHILD, kind, tmp], cwd=HERE, capture_output=True,
                                  text=True, encoding="utf-8", errors="replace", timeout=300)
        self.assertEqual(done.returncode, 0, done.stderr)
        return done.stdout.splitlines(), done.stderr

    def check(self, run: tuple[list[str], str]) -> None:
        lines, stderr = run
        self.assertEqual(len(lines), 2, lines)
        if lines[0].startswith("RuntimeError"):
            # SubprocessPool says only that the worker failed; the worker said why on stderr.
            self.assertNotIn("is not one of the level exports", stderr)
            lines = [stderr]
        for line in lines:
            # The worker found and ran the function: it failed on the missing disc, not on
            # pickling, importing or finding it.
            self.assertNotRegex(line, r"^(PicklingError|AttributeError|ModuleNotFoundError|ImportError|"
                                      r"BrokenProcessPool|NameError)", line)
            self.assertRegex(line, r"no disc\.iso|No such file|FileNotFound|FormatError", line)

    def test_spawned_workers_run_the_exports(self):
        self.check(self.run_child("spawn"))

    def test_subprocess_workers_run_the_exports(self):
        self.check(self.run_child("subprocess"))

    def test_every_function_given_to_the_pool_can_be_found_by_name(self):
        tree = ast.parse((HERE / "openrac_editor.py").read_text(encoding="utf-8"))
        top = {n.name for n in tree.body if isinstance(n, ast.FunctionDef)}
        submitted = {n.args[0].id for n in ast.walk(tree) if isinstance(n, ast.Call)
                     and isinstance(n.func, ast.Attribute) and n.func.attr == "submit" and isinstance(n.args[0], ast.Name)}
        self.assertTrue(submitted)
        self.assertLessEqual(submitted, top, "a worker function must be a top-level one")
        self.assertLessEqual(submitted, set(oe.WORKER_FUNCTIONS), "Android's SubprocessPool runs only these")
        self.assertRegex((HERE / "openrac_editor.py").read_text(encoding="utf-8"), r'\nif __name__ == "__main__":\n',
                         "spawn imports the script again: its commands must not run then")


class FindingOpenRAC(unittest.TestCase):
    def find(self, root: Path | None, version: int = 1) -> subprocess.CompletedProcess:
        env = {k: v for k, v in os.environ.items() if k != "OPENRAC_ROOT"}
        if root is not None:
            (root / "extractor").mkdir(parents=True, exist_ok=True)
            (root / "extractor" / "extractor_api.json").write_text(json.dumps({"version": version}),
                                                                    encoding="utf-8", newline="\n")
            env["OPENRAC_ROOT"] = str(root) + os.sep
        env["PYTHONUTF8"] = "0"
        env["PYTHONIOENCODING"] = "utf-8"  # only so the answer reaches the test; a console's code page is not under test
        return subprocess.run([sys.executable, "-c", "import openrac_root; print(openrac_root.ROOT)"],
                              cwd=HERE, env=env, capture_output=True, text=True, encoding="utf-8")

    def test_a_root_with_spaces_letters_past_ascii_and_a_trailing_separator(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "Program Files" / folder_name("Łódź OpenRAC")
            done = self.find(root)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertEqual(Path(done.stdout.strip()), root.resolve())

    def test_another_api_is_refused_with_what_to_update(self):
        with tempfile.TemporaryDirectory() as tmp:
            done = self.find(Path(tmp) / "OpenRAC", version=999)
            self.assertNotEqual(done.returncode, 0)
            self.assertIn("update OpenRAC or OpenRAC Editor", done.stderr)


if __name__ == "__main__":
    unittest.main()
