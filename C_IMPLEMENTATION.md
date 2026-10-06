# Expanding C support

The active goal is to implement all C that can reasonably run on bob16. This
is unfinished. Successful subset tests are not proof of a complete C compiler.
Both the host compiler and shell cc/go workflow must be covered before claiming
the goal complete. Existing programs and the nano-like editor must keep working.

## Work required

- Define and test a consistent target ABI: character/integer widths, signed and
  unsigned conversions/promotions, pointer arithmetic, alignment, aggregate
  layouts, calling conventions, floating-point and wider integer representations.
- Expand declarations: complete declarators, typedefs, qualifiers, storage
  classes, lexical scopes/shadowing, prototypes, function pointers and variadics.
- Expand types: unsigned/signed integer variants, enums, structs/unions, nested
  arrays/pointers and supported floating-point types with software operations.
- Expand expressions: sizeof, conditional/comma operators, full lvalue/type
  checking, aggregate access, casts, constant expressions and proper conversions.
- Expand statements: switch/case/default, labels/goto and all loop forms with
  correct nesting, scope and break/continue behavior.
- Expand initialization: strings, aggregates, nested/designated initializers,
  zero initialization, inferred bounds and relocation of address constants.
- Support translation units/linkage and preprocessing/includes/macros, supplying
  target headers rather than reusing incompatible host ABI declarations.
- Supply feasible standard library services, including strings/memory, numeric
  conversion, formatted console/file I/O, allocation and mathematical helpers.
  Specify unavailable external facilities explicitly rather than accepting them
  with incorrect behavior.
- Make the shell workflow use the expanded compiler. Current kernel code nearly
  fills its reserved region; compiler/storage architecture and resource limits
  need revisiting instead of silently accepting unsupported source.
- Add positive, negative and semantic native-execution tests for each feature,
  preserving source/output files after failed compilation and program faults.

## Verified additions

The host compiler now implements do/while, including continue to the condition
and break to the loop exit. Local fixed-array initializer lists initialize the
listed elements and zero the remainder; excess elements are rejected.
`host_compiler_and_runtime` executes these features natively and prints bob!.
Conditional ?: selects exactly one branch; comma expressions preserve left-to-
right sequencing while argument and initializer separators remain distinct.
Sizeof handles current scalar/pointer types, declared arrays and string literals
without evaluating its operand; void operands are rejected. It still needs the
full type model (including pointers to arrays, aggregates and qualified types).
Native tests exercise nested conditionals, skipped side effects, comma-based
for-loop steps, argument separators, zero-filled arrays, do-loop control and
unevaluated sizeof increments. Negative tests reject excess initializers, void
sizeof and malformed conditional expressions. These additions are not yet
implemented in the resident compiler; the full goal remains active.

Block locals now activate at their declaration, can shadow outer locals,
parameters and globals, and expire at block exit. For-declaration scope ends
after its loop. Tests reject duplicate locals/parameters, use before declaration
and out-of-scope names. Frame allocation is fixed per function and includes all
block declarations, so jumps do not require runtime stack adjustment.
Function-scoped labels/goto support forward/backward transfers, including
separate functions with the same label name. Missing/duplicate labels fail
before output files are written. Native tests exercise these transfers.

Switch/case/default now emit native dispatch, preserve fall-through and evaluate
the controlling expression once. A separate break-context stack distinguishes
loop/switch breaks from continues to enclosing loops. Nested switch labels are
collected independently. Duplicate cases/defaults and labels outside switches
are rejected; continue without an enclosing loop is rejected.
Integer constant expressions now fold arithmetic, bitwise, shift, comparison,
logical and conditional operators using the current 16-bit model. Zero divisors
and invalid shift counts fail compilation. Native tests cover folded bounds,
initializers/cases and nested dispatch. The regression suite passes 289 checks.

## Resident workflow remains required

The shell cc/go compiler is still the original small native compiler. The host
compiler additions above do not satisfy that part of the goal. The supplied
kernel occupies 36,009 of its available 36,096 words, program files hold 511
words, and the machine has 65,536 word addresses. Supporting substantially larger
native programs and a larger compiler requires revisiting code/data placement,
file storage, loading/relocation and compiler resources. Those changes must
preserve native execution, user sources, existing editor behavior and recovery.
Full types, aggregates, linkage, preprocessing/headers and library services
remain required; the goal is not complete.

## Array/string improvements

The host compiler preserves explicit string lengths through tokenization,
concatenation, constant pooling and emission. Embedded zero characters no
longer truncate literals or cause distinct literals to share incorrect data.
Sizeof uses the complete literal length rather than strlen.
Array bounds can be inferred from initializer lists or character strings.
Character string initialization supports zero-filled padding and the standard
exact-text-size case without a terminator. Undersized arrays, uninitialized
unsized arrays and string initialization of integer arrays are rejected.
Native tests cover global/local inferred arrays, embedded-zero literals,
concatenation, deduplication, padding, exact-size initialization and sizeof.
299 regression checks pass; the kernel rebuilds at the same 36,009 words.
These features remain host-side until the resident compiler architecture and
full type/declarator model are expanded.

## Declaration and relocation improvements

File, block and for declarations now support comma-separated declarators.
Each name starts from the shared base type and applies its own pointer stars,
array bounds and initializer; groups do not introduce an extra block scope.
Tests cover mixed scalar/pointer/array declarations, initialization order,
for-loop scope and same-scope duplicate rejection.
Global scalar pointers can initialize from string literals, addresses of global
objects, array decay/element addresses and constant offsets for current word
element types. Data relocations retain their addend through code compaction.
Native tests verify relocated pointers, embedded-zero string offsets and writes
through global pointers. Reading another global pointer's runtime value is not
accepted as a static address constant. Full declarators, compatible-type checks
and aggregate pointer scaling remain required.

## Function declaration improvements

The host parser retains file-scope prototypes instead of discarding them.
Validation rejects conflicting return kinds, parameter kinds/counts, parameter
initializers and object/function name collisions. Empty old-style declarations
remain unspecified and can precede a typed definition. Unused declarations do
not need a definition; called functions still need native code in this unit.
One-dimensional array parameters (sized or unsized) adjust to pointers, consume
one argument word and report pointer size under sizeof. Tests exercise repeated
compatible declarations, forward calls, array traversal, returning a passed
pointer and negative declaration conflicts. Full pointee, qualifier, promotion
and old-style compatibility checks require the richer type model.
Negative compiler tests also verify that rejected source preserves both prior
BASM and binary output files.

## Derived type foundation

The host compiler now interns pointer and array descriptors carrying element
type, indirection depth and array bound. Pointer declaration/definition checks
distinguish int*, char*, void* and additional indirection. Array parameters
adjust to pointers to their actual element type rather than a generic pointer.
Array objects retain their element type separately from frame/storage size.

Sizeof uses expression type inference for addresses, dereferences, subscripts,
calls, assignments, comma expressions and conditionals. Tests verify *&array
retains the array bound, comma/conditional array operands decay to pointers,
and pointer-array dereferences retain their pointee type without evaluation.
Invalid void dereferences and non-pointer subscripts/dereferences in sizeof
are rejected. Signature tests reject differing pointee kinds and depths.
431 regression checks pass and the kernel still rebuilds at 36,009 words.

This is a foundation rather than a complete type checker. Qualifiers, all
declarator forms, aggregate/wide/floating types, arithmetic conversions,
and constraints on ordinary evaluated expressions still need
implementation. The resident compiler and standard library work also remain.

## Multidimensional arrays and aggregate initialization

The host compiler retains all array dimensions, adjusts array parameters to
pointers to rows, and uses recursive storage sizes for frames, data and sizeof.
Pointer arithmetic, increments, compound addition/subtraction and pointer
differences scale by the pointee size. Array assignment/increment, incompatible
pointer subtraction, pointer-plus-pointer and void pointer arithmetic are rejected.

Initializer parsing recursively flattens nested braces into storage order,
zero-fills omitted subobjects, supports brace elision and infers an omitted outer
bound. Character-array rows accept strings, including braced strings, exact-size
strings without a terminator and padded strings. Global pointer arrays and nested
element addresses retain relocations through code compaction. Scalar braces are
accepted. Struct/union aggregates remain unfinished.

Native tests exercise three-dimensional arrays, row traversal and differences,
local/global nested initialization, partial rows, inferred bounds, strings,
pointer-array relocations and reversed subscripts in static addresses. Rejection
tests cover excess nested elements, excess string initializers, invalid dimensions
and oversized objects. All 503 regression checks pass; kernel compilation remains
36,009 words. These additions are host compiler features: they do not expand the
resident cc/go parser yet. Full C remains an active, incomplete objective.

## Array designated initializers

The host compiler now parses constant array designators, including chained
multidimensional selections. Initialization resumes after the selected subobject;
later entries replace earlier values, omitted elements stay zero and the highest
initialized element determines an omitted outer bound. Recursive initializer
parsing builds a storage-indexed representation before emitting native code.
Pointer designations preserve static relocations and local runtime expressions.

Native tests cover forward/backward selection, continuation, duplicate selection,
partial row replacement, strings, inferred arrays and local/global pointer arrays.
Rejection tests cover negative/out-of-range indices, excess designator depth,
nonconstant indices, scalar designators and missing equals. The 543 regression
checks pass and the kernel rebuild remains 36,009 words. This is still a host
compiler feature; resident parsing, struct member designators and the rest of the
full-C roadmap remain incomplete. Initializer nesting has a 64-level resource
limit, and objects retain their 4096-word storage limit.

## Recursive object and abstract declarators

Object/parameter declarators now collect derived operators outward from the name
and apply them in reverse order to the base type. Parenthesized declarations
therefore distinguish arrays of pointers from pointers to arrays, including
multidimensional rows and arrays of row pointers. Grouped ordinary names and
unnamed row-pointer parameters work through the same parser. Abstract type names
in casts and sizeof also use this model. Sizeof array type names now emits the
full word count instead of assuming every type occupies one word.

Pointers to incomplete arrays are representable without giving the incomplete
type a fictitious size. Sizeof that pointee and arithmetic requiring its size
reject. Arrays of void, incomplete inner arrays, array casts and conflicting
row-pointer declarations reject. Native tests exercise row-pointer globals,
parameters, local traversal, nested pointers, casts and array/pointer sizeof.
All 583 regression checks pass; the kernel rebuild stays at 36,009 words.

The parser limits grouping to 32 levels, derived operators to 64 and dimensions
to 16. Function declarator suffixes still use the earlier direct-function parser;
function pointers and functions returning array pointers remain to implement.
Qualifier semantics, compatible incomplete-type merging, the resident compiler
architecture, remaining C types and the standard library are also incomplete.
