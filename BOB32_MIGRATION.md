# bob32 migration and source audit

The active scope includes completing feasible C support, reviewing every source
file for bugs, and migrating the emulator/toolchain/applications to 32 bits.
These tasks are incomplete. This file tracks actual migration boundaries.

## Current execution modes

The emulator uses 32-bit general registers, accumulator and memory words.
B16K images and existing BASM run with 16-bit normalization and condition flags.
B32K images select native 32-bit word arithmetic and load/store behavior.
Both bob.exe and bob32.exe use this implementation.

Instruction encoding remains 16-bit, while native-mode program counters and
effective addresses are 32-bit word addresses. The original 65,536-word region
uses the existing RAM array; higher addresses use lazily allocated sparse pages
(up to 4,096 resident pages of 4,096 words each). B32K version 2 images can load
and start anywhere in the 32-bit address space. Their payload is currently
limited to 65,536 words by the boot-disk buffer. Version 1 images and supervised
program entry points retain their legacy low-memory bounds. The host C compiler
emits B32K v2 images with `--wide`, and the build generates a B32K kernel image.
Its shell, editor, filesystem and resident compiler run; supervised legacy apps
execute with 16-bit arithmetic compatibility. In B32 mode, resident `cc` and
`go` emit native kind-2 programs with 32-bit `int`, stored in the guest RAM
filesystem and preserved by B32S snapshots. The resident compiler remains a
small C subset, with fixed-size one-dimensional local `int` arrays in bob32.
The host compiler's `--wide-app` mode emits larger B32K v2 images at the
supervised app origin. `import32 HOST_PATH NAME` validates and stores one in the
guest RAM filesystem; `run NAME` loads and executes it from there. `run32 PATH`
remains available for direct host-path execution. The 4,096-word guest file
region is shared by eight slots, so an imported image occupies six metadata
words, its payload and a terminator, less space already used by other files.
The loader validates the header, checksum, exact file size, entry and app-region
bounds, then preflights sparse memory before loading. B32S version 2 snapshots
preserve packed native files. Version 1 fixed-slot B32S snapshots remain
readable and are converted during restore; compatibility mode continues to use
B16S.

The bob32 kernel is loaded at `0x10000`, above the 64K-word compatibility RAM;
the current image ends at `0x1D115`, leaving a gap before the native app origin.
Resident kind-2 programs and imported B32K v2 apps run from `0x20000`; resident
compiler output can use remaining packed filesystem capacity (up to 4,095
words for one file), while source text remains limited to 511 words. Legacy kind-1 apps load at
`0xC200..0xCFFF` and keep 16-bit arithmetic compatibility. The 4,096-word B32 heap is
at `0xD000..0xDFFF`; the bob16 heap remains `0xC000..0xDFFF`. Legacy
applications retain access to their shared heap while their downward stack
starts at `0xD000`.
The bob16 OS loads kind-1 programs at `0xA000` to leave room for kernel growth.
Native B32 apps
use a separate high-memory range.

The bob32 resident C compiler supports local pointers to integer-returning
functions with integer parameters. A pointer can be initialized from a declared
function and invoked indirectly; function-pointer parameters, return values,
globals and pointer arithmetic remain unsupported. This feature is not enabled
in the bob16 resident compiler.

Resident prototypes distinguish `int f(void)` from `int f()`. The latter keeps
unspecified parameter status, while this compiler requires calls to match the
eventual definition's arity so its stack-based calling convention stays sound.

## Wide image format

B32K version 1 retains the original 16-byte header and low-memory bounds. Version
2 uses a 24-byte little-endian header: magic, 16-bit version, 16-bit header size
(24), 32-bit origin, entry, word count and additive checksum, followed by
32-bit little-endian payload words. The loader validates address overflow,
entry range, payload limit, checksum and exact file length before changing RAM.
It loads the payload directly and starts at the 32-bit entry address. Instruction
words still use the low-16-bit encoding and require zero upper bits; data words
retain all 32 bits. Run `bob32.exe --boot PATH` to load a B32K image. B32
applications compiled with `--wide-app` use origin `0x20000`, and `run32` limits
their payload and entry to the supervised region below `0x100000`. B16K and
`--os` continue selecting the existing compatible kernel.

## Audit evidence and remaining coverage

Initial review covered the CPU state, fetch/address/ALU/store paths and image
loader in main.c, plus the snapshot width boundary in storage.c. It found and
fixed undefined negative left shifts in sign extension. Addition now uses
unsigned wraparound, and effective register-relative addresses use unsigned
arithmetic before address truncation, avoiding signed overflow in wide mode.

tests/test_cpu.c exercises the actual CPU implementation: immediate sign
extension, both word-width boundaries/flags, wide memory loads/stores, wide
image reading, guest boot-disk transfer and execution, and mode selection.
The OS/compiler and native console suites also run against the migrated CPU.

### Trap and return-register review

The JSR/PUTS/GETS return-address assignments now apply the selected word-width
normalization, preserving signed bob16 register values above address 0x7fff.
PUTS no longer uses the instruction register as a string temporary; it compares
the complete data word with zero before emitting its low byte. This fixes early
termination of a wide string word such as 0x00010000 and preserves the decoded
trap instruction. GETS computes its input length once rather than repeatedly
scanning the entire string while copying it.

The direct CPU suite now passes 47 checks, including both return-address widths,
wide supervised program results/parent restoration, instruction-state preservation
and full-width string termination. The compatibility suites pass 915 OS/compiler
checks and 28 native console checks. These are targeted findings from the current
review, not proof that every trap edge case or source line has been reviewed.

### Image loader and cycle-limit argument review

The --max-cycles parser previously accepted partial numbers and silently disabled
the limit for zero or invalid text. It now accepts only positive decimal values
within unsigned long range, rejecting signs, whitespace, trailing text and
overflow. Omitting the option still selects unlimited execution.

The direct suite passes 65 checks, adding malformed wide-image subprocess cases
for short headers/payloads, wrong checksum, trailing bytes, invalid entry and
version, plus cycle-limit boundary/invalid values and an actual limited emulator
run with exit status 2. The 915 OS/compiler and 28 native console checks also
pass. Loader validation and argument parsing have been reviewed; wider addresses,
format evolution and the remaining source audit are still unfinished.

### Assembler input and data directive review

Source parsing now closes the input file through one wrapper on both success and
failure. Physical source lines above 255 characters reject rather than becoming
multiple instructions. CRLF characters are token separators. .fill requires one
complete unsigned hexadecimal word and rejects trailing garbage or truncation.

.stringz now checks remaining memory before writing, fixing a possible host
out-of-bounds write. It emits exactly one terminator instead of appending a second
zero through the common instruction path. Following data therefore starts one
word earlier than with that bug; hand-counted offsets relying on the extra padding
need adjustment. The repository demo and compatibility suites still pass.
Assembly can use all 65,536 slots, with the next word rejected before writing.

The direct suite now passes 73 checks, including exact-capacity assembly and a
near-end string overflow attempt. The 915 OS/compiler and 28 console checks pass.
Instruction operand parsing/encoding, especially atoi conversions and register
validation, still needs review; this is not a complete assembler or repository
audit.

### Assembler numeric operands and branch flags

All fourteen atoi operand conversions now use a complete signed-decimal parser
with host-int range checks. Invalid register-shaped tokens cannot silently become
immediate zero; missing digits, trailing text and overflowing numbers reject.
Valid operands still clamp to the existing instruction field widths. Branch
conditions reject unknown letters instead of silently omitting those flags.
The documented NOP instruction now assembles as zero and rejects extra operands.

The direct suite passes 94 checks, including every numeric operand path,
malformed branch flags, preserved signed clamping/JSR/JSRR encodings and NOP.
The 915 OS/compiler and 28 console checks also pass. More encoding/decoder
round-trip checks and the other source files remain to audit; the full bob32
compiler/kernel migration is still incomplete.

### Storage service source review

storage.c has been read through for hashing, metadata validation, descriptor
ranges, save replacement/error cleanup, and restore validation/commit ordering.
The descriptor upper bound now uses subtraction rather than descriptor+4,
preventing unsigned overflow in the internal service API. The guest trap already
narrows its argument, but direct service callers must also be safe. Restore now
checks stream errors and close failure before committing any buffered words.

New direct cases validate empty/used snapshots and reject excessive lengths,
invalid kind/used flags, embedded text zeroes, missing terminators, duplicate
names, overflowing descriptors and wide-mode format use. The direct suite passes
105 checks; compatibility/persistence and console suites pass 915 and 28 checks.
This review does not establish power-loss durability. B32S now preserves full-width data; the remaining migration/audit areas stay open.

### Native instruction word boundary

Wide instruction fetch now rejects nonzero reserved upper bits rather than
silently truncating a 32-bit data word into a different instruction. The native
boot ROM stores zero-extended instruction words; legacy ROM/assembly words still
use signed 16-bit normalization. A supervised malformed wide instruction must
fault and restore parent CPU state, while legacy sign-extended RET remains valid.

The direct suite passes 107 checks, and 915 OS/compiler plus 28 console checks
passed at that stage. The instruction encoding remains compact; B32K v2 later
extends image addresses without changing instruction words. A native 32-bit
compiler/kernel remains unfinished.

### Register-controlled address boundary

JMP, JSRR, RET and PUTS consume register-held addresses. In native mode these
paths preserve the complete address; previously values such as 0x10000 aliased
address zero. Compatibility mode retains its existing 16-bit wrapping. CPU
checks cover all four transfers, sparse high-memory loads/stores, indirect
loads/stores, high-address console/input/disk buffers, supervised high-data
access, B32K v2 high-origin boot, top-of-address-space loading, and malformed
header rejection. Version 1 image loading remains low-memory compatible.

The entire repository has NOT yet been audited line by line. Remaining review
includes assembler/tokenization/error paths, all trap edge cases, image/storage
malformed input, compiler parsing/type checking/emission, arithmetic helpers,
kernel runtime/files/compiler/shell/input/editor, build code and test coverage.
Generated kernel.basm needs generator/round-trip validation rather than merely
assuming generated instructions are correct. Documentation examples and saved
formats also need migration checks.

## Remaining migration requirements

- [x] Define the 32-bit instruction/address ABI and emulator/image limits.
- [x] Add a 32-bit C compiler target and native arithmetic/runtime services.
- [x] Port the kernel, resident compiler, editor, memory allocator and filesystem.
- [x] Version persistent storage and verify compatibility/migration of saved files.
- Verify existing BASM, C applications and snapshots against compatibility mode;
  verify rebuilt applications against the new ABI.
- Complete the whole-source audit and repair findings with targeted tests.
- Complete the remaining full-C implementation documented in C_IMPLEMENTATION.md.

Native snapshot update: B32S version 1 used fixed 512-word file slots. Version 2 retains the 16-byte header and 4,312-word payload size, but packs bob32 file contents by length so native apps can exceed 511 words. Both use little-endian 32-bit words and a checksum over all four bytes. Version 1 snapshots are repacked during restore; version 2 is written going forward.

Native filesystem app workflow: `import32 HOST_PATH NAME` validates B32K v2 images and packs their origin, entry, word count, checksum and payload into a guest file. `run NAME` validates the packed image, reserves all sparse pages before loading, runs the app and reports format, storage, memory, fault and timeout failures. `save` and `restore yes` persist the app through B32S. Integration coverage imports and runs a 629-word image through both `run` and `run32` guest-name commands, snapshots/restores it, and runs it again; direct checks cover checksum corruption and sparse-allocation failure. The current suites pass 203 CPU checks, 1,813 OS/compiler checks and 28 console checks.

Storage address audit: native trap 7 preserves full-width descriptor and table addresses, allowing the relocated bob32 kernel's globals while checking that each table lies in low kernel RAM or the loaded kernel image. Out-of-range descriptors reject rather than alias. Legacy mode retains the signed 16-bit address representation. Native restore checks also verify changed-kernel handling: binary slots are omitted while source text survives.

Decoder reserved-bit audit: NOP and RET require zero low 12 bits, register JSR requires zero low 8 bits, and in-place NOT requires zero low 8 bits. These fields were ignored previously. Malformed encodings now fault in both execution modes; valid instruction encodings remain covered by OS/application checks.

Native trap argument audit: trap 3 accepts a full-width buffer address but keeps its capacity limited to 16 bits; trap 6 remains constrained to the supervised low-memory entry region. Legacy word-address representations remain accepted. Other trap/address paths still require complete review.

Native disk trap boundary: trap 4 rejects upper bits in its destination, offset and count before narrowing or transferring words. Valid current-range native boot reads and legacy mode remain covered. This closes another argument-aliasing gap; full-width address and image ABI work is still unfinished.

Assembler source lines now use a bounded byte reader that rejects embedded NULs rather than silently truncating them through C-string tokenization. It preserves the 255-character limit, CRLF handling and acceptance of a final unterminated line. The direct CPU/assembler suite passes 138 checks, including a binary fixture with an embedded zero; 1,568 OS/compiler checks also pass.

Assembler `not` now requires exactly one destination register or a destination/source register pair. It no longer accepts extra operands or treats an invalid second register as the one-operand form. The direct suite covers those malformed forms and passes 141 checks; the OS/compiler suite passes 1,568 checks.

All load/store register operands now reject every negative parse result, including malformed names and out-of-range register numbers. Previously those handlers rejected only missing tokens; invalid registers could flow into negative shifts and malformed encodings. Direct tests cover both `r8` and malformed names in each affected field. The suite passes 150 CPU checks; 1,568 OS/compiler checks pass.

Assembler trap validation now matches the emulator's implemented vector table (0 through 11). Vectors 12 through 15 previously assembled successfully but faulted when executed. New rejection cases cover 12 and 15. Direct decoder tests also confirm reserved register-form ADD/AND bits fault in legacy and wide modes. The direct suite passes 156 CPU checks, and the OS/compiler suite passes 1,568.
