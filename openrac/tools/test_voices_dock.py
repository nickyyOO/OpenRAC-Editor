"""The Voices dock in Godot's editor (addons/openrac_library/voices_dock.gd and voices_lab.gd):
its process runs voice_lab.py, answers reach the commands that asked, a stop is clean, the dock
lists the models, the disc's voices and lines from a workspace, names a voice, and a cutscene
subtitle gets its voice line. Runs Godot headless, so only when told where it is:

    OPENRAC_GODOT=/path/to/godot python3 -m unittest test_voices_dock
"""

import openrac_root  # noqa: F401,I001  (OpenRAC's extractor on the import path)
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from voices.workspace import Workspace, write_wav

GODOT = os.environ.get("OPENRAC_GODOT")
HERE = Path(__file__).resolve().parent


@unittest.skipUnless(GODOT, "set OPENRAC_GODOT")
class Dock(unittest.TestCase):
    def test_the_dock_end_to_end(self):
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp) / "level"
            project.mkdir()
            (project / "project.godot").write_text('[application]\nconfig/name="t"\n')
            shutil.copytree(HERE.parent / "project" / "addons" / "openrac_library", project / "addons" / "openrac_library")
            shutil.copy(HERE / "test_voices_dock.gd", project / "t.gd")
            data = Path(temp) / "data"
            space = Workspace.of(data / "voices", "rac1", "ntsc")
            clips = []
            for n, (who, text) in enumerate([("Clank", "Hello."), ("Clank", "Ratchet!"), (None, "Need a hand?")]):
                key = f"{n:016x}"
                clips.append({"key": key, "lba": 1, "rate": 16000, "seconds": 2.0, "group": "Help", "name": f"h{n}",
                              "base": f"h{n}", "levels": [1], "language": "en", "lines": [
                                  {"id": f"{key}:0", "start": 0.0, "end": 2.0, "text": text, "text_source": "subtitle",
                                   "voice": "en-0" if who else "en-1", "character": who,
                                   "character_source": "voice" if who else ""}]})
                write_wav(space.snippet(f"{key}:0"), b"\x10\x00" * 32000, 16000)
            space.save({"format": 1, "game": "rac1", "version": "ntsc", "disc": "x.iso", "clips": clips})
            space.save_characters({"characters": [{"id": 1, "name": "Clank"}]})
            out = Path(temp) / "out.json"
            env = {**os.environ, "OPENRAC_ROOT": str(openrac_root.ROOT)}
            done = subprocess.run([GODOT, "--headless", "--editor", "--path", str(project), "--script", "res://t.gd",
                                   "--", str(out), sys.executable, str(HERE), str(data)],
                                  capture_output=True, text=True, timeout=600, env=env)
            self.assertTrue(out.is_file(), done.stdout[-3000:] + done.stderr[-3000:])
            steps = json.loads(out.read_text(encoding="utf-8"))
            named = [line for _, line in Workspace.lines(space.load()) if line.get("character") == "Big Al"]

        answer, refusal, progress, words, broken = steps["parse"]
        self.assertEqual(answer, {"kind": "answer", "id": 3, "ok": True, "result": {"a": 1}, "error": ""})
        self.assertEqual((refusal["id"], refusal["ok"], refusal["error"]), (-1, False, "not a JSON command"))
        self.assertEqual((progress["kind"], progress["stage"], progress["done"], progress["total"]), ("progress", "speak", 1, 3))
        self.assertEqual(words["kind"], "line")
        self.assertEqual(broken["kind"], "line")
        self.assertEqual(steps["arguments"][0][-7:], ["--overrides", "/o", "--game", "rac2", "--version", "pal", "serve"])
        self.assertEqual(steps["arguments"][1][-1], "serve")
        self.assertNotIn("--game", steps["arguments"][1])
        self.assertIn("launcher", steps["no_python"])
        self.assertEqual(steps["start"], "")
        self.assertTrue(steps["busy"])
        self.assertEqual([a[:2] for a in steps["answers"]], [["hardware", True], ["lines", True], ["explode", False]])
        self.assertIn("no command", steps["answers"][2][2])
        self.assertIn(steps["device"], ("cpu", "cuda", "mps"))
        self.assertEqual(steps["clank_lines"], 2)
        self.assertFalse(steps["busy_after"])
        self.assertFalse(steps["running_after_stop"])
        self.assertEqual(steps["after_stop"], [[False, "The voice tools are not running."]])
        self.assertGreaterEqual(steps["models_listed"], 7)
        self.assertIn("Best choice:", steps["hardware_text"])
        self.assertIn("3 lines", steps["disc_text"])
        self.assertEqual(steps["voices"], 2)
        self.assertEqual(steps["lines"], 3)
        self.assertEqual(steps["people"], ["Everyone", "Not named yet", "Clank"])
        self.assertEqual(steps["languages"], ["English"])
        self.assertIn("Big Al", steps["named_status"])
        self.assertEqual(len(named), 2)
        cut = steps["cutscene"]
        self.assertEqual((cut["class"], cut["same"], cut["name"], cut["kind"]), ("CutsceneSound", True, "Line1Voice", 0))
        self.assertAlmostEqual(cut["start"], 2.5)
        self.assertEqual(cut["files"], {"en": "voices/intro_line1_en.wav", "fr": "voices/intro_line1_fr.wav"})
        self.assertTrue(cut["source_is_speaker"])
        self.assertTrue(cut["owned"])
        self.assertEqual(cut["file"], "voices/intro_line_1_en.wav")


if __name__ == "__main__":
    unittest.main()
