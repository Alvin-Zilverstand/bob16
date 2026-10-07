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

Shift assignments and pointer/integer operator constraints bring the suite to
615 checks. Native execution covers destination side effects, chained assignment,
precedence and signed/zero shifts. Negative cases cover evaluated and sizeof
pointer operands for integer operators.

Composite function declaration types bring the suite to 631 checks. Native
programs combine incomplete and complete row-pointer declarations, including
additional indirection. Negative cases verify conflicting completions, element
types and pointer depths remain rejected.

Compound-assignment and unevaluated operand checks bring the suite to 671 checks.
Native tests retain row-pointer offset scaling and verify sizeof suppresses
side effects. Negative cases reject pointer difference/shift assignments and
array, literal or void targets in unevaluated modification expressions.

File-scope extern/tentative object declarations bring the suite to 695 checks.
Native tests verify one shared allocation, initialized values and relocated
pointers, completed arrays and zero-filled tentative storage. Negative tests
verify conflicting declarations, duplicate definitions and unresolved extern
references reject while preserving prior output files.

Block-scope extern objects bring the suite to 727 checks. Native execution
verifies scoped global aliases, shadowing without altering an outer local,
repeated declarations and completed array types. Rejections verify names do not
escape their declaring block/function and that conflicting/initialized block
extern declarations fail. The kernel remains 36,009 words.

Function-local static storage brings the suite to 755 checks. Native programs
verify persistence, private same-name objects, arrays/strings and static pointer
relocations. Negative cases reject runtime initializers, automatic addresses,
duplicate declarations and names escaping their declaring scope.

File-scope static declarations bring the suite to 783 checks. Native programs
combine internal objects/functions, relocated pointers and extern inheritance;
negative tests reject linkage conflicts and incomplete internal tentative arrays.

Constant sizeof initialization brings the suite to 787 checks. Native tests
exercise type/literal bounds and case labels, global initialized sizes and local
static sizeof automatic arrays and side-effect expressions without evaluation.

Parser type binding tests bring the suite to 807 checks. Native execution verifies
named sizeof bounds/case labels, array parameter adjustment, shadowing and block/
loop scope restoration. Rejection tests cover later, escaped and undeclared
bindings. Kernel compilation still produces 36,009 words.

Scoped typedef tests bring the suite to 843 checks. Native execution covers
scalar/pointer/array aliases, casts/parameters/sizeof, nested alias and object
shadowing, scope restoration and void aliases for no-argument functions.
Rejection tests verify ordinary-namespace conflicts and escaped/invalid aliases.

Enum tests bring the suite to 887 checks. Native programs cover distinct enum
objects, scoped constants/tags, explicit/automatic values, aliases, bounds and
switch labels. Negative cases verify namespace/type conflicts, invalid values
and out-of-scope or nonmodifiable constants reject.

Boolean type tests bring the suite to 899 checks. Native execution verifies
normalized storage across initialization, arrays, casts, calls, assignments and
modification; negative cases reject conflicting object/parameter declarations.

Unsigned int tests bring the suite to 915 checks. Native execution covers
high-bit comparisons, mixed signed operands, division/remainder, logical shifts,
casts and wraparound. Negative cases reject conflicting declarations and pointer
division. Conditional helper inclusion keeps the kernel at 36,009 words.

## Initial bob32 CPU migration

The widened emulator passes 42 direct CPU checks, 915 OS/compiler checks and
28 native console checks. CPU cases cover immediate sign extension, legacy and
wide overflow/flags, 32-bit load/store, B32K loading, guest boot transfer and
execution, and B16K compatibility selection. This does not establish a complete
bob32 ABI or whole-repository audit; see BOB32_MIGRATION.md for remaining work.

Trap/return-register audit fixes bring the direct CPU suite to 47 checks. New
cases verify signed compatibility return addresses, native wide return addresses,
supervised wide result preservation and a string word whose low 16 bits are zero.
The string case also requires the trap instruction register to remain intact.
The OS/compiler and console suites remain at 915 and 28 checks respectively.

Image/argument audit cases bring the direct suite to 65 checks. Subprocesses
require malformed B32K images to reject; numeric argument tests cover boundaries,
overflow and malformed strings, and a limited emulator run must return status 2.
Compatibility suites still pass 915 OS/compiler and 28 console checks.

Assembler input/data audit cases bring the direct suite to 73 checks. Cases cover
one string terminator, CRLF registers, complete/range-checked fill words, oversized
physical lines, all 65,536 slots and string overflow rejection before memory writes.
The repository demo plus 915 OS/compiler and 28 console checks pass.

Assembler operand tests bring the direct suite to 94 checks. Every immediate
operand family rejects malformed decimal text, invalid register-shaped tokens
and overflow. Positive tests preserve field clamping, relative/indirect calls
and the documented NOP encoding. Compatibility suites remain at 915 and 28.

Storage metadata/service tests bring the direct suite to 105 checks. Cases cover
descriptor arithmetic boundaries, wide-mode rejection, valid empty/text metadata,
invalid flags/kinds/lengths, content terminators and duplicate names. Existing
persistence cases retain failed-restore RAM preservation; 915 OS/compiler and
28 native console checks pass.

Wide instruction-boundary checks bring the direct suite to 107 checks. Reserved
upper bits must fault under supervision with parent state restored; sign-extended
legacy instructions remain accepted. Native ROM execution and compatibility
suites continue to pass (915 OS/compiler, 28 console).

Native storage verification: B32S round-trip preserves 0x12345678; upper-byte checksum corruption and cross-mode restore reject without changing RAM. Legacy OS save/restore remains covered by 915 OS/compiler checks. All 111 CPU/storage/assembler and 28 native console checks passed.

Host specifier audit: runtime coverage exercises typedefs, reordered function declarations/definitions, signed arrays, unsigned division and unsigned casts/right shifts. Six invalid specifier combinations are rejected while preserving existing output files. All 943 OS/compiler checks passed; regenerating the 36,009-word kernel produced the identical B16K image.

Host escape audit: runtime checks cover missing simple escapes, hexadecimal/octal 255, array length and terminator. Rejection coverage includes hexadecimal 256, a long overflowing hexadecimal sequence, octal 256, and direct unterminated string/character escapes bypassing preprocessing. Lexer failures preserve both prior output artifacts. Kernel rebuild remains byte-for-byte identical.

Storage address validation: direct service and actual trap execution reject native high-bit descriptor addresses. Changed-kernel B32S restore preserves text and marks binaries unused. All 115 CPU/storage/assembler, 967 OS/compiler and 28 native console checks passed.

Host input/lexer verification: direct malformed-source fixtures cover newline-containing string/character literals, 08, 1u2, 1_foo and a zero byte followed by otherwise ignored source. Existing assembly and binary outputs survive input failures. The full OS/compiler suite passed 981 checks, and the regenerated 36,009-word kernel matches the previous image.

Pointer ordering verification: one 4,096-element array crosses 0x8000, and all four relational operators compare its first/last elements and equality boundaries correctly. Additional runtime coverage compares compatible row pointers with incomplete/complete bounds. Invalid relational operand types, including sizeof, are rejected. All 1,005 OS/compiler checks passed; the 36,009-word kernel image remains unchanged.

Compiler output audit: seven collision cases verify preservation of the input, BASM, B16K and map, including Windows case and relative-path aliases. All 1,040 OS/compiler checks passed. An extensionless binary in build/output.audit produces image.map in that directory; the regenerated kernel image remains identical.

Void-expression verification: a runtime fixture counts effects through discarded calls, casts, comma and void conditionals. Fifteen invalid contexts cover operators, assignment, casts, sizeof, conditionals and all loop/if conditions. All 1,104 OS/compiler checks passed; the regenerated kernel image remains unchanged.

Return/argument verification: six invalid programs cover void/value return mismatches and ordinary/trap function arguments. All 1,128 OS/compiler checks passed; kernel regeneration produced the identical 36,009-word image.

Switch type verification: five invalid programs cover pointer, array and void conditions plus pointer/void labels. Runtime coverage verifies unsigned 65535, enum and _Bool dispatch. All 1,152 OS/compiler checks passed; the rebuilt 36,009-word kernel image is unchanged.

Const spelling verification: runtime fixture covers qualified globals, parameters, typedefs, enums, byte arrays, pointer qualifiers, repeated qualifiers and sizeof spellings. All 1,156 OS/compiler checks passed; the rebuilt kernel image is unchanged. These checks do not establish qualifier enforcement.

Build-tool audit: read tools/build.c in full, checked command status propagation and platform path handling, and added checked build-directory creation/type and Windows compiler PATH updates. A regular-file build entry fails before compiler invocation. Full build --test passed 115 CPU/storage/assembler, 1,156 OS/compiler and 28 native console checks. CPU snapshot tests now use a platform-specific environment helper rather than unconditional Windows calls; the POSIX branch remains unverified on a POSIX host.

Decoder verification: eight mode/instruction combinations check malformed NOP, NOT, JSRR and RET and parent register/PC restoration. All 123 CPU/storage/assembler, 1,156 OS/compiler and 28 native console checks passed.

Pointer subtraction verification: runtime cases compare enum/int element pointers and row pointers in both directions and inside sizeof. Three invalid cases reject distinct enum tags, incompatible row extents and incomplete row types. All 1,172 OS/compiler checks passed; the rebuilt 36,009-word kernel image is unchanged.

Conditional pointer verification: runtime cases verify complete/incomplete array composite sizes, zero/null branches, arithmetic zero and object/void pointer combinations. Four invalid programs reject incompatible pointers, nonzero/nonconstant integer branches and invalid sizeof operands. All 1,192 OS/compiler checks passed; the rebuilt 36,009-word kernel is unchanged.

Arithmetic helper review: multiplication replaced with a bounded word-mask loop. Runtime verification repeats 32767 squared and INT_MIN times 32767 sixty-four times, and covers zero, negative operands, INT_MIN wrap and unsigned 65535 squared. All 1,196 OS/compiler and 28 native console checks passed with the rebuilt 36,005-word kernel.

Resident multiplication verification: go product.c compiles and runs sixty-four repeated large products, including the signed minimum value expressed as -32767-1, plus negative and wrapping products. No compile error or supervised timeout occurs, and output is bob! with Exit 0. All 1,200 OS/compiler checks passed. cc_multiply is a wrapper around the shared host-generated helper, so the prior claim of a separate unchanged resident implementation has been corrected. The resident lexer still rejects decimal 32768, including the literal in -32768; broader literal typing remains open.

Resident octal verification: go accepts 010, 077 and 0177777 with expected values and prints bob!. Invalid 09 and overflowing 0200000 are followed by a successful go bob.c recovery. Existing malformed-source cases cover consolidated cc_fail handling. All 1,206 OS/compiler and 28 native console checks passed with the rebuilt 36,092-word kernel.

Code generation verification: the complete 1,206 OS/compiler checks and 28 native console checks passed after removing empty stack adjustments and compacting target-equivalent literals. The rebuilt 35,909-word kernel executes resident compilation, arithmetic, editor and storage paths successfully.

Resident escape and double-negation verification: go escapes.c checks all six additional spellings and prints bob!. Host runtime checks cover double negation of zero, pointers, negative/high-bit values and increments without duplicate effects. All 1,213 OS/compiler and 28 native console checks passed with the 36,096-word kernel.

Frame omission verification: recursion through no-parameter/no-local functions returns the expected result while a caller's automatic local remains intact. All 1,217 OS/compiler checks passed; 28 native console checks passed against the same rebuilt 36,054-word kernel.

Resident line-break verification: malformed multiline string and character literals both fail, then go bob.c succeeds. Existing literal/arithmetic cases exercise unary numeric folding. All 1,220 OS/compiler checks and 28 native console checks passed with the 36,041-word kernel.

Resident concatenation verification: go joined.c merges literals separated by a comment, an empty piece and a newline into bob!, then exits successfully. All 1,223 OS/compiler and 28 native console checks passed with the 36,071-word kernel.

Resident code limit verification: a source that fits the RAM text file but emits more than 511 words fails compilation into an existing output name. The previous executable runs without fault, and a subsequent go bob.c succeeds. All 1,227 OS/compiler checks and 28 native console checks passed with the 36,080-word kernel.

Signed division verification: repeated INT_MIN/1, INT_MAX/1 and INT_MAX/-1 calculations complete; all operand sign combinations and remainder signs are checked. All 1,231 OS/compiler checks and 28 native console checks passed with the 36,093-word kernel.

Assembly prompt audit: oversized filename input now drains through its line ending before retrying, rather than treating the leftover text as another filename. A 300-character input followed by valid BASM recovers and prints bob!. All 125 CPU/storage/assembler, 1,231 OS/compiler and 28 native console checks passed.

Constant type verification: four static initializers reject void/value conditional branches, void logical operands despite short-circuiting, and casts from void to a value. All 1,247 OS/compiler checks passed, and kernel regeneration matches the current 36,093-word image.

Native input/run boundary verification: actual trap execution rejects a high-bit entry address, and supervised input calls reject high-bit address/capacity arguments without modifying the destination. All 128 CPU/storage/assembler, 1,247 OS/compiler and 28 native console checks passed.

Image/source I/O review: readWord now distinguishes read error from truncated input. bootImage checks stream error and close status after checksum/exact-length validation; assemble checks close failures before announcing execution. Diagnostics identify both word-width formats. Existing truncated/corrupt image and assembly checks, plus all 128 CPU/storage/assembler, 1,247 OS/compiler and 28 native console checks, passed. Read/close failures have not been fault-injected.

Native disk argument verification: subprocess fixtures individually set upper bits in destination, offset and count and fail at trap execution. Existing B32K boot/transfer checks still pass. All 131 CPU/storage/assembler, 1,247 OS/compiler and 28 native console checks passed.

Windows console input audit: terminalKey now checks GetConsoleMode and SetConsoleMode results before using the mode, and returns EOF rather than zero when ReadConsoleInput stops without a key. This avoids an uninitialized mode and ambiguous failed-read result. All 28 native console and 1,247 OS/compiler checks passed; Windows API failure paths have not been fault-injected.

Boundary evidence refinement: native disk argument subprocess cases now verify the intended fault diagnostic, excluding unrelated process failures. Assembly filename handling accepts a full 255-character name followed by CRLF; an adjacent oversized line is still consumed before valid BASM execution. All 135 CPU/storage/assembler, 1,247 OS/compiler and 28 native console checks passed. CRLF tests ran on Windows; untranslated POSIX input remains unverified on its native host.

Assembly source boundary audit: a 255-character source line followed by CRLF is accepted without losing the following .fill instruction. A 256-character line still fails. All 137 CPU/storage/assembler, 1,247 OS/compiler and 28 native console checks passed.

Pointer equality verification: runtime coverage checks symmetric object/void and null comparisons, arithmetic zero, sizeof and self equality. Four invalid programs reject nonzero integers, ordinary zero-valued variables, incompatible pointers and invalid sizeof operands. All 1,267 OS/compiler checks passed; the regenerated 36,093-word kernel image is unchanged.

Assignment compatibility verification: runtime cases check object/void pointer assignments, null arithmetic constants, _Bool normalization and sizeof. Four invalid assignments are rejected. Kernel source conversion is explicit and regeneration matches the current image. All 1,287 OS/compiler checks passed.

Return conversion verification: runtime cases check object/void pointer returns, arithmetic null returns and pointer-to-_Bool normalization. Three invalid programs reject nonzero integer, incompatible pointer and implicit pointer-to-integer returns. All 1,303 OS/compiler checks passed; the regenerated kernel image is unchanged.

Function argument conversion verification: a runtime program checks compatible object/void pointers, arithmetic null constants and pointer-to-_Bool normalization. Three invalid programs reject nonzero integer-to-pointer, incompatible pointer and pointer-to-integer arguments. All 137 CPU/storage/assembler, 1,319 OS/compiler and 28 native console checks passed. Kernel binary hash is unchanged after generic memory API signatures and explicit text casts.

Initializer conversion verification: nine invalid programs cover automatic and global pointers, pointer arrays, block-static objects, pointer-to-integer initialization and void-valued initialization. A runtime fixture covers valid local/global/array/static object pointers, object/void conversions, null constants and pointer-to-_Bool normalization. All 1,359 OS/compiler checks passed. The rebuilt kernel binary hash matches the existing 36,093-word image.

Trap argument conversion verification: seven invalid sources reject nonzero integer text pointers, incompatible text/descriptor pointers, pointer capacities, pointer characters and implicit pointer entry addresses. A runtime fixture passes a void pointer to bob_puts and prints bob!. All 1,391 OS/compiler checks passed; regenerated kernel hash matches the existing image.

Unevaluated call verification: five negative sources reject wrong parameter counts, nonzero integer pointers, incompatible pointers and invalid declared trap arguments inside sizeof. A runtime fixture verifies valid object/void pointer arguments and increment expressions are not evaluated. Existing early empty-list/later definition cases still pass. All 1,415 OS/compiler checks passed, and the regenerated 36,093-word kernel hash is unchanged.

Subscript constraint verification: four invalid programs reject void-valued index operands in both orders and indexing through void or incomplete-array element pointers inside sizeof. A runtime fixture verifies unsigned reversed indexing, row size and scalar element size in multidimensional arrays. All 1,435 OS/compiler checks passed; regenerated kernel binary hash is unchanged.

Nested sizeof/address verification: eight invalid programs cover inner void size, invalid call arguments, invalid indexing/assignments, and address-of constants, arithmetic, increments and casts. Runtime fixtures verify nested sizeof suppresses increments and pointers to string-literal arrays preserve full array size and contents. All 1,475 OS/compiler checks passed. The regenerated kernel hash is unchanged at 36,093 words.

Static string address verification: a runtime fixture checks global and block-static pointers to whole literal arrays, their complete sizes and terminators, a string-element address, and scaled one-past pointer arithmetic. All 1,479 OS/compiler checks passed. Kernel regeneration produces the same 36,093-word binary hash.

Conditional static relocation verification: runtime cases cover true/false object address selection, null selection, sizeof-based string selection and nested block-static pointer selection. Negative sources reject variable conditions and incompatible pointer branches. All 1,491 OS/compiler checks passed. Kernel binary regeneration matches the existing 36,093-word image.

Numeric pointer constant verification: a runtime fixture compares initialized pointer-to-array values against runtime arithmetic for forward/reversed addition, subtraction and pointer difference at guest addresses above 0x8000. All 1,495 OS/compiler checks passed. The regenerated 36,093-word kernel binary hash is unchanged.

Unary code-generation verification: three negative programs reject prefix/postfix increments of void lvalues and array increment. Existing runtime fixtures for numeric unary operators, pointer/Boolean increments and literal-array addresses still pass. All 1,507 OS/compiler checks passed. The regenerated 36,093-word kernel hash is unchanged.

Static Boolean address verification: a runtime fixture checks implicit object/string addresses, explicit Boolean address casts, null pointers, Boolean arrays and block-static address initialization. All 1,511 OS/compiler checks passed; regenerated kernel hash matches the existing 36,093-word image.

Relocation boundary verification: a generated source with large static arrays reaches the 65,536-word emission limit while adding pointer relocations. Compilation rejects with the intended code/relocation diagnostic and preserves existing BASM and binary outputs. All 1,516 OS/compiler checks passed. Kernel regeneration matches the existing 36,093-word binary hash. The fixture exercises code-capacity rejection through relocation emission; independent relocation-capacity saturation is not claimed.

Long literal verification: a generated guest program compiles a single 9,000-character literal and checks its last character and terminator before printing bob!. Existing escape, embedded-NUL, malformed literal and code-limit fixtures still pass. All 1,520 OS/compiler checks passed; regenerated kernel hash matches the existing 36,093-word binary. Allocation failure is checked in code but was not fault-injected.

Adjacent literal scaling verification: a generated program joins 9,000 adjacent one-character literals and verifies the final character and terminator before printing bob!. Existing embedded-NUL and ordinary concatenation fixtures remain passing. All 1,524 OS/compiler checks passed. The kernel binary hash is unchanged at 36,093 words. Linear allocation/copy behavior follows from the implementation; no host peak-memory benchmark was recorded.

Integer constant expression type verification: three invalid sources reject pointer-valued expressions used as array bounds, explicit enum values and array designator indexes. All 1,536 OS/compiler checks passed. Kernel regeneration matches the existing 36,093-word image.

Jump bookkeeping verification: host compiler rebuild and kernel regeneration pass after routing jump records through a checked append helper. All 1,536 OS/compiler checks passed; regenerated kernel hash matches the existing 36,093-word image. The explicit jump-table overflow guard is defensive because token/code limits generally fire earlier.

Generated-label bookkeeping now checks the signed serial before incrementing, so future larger outputs fail with a defined diagnostic instead of overflowing label names. The existing 65,536-word code capacity generally rejects first; a dedicated serial-saturation fixture would require constructing a larger-than-emittable AST and was not added.

Switch-context validation now rejects case/default labels outside switches during parsing and caps nested switches at 64 before recursive parsing. File-scope and function-scope invalid labels are covered. All 1,540 OS/compiler checks pass; legal labels later in a switch body remain accepted regardless of earlier break statements.

Integer constant expression syntax is now enforced for array bounds, enumerators, designator indexes and case labels. New negative inputs cover calls and side effects in bounds, designators and cases. The host OS/compiler suite passes all 1,556 checks, and the compiled kernel remains 36,093 words.

ICE type validation now walks casts, unary/binary operands and conditional operands, preventing void or pointer operands from being laundered through sizeof or an integer cast. Added array-bound and enumerator regressions for sizeof of a void cast. The full OS/compiler suite passes 1,564 checks; kernel image size and hash remain unchanged.

Explicit unsigned enum values whose result is outside signed 16-bit int range now fail instead of wrapping into a negative enumerator. The suite includes the 32768U rejection; existing negative and positive signed boundary enums continue to compile. All 1,568 OS/compiler checks pass, and the kernel image is unchanged.

Assembler input now reads bounded physical lines byte by byte and rejects embedded NUL bytes, which fgets/C-string parsing previously treated as end-of-line. CRLF, exact 255-character lines, final lines without newline, and overlong lines remain covered. The direct CPU/assembler suite passes 138 checks, including an embedded-NUL input; OS/compiler suite passes 1,568.

The assembler's `not` instruction now validates exact arity and requires a valid register in the two-register form. Previously `not r0 r8` silently encoded as `not r0`, and a fourth token was ignored. New cases cover invalid/missing/extra operands. Direct suite: 141 checks passed; OS/compiler suite: 1,568 passed.

Load/store register validation now rejects all negative `parseReg` results, not only the missing-token sentinel. Invalid register names previously reached negative shifts and malformed encodings. Added malformed-name and out-of-range cases for LD/LDI/LDR/ST/STI/STR operands. Direct CPU suite: 150 checks passed; OS/compiler suite: 1,568 passed.

The assembler now rejects trap vectors above 11, matching the emulator's implemented trap table; values 12–15 previously assembled and then faulted at runtime. Direct tests cover vectors 12 and 15. Reserved register-form ADD/AND bits also fault in both CPU modes. CPU suite: 156 passed; OS/compiler suite: 1,568 passed.

Host expression parsing now rejects nesting beyond 256 calls, protecting the compiler process from deeply nested parentheses/unary/right-associative expressions exhausting its C stack. A generated 400-level array-bound source verifies the diagnostic. OS/compiler suite: 1,573 checks passed. Recompiled kernel remains 36,093 words and matches SHA-256 F4B1175B84A8BFE2ABFB192D51CBA1E000D7EDDB52B9CECC2C4306E74BF0A4AF.

Host const scalar enforcement now tracks const declarations and typedef aliases through locals, globals and scalar parameters. Assignments and increments reject modification, including within sizeof and array-bound constant-expression parsing. Pointer, array and pointee qualification still need full type-system support. The suite passes 1,601 checks; regenerated kernel remains 36,093 words.

Direct const pointer declarators and top-level const pointer typedef aliases now retain their object qualifier, so pointer assignment and increment reject for globals, locals and parameters. `typedef const int *P` remains assignable as a pointer, confirming the pointee qualifier is distinct; enforcing pointee and array-element const remains incomplete. The OS/compiler suite passes 1,629 checks.

Pointer types and array types now retain const qualification. Assignments/increments through `*p` and `p[i]` reject for const pointees/elements; adding const to a pointer conversion is allowed, while dropping it rejects recursively through pointer levels, address-of, and conditional expressions. `bob_puts` now correctly accepts a const string. Function redeclaration checks preserve pointed-to qualifiers while ignoring top-level parameter qualifiers. Some nested qualified-declarator forms remain unfinished. OS/compiler suite: 1,697 checks passed.

Native program counters and effective addresses now retain 32 bits, with lazy sparse pages above the existing 64K-word low-memory array. JMP/JSRR/RET/PUTS preserve high addresses; LDR/STR and LDI/STI operate there, supervised programs can read/write high data, and GETS/boot-disk writes accept high destinations. B16 mode still wraps at 16 bits. B32K v2 adds 32-bit image origin, entry, length and checksum fields and can boot at any address, including the top two addressable words. Version 1 and B16 images retain their old format. Tests cover malformed v2 headers, payload checksums and command-line high-origin boot. CPU suite: 178 checks; OS/compiler: 1,697; console: 28. The current image payload remains limited to 65,536 words, and the C compiler/OS still target bob16.

The host C compiler now accepts `--wide` to emit native bob32 instructions as B32K v2 images. Wide mode supports 32-bit integer literals, data, arithmetic helpers for multiply/divide/modulo, signed and unsigned comparisons, shifts and bitwise operations, and accepts long/long long as 32-bit aliases with standard integer literal suffix order validation. The word-addressed target uses one C byte per word, so sizeof reports words and remains consistent with current array and pointer layout. Default bob16 output retains its old format. Wide output emits only functions reachable from main, following explicit calls, operator helper dependencies, array indexing, and bob_address references. A helper-free guest compiles to 23 words. `--wide-app` emits B32K v2 images at the supervised app origin, and the bob32 shell's `run32 PATH` validates the image before loading it into high memory; sparse pages are preflighted so allocation failure leaves the app region untouched. A compatibility audit found that bob16 apps can overwrite the tail of the low-address B32 kernel; supervised return now restores any overlapping kernel image words. The B32 resident compiler also emits kind-2 native programs at `0x10000`, using 32-bit literals and arithmetic, and B32S snapshots preserve these program files. It supports right-associative `?:` with selected-branch-only evaluation and `do/while` with `break`/`continue`; resident regressions check nested conditionals, skipped side effects, first-iteration behavior, early exits and continue-to-condition behavior. The bob16 compiler retains its previous behavior because its kernel image is already at the memory limit. Regression coverage also compiles and runs addition, multiplication, division, remainder and local-variable arithmetic, retains the legacy kind-1 bob16 execution path, and snapshots native files. End-to-end tests cover direct `.org` BASM execution, missing-image recovery, B32S save/restore, shell editing and 8-digit hex output. Full C conformance remains incomplete. Current suite: CPU 179, OS/compiler 1,745, console 28.

Resident B32 assignment expressions: right-associative chained assignment returns the stored value, compound assignment works inside expressions, and `<<=`/`>>=` are recognized and executed. Shell integration uses short editor input lines (below the command limit), frees source slots after each run, and asserts all nine successful B32 shell programs. bob16 image remains 36,093 words. Full suite: CPU 189, OS/compiler 1,746, native console 28.

Supervised-store boundary audit found that the B32 condition rejected writes in `0x100000..0x10FFFF` but accidentally allowed all later sparse-memory addresses. The guard now enforces `[0x10000,0x100000)` for native apps and `[0x9000,0xE000)` for B16 legacy apps. B32 legacy apps use `[0xB000,0xE000)`. Direct CPU regressions accept the final B32 app word and reject below-range, end, former-hole, arbitrary-high and maximum addresses without modifying them. Suite: CPU 189, OS/compiler 1,746, console 28.

B32 external-image allocation is now transactional: missing sparse pages are allocated into temporary buffers before any are installed or image words are copied. Allocation failure returns the documented loader error rather than taking the emulator's non-program machine-fault exit. A test-only allocator fault forces this failure and confirms the target page remains absent and unchanged. Suite: CPU 187, OS/compiler 1,746, native console 28.

Nested trap-6 audit found that a rejected recursive run cleared `application32` while the outer native app kept running. The trap now returns `-1` before changing execution mode. A CPU fixture executes nested trap 6, then stores to valid high app memory and exits successfully. Suite: CPU 187, OS/compiler 1,746, console 28.

The expanded bob32 kernel overlapped the legacy program region at `0x9000`. The bob32 OS now loads kind-1 legacy apps at `0xB000..0xBFFF`, leaves the 16-bit bob16 location unchanged, and rejects a legacy run if the active B32 kernel occupies that window. This also removes the old post-run image restore that reset RAM filesystem metadata. Snapshot descriptor validation now allows B32 kernel tables below `0xB000` while retaining the old bob16 ceiling. CPU fixtures run a 16-bit `LEA/PUTS/HALT` program and reject a low legacy entry; OS integration checks `bob!`/exit 33, file listing after execution, and snapshot save/restore. Suite: CPU 189, OS/compiler 1,746, console 28.

Resident B32 functions now use per-call stack frames, parameter offsets, local slots, forward-call patch chains, and a common return epilogue. The shell regression calls a later-defined no-argument function as a statement, calls a two-argument function for its value, and computes factorial recursively; it prints `bob!` and returns 0. The test runner counts all nine successful B32 shell programs. Suite: CPU 189, OS/compiler 1,746, console 28.

Resident B32 frame-offset audit found that local slot numbers were masked to six bits when encoded in LDR/STR instructions. A sufficiently large function with many sequential inner-scope declarations could silently access the wrong stack word after slot 32. The compiler now rejects a 33rd local slot, and a shell regression generates 33 block-scoped declarations to verify rejection. B32 kernel rebuild: 43,113 words; OS/compiler suite: 1,749 checks passed.


The B32 kernel had only 70 words of headroom before the legacy-app region after adding local arrays. Since RAM files are limited to 511 words, the kind-1 B32 legacy window moved to `0xB800..0xBFFF` and was reduced to 2,048 reserved words, still leaving over three times the largest file size. The emulator's entry/store/GETS bounds, OS program loading, snapshot table ceiling, direct CPU fixture and memory-map docs now agree. The kernel ends at `0xAFBA`, leaving 2,118 words before the B32 legacy window. CPU boundary fixtures verify writes at `0xB800` and rejection below it. Full suites: CPU 191, OS/compiler 1,752, console 28.

Resident bob32 local arrays are fixed-size, one-dimensional `int` objects within the 32-slot frame budget. The compiler zero-initializes elements, emits indexed loads, and supports indexed assignment statements. A shell regression fills and sums an array in loops and verifies `bob!`/exit 0. At that stage array parameters, explicit initializers, multidimensional arrays and bob16 resident arrays remained unsupported. Full suites: CPU 191, OS/compiler 1,752, console 28.

Indexed bob32 array statements now also support postfix increment/decrement and compound assignments, including shifts implemented through helper calls. The regression increments element zero, adds to element one and shifts element two before summing. The B32 kernel is 44,833 words, ending at `0xB221` with 1,503 words before the `0xB800` legacy-app region. Full suites: CPU 191, OS/compiler 1,752, console 28.

Fixed-size bob32 local arrays now accept brace initializer lists containing scalar expressions and an optional trailing comma. Omitted elements retain the zero fill emitted for automatic arrays; an excess element triggers a compile error. The shell fixture validates explicit values and zero-filled tail elements before exercising indexed mutation, then confirms an oversized initializer is rejected. The rebuilt B32 kernel is 45,070 words and ends at `0xB30E`, leaving 1,266 words before the legacy-app region. OS/compiler suite: 1,753 checks passed.

Indexed bob32 array prefix/postfix increment and decrement now work inside expressions. The generated code preserves the old value for postfix, stores the updated element, then returns the old value; prefix returns the new value. A separate compiled app verifies both value rules at runtime. It is kept separate from the larger array loop test so both remain below the 511-word RAM-file cap. The kernel is 45,299 words, ending at `0xB3F3` with 1,037 words before the legacy window. OS/compiler suite: 1,754 checks passed.

The bob32 resident parser now accepts comma-separated local scalar and fixed-array declarators, each with its own initializer. Runtime coverage combines an initialized array and scalar, and initializes a scalar from an array postfix expression in the same declaration. The 45,331-word kernel ends at `0xB413`, leaving 1,005 words before the `0xB800` legacy window. OS/compiler suite: 1,754 checks passed.

The bob32 resident parser now accepts `int` function prototypes with named or unnamed integer parameters, permits unused declarations without definitions, and checks prototype/definition and prototype/call argument counts. A shell integration compiles a prototype-before-definition call and an unused unnamed-parameter declaration, runs it, and rejects conflicting prototypes. The 45,486-word kernel ends at `0xB4AE`, leaving 850 words before the `0xB800` legacy window. OS/compiler suite: 1,758 checks passed.

Resident bob32 C now supports local function pointers returning `int` and accepting integer arguments. A pointer can be initialized from a function declared by prototype and called indirectly; the integration fixture checks the result, `bob!` output and exit code. The kind-1 compatibility window moved to `0xBA00..0xBFFF` because the kernel reached 46,733 words (`0xB98D` end), leaving 115 words for growth. Emulator bounds, guest loader, storage limit, CPU fixtures and memory-map docs were updated together. Suites: CPU 191, OS/compiler 1,761, console 28.

Function-pointer initialization and assignment now require a previously declared function with a matching argument count; compatible local function pointers can be copied, and null `0` is supported. Mismatched signatures and integer values are rejected. Kernel growth required moving bob32 compatibility apps to `0xC000..0xCFFF` and the B32 heap to `0xD000..0xDFFF`; bob16 applications move to `0xA000` so its enlarged kernel still fits, with only its dedicated `0x9800..0x980F` compiler-variable area additionally writable by supervised apps. B16 image limits, storage descriptor checks, shell heap bounds, docs and CPU/OS regressions were aligned. Current images: B16 36,325 words (ends `0x90E4`); B32 48,185 words (ends `0xBF38`). Suites: CPU 194, OS/compiler 1,764; console: 28 checks passed.

Resident bob32 now distinguishes `int f()` from the zero-argument prototype `int f(void)`. A no-prototype declaration can be refined by a typed declaration or later definition; calls before that definition are checked against its eventual parameter count to preserve this calling convention. End-to-end tests cover a valid call through `int loose()` and rejection when a one-argument call precedes a two-argument definition. B32 image: 48,185 words, ending at `0xBF38` with 199 words before the compatibility window. Full suites: CPU 194, OS/compiler 1,764, console 28.

The bob32 resident compiler now recognizes `print`, `println`, `print_dec` and `print_hex` as kernel functions without requiring source prototypes. A regression runs `write bobs.c int main(void){println("Bob!");return 0;}` followed by `go bobs.c` and verifies `Bob!` with exit status 0. This kernel growth moves the bob32 compatibility-app window to `0xC200..0xCFFF`; the emulator protects that window from writes below it. Current B32 image: 48,645 words, ending at `0xC164`.

The bob32 resident compiler now stages generated code at the native application origin and can save output up to the available packed filesystem capacity; bob16 retains its 511-word code limit. A shell integration compiles a compact source into a 1,289-word resident app, runs it to print `bob!`, snapshots the filesystem, restores the snapshot, and runs the stored app again. Full suites: CPU 203, OS/compiler 1,822, console 28.

The bob32 resident compiler now adjusts one-dimensional `int values[]` (and fixed-bound parameter spellings) to a single word-addressed pointer argument using the existing stack ABI. Helpers can index and mutate caller arrays, forward an array to nested helper calls, and combine it with scalar parameters. Regression coverage computes a sum, increments elements in a second helper, calls the sum helper repeatedly, and prints `bob!`; negative cases reject scalar/array argument mismatches, conflicting prototypes, and multidimensional parameter syntax. Full suites: CPU 203, OS/compiler 1,825, console 28.

Added app-side `bob_string.h` helpers for length/copy/compare/concatenate/search, whitespace checks, signed decimal and hexadecimal parsing with overflow reporting, integer formatting with capacity checks, and in-place quoted command tokenization. A bob32 runtime fixture covers integer limits, malformed input, quoting and escapes; a compact native app imports into bob32 RAM files, prints `bob!`, survives a B32S snapshot restore, and runs again. Full suites: CPU 203, OS/compiler 1,840, console 28.

The host-compiled C workflow now includes `<stdbool.h>` backed by the tested `_Bool` type. B16 and B32 guest fixtures verify `bool`, `true` and `false` at runtime and print `bob!`. GCC `-fanalyzer` reported no diagnostics in the emulator, compiler or test sources. At this point suites were CPU 194, OS/compiler 1,769, console 28.

Added `<stddef.h>` with width-matched `size_t`/`ptrdiff_t` and `NULL`. `sizeof` now has unsigned type in expression and type-name forms. Native B16/B32 fixtures verify array size, pointer subtraction, null initialization and unsigned `sizeof` arithmetic. Full suite: CPU 194, OS/compiler 1,773, console 28.

Host C now supports typed function-pointer declarations and calls, including local/file-scope pointers, nested pointer parameters, function-address relocations, copying, and parenthesized indirect calls. Empty `()` signatures accept unspecified call arguments; typed signatures enforce argument count and compatibility. B16/B32 positive and negative regressions cover these behaviors, including invalid casts between function and object pointers. Full suite: CPU 194, OS/compiler 1,793, console 28.

Native bob32 application workflow: the RAM filesystem packs eight file slots into 4,096 words, allowing validated B32K v2 images larger than 511 words. `import32 HOST_PATH NAME` stores an image, `run NAME` or `run32 NAME` executes it from guest RAM, and B32S v2 snapshots preserve it; B32S v1 fixed-slot snapshots convert on restore. Integration tests import a 629-word image, execute it through both guest-name commands, save/restore, and execute again. Failure coverage checks missing, invalid and truncated images, storage-capacity exhaustion, checksum corruption and sparse-memory allocation failure. The expanded bob32 kernel now runs at `0x10000..0x1D115`, with native applications starting at `0x20000`. Resident compiler diagnostics include token context with the existing source location. Full suite: CPU 203, OS/compiler 1,813, console 28.
