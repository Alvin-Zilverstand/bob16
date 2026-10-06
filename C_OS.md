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
| `edit name` | Open an existing text file or create one in the line editor |
| `cc bob.c bob` | Compile a C source file inside the emulator |
| `cc bob.c` | Compile to the default program file `app` |
| `go bob.c` | Compile to `app` and run only if compilation succeeds |
| `go bob.c bob` | Compile and run with an explicit output name |
| `load name 0x4001 ...` | Store raw bob16 machine words as a program file |
| `run name` | Load a binary program into reserved RAM, execute, return |
| `halt` | Stop the emulator |

Command lines hold up to 127 characters. Longer lines are completely consumed
and rejected so their tail cannot become another command. Input is terminated
and handles CRLF, backspace and EOF. Decimal values are signed 16-bit; hex values
cover all 16-bit bit patterns. Addresses are word addresses.

The RAM filesystem has eight slots, names up to 23 characters and content up
to 511 words per file. A character occupies one word. `bob.c` occupies one slot
initially. Files and allocations are lost on exit; disk persistence is not
implemented. Allocation is a zeroing bump allocator with no freeing. `poke` is
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

## Edit existing files

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
The editor is a line editor, not a full-screen terminal application.

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
| `0x0300..0x8FFF` | Kernel code, globals, strings and editor/compiler buffers |
| `0x9000..0xBFFF` | Loaded program, variables and downward program stack |
| `0xC000..0xDFFF` | 8,192-word heap |
| `0xE000..0xEFFF` | Kernel stack, growing down from `0xF000` |
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
increment/decrement, if, while, for, break, continue and returns.
Shadowing, structs, typedefs, unsigned types, floating point, function pointers,
sizeof, variadics, local array initialization and standard headers are unsupported.
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
