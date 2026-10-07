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

To build and boot the native 32-bit kernel, close the current emulator session
and run `build.exe --run32`. The build also creates `build/kernel32.b32`, which
can be started directly with `bob32.exe --boot build/kernel32.b32`. The native
kernel supports the shell, RAM files, editor, resident C compiler, supervised
bob16 applications and B32S snapshots. In B32 mode, `cc` and `go` compile the
resident C subset to native bob32 programs with 32-bit `int`; kind-1 bob16 apps
still run with 16-bit arithmetic compatibility. The host `bobcc --wide` option
also emits native bob32 application images for direct boot.
The host compiler also emits a runnable bob32 BASM listing; start it with
`bob32.exe --asm32 build/program.basm`.

To build a native bob32 application for the running bob32 OS, preprocess the C
source and compile it with `--wide-app`:

```text
gcc -E -P -nostdinc -undef -I kernel app.c -o build/app.i
build/bobcc build/app.i build/app.basm build/app.b32 --wide-app
```

Then start the B32 OS with `build.exe --run32` and enter `run32 build/app.b32`.
The application uses the supervised high-memory region and its own 32-bit
stack. `run32` validates the B32K v2 header, range, checksum and file length
before loading it. The image limit is 65,536 words. Apps load from the emulator
host's working directory and are not stored in the guest snapshot. To keep an
image in the guest filesystem, use `import32 build/app.b32 myapp`, then launch
it with `run myapp`. `save` includes imported applications in the B32S snapshot;
after `restore yes`, run them again without re-importing. `run32` remains
available for direct host-path launches.
Set `BOB16_TRACE_FAULT=1` before launching the emulator to log the address and
instruction when a supervised application faults.
The updated emulator also accepts `bob.exe --os` from that directory to boot
the supplied image directly. The welcome message points to `help` and `go bob.c`.

## Small application string library

Native C applications may include `bob_string.h` along with `bob.h`. The string
header supplies word-addressed `bob_strlen`, `bob_strcpy`, `bob_strcmp`,
`bob_strcat`, and `bob_strchr`, plus `bob_isspace`, `bob_parse_int`,
`bob_format_int`, and `bob_token_next`. It is included in the app translation
unit, so the compiler emits only helpers reachable from that app; no separate
library link step is needed. The caller provides enough space for copy/append
destinations. `bob_parse_int` accepts signed decimal or `0x` hexadecimal with
surrounding whitespace and reports validity through its second argument.
`bob_format_int` returns the character count or -1 when the output buffer is
too small. `bob_token_next` edits a writable command buffer in place, groups
single- or double-quoted text, honors backslash escapes, and returns 1 for a
token, 0 at end, or -1 for an unclosed quote.

## Commands

| Command | Behavior |
| --- | --- |
| `help` / `help COMMAND` | Show commands or detailed usage and examples |
| `echo bob!` | Print text |
| `clear` | Clear an ANSI-capable terminal |
| `mem` | Show heap usage and memory regions |
| `peek 0xC000` | Read a word |
| `poke 0xC000 98` | Write a word in the heap (bob16 mode) |
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
| `run32 path` | Load and run a native B32K v2 image from a host path or guest filename |
| `import32 path name` | Import a host B32K v2 image into bob32 RAM files |
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

The RAM filesystem has eight slots and names up to 23 characters. bob16 keeps
eight fixed 511-word slots. bob32 packs files into 4,096 words total; text and
resident programs remain limited to 511 words, while native apps use six
metadata words plus payload and a terminator, sharing space with other files.
A character occupies one word. `bob.c` occupies one slot initially. Unsaved
files and allocations are lost on exit. Use `save` to retain
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

The resident compiler supports one zero-argument `int main(void)` plus up to
15 additional `int` functions with integer parameters. Calls may be forward,
nested or recursive, and may be used as expression statements; each function
gets its own parameter/local stack frame.
The compiler supports up to 16 functions, 16 parameters per function and 16
simultaneously visible local/parameter names. B32 stack frames can reserve up
to 32 local slots across nested scopes; a further declaration is rejected
because frame loads/stores use signed six-bit offsets. It also supports
assignments and compound
assignments, decimal/hex/character/string literals, parentheses, unary `+ - ! ~`, prefix/postfix increment
and decrement, arithmetic, bitwise operators, shifts, short-circuit `&&`/`||`,
comparisons with C operator precedence, blocks, `if`/`else`, `while`, `for`,
`break`, `continue`, and `return`. For-loop initialization can declare an int;
its increment can assign, compound-assign or increment/decrement a variable.
It supports `print`, `println`, `print_dec`, `print_hex`
and `bob_putc` calls with one argument, comments and common string escapes.
Variables follow block scope, including declarations in `for` initializers;
inner blocks may shadow outer names, while duplicate names in one block are
rejected. Compound assignments support `+= -= *= /= %= &= |= ^=`; shift
assignments and assignments inside expressions are unsupported in bob16. Strings hold up to
63 characters and nesting is bounded at 32 parser frames. Both source and
output must fit RAM-file limits. Invalid or unsupported syntax reports an error
without replacing the output file.
The bob32 resident compiler additionally supports C's right-associative `?:`
conditional expression, including evaluation of only the selected branch. This
operator is not enabled in bob16 because its kernel image is at the address
limit; bob16's compiler behavior is unchanged. B32 resident C also supports
`do/while`, including `break` and `continue` (which proceeds to the condition),
assignment expressions (including right-associative chains), and `<<=`/`>>=`.
Both resident compilers provide `print`, `println`, `print_dec`, and `print_hex`
as one-argument built-ins; bob32 source can call them without a prior prototype.

This is a small C subset, not a complete C implementation. Resident includes,
macros, pointers, globals, structs, floating point and a standard library are
unsupported. The bob32 resident compiler supports `int` function prototypes
with integer parameters (prototype parameter names are optional); incompatible
parameter counts are rejected, and a referenced function still needs a
definition. Empty `f()` declarations retain unspecified-parameter status and
may be followed by a typed declaration or definition; calls made through that
declaration must match the eventual definition's arity. bob16 resident
prototypes remain unsupported. The bob32 resident
compiler supports fixed-size,
one-dimensional local `int` arrays with indexed reads and assignment statements;
their elements use the function's 32-word stack-slot budget. Indexed prefix and
postfix increment/decrement work in expressions; indexed compound assignments
work in statements.
Brace initializer lists accept scalar expressions and a trailing comma; omitted
elements are zero-filled and excess elements are rejected. The bob32 resident
compiler accepts one-dimensional `int` array parameters as `values[]` or with a
fixed bound, passing the word-addressed pointer through the ordinary argument
stack. Helpers can index or modify the caller's elements and forward the array
to another helper. Nested/multidimensional arrays and bob16 resident array
parameters remain unsupported.
Local `int` declarations may contain comma-separated scalar and fixed-array
declarators, each with its own initializer.
The bob32 resident compiler also supports local pointers to integer-returning
functions with integer parameters. Initialize or assign one from a previously
declared matching function, copy it to a compatible local pointer, or assign
null (`0`), then call indirectly. Mismatched signatures and integer values are
rejected. Function-pointer parameters, returns, globals, pointer arithmetic
and bob16 resident function pointers remain unsupported.
Programs call the current kernel's
console/arithmetic routines and belong to the kernel build that compiled them.
In bob32 mode the same syntax emits native 32-bit instructions and kind-2 RAM
program files, which can be run with `run` and retained in B32S snapshots. B32
resident programs start at `0x20000` and use variables at `0xF0000..0xF000F`;
their output can use the remaining shared 4,096-word filesystem space. Source
text files remain limited to 511 words. In bob16 mode files remain kind 1 and
use 16-bit integers and the `0xA000` program region. The bob32 shell also runs
kind-1 legacy programs at `0xC200`, clear of the expanded kernel image.

## Native programs and recovery

`run` clears the program region and copies in the binary RAM file at `0xA000`
in bob16 mode or `0xC200` in bob32 mode.
`RET` (`0xE000`) returns to the shell with r0 as the exit status. `TRAP 0` also
ends the running program without ending the shell. The emulator saves/restores
the shell's CPU state, supplies a separate program stack, stops execution after
five million cycles, and rejects writes outside the legacy app/heap range
(`0xA000..0xDFFF` in bob16, `0xC200..0xDFFF` for bob32 compatibility apps).
Native bob32 apps write only their supervised application and stack region.
Invalid
instructions and forbidden services return a program fault.

In bob32 mode `run` also accepts resident kind-2 programs, loading them at
`0x20000` with 32-bit arithmetic. Their compiler variables occupy
`0xF0000..0xF000F`; the host `run32 PATH` command remains available for larger
external B32K images.

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
| `0x0300..0x9FFF` | Kernel code, globals and strings in bob16; current image ends at `0x90E4` |
| `0xA000..0xBFFF` | Loaded program, variables and downward program stack in bob16 |
| `0x0300..0x9FFF` | bob32 low-memory shell data and compatibility space |
| `0xA000..0xCFFF` | Loaded bob16 compatibility program at `0xC200..0xCFFF` |
| `0xD000..0xDFFF` | bob32 heap and compatibility stack region |
| `0xE000..0xEFFF` | Editor/compiler scratch, history and shell stack |
| `0xF000..0xFFFF` | Packed bob32 RAM filesystem contents |
| `0x10000..0x1FFFF` | Native bob32 kernel code, globals and strings |
| `0x20000..0xEFFFF` | Supervised native bob32 program entry/code region |
| `0xF0000..0xFFFFF` | Native app data and compiler variables; stack starts at `0x100000` |

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

The C build utility detects MSYS2 GCC on Windows. It builds `bob.exe`,
`bob32.exe` and `build/bobcc.exe`, preprocesses guest C with GCC, and generates
both `kernel.basm`/`build/kernel.b16` and `build/kernel32.basm`/
`build/kernel32.b32`. `./build.exe --run` boots bob16; `./build.exe --run32`
boots bob32.
On Linux/macOS compile the utility as `build-tool`, then run `./build-tool`.

GCC builds host executables and preprocesses guest C only; `tools/bobcc.c` does
the bob16 code generation. The host compiler supports a broader subset:
signed ints/chars, word pointers and casts, fixed arrays, constant global
initialization, functions and recursion, assignments/compound assignments,
arithmetic/bitwise/shift operators, short-circuit logic, comparisons,
including all arithmetic/bitwise compound assignments and `<<=`/`>>=`,
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
Repeated function declarations merge compatible incomplete/complete array bounds
through pointer types; differing known bounds, element types or depths reject.
Compound assignments validate pointer offsets and integer operands, including
within sizeof. Assignment/increment operands in sizeof must be modifiable scalar
lvalues; their side effects remain unevaluated. Const enforcement and full
assignment conversion checks remain incomplete.
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
Typedef declarations support current scalar, pointer and array types at file and
block scope. Aliases work in parameters, casts, sizeof and further declarations;
void aliases can specify a no-argument function. Compatible identical typedef
redeclarations are accepted; namespace conflicts reject. Inner objects or aliases
can shadow an outer alias, which is restored when the inner scope ends.
Structs, short integer types and floating point remain unsupported. The host
compiler accepts typed function pointers in local and file-scope objects,
function-pointer parameters, address-of function designators and indirect calls
on both targets. Function-pointer/object-pointer conversions and mismatched
signatures are rejected. Returning function pointers and variadic function
pointers are unsupported. The host compiler's bob32 `--wide` target accepts `long` and
`long long` as 32-bit aliases of `int`, consistent with the word-addressed ABI;
`sizeof` reports addressable words (one byte per word). Enums support tagged/anonymous definitions, explicit constant
values, automatic increments, typedef aliases and block/loop tag shadowing.
Enumerators are scoped integer constants usable in bounds, case labels and
initializers. Enum objects use signed 16-bit int storage; distinct enum types
remain distinct in declaration checks and are compatible with this chosen int
representation. Automatic values exceeding 32767 reject. Enum tags must already
be defined when referenced; incomplete enum extensions are unsupported.
Variadics and most standard headers are unsupported; the host C workflow provides
`<stdbool.h>` and `<stddef.h>` with `_Bool`, `size_t`, `ptrdiff_t` and `NULL`.
`sizeof` has the unsigned `size_t` type and tracks array/pointer
expression types, including *&array and array decay in comma/conditional
expressions. Recursive object declarators support grouped names, pointers to
arrays such as `int (*rows)[3]`, and arrays of those pointers. Abstract declarators
in casts and sizeof support these forms too; sizeof array type names uses the
complete storage size. Pointers to incomplete arrays can be declared, but their
pointee has no size for sizeof or arithmetic. Declarators have limits of 32
parenthesis levels and 64 derived types. Function pointer declarators, functions
returning pointers to arrays, and complete C type checking remain unsupported.
Host tools themselves use ordinary host C types.
The host compiler supports `_Bool` as a one-word type. Initializers, assignment,
casts, returns and arguments to boolean parameters convert zero to 0 and nonzero
to 1. Boolean arrays, typedef aliases and static objects use the same conversion;
increment/decrement and compound assignments preserve normalized storage. The
host workflow includes `<stdbool.h>` with `bool`, `true` and `false` definitions.
Unsigned 16-bit int is available as `unsigned` or `unsigned int`, with `u`/`U`
literal suffixes. Mixed int/unsigned arithmetic uses unsigned comparisons,
division and remainder; right shifts of unsigned values fill with zeros.
Arithmetic storage wraps modulo 65536. Unsigned helpers are included only when
explicit unsigned types/literals are used. Unsuffixed literal selection retains
the earlier compiler behavior; full C literal selection, unsigned char/short/long
and all declaration-specifier orderings remain incomplete.
Sizeof is accepted in constant initialization; local static initializers may
query automatic-object types without evaluating their values. Type/literal sizeof
works in bounds and case labels. Named-object sizeof in those contexts uses
parser type bindings with block/for scope and parameter array adjustment.
Objects must already be declared and complete; later or escaped bindings reject.
File-scope object declarations support `extern`, compatible repeated tentative
definitions and a single initialized definition. Incomplete array declarations
merge with known bounds; an uncompleted tentative array reserves one element.
Unused extern objects need no definition, while referenced ones must be defined
in this source unit. Block-scope extern object declarations refer to that same
storage without allocating stack slots. They can shadow an outer local, and
names introduced only in a block do not escape it. Block extern initializers
and conflicts with a local in the same scope reject. Block function declarations,
separate-unit linking remain unsupported. File-scope static objects/functions
use internal linkage within the current source unit. Later extern declarations
inherit prior internal linkage; conflicting external/internal declarations
reject. Function declarations without a storage class retain an earlier static
declaration's linkage. Internal tentative arrays need a complete bound.
Block-scope static objects have private persistent storage initialized once,
including arrays and pointers to other static objects. Initializers must use
supported constant expressions or static addresses; automatic-object addresses
and runtime calls are rejected. Equal names in different blocks/functions remain
independent, and a static object's name is scoped to its declaring block.

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
