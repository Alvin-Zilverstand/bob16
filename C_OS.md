# bob16 OS

The kernel, shell and resident C compiler run as native bob16 instructions.
All project code is C or BASM. No Python or Python packages are used.

## Run now

Start the supplied `bob.exe`, then enter `kernel.basm` at its source-file prompt.
The kernel prints `bob!` and opens `bob>`. Type `help` for commands.
If the emulator starts in another directory, enter the full path to `kernel.basm`.
The supplied emulator has been rebuilt for the new kernel services; older
executables lack the character-input and supervised-program traps.

Alternatively run `bob.exe --boot build/kernel.b16` from the repository root.
The updated emulator also accepts `bob.exe --os` from that directory to boot
the supplied image directly. The welcome message points to `help` and `go bob.c`.

## Commands

| Command | Behavior |
| --- | --- |
| `help` / `help COMMAND` | Show commands or detailed usage and examples |
| `echo bob!` | Print text |
| `clear` | Clear an ANSI-capable terminal |
| `mem` | Show heap usage and memory regions |
| `peek 0xC000` | Read a word |
| `poke 0xC000 98` | Write a word in heap RAM only |
| `alloc 32` | Allocate and zero 32 words; print their address |
| `list` | List RAM files, lengths and kinds |
| `ls` / `dir` | Aliases for list, with the same RAM-file view |
| `copy old new` | Copy text or program contents to a new name |
| `rename old new` | Change a name, including when all slots are full |
| `delete name` | Permanently remove a RAM file and free its slot |
| `read name` | Print a text file |
| `write name bob!` | Create or replace a one-line text file |
| `edit name` | Open/create text in the full-screen editor (Windows console) |
| `cc bob.c bob` | Compile a C source file inside the emulator |
| `cc bob.c` | Compile to the default program file `app` |
| `go bob.c` | Compile to `app` and run only if compilation succeeds |
| `go bob.c bob` | Compile and run with an explicit output name |
| `save` | Save all files to a host snapshot |
| `restore yes` | Validate/load that snapshot, replacing the RAM files |
| `load name 0x4001 ...` | Store raw bob16 machine words as a program file |
| `run name` | Load a binary program into reserved RAM, execute, return |
| `halt` | Stop the emulator |

Command lines hold up to 127 characters. Longer lines are completely consumed
and rejected so their tail cannot become another command. Input is terminated
and handles CRLF, backspace and EOF. Decimal input accepts -32768..65535;
positive values above 32767 represent their 16-bit bit pattern. Hex accepts
0x0000..0xFFFF. Addresses are word addresses; allocation sizes must be positive.

## Shell keys

In a native Windows console, the shell supplies editing and redraw itself:

| Key | Action |
| --- | --- |
| Up / Down | Browse the four most recent nonempty commands; Down restores the draft |
| Left / Right | Move within the command |
| Home / End or Ctrl+A / Ctrl+E | Move to the start/end |
| Backspace / Delete | Remove before/at the cursor |
| Ctrl+U | Clear the line |
| Tab | Complete a unique command or filename; list ambiguous matches |
| Ctrl+D / Ctrl+Z | End input |

Completion operates on the word at the end of the line. Type more characters
when matches are ambiguous. History is session-only, includes failed commands,
and skips consecutive duplicates. `edit` opens its own full-screen view in a
Windows console. Redirected input emits no
redraw or candidate lists. Other terminals use ordinary input; full interactive
key handling currently targets the native Windows console.

The RAM filesystem has eight slots, names up to 23 characters and content up
to 511 words per file. A character occupies one word. `bob.c` occupies one slot
initially. Unsaved files and allocations are lost on exit. Use `save` to retain
files between sessions. Allocation is a zeroing bump allocator with no freeing. `poke` is
restricted to heap RAM so it cannot corrupt the shell.

Copy and rename refuse to overwrite an existing destination. Choose a new
name or explicitly delete the destination first. Delete has no undo. Names
cannot contain spaces. Both commands preserve whether a file is text or a
program; copying requires a free slot, while renaming does not.

To try the supplied example, enter `go bob.c`; its output is `bob!`, followed
by `Exit 0`. The compiler refuses to replace its own source. On failure it
leaves an existing output intact and `go` does not run that old program.
Diagnostics report the first detected error's character offset, line and
column (one based); the location can be just after the token that caused it.

## Keep files between sessions

Run from the repository directory, then enter:

```text
write note bob!
go bob.c
save
halt
```

Start the emulator again, enter `kernel.basm` (or use `--os`), and enter
`restore yes`, then `read note` or `run app`. Restore is explicit; startup does
not automatically replace the supplied `bob.c` or other RAM files.

The default snapshot is `bob-files.b16` in the emulator's working directory.
`save` replaces the previous snapshot, first writing `bob-files.b16.tmp` and
then replacing the destination. Keep a copy of the snapshot for older versions.
Set the host environment variable `BOB16_STORAGE` to choose another file path;
the parent directory must already exist. Messages mention this override.
Restoring replaces **all** RAM files; use `save` first if current files matter.
An invalid, truncated, corrupt or unreadable snapshot leaves RAM unchanged.
The snapshot includes text and program files, not heap allocations or history.

Native programs contain kernel helper addresses. If the kernel image differs
from the one used to save, restore keeps text and omits binary programs.
Run `go SOURCE` to compile them again. BASM and binary boots of the same kernel
share the same identity. Guest programs cannot invoke the storage service.

## Edit existing files

In a native Windows console, `edit name` opens a nano-like, non-modal view:
type directly into the file and use the arrow keys to move. The title shows the
filename and `*` when modified; the bottom row lists shortcuts. The view scrolls
vertically and horizontally to keep the cursor visible.

| Key | Effect |
| --- | --- |
| Ctrl+O | Save to the RAM filesystem and continue editing |
| Ctrl+X | Exit; if modified, ask whether to save |
| Y / N at the exit prompt | Save and exit / discard and exit |
| Ctrl+C at the exit prompt | Cancel and continue editing |
| Ctrl+Z | Toggle undo/redo of the last insertion or deletion |
| Arrows | Move through characters and lines |
| Home / End or Ctrl+A / Ctrl+E | Move to the start/end of the current line |
| Backspace / Delete | Delete before/at the cursor |
| Enter | Insert a newline |
| Tab | Insert a space |

For example, enter `edit bob.c`, move to the text you want to change, type,
then press Ctrl+O and Ctrl+X. Enter `go bob.c` at the shell to compile/run it.
There are no insert/command modes or colon commands in this view. A failed
save keeps changes in the editor. The file limit remains 511 characters;
additional input is rejected without truncating existing text. Ctrl+D follows
the same safe exit path as Ctrl+X. Saves are to RAM; use shell `save` for disk.

### Scripted/other-terminal fallback

For redirected input or terminals without native Windows console support,
the original line interface remains available for scripts:

`edit name` loads the current text, displays numbered lines and opens `edit>`.
Typing ordinary text appends a line; existing content is retained. The editor
uses a scratch buffer, so opening a file does not immediately change it.

| Editor command | Effect |
| --- | --- |
| `:p` | Show all lines with numbers |
| `:p N` | Show only line N for easier navigation |
| `:a TEXT` | Append a line, including text beginning with a colon |
| `:i N TEXT` | Insert a line before line N; a final empty line position allows appending |
| `:r N TEXT` | Replace line N |
| `:d N` | Delete line N |
| `:u` | Toggle undo/redo of the most recent successful text change |
| `:w` | Save while continuing to edit |
| `:wq` or `.` | Save and return to bob> |
| `:q` | Quit if unchanged; otherwise explain how to save or discard |
| `:q!` | Explicitly discard changes since the last save and return to bob> |

For example, `edit bob.c`, `:r 1 int main(void) { println("bob!"); return 0; }`,
then `:wq` changes the existing source rather than recreating it from scratch.
Each edit line has the same 127-character input limit as the shell. Oversize
changes and invalid line numbers are rejected while the editor remains open.
EOF discards unsaved changes. Binary program files cannot be opened as text.
This fallback is a line editor. Interactive Windows sessions use the full-screen
view described above.

## Compile C inside bob16

The initial RAM file `bob.c` contains:

```c
int main(void) { println("bob!"); return 0; }
```

At `bob>` enter:

```text
cc bob.c bob
run bob
```

The program prints `bob!`, reports `Exit 0` and returns to the shell. No host
compiler is involved: the resident compiler parses C and generates bob16
machine words in RAM, rather than interpreting the source.

To write another program, enter `edit count.c`, then:

```c
int main(void) {
    int n = 0;
    while (n < 3) { n = n + 1; }
    if (n == 3) println("bob!");
    return 0;
}
```

Enter `.` on its own line, followed by `cc count.c count` and `run count`.

The resident compiler supports one `int main(void)` or `int main()` function,
up to 16 integer variables, assignments and compound assignments, decimal/hex/
character/string literals, parentheses, unary `+ - ! ~`, prefix/postfix increment
and decrement, arithmetic, bitwise operators, shifts, short-circuit `&&`/`||`,
comparisons with C operator precedence, blocks, `if`/`else`, `while`, `for`,
`break`, `continue`, and `return`. For-loop initialization can declare an int;
its increment can assign, compound-assign or increment/decrement a variable.
It supports `print`, `println`, `print_dec`, `print_hex`
and `bob_putc` calls with one argument, comments and common string escapes.
Variables have a flat function scope; declare names once (including for-loop
variables). Compound assignments support `+= -= *= /= %= &= |= ^=`; shift
assignments and assignments inside expressions are unsupported. Strings hold up to
63 characters and nesting is bounded at 32 parser frames. Both source and
output must fit RAM-file limits. Invalid or unsupported syntax reports an error
without replacing the output file.

This is a small C subset, not a complete C implementation. Resident includes,
macros, arrays, pointers, additional functions, structs, floating point
and a standard library are unsupported. Programs call the current kernel's
console/arithmetic routines and belong to the kernel build that compiled them.

## Native programs and recovery

`run` clears the program region and copies in the binary RAM file at `0x9000`.
`RET` (`0xE000`) returns to the shell with r0 as the exit status. `TRAP 0` also
ends the running program without ending the shell. The emulator saves/restores
the shell's CPU state, supplies a separate program stack, stops execution after
five million cycles, and rejects writes outside `0x9000..0xDFFF`. Invalid
instructions and forbidden services return a program fault.

Programs can write their program region and the shared heap. This is not process
isolation, and input services can wait for input. The raw `load` command accepts
what fits in a single command line; use the resident compiler for larger programs.

## Kernel routines and memory

- `kernel/runtime.c`: print, println, signed decimal/four-digit hex printing,
  bounded line input, strlen, strcmp, word-based memcpy/memset and alloc.
- `kernel/files.c`: RAM filesystem.
- `kernel/compiler.c`: resident C compiler.
- `kernel/shell.c`: commands.
- `kernel/kernel.c`: includes these as one translation unit and runs the shell.

| Word addresses | Use |
| --- | --- |
| `0x0000..0x00FF` | Boot ROM and reserved return sentinel |
| `0x0100..0x01FF` | Shell and fallback editor input buffers |
| `0x0300..0x8FFF` | Kernel code, globals and strings |
| `0x9000..0xBFFF` | Loaded program, variables and downward program stack |
| `0xC000..0xDFFF` | 8,192-word heap |
| `0xE000..0xE3FF` | Shared editor/compiler scratch (never used concurrently) |
| `0xE400..0xE5FF` | Four-command shell history |
| `0xE600..0xEFFF` | Kernel stack, growing down from `0xF000` |
| `0xF000..0xFFFF` | Eight file-content buffers; protected from program writes |

There are no interrupts, multitasking or hardware memory protection. The write
checks and cycle limits are emulator services. Keep kernel recursion within its
reserved stack. Each guest int, char and pointer occupies one 16-bit word;
strings are unpacked ASCII. Arithmetic wraps to 16 bits, division truncates
toward zero and right shift is arithmetic. Arithmetic helpers return zero for
division by zero or invalid shift counts.

## Rebuild using C and GCC

The supplied `bob.exe` and `kernel.basm` require no build step. To edit and
rebuild the kernel, use GCC from the repository root:

Close running `bob.exe` sessions before rebuilding: Windows locks executable
files while they are running. Rebuilding replaces `bob.exe` and `kernel.basm`;
it does not preserve the volatile RAM files from a previous session.

```powershell
& 'C:\msys64\ucrt64\bin\gcc.exe' -std=c11 -O2 tools/build.c -o build.exe
./build.exe
./build.exe --test
```

The C build utility detects MSYS2 GCC on Windows. It builds `bob.exe` and
`build/bobcc.exe`, preprocesses guest C with GCC, and generates `kernel.basm`,
`build/kernel.b16` and `build/kernel.map`. `./build.exe --run` builds and boots.
On Linux/macOS compile the utility as `build-tool`, then run `./build-tool`.

GCC builds host executables and preprocesses guest C only; `tools/bobcc.c` does
the bob16 code generation. The host compiler supports a broader subset:
signed ints/chars, word pointers and casts, fixed arrays, constant global
initialization, functions and recursion, assignments/compound assignments,
arithmetic/bitwise/shift operators, short-circuit logic, comparisons,
increment/decrement, if, while, do/while, for, break, continue, function-scoped
labels/goto and returns. Blocks permit shadowing; for-loop declarations have
loop scope. Duplicate declarations in the same scope are rejected.
Switch/case/default support fall-through, nesting and break; continue targets
the surrounding loop. Integer constant expressions support arithmetic, bitwise,
comparison, logical and conditional operators in bounds/initializers/case labels.
Invalid constant division and shifts are rejected.
Comma-separated declarations work at file/block scope and in for initializers;
each declarator has its own pointer stars and array bounds. Global pointer
initializers can reference strings, global objects and array elements, with
constant offsets scaled to their pointee size. Nested element addresses and
arrays of pointers use relocations as well.
File-scope function prototypes are retained and checked against definitions for
the current scalar/character/pointer kinds and parameter counts. Array parameters
adjust to pointers to their element or row type; the outer bound may be omitted.
Unused prototypes need no definition. Pointer descriptors distinguish current
pointee kinds and indirection depth in prototype checks. Qualifier compatibility,
complete expression checking and linking external definitions remain incomplete.
Multidimensional arrays support indexing, row pointers, scaled pointer arithmetic,
increments and pointer differences. Objects are limited to 4096 words and 16
dimensions. Global/local initializer lists accept nested braces and brace
elision, with omitted elements zero-filled. Excess initializers are rejected.
Array designators such as `[3] = 7` and `[1][2] = 9` select elements; subsequent
entries continue after the selected subobject. Later initializers replace earlier
values for the same elements. Designators also support pointer arrays and inferred
outer bounds. Indices must be constant and within the array bounds. Initializer
braces are limited to 64 nested levels.
Initialized arrays may omit their outer bound. Character arrays accept string
initializers, with inferred bounds or zero-filled padding. A bound equal to the
text length omits the terminator; character-array rows may initialize from
strings, with or without braces. String literals and concatenation preserve
embedded zero characters, including their contribution to sizeof.
Conditional `?:` and comma expressions preserve branch/sequence behavior.
`sizeof` handles the current word-sized scalar/pointer types, declared arrays
and string literals without evaluating its operand; void operands are rejected.
Structs, typedefs, unsigned types, floating point, function pointers,
Variadics and standard headers are unsupported. `sizeof` tracks array/pointer
expression types, including *&array and array decay in comma/conditional
expressions. Recursive object declarators support grouped names, pointers to
arrays such as `int (*rows)[3]`, and arrays of those pointers. Abstract declarators
in casts and sizeof support these forms too; sizeof array type names uses the
complete storage size. Pointers to incomplete arrays can be declared, but their
pointee has no size for sizeof or arithmetic. Declarators have limits of 32
parenthesis levels and 64 derived types. Function pointer declarators, functions
returning pointers to arrays, and complete C type checking remain unsupported.
Host tools themselves use ordinary host C types.

To compile another standalone guest source from the repository root:

```powershell
gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -I kernel my_program.c -o build/program.i
./build/bobcc.exe build/program.i program.basm build/program.b16
```

Host-compiled BASM starts at address zero as a standalone guest. It is not a
RAM file for the shell's run command, whose programs use a different load
address. Host arithmetic helpers are in `tools/arithmetic.c`, also written
in the supported guest C subset.

`tests/test_os.c` checks console/commands, input bounds, memory/string routines,
heap exhaustion, file limits, resident compilation/execution, raw programs,
fault/time-limit recovery, host compilation, binary-image validation and the
interactive BASM route. Successful guest examples print `bob!`.

Function contracts and execution status codes are documented in
[the kernel API](kernel/API.md).

## Emulator services and boot image

Traps 0-3 retain halt, character output, string output and bounded string input.
Trap 4 reads the simulated boot disk; trap 5 returns a character or -1 at EOF;
trap 6 runs a loaded guest. The compiler exposes them through `bob.h`.

A binary kernel contains `B16K` and five little-endian 16-bit fields: version 1,
origin, entry, word count and sum of payload words modulo 65536, then the payload.
The host validates it and installs the boot ROM, which loads it through trap 4
and jumps to startup. Startup initializes the stack and calls main. The BASM
route loads the same instructions through `.fill` and a startup jump.
