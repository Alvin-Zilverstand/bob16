# bob64 application ABI v1 / syscall ABI v12

The kernel and first native applications use the Microsoft x64 ABI, matching
the x64 UEFI toolchain. `bob64/abi.h` is the source for the version and the
application-entry structure. B64E's ABI field must match
`BOB64_APP_ABI_VERSION`.

## Calls

- Integer and pointer arguments occupy positional slots in `RCX`, `RDX`, `R8`,
  and `R9`; later arguments are passed on the stack. Floating-point arguments
  use the corresponding `XMM0` through `XMM3` slots.
- Integer and pointer results use `RAX`; floating-point results use `XMM0`.
- Caller-saved registers are `RAX`, `RCX`, `RDX`, `R8`–`R11`, and `XMM0`–`XMM5`.
  Callee-saved registers are `RBX`, `RBP`, `RSI`, `RDI`, `RSP`, `R12`–`R15`,
  and `XMM6`–`XMM15`.
- The stack grows toward lower addresses. A caller aligns `RSP` to 16 bytes
  before `call` and reserves 32 bytes of shadow space. On function entry, the
  return address makes `RSP` 8 bytes off a 16-byte boundary. There is no red
  zone; native code must not use memory below `RSP` without allocating it.
- The fifth and later integer arguments follow the 32-byte shadow area; the
  fifth argument is at `[RSP+40]` on callee entry.
- Eligible plain C structs of 1, 2, 4, or 8 bytes return in `RAX`. Other
  aggregate return sizes use caller-provided storage passed as a hidden first
  argument. The app entry itself returns only a signed 64-bit status, so it
  has no hidden return pointer.

## Application entry

The entry point has the type:

```c
s64 bob64_app_main(const BOB64_APP_STARTUP *startup);
```

The pointer is passed in `RCX`; the signed exit status returns in `RAX`. The
32-byte startup record contains its size and ABI version, a 64-bit argument
count, a pointer to a null-terminated argv pointer array, and zeroed flags.
The record, pointer array, and strings belong to the application's address
space. The ABI never passes kernel pointers to an application.

The loader validates the record size/version, creates the stack and argument
storage in the application's address space, aligns the entry stack, and
provides the shadow area. `bob64_enter_user` enters at CPL3 with the startup
record in `RCX`; it preserves the kernel's nonvolatile registers and restores
the caller's interrupt-enable state when the app exits. User entry enables
interrupts so configured devices can deliver input. Returning from the entry
function is not supported: apps must use the exit system call.

## System calls

System-call ABI v12 uses interrupt vector `0x80`. Put the service number in
`RAX`; a returning call writes its result to `RAX`. The available services are:

| Number | Name | Argument | Result |
| --- | --- | --- | --- |
| 0 | Query ABI | ignored | `BOB64_SYSCALL_ABI_VERSION` |
| 1 | Write character | low byte of `RCX` | `0` on success |
| 2 | Write buffer | user pointer in `RCX`, byte length in `RDX` | bytes written or `-14` |
| 3 | Exit | signed status in `RCX` | returns to the kernel |
| 4 | Read file | name pointer in `RCX`, name length in `RDX`, writable app buffer in `R8`, capacity in `R9` | bytes read or negative error |
| 5 | Write file | name pointer in `RCX`, name length in `RDX`, app buffer in `R8`, byte length in `R9` | bytes written or negative error |
| 6 | Get display | ignored | pixel width in `RAX`, pixel height in `RDX`, or `-19` if unavailable |
| 7 | Present surface | packed pixel pointer in `RCX`, width in `RDX`, height in `R8` | number of pixels presented or negative error |
| 8 | Wait event | writable app pointer to `BOB64_EVENT` in `RCX` | `0` after keyboard or mouse input, negative error |
| 9 | List files | writable `BOB64_FILE_INFO` array pointer in `RCX`, entry capacity in `RDX` | entry count or negative error |
| 10 | Delete file | name pointer in `RCX`, name length in `RDX` | `0` or negative error |
| 11 | Create window | signed x in `RCX`, signed y in `RDX`, width in `R8`, height in `R9` | nonzero 64-bit handle or negative error |
| 12 | Destroy window | handle in `RCX` | `0` or negative error |
| 13 | Present window | handle in `RCX`, pixel pointer in `RDX`, width in `R8`, height in `R9` | number of pixels presented or negative error |
| 14 | Focus window | handle in `RCX` | `0` or negative error |
| 15 | Open file | name pointer in `RCX`, length in `RDX`, flags in `R8` | 64-bit handle or negative error |
| 16 | Read handle | handle in `RCX`, writable buffer in `RDX`, capacity in `R8` | bytes read, `0` at EOF, or negative error |
| 17 | Write handle | handle in `RCX`, buffer in `RDX`, length in `R8` | bytes written or negative error |
| 18 | Seek handle | handle in `RCX`, nonnegative signed 64-bit absolute offset in `RDX` | new offset or negative error |
| 19 | Close handle | handle in `RCX` | `0` or negative error |
| 20 | Get timer ticks | ignored | 64-bit count of 100 Hz PIT ticks since kernel initialization |
| 21 | Run application | name pointer in `RCX`, name length in `RDX`, writable signed 64-bit exit-status pointer in `R8`, argc in `RSI`, argv pointer-vector in `R9` | `0` after child exit, or negative error |

The first kernel smoke app queries the ABI, prints `bob!` one character at a
time, and exits through this path. Console and per-call file buffers are
limited to 4096 bytes; names are limited to 63 bytes. The legacy whole-file
calls remain available. Streaming opens accept `BOB64_FILE_OPEN_READ`,
`WRITE`, `CREATE`, `TRUNCATE`, and `APPEND`; at least read or write is required,
and truncate/append require write. Create initializes a missing file. Handles
belong to the active foreground app and are invalidated when it exits or the
file is deleted. Reads and writes advance a per-handle 64-bit position; seek
sets a nonnegative signed 64-bit absolute position. Append-mode writes target
the current end of file. Reads can be short at EOF, writes past the end
zero-fill the gap, and each transfer is bounded to 4096 bytes. The app header also
provides whole-file read/write helpers that loop over these bounded calls; the
desktop editor uses a 16 KiB text limit across four transfers. File enumeration accepts
capacity 1 through 32 and returns fixed 72-byte `BOB64_FILE_INFO` records
containing a 64-byte zero-terminated name and a 64-bit size; if the complete
directory does not fit, it returns `-28` without copying a partial result.
File deletion uses the same validated 1-to-63-byte safe filenames as reads and
writes and returns `-2` if the file does not exist. Display surfaces must match the current
GOP resolution and contain packed row-major `0x00RRGGBB` pixels; at most 16 MiB
may be presented per call. The kernel validates every covered user page before
copying pixels in small chunks, and converts channels to the firmware's GOP
format without exposing the framebuffer pointer. File reads require writable
user pages. `BOB64_EVENT` is a fixed 56-byte record with type, set-1 key code,
ASCII character, modifiers, window-local pointer position and movement, button
state, wheel delta, a 64-bit window handle, and screen coordinates. Whole-screen
events use a zero window handle. Extended set-1 keys set bit `0x100` in the key code; key-up events
have no character. Mouse buttons use the low three bits for standard buttons;
IntelliMouse IDs 3 and 4 add signed wheel input, with ID 4 adding buttons four
and five. `bob64_app_wait_event` blocks while the kernel polls PS/2 devices and
copies the event to writable app memory.
The current flat volatile filesystem stores arbitrary bytes, while names reject
path separators and characters outside its documented safe set.
`bob64/app.h` provides matching C wrappers, including `bob64_app_run` for
synchronous child launches. Syscall 21 accepts at most 16 argv strings and
4096 total bytes; `RSI` is argc and `R9` points to the caller-owned pointer
vector. The kernel validates and copies every pointer and string before it
starts the child. The caller supplies the full argv, including argv[0]. The child
exit status is copied to a validated app-owned pointer, and parent syscall and
window state is restored on return. User-entry state supports up to eight
nested app entries. A CPU exception from active user code returns an error
status to the kernel instead of halting the system. `bob64/window.h` provides a reusable
app-owned window manager using 64-bit handles and pointers; it routes the
versioned keyboard/mouse event records without kernel pointers. Syscalls 11-13
create, destroy, and present kernel-owned windows for the foreground app. Each
window has an opaque 64-bit handle and a retained kernel surface; presentation
must match the dimensions used at creation. The kernel composites up to 16
windows in z-order with an aggregate 16 MiB surface limit, routes input to the
focused window, and destroys the session when the app exits. Nonzero event
window handles identify the target; mouse coordinates are local to that window
and `ScreenX`/`ScreenY` retain absolute coordinates.
Only one app runs at a time, so cross-process desktop composition is still a
future step. Floating-point language types,
variadic calls across the kernel boundary, and aggregate application entry
results are not defined yet.
