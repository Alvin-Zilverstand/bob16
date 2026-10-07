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
  classes, lexical scopes/shadowing, prototypes, complete function-pointer support
  and variadics. Typed function pointers and indirect calls now work in the host
  compiler; return types and variadic function-pointer cases remain open.
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

## Shift assignments and integer operand constraints

The host lexer recognizes <<= and >>= before their two-character prefixes.
They parse as right-associative assignments and use the existing native shift
helpers, retaining single evaluation of the destination address. Tests cover
indexed destinations with side effects, chained assignment, expression precedence,
zero shifts and signed right shifts. Integer-only binary operators and unary
plus/minus/complement reject pointer operands, including unevaluated sizeof
expressions. This does not yet implement the full arithmetic conversion model.

All 615 regression checks pass. These changes are in the host compiler; the
resident compiler's corresponding operators and broader C implementation remain
unfinished.

## Composite function declaration types

Function declaration checks now recursively form composite pointer/array types.
An unspecified array bound can match a known bound through additional pointer
levels, retaining the known bound. Conflicting known bounds, element types and
pointer depths still reject. Native tests cover forward declarations with
incomplete row types followed by complete definitions; rejection tests also
cover an incomplete declaration followed by two conflicting completions.
All 631 regression checks pass and the kernel rebuild remains 36,009 words.
Qualifier and old-style promotion compatibility, object redeclarations and the
remaining function declarator forms still require implementation.

## Compound assignment and unevaluated constraints

Compound assignments share an operator mapping and operand checks across native
emission and sizeof type inference. Pointer compound assignments allow only
integer addition/subtraction offsets with complete pointees; pointer subtraction
from another pointer remains a binary expression yielding an integer, not an
assignable pointer offset. Integer compound assignments reject pointer operands.
Assignment and increment checks inside sizeof require a modifiable scalar lvalue,
rejecting arrays, void objects and literal targets without evaluating operands.

Native tests verify valid row-pointer offsets and sizeof suppression of indexed
and increment side effects. Rejection tests cover pointer differences in compound
assignments, pointer shifts, array/literal modification and void pointer offsets.
All 671 checks pass; kernel compilation stays at 36,009 words. Const semantics,
simple-assignment conversions and the remaining full expression/type checker
still need implementation, along with the resident compiler expansion.

## File-scope object declarations and tentative definitions

Global objects are registered by name and compatible declarations form a composite
type. Repeated tentative definitions share one allocation. One initialized
definition may replace the tentative zero initialization; a second initializer
rejects. File-scope extern declarations, including function declarations, parse
without creating storage for declaration-only objects. Referenced undefined
objects fail relocation resolution, while unused extern declarations are accepted.
Incomplete array bounds can be completed by another declaration, and a remaining
tentative array definition reserves one element at translation-unit completion.

Native tests combine extern/tentative/initialized scalars, pointer relocations,
completed multidimensional arrays and an uncompleted tentative array. Rejections
cover conflicting object types/bounds, duplicate initialized definitions,
unresolved references and sizeof an uncompleted extern array. All 695 regression
checks pass; the kernel rebuild remains 36,009 words.

This does not implement block-scope extern/static storage, internal linkage,
separate compilation or declaration-point-sensitive global visibility. Global
resolution still uses the complete source unit. Those semantics, the resident
compiler expansion and the remaining full-C roadmap stay unfinished.

## Block-scope extern objects

Block extern object declarations register compatible external types without
allocating local frame slots. Native emission activates a scoped alias to the
global storage; nested aliases can shadow outer automatic locals and repeated
compatible extern declarations in the same scope share the alias. Extern-only
names are withheld from the ordinary file-scope lookup, preventing them from
escaping their declaring block or appearing in another function without a
declaration. Referenced missing definitions still reject at relocation time.

Native tests verify global writes through a nested alias, preservation of an
outer local, completed array types and repeated extern declarations. Rejection
tests cover unresolved objects, conflicting local/types, block initializers and
names escaping their block/function. All 727 checks pass and the kernel rebuild
remains 36,009 words. Block function declarations, static storage, full linkage
rules and separate compilation remain unfinished; these additions remain in
the host compiler rather than the resident compiler.

## Function-local static storage

Block static objects receive unique private data symbols and scoped aliases,
avoiding stack allocation and per-call initialization. Their native data is
initialized once at image load, with zero initialization for omitted values.
Static initializer references resolve against the declaring scope and retain
private symbols in pointer relocations; references to automatic storage reject.
Objects with equal names in different blocks/functions remain independent.

Native tests exercise persistent counters, pointer access to another local static,
arrays, strings and independent same-name objects. Rejections cover automatic
values/addresses, runtime calls, duplicate declarations, escaped names and
uninitialized unsized arrays. All 755 regression checks pass; the kernel rebuild
remains 36,009 words. File-scope internal linkage, additional constant expression
forms and the resident compiler expansion remain incomplete.

## File-scope static declarations and linkage checks

File-scope objects/functions now parse static storage declarations and retain
linkage metadata. Repeated internal object declarations share one allocation;
subsequent extern declarations inherit existing linkage. Objects cannot switch
between external and internal linkage, and a static function declaration cannot
follow an external declaration. A function definition without a storage class
can follow its earlier static prototype. Incomplete internal tentative arrays
reject instead of silently receiving the external tentative-array completion.

Native tests combine internal objects, arrays, relocated pointers, static
function prototypes and later extern declarations. Negative tests exercise both
object linkage conflict directions, conflicting function declarations and
incomplete internal arrays. All 783 checks pass; the kernel rebuild remains
36,009 words. This remains a single-unit compiler: separate compilation, linker
exports and cross-unit isolation still require implementation, as does the
resident compiler expansion.

## Sizeof in constant initialization

The constant evaluator now handles sizeof expressions using their inferred type.
Literal/type-based sizeof can be used in bounds and case labels, and global
initializers can use sizeof registered global objects. Local static initializer
sizeof operands are folded while their lexical bindings are active, allowing
sizes of automatic arrays without treating them as runtime values or addresses.
Unevaluated side effects remain suppressed.

Native tests cover string/type bounds, global scalar/list initialization, local
static sizes of automatic objects and a sizeof case label. All 787 regression
checks pass and the kernel rebuild remains 36,009 words. Bounds and case labels
are still parsed before runtime symbols are registered, so sizeof a named object
in those contexts requires the remaining declaration-point symbol/type work.
The full-C goal and resident compiler expansion remain incomplete.

## Parser type bindings for constant sizeof

The parser retains object type bindings at declaration points, updates inferred
array types after initialization and restores bindings at block/for/function
boundaries. Parameter arrays expose their adjusted pointer type. Constant sizeof
in bounds, designators and case labels can therefore query declared object types
without depending on the later code-generation symbol table. Compatible repeated
file declarations retain a completed array bound in these parser bindings.

Native tests verify named global/local/parameter sizes, nested shadowing and
restoration, loop scope and named sizeof case labels. Negative tests reject
forward, escaped block/loop and undeclared self references in bounds. All 807
checks pass and the kernel rebuild remains 36,009 words. Parser type bindings
are limited to 4096 active declarations. This improves constant-expression
contexts; declaration-point visibility in ordinary generated expressions,
typedefs and the remaining full-C/resident work still require implementation.

## Scoped typedef names

Parser bindings distinguish objects, functions and typedef names in the ordinary
identifier namespace. Typedef declarations reuse the recursive declarator parser
for the currently supported scalar, pointer and array types, with comma-separated
aliases and identical same-scope redeclarations. Aliases work in declarations,
parameters, casts, sizeof and other aliases. A sole unnamed void alias parameter
specifies no arguments. Scope tracking restores outer aliases after a nested block
or loop and lets inner objects hide outer typedef names.

Native tests combine scalar/pointer/row/incomplete-character-array aliases,
adjusted array parameters, casts, nested alias/object shadowing, loop declarations
and void aliases. Rejections cover incompatible aliases, object/function namespace
collisions, parameter collisions, escaped aliases and void objects. All 843 checks
pass; the kernel rebuild remains 36,009 words. Function-type aliases, qualifiers,
remaining C types and resident compiler support remain incomplete.

## Enum types and scoped enumerators

The host parser supports tagged/anonymous enum definitions, explicit integer
constant values and automatic increments. Enumerators share the ordinary
identifier namespace and become constant AST values at use sites. A separate
scoped tag table permits nested tag shadowing without conflating tag and object
names. Enum descriptors retain distinct identities and use the target's signed
16-bit int representation; declaration composites permit compatibility with that
chosen integer type while rejecting different enum identities.

Native tests cover negative/automatic values, expressions referring to earlier
enumerators, typedef enum aliases, global objects, function parameters, array
bounds, case labels and nested block/loop shadowing. Rejections cover duplicate
tags/constants, namespace collisions, automatic signed overflow, escaped bindings,
constant assignment and conflicting enum parameter types. All 887 regression
checks pass; the kernel rebuild remains 36,009 words. Tags must already be defined
when referenced; incomplete enum extensions and unsigned/wider representations
remain unsupported. Resident enum parsing is still unfinished.

## Boolean type and conversions

The host type parser supports _Bool and aliases of it as a distinct one-word
primitive. Native conversion to 0/1 occurs at explicit casts, scalar/array
initialization, assignment, function arguments/returns and increment/decrement.
Static data initialization and constant casts use the equivalent host folding.
Function parameter emission applies the definition's boolean parameter type,
and declaration compatibility distinguishes bool from int.

Native tests cover nonzero/zero global/local/static initialization, nested arrays,
integer/pointer casts, arguments, return values, compound assignments and prefix/
postfix modification. Rejections cover incompatible boolean object/parameter
redeclarations. All 899 regression checks pass; the kernel rebuild remains
36,009 words. The standard stdbool header, complete arithmetic conversion rules,
other numeric types and resident boolean support still need implementation.

## Unsigned int arithmetic foundation

The host compiler supports explicit unsigned/unsigned int declarations and U/u
literal suffixes as a distinct 16-bit type. Expression inference propagates the
unsigned arithmetic result for current word-width int operands; comparisons,
division/remainder and right shifts select unsigned native operations. Constant
evaluation applies the corresponding unsigned behavior. Existing wraparound
addition/subtraction/multiplication use the same word instructions.

tools/unsigned.c provides native unsigned comparisons, fixed-16-step division,
remainder and logical right shift. It is parsed only for a source unit using
explicit unsigned types/literals, preserving the existing kernel layout.
Native tests cover high-bit ordering, mixed signed operands, division/remainder
across the signed boundary, logical shifts, compound shifts and wraparound.
Conflicting parameter/object types and pointer division reject. All 915 checks
pass and the kernel rebuild remains 36,009 words.

This is not the complete integer conversion model. Unsuffixed literal typing
retains prior behavior, other unsigned/wider integer types and all specifier
orderings are unfinished, and runtime shift/divide exceptional cases retain
the existing helper policy. Resident unsigned parsing/code generation remains
unfinished. Full C stays an active objective.

Host declaration specifiers: signed and signed int now use the existing signed-int type, and int signed / int unsigned are accepted in either order. This applies to globals, locals, typedefs, parameters, return types, casts and sizeof type names. Duplicate int and repeated/conflicting sign specifiers fail compilation. Wider integer types, signed/unsigned char, qualifier enforcement and the resident compiler remain separate work.

Host literal audit: all simple C escape sequences are now recognized, including \a, \f, \v and \?. Character and string escapes accept byte values 0..255. Hexadecimal accumulation checks the range before multiplication, preventing signed host overflow; out-of-range octal/hexadecimal escapes fail. A trailing backslash is rejected before advancing past the source terminator. This byte-literal extension does not provide wide literals, Unicode conversion or the remaining integer-width/type-selection work.

Host input audit: binary zero bytes in preprocessed source are rejected instead of silently truncating the translation unit. Input seeks, reads, stream error and close are checked. Raw newline characters inside ordinary literals and malformed integer token tails now produce lexer errors. This does not expand target integer widths or floating-point support.

Host relational pointer comparisons now use unsigned target address order, including across 0x8000, while signed integer comparisons retain signed order. Compatible pointer types use composite type checks, including pointers to arrays with known/incomplete bounds. Pointer/integer, incompatible pointee and void-pointer relational comparisons are rejected in evaluated expressions and sizeof operands. Full qualifier and aggregate compatibility remain unfinished.

Host output audit: assembly, binary and map output streams now check write/close failures, and missing map output is a compilation failure. Derived map extensions apply only to the filename, preserving dotted parent directories. Input/output and map path collisions are rejected before writing; Windows checks normalize absolute paths and case. Hard-link/symlink aliases and atomic publication of all three files remain unresolved, and an I/O failure after publication starts can still leave partial outputs.

Host void-expression constraints: arithmetic/comparison and logical operands, unary value operators, assignment sources, casts to value types and control-flow conditions now reject void results. Conditional expressions require both branches to be void when either is void. Equivalent checks apply inside sizeof without evaluating its operand. Discarded void calls, casts to void, comma expressions and two-void-branch conditionals remain valid. Other conversion, qualifier and aggregate constraints remain unfinished.

Host return/argument constraints now reject return expressions in void functions, bare return statements in value-returning functions, void results returned as values, and void arguments to ordinary or trap-backed functions. Full assignment-compatible conversion checking remains incomplete.

Host switch constraints: controlling expressions and case-label expressions must have an integer type. Pointer/array/void conditions and pointer/void labels are rejected. Unsigned integer, enum and _Bool switches remain supported. Complete integer-constant-expression rules and the remaining type conversion constraints still need review.

Host const syntax: const can precede or follow scalar, typedef and enum specifiers, appear between signed/unsigned/int specifiers, and repeat at base or pointer levels. Direct const scalar objects, const array elements, scalar parameters, scalar globals, and const pointer objects (including pointer typedef aliases) reject assignment and increment. Address-of preserves const qualification; writes through const-qualified pointers reject, adding const in pointer assignments is allowed, and dropping it is rejected through nested pointer levels and conditional expressions. `bob_puts` accepts a const string. Function redeclarations now require matching pointed-to qualifiers while ignoring top-level parameter qualifiers. Some nested qualified-declarator forms and full C qualifier compatibility remain unfinished.

Host pointer subtraction now checks composite type compatibility, including the target enum's compatible signed-int type and corresponding array rows. Both pointed-to types must have complete storage sizes. Different enum tags, different known row extents and incomplete element types remain rejected.

Host conditional pointer types now combine compatible pointee types, preserve known array bounds, select void pointer for object/void pointer combinations, and permit arithmetic integer constant zero as a null branch. Incompatible pointer branches, nonzero integers and nonconstant integer branches are rejected, including inside sizeof. Null-constant recognition still requires review for all permitted unevaluated-expression cases; qualifier merging remains incomplete.

Native bob16 multiplication now uses a sixteen-step bit-mask algorithm instead of operand-sized repeated addition. It computes the same low 16-bit product for signed and unsigned operands, including INT_MIN, without a sign-normalization branch. The host compiler arithmetic library and rebuilt kernel use it. The resident compiler also receives this improvement through its cc_multiply wrapper, which calls the shared runtime helper. Kernel size is now 36,005 words.

Resident numeric lexer now treats leading-zero integer spellings as octal, accepts octal word values through 0177777, and rejects digits outside the base or values exceeding one word. Decimal range rules remain unchanged. Error handling sets EOF centrally, and the general compile-error message is shorter. Kernel size is 36,092 words, leaving four words below the current kernel/program boundary; increasing usable kernel capacity remains necessary for further resident features.

Host code generation now omits zero-argument call cleanup and zero-sized function-frame allocation. Numeric constants normalize to their target 16-bit bit pattern before choosing short literal instructions, so values such as 0xffff can use the short -1 encoding. The rebuilt kernel is 35,909 words, saving 183 words and leaving 187 words below the current boundary. This creates some immediate room but does not replace the required memory-layout/bob32 work.

Resident literal support now includes all simple C escapes, adding \r, \b, \a, \f, \v and \? to the existing newline/tab/quote/backslash forms. Numeric escapes and embedded string zero bytes remain unfinished. Host code generation collapses !!value to one boolean comparison while evaluating the operand once. Kernel size is 36,096 words: exactly the current reserved region, with no remaining growth space.

Host frame generation now omits frame-pointer save/setup/restore for functions with no parameters and no automatic local storage. Return addresses remain saved, including recursive calls. Functions needing parameter/local addressing retain the existing frame layout. Kernel size is now 36,054 words, leaving 42 words below the boundary.

Resident lexer now rejects raw LF/CR inside character and string literals while retaining escaped controls. Host code generation folds unary minus, complement and logical negation directly on numeric literals into target-word constants; runtime operands retain normal evaluation. Kernel size is 36,041 words, leaving 55 words of current-region capacity.

Resident primary expressions now concatenate adjacent string literal tokens, including intervening comments/newlines and empty pieces. Each piece retains the lexer token limit; combined data goes through the existing bounded code emitter and receives one trailing zero. Numeric escapes and embedded zero bytes remain incomplete. Kernel size is 36,071 words, leaving 25 words below the current boundary.

Resident code emission now stops after the first compiler error instead of continuing writes to its bounded scratch buffer. The existing 511-word capacity guard remains. Kernel size is 36,080 words, leaving 16 words below the current boundary.

Native signed division now bypasses repeated subtraction when the normalized divisor is -1, covering original divisors 1 and -1. Sign detection uses the sign of the XOR of operands. Other divisors retain the existing subtraction algorithm. Host code generation also folds numeric-literal addition/subtraction/multiplication with the existing target-word constant evaluator. Kernel size is 36,093 words, leaving three words.

Host constant evaluation now validates cast, unary, binary and conditional expression types before folding or selecting short-circuit branches. This prevents invalid void operand combinations from being accepted only in static initializers. The broader integer-constant-expression grammar still needs review.

Host pointer equality now requires compatible pointer types, permits object/void pointer combinations, and requires an integer operand to be a recognized null constant. The checks apply in evaluated expressions and sizeof. Qualifier-aware pointer compatibility and complete null-constant grammar remain unfinished.

Host simple assignment expressions now check pointer compatibility and null integer constants, rejecting implicit pointer-to-integer assignments except _Bool conversion. Equivalent validation applies inside sizeof. Declaration initializers and parameter/return conversion compatibility still require integration; qualifiers remain incomplete. Kernel source explicitly casts file_content's int-word storage to char source text, preserving existing machine output.

Host return expressions now receive supported assignment-compatible conversion checks: pointer types must be compatible or object/void combinations, integer-to-pointer returns require a recognized null constant, and pointer-to-integer returns require an explicit cast except _Bool. Existing boolean return normalization remains. Argument/initializer compatibility, qualifications and aggregate conversions remain incomplete.

Ordinary host-compiled function calls now enforce assignment-compatible argument conversions: compatible object pointers, object/void pointer conversions, integer constant zero and pointer-to-_Bool are accepted; incompatible pointers and implicit pointer/integer conversions are rejected. Trap intrinsic conversions and declaration initializer constraints still need further work. Kernel word-copy/fill and file-write APIs accept generic data pointers; text uses explicit casts where file buffers are word pointers. The regenerated 36,093-word kernel image is unchanged.

Host scalar declaration initializers now enforce assignment-compatible conversions for automatic variables, global/static objects and individual array elements. Invalid nonzero integer-to-pointer, incompatible pointer, implicit pointer-to-integer and void-value initializers are rejected. Valid object/void pointer conversions, integer constant zero and pointer-to-_Bool conversions remain supported. Trap intrinsic conversion rules, qualifiers and other C constraints remain incomplete. Regenerating the kernel produces the identical binary.

Emitted trap-backed calls now validate argument conversions against the intrinsic ABI: bob_puts/bob_gets require character pointers, bob_snapshot requires an integer descriptor pointer, and capacities, operations, characters and entry addresses require integer arguments. Object/void pointer conversions and integer null constants follow the same rules as ordinary calls. bob_call also rejects implicit pointer entry arguments. Unevaluated call validation and intrinsic declaration consistency still require review. The regenerated kernel binary is unchanged.

Calls examined during expression type analysis now validate parameter counts and assignment-compatible argument types, including inside sizeof. Declaration selection prefers a function definition or a parameter-bearing prototype so an earlier empty parameter list cannot mask known parameters. A sizeof runtime fixture verifies arguments with increments remain unevaluated. Intrinsic declaration consistency, bob_address special cases and complete source-order declaration semantics remain open. The regenerated kernel binary is unchanged.

Host subscript type analysis now rejects void-valued index operands in either order and requires a complete element type for the pointer operand. This also applies to unevaluated address-of/subscript expressions inside sizeof. Reversed indexing and unsigned indexes into multidimensional arrays remain supported. Kernel regeneration produces the identical 36,093-word image.

Nested sizeof expressions now recursively validate their operands while remaining unevaluated. Address-of type checks reject numeric constants, arithmetic results, casts and increment results even inside sizeof. Address generation now supports pointers to complete string-literal arrays (for example char (*p)[5] = &"bob!"). Function pointers, full qualifier rules and several other lvalue constraints remain incomplete.

Static address relocation now supports addresses of whole string-literal arrays, including global and block-static pointer-to-array initializers and scaled constant offsets. Existing string-element address initializers continue to work. This closes the storage-duration gap left by automatic address generation support; other static initializer forms remain incomplete.

Static pointer initializer relocation now handles conditional expressions with constant conditions, including nested selections of object or string addresses. Full conditional type analysis runs before selecting a branch, preserving rejection of incompatible pointer types. Nonconstant conditions remain invalid for static initialization. Null-selected branches use the existing constant initializer path.

Constant folding of numeric guest pointer arithmetic now scales offsets by complete element size, matching runtime arithmetic for pointer-to-array values. Pointer-plus-integer, integer-plus-pointer, pointer-minus-integer and pointer difference use the same word-based guest layout. Numeric address casts and static pointer differences are implementation extensions; this is not a claim of full standard constant-expression support.

Unary code generation now performs expression type validation before emitting instructions, including optimized numeric unary operations. This closes a path that allowed increment/decrement of void lvalues to emit loads/stores even though the type-analysis path rejected them. Array increment remains invalid. Existing unary runtime tests continue to cover valid integer, pointer and Boolean operations.

Static Boolean initializers now convert resolvable object/string addresses to one, including explicit _Bool casts, arrays and block-static objects. Numeric null pointers still convert to zero through constant evaluation. This implements the existing scalar-to-Boolean conversion consistently across automatic and static storage; full standard constant-expression and qualifier constraints remain incomplete.

Relocation emission now validates both code and relocation capacities before writing either array. Literal-address and static-pointer emission share this checked path. Previously the relocation record was inserted before emit checked code capacity; now rejected oversized programs cannot grow relocation state past the emission boundary.

Host literal lexing now grows checked heap storage as needed rather than rejecting strings above 8,190 characters. Character literal temporary storage is freed; string tokens retain their allocated bytes and explicit length, including embedded NULs. Overall source, object and emitted-code limits remain in force. This host change does not enlarge the resident compiler's token or file limits.

Adjacent host string literals now concatenate with one sized allocation and one copy of each piece. The previous repeated-prefix allocations consumed quadratic time and retained quadratic storage for long runs of adjacent literals. Explicit byte lengths still preserve embedded NULs. Length is bounded by the existing source/token limits before parsing.

Array bounds, array designator indexes and explicit enum values now require an integer expression type before constant evaluation. Pointer- and void-valued expressions are rejected in these contexts. Existing integer constant folding and enum/sizeof behavior remain supported; full standard integer-constant-expression grammar restrictions are still incomplete.

Jump bookkeeping now routes all host call and branch jump records through a checked helper before appending to the fixed jump table. This makes the 65,536-entry jump limit explicit and keeps long-control-flow failure paths deterministic. Token and code limits normally trigger first, but the table itself is now guarded.

Generated host labels now stop at an explicit 65,535-label serial limit, preventing signed counter overflow on unusually large generated control flow. The earlier emitted-code cap normally rejects first; this guard keeps the label namespace safe independently.

Switch parsing now rejects case/default labels outside any switch before code generation and bounds nested switch parsing to 64 levels. This closes a validation gap where generation might fail later or skip malformed code. The host OS/compiler suite passes 1,540 checks, including file-scope and function-scope cases; the 256-labels-per-switch and 64-level boundaries remain explicit.

Compile-time integer contexts now share one syntax check: array bounds, enum values, array designators and switch labels reject calls, assignments and increments, while integer operators, casts, conditional expressions and unevaluated sizeof expressions remain accepted. The host OS/compiler suite passes 1,556 checks, and the kernel remains 36,093 words.

Integer constant expression validation now also checks operand types through nested casts and sizeof. Void and pointer expressions cannot be hidden inside an integer cast, unary/binary operation or conditional; regression cases cover sizeof of a void-valued cast in array and enum contexts. All 1,564 OS/compiler checks pass, with the kernel image unchanged.

Explicit enum values now reject unsigned results outside the documented signed 16-bit int range instead of silently wrapping; signed boundary values remain valid. The OS/compiler suite passes 1,568 checks, with the generated kernel unchanged.

Host expression parsing now caps recursive nesting at 256 levels, preventing deeply nested parentheses, unary operators or right-associative expressions from exhausting the host compiler stack. A generated 400-level array-bound fixture checks the diagnostic. All 1,573 OS/compiler checks pass; the 36,093-word kernel image hash is unchanged.

B32 resident expression support now includes the right-associative conditional operator ?:. Code generation branches around the unselected expression, and nesting shares the existing 32-frame parser guard. Runtime coverage checks nested associativity and confirms skipped side effects. Kept B32-only because the bob16 kernel image remains at its 36,093-word size limit. The full suite passes: CPU 179, OS/compiler 1,745, native console 28.

B32 resident control flow now supports do/while loops. Continue chains patch to the trailing condition, while break chains patch after the loop; regression covers first-iteration behavior, continue, break and a zero condition. The bob16 compiler remains unchanged due to the kernel image limit. Full suite: CPU 179, OS/compiler 1,745, console 28.

B32 resident C now parses right-associative simple/compound assignment expressions, preserving the assigned value in the surrounding expression. Added <<= and >>= tokenization and code generation. A B32 shell regression checks chained assignment, assignment under comparison, and both compound shifts; it asserts all nine successful shell programs to avoid false positives from skipped/overlong input. Bob16 image remains 36,093 words. Full suite: CPU 189, OS/compiler 1,746, console 28.

B32 resident C now supports multiple integer functions with stack-based parameters and locals, forward references, nested calls and recursion, including calls used as expression statements. A no-argument output helper, a two-argument helper and recursive factorial exercise the calling convention from the shell. Forward-call patch chains now store relative offsets until the callee definition is known; applying the program base twice previously made the call jump into unmapped memory. The end-to-end workflow checks all nine successful B32 shell programs. Current suite: CPU 189, OS/compiler 1,746, native console 28.

B32 local declarations now follow lexical block scopes, including legal shadowing and for-initializer scope; stack slots remain unique even after a name leaves scope. Regression coverage also found that the expanded B32 kernel overlaps the old `0x9000` legacy-app base. Kind-1 applications now execute at `0xB000..0xBFFF` in the bob32 OS while bob16 remains at `0x9000`; B32 snapshot descriptors may reside up to that boundary. Removing the unnecessary post-run kernel restore preserves the RAM file table. The end-to-end tests cover recursive resident functions, block shadowing, a legacy `LEA/PUTS/HALT` binary, file listing after execution, and snapshot save/restore. Current CPU suite: 189; OS/compiler: 1,746; console: 28.

The resident B32 compiler now rejects a 33rd cumulative local stack slot across lexical scopes. Its LDR/STR frame displacements are signed six-bit fields; silently masking larger negative offsets could bind locals to the wrong frame word. A generated 33-block regression confirms the source is rejected cleanly. The rebuilt B32 kernel is 43,113 words; OS/compiler suite: 1,749 checks passed.

Resident bob32 C now supports fixed-size one-dimensional local `int` arrays within the 32-slot frame budget. Array declarations reserve contiguous slots and zero them; `array[index]` emits a frame-relative load, while indexed assignment statements preserve the computed address across right-hand-side calls. A shell program fills and sums four elements in loops and prints `bob!`. Array initialization, parameter arrays, multidimensional arrays, and bob16 resident array support remain open. The regenerated B32 kernel is 44,250 words, ending 70 words below the then-current `0xB000` legacy-app boundary; OS/compiler suite: 1,752 checks passed.

To preserve space for further C support, the B32 legacy kind-1 region now starts at `0xB800` and reserves 2,048 words through `0xBFFF`; the largest RAM-file app is 511 words. The emulator, kernel loader, snapshot descriptor boundary, CPU regression and user-facing maps were updated together. The current 44,250-word kernel ends at `0xAFBA`, 2,118 words below the new window. CPU boundary fixtures verify writes at `0xB800` and rejection below it. Full suites: CPU 191, OS/compiler 1,752, console 28.

Supervised-store review found a range hole: native apps faulted in 0x100000..0x10FFFF but could write again at 0x110000 and above. Store validation now uses the documented half-open application range and denies all other addresses. CPU regressions cover the last valid word, both sides of the boundary, arbitrary high sparse memory and UINT32_MAX. Full suite: CPU 185, OS/compiler 1,746, console 28.

B32 external application loading now reserves every missing sparse page before installing pages or copying image words. Sparse-page allocation failure returns the loader error without exiting the emulator or modifying the target region. A BOB_TESTING-only allocator fault regression verifies the failure path. The full suite passes: CPU 186, OS/compiler 1,746, console 28.

Guest trap-6 audit found that a nested run attempt returned the intended -1 but then reset the outer B32 application's mode flag, causing later valid stores to fault. Nested calls now return before touching that flag. A CPU regression verifies the app continues, writes within high memory and exits. The full suite passes: CPU 187, OS/compiler 1,746, console 28.

Resident bob32 array statements now support indexed postfix increment/decrement and all compound assignments, including shifts implemented through helper calls. The regression fills an array, applies `++`, `+=` and `<<=`, then checks the summed result; indexed addresses remain protected across RHS evaluation and helper calls. The 44,833-word kernel ends at `0xB221`, leaving 1,503 words before the `0xB800` app boundary. Full suites: CPU 191, OS/compiler 1,752, console 28.

Resident bob32 fixed-size local arrays now accept scalar brace initializers with an optional trailing comma. The compiler retains zero-filled omitted elements and rejects excess elements; an end-to-end program verifies initialized values and zero padding before performing array updates, and a negative program verifies excess-element rejection. B32 kernel: 45,070 words, ending at `0xB30E` with 1,266 words before the `0xB800` legacy window. OS/compiler suite: 1,753 checks passed.

Resident bob32 array prefix/postfix increment and decrement now compose with expressions: postfix returns the old element after storing the updated value, and prefix returns the updated value. A separate end-to-end app checks both semantics while staying within the 511-word file cap. The kernel is 45,299 words, ending at `0xB3F3` with 1,037 words before the legacy window. OS/compiler suite: 1,754 checks passed.

Resident bob32 local declarations now accept comma-separated scalar and fixed-array declarators with per-declarator initializers. This includes declarations that initialize a scalar from an array postfix expression. The updated 45,331-word kernel ends at `0xB413`, leaving 1,005 words before the legacy window. OS/compiler suite: 1,754 checks passed.

Resident bob32 now parses integer function prototypes with optional parameter names. Prototypes set/check call arity, may precede a later definition, and may remain unused without definitions; unresolved calls and conflicting signatures fail. Runtime coverage checks a prototype-declared forward function call, unused unnamed-parameter declaration and conflicting prototypes. The 45,486-word kernel ends at `0xB4AE`, with 850 words before the `0xB800` legacy window. OS/compiler suite: 1,758 checks passed.

Resident bob32 prototypes now distinguish `int f()` from `int f(void)`. An empty parameter list keeps unspecified status and can be refined by a typed declaration or definition; calls made through it must match the eventual definition's arity to preserve the current stack ABI. End-to-end coverage verifies the valid and mismatched cases. The regenerated B32 kernel is 48,185 words and ends at `0xBF38`, leaving 199 words before the compatibility-app region. CPU 194, OS/compiler 1,764, and console 28 checks pass.

## Recent bob32 and header additions

The bob32 resident compiler now recognizes `print`, `println`, `print_dec` and `print_hex` as one-argument kernel built-ins without source prototypes. A regression runs `write bobs.c int main(void){println("Bob!");return 0;}` followed by `go bobs.c` and verifies the exact `Bob!` output and exit 0. The kernel grew into the old bob32 compatibility-app range, so that range now begins at `0xC200`; the emulator checks its entry and write bounds, and memory-map documentation reflects the relocation. Current B32 image: 48,645 words, ending at `0xC164`.

Added the standard `<stdbool.h>` header for the host-compiled C workflow, mapping `bool`, `true` and `false` to the existing `_Bool` implementation. B16 and B32 runtime fixtures verify true/false conversion and print `bob!`. Static analysis with GCC `-fanalyzer` reported no diagnostics for the emulator, host compiler or test harness. Full suite at that point: CPU 194, OS/compiler 1,769, console 28.

Added `<stddef.h>` with target-width `size_t` and `ptrdiff_t` plus `NULL`. Corrected `sizeof` expressions and type-name forms to have unsigned `size_t` type, so arithmetic such as `sizeof(int)-2` follows unsigned conversion rules. B16 and B32 runtime tests check array sizes, pointer differences, null pointers and unsigned wraparound. Full suite: CPU 194, OS/compiler 1,773, console 28.

The host compiler now parses typed function-pointer declarators, including nested pointer parameters, and type-checks their assignments and calls. Function designators and `&function` relocate to code labels; local/global pointer objects and indirect calls use the B16/B32 call ABI. Calls through an empty `()` signature use unspecified argument checking, while calls through prototypes enforce arity and compatible parameter types. Regression apps cover global initialization, local assignment/copy, direct and parenthesized indirect calls, nested function-pointer parameters, and the unspecified form on both targets; negative cases cover signature, arity, object/function pointer mismatches, and invalid object/function pointer casts. Function-pointer return types remain unsupported. Full suite at that stage: CPU 194, OS/compiler 1,793, console 28.

Resident bob32 syntax diagnostics now include the current token text with the existing character offset and line/column. The source regression checks token context. The packed native application workflow and current verification totals are recorded in `tests/AUDIT.md` and `BOB32_MIGRATION.md`.

The bob32 resident compiler now emits into the native app staging region and saves generated programs against the remaining 4,096-word packed filesystem capacity; bob16 keeps its 511-word output limit. A short resident C source generates a 1,289-word app, prints `bob!`, survives a B32S snapshot and restore, then runs again from guest storage. Full suites: CPU 203, OS/compiler 1,822, console 28.

The bob32 resident compiler now accepts one-dimensional array parameters, adjusting them to pointer-valued stack arguments. It derives addresses for local array arguments, loads and indexes parameter pointers in the callee, and preserves mutation visibility through the caller's array. Prototype/definition signatures retain array-versus-scalar parameter shape, and calls reject known mismatches. Tests cover `sum(values,length)`, indexed iteration, mutation, a nested forwarding call, repeated helper calls, conflicting/multidimensional declarations, and existing local-array behavior. Full suites: CPU 203, OS/compiler 1,825, console 28.

Started the app-side standard library with `kernel/bob_string.h`: basic string operations, whitespace detection, overflow-checked signed decimal/hex parsing, bounded integer formatting and quote-aware in-place tokenization. Runtime tests exercise edge cases in bob32, and a compiled app using the header is imported, launched, snapshotted, restored and run again from the bob32 filesystem. Full suites: CPU 203, OS/compiler 1,840, console 28.
