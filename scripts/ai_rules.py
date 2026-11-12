# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Mateusz Kłysz
"""The repository's rules on AI tools (CONTRIBUTING.md), shared by the git hook
(scripts/commit-msg.py) and the pull request check (scripts/check-ai.py).

Work prepared with an AI agent is welcome, but what enters the history is the maintainer's: no
agent trailers (Co-Authored-By, Claude-Session), no "Generated with Claude" footers, no links to
private AI chat or agent sessions, and never an AI agent as author or committer. Python 3
standard library only.
"""
from __future__ import annotations

import re

# A line that holds this text is not checked (for text that has to name the patterns, as these
# rules do).
WAIVER = "ai-rules: allow"

# The commit subject: Conventional Commits, `type(scope)!: summary`.
TYPES = {"feat", "fix", "docs", "chore", "refactor", "test", "build", "ci", "perf", "style", "revert"}
SUBJECT_RE = re.compile(r"^(?P<type>[a-z]+)(?:\((?P<scope>[a-z0-9._/-]+)\))?!?: (?P<summary>\S.*)$")

# Trailer form only (the line starts with the token and a colon), so prose about the rule survives.
# A person credited by their GitHub noreply address is kept: every other co-author line is an
# agent trailer.
TRAILER_RE = re.compile(
    r"^\s*(?:claude[-_ ]?session\s*:|co[-_ ]?authored[-_ ]?by\s*:"
    r"(?!\s*[\w-]+ <\d+\+[\w-]+@users\.noreply\.github\.com>\s*$))",
    re.IGNORECASE | re.MULTILINE,
)

# What never goes in a commit message, a pull request's title or description, or the files.
FORBIDDEN = [
    (re.compile(r"(?:claude\.ai|chatgpt\.com|chat\.openai\.com)/\S*(?:session|chat|share|c/)", re.I),
     "link to a private AI session"),
    (TRAILER_RE, "agent trailer"),
    (re.compile(r"generated (?:with|by) \[?(?:claude|chatgpt|copilot|codex)", re.I), "AI tool footer"),
    (re.compile(r"\U0001F916\s*generated", re.I), "AI tool footer"),
]

# An AI agent's identity as author or committer: its address, or its name.
AGENT_EMAIL_RE = re.compile(r"@(?:[\w.-]+\.)?(?:anthropic|openai)\.com$"
                            r"|^\d*\+?(?:claude|copilot|codex|chatgpt)[\w.-]*(?:\[bot\])?@users\.noreply\.github\.com$",
                            re.I)
AGENT_NAME_RE = re.compile(r"^(?:claude|chatgpt|codex|copilot)\b|\[bot\]$", re.I)


def subject_problems(subject: str) -> list[str]:
    """What is wrong with a commit subject (or a pull request title)."""
    subject = subject.strip()
    # Subjects git writes itself keep their form.
    if re.match(r'^(?:Merge |Revert "|fixup! |squash! |amend! )', subject):
        return []
    m = SUBJECT_RE.match(subject)
    if not m:
        return ["expected '<type>(<scope>): <summary>' (Conventional Commits)"]
    found = []
    if m.group("type") not in TYPES:
        found.append(f"unknown type '{m.group('type')}' (one of: {', '.join(sorted(TYPES))})")
    if len(subject) > 100:
        found.append(f"subject is {len(subject)} characters (limit 100)")
    if subject.endswith("."):
        found.append("subject should not end with a period")
    return found


def text_problems(text: str) -> list[str]:
    """The forbidden mentions in a message or description."""
    found = []
    for line in text.splitlines():
        if WAIVER in line:
            continue
        for pattern, what in FORBIDDEN:
            m = pattern.search(line)
            if m:
                found.append(f"{what}: {m.group(0).strip()}")
    return found


def identity_problems(role: str, name: str, email: str) -> list[str]:
    """An AI agent as `role` (author or committer)."""
    if AGENT_EMAIL_RE.search(email.strip()) or AGENT_NAME_RE.search(name.strip()):
        return [f"{role} is an AI agent identity ({name} <{email}>); commit as yourself"]
    return []


def strip_trailers(text: str) -> str:
    """The message without its agent trailers (blank-line runs they leave collapsed)."""
    out: list[str] = []
    blank = False
    for line in text.split("\n"):
        if TRAILER_RE.search(line):
            continue
        if not line.strip():
            if blank:
                continue
            blank = True
        else:
            blank = False
        out.append(line)
    while out and not out[-1].strip():
        out.pop()
    return "\n".join(out) + ("\n" if out else "")
