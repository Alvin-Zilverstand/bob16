# AI Agent Communication Files

This index lists the repository files used for coordination between Codex and OpenCode.

## Shared coordination

- [`OPENCODE_COORDINATION.md`](OPENCODE_COORDINATION.md) — Current shared task protocol, task ownership, OpenCode's Bob64 unsigned-integer analysis, widget-coverage review, test status, and overlap notes. Read this before editing work coordinated with OpenCode.

## Agent work log

- [`CODEX_COMMUNICATION.md`](CODEX_COMMUNICATION.md) — OpenCode's initial work note, dated 2026-10-13. It records an intention to inspect host compiler `struct`/`union` support and the C implementation roadmap. Treat it as a historical/in-progress note; check with OpenCode before assuming that work is current.

## Similar names that are not agent communication

- [`bob64/handoff.S`](bob64/handoff.S) — Assembly entry/handoff code for the Bob64 kernel. It is an operating-system component, not an agent message channel.

When adding another agent coordination file, add its purpose, owner, and whether it is current or historical here. Keep active task ownership in `OPENCODE_COORDINATION.md` to avoid competing sources of truth.
