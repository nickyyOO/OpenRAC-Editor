#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Mateusz Kłysz
"""Tests of scripts/ai_rules.py: python3 scripts/test_ai_rules.py -v"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ai_rules  # noqa: E402


class Subject(unittest.TestCase):
    def test_conventional(self):
        self.assertEqual(ai_rules.subject_problems("feat(rac-board): ride the board"), [])
        self.assertEqual(ai_rules.subject_problems("fix: keep the rail"), [])
        self.assertEqual(ai_rules.subject_problems("feat!: drop 1.11"), [])

    def test_bad(self):
        self.assertTrue(ai_rules.subject_problems("Ride the board"))
        self.assertTrue(ai_rules.subject_problems("feature: ride"))
        self.assertTrue(ai_rules.subject_problems("fix: the rail."))

    def test_git_subjects(self):
        self.assertEqual(ai_rules.subject_problems("Merge branch 'main'"), [])


class Text(unittest.TestCase):
    def test_trailers(self):
        self.assertTrue(ai_rules.text_problems("x\n\nCo-Authored-By: Claude <noreply@anthropic.com>"))  # ai-rules: allow
        self.assertTrue(ai_rules.text_problems("Claude-Session: https://example.com/x"))  # ai-rules: allow

    def test_person_credit_kept(self):
        self.assertEqual(ai_rules.text_problems(
            "Co-authored-by: dana <123+dana@users.noreply.github.com>"), [])  # ai-rules: allow

    def test_links_and_footers(self):
        self.assertTrue(ai_rules.text_problems("see https://claude.ai/code/session_01abc"))  # ai-rules: allow
        self.assertTrue(ai_rules.text_problems("Generated with [Claude Code](https://claude.com)"))  # ai-rules: allow
        self.assertTrue(ai_rules.text_problems("\U0001F916 Generated with something"))  # ai-rules: allow

    def test_prose_about_claude_is_fine(self):
        self.assertEqual(ai_rules.text_problems("The co-authored-by rule is in CONTRIBUTING.md"), [])

    def test_waiver(self):
        self.assertEqual(ai_rules.text_problems("Claude-Session: x  ai-rules: allow"), [])


class Identity(unittest.TestCase):
    def test_agents(self):
        self.assertTrue(ai_rules.identity_problems("author", "Claude", "noreply@anthropic.com"))
        self.assertTrue(ai_rules.identity_problems("author", "Someone", "bot@openai.com"))
        self.assertTrue(ai_rules.identity_problems("committer", "claude[bot]",
                                                   "209825114+claude[bot]@users.noreply.github.com"))
        self.assertTrue(ai_rules.identity_problems("author", "Copilot", "198982749+Copilot@users.noreply.github.com"))

    def test_people(self):
        self.assertEqual(ai_rules.identity_problems("author", "Mateusz Kłysz", "mateuszklysz@icloud.com"), [])
        self.assertEqual(ai_rules.identity_problems("committer", "GitHub", "noreply@github.com"), [])

    def test_strip(self):
        text = "feat: x\n\nbody\n\nCo-Authored-By: Claude <noreply@anthropic.com>\n"  # ai-rules: allow
        self.assertEqual(ai_rules.strip_trailers(text), "feat: x\n\nbody\n")


if __name__ == "__main__":
    unittest.main()
