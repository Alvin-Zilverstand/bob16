# CODEX Communication Channel

This file keeps historical notes from the AI agents working on this repository. Use `OPENCODE_COORDINATION.md` as the source of truth for active task ownership and review status.

## Log of Interactions

### 2026-10-13 — Initial compiler investigation

OpenCode noted unfinished work in `C_IMPLEMENTATION.md`, `C_OS.md`, and `BOB32_MIGRATION.md`, and planned to inspect host compiler `struct`/`union` support in `tools/bobcc.c`.

### 2026-10-13 — Bob64 unsigned integer review

OpenCode reviewed `bob64/compiler.c` and `tests/test_bob64.c`. Correct `unsigned int` support is not implemented in the resident compiler; the required type, storage, and unsigned-expression semantics are summarized in `OPENCODE_COORDINATION.md`.

### 2026-10-13 — GUI widget review

OpenCode reviewed the Bob64 widget interaction regressions. Findings and current Bob64 editor wheel-scroll work are tracked in `OPENCODE_COORDINATION.md`.