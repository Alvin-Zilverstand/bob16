# OS completion checks

The C test runner is `tests/test_os.c`. Compile it with GCC and run it from the
repository root, or use `build.exe --test` with the emulator closed. Its current
suite prints `bob!` and its assertion count when all pass. Guest output is
captured for assertions; no guest examples print hello world.

| Requested feature | Implementation | Runtime evidence |
| --- | --- | --- |
| Console print/println, decimal/hex, bounded input | `kernel/runtime.c` | `console_and_commands`, `input_bounds`, exact -32768/32767/zero/0xFFFF checks in `host_compiler_and_runtime` |
| strlen/strcmp/memcpy/memset | `kernel/runtime.c` | Empty/equal/differing strings, filled/copied arrays, mutation and pointer access in `host_compiler_and_runtime` |
| Repeating bob> command loop | `kernel/kernel.c` | Multiple commands and prompts across all shell cases; EOF exits cleanly |
| help/echo/clear/mem/peek/poke/halt | `kernel/shell.c` | `console_and_commands` checks help, echo, ANSI clear, memory totals, reads/writes, protected addresses and successful halt |
| Reserved heap and allocation | `kernel/runtime.c` | `allocator_and_files` checks zeroing previously nonzero RAM, consecutive addresses, free counts, invalid sizes and complete exhaustion |
| RAM files with list/read/write | `kernel/files.c` | `allocator_and_files` checks create/replace/read/list, table exhaustion, name limits, exact 511-character capacity and cancelled overlong edits |
| Program load/run/return | `kernel/shell.c`, trap 6 in `main.c` | `program_loading_and_recovery` checks raw native words, return value, halt returning to shell, illegal instructions, protected writes, rejected input/disk services, and infinite-loop timeout |
| C code running inside emulator | `kernel/compiler.c` | `resident_compiler` invokes shell cc then run, verifies bob!, variables, arithmetic, while, if/else, comments, malformed input, depth bounds, preservation of an existing output and numeric APIs |
| Rebuilt emulator and required features | `main.c`, `bob.exe` | The supplied executable passes the suite; traps 5/6 supply EOF input and supervised execution |
| Only C/BASM tooling | `tools/build.c`, `tools/bobcc.c`, `tools/arithmetic.c`, `tests/test_os.c` | GCC builds tools and emulator; C compiler emits both BASM and binary images; Python sources/dependencies removed |
| Markdown documentation | `C_OS.md`, `kernel/API.md` | Run/build instructions, command reference, resident C examples, supported language limits, memory map, filesystem/allocator lifetime and execution statuses |
| Expanded resident C syntax | `kernel/compiler.c` | `compiler_extensions` checks for, break/continue, increments, compound assignment, bitwise/shift operations, short-circuit side effects and precedence |
| Editing existing files | `kernel/shell.c` | `editor_and_aliases` checks opening existing content, insert/replace/delete, numbered display, undo, save/discard, invalid lines and refusal to edit binary programs |
| ls/dir aliases | `kernel/shell.c` | `editor_and_aliases` lists existing RAM files through both aliases |
| File copy/rename/delete | `kernel/files.c` | `file_management` checks text and native programs, destination collision protection, full storage, slot reuse, names and malformed arguments |
| Compile and run workflow | `kernel/compiler.c`, `kernel/shell.c` | `compile_workflow` checks default/explicit outputs, source preservation, line diagnostics and no stale execution on errors |
| Startup/help/editor usability | `main.c`, `kernel/kernel.c`, `kernel/shell.c` | `usability` boots via --os, runs bob.c, exercises per-command help, unsaved quit protection, explicit discard, line navigation and undo/redo |

`basm_route` additionally starts the supplied emulator at its ordinary filename
prompt, passes `kernel.basm`, compiles/runs bob.c and checks return to the shell.
It also checks the original `demo.basm`. `image_validation` corrupts/truncates
binary images and verifies rejection before boot.

These checks establish this starter's functionality, not conformance to all of C
or isolation between processes. The resident compiler is a documented subset;
RAM storage is volatile, allocation has no free, and multitasking is absent.
