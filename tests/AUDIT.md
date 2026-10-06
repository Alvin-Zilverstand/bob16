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
| Persistent files | `storage.c`, trap 7, `kernel/files.c` | `persistence` checks explicit replacement, restart with text/programs, BASM/binary identity, checksum/metadata/truncation rejection with RAM preservation, changed-kernel text recovery, unavailable storage and program denial |
| Shell editing/history/completion | `kernel/input.c`, traps 8/9 | `shell_editing` feeds key sequences for history limits/drafts, cursor edits, deletion, unique/ambiguous completion and overlong input recovery; redirected output has no redraw |
| Native Windows console | `tests/test_console.c` | 16 checks use a hidden, test-owned console and real Windows key events to verify redraw, cursor edits, recall, completion, narrow width, maximum input and recovery after truncation |
| Number/storage feedback | `kernel/shell.c`, `kernel/files.c` | `shell_editing` checks unsigned decimal addresses/values, overflow rejection, invalid sizes and used-slot totals |

`basm_route` additionally starts the supplied emulator at its ordinary filename
prompt, passes `kernel.basm`, compiles/runs bob.c and checks return to the shell.
It also checks the original `demo.basm`. `image_validation` corrupts/truncates
binary images and verifies rejection before boot.

These checks establish this starter's functionality, not conformance to all of C
or isolation between processes. The resident compiler is a documented subset;
Files are volatile until save, allocation has no free, and multitasking is absent.

## UX goal completion audit

Final delivery was rebuilt with `build.exe --test`: 231 regression assertions
and 28 real Windows console assertions pass on the delivered `bob.exe`.
The generated kernel is 36,009 words at `0x0300`, ending below `0x9000`.

| Goal requirement | Verified evidence |
| --- | --- |
| 1. Simple startup/welcome | `usability` boots --os; `basm_route` boots kernel.basm through the original prompt; welcome offers help/go |
| 2. History/editing/completion/prompts | `shell_editing` and `test_console.c` verify four-entry recall, drafts, insertion/deletion, cursor movement, unique/ambiguous completion, redirected input, narrow redraw and input limits |
| 3. Discoverable help/errors/examples | Help lists commands and keys; command_help covers every shell command; `usability` exercises help and unknown names; tests cover recovery advice |
| 4. Editor navigation/undo/save/discard/loss protection | `editor_and_aliases` and `usability` verify numbered lines, :p N, changes, undo/redo, save, safe :q, explicit :q! and rejected invalid edits; documented EOF behavior |
| 5. Files/details/limits | `file_management` verifies copy/rename/delete for text/programs, collision rejection, full-table rename, slot reuse; ls/dir show kinds/lengths/used slots |
| 6. C workflow/diagnostics/examples | `compile_workflow` verifies defaults, go, source preservation, first-error line/column and no stale execution; supplied bob.c prints bob! |
| 7. Number/address feedback/protection | Signed/raw decimal and hex limits, overflow, protected poke and allocation feedback checked in console/allocator/shell_editing cases |
| 8. Persistent save/load | `persistence` verifies save/restore across sessions, storage location override, RAM preservation on failure, binary compatibility and text recovery after kernel change |
| 9. EOF/malformed/oversized/fault recovery | Input, editor, compiler, boot-image and program-recovery tests verify bounds/EOF/faults/timeouts while retaining the shell or unrelated files as applicable |
| 10. Docs/tutorial | C_OS.md supplies startup, keys, command/editor references, go bob.c and restart/save tutorial; API and this audit describe contracts/limits |
| 11. Native verification/delivery/cleanup | Build regenerates bob.exe, C tools, kernel.basm/B16K; complete C suites pass; temporary staged binaries, fixtures and logs removed after verification |

Interactive shell keys target native Windows console handles. Completion
operates at the line end and undo retains one edit. Full C is not claimed.

## Nano-like editor update

Interactive `edit` now runs `kernel/nano.c`, with direct typing, two-dimensional
cursor movement, scrolling, visible shortcuts, Ctrl+O save, Ctrl+X safe exit,
Y/N/Cancel and Ctrl+Z undo/redo. Redirected tests retain the line fallback.
The extended native console suite checks multiline insertion/navigation,
save/exit, confirmation, cancellation, discard, undo/redo, 511-character limits
and full-slot save failure without losing edits. Shared scratch/history were
relocated into reserved RAM; all compiler and fallback regression cases still
run against the resulting kernel.

## Host C compiler expansion

The current OS/compiler regression suite passes 503 checks. New native programs
cover multidimensional storage and sizeof, row pointer scaling, nested/partial
initializers, character-array rows and static nested address/pointer-array
relocations. Negative cases verify malformed or excessive initialization is
rejected while preserving earlier output files. These checks cover the host
compiler additions; the resident compiler retains its documented smaller subset.
The generated kernel remains 36,009 words.

Array-designator additions bring the regression suite to 543 checks. Tests execute
native bob16 programs using selected scalar/row/pointer elements, inferred bounds,
continuation and replacement. Malformed and out-of-bounds designators must reject
without replacing previously generated BASM or binary files.

Recursive object/abstract declarators bring the suite to 583 checks. Native tests
exercise pointer-to-array parameters/globals/locals, arrays of row pointers,
grouped names, casts and sizeof array type names. Rejections verify incomplete
pointee sizing/arithmetic, arrays of void, invalid casts and incompatible row
pointer declarations. Kernel compilation still produces 36,009 words.
