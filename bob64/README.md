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
supplies kernel and app tick counts. USB HID boot-keyboard events share the
shell and GUI key-event queue, and boot-mouse events update the shared pointer
queue. Four-byte USB mouse reports also feed wheel events to GUI scrolling.
Directories remain open. The RAM filesystem can be checkpointed to the
dedicated GPT disk store with `save`; direct per-file block-backed storage is
still a future step.

Kernel startup now scans PCI configuration space through mechanism #1, follows
PCI-to-PCI bridges, and reports USB host controllers with their BAR addresses.
It maps the xHCI register area uncached, reads the controller version, limits,
scratchpad requirement, and runtime/doorbell offsets, then halts and resets the
controller. It identity-maps DMA pages, sets up the command and event rings,
starts the controller, and verifies a No Op command completion. It scans root
port status; the QEMU/OVMF profile attaches an emulated USB keyboard and checks
that one connected port is detected and reset, the xHC assigns a device slot,
and endpoint-0 GET_DESCRIPTOR requests read the device and configuration
descriptors. It parses the configuration, selects it, requests HID boot
protocol, and configures a keyboard or mouse interrupt-IN endpoint. It keeps a
transfer posted and re-arms after each report. Up to 32 directly connected
boot-HID devices use independent slots, rings, and report buffers, bounded by
the controller's advertised slot count. Each USB keyboard keeps its own held
keys and modifier state while feeding the common key-event queue. Mouse buttons
are combined across USB and PS/2 devices, while movement and wheel deltas update
the shared pointer and event queue. With the 8042 disabled, QEMU verifies USB shell typing,
mouse movement and wheel events delivered to a managed app window, plus the
keyboard path through launching the desktop and Help, closing Help, and
returning to the shell. The QEMU `many` profile verifies one keyboard and eight
mice configured simultaneously, along with their shared shell and GUI input
paths. A QEMU hot-unplug probe confirms root-port removal releases a held
  keyboard key, then re-adds the keyboard and verifies re-enumeration and a new
  interrupt report. The kernel also configures USB 2 hubs, marks their xHCI
  slots as hubs, powers ports when the hub supports switching, and reads
  downstream port status. A QEMU eight-port hub test detects its attached
  keyboard and mouse, resets both ports, addresses them through their xHCI
  route strings, configures their HID endpoints, and verifies keyboard and mouse
  input. QEMU hot-adds a keyboard behind the hub and verifies that the kernel
  reads and clears the changed port, resets it, enumerates the keyboard, and
  types through it after removing the original keyboard. The test holds a key
  during hot-unplug, verifies held input is released, and re-adds a keyboard to
  verify shell input recovers. Scanning is capped at 16 ports and 32 queued
  children.

The shell also supports `save` and `restore` using B64S version 1. The format
stores fixed-width lengths and arbitrary file bytes with a CRC-32, never raw
pointers. `save` keeps a RAM checkpoint and writes a compressed copy to UEFI
non-volatile storage in two alternating slots. Each compressed snapshot can
span up to six bounded variables, and the active-slot record changes only after
all chunks are written; restore falls back to the prior committed slot if the
active one is damaged. Bob64 restores that snapshot on the next boot; if firmware
storage is unavailable or full, the RAM checkpoint still works for the current
session. Firmware variable and total-store limits apply. If firmware does not
implement `QueryVariableInfo`, the kernel still attempts the write and reports
the firmware's result. The OVMF test script verifies save, reset, and restore.

The kernel installs `echo.b64e`, a native 64-bit app that writes its startup
arguments and a newline. `run echo.b64e bob!` exercises the
pointer-width-safe startup record and makes the utility available to the
desktop's Run App action. The UEFI smoke path passes `bob!` through the startup
record and verifies those exact output bytes. Run `echo.b64e gui TEXT ...` to
view the arguments in a managed window; use Up/Down or the mouse wheel to
scroll and Escape or **CLOSE** to return.

`ls.b64e` is also installed by default. Run `run ls.b64e` to list RAM files and
their byte sizes through the application-facing file-list syscall. Its boot
smoke check validates a real returned filename in app memory. Run
`run ls.b64e gui [FILE]` for a managed window with file names and sizes. A
double-click, Enter, `O`, or **OPEN** runs a selected `.b64e` app or opens an
ordinary file in `cat`'s text viewer. Up/Down and the mouse wheel change
selection; an optional filename selects the initial row. The original shell
listing remains the default.

`cat.b64e` reads a named file using 4 KiB streaming transfers, preserving its
contents and adding a final newline only when needed. `run cat.b64e FILE` uses
the same file-handle ABI available to other native applications. Run
`run cat.b64e gui FILE` to open a managed text-viewer window for files up to
16 KiB; use Up/Down, Page Up/Down, or the mouse wheel to scroll, then Escape or
Close to return.

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
kernel image, and its output ends with `bob!` for smoke testing. Run
`run info.b64e gui` to see those values in a managed window; press `R` to
refresh the file and uptime figures, or Escape/Close to return.

`launcher.b64e` opens a managed native window listing installed `.b64e` apps.
Run `launcher.b64e APP [ARG ...]` from the shell to start an app directly with
its full argument vector; the launcher forwards argv[0] and the remaining args.
Use Up/Down and Enter (or click an app and **RUN APP**) to start a selected app;
Escape, `X`, or **CLOSE** returns to the shell. This standalone launcher starts
the app picker when started without arguments. When given `APP [ARG ...]`, it
launches that app directly and forwards its full argument vector.

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
The kernel compositor tracks surface, focus, and movement damage and updates
only the affected screen region; pointer events that change no pixels skip
framebuffer writes.
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
kernel compositor, prints `bob!`, and returns to the shell through its clickable
Close button or the X/Escape shortcuts.
The bob64 build packages it as `build/bob64-app/display.b64e`, installs it in
the RAM filesystem, and QEMU tests the window presentation and clean return.
The app-owned drawing surfaces are zero-fill B64E data, so they do not inflate
firmware snapshots. `bob64_app_wait_event` receives typed PS/2 key-down/up
events, including Shift, Caps, Control, Alt and extended arrow-key codes, plus
mouse movement, buttons and IntelliMouse wheel events.
PIT IRQ0 supplies 100 Hz ticks; keyboard IRQ1, USB HID boot reports and mouse
IRQ12 feed the shared event queues. Mouse position and packet decoding happen
in the IRQ handler. USB support covers directly connected boot-HID keyboards
and mice, plus boot-time and hotplug enumeration of HID devices behind USB 2
hubs. Root-port and hub-child removal release held input, and reconnect triggers
re-enumeration. QEMU verifies both paths. The kernel enumerates a SuperSpeed
USB SCSI Bulk-Only Transport device, configures both bulk endpoints, and issues
INQUIRY, READ CAPACITY, and READ(10) commands. A large-disk probe exercises
READ CAPACITY(16) and reads a verified sector above 2 TiB with READ(16). The
QEMU storage-hotplug probe
removes and re-adds the device, then verifies the same commands succeed after
re-enumeration; a SCSI CHECK CONDITION triggers one REQUEST SENSE and one retry.
A synchronous storage transfer now observes xHCI port-change events while
waiting for its completion; removal of that transfer's root port aborts the
request promptly and leaves the event queued for normal device teardown.
`bob64/block_device.h` defines the shared 64-bit LBA interface with bounded
transfers, read-only enforcement, and backend read/write/flush callbacks; the
xHCI storage driver registers through this interface. The SCSI layer uses
READ/WRITE(10) when the LBA range fits and READ/WRITE(16) otherwise, and falls
back from READ CAPACITY(10) to READ CAPACITY(16) for large disks.
`bob64/partition.c` validates the primary GPT header and entry-array CRCs,
checks usable-LBA bounds, and discovers the dedicated bob64 data partition
type GUID `7b616264-3030-4634-9a21-424f42363401`. The normal handoff build keeps
storage read-only unless this validated partition is present.

The partition stores two checksummed B64S snapshot generations. Save flushes
the payload before committing its header, and restore can fall back to the
previous valid generation. The QEMU snapshot test checks save, reboot, restore,
and the raw disk contents. Its large-LBA variant puts the partition above 2 TiB
and verifies those operations through READ/WRITE(16). The explicit storage
write test also writes a pattern to LBA 1, checks bounds, issues SYNCHRONIZE
CACHE, reads the sector back, and verifies the image after QEMU exits, including
across hotplug. The separate
`bob64-handoff-storage-active-test` image is built with
`build-tool --bob64-handoff-storage-active-test`; its QEMU probe removes a
throttled disk during a pending READ(10) transfer and checks request abortion
and device teardown. Host tests cover USB descriptors and BOT command/status
validation. General filesystem mounting and direct per-file block storage
remain future work.
`bob64/window.h` provides app-owned windows, 64-bit handles/context pointers,
focus, z-order, title-bar dragging, hit testing, dirty redraws and local event
routing. The v12 kernel API additionally creates, destroys and composites up to
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
cancel. Press `M` from the Files window to open a popup menu with Open, Run,
Delete, and Close actions; use Up/Down and Enter or click an item, and press
Escape or click outside to dismiss it. Type into the editor, use arrow keys to move, Home/End to jump to the current line edges, Backspace to delete,
click in the text to place the cursor, Enter for a new line, and Ctrl+S to save.
The editor scrolls to keep the caret visible. Click the **NAME** field above
the editor to place its caret and edit the destination; Enter or Tab leaves the
field, and Ctrl+S saves the document under that name. Names accept letters,
digits, period, underscore or hyphen; executable names are protected. Escape
returns to the shell; unsaved text or name changes require a second Escape.
The QEMU GUI acceptance test verifies that the first Escape leaves a dirty
editor open, another key cancels the warning, and the second Escape exits
without changing the saved file. It also inserts text into a fixture, saves it
with Ctrl+S, and checks the exact saved line through the shell.
Select a file and press Enter to open it in the editor, or run a selected
`.b64e` app; `O` always opens a regular file and `A` or **Run App** launches
the selected app. The desktop resumes with the child's exit status when it
finishes. Click **APPS**
in the file window or press `P` from any desktop window to open the Applications
window. Use Up/Down to select an installed `.b64e` app, press Enter or `A` to
launch it, and click **X CLOSE** or press Escape/`X` to close the launcher.
Click **HELP** or press `H`/`?` from the Files window to view the desktop
shortcuts; Escape, `X`, or **CLOSE** dismisses it.
The desktop buttons share `bob64/widgets.h`, which handles hover and pressed
feedback and activates on a left-button release inside the control. The same
widget header provides the bounded popup menu used by the Files window.
The desktop editor supports text files up to 16 KiB, matching the resident
compiler's source limit. It uses bounded 4 KiB streaming transfers; larger
files are protected from accidental overwrite. Under QEMU/OVMF at
1280x800, both windows rendered, PS/2 input created an additional window and
returned to the shell, a mouse click selected a file-list row, and Ctrl+S saved
editor text through the file syscall.
The editor refuses `.b64e` files and text containing binary control bytes;
press Enter or `A` to run executables.
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
accepts `int`, `short`, `long`, or `long long` main returns; scalar local
declarations with or without initializers; local assignment; calls to
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
full result. `unsigned long long` (with optional trailing `int`) uses the
unsigned 64-bit `usize` representation, including unsigned comparisons and
calls. Other unsigned object widths are not supported yet. Short globals,
locals, struct fields, function parameters and
returns are sign-extended on load and use 16-bit storage. The resident subset
supports `sizeof(type)` for supported scalar and pointer types, typedef aliases,
named structs, and single-dimension fixed arrays; it yields an unsigned
pointer-sized value and follows the documented LLP64 sizes. `sizeof(void *)`
  reports the eight-byte native pointer width. Opaque `void *` values can be
  stored in locals, globals and struct fields, assigned, passed and returned,
  and compared with object pointers. Global initializers can point to scalar
  objects, arrays and strings. Explicit casts support scalar and pointer types,
  including integer/pointer round trips. Casts to `char`, `short`, and `int`
  truncate to the target width and sign-extend; pointer-sized integer casts
  preserve all 64 bits. Dereference, indexing, and arithmetic require a typed
  pointer.
  `sizeof(expression)`
supports scalar, pointer, array-variable, indexed-element, and string-literal
expressions; expressions remain unevaluated, and arrays and string literals
retain their full size. Incomplete types remain unsupported. The resident subset
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
blocks and `for` initializers have lexical scopes with local shadowing. Helper
functions may return the supported scalar types, `void`, or supported typed
object pointers; `main` must return a value. Only `void` helpers may fall
through without a return statement. Function-pointer and array return types
remain unsupported and report a source offset. `if`/`else` and `while`
support nonzero conditions plus signed or unsigned integer
`==`, `!=`, `<`, `<=`, `>`, and `>=` comparisons, and may use braces. Unary `!`/`~`
and short-circuit `&&`/`||` are supported. Basic `for` loops accept an optional initializer,
condition, and scalar assignment or prefix/postfix increment/decrement update.
Prefix and postfix `++`/`--` support scalar locals and typed pointer locals;
pointer increments use the pointee size. `break` and `continue` target
the nearest enclosing loop, and `continue` in a `for` loop runs its update.
`do`/`while` loops execute their body once before testing the condition, and
`continue` routes through that condition, including inside nested loops.
Named `enum Tag { ... };` definitions support up to 64 integer or character
enumerators, implicit numbering, literal initializers, enum-typed declarations,
and `typedef enum Tag Alias;`. Enum constants work in expressions, global
scalar and array initializers, and `switch` case labels. Enum types use the compiler's 32-bit `int` representation;
initializer expressions beyond a single signed integer, character, or prior
enumerator are not supported yet. `switch` supports integer expressions, integer,
character, and enum-constant `case` labels, one optional `default`, lexical
fallthrough, and `break`; `continue` still targets the nearest enclosing loop.
Case labels are limited to the switch body's top level in this subset.
`return` is also valid inside conditional and loop statements, followed by the
function's required final return for the current subset.

`make bob64cc` builds a host-side C-to-B64E bridge using the configured
x86-64 MinGW GCC toolchain. `bob64/app.h` checks at compile time that native
applications use 64-bit pointers and the same LLP64 scalar widths as the
resident compiler. For example, `bob64cc apps/bob64_smoke.c
build/bob64-app/smoke.b64e` compiles C that uses `bob64/app.h`, links it at the
process image base, and packages the executable sections into a checksummed
B64E file. The entry wrapper converts a normal C return into the process-exit
syscall, and `bob64/libc.c` supplies 64-bit-length memory operations, bounded
string helpers, and `bob64_snprintf` for strings, characters, signed and
unsigned integers, pointers, flags, widths, and precision. Floating-point
format conversions are rejected. Host tests cover edge behavior, truncation,
integer boundaries, and 64-bit pointers; the boot smoke app runs the helpers and
formatter inside a packaged ring-3 x86-64 application.
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
`bob32.exe`, B16K/B32K images, or the existing resident compiler. B64S snapshots
can be stored in a dedicated GPT bob64 data partition on USB mass storage. The
store alternates between two checksummed slots, keeps the previous generation
until the new snapshot is committed, and falls back to the older slot if the
newer payload is damaged. Boot and the shell restore path prefer this disk store
and retain UEFI-variable snapshots as a fallback. The kernel never formats a
disk: the GPT partition must be created separately, and ordinary disks remain
read-only unless that exact validated partition is present. Remaining migration
work includes growing the resident C subset, directories, additional USB
classes and hub features, and fuller runtime coverage of the ported
applications.

For an emulator disk image, create a blank raw GPT image with the dedicated
partition using:

```powershell
.\tools\new_bob64_disk.ps1 -Path .\build\bob64-data.img -SizeMiB 64
```

The command refuses to overwrite an existing image unless `-Force` is passed.
Attach the raw image as USB mass storage. It contains GPT metadata and an empty
bob64 data partition; it does not create a filesystem or format existing media.

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
app into B64S persistent storage, resets the VM, and runs the app again after
restore. The C sample verifies a 16-argument call, forty local variables, a
64-element stack array, and local accesses outside the 8-bit frame-offset range.
It opens the desktop Applications
launcher while the editor and file-browser windows remain active, runs the
selected `bob!` app, and checks that the desktop returns to the shell. After
restore, it repeats the GUI launch and child-app round trip using a PS/2 mouse
click on Run App. QEMU verifies button cancellation when release occurs outside
the launcher Close control, followed by a successful inside click. It also
verifies popup dismissal and mouse selection of Close Menu, plus filename-field
Home/Right caret motion, insertion, and Backspace; further widget input
combinations still need broader runtime coverage. To test USB keyboard
removal while a key is held and automatic reconnection, run the same script with
`-ProbeOnly -ProbeDevice disconnect`; the kernel must release held input, then
re-enumerate the keyboard and receive an interrupt report.
Run `-ProbeOnly -ProbeDevice hub` to verify hub setup, downstream keyboard and
mouse input, and hot-add enumeration with an emulated eight-port hub.

To run the full shell, GUI, and snapshot regression with the disk-backed B64S
store, add `-DiskSnapshotTest`. The script creates a temporary raw GPT image,
attaches it as USB storage, verifies save and reboot restore from the partition,
and removes the image afterward:

```powershell
.\tools\test_bob64_qemu.ps1 -QemuPath C:\path\to\qemu-system-x86_64.exe `
    -OvmfCodePath C:\path\to\edk2-x86_64-code.fd `
    -OvmfVarsPath C:\path\to\edk2-x86_64-vars.fd `
    -DiskSnapshotTest -TimeoutSeconds 180
```

To exercise block writes without touching a user's disk, build the isolated
storage-test image with `build-tool --bob64-handoff-storage-test`. Then run:

```powershell
.\tools\test_bob64_qemu.ps1 -QemuPath C:\path\to\qemu-system-x86_64.exe `
    -OvmfCodePath C:\path\to\edk2-x86_64-code.fd `
    -OvmfVarsPath C:\path\to\edk2-x86_64-vars.fd `
    -ImageRoot build\bob64-handoff-storage-test -ProbeOnly `
    -ProbeDevice storage-hotplug -ExpectStorageWrite
```

The script creates and removes temporary raw disks for both attachment phases.
The separate storage-test kernel target is opt-in; do not boot it with a disk
containing user data.
