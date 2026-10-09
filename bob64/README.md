# bob64 boot milestone

`make bob64` creates a standalone x86-64 UEFI application at
`build/bob64/EFI/BOOT/BOOTX64.EFI`. Copy the `EFI` directory to the root of a
FAT32 USB drive, boot a machine with x64 UEFI, and select the USB device. From a
UEFI shell, the same image can be started with:

```text
fs0:\EFI\BOOT\BOOTX64.EFI
```

UEFI's x64 application contract transfers control in 64-bit long mode with the
Microsoft x64 calling convention, a valid stack, and firmware page tables. The
first bob64 entry checks CPUID long-mode support and `EFER.LMA`, then prints
`bob64!` through the firmware console. It reads the UEFI memory map and seeds a
64-bit first-fit allocator for conventional 4 KiB physical pages. The PE image
has a base-relocation directory, allowing firmware to load it away from its
preferred address. A tested bootstrap mapper can build identity mappings for
the image, a replacement stack, and its page-table pool. The EFI entry now
allocates those regions and verifies their mappings. The default build remains
a safe probe that returns to firmware. The opt-in `make bob64-handoff` build
exits Boot Services, maps a supported GOP framebuffer, installs the kernel
GDT/IDT and page-table root, switches to a replacement stack, and enters a tiny
C kernel. The kernel checks the boot information and active CR3, prints
`bob64!` and address-space details to the framebuffer and COM1, then checks its
heap and opens a command shell. Unsupported GOP modes fall back to COM1. The
handoff has been booted under QEMU with x64 OVMF firmware; the kernel reaches its
shell after switching to its own page tables, and the GOP framebuffer has been
visually checked at 1280x800.

Kernel entry also adopts the seeded physical-page allocator and maps pages into
a small coalescing heap in the high canonical address range. Its boot smoke test
checks aligned allocation, zeroing, multi-page growth, writes, and freeing. The
early heap reclaims full trailing pages when they become unused. The page-table
pool grows in mapped 64-page chunks before its reserve runs out. SMP locking
remains open.

After the memory checks, bob64 opens a shell. When an 8042 controller is
available, keyboard IRQ1 feeds its event queue; otherwise input falls back to
PS/2 polling. COM1 remains polled. The shell provides `help`, `clear`, `mem`, `heap`, `version`, and
`echo`. A heap-backed RAM filesystem adds `ls`, `cat NAME`, `write NAME TEXT`,
and `rm NAME`; it stores arbitrary file bytes, and its contents disappear on
reboot. Up/Down recalls the last eight shell commands, and Tab completes built-in
commands plus file arguments for `cat`, `rm`, `run`, and `cc`. A 100 Hz PIT IRQ
supplies kernel and app tick counts. USB keyboards, directories, and persistent
storage are still open.

The shell also supports `save` and `restore` using B64S version 1. The format
stores fixed-width lengths and arbitrary file bytes with a CRC-32, never raw
pointers. `save` keeps a RAM checkpoint and writes a compressed copy to UEFI
non-volatile storage in two alternating slots. Each compressed snapshot is
split into bounded variables, and the active-slot record changes only after all
chunks are written; restore falls back to the prior committed slot if the active
one is damaged. Bob64 restores that snapshot on the next boot; if firmware
storage is unavailable or full, the RAM checkpoint still works for the current
session. Firmware variable and total-store limits apply. If firmware does not
implement `QueryVariableInfo`, the kernel still attempts the write and reports
the firmware's result. The OVMF test script verifies save, reset, and restore.

The kernel installs `echo.b64e`, a native 64-bit app that writes its startup
arguments and a newline. `run echo.b64e bob!` exercises the
pointer-width-safe startup record and makes the utility available to the
desktop's Run App action. The UEFI smoke path passes `bob!` through the startup
record and verifies those exact output bytes.

`ls.b64e` is also installed by default. Run `run ls.b64e` to list RAM files and
their byte sizes through the application-facing file-list syscall. Its boot
smoke check validates a real returned filename in app memory.

`cat.b64e` reads a named file using 4 KiB streaming transfers, preserving its
contents and adding a final newline only when needed. `run cat.b64e FILE` uses
the same file-handle ABI available to other native applications.

`notes.b64e` opens an editable Notes window when launched from the desktop.
Use the arrow keys, Home/End, Backspace, and Delete to edit `notes.txt`; Ctrl+S
saves it and Escape closes the window. The Notes editor supports up to 16 KiB
using 4 KiB streaming transfers. From the shell, use `run notes.b64e copy SOURCE DEST`
to copy text files, or `put NAME TEXT`, `add NAME TEXT`,
`show NAME`, and `delete NAME` (`edit` aliases `put`). Text is passed as
command-line arguments, so spaces between arguments are preserved. The app
refuses to replace or delete binary files. The kernel smoke path copies a
12 KiB text file and verifies every byte.

`info.b64e` is the native system-information app. It reports the syscall ABI,
display resolution when GOP is available, uptime in seconds, and the number and
total data size of files in the RAM filesystem. The build embeds it in the
kernel image, and its output ends with `bob!` for smoke testing.

`launcher.b64e` opens a managed native window listing installed `.b64e` apps.
Run `launcher.b64e APP [ARG ...]` from the shell to start an app directly with
its full argument vector; the launcher forwards argv[0] and the remaining args.
Use Up/Down and Enter (or click an app and **RUN APP**) to start a selected app;
Escape, `X`, or **CLOSE** returns to the shell. This standalone launcher starts
apps without command-line arguments; use the shell's `run` command when an app
needs arguments.

`bob64/exec.h` defines the B64E v1 flat, position-independent native image
format. It carries an explicit ABI version and 64-bit payload, memory, code, and
entry-offset fields. The host-tested parser validates exact lengths, reserved
fields, and a payload CRC before exposing the image; the loading primitive
preserves addresses above 4 GiB and zeroes the memory-only tail. `bob64/abi.h`
defines the matching Microsoft x64 app entry call and a 32-byte startup record
whose arguments live in app memory. `bob64/process.c` has a callback-backed
loader core that validates B64E again, prepares user code/data/stack mappings,
builds the app-owned argv/startup record, and rolls back mapped frames on
failure. Host tests use synthetic pages above 4 GiB. The kernel smoke test
clones its active page-table root, isolates a 34 MiB app image/stack arena, and
switches CR3 while it loads a synthetic app. The shell can run B64E files with
`run NAME [ARG ...]`; the kernel seeds `bob.b64e` as a ready-to-run example.
Each launch gets a fresh isolated root, and the kernel frees its app pages and
page tables after the exit. The example app prints `bob!` through the DPL3
`int 0x80` gate and exits back to the kernel.
Syscall ABI v12 exposes an ABI query, character and bounded buffer output,
whole-file and handle-based streaming file read/write/list/delete, display dimensions, full-screen packed
`0x00RRGGBB` surface presentation, app-owned-window creation/destruction and
presentation through a kernel compositor, focus control, input events, and
process exit, and synchronous child-app launch with bounded, validated argv
copying and parent-state restoration.
Applications can use
`bob64_app_get_display` and `bob64_app_present` from `bob64/app.h`; the kernel
validates the whole user surface, copies it in bounded chunks and converts it
to GOP channel order. Surfaces must match the display resolution and are capped
at 16 MiB. User buffers are checked against the process page
tables; file reads require writable app pages. `bob64/app.h` provides C wrappers
without exposing kernel-internal types. The C smoke app writes and reads back a
small RAM file before it exits.
`apps/bob64_display.c` ports Bob32's graphics demo as a centered 640x400 native
window. It draws colored panels and diagonal lines, presents them through the
kernel compositor, prints `bob!`, then returns to the shell on X or Escape.
The bob64 build packages it as `build/bob64-app/display.b64e`, installs it in
the RAM filesystem, and QEMU tests the window presentation and clean return.
The smaller app-owned drawing surface also keeps firmware snapshots within
their storage budget. `bob64_app_wait_event` receives typed PS/2 key-down/up
events, including Shift, Caps, Control, Alt and extended arrow-key codes, plus
mouse movement, buttons and IntelliMouse wheel events.
PIT IRQ0 supplies 100 Hz ticks; keyboard IRQ1 and mouse IRQ12 feed event queues
when the 8042 is available. Mouse position and packet decoding happen in the
IRQ handler. USB input remains future work.
`bob64/window.h` provides app-owned windows, 64-bit handles/context pointers,
focus, z-order, title-bar dragging, hit testing, dirty redraws and local event
routing. The v11 kernel API additionally creates, destroys and composites up to
16 kernel-owned window surfaces per foreground app, routes events with 64-bit
window handles, and releases surfaces when the app exits. The desktop now draws
its file browser and editor into separate kernel-managed surfaces while keeping
the app-side manager for widget layout and mouse hit testing. Concurrent apps
remain a future step. `apps/bob64_gui.c` is the first interactive desktop:
launch `run desktop.b64e [filename]` from the shell (the default file is
`notes.txt`), browse files in the left window, use Up/Down or the mouse wheel
to select and preview the selected file's first 384 bytes, `O` or Enter to open,
`R` to refresh, and `D` or **DELETE** to open
a Yes/Cancel confirmation. Press `Y` or Enter to confirm, and `N` or Escape to
cancel. Type into the editor, use arrow keys to move, Home/End to jump to the current line edges, Backspace to delete,
click in the text to place the cursor, Enter for a new line, and Ctrl+S to save.
The editor scrolls to keep the caret visible. Click the **NAME** field above
the editor to place its caret and edit the destination; Enter or Tab leaves the
field, and Ctrl+S saves the document under that name. Names accept letters,
digits, period, underscore or hyphen; executable names are protected. Escape
returns to the shell; unsaved text or name changes require a second Escape.
Select a `.b64e` file and press `A` or click **Run App** to launch it; the
desktop resumes with the child's exit status when it finishes. Click **APPS**
in the file window or press `P` from any desktop window to open the Applications
window. Use Up/Down to select an installed `.b64e` app, press Enter or `A` to
launch it, and click **X CLOSE** or press Escape/`X` to close the launcher.
Click **HELP** or press `H`/`?` from the Files window to view the desktop
shortcuts; Escape, `X`, or **CLOSE** dismisses it.
The desktop buttons share `bob64/widgets.h`, which handles hover and pressed
feedback and activates on a left-button release inside the control.
The desktop editor supports text files up to 16 KiB, matching the resident
compiler's source limit. It uses bounded 4 KiB streaming transfers; larger
files are protected from accidental overwrite. Under QEMU/OVMF at
1280x800, both windows rendered, PS/2 input created an additional window and
returned to the shell, a mouse click selected a file-list row, and Ctrl+S saved
editor text through the file syscall.
The editor refuses `.b64e` files and text containing binary control bytes;
use the Applications window to run executables.
User-origin CPU exceptions return an error status to the kernel. User apps run
with interrupts enabled when the keyboard IRQ path is active, and the loader
restores the caller's interrupt-enable state on return. The desktop editor
and Notes app use bounded 4 KiB streaming transfers and support up to 16 KiB
of text; larger files are
protected from accidental overwrite. Other apps can use ABI v12 streaming
handles for bounded transfers at 64-bit offsets. The kernel smoke app launches another native app and checks its return
status; simultaneous app management remains future work. QEMU/OVMF runtime
checks exercise the ring-3 smoke app, syscalls,
application loader, shell, and desktop.

The bob64 build compiles this C smoke app and embeds its B64E file in the kernel
image. The shell's `run bob.b64e` command exercises the C compiler bridge,
executable loader, user syscall ABI, RAM-file read/write, and process exit path.
The smaller kernel-generated app remains a separate ring-3 boot smoke test.

The shell also embeds `bob.c` and supports `cc bob.c` followed by
`run app.b64e`. The resident compiler accepts up to 16 KiB of source and emits
a B64E image with separate read-only executable code and writable/NX data
regions of up to 16 KiB each. Its initial C subset
accepts `int`, `short`, `long`, or `long long` main returns; initialized
`short`, `int`, `long`, and `long long` locals; local assignment; calls to
`bob64_app_write` with typed `char *` expressions and scalar length expressions; and
`return` expressions using 64-bit
integer literals, parentheses, unary minus, addition, subtraction, multiplication,
signed division/remainder and unsigned pointer-sized division/remainder. Division
truncates signed results toward zero; division by zero and signed overflow remain
C undefined behavior. Bitwise complement, AND, XOR, OR and left/right shifts use
C precedence; right shift is arithmetic for signed expressions and logical for
`usize`. Relational comparisons bind more tightly than equality comparisons, and
both bind more tightly than bitwise operators; `&&` and `||` retain short-circuit
behavior. The compiler follows LLP64: `short` is 16-bit; `int` and `long`
are 32-bit and sign-extend on return; `long long` is 64-bit and preserves its
full result. Short globals, locals, struct fields, function parameters and
returns are sign-extended on load and use 16-bit storage. The resident subset
supports `void` helper functions with explicit `return;` or fallthrough, and
scalar `char` returns with signed 8-bit extension. Void calls work as statements
and are rejected when used as values. The resident subset recognizes
`usize`/`uintptr_t` as unsigned 64-bit integers and
`isize`/`intptr_t` as signed 64-bit integers, including function parameters,
returns, and scalar storage. Function prototypes and definitions can also
return `char *`, `short *`, `int *`, or pointers to declared structs, with
matching pointee types enforced at return statements. The pointer-sized integer
names are compiler built-ins. Bounded file-scope typedefs can alias supported
scalar types, scalar pointers, and previously declared named structs, including
alias chains; typedef arrays, function pointers, anonymous structs, and header
inclusion are not implemented. `usize *`, `uintptr_t *`, `isize *`, `intptr_t *`, and
`long long *` support dereference, indexing, assignment, function arguments and
returns, eight-byte scaled arithmetic, and matching global scalar pointers.
Signed and unsigned wide pointers have separate types; `isize *` and
`long long *` are equivalent in this subset. Fixed-size arrays of these wide
scalar types support local and global storage, brace initialization,
indexed reads and writes, and array-parameter decay. Each function can use up
to 64 locals and 1 KiB of local storage; local arrays can use that frame subject
to element-size and alignment. Global arrays share the 16 KiB data region.
The generated entry shim follows the Microsoft x64 ABI and exits via syscall ABI
v4. File-scope `short`, `int`, `long`, `char`, and
`long long` objects use the app's 64-bit-addressed writable data page; scalar
constants and fixed `short`/`int`/`char` arrays can be initialized; fixed
`long long`/`usize` arrays are also supported. Global
`short *`, `int *`, and `char *` values can point to
previously declared arrays or string literals, and global pointers to named
struct objects can use `&object`; `main` initializes them with position-
independent x64 code, without fixed-address relocations. Globals are shared by
all compiled functions. Named structs support aligned `short`, `int`, `long`,
`char`, `long long`, and scalar pointer fields, local/global instances, and
`.`/`->` field reads and writes. Nested structs, struct arrays, and multiple translation units
remain unsupported. Limits are 8 struct types, 16 fields per type, 32 typedefs, 16 global
objects, and 16 KiB of global data. Struct pointers can be passed to helper
functions, which can access fields with `->`. The resident
compiler also supports fixed local `short` and `int` arrays (up to 512 short or
256 int elements within the 1 KiB frame), `short *`/`int *` locals,
array-to-pointer decay, address-of and dereference, indexed loads/stores, and
scaled pointer addition and subtraction, including element-count pointer
differences. String literals
are NUL-terminated `char *` values; `char *` locals and parameters, byte-indexed
reads and stores, byte dereference, byte-scaled pointer arithmetic, signed
8-bit `char` locals/parameters, and escaped character constants including
`'\\0'` are supported. Local `char` arrays can be initialized from string
literals or scalar brace lists, decay to `char *` arguments, and be indexed or
updated byte by byte. Host tests execute string traversal and mutable text
buffers. It rejects
pointer multiplication and incompatible pointer initializers/assignments. Local
array brace initializers support scalar expressions, trailing commas, omitted
element zero-fill, and excess-element rejection. Integer helper functions may
follow `main`; calls can be forward or nested and pass up to sixteen `short`,
`int`, `long`, `char`, `short *`/array, `int *`/array, `char *`, or struct-pointer
arguments through the
Microsoft x64 ABI, using RCX/RDX/R8/R9 and the caller's stack argument area.
Function definitions may appear before or after `main`; matching prototypes
may declare or omit parameter names. Array parameters index as `int *`. Compound
blocks and `for` initializers have lexical scopes with local shadowing. Return
types other than `int`, `short`, `long`, or `long long` remain unsupported and
report a source offset. `if`/`else` and `while`
support nonzero conditions plus signed or unsigned integer
`==`, `!=`, `<`, `<=`, `>`, and `>=` comparisons, and may use braces. Unary `!`/`~`
and short-circuit `&&`/`||` are supported. Basic `for` loops accept an optional initializer,
condition, and scalar-variable assignment update. `break` and `continue` target
the nearest enclosing loop, and `continue` in a `for` loop runs its update.
`return` is also valid inside conditional and loop statements, followed by the
function's required final return for the current subset.

`make bob64cc` builds a host-side C-to-B64E bridge using the configured
x86-64 GCC toolchain. For example, `bob64cc apps/bob64_smoke.c
build/bob64-app/smoke.b64e` compiles C that uses `bob64/app.h`, links it at the
process image base, and packages the executable sections into a checksummed
B64E file. The entry wrapper converts a normal C return into the process-exit
syscall, and `bob64/libc.c` supplies 64-bit-length memory and string operations.
`make bob64-app-test` validates this generated file with the B64E parser. The
resident backend now exists for the small `bob.c` demo; the host bridge still
supports the broader C source accepted by GCC while the resident backend grows.

Run `make bob64-handoff-test` for host-side PE checks. They validate the image
format and relocation directory, but do not execute privileged CPU
instructions or prove `ExitBootServices` succeeds on a given firmware.
To boot the interactive shell, copy `build/bob64-handoff/EFI` to the FAT32
drive's root so firmware starts its `EFI/BOOT/BOOTX64.EFI` image. A PS/2
keyboard or COM1 serial terminal can provide shell input.

The bob64 build uses a separate header and target. It does not change `bob.exe`,
`bob32.exe`, B16K/B32K images, or the existing resident compiler. Remaining
migration work includes growing the resident C subset, disk-backed persistence,
additional device coverage, USB input, and porting the rest of the applications.

## UEFI runtime test

Build the handoff image with `build-tool --bob64-handoff-test`, then run the
serial-checked QEMU/OVMF boot test:

```powershell
.\tools\test_bob64_qemu.ps1 -QemuPath C:\path\to\qemu-system-x86_64.exe `
    -OvmfCodePath C:\path\to\edk2-x86_64-code.fd `
    -OvmfVarsPath C:\path\to\edk2-x86_64-vars.fd
```

The test waits for long-mode entry, `ExitBootServices`, ring-3 application and
memory checks, timer and keyboard IRQs, and the kernel shell. It injects a PS/2
mouse move through QEMU's monitor and verifies that a ring-3 app receives the
event. It compiles `bob.c` with the resident compiler, runs the generated app
and checks its `bob!` output and exit status, then saves a file and the compiled
app into B64S firmware storage, resets the VM, and runs the app again after
restore. The C sample verifies a 16-argument call, forty local variables, a
64-element stack array, and local accesses outside the 8-bit frame-offset range.
It opens the desktop Applications
launcher while the editor and file-browser windows remain active, runs the
selected `bob!` app, and checks that the desktop returns to the shell. After
restore, it repeats the GUI launch and child-app round trip using a PS/2 mouse
click on Run App. Broader widget interaction still needs runtime coverage.
