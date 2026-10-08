#include "../bob64/types.h"
#include "../bob64/memory.h"
#include "../bob64/paging.h"
#include "../bob64/cpu.h"
#include "../bob64/descriptors.h"
#include "../bob64/interrupts.h"
#include "../bob64/bootstrap.h"
#include "../bob64/console.h"
#include "../bob64/heap.h"
#include "../bob64/keyboard.h"
#include "../bob64/mouse.h"
#include "../bob64/window.h"
#include "../bob64/window_server.h"
#include "../bob64/editor.h"
#include "../bob64/shell.h"
#include "../bob64/exec.h"
#include "../bob64/process.h"
#include "../bob64/syscall.h"
#include "../bob64/app.h"
#include "../bob64/libc.h"
#include "../bob64/compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

typedef struct {
    _Alignas(16) u8 Bytes[4*4096];
    usize PagesUsed;
} HEAP_TEST_ARENA;

static int heap_test_grow(void *context,void **region,usize *region_size);

#define CHECK(expression,message) do { if(!(expression)){fprintf(stderr,"bob64 check failed: %s\n",message);return 1;} } while(0)

typedef struct {
    u64 Base;
    usize Used;
    usize Capacity;
    u64 Entries[128][512];
} TEST_PAGE_TABLES;

static TEST_PAGE_TABLES test_clone_tables={0x100000000ULL,0,128,{{0}}};
static u32 read32(const u8 *bytes);
static u64 read64(const u8 *bytes);
static int contains_bytes(const u8 *bytes,usize length,const char *needle,
                          usize needle_length);
static u8 resident_compiler_image[BOB64_COMPILER_IMAGE_LIMIT];
static usize resident_compiler_image_length;

static int execute_compiled_main(const BOB64_EXEC_IMAGE *image,s64 *result) {
    typedef s64 (BOB64_MS_ABI *GENERATED_MAIN)(void);
    void *memory;
    if(!image||!result||image->CodeSize<64||image->FileSize>image->MemorySize)return -1;
#ifdef _WIN32
    memory=VirtualAlloc(0,(SIZE_T)image->MemorySize,MEM_RESERVE|MEM_COMMIT,
                        PAGE_EXECUTE_READWRITE);
    if(!memory)return -1;
#else
    memory=mmap(0,(usize)image->MemorySize,PROT_READ|PROT_WRITE|PROT_EXEC,
                MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(memory==MAP_FAILED)return -1;
#endif
    memcpy(memory,image->Image,(usize)image->FileSize);
    GENERATED_MAIN main_function=(GENERATED_MAIN)((u8 *)memory+32);
    *result=main_function();
#ifdef _WIN32
    VirtualFree(memory,0,MEM_RELEASE);
#else
    munmap(memory,(usize)image->MemorySize);
#endif
    return 0;
}

static int libc_tests(void) {
    char source[]="bob64!",copy[16],overlap[12]="0123456789";
    static const char expected[]="0101234567";
    u8 fill[4];
    CHECK(bob64_memcpy(copy,source,sizeof(source))==copy&&
          bob64_memcmp(copy,source,sizeof(source))==0,
          "copy and compare native application memory");
    CHECK(bob64_memmove(overlap+2,overlap,8)==overlap+2&&
          bob64_memcmp(overlap,expected,10)==0,
          "move overlapping ranges in either address direction");
    CHECK(bob64_memset(fill,0xa5,sizeof(fill))==fill&&fill[0]==0xa5&&
          fill[3]==0xa5&&bob64_memcmp(fill,fill,sizeof(fill))==0,
          "fill memory with an 8-bit value");
    CHECK(bob64_strlen(source)==6&&bob64_strlen("")==0&&
          bob64_strcmp("bob64!","bob64!")==0&&bob64_strcmp("a","b")<0&&
          bob64_strcmp("b","a")>0,
          "measure and compare NUL-terminated strings");
    return 0;
}

static int compiler_tests(void) {
    static const char source[]=
        "int main(void) { bob64_app_write(\"bob!\", 4); return 0; }";
    static const char invalid[]="int main(void) { return value; }";
    usize length=0,error_offset=0;
    BOB64_EXEC_IMAGE image;
    CHECK(!bob64_compile_c(source,sizeof(source)-1,resident_compiler_image,
                           sizeof(resident_compiler_image),&length,
                           &error_offset)&&length==sizeof(resident_compiler_image),
          "compile the resident C smoke program into a B64E image");
    resident_compiler_image_length=length;
    CHECK(!bob64_exec_parse(resident_compiler_image,length,&image)&&image.EntryOffset==0&&
          image.CodeSize==BOB64_PAGE_SIZE&&image.FileSize==2*BOB64_PAGE_SIZE&&
          image.MemorySize==2*BOB64_PAGE_SIZE,
          "validate resident compiler output with the shared B64E loader parser");
    CHECK(image.Image[0]==0x48&&image.Image[1]==0x83&&image.Image[2]==0xec&&
          image.Image[3]==0x28&&image.Image[4]==0xe8&&
          image.Image[43]==0x48&&image.Image[44]==0x8d&&
          image.Image[50]==0xba&&image.Image[51]==4&&image.Image[55]==0xb8&&
          image.Image[60]==0xcd&&image.Image[61]==0x80&&
          !memcmp(image.Image+BOB64_PAGE_SIZE,"bob!",4),
          "emit Microsoft x64 entry code and the bob! console syscall");
    CHECK(read32(image.Image+5)==23&&
          (s32)read32(image.Image+46)==(s32)(BOB64_PAGE_SIZE-50)&&
          image.Image[62]==0x48&&image.Image[63]==0xb8&&
          read64(image.Image+64)==0&&image.Image[79]==0xc3,
          "resolve the x64 entry call and RIP-relative string address");
    CHECK(bob64_compile_c(invalid,sizeof(invalid)-1,resident_compiler_image,
                          sizeof(resident_compiler_image),&length,
                          &error_offset)==-2&&error_offset>0,
          "reject unsupported C syntax with a source offset");
    CHECK(bob64_compile_c(source,sizeof(source)-1,resident_compiler_image,
                          sizeof(resident_compiler_image)-1,&length,
                          &error_offset)==-1,
          "reject a B64E output buffer that is too small");
    return 0;
}

static int compiler_arithmetic_tests(void) {
    static const char source[]=
        "long long main(void) { return (0x100000001ULL + 2) * 3; }";
    static const char bad_suffix[]="long long main(void) { return 1LUL; }";
    static const char signed_source[]="int main(void) { return -1; }";
    u8 file[BOB64_COMPILER_IMAGE_LIMIT];usize length,error_offset;
    BOB64_EXEC_IMAGE image;
    CHECK(!bob64_compile_c(source,sizeof(source)-1,file,sizeof(file),&length,
                           &error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile a 64-bit arithmetic expression as a native app");
    CHECK(image.Image[43]==0x48&&image.Image[44]==0xb8&&
          read64(image.Image+45)==0x100000001ULL&&image.Image[53]==0x50&&
          image.Image[64]==0x59&&image.Image[65]==0x48&&
          image.Image[66]==0x01&&image.Image[67]==0xc8&&image.Image[68]==0x50&&
          image.Image[79]==0x59&&image.Image[80]==0x48&&
          image.Image[81]==0x0f&&image.Image[82]==0xaf&&
          image.Image[83]==0xc1&&image.Image[88]==0xc3,
          "emit 64-bit immediates, stack temporaries, precedence, and multiply");
    s64 result=0;
    CHECK(!execute_compiled_main(&image,&result)&&result==0x300000009LL,
          "execute generated 64-bit arithmetic with the Microsoft x64 stack ABI");
    CHECK(bob64_compile_c(bad_suffix,sizeof(bad_suffix)-1,file,sizeof(file),&length,
                          &error_offset)==-2,
          "reject malformed C integer suffixes");
    CHECK(!bob64_compile_c(signed_source,sizeof(signed_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==-1,
          "sign-extend a 32-bit int return through the 64-bit app ABI");
    return 0;
}

static int compiler_local_tests(void) {
    static const char source[]=
        "long long main(void) { long long value = 0x100000001ULL; "
        "int delta = 2; value = value + delta; return value * 3; }";
    u8 file[BOB64_COMPILER_IMAGE_LIMIT];usize length,error_offset;
    BOB64_EXEC_IMAGE image;
    CHECK(!bob64_compile_c(source,sizeof(source)-1,file,sizeof(file),&length,
                           &error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile typed local variables and assignments into B64E");
    CHECK(contains_bytes(image.Image,(usize)image.CodeSize,"\x48\x89\x45\xf8",4)&&
          contains_bytes(image.Image,(usize)image.CodeSize,"\x89\x45\xf0",3)&&
          contains_bytes(image.Image,(usize)image.CodeSize,"\x48\x8b\x45\xf8",4)&&
          contains_bytes(image.Image,(usize)image.CodeSize,"\x48\x63\x45\xf0",4),
          "use 64-bit stack slots and sign-extend 32-bit local loads");
    s64 result=0;
    CHECK(!execute_compiled_main(&image,&result)&&result==0x300000009LL,
          "execute generated local loads/stores and the 64-bit function frame");
    static const char string_pointer_source[]=
        "int main(void) { char *text = \"bob!\"; return length(text); } "
        "int length(char *text) { int n = 0; "
        "while (text[n] != '\\0') n = n + 1; return n; }";
    CHECK(!bob64_compile_c(string_pointer_source,sizeof(string_pointer_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile string literals, char pointers, and string-taking helpers");
    CHECK(!execute_compiled_main(&image,&result)&&result==4,
          "execute char-pointer indexing and NUL-terminated string traversal");
    static const char char_array_source[]=
        "int main(void) { char text[8] = \"bob!\"; text[1] = 'O'; "
        "return length(text) + text[1]; } "
        "int length(char text[]) { int n = 0; "
        "while (text[n] != '\\0') n = n + 1; return n; }";
    CHECK(!bob64_compile_c(char_array_source,sizeof(char_array_source)-1,file,
                           sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile initialized local char arrays and array parameters");
    CHECK(!execute_compiled_main(&image,&result)&&result==4+'O',
          "execute byte-array initialization, mutation, decay, and traversal");
    static const char char_brace_array_source[]=
        "int main(void) { char text[4] = {'b','o','b','!'}; return text[3]; }";
    CHECK(!bob64_compile_c(char_brace_array_source,
                           sizeof(char_brace_array_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result=='!',
          "compile and execute brace-initialized char arrays");
    static const char exact_char_array_source[]=
        "int main(void) { char text[3] = \"bob\"; return text[2]; }";
    CHECK(!bob64_compile_c(exact_char_array_source,
                           sizeof(exact_char_array_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result=='b',
          "allow a character array sized exactly to its string initializer");
    static const char char_array_zero_fill_source[]=
        "int main(void) { char text[4] = {'b'}; return text[1]; }";
    CHECK(!bob64_compile_c(char_array_zero_fill_source,
                           sizeof(char_array_zero_fill_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==0,
          "zero-fill omitted elements in a char array initializer");
    static const char oversized_char_string_source[]=
        "int main(void) { char text[3] = \"long\"; return text[0]; }";
    CHECK(bob64_compile_c(oversized_char_string_source,
                          sizeof(oversized_char_string_source)-1,file,sizeof(file),
                          &length,&error_offset)==-2,
          "reject a string initializer that exceeds its char array");
    static const char global_storage_source[]=
        "static int counter = 40; char text[5] = \"bob!\"; "
        "int values[3] = {1, 2}; "
        "int main(void) { counter = counter + 2; text[1] = 'O'; "
        "values[2] = counter; return letter(text) + values[2]; } "
        "int letter(char *value) { return value[1]; }";
    CHECK(!bob64_compile_c(global_storage_source,sizeof(global_storage_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile initialized global scalars and mutable arrays across functions");
    CHECK(!execute_compiled_main(&image,&result)&&result=='O'+42,
          "execute RIP-relative global loads/stores and global array access");
    static const char wide_global_source[]=
        "long long wide = 0x100000001ULL; int zero_value; "
        "long long main(void) { return wide + zero_value + 1; }";
    CHECK(!bob64_compile_c(wide_global_source,sizeof(wide_global_source)-1,file,
                           sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==0x100000002LL,
          "preserve 64-bit initialized globals and zero-fill uninitialized globals");
    static const char global_pointer_source[]=
        "char text[5] = \"bob!\"; int numbers[2] = {40, 2}; "
        "char *active_text = text; int *values = numbers; char *label = \"gui\"; "
        "int main(void) { active_text[1] = 'O'; return read_text() + values[0] + values[1] + label[0]; } "
        "int read_text(void) { return active_text[1]; }";
    CHECK(!bob64_compile_c(global_pointer_source,sizeof(global_pointer_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result=='O'+42+'g',
          "initialize global pointers to arrays and strings without fixed-address relocations");
    static const char struct_member_source[]=
        "struct Record { int value; char mark; long long count; char *text; }; "
        "struct Record shared; "
        "int first(char *text) { return text[0]; } "
        "int main(void) { struct Record local; local.value=40; local.mark='!'; "
        "local.count=2; shared.value=local.value+local.count; "
        "shared.mark=local.mark; shared.text=\"bob\"; "
        "return shared.value+shared.mark+first(shared.text); }";
    CHECK(!bob64_compile_c(struct_member_source,sizeof(struct_member_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile named struct layouts and local/global scalar and pointer members");
    CHECK(!execute_compiled_main(&image,&result)&&result==40+2+'!'+ 'b',
          "execute aligned struct field loads and stores in local and global objects");
    static const char struct_pointer_source[]=
        "struct Node { int value; }; struct Node shared; "
        "struct Node *shared_pointer = &shared; "
        "int main(void) { struct Node local; struct Node *pointer=&local; "
        "pointer->value=40; shared_pointer->value=2; "
        "return shared_pointer->value+pointer->value; }";
    CHECK(!bob64_compile_c(struct_pointer_source,sizeof(struct_pointer_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile struct pointers and arrow-member expressions");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "execute global and local struct pointers across object layouts");
    static const char struct_pointer_function_source[]=
        "struct Node { int value; }; "
        "int read_value(struct Node *node) { return node->value; } "
        "int main(void) { return 42; }";
    CHECK(!bob64_compile_c(struct_pointer_function_source,
                           sizeof(struct_pointer_function_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==42,
          "compile struct pointer parameters and their member access in helper functions");
    static const char struct_pointer_call_source[]=
        "struct Node { int value; }; "
        "int read_value(struct Node *node) { return node->value; } "
        "int main(void) { struct Node local; struct Node *pointer=&local; "
        "pointer->value=42; return read_value(pointer); }";
    int struct_pointer_call_result=bob64_compile_c(struct_pointer_call_source,
        sizeof(struct_pointer_call_source)-1,file,sizeof(file),&length,&error_offset);
    if(struct_pointer_call_result) {
        usize begin=error_offset>24?error_offset-24:0;
        usize end=error_offset+24<sizeof(struct_pointer_call_source)-1?
                  error_offset+24:sizeof(struct_pointer_call_source)-1;
        fprintf(stderr,"struct pointer call compile=%d offset=%zu context=%.*s\n",
                struct_pointer_call_result,error_offset,(int)(end-begin),
                struct_pointer_call_source+begin);
    }
    CHECK(!struct_pointer_call_result&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==42,
          "call a struct-pointer helper using the x64 argument ABI");
    static const char struct_pointer_stack_call_source[]=
        "struct Node { int value; }; "
        "int read_value(int a, int b, int c, int d, struct Node *node) { "
        "return node->value+a+b+c+d; } "
        "int main(void) { struct Node local; struct Node *pointer=&local; "
        "pointer->value=32; return read_value(1, 2, 3, 4, pointer); }";
    CHECK(!bob64_compile_c(struct_pointer_stack_call_source,
                           sizeof(struct_pointer_stack_call_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile a struct pointer in the fifth Microsoft x64 argument slot");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "preserve struct pointers passed on the x64 argument stack");
    static const char char_pointer_arithmetic_source[]=
        "int main(void) { char *text = \"bob!\"; return *(text + 1) == 'o'; }";
    CHECK(!bob64_compile_c(char_pointer_arithmetic_source,
                           sizeof(char_pointer_arithmetic_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==1,
          "execute byte-scaled char-pointer arithmetic, dereference, and character literals");
    static const char char_pointer_store_source[]=
        "int main(void) { char *text = \"bob!\"; text[1] = 97; return text[1]; }";
    CHECK(!bob64_compile_c(char_pointer_store_source,
                           sizeof(char_pointer_store_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result=='a',
          "execute byte stores through a char pointer");
    static const char signed_char_source[]=
        "int main(void) { char value = 255; return value; }";
    CHECK(!bob64_compile_c(signed_char_source,sizeof(signed_char_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==-1,
          "narrow and sign-extend a signed 8-bit char local");
    static const char char_argument_source[]=
        "int main(void) { return value('A'); } int value(char input) { return input; }";
    CHECK(!bob64_compile_c(char_argument_source,sizeof(char_argument_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result=='A',
          "pass and sign-extend a char argument through the x64 ABI");
    static const char array_source[]=
        "int main(void) { int values[3]; int *pointer = values; "
        "pointer[0] = 40; pointer[1] = 2; return pointer[0] + pointer[1]; }";
    CHECK(!bob64_compile_c(array_source,sizeof(array_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile a local int array through a 64-bit pointer");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "execute pointer indexing, scaled address arithmetic, and array loads/stores");
    static const char pointer_arithmetic_source[]=
        "int main(void) { int values[3]; int *pointer = values; "
        "pointer = 1 + pointer; *pointer = 40; pointer = pointer - 1; "
        "pointer[0] = 2; return values[1] + values[0]; }";
    CHECK(!bob64_compile_c(pointer_arithmetic_source,
                           sizeof(pointer_arithmetic_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile scaled pointer arithmetic in both addition operand orders");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "execute pointer arithmetic with sizeof(int) scaling");
    static const char pointer_difference_source[]=
        "long long main(void) { int values[3]; int *pointer = values; "
        "pointer = pointer + 2; return pointer - values; }";
    CHECK(!bob64_compile_c(pointer_difference_source,
                           sizeof(pointer_difference_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile int-pointer difference as an element count");
    CHECK(!execute_compiled_main(&image,&result)&&result==2,
          "execute pointer difference in int elements");
    static const char array_initializer_source[]=
        "int main(void) { int seed = 40; int values[4] = {seed, 2,}; "
        "return values[0] + values[1] + values[2] + values[3]; }";
    CHECK(!bob64_compile_c(array_initializer_source,
                           sizeof(array_initializer_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile local array brace initialization with a trailing comma");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "execute local array initialization and zero-fill omitted elements");
    static const char excess_array_initializer[]=
        "int main(void) { int values[1] = {1, 2}; return values[0]; }";
    CHECK(bob64_compile_c(excess_array_initializer,
                         sizeof(excess_array_initializer)-1,file,sizeof(file),
                         &length,&error_offset),
          "reject an array initializer with excess elements");
    static const char array_parameter_source[]=
        "int main(void) { int values[3] = {40, 2}; return sum(values, 2); } "
        "int sum(int values[], int count) { "
        "return values[0] + values[1] + count; }";
    CHECK(!bob64_compile_c(array_parameter_source,sizeof(array_parameter_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile array-parameter functions and a forward call from main");
    CHECK(!execute_compiled_main(&image,&result)&&result==44,
          "execute an array parameter with the Microsoft x64 integer-argument ABI");
    static const char nested_call_source[]=
        "int double_value(int value) { return value * 2; } "
        "int main(void) { return add(double_value(21), 1); } "
        "int add(int left, int right) { return left + right; }";
    CHECK(!bob64_compile_c(nested_call_source,sizeof(nested_call_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile nested calls and multiple integer arguments");
    CHECK(!execute_compiled_main(&image,&result)&&result==43,
          "execute nested calls with stack alignment and forward-call fixups");
    static const char wide_return_source[]=
        "long long wide_value(void); "
        "long long main(void) { return wide_value(); } "
        "long long wide_value(void) { return 0x100000001ULL; }";
    CHECK(!bob64_compile_c(wide_return_source,sizeof(wide_return_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile long-long helper definitions before main");
    CHECK(!execute_compiled_main(&image,&result)&&result==0x100000001LL,
          "preserve a 64-bit helper return through the C call ABI");
    static const char long_type_source[]=
        "long shared=40; long long wide=0x100000001ULL; "
        "long read_shared(void); long long main(void) { long local=read_shared(); "
        "return local+shared+wide; } "
        "long read_shared(void) { return shared; }";
    CHECK(!bob64_compile_c(long_type_source,sizeof(long_type_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile 64-bit long globals, locals, prototypes, and returns");
    CHECK(!execute_compiled_main(&image,&result)&&result==0x100000051LL,
          "execute LLP64 long and long-long values at their declared widths");
    static const char short_global_source[]=
        "short shared=64302; int main(void) { return shared; }";
    CHECK(!bob64_compile_c(short_global_source,sizeof(short_global_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==-1234,
          "load and sign-extend a 16-bit global");
    static const char short_local_source[]=
        "int main(void) { short value=64302; return value; }";
    CHECK(!bob64_compile_c(short_local_source,sizeof(short_local_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==-1234,
          "store and sign-extend a 16-bit local");
    static const char short_call_source[]=
        "int main(void) { return echo(64302); } "
        "short echo(short value) { return value; }";
    CHECK(!bob64_compile_c(short_call_source,sizeof(short_call_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile short helper call");
    CHECK(!execute_compiled_main(&image,&result)&&result==-1234,
          "pass and return signed short values through a helper call");
    static const char short_stack_call_source[]=
        "int main(void) { return fifth(1,2,3,4,64302); } "
        "short fifth(int a,int b,int c,int d,short value) { return value; }";
    CHECK(!bob64_compile_c(short_stack_call_source,sizeof(short_stack_call_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==-1234,
          "pass and return a signed short in the fifth ABI slot");
    static const char short_type_source[]=
        "struct ShortRecord { char tag; short value; int tail; }; "
        "short shared=64302; short echo(short value) { return value; } "
        "short fifth(int a,int b,int c,int d,short value) { return value; } "
        "int main(void) { short local=echo(shared); struct ShortRecord record; "
        "record.value=fifth(1,2,3,4,local); record.tail=128; "
        "if(local == -1234 && record.value == -1234 && record.tail == 128) "
        "return 42; return 0; }";
    CHECK(!bob64_compile_c(short_type_source,sizeof(short_type_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile signed 16-bit globals, locals, fields, and ABI parameters");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "sign-extend short loads and preserve short values across calls");
    static const char short_array_pointer_source[]=
        "short shared[2]={65535,2}; short *shared_pointer=shared; "
        "int sum(short values[]) { return values[0]+values[1]+values[2]; } "
        "int main(void) { short values[4]={65535,2}; short *pointer=values; "
        "*pointer=40; values[2]=shared_pointer[1]; "
        "return sum(pointer)+*(shared_pointer+1); }";
    CHECK(!bob64_compile_c(short_array_pointer_source,
                           sizeof(short_array_pointer_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile short arrays, pointers, initialization, and array parameters");
    CHECK(!execute_compiled_main(&image,&result)&&result==46,
          "index, dereference, and scale pointer arithmetic by two bytes");
    static const char pointer_sized_integer_source[]=
        "uintptr_t high=0x100000001ULL; "
        "usize echo_unsigned(usize value) { return value; } "
        "isize echo_signed(isize value) { return value; } "
        "int main(void) { usize size=echo_unsigned(high); "
        "intptr_t distance=echo_signed(-42); "
        "if(size > 0xffffffffULL && distance == -42) return 42; return 0; }";
    CHECK(!bob64_compile_c(pointer_sized_integer_source,
                           sizeof(pointer_sized_integer_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile uintptr_t/usize and intptr_t/isize function types");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "preserve unsigned pointer-sized values above four GiB and signed isize arguments");
    static const char prototype_arguments_source[]=
        "int combine(int, int); "
        "int main(void) { return combine(20, 22); } "
        "int combine(int left, int right) { return left + right; }";
    CHECK(!bob64_compile_c(prototype_arguments_source,
                           sizeof(prototype_arguments_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile a forward declaration with unnamed parameters");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "resolve a prototype to its later definition");
    static const char conflicting_prototype_source[]=
        "int combine(int); int main(void) { return combine(42); } "
        "int combine(int left, int right) { return left + right; }";
    CHECK(bob64_compile_c(conflicting_prototype_source,
                         sizeof(conflicting_prototype_source)-1,file,sizeof(file),
                         &length,&error_offset),
          "reject a function definition that conflicts with its prototype");
    static const char missing_function_definition_source[]=
        "int absent(int); int main(void) { return absent(42); }";
    CHECK(bob64_compile_c(missing_function_definition_source,
                         sizeof(missing_function_definition_source)-1,
                         file,sizeof(file),&length,&error_offset),
          "reject a call whose prototype has no definition");
    static const char four_argument_source[]=
        "int main(void) { return sum4(1, 2, 3, 4); } "
        "int sum4(int first, int second, int third, int fourth) { "
        "return first + second + third + fourth; }";
    CHECK(!bob64_compile_c(four_argument_source,sizeof(four_argument_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile a four-argument call using all Microsoft x64 integer registers");
    CHECK(!execute_compiled_main(&image,&result)&&result==10,
          "execute arguments in RCX, RDX, R8, and R9");
    static const char eight_argument_source[]=
        "int main(void) { return sum8(1, 2, 3, 4, 5, 6, 7, 8); } "
        "int sum8(int a, int b, int c, int d, int e, int f, int g, int h) { "
        "return a + b + c + d + e + f + g + h; }";
    CHECK(!bob64_compile_c(eight_argument_source,sizeof(eight_argument_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile stack-passed fifth through eighth arguments");
    CHECK(!execute_compiled_main(&image,&result)&&result==36,
          "execute eight arguments across registers and the Microsoft x64 stack area");
    static const char sixteen_argument_source[]=
        "int main(void) { return sum16(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16); } "
        "int sum16(int a,int b,int c,int d,int e,int f,int g,int h,"
        "int i,int j,int k,int l,int m,int n,int o,int p) { "
        "return a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p; }";
    CHECK(!bob64_compile_c(sixteen_argument_source,sizeof(sixteen_argument_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile calls and definitions with sixteen ABI parameters");
    CHECK(!execute_compiled_main(&image,&result)&&result==136,
          "execute sixteen arguments including the widest stack displacement");
    static const char lexical_scope_source[]=
        "int main(void) { int value=40; { int value=2; value=value+1; } "
        "{ int temporary=3; } { int temporary=4; } return value+2; }";
    CHECK(!bob64_compile_c(lexical_scope_source,sizeof(lexical_scope_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile shadowing locals and reused names in sibling C blocks");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "restore the outer local binding after leaving a nested scope");
    static const char for_scope_source[]=
        "int main(void) { int total=0; for(int index=0;index<3;index=index+1) "
        "{ int item=index+1; total=total+item; } return total; }";
    CHECK(!bob64_compile_c(for_scope_source,sizeof(for_scope_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==6,
          "scope a for-loop variable and its compound body");
    static const char duplicate_scope_local_source[]=
        "int main(void) { { int value=1; int value=2; } return 0; }";
    CHECK(bob64_compile_c(duplicate_scope_local_source,
                         sizeof(duplicate_scope_local_source)-1,file,sizeof(file),
                         &length,&error_offset),
          "reject duplicate declarations in the same lexical scope");
    static const char for_scope_escape_source[]=
        "int main(void) { for(int index=0;index<1;index=index+1) { } "
        "return index; }";
    CHECK(bob64_compile_c(for_scope_escape_source,
                          sizeof(for_scope_escape_source)-1,file,sizeof(file),
                          &length,&error_offset),
          "reject access to a for-loop variable after its scope");
    static const char stack_pointer_argument_source[]=
        "int main(void) { int value = 42; return read_pointer(1, 2, 3, 4, &value); } "
        "int read_pointer(int a, int b, int c, int d, int *pointer) { "
        "return *pointer; }";
    CHECK(!bob64_compile_c(stack_pointer_argument_source,
                           sizeof(stack_pointer_argument_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile a pointer passed in the fifth argument slot");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "preserve a 64-bit pointer through stack argument passing");
    static const char bad_call_arity[]=
        "int main(void) { return add(1); } "
        "int add(int left, int right) { return left + right; }";
    CHECK(bob64_compile_c(bad_call_arity,sizeof(bad_call_arity)-1,file,sizeof(file),
                          &length,&error_offset),
          "reject calls with the wrong number of arguments");
    static const char bad_pointer_argument[]=
        "int main(void) { int value = 4; return read_value(value); } "
        "int read_value(int *pointer) { return *pointer; }";
    CHECK(bob64_compile_c(bad_pointer_argument,sizeof(bad_pointer_argument)-1,
                          file,sizeof(file),&length,&error_offset),
          "reject an integer argument where an int pointer is required");
    static const char control_flow_source[]=
        "int main(void) { int count = 4; int total = 0; "
        "while (count > 0) { if (count != 2) total = total + count; "
        "else total = total + 2; count = count - 1; } return total; }";
    CHECK(!bob64_compile_c(control_flow_source,sizeof(control_flow_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile nested if/else and while control flow");
    CHECK(!execute_compiled_main(&image,&result)&&result==10,
          "execute backward and forward branches through a counted loop");
    static const char for_loop_source[]=
        "int main(void) { int total = 0; "
        "for (int index = 0; index < 5; index = index + 1) "
        "total = total + index; return total; }";
    CHECK(!bob64_compile_c(for_loop_source,sizeof(for_loop_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile a C-style for loop with a declaration, condition, and update");
    CHECK(!execute_compiled_main(&image,&result)&&result==10,
          "execute for-loop condition, body, update, and back edges");
    static const char loop_control_source[]=
        "int main(void) { int total = 0; "
        "for (int index = 0; index < 6; index = index + 1) { "
        "if (index == 2) continue; if (index == 5) break; "
        "total = total + index; } return total; }";
    CHECK(!bob64_compile_c(loop_control_source,sizeof(loop_control_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile break and continue in a for loop");
    CHECK(!execute_compiled_main(&image,&result)&&result==8,
          "continue runs the for update and break exits the nearest loop");
    static const char nested_loop_control_source[]=
        "int main(void) { int outer = 0; int inner = 0; int total = 0; "
        "while (outer < 3) { outer = outer + 1; inner = 0; "
        "while (inner < 3) { inner = inner + 1; "
        "if (inner == 2) continue; if (outer == 2) break; "
        "total = total + 1; } if (outer == 3) break; } return total; }";
    CHECK(!bob64_compile_c(nested_loop_control_source,
                           sizeof(nested_loop_control_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile nested loops with loop-local break and continue targets");
    CHECK(!execute_compiled_main(&image,&result),
          "execute nested loop control flow");
    CHECK(result==4,"route break and continue to the nearest nested loop");
    static const char early_return_source[]=
        "int main(void) { return absolute(-42); } "
        "int absolute(int value) { if (value < 0) return -value; return value; }";
    CHECK(!bob64_compile_c(early_return_source,sizeof(early_return_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile an early return inside a conditional helper");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "execute an early conditional return and fallthrough return");
    static const char invalid_break_source[]=
        "int main(void) { break; return 0; }";
    CHECK(bob64_compile_c(invalid_break_source,sizeof(invalid_break_source)-1,
                          file,sizeof(file),&length,&error_offset),
          "reject break outside a loop");
    static const char invalid_continue_source[]=
        "int main(void) { continue; return 0; }";
    CHECK(bob64_compile_c(invalid_continue_source,sizeof(invalid_continue_source)-1,
                          file,sizeof(file),&length,&error_offset),
          "reject continue outside a loop");
    static const char comparison_source[]=
        "int main(void) { int value = -1; int result = 0; "
        "if (value < 0) { if (value <= -1) { if (value >= -1) { "
        "if (value == -1) result = 42; } } } return result; }";
    CHECK(!bob64_compile_c(comparison_source,sizeof(comparison_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile signed relational and equality operators");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "execute signed comparisons for if/else conditions");
    static const char logical_source[]=
        "int main(void) { int value = 1; int result = 0; "
        "if (0 && change(&value)) result = 9; "
        "if (1 || change(&value)) result = value + 41; "
        "if (!result) result = 0; return result; } "
        "int change(int *pointer) { *pointer = 40; return 1; }";
    CHECK(!bob64_compile_c(logical_source,sizeof(logical_source)-1,file,sizeof(file),
                           &length,&error_offset)&&!bob64_exec_parse(file,length,&image),
          "compile unary not and short-circuit logical expressions");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "short-circuit logical operators skip pointer-mutating function calls");
    static const char invalid_pointer_arithmetic[]=
        "int main(void) { int values[2]; int *pointer = values; "
        "return pointer * 2; }";
    CHECK(bob64_compile_c(invalid_pointer_arithmetic,
                         sizeof(invalid_pointer_arithmetic)-1,file,sizeof(file),
                         &length,&error_offset),
          "reject multiplication of a pointer by an integer");
    static const char invalid_pointer_assignment[]=
        "int main(void) { int *pointer = 1; return 0; }";
    CHECK(bob64_compile_c(invalid_pointer_assignment,
                         sizeof(invalid_pointer_assignment)-1,file,sizeof(file),
                         &length,&error_offset),
          "reject assigning an integer expression to an int pointer");
    static const char pointer_source[]=
        "long long main(void) { int value = 42; int *pointer = &value; "
        "*pointer = *pointer + 1; return pointer; }";
    CHECK(!bob64_compile_c(pointer_source,sizeof(pointer_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile address-of, int-pointer dereference, and a pointer return");
    CHECK(!execute_compiled_main(&image,&result)&&result>0xffffffffLL,
          "preserve a generated stack pointer value above the 4 GiB boundary");
    static const char usize_pointer_source[]=
        "usize update(usize *pointer); "
        "int main(void) { usize value = 0x100000001ULL; "
        "usize *pointer = &value; update(pointer); "
        "return pointer[0] - 0x100000003ULL; } "
        "usize update(usize *pointer) { pointer[0] = pointer[0] + 2; "
        "return *pointer; }";
    CHECK(!bob64_compile_c(usize_pointer_source,sizeof(usize_pointer_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile usize pointers through calls, dereference, and indexing");
    CHECK(!execute_compiled_main(&image,&result)&&result==0,
          "preserve pointer-sized unsigned values above 4 GiB through memory");
    static const char usize_array_source[]=
        "usize sum(usize values[], usize length) { usize total = 0; "
        "usize index = 0; while (index < length) { total = total + values[index]; "
        "index = index + 1; } return total; } "
        "long long main(void) { usize values[3] = {0x100000000ULL, 2}; "
        "values[1] = values[1] + 3; "
        "return sum(values, 2) - 0x100000005ULL; }";
    CHECK(!bob64_compile_c(usize_array_source,sizeof(usize_array_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile local usize arrays, indexing, initialization, and array parameters");
    CHECK(!execute_compiled_main(&image,&result)&&result==0,
          "sum initialized 64-bit array elements above 4 GiB through a helper");
    static const char isize_array_source[]=
        "long long main(void) { isize values[3] = {-0x100000000LL, 2}; "
        "values[1] = values[0] + 3; "
        "return values[1] + 0xfffffffeLL; }";
    CHECK(!bob64_compile_c(isize_array_source,sizeof(isize_array_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile signed wide arrays and indexed assignment");
    CHECK(!execute_compiled_main(&image,&result)&&result==1,
          "preserve signed 64-bit values in wide array elements");
    static const char global_usize_array_source[]=
        "usize values[2] = {0x100000000ULL, 1}; usize *pointer = values; "
        "long long main(void) { return pointer[1] - pointer[0] + 0xffffffffLL; }";
    CHECK(!bob64_compile_c(global_usize_array_source,
                           sizeof(global_usize_array_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile global usize arrays and a decayed global pointer");
    CHECK(!execute_compiled_main(&image,&result)&&result==0,
          "load global wide arrays through a 64-bit pointer");
    static const char isize_pointer_return_source[]=
        "isize *step(isize *pointer) { return pointer + 1; } "
        "long long main(void) { isize first = 0; isize second = 0; "
        "return step(&second) - &second; }";
    CHECK(!bob64_compile_c(isize_pointer_return_source,
                           sizeof(isize_pointer_return_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile isize pointer returns, scaled arithmetic, and subtraction");
    CHECK(!execute_compiled_main(&image,&result)&&result==1,
          "scale isize pointer arithmetic by the eight-byte pointee size");
    static const char isize_pointer_source[]=
        "struct Box { isize *value; }; "
        "long long advance(isize *pointer) { pointer[0] = pointer[0] + 1; "
        "return *pointer; } "
        "long long main(void) { isize value = -0x100000001LL; "
        "struct Box box; box.value = &value; "
        "return advance(box.value) + 0x100000000LL; }";
    CHECK(!bob64_compile_c(isize_pointer_source,sizeof(isize_pointer_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile isize pointers in parameters and struct fields");
    CHECK(!execute_compiled_main(&image,&result)&&result==0,
          "preserve signed 64-bit pointees through indexed and direct access");
    static const char mismatched_wide_pointer_source[]=
        "usize read_value(usize *pointer) { return *pointer; } "
        "long long main(void) { isize value = 1; "
        "return read_value(&value); }";
    CHECK(bob64_compile_c(mismatched_wide_pointer_source,
                         sizeof(mismatched_wide_pointer_source)-1,file,sizeof(file),
                         &length,&error_offset),
          "reject passing a signed wide pointer to an unsigned pointer parameter");
    static const char global_usize_pointer_source[]=
        "usize shared = 0x100000002ULL; usize *cursor = &shared; "
        "long long main(void) { return *cursor - 0x100000002ULL; }";
    CHECK(!bob64_compile_c(global_usize_pointer_source,
                           sizeof(global_usize_pointer_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image)&&
          !execute_compiled_main(&image,&result)&&result==0,
          "initialize a global usize pointer to a global scalar");
    static const char pointer_return_source[]=
        "int *identity(int *pointer); "
        "int main(void) { int value = 42; int *same = identity(&value); "
        "return *same; } "
        "int *identity(int *pointer) { return pointer; }";
    CHECK(!bob64_compile_c(pointer_return_source,sizeof(pointer_return_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile a typed pointer return through a prototype and call");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "preserve pointer identity through a generated function return");
    static const char string_return_source[]=
        "char *message(void) { return \"Bob!\"; } "
        "int main(void) { char *text = message(); "
        "return text[0] + text[1] + text[2] + text[3]; }";
    CHECK(!bob64_compile_c(string_return_source,sizeof(string_return_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile a returned string pointer and index its data");
    CHECK(!execute_compiled_main(&image,&result)&&result=='B'+'o'+'b'+'!',
          "retain a string literal address through a pointer return");
    static const char struct_pointer_return_source[]=
        "struct Item { int value; }; "
        "struct Item *identity(struct Item *item); "
        "int main(void) { struct Item value; value.value = 42; "
        "struct Item *same = identity(&value); return same->value; } "
        "struct Item *identity(struct Item *item) { return item; }";
    CHECK(!bob64_compile_c(struct_pointer_return_source,
                           sizeof(struct_pointer_return_source)-1,file,sizeof(file),
                           &length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile a declared-struct pointer return through a prototype");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "preserve a struct pointer through a generated function return");
    static const char incompatible_pointer_return_source[]=
        "int *wrong(void) { return \"Bob!\"; } "
        "int main(void) { return 0; }";
    CHECK(bob64_compile_c(incompatible_pointer_return_source,
                         sizeof(incompatible_pointer_return_source)-1,
                         file,sizeof(file),&length,&error_offset),
          "reject a pointer return with an incompatible pointee type");
    static const char char_return_source[]=
        "char negative_byte(void); "
        "int main(void) { return negative_byte(); } "
        "char negative_byte(void) { return 255; }";
    CHECK(!bob64_compile_c(char_return_source,sizeof(char_return_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile a scalar char return through a prototype");
    CHECK(!execute_compiled_main(&image,&result)&&result==-1,
          "sign-extend a returned high-bit char through the x64 ABI");
    static const char void_return_source[]=
        "void store(int *pointer); "
        "void add_two(int *pointer) { *pointer = *pointer + 2; } "
        "int main(void) { int value = 0; store(&value); "
        "add_two(&value); return value; } "
        "void store(int *pointer) { *pointer = 40; return; }";
    CHECK(!bob64_compile_c(void_return_source,sizeof(void_return_source)-1,
                           file,sizeof(file),&length,&error_offset)&&
          !bob64_exec_parse(file,length,&image),
          "compile void calls with explicit and implicit function returns");
    CHECK(!execute_compiled_main(&image,&result)&&result==42,
          "execute void helper calls and restore the caller stack");
    static const char invalid_void_value_source[]=
        "void action(void) { return; } "
        "int main(void) { return action(); }";
    CHECK(bob64_compile_c(invalid_void_value_source,
                         sizeof(invalid_void_value_source)-1,file,sizeof(file),
                         &length,&error_offset),
          "reject using a void function call as a value");
    static const char invalid_nonvoid_empty_return_source[]=
        "int value(void) { return; } int main(void) { return 0; }";
    CHECK(bob64_compile_c(invalid_nonvoid_empty_return_source,
                         sizeof(invalid_nonvoid_empty_return_source)-1,file,sizeof(file),
                         &length,&error_offset),
          "reject an empty return in a non-void function");
    static const char invalid_void_value_return_source[]=
        "void action(void) { return 42; } int main(void) { return 0; }";
    CHECK(bob64_compile_c(invalid_void_value_return_source,
                         sizeof(invalid_void_value_return_source)-1,file,sizeof(file),
                         &length,&error_offset),
          "reject a value return in a void function");
    return 0;
}

static u64 test_table_allocate(void *context) {
    TEST_PAGE_TABLES *tables=(TEST_PAGE_TABLES *)context;
    if(tables->Used==tables->Capacity)return 0;
    return tables->Base+(tables->Used++)*BOB64_PAGE_SIZE;
}

static u64 *test_table_access(void *context,u64 physical) {
    TEST_PAGE_TABLES *tables=(TEST_PAGE_TABLES *)context;
    if(physical<tables->Base||(physical-tables->Base)%BOB64_PAGE_SIZE)return 0;
    usize index=(usize)((physical-tables->Base)/BOB64_PAGE_SIZE);
    if(index>=tables->Used)return 0;
    return tables->Entries[index];
}

static int paging_tests(void) {
    TEST_PAGE_TABLES storage={0x100000000ULL,0,16,{{0}}};
    BOB64_PAGE_TABLE table;
    u64 physical=0,flags=0;
    CHECK(!bob64_page_table_init(&table,48,test_table_allocate,test_table_access,&storage),
          "initialize an x86-64 PML4 with physical pages above 4 GiB");
    CHECK(table.RootPhysical==0x100000000ULL,"retain a full-width PML4 address");
    CHECK(!bob64_page_map(&table,0x0000000100000000ULL,0x1234500000ULL,
                          BOB64_PAGE_WRITE|BOB64_PAGE_NX),
          "map a 4 KiB page through the four-level hierarchy");
    CHECK(bob64_page_translate(&table,0x0000000100000123ULL,&physical,&flags)==1&&
          physical==0x1234500123ULL&&(flags&BOB64_PAGE_WRITE)&&(flags&BOB64_PAGE_NX),
          "translate an offset with 64-bit physical address and permissions");
    CHECK(bob64_page_translate(&table,0x0000000100001000ULL,&physical,&flags)==0,
          "report an unmapped page");
    CHECK(!bob64_page_map(&table,0xffff800000001000ULL,0x100000000ULL,
                          BOB64_PAGE_USER),
          "map the canonical upper-half address without truncation");
    CHECK(bob64_page_translate(&table,0xffff800000001000ULL,&physical,&flags)==1&&
          physical==0x100000000ULL&&(flags&BOB64_PAGE_USER),
          "walk upper-half PML4 entries");
    CHECK(bob64_page_map(&table,0x1001,0x2000,0)==BOB64_PAGING_INVALID,
          "reject unaligned virtual pages");
    CHECK(bob64_page_map(&table,0x0000800000000000ULL,0x2000,0)==BOB64_PAGING_INVALID,
          "reject noncanonical virtual addresses");
    CHECK(bob64_page_map(&table,0x3000,0x2000,0x2000)==BOB64_PAGING_INVALID,
          "reject address bits smuggled through page flags");
    CHECK(!bob64_page_map(&table,0x9000,0xa000,BOB64_PAGE_USER|BOB64_PAGE_WRITE)&&
          !bob64_page_protect(&table,0x9000,BOB64_PAGE_USER|BOB64_PAGE_NX)&&
          bob64_page_translate(&table,0x9000,&physical,&flags)==1&&
          physical==0xa000&&(flags&BOB64_PAGE_USER)&&(flags&BOB64_PAGE_NX)&&
          !(flags&BOB64_PAGE_WRITE),
          "tighten a mapped page to read-only user data and disable execution");
    u64 removed_physical=0,removed_flags=0;
    CHECK(!bob64_page_unmap(&table,0x9000,&removed_physical,&removed_flags)&&
          removed_physical==0xa000&&(removed_flags&BOB64_PAGE_USER)&&
          bob64_page_translate(&table,0x9000,&physical,&flags)==0&&
          bob64_page_protect(&table,0x9000,BOB64_PAGE_WRITE)==BOB64_PAGING_NOT_MAPPED,
          "unmap a page, report its frame and permissions, and reject stale protection");
    CHECK(!bob64_page_map(&table,0x9000,0xb000,BOB64_PAGE_USER|BOB64_PAGE_WRITE)&&
          bob64_page_translate(&table,0x9000,&physical,&flags)==1&&physical==0xb000,
          "reuse a virtual page after removing its previous mapping");
    CHECK(bob64_page_map(&table,0x0000000100000000ULL,0x1000,0)==
          BOB64_PAGING_ALREADY_MAPPED,"reject accidental remapping");
    CHECK(!bob64_page_map_range(&table,0x400123,0x500123,0x2200,
                                BOB64_PAGE_WRITE|BOB64_PAGE_NX),
          "identity-map a byte range spanning partial first and last pages");
    CHECK(bob64_page_translate(&table,0x400123,&physical,&flags)==1&&
          physical==0x500123&&bob64_page_translate(&table,0x402322,&physical,&flags)==1&&
          physical==0x502322,"preserve offsets over a multi-page mapping");
    CHECK(bob64_page_map_range(&table,0x00007ffffffff000ULL,0x7000,0x2000,0)==
          BOB64_PAGING_INVALID&&
          bob64_page_translate(&table,0x00007ffffffff000ULL,&physical,&flags)==0,
          "reject a span crossing the canonical-address hole before changing page tables");
    CHECK(bob64_page_map_range(&table,0x6000,0x8000,0,0)==BOB64_PAGING_INVALID,
          "reject empty virtual-memory ranges");
    CHECK(!bob64_page_table_init(&table,40,test_table_allocate,test_table_access,&storage),
          "initialize a page table with the detected physical-address width");
    CHECK(bob64_page_map(&table,0x4000,0x10000000000ULL,0)==BOB64_PAGING_INVALID,
          "reject physical mappings above the CPU-reported address width");
    CHECK(bob64_page_table_init(&table,53,test_table_allocate,test_table_access,&storage)==
          BOB64_PAGING_INVALID,"reject unsupported physical-address widths");
    return 0;
}

static int process_address_space_tests(void) {
    TEST_PAGE_TABLES *storage=&test_clone_tables;
    BOB64_PAGE_TABLE kernel_table,process_table;
    u64 physical,flags;
    storage->Used=0;
    CHECK(!bob64_page_table_init(&kernel_table,48,test_table_allocate,
          test_table_access,storage),"initialize shared kernel mappings for a process root");
    CHECK(!bob64_page_map(&kernel_table,0x200000,0x300000,
          BOB64_PAGE_WRITE|BOB64_PAGE_NX)&&
          !bob64_page_map(&kernel_table,0xffff900000000000ULL,0x400000,
          BOB64_PAGE_WRITE|BOB64_PAGE_NX)&&
          !bob64_page_map(&kernel_table,BOB64_PROCESS_IMAGE_BASE,0x500000,
          BOB64_PAGE_USER|BOB64_PAGE_WRITE|BOB64_PAGE_NX)&&
          !bob64_page_map(&kernel_table,BOB64_PROCESS_STACK_BASE,0x501000,
          BOB64_PAGE_USER|BOB64_PAGE_WRITE|BOB64_PAGE_NX)&&
          !bob64_page_map(&kernel_table,BOB64_PROCESS_IMAGE_BASE+34ULL*1024*1024,
          0x502000,BOB64_PAGE_WRITE|BOB64_PAGE_NX),
          "seed kernel, stale process, stack, and adjacent high-half mappings");
    CHECK(!bob64_page_table_init(&process_table,48,test_table_allocate,
          test_table_access,storage)&&
          !bob64_page_table_clone_isolated(&process_table,&kernel_table,
             BOB64_PROCESS_IMAGE_BASE,34ULL*1024*1024),
          "clone the kernel mappings while privatizing the complete application arena");
    CHECK(bob64_page_translate(&process_table,0x200000,&physical,&flags)==1&&
          physical==0x300000&&bob64_page_translate(&process_table,
          0xffff900000000000ULL,&physical,&flags)==1&&physical==0x400000,
          "retain low identity and high-half kernel mappings in the process root");
    CHECK(bob64_page_translate(&process_table,BOB64_PROCESS_IMAGE_BASE,
          &physical,&flags)==0&&bob64_page_translate(&kernel_table,
          BOB64_PROCESS_IMAGE_BASE,&physical,&flags)==1&&physical==0x500000&&
          bob64_page_translate(&process_table,BOB64_PROCESS_STACK_BASE,
          &physical,&flags)==0,
          "remove stale image and stack pages only from the cloned user arena");
    CHECK(bob64_page_translate(&process_table,
          BOB64_PROCESS_IMAGE_BASE+34ULL*1024*1024,&physical,&flags)==1&&
          physical==0x502000,"preserve mappings immediately outside the isolated range");
    CHECK(!bob64_page_map(&process_table,BOB64_PROCESS_IMAGE_BASE+0x1000,0x600000,
          BOB64_PAGE_USER)&&!bob64_page_map(&process_table,
          BOB64_PROCESS_IMAGE_BASE+2ULL*1024*1024,0x601000,
          BOB64_PAGE_USER|BOB64_PAGE_WRITE|BOB64_PAGE_NX)&&
          bob64_page_translate(&kernel_table,BOB64_PROCESS_IMAGE_BASE+0x1000,
          &physical,&flags)==0&&bob64_page_translate(&process_table,
          BOB64_PROCESS_IMAGE_BASE+0x1000,&physical,&flags)==1&&physical==0x600000&&
          (flags&BOB64_PAGE_USER)&&!(flags&BOB64_PAGE_WRITE),
          "map private read-only executable code and writable NX data pages");
    CHECK(bob64_page_table_clone_isolated(&process_table,&kernel_table,
          BOB64_PROCESS_IMAGE_BASE+1,34ULL*1024*1024)==BOB64_PAGING_INVALID&&
          bob64_page_table_clone_isolated(&process_table,&kernel_table,
          BOB64_PROCESS_IMAGE_BASE,2ULL<<30)==BOB64_PAGING_INVALID,
          "reject unaligned and cross-PDPT isolation requests");
    return 0;
}

static int bootstrap_tests(void) {
    TEST_PAGE_TABLES storage={0x100000000ULL,16,16,{{0}}};
    BOB64_BOOTSTRAP_SPACE space;
    u64 physical,flags;
    CHECK(!bob64_bootstrap_space_init(&space,48,test_table_access,&storage,
          storage.Base,16,0x200000,0x5000,0x800000,0x8000,1),
          "build an identity-mapped bootstrap image, stack and table pool");
    CHECK(space.TablePoolUsed>1&&space.TablePoolUsed<=space.TablePoolPages,
          "allocate every paging level from the pre-reserved table pool");
    CHECK(bob64_page_translate(&space.PageTable,0x204123,&physical,&flags)==1&&
          physical==0x204123&&(flags&BOB64_PAGE_WRITE)&&!(flags&BOB64_PAGE_NX),
          "keep executable EFI image pages identity-mapped");
    CHECK(bob64_page_translate(&space.PageTable,0x807fff,&physical,&flags)==1&&
          physical==0x807fff&&(flags&BOB64_PAGE_WRITE)&&(flags&BOB64_PAGE_NX),
          "map the replacement stack writable and non-executable");
    CHECK(bob64_page_translate(&space.PageTable,storage.Base+0x9000,&physical,&flags)==1&&
          physical==storage.Base+0x9000&&(flags&BOB64_PAGE_NX),
          "identity-map table pages needed after loading the new CR3");
    CHECK(bob64_bootstrap_space_init(&space,48,test_table_access,&storage,
          storage.Base,1,0x200000,0x5000,0x800000,0x8000,1)==BOB64_PAGING_NO_MEMORY,
          "fail cleanly when the reserved table pool cannot cover all mappings");
    CHECK(bob64_bootstrap_space_init(&space,48,test_table_access,&storage,
          storage.Base,16,0x200000,0x5000,0x204000,0x2000,1)<0,
          "reject overlapping image and stack identity ranges");
    return 0;
}

static int cpu_tests(void) {
    BOB64_CPU_FEATURES features;
    BOB64_CPU_FEATURES host_features;
    CHECK(!bob64_cpu_decode_features(1,0x80000008u,1u<<6,
          (1u<<29)|(1u<<20),48,&features),"decode x86-64 CPU feature leaves");
    CHECK(features.PAE&&features.LongMode&&features.NX&&features.PhysicalAddressBits==48,
          "record PAE, long mode, NX and physical-address width");
    CHECK(!bob64_cpu_decode_features(1,0x80000001u,1u<<6,1u<<29,0,&features)&&
          features.PhysicalAddressBits==36,
          "use the architectural minimum address width when leaf 80000008 is absent");
    CHECK(!bob64_cpu_decode_features(0,0,1u<<6,(1u<<29)|(1u<<20),0,&features)&&
          !features.PAE&&!features.LongMode&&!features.NX,
          "do not trust feature bits from leaves the CPU does not provide");
    CHECK(bob64_cpu_decode_features(1,0x80000008u,1u<<6,1u<<29,53,&features)<0,
          "reject impossible CPUID physical-address widths");
    CHECK(bob64_cpu_decode_features(1,0x80000008u,1u<<6,1u<<29,35,&features)<0,
          "reject physical-address widths below PAE requirements");
    CHECK(!bob64_cpu_detect(&host_features)&&host_features.PAE&&host_features.LongMode&&
          host_features.PhysicalAddressBits>=36&&host_features.PhysicalAddressBits<=52,
          "query real host CPUID leaves without privileged instructions");
    return 0;
}

static int descriptor_tests(void) {
    u64 gdt[BOB64_GDT_ENTRY_COUNT];
    BOB64_TSS tss;
    BOB64_IDT_GATE idt[BOB64_IDT_ENTRIES];
    BOB64_DESCRIPTOR_TABLE_POINTER pointer;
    const u64 handler=0x1234567887654321ULL;
    CHECK(!bob64_tss_init(&tss,0x1234567887654000ULL)&&tss.Rsp0==0x1234567887654000ULL&&
          tss.IoMapBase==sizeof(tss)&&tss.Reserved0==0&&
          bob64_tss_init(&tss,0x1234567887654008ULL)<0,
          "initialize a 64-bit TSS with aligned privilege stack and disabled I/O bitmap");
    CHECK(!bob64_tss_set_rsp0(&tss,0xabcdef0087654000ULL)&&
          tss.Rsp0==0xabcdef0087654000ULL&&
          bob64_tss_set_rsp0(&tss,0xabcdef0087654008ULL)<0&&
          bob64_tss_set_rsp0(0,0x1234567887654000ULL)<0&&
          tss.Rsp0==0xabcdef0087654000ULL,
          "switch the TSS privilege stack without truncating its 64-bit address");
    CHECK(!bob64_tss_init(&tss,0x1234567887654000ULL),"restore a valid TSS after bad input");
    bob64_gdt_init(gdt,&pointer,&tss);
    CHECK(gdt[0]==0&&gdt[1]==0x00af9a000000ffffULL&&
          gdt[2]==0x00cf92000000ffffULL&&gdt[3]==0x00cff2000000ffffULL&&
          gdt[4]==0x00affa000000ffffULL,
          "build kernel and DPL3 user code/data segments in the 64-bit GDT");
    u64 tss_base=(u64)(uintptr_t)&tss;
    u64 tss_low=(103ULL)|((tss_base&0xffffffULL)<<16)|((u64)0x89<<40)|
                ((tss_base&0xff000000ULL)<<32);
    CHECK(gdt[5]==tss_low&&gdt[6]==(tss_base>>32)&&pointer.Limit==55&&
          pointer.Base==(u64)(uintptr_t)gdt&&pointer.Base>0xffffffffULL,
          "encode the complete TSS base in a two-slot 64-bit GDT descriptor");
    CHECK(!bob64_idt_init(idt,handler,BOB64_GDT_KERNEL_CODE_SELECTOR,0),
          "initialize a full 256-entry IDT");
    CHECK(idt[255].OffsetLow==0x4321&&idt[255].OffsetMiddle==0x8765&&
          idt[255].OffsetHigh==0x12345678&&idt[255].Selector==0x08&&
          idt[255].TypeAttributes==0x8e&&idt[255].Reserved==0,
          "split a 64-bit handler address into an x86-64 interrupt gate");
    CHECK(!bob64_idt_set_gate(&idt[14],handler,0x08,3,0,BOB64_IDT_TRAP_GATE)&&
          idt[14].Ist==3&&idt[14].TypeAttributes==0x8f,
          "support a trap gate and IST selection");
    CHECK(!bob64_idt_set_gate(&idt[128],handler,BOB64_GDT_KERNEL_CODE_SELECTOR,0,3,
          BOB64_IDT_INTERRUPT_GATE)&&idt[128].TypeAttributes==0xee,
          "encode a DPL3 interrupt gate for a future user syscall entry");
    bob64_idt_pointer(&pointer,idt);
    CHECK(pointer.Limit==4095&&pointer.Base==(u64)(uintptr_t)idt&&
          pointer.Base>0xffffffffULL,"build an IDTR without truncating its base");
    CHECK(bob64_idt_set_gate(&idt[0],handler,0,0,0,BOB64_IDT_INTERRUPT_GATE)<0&&
          bob64_idt_set_gate(&idt[0],handler,0x08,8,0,BOB64_IDT_INTERRUPT_GATE)<0&&
          bob64_idt_set_gate(&idt[0],handler,0x08,0,4,BOB64_IDT_INTERRUPT_GATE)<0&&
          bob64_idt_set_gate(&idt[0],handler,0x08,0,0,0x0c)<0,
          "reject invalid IDT selectors, IST, privilege and gate types");
    CHECK(bob64_idt_init(idt,0,0x08,0)<0,"reject an unusable default exception gate");
    CHECK(!bob64_interrupts_build_idt(idt,BOB64_GDT_KERNEL_CODE_SELECTOR),
          "wire all CPU exceptions to their dedicated 64-bit assembly stubs");
    for(usize vector=0;vector<32;vector++) {
        u64 stub=(u64)(uintptr_t)bob64_exception_stub_table[vector];
        CHECK(idt[vector].OffsetLow==(u16)stub&&
              idt[vector].OffsetMiddle==(u16)(stub>>16)&&
              idt[vector].OffsetHigh==(u32)(stub>>32),
              "preserve each assembly stub address in its IDT gate");
    }
    CHECK(idt[34].OffsetLow==(u16)(uintptr_t)bob64_unhandled_interrupt&&
          idt[255].OffsetHigh==(u32)((u64)(uintptr_t)bob64_unhandled_interrupt>>32),
          "route unassigned vectors to the fatal fallback stub");
    CHECK(idt[0x80].OffsetLow==(u16)(uintptr_t)bob64_syscall_entry&&
          idt[0x80].TypeAttributes==0xee,
          "install the user-callable DPL3 syscall gate");
    CHECK(idt[0x21].OffsetLow==(u16)(uintptr_t)bob64_irq1_entry&&
          idt[0x21].TypeAttributes==0x8e,
          "install the kernel-only keyboard IRQ gate");
    CHECK(idt[0x20].OffsetLow==(u16)(uintptr_t)bob64_irq0_entry&&
          idt[0x20].TypeAttributes==0x8e,
          "install the kernel-only PIT timer IRQ gate");
    CHECK(idt[0x2c].OffsetLow==(u16)(uintptr_t)bob64_irq12_entry&&
          idt[0x2c].TypeAttributes==0x8e,
          "install the kernel-only PS/2 mouse IRQ gate");
    return 0;
}

static char syscall_test_output[16];
static usize syscall_test_output_length;
static u64 syscall_test_user_base;
static char syscall_test_file_name[BOB64_SYSCALL_MAX_FILENAME+1];
static u8 syscall_test_file_data[BOB64_SYSCALL_MAX_BUFFER];
static usize syscall_test_file_length;
static u8 syscall_test_user_output[BOB64_SYSCALL_MAX_BUFFER];
static usize syscall_test_user_output_length;
static u32 syscall_test_surface[6*8];
static u32 syscall_test_display_pixels[6*8];
static u32 syscall_context_test_counts[2];
static BOB64_EVENT syscall_test_event={.Type=BOB64_EVENT_KEY_DOWN,.Key=0x01,
    .Character=0x1b};
static void syscall_test_write(void *context,u8 character) {
    (void)context;
    if(syscall_test_output_length<sizeof(syscall_test_output))
        syscall_test_output[syscall_test_output_length++]=(char)character;
}

static void syscall_context_test_write(void *context,u8 character) {
    u32 *count=(u32 *)context;
    if(count&&character)(*count)++;
}

static int syscall_context_tests(void) {
    BOB64_INTERRUPT_FRAME frame={0};
    frame.CS=BOB64_GDT_USER_CODE_SELECTOR;
    frame.RAX=BOB64_SYSCALL_WRITE_CHAR;
    frame.RCX='a';
    syscall_context_test_counts[0]=syscall_context_test_counts[1]=0;
    bob64_syscall_set_write(syscall_context_test_write,&syscall_context_test_counts[0]);
    bob64_user_active=1;
    if(bob64_syscall_dispatch(&frame)||frame.RAX||syscall_context_test_counts[0]!=1||
       bob64_syscall_context_push())return -1;
    bob64_syscall_set_write(syscall_context_test_write,&syscall_context_test_counts[1]);
    frame.RAX=BOB64_SYSCALL_WRITE_CHAR;frame.RCX='b';
    if(bob64_syscall_dispatch(&frame)||frame.RAX||syscall_context_test_counts[1]!=1||
       bob64_syscall_context_push())return -1;
    bob64_syscall_set_write(0,0);
    frame.RAX=BOB64_SYSCALL_WRITE_CHAR;frame.RCX='c';
    if(bob64_syscall_dispatch(&frame)||frame.RAX!=(u64)-1||
       bob64_syscall_context_pop())return -1;
    frame.RAX=BOB64_SYSCALL_WRITE_CHAR;frame.RCX='d';
    if(bob64_syscall_dispatch(&frame)||frame.RAX||syscall_context_test_counts[1]!=2||
       bob64_syscall_context_pop())return -1;
    frame.RAX=BOB64_SYSCALL_WRITE_CHAR;frame.RCX='e';
    if(bob64_syscall_dispatch(&frame)||frame.RAX||syscall_context_test_counts[0]!=2)
        return -1;
    bob64_user_active=0;
    bob64_syscall_set_write(0,0);
    for(u32 i=0;i<BOB64_SYSCALL_CONTEXT_MAX_DEPTH;i++)
        if(bob64_syscall_context_push())return -1;
    if(bob64_syscall_context_push()!=-1)return -1;
    for(u32 i=0;i<BOB64_SYSCALL_CONTEXT_MAX_DEPTH;i++)
        if(bob64_syscall_context_pop())return -1;
    return bob64_syscall_context_pop()?0:-1;
}
static int syscall_test_read_user(void *context,u64 address,void *destination,
                                  usize length) {
    static const char text[]="abc",name[]="note",data[]="data";
    const char *source=0;usize source_length=0;
    (void)context;
    if(address==syscall_test_user_base){source=text;source_length=sizeof(text)-1;}
    else if(address==syscall_test_user_base+16){source=name;source_length=sizeof(name)-1;}
    else if(address==syscall_test_user_base+32){source=data;source_length=sizeof(data)-1;}
    else if(address==syscall_test_user_base+3*BOB64_PAGE_SIZE&&
            length<=sizeof(syscall_test_user_output)) {
        memcpy(destination,syscall_test_user_output,length);
        return 0;
    }
    else if(address>=syscall_test_user_base+64&&
            address-syscall_test_user_base-64<=sizeof(syscall_test_surface)&&
            length<=sizeof(syscall_test_surface)-
                    (usize)(address-syscall_test_user_base-64)) {
        memcpy(destination,(const u8 *)syscall_test_surface+
               (usize)(address-syscall_test_user_base-64),length);
        return 0;
    }
    if(!source||length>source_length)return -1;
    memcpy(destination,source,length);
    return 0;
}
static int syscall_test_write_user(void *context,u64 address,const void *source,
                                   usize length) {
    (void)context;
    if((address!=syscall_test_user_base+48&&
        address!=syscall_test_user_base+3*BOB64_PAGE_SIZE)||
       length>sizeof(syscall_test_user_output))return -1;
    memcpy(syscall_test_user_output,source,length);
    syscall_test_user_output_length=length;
    return 0;
}
static int syscall_test_wait_event(void *context,BOB64_EVENT *event) {
    (void)context;
    if(!event)return -1;
    *event=syscall_test_event;
    return 0;
}
static s64 syscall_test_file_read(void *context,const char *name,u8 *buffer,
                                  usize capacity) {
    (void)context;
    if(strcmp(name,syscall_test_file_name))return -2;
    if(syscall_test_file_length>capacity)return -28;
    memcpy(buffer,syscall_test_file_data,syscall_test_file_length);
    return (s64)syscall_test_file_length;
}
static s64 syscall_test_file_write(void *context,const char *name,const u8 *buffer,
                                   usize length) {
    (void)context;
    if(length>sizeof(syscall_test_file_data)||strlen(name)>=sizeof(syscall_test_file_name))
        return -28;
    strcpy(syscall_test_file_name,name);memcpy(syscall_test_file_data,buffer,length);
    syscall_test_file_length=length;return (s64)length;
}
static s64 syscall_test_stream_open(void *context,const char *name,u32 flags) {
    (void)context;(void)flags;
    return strcmp(name,syscall_test_file_name)?-2:(s64)syscall_test_file_length;
}
static s64 syscall_test_stream_read(void *context,const char *name,u64 offset,
                                    u8 *buffer,usize capacity) {
    (void)context;
    if(strcmp(name,syscall_test_file_name))return -2;
    if(offset>=syscall_test_file_length)return 0;
    usize count=syscall_test_file_length-(usize)offset;
    if(count>capacity)count=capacity;
    memcpy(buffer,syscall_test_file_data+(usize)offset,count);
    return (s64)count;
}
static s64 syscall_test_stream_write(void *context,const char *name,u64 offset,
                                     const u8 *buffer,usize length) {
    (void)context;
    if(strcmp(name,syscall_test_file_name)||offset>sizeof(syscall_test_file_data)||
       length>sizeof(syscall_test_file_data)-(usize)offset)return -28;
    memcpy(syscall_test_file_data+(usize)offset,buffer,length);
    if((usize)offset+length>syscall_test_file_length)
        syscall_test_file_length=(usize)offset+length;
    return (s64)length;
}
static s64 syscall_test_file_list(void *context,BOB64_FILE_INFO *entries,
                                  usize capacity) {
    (void)context;
    if(!entries||!capacity)return -22;
    memset(entries,0,sizeof(*entries));
    strcpy(entries[0].Name,"note");entries[0].Size=4;
    return 1;
}
static s64 syscall_test_file_delete(void *context,const char *name) {
    (void)context;
    return strcmp(name,"note")?-2:0;
}

static int syscall_tests(void) {
    TEST_PAGE_TABLES *storage=&test_clone_tables;
    BOB64_PAGE_TABLE table;
    HEAP_TEST_ARENA window_arena={0};
    BOB64_HEAP window_heap;
    BOB64_WINDOW_SERVER window_server;
    BOB64_INTERRUPT_FRAME frame={0};
    u64 first=BOB64_PROCESS_IMAGE_BASE,physical;
    u64 window_owner=0x100000001ULL,window_handle;
    u64 timer_before;
    u64 *root;
    if(bob64_framebuffer_init((u64)(uintptr_t)syscall_test_display_pixels,
        sizeof(syscall_test_display_pixels),6,8,6,0))return -1;
    if(bob64_heap_init(&window_heap,sizeof(window_arena.Bytes),heap_test_grow,
       &window_arena)||bob64_window_server_init(&window_server,&window_heap,
       window_owner,6,8))return -1;
    for(usize i=0;i<sizeof(syscall_test_surface)/sizeof(syscall_test_surface[0]);i++)
        syscall_test_surface[i]=i?0x000000ffu:0x00ff0000u;
    storage->Used=0;
    if(bob64_page_table_init(&table,48,test_table_allocate,test_table_access,storage)||
       bob64_page_map(&table,first,0x800000,BOB64_PAGE_USER|BOB64_PAGE_WRITE)||
       bob64_page_map(&table,first+BOB64_PAGE_SIZE,0x801000,0)||
       bob64_page_map(&table,first+2*BOB64_PAGE_SIZE,0x802000,BOB64_PAGE_USER)||
       bob64_page_map(&table,first+3*BOB64_PAGE_SIZE,0x803000,
                      BOB64_PAGE_USER|BOB64_PAGE_WRITE)||
       bob64_page_map(&table,first+4*BOB64_PAGE_SIZE,0x804000,
                      BOB64_PAGE_USER|BOB64_PAGE_WRITE))return -1;
    if(bob64_syscall_validate_user_range(&table,first+16,64)||
       bob64_syscall_validate_user_range(&table,first+BOB64_PAGE_SIZE,1)==0||
       bob64_syscall_validate_user_range(&table,first+BOB64_PAGE_SIZE-1,2)==0||
       bob64_syscall_validate_user_range(&table,BOB64_PROCESS_STACK_GUARD,1)==0||
       bob64_syscall_validate_user_range(&table,first+3*BOB64_PAGE_SIZE,
                                         BOB64_SYSCALL_MAX_BUFFER)!=0||
       bob64_syscall_validate_user_range(&table,first+3*BOB64_PAGE_SIZE,
                                         BOB64_SYSCALL_MAX_BUFFER+1)==0||
       bob64_page_translate(&table,first,&physical,0)!=1)return -1;
    root=table.Access(table.Context,table.RootPhysical);
    usize root_index=(usize)((first>>39)&0x1ff);
    u64 original_root_entry=root[root_index];
    root[root_index]&=~BOB64_PAGE_USER;
    if(bob64_syscall_validate_user_range(&table,first,1)==0)return -1;
    root[root_index]=original_root_entry;
    bob64_syscall_set_address_space(&table);
    bob64_syscall_set_write(syscall_test_write,0);
    bob64_syscall_set_read_user(syscall_test_read_user,0);
    bob64_syscall_set_write_user(syscall_test_write_user,0);
    bob64_syscall_set_filesystem(syscall_test_file_read,syscall_test_file_write,0);
    bob64_syscall_set_file_stream(syscall_test_stream_open,syscall_test_stream_read,
                                  syscall_test_stream_write,0);
    bob64_syscall_set_file_manager(syscall_test_file_list,
                                   syscall_test_file_delete,0);
    bob64_syscall_set_wait_event(syscall_test_wait_event,0);
    bob64_syscall_set_window_server(&window_server,window_owner);
    bob64_user_active=1;
    syscall_test_output_length=0;syscall_test_user_base=first+16;
    frame.CS=0x23;frame.RAX=BOB64_SYSCALL_QUERY_ABI;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=BOB64_SYSCALL_ABI_VERSION)return -1;
    timer_before=bob64_timer_ticks();bob64_timer_irq_tick();
    frame.RAX=BOB64_SYSCALL_GET_TICKS;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=timer_before+1)return -1;
    frame.RAX=BOB64_SYSCALL_GET_DISPLAY;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=6||frame.RDX!=8)return -1;
    frame.RAX=BOB64_SYSCALL_PRESENT;frame.RCX=first+80;frame.RDX=6;frame.R8=8;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=48||
       syscall_test_display_pixels[0]!=0x000000ffu||
       syscall_test_display_pixels[1]!=0x00ff0000u)return -1;
    if(bob64_framebuffer_init((u64)(uintptr_t)syscall_test_display_pixels,
        sizeof(syscall_test_display_pixels),6,8,6,1))return -1;
    frame.RAX=BOB64_SYSCALL_PRESENT;frame.RCX=first+80;frame.RDX=6;frame.R8=8;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=48||
       syscall_test_display_pixels[0]!=0x00ff0000u||
       syscall_test_display_pixels[1]!=0x000000ffu)return -1;
    frame.RAX=BOB64_SYSCALL_PRESENT;frame.RCX=first+BOB64_PAGE_SIZE;
    frame.RDX=6;frame.R8=8;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-14)return -1;
    frame.RAX=BOB64_SYSCALL_PRESENT;frame.RCX=first+80;frame.RDX=5;frame.R8=8;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-14)return -1;
    frame.RAX=BOB64_SYSCALL_WAIT_EVENT;frame.RCX=first+64;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX||
       syscall_test_user_output_length!=sizeof(syscall_test_event)||
       memcmp(syscall_test_user_output,&syscall_test_event,sizeof(syscall_test_event)))return -1;
    frame.RAX=BOB64_SYSCALL_WAIT_EVENT;frame.RCX=first+BOB64_PAGE_SIZE;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-14)return -1;
    frame.RAX=BOB64_SYSCALL_WRITE_CHAR;frame.RCX='!';
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX||
       syscall_test_output_length!=1||syscall_test_output[0]!='!')return -1;
    frame.RAX=BOB64_SYSCALL_WRITE_BUFFER;frame.RCX=first+16;frame.RDX=3;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=3||
       syscall_test_output_length!=4||memcmp(syscall_test_output,"!abc",4))return -1;
    frame.RAX=BOB64_SYSCALL_WRITE_BUFFER;frame.RCX=first+BOB64_PAGE_SIZE;frame.RDX=1;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-14)return -1;
    frame.RAX=BOB64_SYSCALL_WRITE_FILE;frame.RCX=first+32;frame.RDX=4;
    frame.R8=BOB64_PROCESS_STACK_GUARD;frame.R9=4;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-14)return -1;
    frame.RAX=BOB64_SYSCALL_WRITE_FILE;
    frame.RCX=first+32;frame.R8=first+48;
    frame.RDX=4;frame.R9=4;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=4||
       strcmp(syscall_test_file_name,"note")||syscall_test_file_length!=4||
       memcmp(syscall_test_file_data,"data",4))return -1;
    frame.RAX=BOB64_SYSCALL_READ_FILE;frame.RCX=first+32;frame.RDX=4;
    frame.R8=first+64;frame.R9=16;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=4||
       syscall_test_user_output_length!=4||memcmp(syscall_test_user_output,"data",4))return -1;
    frame.RAX=BOB64_SYSCALL_OPEN_FILE;frame.RCX=first+32;frame.RDX=4;
    frame.R8=BOB64_FILE_OPEN_READ|BOB64_FILE_OPEN_WRITE;
    if(bob64_syscall_dispatch(&frame)!=0||!frame.RAX||(s64)frame.RAX<0)return -1;
    u64 stream_handle=frame.RAX;
    frame.RAX=BOB64_SYSCALL_READ_HANDLE;frame.RCX=stream_handle;
    frame.RDX=first+64;frame.R8=2;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=2||
       memcmp(syscall_test_user_output,"da",2))return -1;
    frame.RAX=BOB64_SYSCALL_SEEK_HANDLE;frame.RCX=stream_handle;
    frame.RDX=0x100000123ULL;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=0x100000123ULL)return -1;
    frame.RAX=BOB64_SYSCALL_SEEK_HANDLE;frame.RCX=stream_handle;frame.RDX=2;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=2)return -1;
    frame.RAX=BOB64_SYSCALL_WRITE_HANDLE;frame.RCX=stream_handle;
    frame.RDX=first+48;frame.R8=2;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=2||
       syscall_test_file_length!=4||memcmp(syscall_test_file_data,"dada",4))return -1;
    frame.RAX=BOB64_SYSCALL_CLOSE_HANDLE;frame.RCX=stream_handle;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX)return -1;
    frame.RAX=BOB64_SYSCALL_READ_HANDLE;frame.RCX=stream_handle;
    frame.RDX=first+64;frame.R8=1;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-9)return -1;
    frame.RAX=BOB64_SYSCALL_OPEN_FILE;frame.RCX=first+32;frame.RDX=4;
    frame.R8=BOB64_FILE_OPEN_WRITE|BOB64_FILE_OPEN_APPEND;
    if(bob64_syscall_dispatch(&frame)!=0||!frame.RAX||(s64)frame.RAX<0)return -1;
    stream_handle=frame.RAX;
    frame.RAX=BOB64_SYSCALL_WRITE_HANDLE;frame.RCX=stream_handle;
    frame.RDX=first+48;frame.R8=2;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=2||
       syscall_test_file_length!=6||memcmp(syscall_test_file_data,"dadada",6))return -1;
    frame.RAX=BOB64_SYSCALL_CLOSE_HANDLE;frame.RCX=stream_handle;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX)return -1;
    frame.RAX=BOB64_SYSCALL_READ_FILE;frame.RCX=first+32;frame.RDX=4;
    frame.R8=first+BOB64_PAGE_SIZE;frame.R9=16;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-14)return -1;
    frame.RAX=BOB64_SYSCALL_READ_FILE;frame.RCX=first+32;frame.RDX=4;
    frame.R8=first+2*BOB64_PAGE_SIZE;frame.R9=16;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-14)return -1;
    for(usize i=0;i<BOB64_SYSCALL_MAX_BUFFER;i++)
        syscall_test_file_data[i]=(u8)(i*37u+11u);
    syscall_test_file_length=BOB64_SYSCALL_MAX_BUFFER;
    memcpy(syscall_test_user_output,syscall_test_file_data,
           BOB64_SYSCALL_MAX_BUFFER);
    syscall_test_user_output_length=BOB64_SYSCALL_MAX_BUFFER;
    frame.RAX=BOB64_SYSCALL_READ_FILE;frame.RCX=first+32;frame.RDX=4;
    frame.R8=first+3*BOB64_PAGE_SIZE+16;frame.R9=BOB64_SYSCALL_MAX_BUFFER;
    if(bob64_syscall_dispatch(&frame)!=0||
       frame.RAX!=BOB64_SYSCALL_MAX_BUFFER||
       syscall_test_user_output_length!=BOB64_SYSCALL_MAX_BUFFER||
       memcmp(syscall_test_user_output,syscall_test_file_data,
              BOB64_SYSCALL_MAX_BUFFER))return -1;
    frame.RAX=BOB64_SYSCALL_WRITE_FILE;frame.RCX=first+32;frame.RDX=4;
    frame.R8=first+3*BOB64_PAGE_SIZE+16;frame.R9=BOB64_SYSCALL_MAX_BUFFER;
    if(bob64_syscall_dispatch(&frame)!=0||
       frame.RAX!=BOB64_SYSCALL_MAX_BUFFER||
       syscall_test_file_length!=BOB64_SYSCALL_MAX_BUFFER||
       memcmp(syscall_test_file_data,syscall_test_user_output,
              BOB64_SYSCALL_MAX_BUFFER))return -1;
    frame.RAX=BOB64_SYSCALL_LIST_FILES;frame.RCX=first+3*BOB64_PAGE_SIZE+16;
    frame.RDX=1;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=1||
       syscall_test_user_output_length!=sizeof(BOB64_FILE_INFO)||
       strcmp(((BOB64_FILE_INFO *)syscall_test_user_output)->Name,"note")||
       ((BOB64_FILE_INFO *)syscall_test_user_output)->Size!=4)return -1;
    frame.RAX=BOB64_SYSCALL_LIST_FILES;frame.RCX=first+2*BOB64_PAGE_SIZE+16;
    frame.RDX=1;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-14)return -1;
    frame.RAX=BOB64_SYSCALL_LIST_FILES;frame.RCX=first+3*BOB64_PAGE_SIZE+16;
    frame.RDX=BOB64_SYSCALL_MAX_FILES+1;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-22)return -1;
    frame.RAX=BOB64_SYSCALL_DELETE_FILE;frame.RCX=first+32;frame.RDX=4;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX)return -1;
    frame.RAX=BOB64_SYSCALL_DELETE_FILE;frame.RCX=first+BOB64_PAGE_SIZE;frame.RDX=4;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-14)return -1;
    frame.RAX=BOB64_SYSCALL_CREATE_WINDOW;frame.RCX=1;frame.RDX=2;
    frame.R8=2;frame.R9=2;
    if(bob64_syscall_dispatch(&frame)!=0||!frame.RAX||(s64)frame.RAX<0)return -1;
    window_handle=frame.RAX;
    frame.RAX=BOB64_SYSCALL_FOCUS_WINDOW;frame.RCX=window_handle;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX||
       window_server.Manager.Focused!=window_handle)return -1;
    frame.RAX=BOB64_SYSCALL_PRESENT_WINDOW;frame.RCX=window_handle;
    frame.RDX=first+80;frame.R8=2;frame.R9=2;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX!=4||
       syscall_test_display_pixels[2*6+1]!=0x00ff0000u)return -1;
    frame.RAX=BOB64_SYSCALL_PRESENT_WINDOW;frame.RCX=window_handle;
    frame.RDX=first+80;frame.R8=1;frame.R9=2;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-14)return -1;
    frame.RAX=BOB64_SYSCALL_DESTROY_WINDOW;frame.RCX=window_handle;
    if(bob64_syscall_dispatch(&frame)!=0||frame.RAX||window_server.Manager.Count)
        return -1;
    frame.RAX=BOB64_SYSCALL_READ_FILE;frame.RCX=first+32;frame.RDX=4;
    frame.R8=first+3*BOB64_PAGE_SIZE+16;
    frame.R9=BOB64_SYSCALL_MAX_BUFFER+1;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-22)return -1;
    frame.RAX=BOB64_SYSCALL_EXIT;frame.RCX=42;
    if(bob64_syscall_dispatch(&frame)!=1||bob64_user_active||
       bob64_user_return_value!=42)return -1;
    bob64_user_active=1;frame.RAX=99;
    if(bob64_syscall_dispatch(&frame)!=1||bob64_user_return_value!=-38||
       bob64_user_active)return -1;
    frame.CS=BOB64_GDT_KERNEL_CODE_SELECTOR;frame.RAX=BOB64_SYSCALL_EXIT;
    if(bob64_syscall_dispatch(&frame)!=0||(s64)frame.RAX!=-1||bob64_user_active)
        return -1;
    bob64_syscall_set_write(0,0);
    bob64_syscall_set_read_user(0,0);
    bob64_syscall_set_write_user(0,0);
    bob64_syscall_set_filesystem(0,0,0);
    bob64_syscall_set_file_stream(0,0,0,0);
    bob64_syscall_set_file_manager(0,0,0);
    bob64_syscall_set_wait_event(0,0);
    bob64_syscall_set_window_server(0,0);
    bob64_syscall_set_address_space(0);
    bob64_window_server_close(&window_server);
    return 0;
}

static u16 read16(const u8 *bytes) {
    return (u16)(bytes[0]|((u16)bytes[1]<<8));
}

static u32 read32(const u8 *bytes) {
    return (u32)bytes[0]|((u32)bytes[1]<<8)|((u32)bytes[2]<<16)|((u32)bytes[3]<<24);
}

static u64 read64(const u8 *bytes) {
    return (u64)read32(bytes)|((u64)read32(bytes+4)<<32);
}

static int memory_tests(void) {
    struct ExtendedDescriptor { EFI_MEMORY_DESCRIPTOR Descriptor;u64 Extension; } extended[2]={0};
    EFI_MEMORY_DESCRIPTOR allocation_map={EFI_MEMORY_CONVENTIONAL,0,0x400000,0,
                                          BOB64_PAGE_ALLOCATION_LIMIT+1,0};
    EFI_MEMORY_DESCRIPTOR physical_limit_map={EFI_MEMORY_CONVENTIONAL,0,
                                               0x0001000000000000ULL-0x2000ULL,0,4,0};
    BOB64_PAGE_EXTENT allocation_extents[2];
    EFI_MEMORY_DESCRIPTOR map[5]={0};
    BOB64_PAGE_EXTENT extents[4];
    BOB64_PAGE_ALLOCATOR allocator;
    map[0].Type=EFI_MEMORY_CONVENTIONAL;map[0].PhysicalStart=0x10000;map[0].NumberOfPages=0x80;
    map[1].Type=EFI_MEMORY_CONVENTIONAL;map[1].PhysicalStart=0x100000;map[1].NumberOfPages=4;
    map[2].Type=EFI_MEMORY_CONVENTIONAL;map[2].PhysicalStart=0x104000;map[2].NumberOfPages=4;
    map[3].Type=1;map[3].PhysicalStart=0x108000;map[3].NumberOfPages=4;
    map[4].Type=EFI_MEMORY_CONVENTIONAL;map[4].PhysicalStart=0x100000000ULL;map[4].NumberOfPages=3;
    CHECK(bob64_page_allocator_init(&allocator,extents,4,48,map,sizeof(map),sizeof(map[0]))==2,
          "build physical extents from conventional descriptors");
    CHECK(extents[0].Base==0x100000&&extents[0].Pages==8,
          "discard low memory and merge adjacent 4 KiB ranges");
    CHECK(extents[1].Base==0x100000000ULL&&extents[1].Pages==3,
          "preserve physical ranges above 4 GiB");
    extended[0].Descriptor.Type=EFI_MEMORY_CONVENTIONAL;
    extended[0].Descriptor.PhysicalStart=0x200000;extended[0].Descriptor.NumberOfPages=2;
    extended[1].Descriptor.Type=EFI_MEMORY_CONVENTIONAL;
    extended[1].Descriptor.PhysicalStart=0x100200000ULL;extended[1].Descriptor.NumberOfPages=2;
    CHECK(bob64_page_allocator_init(&allocator,extents,4,48,extended,sizeof(extended),
          sizeof(extended[0]))==2&&extents[1].Base==0x100200000ULL,
          "walk firmware descriptors with a larger versioned stride");
    CHECK(bob64_page_allocator_init(&allocator,extents,1,48,map,sizeof(map),sizeof(map[0]))==
          BOB64_PAGE_ALLOC_FULL,"reject maps with more runs than allocator storage");
    CHECK(bob64_page_allocator_init(&allocator,extents,4,48,map,sizeof(map),sizeof(map[0]))==2,
          "reinitialize allocator after rejected capacity");
    u64 low=bob64_page_alloc(&allocator,6);
    CHECK(low==0x100000&&extents[0].Base==0x106000&&extents[0].Pages==2,
          "allocate contiguous 4 KiB pages");
    CHECK(bob64_page_alloc(&allocator,2)==0x106000,
          "consume the remainder of a physical extent");
    u64 high=bob64_page_alloc(&allocator,2);
    CHECK(high==0x100000000ULL&&high>0xffffffffULL,
          "allocate a physical page address above 4 GiB without truncation");
    CHECK(bob64_page_alloc(&allocator,2)==0,"report physical page exhaustion");
    CHECK(!bob64_page_free(&allocator,low,6),"free an allocated range");
    CHECK(!bob64_page_free(&allocator,0x106000,2)&&allocator.Count==2&&
          extents[0].Base==0x100000&&extents[0].Pages==8,
          "merge adjacent freed ranges");
    CHECK(bob64_page_free(&allocator,low,1)==BOB64_PAGE_ALLOC_INVALID,
          "reject double free of overlapping pages");
    CHECK(bob64_page_free(&allocator,0x200000,1)==BOB64_PAGE_ALLOC_INVALID,
          "reject freeing a page not allocated by the manager");
    CHECK(bob64_page_free(&allocator,0x100001,1)==BOB64_PAGE_ALLOC_INVALID,
          "reject unaligned physical frees");
    CHECK(!bob64_page_free(&allocator,high,2)&&extents[1].Pages==3,
          "free high physical addresses without narrowing");
    CHECK(bob64_page_allocator_init(&allocator,extents,4,48,map,sizeof(map),sizeof(map[0])-1)==
          BOB64_PAGE_ALLOC_INVALID,"reject short memory-map descriptors");
    CHECK(bob64_page_allocator_init(&allocator,extents,4,48,map,sizeof(map)-1,sizeof(map[0]))==
          BOB64_PAGE_ALLOC_INVALID,"reject a truncated memory map");
    map[4].PhysicalStart=0xfffffffffffff000ULL;map[4].NumberOfPages=2;
    CHECK(bob64_page_allocator_init(&allocator,extents,4,48,map,sizeof(map),sizeof(map[0]))==
          BOB64_PAGE_ALLOC_INVALID,"reject overflowing 64-bit physical ranges");
    CHECK(bob64_page_allocator_init(&allocator,extents,4,48,&physical_limit_map,
          sizeof(physical_limit_map),sizeof(physical_limit_map))==1&&extents[0].Pages==2,
          "clip conventional memory at the CPUID physical-address limit");
    physical_limit_map.PhysicalStart=0x0001000000000000ULL;
    CHECK(bob64_page_allocator_init(&allocator,extents,4,48,&physical_limit_map,
          sizeof(physical_limit_map),sizeof(physical_limit_map))==0,
          "exclude ranges beginning above the CPUID physical-address limit");
    CHECK(bob64_page_allocator_init(&allocator,allocation_extents,2,48,&allocation_map,
          sizeof(allocation_map),sizeof(allocation_map))==1,
          "initialize a large range for page-table-frame allocation");
    for(usize i=0;i<BOB64_PAGE_ALLOCATION_LIMIT;i++)
        CHECK(bob64_page_alloc(&allocator,1)==0x400000+i*EFI_PAGE_SIZE,
              "allocate more than 128 independent page-table frames");
    CHECK(allocator.AllocatedCount==BOB64_PAGE_ALLOCATION_LIMIT&&
          bob64_page_alloc(&allocator,1)==0,
          "track 16,384 live 4 KiB allocations and report allocator-record exhaustion");
    return 0;
}

static int console_tests(void) {
    u32 pixels[16*16];
    CHECK(!bob64_framebuffer_init((u64)(uintptr_t)pixels,sizeof(pixels),16,16,16,0)&&
          bob64_framebuffer_available(),"initialize a valid GOP framebuffer");
    CHECK(pixels[0]==0x001c120cu&&pixels[5]==0x001c120cu,
          "clear framebuffer and encode GOP RGB-reserved pixels");
    bob64_framebuffer_write("B");
    CHECK(pixels[0]==0x00f8eee4u&&pixels[5]==0x001c120cu,
          "render a 5x7 glyph into the framebuffer");
    CHECK(!bob64_framebuffer_init((u64)(uintptr_t)pixels,sizeof(pixels),16,16,16,1)&&
          pixels[0]==0x000c121cu,
          "clear framebuffer in GOP BGR-reserved pixel format");
    bob64_framebuffer_write("B");
    CHECK(pixels[0]==0x00e4eef8u,"render BGR-reserved foreground pixels");
    CHECK(bob64_framebuffer_init((u64)(uintptr_t)pixels,sizeof(pixels)-1,16,16,16,1)<0&&
          bob64_framebuffer_init((u64)(uintptr_t)pixels,sizeof(pixels),16,16,16,3)<0&&
          bob64_framebuffer_init((u64)(uintptr_t)pixels,sizeof(pixels),4,16,16,0)<0,
          "reject undersized, blit-only, and too-small framebuffers");
    return 0;
}

static int heap_test_grow(void *context,void **region,usize *region_size) {
    HEAP_TEST_ARENA *arena=(HEAP_TEST_ARENA *)context;
    if(arena->PagesUsed>=4)return 0;
    *region=arena->Bytes+arena->PagesUsed*4096;
    *region_size=4096;
    arena->PagesUsed++;
    return 1;
}

static int heap_tests(void) {
    HEAP_TEST_ARENA arena={0};
    BOB64_HEAP heap;
    u8 *small,*large,*zero,*coalesced;
    CHECK(!bob64_heap_init(&heap,sizeof(arena.Bytes),heap_test_grow,&arena),
          "initialize a bounded 64-bit heap");
    small=(u8 *)bob64_heap_alloc(&heap,65);
    large=(u8 *)bob64_heap_alloc(&heap,9000);
    zero=(u8 *)bob64_heap_calloc(&heap,32,2);
    CHECK(small&&large&&zero&&((uintptr_t)small&15)==0&&
          ((uintptr_t)large&15)==0&&((uintptr_t)zero&15)==0&&
          bob64_heap_mapped_bytes(&heap)==3*4096,
          "grow the heap in contiguous pages and align allocations");
    small[0]=0x64;large[8999]=0x32;
    for(usize i=0;i<64;i++)CHECK(zero[i]==0,"calloc clears all requested bytes");
    CHECK(small[0]==0x64&&large[8999]==0x32,
          "heap allocations remain distinct and writable");
    CHECK(!bob64_heap_free(&heap,small)&&!bob64_heap_free(&heap,large)&&
          !bob64_heap_free(&heap,zero)&&bob64_heap_free(&heap,zero)<0&&
          bob64_heap_free(&heap,arena.Bytes)<0,
          "free blocks, reject double free, and reject non-allocation pointers");
    coalesced=(u8 *)bob64_heap_alloc(&heap,9000);
    CHECK(coalesced&&((uintptr_t)coalesced&15)==0,
          "coalesce adjacent free blocks for a larger allocation");
    CHECK(!bob64_heap_free(&heap,coalesced),"free the coalesced allocation");
    CHECK(!bob64_heap_calloc(&heap,(usize)-1,2),"reject calloc multiplication overflow");
    return 0;
}

static int snapshot_tests(void) {
    HEAP_TEST_ARENA arena={0};
    BOB64_HEAP heap;
    BOB64_FILESYSTEM source,target;
    u8 snapshot[512];
    usize size,written,length;
    const char *contents;
    CHECK(!bob64_heap_init(&heap,sizeof(arena.Bytes),heap_test_grow,&arena)&&
          !bob64_fs_init(&source,&heap)&&!bob64_fs_init(&target,&heap),
          "initialize B64S test filesystems");
    CHECK(!bob64_fs_write(&source,"alpha.txt","bob64",5)&&
          !bob64_fs_write(&source,"empty.txt",0,0)&&
          !bob64_fs_write(&source,"data-2","64 bit",6),
          "seed B64S with text and empty files");
    CHECK(!bob64_fs_snapshot_size(&source,&size)&&size<sizeof(snapshot)&&
          !bob64_fs_snapshot_write(&source,snapshot,sizeof(snapshot),&written)&&
          written==size&&snapshot[0]=='B'&&snapshot[1]=='6'&&
          snapshot[2]=='4'&&snapshot[3]=='S'&&snapshot[4]==1&&snapshot[16]==3,
          "serialize a versioned pointer-free B64S snapshot");
    CHECK(bob64_fs_snapshot_write(&source,snapshot,size-1,&written)<0,
          "reject undersized snapshot output buffer");
    CHECK(!bob64_fs_write(&target,"keep.txt","old",3)&&
          !bob64_fs_snapshot_restore(&target,snapshot,size)&&target.FileCount==3&&
          target.BytesUsed==11&&bob64_fs_read(&target,"alpha.txt",&contents,&length)==0&&
          length==5&&!strcmp(contents,"bob64")&&
          bob64_fs_read(&target,"keep.txt",&contents,&length)<0,
          "restore files transactionally and replace prior filesystem state");
    snapshot[size-1]^=1;
    CHECK(bob64_fs_snapshot_restore(&target,snapshot,size)<0&&target.FileCount==3&&
          bob64_fs_read(&target,"alpha.txt",&contents,&length)==0&&
          !strcmp(contents,"bob64"),"reject checksum corruption without changing files");
    snapshot[size-1]^=1;
    CHECK(bob64_fs_snapshot_restore(&target,snapshot,size-1)<0&&target.FileCount==3,
          "reject truncated snapshots without changing files");
    snapshot[4]=2;
    CHECK(bob64_fs_snapshot_restore(&target,snapshot,size)<0&&target.FileCount==3,
          "reject unsupported B64S versions");
    return 0;
}

static void exec_write16(u8 *bytes,u16 value) {
    bytes[0]=(u8)value;bytes[1]=(u8)(value>>8);
}

static void exec_write32(u8 *bytes,u32 value) {
    for(u32 i=0;i<4;i++)bytes[i]=(u8)(value>>(i*8));
}

static void exec_write64(u8 *bytes,u64 value) {
    exec_write32(bytes,(u32)value);exec_write32(bytes+4,(u32)(value>>32));
}

static u32 exec_test_checksum(const u8 *bytes,usize length) {
    u32 value=0xffffffffu;
    for(usize i=0;i<length;i++) {
        value^=bytes[i];
        for(u32 bit=0;bit<8;bit++)value=(value>>1)^(0xedb88320u&-(value&1u));
    }
    return ~value;
}

static u64 BOB64_MS_ABI abi_sum4(u64 a,u64 b,u64 c,u64 d) {
    return a+b*2+c*3+d*4;
}

static u64 BOB64_MS_ABI abi_sum6(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f) {
    return abi_sum4(a,b,c,d)+e*5+f*6;
}

static s64 BOB64_MS_ABI abi_test_entry(const BOB64_APP_STARTUP *startup) {
    if(!startup||startup->StructSize!=sizeof(*startup)||
       startup->AbiVersion!=BOB64_APP_ABI_VERSION||startup->Flags||
       startup->ArgumentCount!=3||!startup->Arguments||
       strcmp(startup->Arguments[0],"program")||
       strcmp(startup->Arguments[1],"two words")||
       strcmp(startup->Arguments[2],"last"))return -1;
    return 42;
}

#define TEST_APP_FRAME_LIMIT 32
typedef struct {
    u64 PhysicalAddress,VirtualAddress,Flags;
    u8 Allocated,Mapped;
    u8 Bytes[BOB64_PAGE_SIZE];
} TEST_APP_FRAME;

typedef struct {
    TEST_APP_FRAME Frames[TEST_APP_FRAME_LIMIT];
    u64 NextPhysical;
    usize AllocationCount,FreeCount,MapCount,UnmapCount,MapAttempts,FailMapAt;
} TEST_APP_MEMORY;

static u64 app_test_allocate(void *context) {
    TEST_APP_MEMORY *memory=(TEST_APP_MEMORY *)context;
    for(usize i=0;i<TEST_APP_FRAME_LIMIT;i++)if(!memory->Frames[i].Allocated) {
        TEST_APP_FRAME *frame=&memory->Frames[i];
        frame->Allocated=1;frame->Mapped=0;
        frame->PhysicalAddress=memory->NextPhysical;
        memory->NextPhysical+=BOB64_PAGE_SIZE;memory->AllocationCount++;
        return frame->PhysicalAddress;
    }
    return 0;
}

static int app_test_free(void *context,u64 physical) {
    TEST_APP_MEMORY *memory=(TEST_APP_MEMORY *)context;
    for(usize i=0;i<TEST_APP_FRAME_LIMIT;i++) {
        TEST_APP_FRAME *frame=&memory->Frames[i];
        if(frame->Allocated&&frame->PhysicalAddress==physical) {
            if(frame->Mapped)return -1;
            frame->Allocated=0;memory->FreeCount++;return 0;
        }
    }
    return -1;
}

static int app_test_map(void *context,u64 virtual_address,u64 physical,u64 flags,
                        u8 **writable) {
    TEST_APP_MEMORY *memory=(TEST_APP_MEMORY *)context;
    memory->MapAttempts++;
    if(memory->FailMapAt&&memory->MapAttempts==memory->FailMapAt)return -1;
    for(usize i=0;i<TEST_APP_FRAME_LIMIT;i++) {
        TEST_APP_FRAME *frame=&memory->Frames[i];
        if(frame->Allocated&&frame->PhysicalAddress==physical) {
            if(frame->Mapped)return -1;
            frame->Mapped=1;frame->VirtualAddress=virtual_address;frame->Flags=flags;
            *writable=frame->Bytes;memory->MapCount++;return 0;
        }
    }
    return -1;
}

static int app_test_unmap(void *context,u64 virtual_address) {
    TEST_APP_MEMORY *memory=(TEST_APP_MEMORY *)context;
    for(usize i=0;i<TEST_APP_FRAME_LIMIT;i++) {
        TEST_APP_FRAME *frame=&memory->Frames[i];
        if(frame->Mapped&&frame->VirtualAddress==virtual_address) {
            frame->Mapped=0;memory->UnmapCount++;return 0;
        }
    }
    return -1;
}

static TEST_APP_FRAME *app_test_find(TEST_APP_MEMORY *memory,u64 address);

static int app_test_protect(void *context,u64 virtual_address,u64 flags) {
    TEST_APP_MEMORY *memory=(TEST_APP_MEMORY *)context;
    TEST_APP_FRAME *frame=app_test_find(memory,virtual_address);
    if(!frame)return -1;
    frame->Flags=flags;return 0;
}

static TEST_APP_FRAME *app_test_find(TEST_APP_MEMORY *memory,u64 address) {
    u64 page=address&~(BOB64_PAGE_SIZE-1);
    for(usize i=0;i<TEST_APP_FRAME_LIMIT;i++)
        if(memory->Frames[i].Mapped&&memory->Frames[i].VirtualAddress==page)
            return memory->Frames+i;
    return 0;
}

static int app_test_read(TEST_APP_MEMORY *memory,u64 address,void *destination,usize length) {
    u8 *output=(u8 *)destination;
    while(length) {
        TEST_APP_FRAME *frame=app_test_find(memory,address);
        usize offset=(usize)(address&(BOB64_PAGE_SIZE-1));
        usize part=(usize)BOB64_PAGE_SIZE-offset;
        if(!frame)return -1;
        if(part>length)part=length;
        memcpy(output,frame->Bytes+offset,part);
        output+=part;address+=part;length-=part;
    }
    return 0;
}

static int process_loader_tests(const void *file,usize file_size) {
    TEST_APP_MEMORY memory={.NextPhysical=0x100000000ULL};
    BOB64_PROCESS_PAGE pages[24];
    BOB64_PROCESS process;
    BOB64_PROCESS_OPERATIONS operations={app_test_allocate,app_test_free,
        app_test_map,app_test_protect,app_test_unmap,&memory,1};
    const char *arguments[]={"program","two words","last"};
    BOB64_APP_STARTUP startup;
    u64 argv_pointer,return_address;
    u64 argv[4];
    u8 zero_check[8183];
    char text[16];
    CHECK(!bob64_process_init(&process,pages,24,&operations)&&
          !bob64_process_load(&process,file,file_size,3,arguments),
          "load a B64E image into separate 64-bit user code, data, and stack pages");
    CHECK(process.Loaded&&process.EntryAddress==BOB64_PROCESS_IMAGE_BASE+1&&
          process.PageCount==19&&process.StartupAddress&&
          process.InitialStackPointer>=BOB64_PROCESS_STACK_BASE&&
          (process.InitialStackPointer&15)==8&&
          app_test_find(&memory,BOB64_PROCESS_STACK_GUARD)==0,
          "place the entry, startup frame, and guarded stack in the 64-bit app layout");
    TEST_APP_FRAME *code=app_test_find(&memory,BOB64_PROCESS_IMAGE_BASE);
    TEST_APP_FRAME *data=app_test_find(&memory,BOB64_PROCESS_IMAGE_BASE+BOB64_PAGE_SIZE);
    TEST_APP_FRAME *stack=app_test_find(&memory,BOB64_PROCESS_STACK_BASE);
    CHECK(code&&data&&stack&&code->PhysicalAddress>0xffffffffULL&&
          (code->Flags&BOB64_PAGE_USER)&&!(code->Flags&BOB64_PAGE_WRITE)&&
          !(code->Flags&BOB64_PAGE_NX)&&(data->Flags&BOB64_PAGE_WRITE)&&
          (data->Flags&BOB64_PAGE_NX)&&(stack->Flags&BOB64_PAGE_USER)&&
          (stack->Flags&BOB64_PAGE_WRITE)&&(stack->Flags&BOB64_PAGE_NX),
          "enforce read-only executable code and writable non-executable user memory");
    CHECK(!app_test_read(&memory,process.StartupAddress,&startup,sizeof(startup))&&
          startup.StructSize==sizeof(startup)&&startup.AbiVersion==BOB64_APP_ABI_VERSION&&
          startup.ArgumentCount==3,
          "construct the versioned startup structure in application memory");
    argv_pointer=(u64)(uintptr_t)startup.Arguments;
    CHECK(!app_test_read(&memory,argv_pointer,argv,sizeof(argv))&&argv[3]==0,
          "construct a 64-bit, null-terminated app argv array");
    CHECK(!app_test_read(&memory,argv[0],text,8)&&!strcmp(text,"program")&&
          !app_test_read(&memory,argv[1],text,10)&&!strcmp(text,"two words")&&
          !app_test_read(&memory,argv[2],text,5)&&!strcmp(text,"last"),
          "copy argument strings into the app-owned stack");
    CHECK(!app_test_read(&memory,process.InitialStackPointer,&return_address,
                         sizeof(return_address))&&return_address==0&&
          !app_test_read(&memory,process.EntryAddress+4104,zero_check,sizeof(zero_check)),
          "set up the Microsoft x64 entry stack and zero data/BSS beyond the file image");
    for(usize i=0;i<sizeof(zero_check);i++)CHECK(zero_check[i]==0,
          "zero all executable memory not initialized by the file");
    CHECK(!bob64_process_unload(&process)&&!process.Loaded&&!process.PageCount&&
          memory.AllocationCount==memory.FreeCount&&memory.MapCount==memory.UnmapCount,
          "unmap and free all application and stack pages after exit");
    CHECK(!bob64_process_init(&process,pages,24,&operations),
          "reinitialize the app loader after a successful unload");
    memory.MapAttempts=0;memory.FailMapAt=5;
    CHECK(bob64_process_load(&process,file,file_size,3,arguments)<0&&!process.PageCount&&
          memory.AllocationCount==memory.FreeCount&&memory.MapCount==memory.UnmapCount,
          "roll back every physical frame and mapping after an allocation failure");
    return 0;
}

static int compiler_process_tests(const void *file,usize file_size) {
    TEST_APP_MEMORY memory={.NextPhysical=0x100000000ULL};
    BOB64_PROCESS_PAGE pages[24];
    BOB64_PROCESS process;
    BOB64_PROCESS_OPERATIONS operations={app_test_allocate,app_test_free,
        app_test_map,app_test_protect,app_test_unmap,&memory,1};
    const char *arguments[]={"resident"};
    CHECK(!bob64_process_init(&process,pages,24,&operations)&&
          !bob64_process_load(&process,file,file_size,1,arguments),
          "load the resident compiler's B64E output into a 64-bit process");
    TEST_APP_FRAME *code=app_test_find(&memory,BOB64_PROCESS_IMAGE_BASE);
    TEST_APP_FRAME *data=app_test_find(&memory,BOB64_PROCESS_IMAGE_BASE+BOB64_PAGE_SIZE);
    u64 string_address=BOB64_PROCESS_IMAGE_BASE+50+
                       (s64)(s32)read32(code?code->Bytes+46:(const u8 *)"\0\0\0\0");
    CHECK(process.EntryAddress==BOB64_PROCESS_IMAGE_BASE&&process.PageCount==18&&
          code&&data&&code->PhysicalAddress>0xffffffffULL&&
          !(code->Flags&BOB64_PAGE_WRITE)&&!(code->Flags&BOB64_PAGE_NX)&&
          (data->Flags&BOB64_PAGE_WRITE)&&(data->Flags&BOB64_PAGE_NX)&&
          !memcmp(code->Bytes,"\x48\x83\xec\x28",4)&&
          !memcmp(data->Bytes,"bob!",4)&&
          string_address==BOB64_PROCESS_IMAGE_BASE+BOB64_PAGE_SIZE&&
          string_address>0xffffffffULL,
          "map compiler-generated machine code RX and string data RW/NX above 4 GiB");
    CHECK(!bob64_process_unload(&process)&&memory.AllocationCount==memory.FreeCount&&
          memory.MapCount==memory.UnmapCount,
          "release every page after unloading the compiler-produced app");
    return 0;
}

static int executable_tests(void) {
    u8 file[BOB64_EXEC_HEADER_SIZE+4096+9]={0};
    u8 loaded[12288],before[12288];
    BOB64_EXEC_IMAGE parsed;
    const char *arguments[]={"program","two words","last",0};
    BOB64_APP_STARTUP startup={sizeof(startup),BOB64_APP_ABI_VERSION,3,arguments,0};
    BOB64_APP_ENTRY app_entry=abi_test_entry;
    u64 entry=0;
    CHECK(sizeof(BOB64_APP_STARTUP)==32&&app_entry(&startup)==42,
          "pass a versioned app-owned startup block through the x64 entry ABI");
    CHECK(abi_sum6(1,2,3,4,5,6)==91,
          "preserve Microsoft x64 register and stack arguments across nested calls");
    file[0]='B';file[1]='6';file[2]='4';file[3]='E';
    exec_write16(file+4,BOB64_EXEC_VERSION);
    exec_write16(file+6,BOB64_EXEC_HEADER_SIZE);
    exec_write32(file+8,BOB64_EXEC_ABI_VERSION);
    exec_write64(file+16,4096+9);exec_write64(file+24,sizeof(loaded));
    exec_write64(file+32,1);exec_write64(file+40,4096);
    memset(file+BOB64_EXEC_HEADER_SIZE,0x90,4096);
    for(usize i=0;i<9;i++)file[BOB64_EXEC_HEADER_SIZE+4096+i]=(u8)(0xa0+i);
    exec_write32(file+48,exec_test_checksum(file+BOB64_EXEC_HEADER_SIZE,4096+9));
    CHECK(!bob64_exec_parse(file,sizeof(file),&parsed)&&parsed.FileSize==4096+9&&
          parsed.MemorySize==sizeof(loaded)&&parsed.EntryOffset==1&&parsed.CodeSize==4096&&
          parsed.AbiVersion==BOB64_EXEC_ABI_VERSION,
          "parse a bounded B64E image with a versioned 64-bit memory/entry layout");
    CHECK(process_loader_tests(file,sizeof(file))==0,
          "B64E application loader maps, initializes, and releases user memory");
    memset(loaded,0xcc,sizeof(loaded));
    CHECK(!bob64_exec_load(&parsed,0x123456780000ULL,loaded,sizeof(loaded),&entry)&&
          entry==0x123456780001ULL&&entry>0xffffffffULL&&
          !memcmp(loaded,file+BOB64_EXEC_HEADER_SIZE,4096+9),
          "load at a synthetic address above 4 GiB without truncating the entry");
    for(usize i=4096+9;i<sizeof(loaded);i++)CHECK(loaded[i]==0,
          "zero the executable's memory-only tail");
    memset(loaded,0x5a,sizeof(loaded));memcpy(before,loaded,sizeof(loaded));
    CHECK(bob64_exec_load(&parsed,0x1000,loaded,sizeof(loaded)-1,&entry)<0&&
          !memcmp(loaded,before,sizeof(loaded)),
          "reject insufficient destination capacity before modifying memory");
    CHECK(bob64_exec_load(&parsed,~(u64)0-7,loaded,sizeof(loaded),&entry)<0&&
          !memcmp(loaded,before,sizeof(loaded)),
          "reject a wrapped 64-bit load range before modifying memory");
    file[4]=2;
    CHECK(bob64_exec_parse(file,sizeof(file),&parsed)<0,
          "reject an unsupported B64E executable version");
    file[4]=BOB64_EXEC_VERSION;
    CHECK(bob64_exec_parse(file,sizeof(file)-1,&parsed)<0,
          "reject a truncated B64E image");
    exec_write64(file+16,4096+10);
    CHECK(bob64_exec_parse(file,sizeof(file),&parsed)<0,
          "reject a declared payload size that differs from exact file length");
    exec_write64(file+16,4096+9);exec_write64(file+32,4096);
    CHECK(bob64_exec_parse(file,sizeof(file),&parsed)<0,
          "reject entry offsets outside the executable code range");
    exec_write64(file+32,1);exec_write64(file+40,8192);
    CHECK(bob64_exec_parse(file,sizeof(file),&parsed)<0,
          "reject code ranges larger than the file payload");
    exec_write64(file+40,4096);exec_write64(file+24,4096+8);
    CHECK(bob64_exec_parse(file,sizeof(file),&parsed)<0,
          "reject a memory image smaller than its file image");
    exec_write64(file+24,sizeof(loaded));exec_write64(file+40,4095);
    CHECK(bob64_exec_parse(file,sizeof(file),&parsed)<0,
          "reject a code/data boundary that cannot receive separate page permissions");
    exec_write64(file+40,4096);exec_write64(file+32,4096);
    CHECK(bob64_exec_parse(file,sizeof(file),&parsed)<0,
          "reject an executable entry exactly at the code boundary");
    exec_write64(file+32,1);file[BOB64_EXEC_HEADER_SIZE]^=1;
    CHECK(bob64_exec_parse(file,sizeof(file),&parsed)<0,
          "reject payload checksum corruption");
    file[BOB64_EXEC_HEADER_SIZE]^=1;file[60]=1;
    CHECK(bob64_exec_parse(file,sizeof(file),&parsed)<0,
          "reject nonzero reserved executable-header bytes");
    return 0;
}

static int keyboard_tests(void) {
    BOB64_KEYBOARD_STATE state={0};
    BOB64_EVENT event;
    state=(BOB64_KEYBOARD_STATE){0};
    CHECK(!bob64_keyboard_decode_event(&state,0x1e,&event)&&
          event.Type==BOB64_EVENT_KEY_DOWN&&event.Key==0x1e&&event.Character=='a',
          "emit typed key-down events");
    CHECK(!bob64_keyboard_decode_event(&state,0x9e,&event)&&
          event.Type==BOB64_EVENT_KEY_UP&&event.Key==0x1e&&!event.Character,
          "emit key-up events without repeating characters");
    CHECK(!bob64_keyboard_decode_event(&state,0x2a,&event)&&
          (event.Modifiers&BOB64_EVENT_MOD_SHIFT)&&
          !bob64_keyboard_decode_event(&state,0x1e,&event)&&event.Character=='A'&&
          !bob64_keyboard_decode_event(&state,0xaa,&event)&&
          !(event.Modifiers&BOB64_EVENT_MOD_SHIFT),
          "report modifier state in key events");
    CHECK(!bob64_keyboard_decode_event(&state,0x1d,&event)&&
          (event.Modifiers&BOB64_EVENT_MOD_CONTROL)&&
          !bob64_keyboard_decode_event(&state,0x1f,&event)&&event.Character=='s'&&
          (event.Modifiers&BOB64_EVENT_MOD_CONTROL)&&
          !bob64_keyboard_decode_event(&state,0x9d,&event)&&
          !(event.Modifiers&BOB64_EVENT_MOD_CONTROL),
          "report control state for app shortcuts");
    CHECK(bob64_keyboard_decode_event(&state,0xe0,&event)<0&&
          !bob64_keyboard_decode_event(&state,0x38,&event)&&
          (event.Modifiers&BOB64_EVENT_MOD_ALT)&&
          bob64_keyboard_decode_event(&state,0xe0,&event)<0&&
          !bob64_keyboard_decode_event(&state,0xb8,&event)&&
          !(event.Modifiers&BOB64_EVENT_MOD_ALT),
          "report extended right-alt make and release");
    CHECK(bob64_keyboard_decode_event(&state,0xe0,&event)<0&&
          !bob64_keyboard_decode_event(&state,0x48,&event)&&
          event.Key==(BOB64_EVENT_KEY_EXTENDED|0x48)&&!event.Character,
          "preserve extended arrow-key identity for GUI navigation");
    state=(BOB64_KEYBOARD_STATE){0};
    CHECK(bob64_keyboard_decode(&state,0x1e)=='a',"decode unshifted set-1 letters");
    CHECK(bob64_keyboard_decode(&state,0x2a)<0&&
          bob64_keyboard_decode(&state,0x1e)=='A',"decode left shift make");
    CHECK(bob64_keyboard_decode(&state,0xaa)<0&&
          bob64_keyboard_decode(&state,0x1e)=='a',"decode shift release");
    CHECK(bob64_keyboard_decode(&state,0x3a)<0&&
          bob64_keyboard_decode(&state,0x1e)=='A',"decode caps lock");
    CHECK(bob64_keyboard_decode(&state,0x2a)<0&&
          bob64_keyboard_decode(&state,0x1e)=='a'&&
          bob64_keyboard_decode(&state,0xaa)<0,"combine shift and caps lock");
    CHECK(bob64_keyboard_decode(&state,0x02)=='1'&&
          bob64_keyboard_decode(&state,0x2a)<0&&
          bob64_keyboard_decode(&state,0x02)=='!',"decode shifted punctuation");
    CHECK(bob64_keyboard_decode(&state,0xaa)<0&&
          bob64_keyboard_decode(&state,0x0e)=='\b'&&
          bob64_keyboard_decode(&state,0x1c)=='\n',"decode editing and enter keys");
    CHECK(bob64_keyboard_decode(&state,0xe0)<0&&
          bob64_keyboard_decode(&state,0x48)<0,"ignore unsupported extended keys");
    CHECK(bob64_keyboard_decode(&state,0xe1)<0&&
          bob64_keyboard_decode(&state,0x1d)<0&&bob64_keyboard_decode(&state,0x45)<0&&
          bob64_keyboard_decode(&state,0xe1)<0&&bob64_keyboard_decode(&state,0x9d)<0&&
          bob64_keyboard_decode(&state,0xc5)<0&&
          bob64_keyboard_decode(&state,0x1e)=='A',"ignore Pause sequence without losing later keys");
    bob64_keyboard_reset();bob64_keyboard_irq_set_active(1);
    CHECK(!bob64_keyboard_irq_capture(0x1e)&&!bob64_keyboard_irq_capture(0x9e)&&
          !bob64_keyboard_irq_pop_event(&event)&&event.Type==BOB64_EVENT_KEY_DOWN&&
          event.Character=='a'&&!bob64_keyboard_irq_pop_event(&event)&&
          event.Type==BOB64_EVENT_KEY_UP&&bob64_keyboard_irq_pop_event(&event)<0,
          "transfer IRQ-produced make/release events through the keyboard queue");
    for(u32 i=0;i<64;i++)CHECK(!bob64_keyboard_irq_capture(0x1e),
                               "fill the keyboard event queue");
    CHECK(bob64_keyboard_irq_capture(0x1e)<0&&
          !bob64_keyboard_irq_pop_event(&event)&&event.Character=='a',
          "drop new keyboard events when the IRQ queue is full");
    bob64_keyboard_irq_set_active(0);
    return 0;
}

static int mouse_tests(void) {
    BOB64_MOUSE_STATE state;
    BOB64_EVENT event;
    CHECK(!bob64_mouse_reset(&state,100,80)&&state.X==50&&state.Y==40,
          "initialize pointer at the screen center");
    CHECK(bob64_mouse_reset(&state,0,80)<0&&bob64_mouse_reset(&state,100,0)<0,
          "reject empty display geometry");
    CHECK(bob64_mouse_decode(&state,0x01,&event)<0&&
          bob64_mouse_decode(&state,0x08,&event)<0&&
          bob64_mouse_decode(&state,5,&event)<0&&
          !bob64_mouse_decode(&state,0xfd,&event)&&
          event.Type==BOB64_EVENT_MOUSE_MOVE&&event.X==55&&event.Y==43&&
          event.DeltaX==5&&event.DeltaY==3&&!event.Buttons,
          "decode signed three-byte motion packets with screen coordinates");
    CHECK(bob64_mouse_decode(&state,0x09,&event)<0&&
          bob64_mouse_decode(&state,0,&event)<0&&
          !bob64_mouse_decode(&state,0,&event)&&
          event.Type==BOB64_EVENT_MOUSE_BUTTON&&event.X==55&&event.Y==43&&
          event.Buttons==1&&!event.DeltaX&&!event.DeltaY,
          "emit left-button transitions at the pointer location");
    CHECK(bob64_mouse_decode(&state,0x08,&event)<0&&
          bob64_mouse_decode(&state,0,&event)<0&&
          !bob64_mouse_decode(&state,0,&event)&&!event.Buttons,
          "emit mouse-button release events");
    state.X=97;state.Y=1;
    CHECK(bob64_mouse_decode(&state,0x08,&event)<0&&
          bob64_mouse_decode(&state,10,&event)<0&&
          !bob64_mouse_decode(&state,10,&event)&&event.X==99&&event.Y==0&&
          event.DeltaX==2&&event.DeltaY==-1,
          "clamp pointer motion to the framebuffer edges");
    state.X=50;state.Y=40;
    CHECK(bob64_mouse_decode(&state,0x49,&event)<0&&
          bob64_mouse_decode(&state,127,&event)<0&&
          !bob64_mouse_decode(&state,0,&event)&&event.X==50&&event.Y==40&&
          event.Buttons==1,
          "ignore packet overflow deltas without wrapping coordinates");
    state.X=50;state.Y=40;state.Buttons=0;state.PacketSize=4;state.DeviceId=3;
    CHECK(bob64_mouse_decode(&state,0x08,&event)<0&&
          bob64_mouse_decode(&state,0,&event)<0&&
          bob64_mouse_decode(&state,0,&event)<0&&
          !bob64_mouse_decode(&state,0x0f,&event)&&
          event.Type==BOB64_EVENT_MOUSE_WHEEL&&event.Wheel==-1&&
          event.X==50&&event.Y==40,
          "decode signed IntelliMouse wheel events");
    state.DeviceId=4;
    CHECK(bob64_mouse_decode(&state,0x08,&event)<0&&
          bob64_mouse_decode(&state,0,&event)<0&&
          bob64_mouse_decode(&state,0,&event)<0&&
          !bob64_mouse_decode(&state,0x31,&event)&&
          event.Type==BOB64_EVENT_MOUSE_BUTTON&&event.Wheel==1&&
          event.Buttons==24,
          "decode extended mouse buttons alongside wheel movement");
    CHECK(!bob64_mouse_irq_queue_init(100,80),
          "initialize the IRQ mouse packet decoder and event queue");
    bob64_mouse_irq_set_active(1);
    CHECK(bob64_mouse_irq_capture(0x08)<0&&bob64_mouse_irq_capture(1)<0&&
          !bob64_mouse_irq_capture(0)&&!bob64_mouse_irq_pop_event(&event)&&
          event.Type==BOB64_EVENT_MOUSE_MOVE&&event.X==51&&event.Y==40&&
          event.DeltaX==1&&event.DeltaY==0&&bob64_mouse_irq_pop_event(&event)<0,
          "queue complete IRQ mouse packets as 64-bit app events");
    bob64_mouse_irq_set_active(0);
    return 0;
}

static int window_manager_tests(void) {
    BOB64_WINDOW_MANAGER manager;
    BOB64_WINDOW windows[4];
    BOB64_WINDOW_HANDLE order[4],first,second,wide_handle;
    BOB64_EVENT event={0},routed={0};
    void *wide_context=(void *)(uintptr_t)0x1234567887654321ULL;
    CHECK(!bob64_wm_init(&manager,windows,4,order,640,480),
          "initialize caller-owned 64-bit window-manager state");
    first=bob64_wm_create(&manager,10,10,300,220,(void *)(uintptr_t)0x100000001ULL);
    second=bob64_wm_create(&manager,50,40,200,150,wide_context);
    CHECK(first&&second&&first!=second&&manager.Focused==second&&
          windows[1].Context==wide_context&&
          (uintptr_t)windows[1].Context==0x1234567887654321ULL,
          "retain 64-bit window handles and app context pointers");
    CHECK(!bob64_wm_begin_draw(&manager)&&bob64_wm_next_dirty(&manager)==first&&
          bob64_wm_next_dirty(&manager)==second&&!bob64_wm_next_dirty(&manager),
          "draw dirty windows from bottom to top");
    event.Type=BOB64_EVENT_MOUSE_BUTTON;event.Buttons=1;event.X=60;event.Y=50;
    CHECK(bob64_wm_dispatch(&manager,&event,&routed)==(s64)second&&
          manager.Dragging==second&&manager.Focused==second&&
          routed.X==10&&routed.Y==10,
          "focus and begin dragging a window from its title bar");
    event=(BOB64_EVENT){0};event.Type=BOB64_EVENT_MOUSE_MOVE;
    event.Buttons=1;event.X=100;event.Y=100;
    CHECK(bob64_wm_dispatch(&manager,&event,&routed)==(s64)second&&
          windows[1].X==90&&windows[1].Y==90&&routed.X==10&&routed.Y==10,
          "route movement in window-local coordinates while dragging");
    event=(BOB64_EVENT){0};event.Type=BOB64_EVENT_KEY_DOWN;
    event.Character='a';event.Buttons=0;
    CHECK(bob64_wm_dispatch(&manager,&event,&routed)==(s64)second&&
          manager.LastButtons==1,
          "route keyboard input to focus without losing held mouse state");
    event=(BOB64_EVENT){0};event.Type=BOB64_EVENT_MOUSE_BUTTON;
    event.X=105;event.Y=110;event.Buttons=0;
    CHECK(bob64_wm_dispatch(&manager,&event,&routed)==(s64)second&&
          !manager.Dragging&&windows[1].X==95&&windows[1].Y==100,
          "apply final pointer movement before ending a drag");
    CHECK(!bob64_wm_move(&manager,second,1000,1000)&&
          windows[1].X==440&&windows[1].Y==330,
          "clamp moved windows to the screen edges");
    event=(BOB64_EVENT){0};event.Type=BOB64_EVENT_MOUSE_WHEEL;
    event.X=450;event.Y=340;event.Wheel=-1;
    CHECK(bob64_wm_dispatch(&manager,&event,&routed)==(s64)second&&
          routed.X==10&&routed.Y==10&&routed.Wheel==-1,
          "route wheel input in window-local coordinates");
    event.X=320;event.Y=240;
    CHECK(bob64_wm_dispatch(&manager,&event,&routed)==0,
          "ignore pointer events outside all visible windows");
    CHECK(bob64_wm_show(&manager,second,0)==1&&manager.Focused==first&&
          bob64_wm_hit_test(&manager,60,50)==first,
          "hide a window and restore focus to the remaining window");
    CHECK(bob64_wm_show(&manager,second,1)==1&&manager.Focused==second,
          "show and raise a window");
    manager.NextHandle=0x100000001ULL;
    wide_handle=bob64_wm_create(&manager,0,0,20,20,wide_context);
    CHECK(wide_handle==0x100000001ULL&&
          bob64_wm_dispatch(&manager,&(BOB64_EVENT){.Type=BOB64_EVENT_KEY_DOWN},
                            &routed)==(s64)wide_handle,
          "route focused-window events through a handle above 4 GiB");
    bob64_wm_invalidate(&manager,first);
    CHECK(!bob64_wm_begin_draw(&manager)&&bob64_wm_next_dirty(&manager)==first&&
          bob64_wm_next_dirty(&manager)==second&&bob64_wm_next_dirty(&manager)==wide_handle,
          "redraw overlaps after invalidation in z-order");
    CHECK(!bob64_wm_destroy(&manager,wide_handle)&&
          bob64_wm_destroy(&manager,wide_handle)<0,
          "destroy window handles without leaving stale slots");
    return 0;
}

static int window_server_tests(void) {
    HEAP_TEST_ARENA arena={0};
    BOB64_HEAP heap;
    BOB64_WINDOW_SERVER server;
    BOB64_EVENT event={0},routed={0};
    u32 framebuffer[8*8],red[3*3],green[3*3];
    u64 owner=0x100000001ULL;
    BOB64_WINDOW_HANDLE first,second;
    for(usize i=0;i<9;i++)red[i]=0x00ff0000u,green[i]=0x0000ff00u;
    CHECK(!bob64_heap_init(&heap,sizeof(arena.Bytes),heap_test_grow,&arena)&&
          !bob64_framebuffer_init((u64)(uintptr_t)framebuffer,sizeof(framebuffer),
                                 8,8,8,0)&&
          !bob64_window_server_init(&server,&heap,owner,8,8),
          "initialize a kernel-owned compositor for one 64-bit app owner");
    first=bob64_window_server_create(&server,owner,1,1,3,3);
    CHECK(first&&bob64_window_server_write_pixels(&server,owner,first,0,red,9)==0&&
          !bob64_window_server_present(&server,owner,first)&&
          framebuffer[1*8+1]==0x000000ffu&&framebuffer[0]!=0x000000ffu,
          "composite an app surface without exposing its kernel window record");
    second=bob64_window_server_create(&server,owner,2,2,3,3);
    CHECK(second&&second!=first&&
          bob64_window_server_write_pixels(&server,owner,second,0,green,9)==0&&
          !bob64_window_server_present(&server,owner,second)&&
          framebuffer[2*8+2]==0x0000ff00u&&framebuffer[1*8+1]==0x000000ffu,
          "compose overlapping surfaces in z-order while preserving visible pixels");
    CHECK(bob64_window_server_present(&server,owner+1,second)<0&&
          bob64_window_server_move(&server,owner+1,second,4,4)<0&&
          bob64_window_server_destroy(&server,owner+1,second)<0,
          "reject operations from a different 64-bit process owner");
    event.Type=BOB64_EVENT_MOUSE_BUTTON;event.Buttons=1;event.X=3;event.Y=3;
    CHECK(bob64_window_server_route_event(&server,owner,&event,&routed)==(s64)second&&
          routed.WindowHandle==second&&routed.X==1&&routed.Y==1&&
          routed.ScreenX==3&&routed.ScreenY==3&&server.Manager.Focused==second&&
          server.Manager.Dragging==second,
          "route mouse focus and title-bar dragging through kernel window state");
    CHECK(!bob64_window_server_move(&server,owner,second,4,4)&&
          framebuffer[2*8+2]==0x000000ffu&&framebuffer[4*8+4]==0x0000ff00u,
          "redraw exposed surfaces after a window moves");
    CHECK(!bob64_window_server_destroy(&server,owner,second)&&
          framebuffer[2*8+2]==0x000000ffu&&server.BytesAllocated==9*sizeof(u32),
          "free window surfaces and reveal windows beneath them");
    bob64_window_server_close(&server);
    CHECK(!server.Active&&!server.BytesAllocated&&!server.Manager.Count,
          "release every app window when its kernel window session closes");
    return 0;
}

static int editor_tests(void) {
    BOB64_EDITOR editor;
    BOB64_EVENT event={0};
    bob64_editor_init(&editor,"abc",3);
    bob64_editor_move(&editor,-1);
    event.Type=BOB64_EVENT_KEY_DOWN;event.Character='!';
    bob64_editor_handle_key(&editor,&event);
    CHECK(editor.Length==4&&editor.Cursor==3&&!memcmp(editor.Text,"ab!c",4)&&editor.Dirty,
          "insert text at the editor cursor");
    event.Character='\b';bob64_editor_handle_key(&editor,&event);
    CHECK(editor.Length==3&&!memcmp(editor.Text,"abc",3),
          "backspace removes the character before the cursor");
    event.Character=0;event.Key=BOB64_EVENT_KEY_EXTENDED|0x4b;
    bob64_editor_handle_key(&editor,&event);
    event.Key=0;event.Character='\n';bob64_editor_handle_key(&editor,&event);
    CHECK(editor.Length==4&&editor.Text[1]=='\n'&&editor.Cursor==2,
          "insert a newline between existing characters");
    event.Character=0;event.Key=BOB64_EVENT_KEY_EXTENDED|0x53;
    bob64_editor_handle_key(&editor,&event);
    CHECK(editor.Length==3&&!memcmp(editor.Text,"a\nc",3),
          "Delete removes the character at the cursor");
    bob64_editor_init(&editor,"abc\ndef",7);
    bob64_editor_move_vertical(&editor,-1);
    CHECK(editor.Cursor==3,"move vertically while preserving the text column");
    bob64_editor_move_vertical(&editor,1);
    CHECK(editor.Cursor==7,"move vertically back to the next line");
    editor.Cursor=1;
    CHECK(!bob64_editor_delete(&editor)&&editor.Length==6&&
          !memcmp(editor.Text,"ac\ndef",6),"delete the character at the cursor");
    bob64_editor_init(&editor,"abcdefghij",10);
    u32 row,column;
    bob64_editor_visual_position(&editor,4,&row,&column);
    CHECK(row==2&&column==2,"track the visual row and column across wrapped lines");
    bob64_editor_init(&editor,"abcd\nx",6);
    editor.Cursor=5;
    bob64_editor_visual_position(&editor,4,&row,&column);
    CHECK(row==1&&column==0,"do not double-wrap a newline after a full visual line");
    bob64_editor_init(&editor,0,0);
    for(u32 i=0;i<BOB64_EDITOR_CAPACITY;i++)
        if(bob64_editor_insert(&editor,'b'))return -1;
    CHECK(editor.Length==BOB64_EDITOR_CAPACITY&&
          bob64_editor_insert(&editor,'!')<0,
          "accept a full 4 KiB editor buffer and reject overflow");
    return 0;
}

typedef struct {
    char Text[4096];
    usize Length;
    usize ClearCount;
    usize RunCount,RunArgumentCount;
    usize CompileCount;
    char RunName[64],RunArguments[3][32],CompileSource[64],CompileOutput[64];
    int RunStarted;
    BOB64_FILESYSTEM *Filesystem;
} SHELL_TEST_OUTPUT;

static void shell_test_write(void *context,const char *text) {
    SHELL_TEST_OUTPUT *output=(SHELL_TEST_OUTPUT *)context;
    while(*text&&output->Length+1<sizeof(output->Text))
        output->Text[output->Length++]=*text++;
    output->Text[output->Length]=0;
}

static void shell_test_clear(void *context) {
    SHELL_TEST_OUTPUT *output=(SHELL_TEST_OUTPUT *)context;
    output->ClearCount++;
}

static s64 shell_test_run(void *context,const char *name,usize argument_count,
                          const char *const *arguments,int *started) {
    SHELL_TEST_OUTPUT *output=(SHELL_TEST_OUTPUT *)context;
    usize name_length=strlen(name);
    output->RunCount++;output->RunArgumentCount=argument_count;
    if(name_length>=sizeof(output->RunName))return -1;
    memcpy(output->RunName,name,name_length+1);
    for(usize i=0;i<argument_count&&i<3;i++) {
        usize length=strlen(arguments[i]);
        if(length>=sizeof(output->RunArguments[i]))return -1;
        memcpy(output->RunArguments[i],arguments[i],length+1);
    }
    *started=output->RunStarted;
    return -7;
}

static int shell_test_compile(void *context,const char *source,const char *output,
                              usize *error_offset) {
    static u8 compiled[BOB64_COMPILER_IMAGE_LIMIT];
    SHELL_TEST_OUTPUT *result=(SHELL_TEST_OUTPUT *)context;
    const char *source_text;usize source_length,compiled_length;
    if(strlen(source)>=sizeof(result->CompileSource)||
       strlen(output)>=sizeof(result->CompileOutput)||!result->Filesystem||
       bob64_fs_read(result->Filesystem,source,&source_text,&source_length))return -1;
    strcpy(result->CompileSource,source);strcpy(result->CompileOutput,output);
    int compile_result=bob64_compile_c(source_text,source_length,compiled,
        sizeof(compiled),&compiled_length,error_offset);
    if(compile_result)return compile_result;
    if(bob64_fs_write(result->Filesystem,output,compiled,compiled_length))return -1;
    result->CompileCount++;
    return 0;
}

static void shell_test_command(BOB64_SHELL *shell,const char *command) {
    while(*command)bob64_shell_input(shell,(u8)*command++);
    bob64_shell_input(shell,'\n');
}

static usize count_text(const char *text,const char *needle) {
    usize count=0,length=strlen(needle);
    while((text=strstr(text,needle))!=0){count++;text+=length;}
    return count;
}

static int shell_tests(void) {
    SHELL_TEST_OUTPUT output={0};
    BOB64_SHELL shell;
    BOB64_PAGE_ALLOCATOR allocator={0};
    BOB64_PAGE_EXTENT extent={0x200000,3};
    HEAP_TEST_ARENA arena={0};
    BOB64_HEAP heap;
    BOB64_FILESYSTEM filesystem;
    const char *file_data;
    usize file_length;
    allocator.Count=1;allocator.Capacity=1;allocator.Extents=&extent;
    allocator.AllocatedCount=1;allocator.Allocated[0].Pages=2;
    CHECK(!bob64_heap_init(&heap,sizeof(arena.Bytes),heap_test_grow,&arena)&&
          !bob64_fs_init(&filesystem,&heap)&&
          !bob64_fs_write(&filesystem,"seed.txt","initial",7),
          "initialize shell filesystem over the kernel heap");
    CHECK(!bob64_fs_read(&filesystem,"seed.txt",&file_data,&file_length)&&
          file_length==7&&!strcmp(file_data,"initial")&&
          !bob64_fs_write(&filesystem,"seed.txt","updated",7)&&
          !bob64_fs_read(&filesystem,"seed.txt",&file_data,&file_length)&&
          !strcmp(file_data,"updated"),
          "read and atomically replace heap-backed text files");
    char binary_data[3]={'a',0,'b'};
    CHECK(!bob64_fs_write(&filesystem,"binary.b64e",binary_data,sizeof(binary_data))&&
          !bob64_fs_read(&filesystem,"binary.b64e",&file_data,&file_length)&&
          file_length==sizeof(binary_data)&&!memcmp(file_data,binary_data,sizeof(binary_data))&&
          !bob64_fs_write(&filesystem,"seed.txt","updated",7),
          "store arbitrary binary executable data without changing text files");
    CHECK(!bob64_shell_init(&shell,&allocator,&heap,&filesystem,shell_test_write,
                            shell_test_clear,&output),"initialize the command shell");
    output.Filesystem=&filesystem;
    shell_test_command(&shell,"save");
    shell_test_command(&shell,"rm seed.txt");
    CHECK(filesystem.FileCount==1,"remove one file after creating a shell checkpoint");
    shell_test_command(&shell,"restore");
    CHECK(filesystem.FileCount==2&&
          !bob64_fs_read(&filesystem,"seed.txt",&file_data,&file_length)&&
          !strcmp(file_data,"updated"),"restore the shell B64S checkpoint");
    CHECK(!bob64_fs_read(&filesystem,"binary.b64e",&file_data,&file_length)&&
          file_length==sizeof(binary_data)&&!memcmp(file_data,binary_data,sizeof(binary_data)),
          "restore a binary executable file from B64S");
    shell_test_command(&shell,"cat binary.b64e");
    CHECK(strstr(output.Text,"a\\x00b")!=0,
          "render embedded binary NUL bytes safely in the shell");
    shell_test_command(&shell,"eCho Bob!");
    CHECK(strstr(output.Text,"Bob!\r\n")!=0&&shell.Length==0,
          "shell edits and executes case-insensitive commands");
    shell_test_command(&shell,"mem");
    CHECK(strstr(output.Text,"free pages=0x0000000000000003")!=0&&
          strstr(output.Text,"allocated pages=0x0000000000000002")!=0&&
          strstr(output.Text,"heap mapped=0x0000000000001000 bytes")!=0,
          "shell reports physical allocator and heap statistics");
    shell_test_command(&shell,"write notes.txt bob64 is alive");
    shell_test_command(&shell,"cat notes.txt");
    CHECK(strstr(output.Text,"saved\r\n")!=0&&
          strstr(output.Text,"bob64 is alive\r\n")!=0,
          "shell creates and reads text files");
    shell_test_command(&shell,"ls");
    CHECK(strstr(output.Text,"notes.txt")!=0&&filesystem.FileCount==3,
          "shell lists files backed by the kernel heap");
    shell_test_command(&shell,"rm notes.txt");
    CHECK(filesystem.FileCount==2&&filesystem.BytesUsed==10,
          "shell deletes files and returns heap blocks");
    CHECK(!bob64_fs_delete(&filesystem,"binary.b64e"),
          "remove the binary file after testing binary snapshot round-trip");
    usize streamed=0;u8 stream_buffer[8];
    CHECK(!bob64_fs_write(&filesystem,"stream.bin","abc",3)&&
          !bob64_fs_read_at(&filesystem,"stream.bin",1,stream_buffer,2,&streamed)&&
          streamed==2&&!memcmp(stream_buffer,"bc",2)&&
          !bob64_fs_write_at(&filesystem,"stream.bin",5,"Z",1)&&
          !bob64_fs_read(&filesystem,"stream.bin",&file_data,&file_length)&&
          file_length==6&&!memcmp(file_data,"abc\0\0Z",6)&&
          !bob64_fs_read_at(&filesystem,"stream.bin",6,stream_buffer,sizeof(stream_buffer),&streamed)&&
          streamed==0&&bob64_fs_write_at(&filesystem,"stream.bin",~(u64)0,"x",1)<0&&
          !bob64_fs_delete(&filesystem,"stream.bin"),
          "stream file ranges with 64-bit offsets, sparse zero-fill, EOF and overflow checks");
    static const char resident_source[]=
        "int main(void) { bob64_app_write(\"bob!\", 4); return 0; }";
    CHECK(!bob64_fs_write(&filesystem,"bob.c",resident_source,
                          sizeof(resident_source)-1),
          "place resident C source in the shell filesystem");
    shell_test_command(&shell,"ab\bc");
    CHECK(strstr(output.Text,"unknown command: ac")!=0,
          "shell backspace edits the command buffer");
    shell_test_command(&shell,"clear");
    CHECK(output.ClearCount==1&&strstr(output.Text,"bob64! kernel shell")!=0,
          "clear command resets the display");
    shell_test_command(&shell,"help");
    CHECK(strstr(output.Text,"version show kernel version")!=0&&
          strstr(output.Text,"restore  restore RAM checkpoint")!=0&&
          strstr(output.Text,"cc SOURCE [OUTPUT]")!=0&&
          strstr(output.Text,"desktop.b64e [FILE]")!=0,
          "help lists available commands");
    bob64_shell_set_runner(&shell,shell_test_run);
    bob64_shell_set_compiler(&shell,shell_test_compile);
    shell_test_command(&shell,"cc bob.c demo.b64e");
    CHECK(output.CompileCount==1&&!strcmp(output.CompileSource,"bob.c")&&
          !strcmp(output.CompileOutput,"demo.b64e")&&
          strstr(output.Text,"compiled to demo.b64e")!=0,
          "shell routes source and destination to the resident compiler");
    const char *compiled_file;usize compiled_file_length;BOB64_EXEC_IMAGE compiled_image;
    CHECK(!bob64_fs_read(&filesystem,"demo.b64e",&compiled_file,&compiled_file_length)&&
          !bob64_exec_parse(compiled_file,compiled_file_length,&compiled_image)&&
          !memcmp(compiled_image.Image+BOB64_PAGE_SIZE,"bob!",4),
          "save the resident compiler's validated B64E output in the filesystem");
    output.RunStarted=1;
    shell_test_command(&shell,"run demo.b64e first second");
    CHECK(output.RunCount==1&&output.RunArgumentCount==3&&
          !strcmp(output.RunName,"demo.b64e")&&
          !strcmp(output.RunArguments[0],"demo.b64e")&&
          !strcmp(output.RunArguments[1],"first")&&
          !strcmp(output.RunArguments[2],"second")&&
          strstr(output.Text,"application exit status=0xfffffffffffffff9")!=0,
          "launch a named app with argv and display its 64-bit exit status");
    output.RunStarted=0;
    shell_test_command(&shell,"run missing.b64e");
    CHECK(output.RunCount==2&&strstr(output.Text,"unable to start application")!=0,
          "report app loader failures without claiming an app was started");
    usize prompts=count_text(output.Text,"bob64> ");
    for(const char *character="version";*character;character++)
        bob64_shell_input(&shell,(u8)*character);
    bob64_shell_input(&shell,'\r');bob64_shell_input(&shell,'\n');
    CHECK(count_text(output.Text,"bob64> ")==prompts+1&&
          strstr(output.Text,"bob64 kernel 0.1")!=0,
          "consume CRLF as one Enter key and execute one command");
    CHECK(bob64_fs_write(&filesystem,"../escape","x",1)<0&&
          bob64_fs_write(&filesystem,"bad-name/part","x",1)<0,
          "reject path separators in flat RAM filesystem names");
    return 0;
}

static int contains_bytes(const u8 *bytes,usize length,const char *needle,usize needle_length) {
    if(needle_length>length)return 0;
    for(usize i=0;i<=length-needle_length;i++)
        if(!memcmp(bytes+i,needle,needle_length))return 1;
    return 0;
}

int main(int argc,char **argv) {
    FILE *file;
    long file_size;
    u8 *image;
    u32 pe_offset,entry_rva,relocation_rva,relocation_size;
    u16 sections,optional_size;
    usize i;
    int found_rdmsr=0;
    uintptr_t synthetic=(uintptr_t)0x1234567887654321ULL;

    CHECK(argc>=2&&argc<=5,"expected BOOTX64.EFI and optional B64E app paths");
    CHECK(sizeof(void *)==8&&sizeof(uintptr_t)==8&&sizeof(usize)==8,
          "the test and target use 64-bit pointers and sizes");
    CHECK(sizeof(char)==1&&sizeof(short)==2&&sizeof(int)==4&&sizeof(long long)==8,
          "bob64 fixed-width scalar model");
    CHECK(libc_tests()==0,"bob64 C runtime tests");
    CHECK(compiler_tests()==0,"resident x86-64 C compiler tests");
    CHECK(compiler_arithmetic_tests()==0,"64-bit resident compiler arithmetic tests");
    CHECK(compiler_local_tests()==0,"resident compiler local-variable tests");
    CHECK(memory_tests()==0,"physical page allocator regression tests");
    CHECK(console_tests()==0,"GOP framebuffer console regression tests");
    CHECK(heap_tests()==0,"64-bit heap allocator regression tests");
    CHECK(snapshot_tests()==0,"versioned B64S snapshot regression tests");
    CHECK(executable_tests()==0,"versioned 64-bit native executable regression tests");
    CHECK(compiler_process_tests(resident_compiler_image,
          resident_compiler_image_length)==0,
          "resident compiler output integration with the process loader");
    CHECK(keyboard_tests()==0,"PS/2 keyboard scan-code regression tests");
    CHECK(mouse_tests()==0,"PS/2 mouse packet and pointer regression tests");
    CHECK(window_manager_tests()==0,"64-bit GUI window-manager regression tests");
    CHECK(window_server_tests()==0,"64-bit kernel window-server regression tests");
    CHECK(editor_tests()==0,"native GUI text-editor buffer regression tests");
    CHECK(shell_tests()==0,"interactive shell command regression tests");
    CHECK(paging_tests()==0,"four-level x86-64 page table regression tests");
    CHECK(process_address_space_tests()==0,
          "isolated process page-table root regression tests");
    CHECK(bootstrap_tests()==0,"kernel bootstrap address-space regression tests");
    CHECK(cpu_tests()==0,"CPUID feature decoder regression tests");
    CHECK(descriptor_tests()==0,"64-bit GDT/IDT descriptor regression tests");
    CHECK(syscall_tests()==0,"versioned write and process-exit syscall tests");
    CHECK(syscall_context_tests()==0,
          "nested syscall service contexts restore callbacks and enforce depth");
    CHECK(synthetic>0xffffffffu&&synthetic+0x1000u==0x1234567887655321ULL,
          "synthetic pointer arithmetic preserves bits above 4 GiB");

    file=fopen(argv[1],"rb");CHECK(file,"open UEFI image");
    CHECK(!fseek(file,0,SEEK_END),"seek UEFI image");
    file_size=ftell(file);CHECK(file_size>0&&file_size<16000000,"reasonable UEFI image size");
    CHECK(!fseek(file,0,SEEK_SET),"rewind UEFI image");
    image=(u8 *)malloc((usize)file_size);CHECK(image,"allocate image buffer");
    CHECK(fread(image,1,(usize)file_size,file)==(usize)file_size,"read UEFI image");
    CHECK(!fclose(file),"close UEFI image");
    CHECK(file_size>=64&&image[0]=='M'&&image[1]=='Z',"DOS-compatible PE header");
    pe_offset=read32(image+0x3c);
    CHECK((u64)pe_offset+24u<=(u64)file_size,"PE header lies inside image");
    CHECK(image[pe_offset]=='P'&&image[pe_offset+1]=='E'&&
          image[pe_offset+2]==0&&image[pe_offset+3]==0,"PE signature");
    CHECK(read16(image+pe_offset+4)==0x8664,"AMD64 machine type");
    sections=read16(image+pe_offset+6);
    optional_size=read16(image+pe_offset+20);
    CHECK(sections>0&&optional_size>=112,"PE32+ optional header is present");
    usize optional=(usize)pe_offset+24u;
    CHECK(optional+(usize)optional_size<=(usize)file_size,"optional header bounds");
    CHECK(read16(image+optional)==0x20b,"PE32+ (64-bit) optional header");
    entry_rva=read32(image+optional+16);
    CHECK(entry_rva!=0,"UEFI entry point is set");
    CHECK(read64(image+optional+24)!=0,"64-bit image base is represented");
    CHECK(read16(image+optional+68)==10,"EFI application subsystem");
    CHECK(read16(image+optional+70)&0x40,"UEFI image advertises relocatable loading");
    relocation_rva=read32(image+optional+112+5*8);
    relocation_size=read32(image+optional+112+5*8+4);
    CHECK(relocation_rva&&relocation_size>=8,
          "PE image has a base relocation directory for firmware-selected load addresses");
    CHECK(contains_bytes(image,(usize)file_size,"B64E",4)&&
          contains_bytes(image,(usize)file_size,"bob!",4)&&
          contains_bytes(image,(usize)file_size,"int main(void)",14),
          "embed the C-compiled app and resident compiler source in the kernel image");
    for(i=0;i+1<(usize)file_size;i++)if(image[i]==0x0f&&image[i+1]==0x32){found_rdmsr=1;break;}
    CHECK(found_rdmsr,"image contains the EFER.LMA long-mode check");

    if(argc>=3) {
        BOB64_EXEC_IMAGE app;
        unsigned char *app_file;
        long app_file_size;
        FILE *app_stream=fopen(argv[2],"rb");
        CHECK(app_stream,"open compiler-produced B64E application");
        CHECK(!fseek(app_stream,0,SEEK_END)&&(app_file_size=ftell(app_stream))>0&&
              !fseek(app_stream,0,SEEK_SET),"measure compiler-produced B64E application");
        app_file=(unsigned char *)malloc((usize)app_file_size);
        CHECK(app_file,"allocate compiler-produced B64E application buffer");
        CHECK(fread(app_file,1,(usize)app_file_size,app_stream)==(usize)app_file_size&&
              !fclose(app_stream),"read compiler-produced B64E application");
        usize app_size=(usize)app_file_size;
        CHECK(app_file,"read compiler-produced B64E application");
        CHECK(!bob64_exec_parse(app_file,app_size,&app)&&
              app.AbiVersion==BOB64_APP_ABI_VERSION&&app.EntryOffset>0&&
              app.CodeSize==BOB64_PAGE_SIZE&&app.FileSize>app.CodeSize&&
              app.MemorySize>=app.FileSize&&app.MemorySize<=BOB64_PROCESS_IMAGE_LIMIT,
              "parse compiler-produced B64E metadata and bounded memory layout");
        CHECK(app.Image[app.EntryOffset]==0x48&&app.Image[app.EntryOffset+1]==0x83&&
              app.Image[app.EntryOffset+2]==0xec&&app.Image[app.EntryOffset+3]==0x28&&
              contains_bytes(app.Image,(usize)app.FileSize,"bob!",4),
              "preserve the x64 app entry stub and compiled message bytes");
        free(app_file);
    }

    if(argc>=4) {
        BOB64_EXEC_IMAGE app;
        unsigned char *app_file;
        long app_file_size;
        FILE *app_stream=fopen(argv[3],"rb");
        CHECK(app_stream,"open graphics surface B64E application");
        CHECK(!fseek(app_stream,0,SEEK_END)&&(app_file_size=ftell(app_stream))>0&&
              !fseek(app_stream,0,SEEK_SET),"measure graphics surface application");
        app_file=(unsigned char *)malloc((usize)app_file_size);
        CHECK(app_file,"allocate graphics application buffer");
        CHECK(fread(app_file,1,(usize)app_file_size,app_stream)==(usize)app_file_size&&
              !fclose(app_stream),"read graphics surface application");
        CHECK(!bob64_exec_parse(app_file,(usize)app_file_size,&app)&&
              app.AbiVersion==BOB64_APP_ABI_VERSION&&app.EntryOffset>0&&
              app.MemorySize>1024u*768u*sizeof(u32)&&
              contains_bytes(app.Image,(usize)app.FileSize,"bob!",4),
              "package a native graphics app with a full-screen frame and bob! output");
        free(app_file);
    }

    if(argc==5) {
        BOB64_EXEC_IMAGE app;
        unsigned char *app_file;
        long app_file_size;
        FILE *app_stream=fopen(argv[4],"rb");
        CHECK(app_stream,"open multi-window desktop B64E application");
        CHECK(!fseek(app_stream,0,SEEK_END)&&(app_file_size=ftell(app_stream))>0&&
              !fseek(app_stream,0,SEEK_SET),"measure desktop application");
        app_file=(unsigned char *)malloc((usize)app_file_size);
        CHECK(app_file,"allocate desktop application buffer");
        CHECK(fread(app_file,1,(usize)app_file_size,app_stream)==(usize)app_file_size&&
              !fclose(app_stream),"read desktop application");
        CHECK(!bob64_exec_parse(app_file,(usize)app_file_size,&app)&&
              app.AbiVersion==BOB64_APP_ABI_VERSION&&app.EntryOffset>0&&
              app.MemorySize>1024u*768u*sizeof(u32)&&
              contains_bytes(app.Image,(usize)app.FileSize,"BOB64 DESKTOP",13)&&
              contains_bytes(app.Image,(usize)app.FileSize,"CTRL S SAVE",11)&&
              contains_bytes(app.Image,(usize)app.FileSize,"ESC AGAIN TO QUI",16)&&
              contains_bytes(app.Image,(usize)app.FileSize,"notes.txt",9)&&
              contains_bytes(app.Image,(usize)app.FileSize,"bob!",4),
              "package a multi-window editor desktop with save support and bob! output");
        free(app_file);
    }

    free(image);
    puts("bob!");
    puts("64-bit UEFI image checks passed.");
    return 0;
}
