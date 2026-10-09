# bob64 migration status

This document tracks the separate x86-64 OS target. `bob16` and `bob32` are
software-emulated custom instruction sets; the current `bob32` kernel is not an
x86 kernel and cannot be promoted to x86-64 by changing C integer widths. The
new target starts as an x64 UEFI application. Its opt-in handoff now takes
ownership of paging, early memory allocation, display output and a minimal shell;
the existing emulator/build outputs stay separate.

## Implemented stages

- `bob64/types.h` defines fixed-width scalar types and pointer/size types from
  compiler-provided x86-64 definitions, with compile-time width checks.
- `bob64/boot.c` builds as a PE32+ AMD64 EFI application at the standard
  removable-media path `EFI/BOOT/BOOTX64.EFI`. The image includes a PE base
  relocation directory and `DYNAMIC_BASE`, so firmware may load it away from its
  preferred `0x100000` image base.
- x64 UEFI transfers control in 64-bit long mode with its x64 ABI, stack and
  page tables. The entry checks the maximum CPUID leaves before reading feature
  bits, requires PAE and long-mode support, and confirms `EFER.LMA` before it
  prints `bob64!` through UEFI text output. `bob64/cpu.c` also decodes NX
  support and the CPU's physical-address width for paging setup. The safe probe
  returns through firmware; the handoff variant switches to bob64-owned tables
  and exits Boot Services.
- The boot entry reads the UEFI memory map through the x64 Boot Services table.
  `bob64/memory.c` parses the firmware-provided descriptor stride and seeds a
  sorted, coalesced first-fit allocator of 4 KiB conventional physical pages,
  keeping physical addresses in `u64`, clipping ranges to the detected CPU
  physical-address width, and reserving the first MiB. Allocation
  records make invalid and double frees fail; the independent live-allocation
  ledger supports up to 16,384 ranges, while free extents have a separate
  128-range limit. Boot reserves the replacement stack and table pool from this
  allocator, and the kernel continues allocating from the preserved state.
- The x64 UEFI ABI is Microsoft's x64 ABI: integer/pointer arguments use
  `RCX`, `RDX`, `R8`, and `R9`; integer/pointer returns use `RAX`; `RSP` is
  16-byte aligned at call sites with 32 bytes of caller-provided shadow space.
  The compiler data model is LLP64 (`char` 8, `short` 16, `int` 32, `long` 32,
  `long long` 64, pointer/`size_t` 64). bob64 code should use `u64`/`s64` and
  `usize`/`isize` for explicit widths rather than assuming `long` is 64-bit.
- `bob64/paging.c` builds four-level x86-64 page tables with 4 KiB
  leaves. Allocate/access callbacks keep physical page addresses distinct from
  currently accessible pointers; the builder enforces the CPUID-reported
  physical-address width. `bob64_page_map_range` validates canonical spans and
  maps byte ranges while preserving partial-page offsets. `bob64_page_protect`
  tightens leaf permissions and `bob64_page_unmap` returns the removed physical
  frame and flags; callers invalidate the TLB for live edits. The host suite
  walks low and upper canonical virtual addresses and physical/table addresses
  above 4 GiB, and verifies writable pages can become read-only/NX, be unmapped,
  and be mapped again. The handoff activates the root after enabling `EFER.NXE`
  when supported; QEMU runtime smoke tests verify active mappings through the
  kernel memory and user-page-protection checks.
- `bob64/bootstrap.c` composes that mapper into a bootstrap identity address
  space for the loaded EFI image, a replacement stack, and a pre-reserved
  contiguous page-table pool. It marks stack/pool pages NX when supported and
  rejects overlapping ranges or an undersized pool. Tests verify translation
  and permissions for all three regions. The EFI entry obtains the loaded-image
  protocol and uses its full-width image base and size to prepare and verify
  identity mappings for the actual image, an 8-page replacement stack, and a
  128-page table pool. The handoff also maps its memory-map buffer and supported
  GOP framebuffer. It builds the kernel GDT and exception IDT while the image
  is mapped. The default `--bob64` image still returns safely to firmware after
  preparation.
- `bob64/descriptors.c` builds kernel and DPL3 user code/data GDT entries, a
  full-width 64-bit TSS descriptor, the packed 10-byte GDTR/IDTR operand, and
  16-byte x86-64 IDT gates with split 64-bit handler addresses. Boot sets TSS
  `RSP0` to the initial kernel stack, and the handoff loads the task register.
  After heap startup, the kernel changes `RSP0` to a separate supervisor-only
  privilege-transition stack so user interrupts will not overwrite the paused
  kernel call stack. Each shell-launched application now gets its own zeroed
  16 KiB privilege stack; the previous TSS stack is restored before that
  allocation is released. Tests verify selector values, TSS offsets/base/limit,
  full-width stack replacement, alignment, DPL3 gates, IST, and gate type.
- `bob64/interrupts.S` contains 64-bit stubs for CPU exception vectors 0-31,
  normalizing hardware error-code and no-error-code frames before saving all
  general-purpose registers and calling the Microsoft x64 C dispatcher. The
  common path restores registers and uses `iretq`. `interrupts.c` wires an IDT
  to those stubs and a fatal fallback, and prints full 64-bit register values
  over COM1 and the framebuffer before halting. The opt-in handoff loads the
  IDT. The DPL3 vector `0x80` enters a dedicated syscall stub; runtime
  exception and syscall delivery still need firmware/emulator testing.
- `bob64/kernel.c` is the first kernel-owned C entry point. It checks the
  boot-information structure, active CR3, memory-map range, and stack, then
  reports the 64-bit root and map details, checks the growable kernel heap, then
  starts the polled shell. The filesystem and shell are still bring-up services,
  not yet a general-purpose OS.
- `bob64/handoff.S` provides the Microsoft x64 ABI entry bridge: interrupts are
  disabled, the kernel GDT is loaded and CS reloaded, the IDT is loaded, NXE
  and CR0.WP are enabled when supported, the TSS is loaded, CR3 is switched, and execution moves
  to the replacement stack before calling the kernel. The EFI boot path calls
  `ExitBootServices` using a freshly acquired map key, retrying a stale key up
  to three times.
- The handoff is opt-in so the original EFI probe stays available:
  `build-tool --bob64-handoff` emits a distinct image and
  `build-tool --bob64-handoff-test` runs host PE checks against it. Those checks
  cannot prove the privileged transition works on real firmware.
- The handoff queries UEFI GOP and maps a supported RGB/BGR framebuffer into
  the new address space as writable, non-executable, uncached memory. The
  kernel's `bob64/console.c` draws a compact 5x7 text font, wraps and scrolls,
  and mirrors output to COM1. Unsupported or invalid GOP modes retain the serial
  console fallback. Host tests exercise format encoding, glyph output, and
  invalid framebuffer bounds; actual display output still needs UEFI testing.
- `bob64/heap.c` provides a 16-byte-aligned, coalescing kernel heap with
  `alloc`, `calloc`, and `free`. Kernel entry adopts the page allocator and
  page-table pool seeded by boot, then grows the heap by mapping physical 4 KiB
  pages into a 64 MiB high-half range at `0xffff900000000000`. New pages are
  zeroed before use. Kernel entry smoke-tests multi-page allocations, alignment,
  zeroing, writes, frees, and double-free rejection. Host tests cover splitting,
  coalescing, overflow, invalid frees, and four-thread allocation/free stress.
  Heap metadata is protected by an atomic lock; kernel builds disable local
  interrupts while holding it to avoid same-CPU interrupt deadlocks. SMP is not
  enabled in the kernel, so hardware multicore runtime contention is untested.
- `bob64/keyboard.c` decodes PS/2 set-1 input, including shift, caps lock,
  editing keys, and Pause/extended sequences. IRQ1 now feeds a bounded 64-event
  queue used by the shell and app event service after the kernel remaps the PIC;
  the kernel keeps polling as a fallback if the controller is unavailable.
  IRQ12 feeds a bounded mouse-event queue through the same app event service;
  COM1 remains polled. Unit tests cover scan-code state, keyboard/mouse queue
  behavior, and shell command/edit behavior. A 100 Hz PIT IRQ now supplies a
  monotonic tick counter through the versioned syscall ABI. USB keyboards,
  history and completion remain future work.
- `bob64/filesystem.c` adds a flat, volatile binary-safe filesystem backed by the
  kernel heap. It supports 64-bit lengths, case-sensitive names, atomic file
  replacement, listing, reads, and deletion; names reject path separators and
  characters outside the safe filename set. The shell exposes `ls`, `cat NAME`,
  `write NAME TEXT`, and `rm NAME`. Host tests cover replacement, invalid names,
  binary NUL round-trips, and shell workflows.
- `bob64/snapshot.c` defines B64S version 1 as a pointer-free little-endian
  format with 64-bit total/data lengths and a CRC-32. Restore validates version,
  size, checksum, record bounds, names, and duplicate files before replacing
  the live filesystem. The shell's `save` and `restore` commands keep a RAM
  checkpoint and store a compressed B64S copy in a non-volatile UEFI variable
  when firmware capacity permits. Startup restores that snapshot after
  installing the default apps. Host tests cover round-trip, empty files,
  replace-on-restore,
  truncation, unsupported versions, checksum errors, and unchanged state after
  rejected input. A mock runtime-services suite verifies compressed variable
  save/restore, corruption rejection, and missing-variable behavior. QEMU/OVMF
  reboot testing saved a shell-created file to writable NVRAM, reset the VM, and
  verified the file after automatic restore. Disk-backed
  filesystem persistence and directories remain future stages; the
  app filesystem ABI supports bounded whole-file operations and ABI v12 streaming
  handles with 64-bit seek positions and bounded reads/writes; disk-backed
  persistence remains outstanding.
- `bob64/exec.c` defines B64E version 1, a flat position-independent 64-bit
  executable image with a versioned ABI, exact payload length, 64-bit memory
  size and entry offset, page-aligned code/data boundary, payload CRC-32, and
  zero-required reserved fields. The shared parser rejects malformed/truncated
  images before returning a view; a contiguous copy helper preflights capacity
  and address overflow, copies the image, clears the memory-only tail, and
  preserves entry addresses above 4 GiB. Mapped application loading is provided
  separately by `process.c`. The `bob64cc` packager keeps PE zero-fill data out
  of the B64E file while retaining its full 64-bit `MemorySize`; the desktop
  image is about 37 KiB on disk and still maps over 4 MiB at runtime. A
  synthetic B64E app now runs at CPL3, writes
  `bob!` through syscall v2, performs a bounded RAM-file round trip, and exits
  back to the saved kernel stack.
- `bob64/abi.h` starts application ABI v1 on the same Microsoft x64 calling
  convention used by the UEFI kernel: integer/pointer arguments in RCX, RDX,
  R8, and R9; scalar status in RAX; 32-byte shadow space; 16-byte call-site
  stack alignment; and no red zone. Entry receives one pointer to a fixed
  32-byte startup record containing a 64-bit argument count and argv pointer.
  The record and its strings are app-space data, so the contract does not pass
  kernel pointers. Tests check the record layout, entry call, nested calls, and
  stack-passed arguments. Syscall ABI v12 defines an ABI query, character and
  bounded buffer output, file reads and writes, pixel-display query/present,
  blocking keyboard and PS/2 mouse events, and process exit through interrupt
  vector `0x80`; user pointers are validated
  against the active process root before buffer reads or writes. Display apps
  submit a packed 0x00RRGGBB frame matching GOP dimensions; the kernel copies
  bounded chunks into the device surface without exposing its address. Tests
  cover the syscall and both GOP channel formats.
  `apps/bob64_display.c` exercises the app-facing wrapper and is packaged as a
  B64E image with a bounded 1024x768 static surface; runtime execution awaits
  UEFI verification.
- `bob64/window.h` ports the reusable window-manager behavior to app-owned
  fixed-capacity 64-bit state: nonzero 64-bit handles, full-width context
  pointers, visibility, focus, z-order, hit testing, title-bar dragging,
  clamping, dirty redraws, and keyboard/mouse event routing to window-local
  coordinates. Host tests exercise overlap redraw, wheel routing, focus and
  pointer values above 4 GiB. `apps/bob64_gui.c` is the first native desktop
  on this manager. It opens a named RAM text file (or `notes.txt` by default),
  lists RAM files in a separate window, supports selection/open/refresh and
  confirmed deletion, plus insertion, line breaks, cursor movement, click
  placement, scrolling, Ctrl+S save and an unsaved-exit prompt. It validates flat
  filenames, streams files in 4 KiB chunks, and supports up to 16 KiB of text,
  matching the resident compiler's source limit. The shell launches it with
  `run desktop.b64e [filename]`; host tests cover the editor buffer, packaged
  UI strings, file ABI and window manager. The full existing bob GUI, a file
  picker and real UEFI interaction remain to be ported/verified.
- `bob64/ABI.md` records the register-preservation rules, stack alignment and
  shadow-space contract, pointer ownership, and application entry shape. The
  compiler's aggregate behavior follows the Microsoft x64 ABI; aggregate
  returns are not part of the application-entry contract. `bob64/app.h` exposes
  syscall wrappers for native C apps without kernel-internal types. The small
  `bob64/libc.c` runtime provides memory and string operations with 64-bit sizes.
- `bob64/compiler.c` is the first resident x86-64 code-generation stage. The
  shell exposes `cc bob.c [OUTPUT]`, which packages the accepted source directly
  as B64E; `run app.b64e` launches it. The current syntax accepts `int` or
  `long`, `long long`, or `int main(void)`, initialized `int`/`long`/`long long`
  locals and assignments,
  `bob64_app_write` calls with typed `char *` expressions and scalar length
  expressions, and 64-bit integer
  return expressions with parentheses, unary minus, addition, subtraction,
  multiplication, signed division/remainder, and unsigned pointer-sized
  division/remainder. Signed division truncates toward zero. Its frame and entry
  wrapper follow the Microsoft x64 stack
  contract; syscall ABI v12 provides exit, graphics output, whole-file and
  streaming file I/O/list/delete,
  and input. Host compiler-model tests execute both 32-bit `long` truncation
  and 64-bit pointer-sized values, while the test harness asserts the host and
  UEFI targets use the documented LLP64 scalar widths.
  The compiler recognizes `usize`/`uintptr_t` as unsigned 64-bit scalar types
  and `isize`/`intptr_t` as signed 64-bit scalar types in declarations and
  function signatures. These are built-in type names. Bounded file-scope
  typedefs alias supported scalar types, scalar pointers, and previously
  declared named structs; alias chains work, while typedef arrays, function
  pointers, anonymous structs and header inclusion remain unsupported.
  `usize *`, `uintptr_t *`, `isize *`,
  `intptr_t *`, and `long long *` map to signed/unsigned wide-pointer types with
  eight-byte loads, stores, indexing and scaled pointer arithmetic. They work as local,
  parameter, return, global, and struct-field types; global pointers may target
  matching scalar objects. Fixed-size signed and unsigned 64-bit arrays now
  support local and global storage, initialization, indexing, mutation, array
  parameter decay, and eight-byte element addressing. Local automatic storage
  is bounded by the function frame; global arrays share the 16 KiB compiler data
  region.
  Function declarations and definitions can return `char *`, `short *`, `int *`,
  and pointers to declared structs. Returned pointers keep all 64 bits; return
  expressions must match the declared pointee type. Returning a pointer through
  `long long`/`usize` remains accepted for the compiler's existing raw-address
  tests. Host execution tests cover pointer identity through a prototype and a
  returned string literal used by the caller.
- `bob64/process.c` adds a callback-backed B64E loader core. It reparses the
  exact file before allocation, maps up to 16 MiB of app memory at
  `0x0000004000000000`, keeps code user-readable/executable and read-only, and
  maps data, BSS, and a 64 KiB stack writable/user/NX. It leaves an unmapped
  guard page below the stack, builds a 64-bit argv array and startup record in
  app memory, and prepares an ABI-aligned entry stack with shadow space. Failed
  loads roll back mappings and frames; unload removes each mapping before
  returning its physical frame. Host tests use physical frames above 4 GiB and
  verify argument contents, permissions, zero-filled BSS, guard placement, and
  rollback. The kernel wires these callbacks to its real page allocator and a
  private cloned root with a 34 MiB isolated app arena. At boot, the synthetic
  user app prints `bob!` and exits through the controlled syscall path.
  The shell's `run NAME [ARG ...]` command now reads B64E files from the RAM
  filesystem and launches them with app-owned arguments; `bob.b64e` is seeded
  as an example. General app services and persistent storage remain ahead.
- `build-tool --bob64` builds only the new EFI image. `build-tool --bob64-test`
  also validates the PE machine, PE32+ header, EFI application subsystem,
  64-bit type sizes, synthetic pointer arithmetic above 4 GiB, and the presence
  of base relocations for firmware-selected load addresses. It also exercises
  four-level mapping, translation, permissions, canonical-address checks,
  physical-width limits in both allocator and page tables, multi-page range
  mapping, protect/unmap/remap behavior, duplicate-map rejection, bootstrap identity mappings, synthetic
  CPUID feature decoding, 64-bit GDT/IDT layouts, and exception-stub wiring.

## Still required by the migration goal

- [x] Boot the handoff image under x64 UEFI emulation. QEMU/OVMF verified the
  long-mode entry, `ExitBootServices`, owned CR3, kernel heap, framebuffer
  console, COM1 diagnostics, ring-3 smoke app, and shell. A runtime script
  repeats the boot and checks its serial milestones.
- [x] Exercise ring-3 syscalls and user exception recovery after loading the
  GDT/IDT and new CR3. A deliberate invalid opcode was diagnosed with the full
  register frame and returned to the kernel; the corrected smoke app now exits
  normally. The IDT now also contains PIT, PS/2 keyboard, and PS/2 mouse IRQ
  handlers; PIT tick advancement is now checked at runtime, keyboard IRQ
  delivery is exercised by the shell/desktop sessions, and live mouse-event
  assertions remain open below.
- [x] Verify actual ring-3 text protection and data NX behavior. QEMU runtime
  probes attempt to write the read-only executable page and execute the NX data
  page; both receive the expected user page-fault error bits and return to the
  kernel. The kernel also checks that the stack guard is unmapped.
- [x] Grow the page-table pool dynamically in 64-page chunks, triggered while
  eight reserve pages remain. New chunks are identity-mapped using the reserve
  before becoming allocatable. Host bootstrap tests cover growth beyond the
  initial 128 pages, and the kernel now runs a 140-table smoke test.
- [x] Verify the expanded page-table smoke test under QEMU; the runtime script
  waits for its `dynamic page-table pool passed` marker.
- [x] Protect heap metadata with an atomic lock and disable local interrupts
  while kernel callers hold it. Four-thread host stress covers concurrent
  allocation, free, coalescing, and page shrinking. Kernel SMP is not enabled.
- [x] Reclaim full trailing heap pages when the final free block reaches the
  heap end. The heap keeps its first page mapped, returns released physical
  frames to the allocator, invalidates each removed virtual mapping, and
  regrows contiguously when later allocations need the pages again. Host and
  kernel smoke tests cover reclaim and regrowth.
- [x] Verify GOP, COM1, PS/2 keyboard, and mouse input under UEFI emulation.
  QEMU showed mouse movement and a click selecting a file-list row; keyboard
  input launched/closed the desktop and created a third window.
- [x] Verify live IRQ1 keyboard and IRQ12 mouse input under UEFI emulation.
  Host tests cover the IDT gates and bounded queues. Existing desktop runs
  exercise keyboard input; the QEMU runner now injects a PS/2 mouse move,
  launches a ring-3 event-wait app, and requires its nonzero movement event to
  arrive through the syscall ABI.
- [x] Add a 100 Hz PIT timer IRQ, tick counter, and user tick query. Host tests
  cover the syscall path, and the UEFI kernel now waits for a real tick before
  continuing; QEMU checks the `timer tick passed` marker.
- [x] Add synchronous B64E child launch through syscall ABI v12, with validated
  argv copying so native apps can open selected files or pass commands onward.
  The user-entry
  return bridge keeps eight nested return frames; per-app kernel stacks,
  syscall-service contexts, file handles, and window-server sessions are
  restored on return. Host syscall tests cover nested context restoration, and
  the boot image includes a `bob!` nested-launch smoke app; QEMU runtime testing
  confirms the child returns and the parent continues.
- [ ] Add USB keyboard support; COM1 remains polled.
- [x] Add an eight-command shell history on Up/Down and Tab completion for
  built-in commands plus file arguments to `cat`, `rm`, `run`, and `cc`. Host
  tests cover draft restoration, keyboard arrow translation, and unique command
  and filename completion.
- [x] Verify compressed B64S persistence across reboot with writable OVMF
  NVRAM. The kernel stores compressed snapshots in two alternating EFI slots,
  split across bounded variables, and commits the active-slot record last. Host
  tests cover multi-variable round-trip, corruption fallback, missing storage,
  and firmware without `QueryVariableInfo`. QEMU/OVMF runtime testing saved a
  file in the shell, reset the VM, restored the snapshot, and verified that
  file. Disk-backed filesystem persistence remains open.
- [ ] Port remaining device services and add USB keyboard support.
- [x] Add typed pointer returns to the resident x86-64 compiler for
  `char *`, `short *`, `int *`, and declared struct pointers. Tests cover
  prototype calls, string literal and struct pointer returns, and rejection of
  incompatible pointee types.
- [x] Add `void` helper functions and scalar `char` returns to the resident
  compiler. Void helpers accept explicit `return;` or fall through, can be called
  as statements, and are rejected in value contexts. Scalar `char` returns
  sign-extend from eight bits; main remains required to return a value.
- [x] Add typed pointers to `usize`/`uintptr_t` and `isize`/`intptr_t` values.
  Local/global/struct-field storage, parameters, returns, dereference, indexing,
  assignment, and 8-byte-scaled arithmetic are covered, including synthetic
  pointer values above 4 GiB.
- [x] Add fixed signed and unsigned 64-bit arrays with local/global storage,
  initialization, indexed reads/writes, and array-parameter decay. Local arrays
  are bounded to 16 elements and global data to the compiler's 16 KiB limit.
  File-scope `short`, `int`, `long`, `char`, and `long long` scalars plus bounded
  fixed `short`/`int`/`char` arrays now use initialized writable B64E data and
  RIP-relative x64 access across functions. Global `short *`/`int *`/`char *`
  initializers may target
  previously declared arrays or string literals. Named structs define aligned
  scalar/pointer layouts and support local/global instances plus `.`/`->` field
  loads/stores. Struct arrays, nested structs, and multiple translation
  units remain unsupported. Limits are 8 struct types, 16 fields per type, 16
  global objects, within the compiler's 16 KiB global data limit.
  Local fixed `short`/`int` arrays, `short *`/`int *` locals,
  dereference/indexing, scaled pointer arithmetic and pointer differences, and
  local brace initializers are
  implemented. `short` (16-bit), `int`/`long` (32-bit), and `long long`
  (64-bit) helper functions and matching prototypes before or after `main`
  support nested/forward
  calls with up to sixteen scalar, `short *`/array, `int *`/array, `char *`,
  `usize *`, `isize *`, or struct-pointer parameters, following the Microsoft
  x64 register and stack argument layout.
  Compound blocks and `for` initializers now have lexical local scopes with
  shadowing and stack-slot reuse after scope exit.
  NUL-terminated string
  literals, signed 8-bit `char` locals/parameters, escaped character constants,
  byte-scaled `char *` indexing, stores, dereference, and arithmetic, plus local
  `char` arrays with string or brace initializers, now support resident C string
  traversal and mutable text buffers. Struct pointer parameters can be passed
  through helper calls using the Microsoft x64 argument ABI; helpers can read
  and write fields through `->`.
  `if`/`else` and `while` support signed
  relational/equality comparisons, and expressions support unary `!` plus
  short-circuit `&&`/`||`. Basic `for` loops now emit initialization, condition,
  body, and scalar assignment update code. `break` and `continue` patch to the
  nearest loop; `for` continue enters its update clause. Early `return`
  statements work inside branches and loops; the subset still requires a final
  return at the end of each function.
- [x] Expand the resident compiler's bounded source, machine-code, and global
  data capacities from 4 KiB each to 16 KiB each. Host execution tests compile
  and run a source file larger than 4 KiB with generated code beyond the old
  page limit, and access an 8 KiB global array. B64E still separates read-only
  code from writable/NX data pages.
- [x] Expand resident C function frames from 128 bytes and 16 variables to a
  1 KiB frame and 64 variables. Code generation now uses disp8 or disp32 RBP
  operands as needed. Host execution tests cover forty local scalars, a 64-item
  local integer array and a 256-byte initialized character array.
- [x] Add bounded resident C typedefs for scalar, pointer, and named-struct
  aliases. Host execution tests cover aliases in declarations, arrays, struct
  fields, pointer parameters/returns, and member access through an aliased
  struct pointer.
- [x] Add signed division/remainder and unsigned `usize` division/remainder to
  resident C integer expressions. Host tests execute negative 64-bit operands
  and an unsigned value above 4 GiB; the QEMU resident-compiler smoke program
  covers signed truncation/remainder and wide unsigned arithmetic.
- [x] Add C-precedence bitwise complement, AND/XOR/OR, and left/right shifts.
  Host execution tests cover operator precedence, signed arithmetic shift, and
  unsigned right shift above 4 GiB; the QEMU resident-compiler smoke program
  exercises the same operators in the running kernel.
- [x] Add a kernel-owned per-app window manager and port the desktop's file and
  editor windows to its surfaces. Syscall ABI v12 creates, focuses, destroys,
  validates and copies app surfaces, composites z-order, routes input with
  64-bit handles and local/screen coordinates, and releases the session on app
  exit. Concurrent apps are still not displayed together.
- [x] Run the native desktop under QEMU/OVMF at 1280x800. Both managed windows
  rendered; keyboard input created a third window, Escape returned to the
  shell, and Ctrl+S saved editor text through the file syscall. The full screen
  and B64S shell commands were also exercised in the same UEFI session. The
  automated QEMU pass now opens the Applications window while both desktop
  windows remain active, launches a child app, and repeats the GUI/child-app
  round trip after B64S restore. The restored run opens the launcher, uses a
  PS/2 mouse click on Run App and X CLOSE, then verifies Escape returns to the
  shell.
- [x] Add a desktop launcher control for selected `.b64e` files. It runs the
  child synchronously and displays its exit status after returning to the
  editor; host packaging tests cover the control strings. QEMU/OVMF now opens
  the Applications launcher, starts its selected `bob!` child, and verifies
  that the desktop returns to the shell with a zero exit status.
- [x] Port reusable button behavior to the native framebuffer GUI. The
  `bob64/widgets.h` button handles hover/pressed states and activates only on
  an inside left-button release; the desktop uses it for Run App, APPS and
  X CLOSE. Host tests cover hit bounds and cancel-on-release-outside; QEMU
  verifies mouse launch and close interactions.
- [x] Port bounded single-line text entry to the native GUI and add Save As
  through the desktop's filename field. Host tests cover caret placement,
  insertion, deletion, horizontal reveal, and capacity bounds. QEMU clicks the
  field, edits a filename, saves through the GUI, verifies the file from the
  shell, and removes the smoke fixture.
- [x] Replace Bob64's two-press file deletion shortcut with a visible
  Yes/Cancel confirmation window. `D` and the file-window Delete button open
  the dialog; `Y`/Enter confirms and `N`/Escape cancels. The open editor file
  remains protected. The handoff build and full QEMU/OVMF regression pass;
  direct QEMU interaction coverage for this dialog remains to be added.
- [x] Port the Bob32 desktop's on-screen help overlay as a managed Bob64 help
  window. It is reachable from the Files panel or `H`/`?`, and documents the
  native desktop's current navigation, app-launch, delete, save, and focus
  shortcuts. Host packaging tests assert the help window's contents.
- [x] Add a bounded file-browser preview to the Bob64 desktop. Selection changes
  load up to 384 bytes into a separate scratch buffer, show text, mark truncated
  files, and identify executable/binary files without putting them in the editor.
  Host package checks assert the preview UI and its app/error states.
- [x] Port the argument-printing `echo` utility to the B64E startup ABI and
  install it in the default RAM filesystem. The UEFI smoke path passes a
  `bob!` argument and checks the native app output; the QEMU runtime script
  verifies the app marker and argument output.
- [x] Port a native `ls.b64e` app over the 64-bit file-list syscall. The kernel
  launches it during smoke testing and checks its output begins with the
  installed `ls.b64e` filename; QEMU runtime testing confirms the marker.
- [x] Port a native `cat.b64e` app over streaming file handles. The kernel
  smoke path reads a `bob!` RAM-file fixture, verifies the output, and removes
  the fixture afterward; QEMU runtime testing confirms the marker.
- [x] Port Bob32's notes utility as native `notes.b64e`, with a desktop-editable
  notes window and shell `show`, `put` / `edit`, `add`, and `delete` commands
  over the 64-bit file ABI. It rejects binary files and is packaged for the
  desktop app launcher. The kernel smoke path creates, shows, verifies, and
  deletes a `bob!` note; QEMU runtime testing confirms its marker and output.
- [x] Port Bob32's keyboard/mouse application launcher into the Bob64 desktop.
  It lists installed `.b64e` files, launches the selected app through the
  nested-app ABI, and supports keyboard/mouse selection plus run/close controls.
  Host packaging checks verify its visible title and shortcuts.
- [x] Match the text editor's Home/End line navigation from Bob32 and cover it
  with native editor unit checks.
- [x] Protect Bob64 editor contents from executable/binary files. The GUI now
  uses a scratch buffer, rejects `.b64e` and control-byte content, and leaves
  the current document untouched when a binary file is selected.
- [x] Expand the desktop editor and Notes app to 16 KiB using high-level
  streaming file helpers over the existing 4 KiB syscall transfers. Editor
  unit checks fill the complete buffer and reject the first excess character.
- [x] Keep zero-fill PE sections out of B64E file payloads while preserving
  their virtual memory sizes. Host packaging tests assert the desktop's
  multi-megabyte zero-fill tail is loaded from B64E `MemorySize`, and QEMU
  verifies the desktop runs and B64S save/restore still succeeds.
- [ ] Port the remaining applications to the kernel window API. Syscall ABI v11
  also provides bounded file reads/writes/list/delete, console I/O,
  full-screen presentation, blocking PS/2 keyboard/mouse events, ABI query, and
  process exit. The host `bob64cc`
  bridge continues to package the broader C subset supported by GCC.
- [ ] Port the remaining existing applications and full GUI/widget behavior;
  the current native text editor is an initial, limited desktop milestone.
- [x] Port Bob32's graphics demo as a 640x400 kernel-composited Bob64 window.
  The native `display.b64e` app draws colored panels and diagonal lines, prints
  `bob!`, and returns to the shell on X or Escape. The QEMU runtime test checks
  presentation, clean app exit, and compatibility with snapshot save/restore.
- [x] Port the system-information utility as native `info.b64e`. It reports the
  syscall ABI, available display resolution, uptime, and RAM filesystem usage;
  the kernel embeds and launches it in the application smoke path. The kernel
  smoke test checks its captured ABI, uptime, filesystem fields, and final
  `bob!` output; the QEMU runtime script confirms the marker and output.
- [x] Port the standalone application launcher to a native Bob64 window. It
  lists installed `.b64e` apps, launches the selected app through the nested
  app-run syscall, and closes on Escape/X/Close. Host packaging checks validate
  the B64E image; QEMU opens it, checks `bob!`, and verifies a clean return to
  the shell. The shell can also run the launcher with `APP [ARG ...]`; ABI v12
  validates and forwards the full argv vector to the selected child.
- [x] Run the kernel-generated ring-3 `bob!` app under x64 UEFI emulation. The
  QEMU runtime test also compiles `bob.c` through the resident shell compiler,
  launches the resulting B64E app, and checks `bob!` and a zero exit status.
  The source exercises local character/integer arrays, helper functions, array
  parameters, loops, pointer-based syscall output, computed string length,
  forty local variables, a 64-element stack array and a nested call with sixteen
  integer arguments (four register-passed and twelve stack-passed).
  The test saves the compiled app in B64S, resets, and launches it again after
  restore, then relaunches the GUI and a child app from the restored system.
- [ ] Complete the full acceptance flow and add runtime tests for broader
  calling-convention/stack cases and the remaining widget interactions.
  The ring-3 app image runs at a virtual address above
  4 GiB; host tests also cover synthetic high addresses and broad ABI/memory
  cases.
- [x] Verify bob64 boots in x64 UEFI emulation and performs its own kernel
  initialization without executing 32-bit kernel or app code. The broader
  migration remains active: the resident C subset, USB and other device
  support, disk-backed persistence, and remaining application ports still need
  work.

## Build and test

```text
build-tool --bob64
build-tool --bob64-test
build-tool --bob64-handoff
build-tool --bob64-handoff-test
tools/test_bob64_qemu.ps1 -QemuPath <qemu-system-x86_64.exe> -OvmfCodePath <edk2-x86_64-code.fd> -OvmfVarsPath <edk2-x86_64-vars.fd>
```

The safe output goes to `build/bob64/EFI/BOOT/BOOTX64.EFI`; the opt-in handoff
  image goes to `build/bob64-handoff/EFI/BOOT/BOOTX64.EFI`. Host tests validate
the PE/COFF architecture and subsystem, 64-bit pointers, synthetic addresses
  above 4 GiB, memory-map descriptor strides, page allocation/coalescing,
  allocation-ledger capacity, invalid free handling and overflow checks. The
  PowerShell QEMU test performs a real OVMF boot of the handoff image and checks
  serial markers through the kernel shell; it needs the QEMU executable and
  `edk2-x86_64-code.fd` paths supplied by the caller. The full script does not
  replace testing on physical UEFI hardware.

## Memory layout boundary

The current owned paging stage uses four-level tables. The image, bootstrap
stack, table pool, memory map, and supported GOP framebuffer are mapped at
identity addresses in the lower canonical half. Physical addresses below 1 MiB
remain reserved. The early heap occupies a 64 MiB window beginning at
`0xffff900000000000`. The B64E loader reserves a lower-half user image arena of
16 MiB beginning at `0x0000004000000000`; its 64 KiB user stack begins at
`0x0000004002001000`, with an unmapped guard page at `0x0000004002000000`.
The kernel smoke test clones the active root, privately copies the page-table
branch covering a 34 MiB image-and-stack arena, clears stale mappings in that
arena, and switches CR3 before loading and unloading a synthetic app. Other
kernel mappings remain shared. The kernel enters the synthetic app at CPL3
using an `iretq` frame. The app can query the syscall ABI, print characters
through `int 0x80`, and exit through a controlled return to the saved kernel
stack. The user-entry bridge enables interrupts for apps and restores the
caller's prior IF state when the app exits. The boot smoke app checks that the
keyboard IRQ path remains enabled after a user process returns. The versioned
syscall ABI also includes file handles and window services.
The fallback kernel privilege stack and each app's dedicated 16 KiB interrupt
stack are allocated from the supervisor-only heap. App runs also push a bounded
syscall context that preserves the parent's callbacks, user page table, scratch
buffers, and file handles; each app owns a separately allocated window-server
session. The user-entry return bridge keeps a bounded stack of nested return
frames, allowing synchronous child-app execution while preserving the parent.
The volatile
filesystem and B64S checkpoint live in the kernel heap; there is no full
physical direct map or filesystem mapping.
