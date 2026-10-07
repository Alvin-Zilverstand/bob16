# Kernel API

Include `runtime.h` in guest kernel source. The host bob16 compiler treats each
int, char and pointer as a single 16-bit word. These are guest routines, not the
host C library. Headers and source are compiled together by `kernel/kernel.c`.

## Console

| Function | Contract |
| --- | --- |
| `void print(char *text)` | Print an unpacked ASCII string up to its zero terminator |
| `void println(char *text)` | Print the string and one newline |
| `void print_dec(int number)` | Print a signed value, including -32768, without a newline |
| `void print_hex(int number)` | Print its raw 16 bits as `0x` and four uppercase hex digits |
| `int read_line(char *buffer, int capacity)` | Read/terminate a line; length on success, -1 for truncation/invalid capacity, -2 for EOF with no characters |

The caller must supply a writable buffer of at least capacity words. At most
capacity-1 characters are stored. Excess characters are consumed until newline,
so they do not leak into the next command. Newline/CR are not stored. Backspace
removes a stored character. A partial final line at EOF is returned normally;
the next read reports EOF. The host terminal supplies line editing and echo.

The low-level `bob_putc(int)` sends the low eight bits to stdout. `bob_getc()`
returns 0..255 or -1 at EOF. `bob_puts()` adds a newline; use print for prompts.

`bob_key()` (trap 8) reads a key: -1 EOF, byte character, or 256/257 up/down,
258/259 left/right, 260/261 home/end, 262 delete. Windows console input uses
unbuffered keys, converts Enter to newline, and treats Ctrl+D as EOF.
Ctrl+Z is an ordinary control character: shell EOF, full-screen editor undo.
Redirected input uses getchar. `bob_terminal()` (trap 9) returns 1 only when
both input/output are native Windows console handles; otherwise 0. Shell
history, completion and editing are guest code in `kernel/input.c`. ANSI key
sequences in redirected regression fixtures exercise the same guest actions.
`bob_columns()` (trap 10) reports the console buffer width, or 80 as fallback.
The shell scrolls a long input line horizontally to keep redraw within it.

## Strings, memory and allocation

The kernel-only `bob_snapshot(int *descriptor, int operation)` is trap 7.
Operation 0 saves; 1 restores. The four-word descriptor contains pointers to
the 192-word names table, then eight-word length, kind and used tables. File
contents occupy the fixed `0xF000..0xFFFF` region. Descriptors can be on the
kernel stack; tables must be disjoint and below `0xA000` in bob16 mode. In
bob32 mode they may be in low kernel RAM or in the loaded kernel image, which
now resides above `0x10000`. The host validates
metadata, payload checksum and complete file length before committing a restore.
Returns 0 on success, 1 when restored binaries were omitted because the kernel
changed, -1 for storage errors, -2 for invalid data and -3 for invalid arguments.
Invocation by a supervised program faults and restores the shell. The default
host path is `bob-files.b16`; `BOB16_STORAGE` overrides it. See `C_OS.md` for the
user-facing save/restore workflow.

| Function | Contract |
| --- | --- |
| `int strlen(char *text)` | Count words before the zero terminator |
| `int strcmp(char *a, char *b)` | Negative, zero or positive according to the first differing ASCII character |
| `void memcpy(void *dst, void *src, int count)` | Copy count words forward; source and destination must not overlap |
| `void memset(void *dst, int value, int count)` | Fill count words with a 16-bit value |
| `int *alloc(int words)` | Return a zeroed heap block, or null for an invalid size or exhaustion |

String routines require terminated strings. Memory routines require valid
regions; they do not perform bounds checks for kernel callers. Counts are words,
not host bytes. The bob16 heap reserves 8,192 words from 0xC000; bob32 reserves
4,096 words from 0xD000 so the enlarged kernel and compatibility-program stack
fit below it. The heap is shared with loaded programs. There is no free or reset
operation.

## RAM filesystem

`file_find(name)` returns a slot or -1. `file_name(slot)` and
`file_content(slot)` return addresses into the filesystem buffers.
`file_write(name, data, length, kind)` copies data and creates/replaces a file,
returning its slot or -1 for a bad name, length or a full table. Kind 0 is text;
kind 1 is machine code, kind 2 is resident bob32 C code, and kind 3 is a packed
native B32K v2 image. Its copy appends a zero word, which is not part of length.
Names hold at most 23 characters. bob16 retains eight fixed 511-word slots;
bob32 packs files into a shared 4,096-word content region. Text files remain
limited to 511 words; resident programs and native images can use the remaining
shared capacity (up to 4,095 words per file). Valid slot indices are 0..7.
These are internal kernel APIs.

Failed size/name validation does not overwrite an existing file. Edits start
with the existing file contents in a scratch buffer and commit on :w, :wq or a
single dot line. :q protects unsaved changes; :q! or EOF discards them. Oversize
changes are rejected without modifying the current buffer.
Resident compilation similarly commits output only after successful parsing and
code generation. RAM contents disappear when bob.exe stops; save snapshots files for explicit restore in another session.

Interactive `edit` uses native `kernel/nano.c`: direct insertion, cursor motion,
Ctrl+O save, Ctrl+X safe exit with Y/N/Cancel, and Ctrl+Z one-change undo/redo.
The legacy colon-command interface is only a redirected/unsupported-terminal
fallback. Both share `0xE000..0xE1FF` text and `0xE200..0xE3FF` undo scratch.
Resident compilation reuses these regions for code/names/tokens/loop chains;
it must not run concurrently with editing. History uses `0xE400..0xE5FF`, with
kernel stack reserved at `0xE600..0xEFFF`. Input buffers are `0x0100..0x01FF`.
`bob_rows()` (trap 11) reports visible console height, with a 25-row fallback.

## Program execution

`bob_run(entry)` invokes emulator trap 6. In bob16 mode valid entries are
0xA000..0xBDFF. In bob32 mode kind-1 legacy entries are 0xC200..0xCFFF and
kind-2 native entries are 0x20000..0xEFFFF when the caller explicitly supplies
a wide entry. The shell places legacy programs at 0xC200 in bob32 mode to avoid
overwriting the bob32 kernel image, and native bob32 files at 0x20000. The entry executes
with r6 at the top of its protected stack region, r5=0, and r7 pointing to the
reserved return sentinel. In bob32 mode kind-1 apps use `0xC200..0xCFFF` because
the expanded kernel occupies the old bob16 program window. Compatibility apps
retain access to the shared `0xD000..0xDFFF` heap. The shell CPU state is
restored on completion. RET or TRAP 0 ends the program.

Return values -1, -2 and -3 are reserved for invalid entry/nested execution,
program fault, and timeout. Otherwise r0 is the program's exit status. A program
returning one of those reserved values is indistinguishable from that service
status. Five million cycles is the per-run limit. Programs may write only
their application and stack regions; bob16 and compatibility applications may
also write the shared heap. Kernel code, RAM files and the shell stack
are protected from their stores. Program and heap contents themselves are not
private or restored.

The resident compiler uses variable words at 0x9800..0x980F in bob16 mode and
0xF0000..0xF000F in bob32 mode; string data is inline in the program image.
In bob16 its files can contain at most 511 code/data words; bob32 compiler output
uses the available packed filesystem capacity. Console and arithmetic calls
use the separate program stack. Compiled resident programs depend on addresses
in the current kernel and should not be reused with a different kernel build.

`bob_address(function_name)` is a host-compiler intrinsic that inserts a kernel
function address. `bob_call(entry)` is an unchecked direct call for trusted
kernel code; ordinary shell programs use bob_run for recovery and write checks.

See [the OS guide](../C_OS.md) for commands, examples and C subset limits.

In bob32 mode, `import32 HOST_PATH NAME` validates and packs a B32K v2 image
into a guest RAM file; `run NAME` loads and executes it. B32S version 2
snapshots preserve packed files; version 1 fixed-slot snapshots remain readable
and are converted to the packed layout on restore. `run32 HOST_PATH` remains
available for direct host-path launches.

Native C applications can include `bob_string.h` for small app-side string
and parsing helpers. It provides bounded integer formatting, status-returning
decimal/hex parsing, and in-place quoted tokenization without depending on the
kernel's private runtime functions.
