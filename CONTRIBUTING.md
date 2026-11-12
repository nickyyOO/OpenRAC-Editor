# Contributing

OpenRAC Editor is a fork of Godot 4.7.2 for OpenRAC's level editor. Keep changes small and mark
each source change with an `OpenRAC Editor:` comment. Commits follow Conventional Commits
(`type(scope): summary`).

## AI tools

Work prepared with an AI agent is fine; what enters the history is yours:

- No agent trailers (`Co-Authored-By:`, `Claude-Session:`) in commit messages, pull request
  descriptions or review comments. The one co-author line allowed credits a person, by their
  GitHub noreply address: `Co-authored-by: login <12345+login@users.noreply.github.com>`.
- No AI tool footers ("Generated with ...") and no links to AI chat or agent sessions
  (claude.ai, chatgpt.com): sessions are private, this repository is public.
- Author and committer are you, never an AI agent identity (an anthropic.com or openai.com
  address, Claude, Copilot or another bot).

The `ai-rules` workflow (`scripts/check-ai.py`) checks every pull request and push. Run the same
check on your own commits with the hook:

```sh
ln -sf ../../scripts/commit-msg.py .git/hooks/commit-msg
```

It drops agent trailers from your message and rejects the rest. A line that has to name what the
rules forbid (a test of the rules) carries `ai-rules: allow`.
