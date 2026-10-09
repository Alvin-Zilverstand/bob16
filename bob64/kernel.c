#include "kernel.h"
#include "interrupts.h"
#include "paging.h"
#include "console.h"
#include "heap.h"
#include "runtime.h"
#include "keyboard.h"
#include "mouse.h"
#include "shell.h"
#include "filesystem.h"
#include "exec.h"
#include "process.h"
#include "syscall.h"
#include "compiler.h"
#include "firmware_store.h"

#define BOB64_KERNEL_HEAP_BASE 0xffff900000000000ULL
#define BOB64_KERNEL_HEAP_LIMIT (64ULL*1024ULL*1024ULL)
#define BOB64_KERNEL_PRIVILEGE_STACK_SIZE (16ULL*1024ULL)

static BOB64_HEAP kernel_heap;
static BOB64_FILESYSTEM kernel_filesystem;
static u64 kernel_next_window_owner=1;
static BOB64_SHELL kernel_shell;
typedef struct {
    BOB64_FILESYSTEM *Filesystem;
    u8 NxSupported,PhysicalAddressBits;
} KERNEL_APP_CONTEXT;
static KERNEL_APP_CONTEXT kernel_app_context;
static usize kernel_heap_mapped;
static usize kernel_heap_high_water;
static void *kernel_privilege_stack;
static char kernel_user_output[1024];
static usize kernel_user_output_length;
static u8 kernel_user_output_truncated;
static u8 kernel_compiler_output[BOB64_COMPILER_IMAGE_LIMIT];
static void kernel_write(const char *text);
static void kernel_hex64(u64 value);

static int kernel_text_contains(const char *text,const char *needle) {
    if(!text||!needle)return 0;
    for(usize i=0;text[i];i++) {
        usize j=0;
        while(needle[j]&&text[i+j]&&text[i+j]==needle[j])j++;
        if(!needle[j])return 1;
    }
    return 0;
}

static int kernel_interrupts_enabled(void) {
    u64 flags;
    __asm__ volatile("pushfq; pop %0":"=r"(flags));
    return (flags&0x200)!=0;
}
static void kernel_user_write_character(void *context,u8 character);
static s64 kernel_run_application(void *context,const char *name,
                                  usize argument_count,
                                  const char *const *arguments,int *started);
static int kernel_app_run_application(void *context,const char *name,
                                      usize argument_count,
                                      const char *const *arguments,
                                      s64 *exit_status);
static int kernel_copy_user_bytes(void *context,u64 address,void *destination,
                                  usize length);
static int kernel_process_fault_smoke_test(BOB64_PROCESS *process,
    BOB64_PAGE_TABLE *process_table,const BOB64_PROCESS_OPERATIONS *operations,
    const u8 *executable,usize executable_size,u64 expected_rip,u64 error_mask,
    u64 error_value,const char *success_message);
static int kernel_wait_for_input_event(void *context,BOB64_EVENT *event);
static int kernel_snapshot_save(void *context,const void *snapshot,
                                usize snapshot_size);
static int kernel_snapshot_restore(void *context,BOB64_FILESYSTEM *filesystem);
extern const u8 bob64_embedded_app[];
extern const u8 bob64_embedded_app_end[];
extern const u8 bob64_embedded_display[];
extern const u8 bob64_embedded_display_end[];
extern const u8 bob64_embedded_gui[];
extern const u8 bob64_embedded_gui_end[];
extern const u8 bob64_embedded_nested_app[];
extern const u8 bob64_embedded_nested_app_end[];
extern const u8 bob64_embedded_echo[];
extern const u8 bob64_embedded_echo_end[];
extern const u8 bob64_embedded_ls[];
extern const u8 bob64_embedded_ls_end[];
extern const u8 bob64_embedded_cat[];
extern const u8 bob64_embedded_cat_end[];
extern const u8 bob64_embedded_notes[];
extern const u8 bob64_embedded_notes_end[];
extern const u8 bob64_embedded_info[];
extern const u8 bob64_embedded_info_end[];
extern const u8 bob64_embedded_mouse_smoke[];
extern const u8 bob64_embedded_mouse_smoke_end[];
extern const u8 bob64_embedded_launcher[];
extern const u8 bob64_embedded_launcher_end[];
extern const u8 bob64_embedded_source[];
extern const u8 bob64_embedded_source_end[];

typedef struct {
    BOB64_PAGE_TABLE *PageTable;
} KERNEL_PROCESS_CONTEXT;

static u64 read_cr3(void);

static u64 process_allocate_page(void *context) {
    (void)context;
    return bob64_page_alloc(&bob64_boot_page_allocator,1);
}

static int process_free_page(void *context,u64 physical_address) {
    (void)context;
    return bob64_page_free(&bob64_boot_page_allocator,physical_address,1);
}

static int process_map_page(void *context,u64 virtual_address,u64 physical_address,
                            u64 flags,u8 **writable_address) {
    KERNEL_PROCESS_CONTEXT *process=(KERNEL_PROCESS_CONTEXT *)context;
    if(!process||!process->PageTable||!writable_address||
       bob64_page_map(process->PageTable,
       virtual_address,physical_address,flags))return -1;
    __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)virtual_address):"memory");
    *writable_address=(u8 *)(uintptr_t)virtual_address;
    return 0;
}

static int process_protect_page(void *context,u64 virtual_address,u64 flags) {
    KERNEL_PROCESS_CONTEXT *process=(KERNEL_PROCESS_CONTEXT *)context;
    if(!process||!process->PageTable||
       bob64_page_protect(process->PageTable,virtual_address,flags))return -1;
    __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)virtual_address):"memory");
    return 0;
}

static int process_unmap_page(void *context,u64 virtual_address) {
    KERNEL_PROCESS_CONTEXT *process=(KERNEL_PROCESS_CONTEXT *)context;
    if(!process||!process->PageTable||
       bob64_page_unmap(process->PageTable,virtual_address,0,0))return -1;
    __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)virtual_address):"memory");
    return 0;
}

static void kernel_write16(u8 *bytes,u16 value) {
    bytes[0]=(u8)value;bytes[1]=(u8)(value>>8);
}

static void kernel_write32(u8 *bytes,u32 value) {
    for(u32 i=0;i<4;i++)bytes[i]=(u8)(value>>(i*8));
}

static void kernel_write64(u8 *bytes,u64 value) {
    kernel_write32(bytes,(u32)value);kernel_write32(bytes+4,(u32)(value>>32));
}

static usize kernel_emit_test_syscall(u8 *code,usize offset,u32 number,u32 argument) {
    code[offset++]=0xb8;kernel_write32(code+offset,number);offset+=4;
    code[offset++]=0xb9;kernel_write32(code+offset,argument);offset+=4;
    code[offset++]=0xcd;code[offset++]=0x80;
    return offset;
}

static usize kernel_emit_test_buffer_syscall(u8 *code,usize offset,
                                             const char *text,usize length) {
    usize instruction_start=offset,lea_end,buffer_offset;
    code[offset++]=0xb8;kernel_write32(code+offset,BOB64_SYSCALL_WRITE_BUFFER);offset+=4;
    code[offset++]=0x48;code[offset++]=0x8d;code[offset++]=0x0d;
    lea_end=offset+4;
    buffer_offset=instruction_start+24;
    kernel_write32(code+offset,(u32)(buffer_offset-lea_end));offset+=4;
    code[offset++]=0xba;kernel_write32(code+offset,(u32)length);offset+=4;
    code[offset++]=0xcd;code[offset++]=0x80;
    code[offset++]=0xe9;
    kernel_write32(code+offset,(u32)length);offset+=4;
    for(usize i=0;i<length;i++)code[offset++]=(u8)text[i];
    return offset;
}

static u32 kernel_crc32(const u8 *bytes,usize length) {
    u32 value=0xffffffffu;
    for(usize i=0;i<length;i++) {
        value^=bytes[i];
        for(u32 bit=0;bit<8;bit++)value=(value>>1)^(0xedb88320u&-(value&1u));
    }
    return ~value;
}

static int kernel_bytes_equal(const char *left,const char *right) {
    while(*left&&*left==*right){left++;right++;}
    return *left==*right;
}

static void kernel_build_bob_application(u8 *executable) {
    static const char text[]="bob!";
    usize code_size=0;
    for(usize i=0;i<BOB64_EXEC_HEADER_SIZE+BOB64_PAGE_SIZE;i++)executable[i]=0;
    executable[0]='B';executable[1]='6';executable[2]='4';executable[3]='E';
    kernel_write16(executable+4,BOB64_EXEC_VERSION);
    kernel_write16(executable+6,BOB64_EXEC_HEADER_SIZE);
    kernel_write32(executable+8,BOB64_APP_ABI_VERSION);
    kernel_write64(executable+16,BOB64_PAGE_SIZE);
    kernel_write64(executable+24,2*BOB64_PAGE_SIZE);
    kernel_write64(executable+32,0);
    kernel_write64(executable+40,BOB64_PAGE_SIZE);
    for(usize i=0;i<BOB64_PAGE_SIZE;i++)
        executable[BOB64_EXEC_HEADER_SIZE+i]=0x90;
    code_size=kernel_emit_test_syscall(executable+BOB64_EXEC_HEADER_SIZE,
        code_size,BOB64_SYSCALL_QUERY_ABI,0);
    code_size=kernel_emit_test_buffer_syscall(executable+BOB64_EXEC_HEADER_SIZE,
        code_size,text,sizeof(text)-1);
    (void)kernel_emit_test_syscall(executable+BOB64_EXEC_HEADER_SIZE,
        code_size,BOB64_SYSCALL_EXIT,0);
    kernel_write32(executable+48,kernel_crc32(executable+BOB64_EXEC_HEADER_SIZE,
                                               (usize)BOB64_PAGE_SIZE));
}

static void kernel_build_code_write_fault_application(u8 *executable) {
    static const char text[]="bob!";
    u8 *code=executable+BOB64_EXEC_HEADER_SIZE;
    usize offset;
    kernel_build_bob_application(executable);
    offset=kernel_emit_test_buffer_syscall(code,0,text,sizeof(text)-1);
    code[offset++]=0xc6;code[offset++]=0x05;
    kernel_write32(code+offset,(u32)(0-(offset+5)));offset+=4;
    code[offset++]=0x90;
    (void)offset;
    kernel_write32(executable+48,kernel_crc32(executable+BOB64_EXEC_HEADER_SIZE,
                                               (usize)BOB64_PAGE_SIZE));
}

static void kernel_build_nx_fault_application(u8 *executable) {
    u8 *code=executable+BOB64_EXEC_HEADER_SIZE;
    usize offset;
    kernel_build_bob_application(executable);
    offset=kernel_emit_test_buffer_syscall(code,0,"bob!",4);
    code[offset++]=0xe9;
    kernel_write32(code+offset,(u32)((u64)BOB64_PAGE_SIZE-(offset+4)));offset+=4;
    executable[BOB64_EXEC_HEADER_SIZE+BOB64_PAGE_SIZE]=0x0f;
    executable[BOB64_EXEC_HEADER_SIZE+BOB64_PAGE_SIZE+1]=0x0b;
    kernel_write64(executable+16,2*BOB64_PAGE_SIZE);
    kernel_write64(executable+24,2*BOB64_PAGE_SIZE);
    kernel_write32(executable+48,kernel_crc32(executable+BOB64_EXEC_HEADER_SIZE,
                                               (usize)(2*BOB64_PAGE_SIZE)));
}

static int kernel_install_bob_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_app_end-bob64_embedded_app);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"bob.b64e",bob64_embedded_app,length);
}

static int kernel_install_display_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_display_end-bob64_embedded_display);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"display.b64e",bob64_embedded_display,length);
}

static int kernel_install_gui_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_gui_end-bob64_embedded_gui);
    if(!length)return -1;
    return bob64_fs_write(filesystem,"desktop.b64e",bob64_embedded_gui,length);
}

static int kernel_install_nested_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_nested_app_end-
                         bob64_embedded_nested_app);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"nested.b64e",bob64_embedded_nested_app,
                          length);
}

static int kernel_install_echo_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_echo_end-bob64_embedded_echo);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"echo.b64e",bob64_embedded_echo,length);
}

static int kernel_install_ls_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_ls_end-bob64_embedded_ls);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"ls.b64e",bob64_embedded_ls,length);
}

static int kernel_install_cat_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_cat_end-bob64_embedded_cat);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"cat.b64e",bob64_embedded_cat,length);
}

static int kernel_install_notes_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_notes_end-bob64_embedded_notes);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"notes.b64e",bob64_embedded_notes,length);
}

static int kernel_install_info_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_info_end-bob64_embedded_info);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"info.b64e",bob64_embedded_info,length);
}

static int kernel_install_mouse_smoke_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_mouse_smoke_end-
                         bob64_embedded_mouse_smoke);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"mousesmoke.b64e",
                          bob64_embedded_mouse_smoke,length);
}

static int kernel_install_launcher_application(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_launcher_end-
                         bob64_embedded_launcher);
    if(length<BOB64_EXEC_HEADER_SIZE)return -1;
    return bob64_fs_write(filesystem,"launcher.b64e",bob64_embedded_launcher,
                          length);
}

static int kernel_install_bob_source(BOB64_FILESYSTEM *filesystem) {
    usize length=(usize)(bob64_embedded_source_end-bob64_embedded_source);
    return bob64_fs_write(filesystem,"bob.c",bob64_embedded_source,length);
}

static int kernel_process_smoke_test(const BOB64_KERNEL_BOOT_INFO *info) {
    static const char *arguments[]={"bob!"};
    static u8 executable[BOB64_EXEC_HEADER_SIZE+BOB64_PAGE_SIZE];
    static u8 nx_executable[BOB64_EXEC_HEADER_SIZE+2*BOB64_PAGE_SIZE];
    BOB64_PAGE_TABLE process_table;
    KERNEL_PROCESS_CONTEXT process_context={&process_table};
    BOB64_PROCESS_OPERATIONS operations={process_allocate_page,process_free_page,
        process_map_page,process_protect_page,process_unmap_page,&process_context,0};
    BOB64_PROCESS process;
    BOB64_PROCESS_PAGE *pages;
    BOB64_APP_STARTUP *startup;
    u64 physical,flags,kernel_root,table_checkpoint;
    usize allocated_before,heap_checkpoint;
    if(!info->NxSupported)return 1;
    pages=(BOB64_PROCESS_PAGE *)bob64_heap_calloc(&kernel_heap,
          (usize)BOB64_PROCESS_PAGE_LIMIT,sizeof(*pages));
    if(!pages)return -1;
    kernel_root=read_cr3();
    table_checkpoint=bob64_bootstrap_space.TablePoolUsed;
    heap_checkpoint=kernel_heap_high_water;
    allocated_before=bob64_boot_page_allocator.AllocatedCount;
    operations.NxSupported=info->NxSupported;
    if(bob64_process_init(&process,pages,(usize)BOB64_PROCESS_PAGE_LIMIT,&operations)) {
        bob64_heap_free(&kernel_heap,pages);return -1;
    }
    if(bob64_page_table_init(&process_table,info->PhysicalAddressBits,
       bob64_bootstrap_space.PageTable.Allocate,bob64_bootstrap_space.PageTable.Access,
       bob64_bootstrap_space.PageTable.Context)||
       bob64_page_table_clone_isolated(&process_table,&bob64_bootstrap_space.PageTable,
       BOB64_PROCESS_IMAGE_BASE,BOB64_PROCESS_ISOLATED_SIZE)) {
        if(kernel_heap_high_water==heap_checkpoint)
            bob64_bootstrap_space.TablePoolUsed=table_checkpoint;
        bob64_heap_free(&kernel_heap,pages);return -1;
    }
    __asm__ volatile("mov %0,%%cr3"::"r"(process_table.RootPhysical):"memory");
    kernel_build_bob_application(executable);
    int result=bob64_process_load(&process,executable,sizeof(executable),1,arguments);
    if(result) {
        bob64_process_unload(&process);
        __asm__ volatile("mov %0,%%cr3"::"r"(kernel_root):"memory");
        if(kernel_heap_high_water==heap_checkpoint)
            bob64_bootstrap_space.TablePoolUsed=table_checkpoint;
        bob64_heap_free(&kernel_heap,pages);return -1;
    }
    startup=(BOB64_APP_STARTUP *)(uintptr_t)process.StartupAddress;
    result=process.EntryAddress==BOB64_PROCESS_IMAGE_BASE&&
           startup->AbiVersion==BOB64_APP_ABI_VERSION&&startup->ArgumentCount==1&&
           startup->Arguments&&kernel_bytes_equal(startup->Arguments[0],"bob!")&&
           bob64_page_translate(&process_table,
             BOB64_PROCESS_IMAGE_BASE,&physical,&flags)==1&&physical&&
           (flags&BOB64_PAGE_USER)&&!(flags&BOB64_PAGE_WRITE)&&!(flags&BOB64_PAGE_NX)&&
           bob64_page_translate(&process_table,
             BOB64_PROCESS_IMAGE_BASE+BOB64_PAGE_SIZE,&physical,&flags)==1&&
           (flags&BOB64_PAGE_USER)&&(flags&BOB64_PAGE_WRITE)&&(flags&BOB64_PAGE_NX)&&
           bob64_page_translate(&process_table,
             BOB64_PROCESS_STACK_GUARD,&physical,&flags)==0;
    if(result) {
        kernel_user_output_length=0;
        kernel_user_output_truncated=0;
        bob64_syscall_set_address_space(&process_table);
        bob64_syscall_set_read_user(kernel_copy_user_bytes,0);
        bob64_syscall_set_write(kernel_user_write_character,0);
        bob64_syscall_set_wait_event(kernel_wait_for_input_event,0);
        s64 app_status=bob64_enter_user(process.EntryAddress,process.StartupAddress,
                                         process.InitialStackPointer);
        bob64_syscall_set_wait_event(0,0);
        bob64_syscall_set_write(0,0);
        bob64_syscall_set_read_user(0,0);
        bob64_syscall_set_address_space(0);
        kernel_user_output[kernel_user_output_length]=0;
        if(app_status||kernel_user_output_truncated||
           kernel_user_output_length!=4||
           !kernel_bytes_equal(kernel_user_output,"bob!"))result=0;
        else kernel_write("\r\n");
    }
    int unload_result=bob64_process_unload(&process);
    if(unload_result||process.PageCount||process.Loaded||
       bob64_boot_page_allocator.AllocatedCount!=allocated_before)result=-1;
    else result=result?0:-1;
    if(!result) {
        kernel_build_code_write_fault_application(executable);
        if(kernel_process_fault_smoke_test(&process,&process_table,&operations,
           executable,sizeof(executable),BOB64_PROCESS_IMAGE_BASE+28,0x7,0x7,
           "bob64 kernel: read-only code protection passed\r\n"))result=-1;
    }
    if(!result) {
        kernel_build_nx_fault_application(nx_executable);
        if(kernel_process_fault_smoke_test(&process,&process_table,&operations,
           nx_executable,sizeof(nx_executable),
           BOB64_PROCESS_IMAGE_BASE+BOB64_PAGE_SIZE,0x15,0x15,
           "bob64 kernel: non-executable data protection passed\r\n"))result=-1;
    }
    __asm__ volatile("mov %0,%%cr3"::"r"(kernel_root):"memory");
    if(kernel_heap_high_water==heap_checkpoint)
        bob64_bootstrap_space.TablePoolUsed=table_checkpoint;
    if(bob64_boot_page_allocator.AllocatedCount!=allocated_before)result=-1;
    if(bob64_heap_free(&kernel_heap,pages))result=-1;
    return result;
}

static u64 read_cr3(void) {
    u64 value;
    __asm__ volatile("mov %%cr3,%0":"=r"(value));
    return value&BOB64_PAGE_ADDRESS_MASK;
}

static __attribute__((noreturn)) void kernel_halt(void) {
    __asm__ volatile("cli");
    for(;;)__asm__ volatile("hlt");
}

static void kernel_write(const char *text) {
    bob64_early_console_write(text);
    bob64_framebuffer_write(text);
}

static void kernel_user_write_character(void *context,u8 character) {
    char text[2]={(char)character,0};
    (void)context;
    if(kernel_user_output_length+1<sizeof(kernel_user_output))
        kernel_user_output[kernel_user_output_length++]=(char)character;
    else kernel_user_output_truncated=1;
    kernel_write(text);
}

static int kernel_copy_user_bytes(void *context,u64 address,void *destination,
                                  usize length) {
    const volatile u8 *source=(const volatile u8 *)(uintptr_t)address;
    u8 *target=(u8 *)destination;
    (void)context;
    if((!source&&length)||(!target&&length)||
       length>BOB64_SYSCALL_COPY_CHUNK_BYTES)
        return -1;
    for(usize i=0;i<length;i++)target[i]=source[i];
    return 0;
}

static int kernel_process_fault_smoke_test(BOB64_PROCESS *process,
    BOB64_PAGE_TABLE *process_table,const BOB64_PROCESS_OPERATIONS *operations,
    const u8 *executable,usize executable_size,u64 expected_rip,u64 error_mask,
    u64 error_value,const char *success_message) {
    static const char *arguments[]={"bob!"};
    s64 status;
    int passed;
    if(bob64_process_init(process,process->Pages,process->PageCapacity,operations)||
       bob64_process_load(process,executable,executable_size,1,arguments)) {
        (void)bob64_process_unload(process);
        return -1;
    }
    kernel_user_output_length=0;
    kernel_user_output_truncated=0;
    bob64_user_exception_vector=~(u64)0;
    bob64_user_exception_error=~(u64)0;
    bob64_user_exception_rip=~(u64)0;
    bob64_user_expected_exception_vector=14;
    bob64_user_expected_exception_error_mask=error_mask;
    bob64_user_expected_exception_error_value=error_value;
    bob64_syscall_set_address_space(process_table);
    bob64_syscall_set_read_user(kernel_copy_user_bytes,0);
    bob64_syscall_set_write(kernel_user_write_character,0);
    status=bob64_enter_user(process->EntryAddress,process->StartupAddress,
                            process->InitialStackPointer);
    bob64_user_expected_exception_vector=~(u64)0;
    bob64_syscall_set_write(0,0);
    bob64_syscall_set_read_user(0,0);
    bob64_syscall_set_address_space(0);
    kernel_user_output[kernel_user_output_length]=0;
    passed=status==-142&&bob64_user_exception_vector==14&&
        bob64_user_exception_rip==expected_rip&&
        (bob64_user_exception_error&error_mask)==error_value&&
        !kernel_user_output_truncated&&kernel_user_output_length==4&&
        kernel_bytes_equal(kernel_user_output,"bob!");
    if(bob64_process_unload(process))return -1;
    if(!passed)return -1;
    kernel_write(success_message);
    return 0;
}

static int kernel_wait_for_input_event(void *context,BOB64_EVENT *event) {
    (void)context;
    if(!event)return -1;
    /* int 0x80 enters through an interrupt gate, which clears IF. */
    __asm__ volatile("sti":::"memory");
    for(;;) {
        if(!bob64_mouse_poll_event(event)||!bob64_keyboard_poll_event(event))break;
        __asm__ volatile("pause");
    }
    /* Keep the syscall handler's state until iretq restores the user flags. */
    __asm__ volatile("cli":::"memory");
    return 0;
}

static int kernel_write_user_bytes(void *context,u64 address,const void *source,
                                   usize length) {
    volatile u8 *target=(volatile u8 *)(uintptr_t)address;
    const u8 *bytes=(const u8 *)source;
    (void)context;
    if((!target&&length)||(!bytes&&length)||length>BOB64_SYSCALL_MAX_BUFFER)
        return -1;
    for(usize i=0;i<length;i++)target[i]=bytes[i];
    return 0;
}

static s64 kernel_app_read_file(void *context,const char *name,u8 *buffer,
                                usize capacity) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    const char *data;
    usize length;
    if(!app||!app->Filesystem||!name||(!buffer&&capacity))return -22;
    if(bob64_fs_read(app->Filesystem,name,&data,&length))return -2;
    if(length>capacity)return -28;
    for(usize i=0;i<length;i++)buffer[i]=(u8)data[i];
    return (s64)length;
}

static s64 kernel_app_write_file(void *context,const char *name,const u8 *buffer,
                                 usize length) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    if(!app||!app->Filesystem||!name||(!buffer&&length))return -22;
    if(bob64_fs_write(app->Filesystem,name,buffer,length))return -28;
    return (s64)length;
}

static s64 kernel_app_open_stream(void *context,const char *name,u32 flags) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    const char *data;usize length;
    if(!app||!app->Filesystem||!name)return -22;
    if(bob64_fs_read(app->Filesystem,name,&data,&length)) {
        if(!(flags&BOB64_FILE_OPEN_CREATE))return -2;
        if(bob64_fs_write(app->Filesystem,name,0,0))return -28;
        length=0;
    } else if(flags&BOB64_FILE_OPEN_TRUNCATE) {
        if(bob64_fs_write(app->Filesystem,name,0,0))return -28;
        length=0;
    }
    if(length>0x7fffffffffffffffULL)return -75;
    return (s64)length;
}

static s64 kernel_app_read_stream(void *context,const char *name,u64 offset,
                                  u8 *buffer,usize capacity) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    usize count=0;
    if(!app||!app->Filesystem||!name||(!buffer&&capacity))return -22;
    if(bob64_fs_read_at(app->Filesystem,name,offset,buffer,capacity,&count))return -2;
    return (s64)count;
}

static s64 kernel_app_write_stream(void *context,const char *name,u64 offset,
                                   const u8 *buffer,usize length) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    if(!app||!app->Filesystem||!name||(!buffer&&length))return -22;
    if(bob64_fs_write_at(app->Filesystem,name,offset,buffer,length))return -28;
    return (s64)length;
}

typedef struct {
    BOB64_FILE_INFO *Entries;
    usize Capacity,Count;
    int Full;
} KERNEL_FILE_LIST_CONTEXT;

static int kernel_app_list_file(void *context,const char *name,usize size) {
    KERNEL_FILE_LIST_CONTEXT *list=(KERNEL_FILE_LIST_CONTEXT *)context;
    usize length=0;
    BOB64_FILE_INFO *entry;
    while(name[length]&&length<sizeof(list->Entries[0].Name)-1)length++;
    if(name[length]||list->Count>=list->Capacity) {
        list->Full=1;return 1;
    }
    entry=&list->Entries[list->Count++];
    for(usize i=0;i<sizeof(*entry);i++)((u8 *)entry)[i]=0;
    for(usize i=0;i<length;i++)entry->Name[i]=name[i];
    entry->Size=(u64)size;
    return 0;
}

static s64 kernel_app_list_files(void *context,BOB64_FILE_INFO *entries,
                                 usize capacity) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    KERNEL_FILE_LIST_CONTEXT list={entries,capacity,0,0};
    if(!app||!app->Filesystem||!entries||!capacity)return -22;
    bob64_fs_list(app->Filesystem,kernel_app_list_file,&list);
    return list.Full?-28:(s64)list.Count;
}

static s64 kernel_app_delete_file(void *context,const char *name) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    if(!app||!app->Filesystem||!name)return -22;
    return bob64_fs_delete(app->Filesystem,name)?-2:0;
}

static s64 kernel_run_application(void *context,const char *name,usize argument_count,
                                  const char *const *arguments,int *started) {
    BOB64_PAGE_TABLE process_table;
    KERNEL_PROCESS_CONTEXT process_context={&process_table};
    BOB64_PROCESS_OPERATIONS operations;
    BOB64_PROCESS process;
    BOB64_PROCESS_PAGE *pages;
    const char *image;
    usize image_size;
    u64 kernel_root,table_checkpoint;
    usize heap_checkpoint;
    s64 status=-1;
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    void *application_privilege_stack=0;
    BOB64_WINDOW_SERVER *window_server=0;
    u64 previous_privilege_stack=bob64_kernel_tss.Rsp0;
    int entered=0,cleanup_error=0,syscall_context_saved=0;
    int window_server_ready=0;
    u64 window_owner=0;
    if(started)*started=0;
    if(!app||!app->Filesystem||!app->NxSupported||!started||
       !app->PhysicalAddressBits||
       bob64_user_depth>=BOB64_USER_ENTRY_MAX_DEPTH||
       bob64_fs_read(app->Filesystem,name,&image,&image_size))return -1;
    operations=(BOB64_PROCESS_OPERATIONS){process_allocate_page,process_free_page,
        process_map_page,process_protect_page,process_unmap_page,&process_context,
        app->NxSupported};
    pages=(BOB64_PROCESS_PAGE *)bob64_heap_calloc(&kernel_heap,
          (usize)BOB64_PROCESS_PAGE_LIMIT,sizeof(*pages));
    if(!pages)return -1;
    kernel_root=read_cr3();
    table_checkpoint=bob64_bootstrap_space.TablePoolUsed;
    heap_checkpoint=kernel_heap_high_water;
    if(bob64_process_init(&process,pages,(usize)BOB64_PROCESS_PAGE_LIMIT,&operations))
        goto done;
    if(bob64_page_table_init(&process_table,app->PhysicalAddressBits,
       bob64_bootstrap_space.PageTable.Allocate,bob64_bootstrap_space.PageTable.Access,
       bob64_bootstrap_space.PageTable.Context))goto done;
    if(bob64_page_table_clone_isolated(&process_table,&bob64_bootstrap_space.PageTable,
       BOB64_PROCESS_IMAGE_BASE,BOB64_PROCESS_ISOLATED_SIZE))goto done;
    __asm__ volatile("mov %0,%%cr3"::"r"(process_table.RootPhysical):"memory");
    if(bob64_process_load(&process,image,image_size,argument_count,arguments))goto restore;
    application_privilege_stack=bob64_heap_alloc(&kernel_heap,
        (usize)BOB64_KERNEL_PRIVILEGE_STACK_SIZE);
    if(!application_privilege_stack) {
        if(bob64_process_unload(&process))cleanup_error=1;
        goto restore;
    }
    for(usize i=0;i<(usize)BOB64_KERNEL_PRIVILEGE_STACK_SIZE;i++)
        ((u8 *)application_privilege_stack)[i]=0;
    u64 application_stack_top=(u64)(uintptr_t)application_privilege_stack+
                               BOB64_KERNEL_PRIVILEGE_STACK_SIZE;
    if(bob64_tss_set_rsp0(&bob64_kernel_tss,application_stack_top)||
       bob64_kernel_tss.Rsp0!=application_stack_top) {
        if(bob64_process_unload(&process))cleanup_error=1;
        goto restore;
    }
    if(bob64_syscall_context_push()) {
        if(bob64_process_unload(&process))cleanup_error=1;
        goto restore;
    }
    syscall_context_saved=1;
    kernel_user_output_length=0;
    kernel_user_output_truncated=0;
    u32 display_width,display_height;
    if(!bob64_framebuffer_resolution(&display_width,&display_height)) {
        window_owner=kernel_next_window_owner++;
        if(!window_owner)window_owner=kernel_next_window_owner++;
        window_server=(BOB64_WINDOW_SERVER *)bob64_heap_calloc(&kernel_heap,1,
                                                               sizeof(*window_server));
        if(window_server&&!bob64_window_server_init(window_server,&kernel_heap,
              window_owner,display_width,display_height))window_server_ready=1;
        else if(window_server) {
            bob64_window_server_close(window_server);
            if(bob64_heap_free(&kernel_heap,window_server))cleanup_error=1;
            window_server=0;
        }
    }
    bob64_syscall_set_address_space(&process_table);
    bob64_syscall_set_read_user(kernel_copy_user_bytes,0);
    bob64_syscall_set_write_user(kernel_write_user_bytes,0);
    bob64_syscall_set_filesystem(kernel_app_read_file,kernel_app_write_file,app);
    bob64_syscall_set_file_stream(kernel_app_open_stream,kernel_app_read_stream,
                                  kernel_app_write_stream,app);
    bob64_syscall_set_file_manager(kernel_app_list_files,kernel_app_delete_file,app);
    bob64_syscall_set_app_runner(kernel_app_run_application,app);
    bob64_syscall_set_write(kernel_user_write_character,0);
    bob64_syscall_set_wait_event(kernel_wait_for_input_event,0);
    bob64_syscall_set_window_server(window_server_ready?window_server:0,
                                    window_owner);
    *started=1;entered=1;
    status=bob64_enter_user(process.EntryAddress,process.StartupAddress,
                             process.InitialStackPointer);
    kernel_user_output[kernel_user_output_length]=0;
    if(bob64_tss_set_rsp0(&bob64_kernel_tss,previous_privilege_stack)||
       bob64_kernel_tss.Rsp0!=previous_privilege_stack)cleanup_error=1;
    if(bob64_heap_free(&kernel_heap,application_privilege_stack))cleanup_error=1;
    application_privilege_stack=0;
    if(window_server_ready)bob64_window_server_close(window_server);
    if(window_server&&bob64_heap_free(&kernel_heap,window_server))cleanup_error=1;
    if(syscall_context_saved&&bob64_syscall_context_pop())cleanup_error=1;
    kernel_write("\r\n");
    if(bob64_process_unload(&process))cleanup_error=1;
restore:
    if(application_privilege_stack) {
        if(bob64_tss_set_rsp0(&bob64_kernel_tss,previous_privilege_stack))
            cleanup_error=1;
        if(bob64_heap_free(&kernel_heap,application_privilege_stack))cleanup_error=1;
        application_privilege_stack=0;
    }
    __asm__ volatile("mov %0,%%cr3"::"r"(kernel_root):"memory");
done:
    /* A heap peak can leave shared page-table levels mapped after the heap
       later shrinks. Preserve those table pages across this address space. */
    if(kernel_heap_high_water==heap_checkpoint)
        bob64_bootstrap_space.TablePoolUsed=table_checkpoint;
    if(bob64_heap_free(&kernel_heap,pages)||cleanup_error)status=-1;
    if(!entered)*started=0;
    return status;
}

static int kernel_app_run_application(void *context,const char *name,
                                      usize argument_count,
                                      const char *const *arguments,
                                      s64 *exit_status) {
    int started=0;
    s64 status;
    if(!exit_status)return -22;
    status=kernel_run_application(context,name,argument_count,arguments,&started);
    if(!started)return -2;
    *exit_status=status;
    return 0;
}

static int kernel_compile_application(void *context,const char *source_name,
                                      const char *output_name,usize *error_offset) {
    KERNEL_APP_CONTEXT *app=(KERNEL_APP_CONTEXT *)context;
    const char *source;
    usize source_length,output_length;
    int result;
    if(!app||!app->Filesystem||!source_name||!output_name||
       bob64_fs_read(app->Filesystem,source_name,&source,&source_length))return -1;
    result=bob64_compile_c(source,source_length,kernel_compiler_output,
                           sizeof(kernel_compiler_output),&output_length,error_offset);
    if(result)return result;
    if(bob64_fs_write(app->Filesystem,output_name,kernel_compiler_output,output_length))
        return -1;
    return 0;
}

static void kernel_hex64(u64 value) {
    static const char digits[]="0123456789abcdef";
    char text[17];
    for(u32 i=0;i<16;i++)text[i]=digits[(value>>(60-i*4))&15];
    text[16]=0;
    kernel_write(text);
}

static int kernel_heap_grow(void *context,void **region,usize *region_size) {
    const BOB64_KERNEL_BOOT_INFO *info=(const BOB64_KERNEL_BOOT_INFO *)context;
    u64 physical,virtual_address;
    int map_result;
    if(!info||!region||!region_size||kernel_heap_mapped>=BOB64_KERNEL_HEAP_LIMIT)
        return 0;
    physical=bob64_page_alloc(&bob64_boot_page_allocator,1);
    if(!physical) {
        kernel_write("bob64 kernel: heap physical page allocation failed\r\n");
        return 0;
    }
    virtual_address=BOB64_KERNEL_HEAP_BASE+kernel_heap_mapped;
    map_result=bob64_page_map(&bob64_bootstrap_space.PageTable,virtual_address,
       physical,BOB64_PAGE_WRITE|(info->NxSupported?BOB64_PAGE_NX:0));
    if(map_result) {
        kernel_write("bob64 kernel: heap page-table mapping failed (result=0x");
        kernel_hex64((u64)(s64)map_result);
        kernel_write(" tables=0x");kernel_hex64(bob64_bootstrap_space.TablePoolUsed);
        kernel_write("/0x");kernel_hex64(bob64_bootstrap_space.TablePoolPages);
        kernel_write(")\r\n");
        bob64_page_free(&bob64_boot_page_allocator,physical,1);
        return 0;
    }
    __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)virtual_address):"memory");
    volatile u64 *words=(volatile u64 *)(uintptr_t)virtual_address;
    for(usize i=0;i<EFI_PAGE_SIZE/sizeof(u64);i++)words[i]=0;
    kernel_heap_mapped+=(usize)EFI_PAGE_SIZE;
    if(kernel_heap_mapped>kernel_heap_high_water)
        kernel_heap_high_water=kernel_heap_mapped;
    *region=(void *)(uintptr_t)virtual_address;
    *region_size=(usize)EFI_PAGE_SIZE;
    return 1;
}

static int kernel_heap_shrink(void *context,void *region,usize region_size) {
    const BOB64_KERNEL_BOOT_INFO *info=(const BOB64_KERNEL_BOOT_INFO *)context;
    u64 expected;
    if(!info||!region||!region_size||
       (region_size&(usize)(EFI_PAGE_SIZE-1))||
       region_size>kernel_heap_mapped||
       region_size>~(u64)0-(u64)(uintptr_t)BOB64_KERNEL_HEAP_BASE)
        return -1;
    expected=BOB64_KERNEL_HEAP_BASE+kernel_heap_mapped-region_size;
    if((u64)(uintptr_t)region!=expected)return -1;
    for(usize offset=0;offset<region_size;offset+=(usize)EFI_PAGE_SIZE) {
        u64 translated,flags;
        if(bob64_page_translate(&bob64_bootstrap_space.PageTable,
             expected+offset,&translated,&flags)!=1||
           (translated&(EFI_PAGE_SIZE-1))||
           (flags&(BOB64_PAGE_PRESENT|BOB64_PAGE_WRITE))!=
             (BOB64_PAGE_PRESENT|BOB64_PAGE_WRITE))return -1;
    }
    for(usize offset=0;offset<region_size;offset+=(usize)EFI_PAGE_SIZE) {
        u64 unmapped_physical;
        if(bob64_page_unmap(&bob64_bootstrap_space.PageTable,expected+offset,
                            &unmapped_physical,0))return -1;
        __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)(expected+offset)):
                         "memory");
        if(bob64_page_free(&bob64_boot_page_allocator,unmapped_physical,1))
            return -1;
    }
    kernel_heap_mapped-=region_size;
    return 0;
}

static int kernel_heap_smoke_test(const BOB64_KERNEL_BOOT_INFO *info) {
    char *first,*large,*zero;
    if(bob64_heap_init(&kernel_heap,(usize)BOB64_KERNEL_HEAP_LIMIT,
                       kernel_heap_grow,(void *)info))return -1;
    bob64_heap_set_shrink(&kernel_heap,kernel_heap_shrink);
    first=(char *)bob64_heap_alloc(&kernel_heap,64);
    large=(char *)bob64_heap_alloc(&kernel_heap,5000);
    zero=(char *)bob64_heap_calloc(&kernel_heap,32,2);
    if(!first||!large||!zero||((uintptr_t)first&15)||((uintptr_t)large&15)||
       ((uintptr_t)zero&15)||!((uintptr_t)first>=BOB64_KERNEL_HEAP_BASE))return -1;
    first[0]='b';first[1]='o';first[2]='b';first[3]=0;
    large[0]='6';large[1]='4';large[4999]='!';
    for(usize i=0;i<64;i++)if(zero[i])return -1;
    if(first[0]!='b'||first[1]!='o'||first[2]!='b'||large[0]!='6'||
       large[1]!='4'||large[4999]!='!')return -1;
    if(bob64_heap_free(&kernel_heap,large)||bob64_heap_free(&kernel_heap,first)||
       bob64_heap_free(&kernel_heap,zero)||bob64_heap_free(&kernel_heap,zero)!=-1)
        return -1;
    if(bob64_heap_mapped_bytes(&kernel_heap)!=EFI_PAGE_SIZE||
       kernel_heap_mapped!=EFI_PAGE_SIZE)return -1;
    return 0;
}

static int kernel_table_pool_smoke_test(const BOB64_KERNEL_BOOT_INFO *info) {
    BOB64_PAGE_TABLE table;
    u64 checkpoint,initial_pages,physical;
    if(!info||!bob64_bootstrap_space.TablePoolPages)return -1;
    checkpoint=bob64_bootstrap_space.TablePoolUsed;
    initial_pages=bob64_bootstrap_space.TablePoolPages;
    if(bob64_page_table_init(&table,info->PhysicalAddressBits,
       bob64_bootstrap_space.PageTable.Allocate,
       bob64_bootstrap_space.PageTable.Access,
       bob64_bootstrap_space.PageTable.Context))return -1;
    for(u64 i=0;i<140;i++) {
        u64 virtual_address=0x0000001000000000ULL+i*(2ULL<<20);
        u64 physical_address=0x0000000010000000ULL+i*BOB64_PAGE_SIZE;
        if(bob64_page_map(&table,virtual_address,physical_address,
                          BOB64_PAGE_WRITE|BOB64_PAGE_NX)) {
            bob64_bootstrap_space.TablePoolUsed=checkpoint;
            return -1;
        }
    }
    int valid=bob64_bootstrap_space.TablePoolPages>initial_pages&&
        bob64_bootstrap_space.TablePoolUsed>checkpoint&&
        bob64_page_translate(&table,0x0000001000000000ULL+139*(2ULL<<20),
                             &physical,0)==1&&
        physical==0x0000000010000000ULL+139*BOB64_PAGE_SIZE;
    bob64_bootstrap_space.TablePoolUsed=checkpoint;
    return valid?0:-1;
}

static int kernel_privilege_stack_init(void) {
    kernel_privilege_stack=bob64_heap_alloc(&kernel_heap,
                                            (usize)BOB64_KERNEL_PRIVILEGE_STACK_SIZE);
    if(!kernel_privilege_stack)return -1;
    for(usize i=0;i<(usize)BOB64_KERNEL_PRIVILEGE_STACK_SIZE;i++)
        ((u8 *)kernel_privilege_stack)[i]=0;
    return bob64_tss_set_rsp0(&bob64_kernel_tss,
        (u64)(uintptr_t)kernel_privilege_stack+
        BOB64_KERNEL_PRIVILEGE_STACK_SIZE);
}

static void shell_write(void *context,const char *text) {
    (void)context;
    kernel_write(text);
}

static void shell_clear(void *context) {
    (void)context;
    bob64_framebuffer_clear();
    bob64_early_console_write("\x1b[2J\x1b[H");
}

static EFI_RUNTIME_SERVICES *kernel_runtime_services(
        const BOB64_KERNEL_BOOT_INFO *info) {
    return info&&info->RuntimeServices?
        (EFI_RUNTIME_SERVICES *)(uintptr_t)info->RuntimeServices:0;
}

static int kernel_snapshot_save(void *context,const void *snapshot,
                                usize snapshot_size) {
    const BOB64_KERNEL_BOOT_INFO *info=(const BOB64_KERNEL_BOOT_INFO *)context;
    EFI_STATUS firmware_status=EFI_SUCCESS;
    UINTN maximum_variable_size=0,remaining_storage_size=0;
    int result=bob64_firmware_snapshot_save(kernel_runtime_services(info),
        &kernel_heap,snapshot,snapshot_size,&firmware_status,
        &maximum_variable_size,&remaining_storage_size);
    if(result) {
        kernel_write("firmware snapshot save failed (EFI 0x");
        kernel_hex64(firmware_status);kernel_write(" max=0x");
        kernel_hex64(maximum_variable_size);kernel_write(" free=0x");
        kernel_hex64(remaining_storage_size);kernel_write(")\r\n");
    }
    return result;
}

static int kernel_snapshot_restore(void *context,BOB64_FILESYSTEM *filesystem) {
    const BOB64_KERNEL_BOOT_INFO *info=(const BOB64_KERNEL_BOOT_INFO *)context;
    EFI_RUNTIME_SERVICES *services=kernel_runtime_services(info);
    return services?bob64_firmware_snapshot_restore(services,filesystem):
                    BOB64_FIRMWARE_SNAPSHOT_NOT_FOUND;
}

__attribute__((noreturn)) void bob64_kernel_main(const BOB64_KERNEL_BOOT_INFO *info) {
    bob64_early_console_init();
    if(info&&info->HasFramebuffer&&bob64_framebuffer_init(info->FramebufferBase,
       info->FramebufferSize,info->FramebufferWidth,info->FramebufferHeight,
       info->FramebufferPixelsPerScanLine,info->FramebufferPixelFormat))
        bob64_early_console_write("bob64 kernel: framebuffer rejected\r\n");
    kernel_write("bob64!\r\nbob64 kernel: long mode entry\r\n");
    if(!info||sizeof(void *)!=8||read_cr3()!=info->PageTableRoot||
       !info->MemoryMapAddress||!info->MemoryMapSize||
       info->MemoryDescriptorSize<40||!info->StackBase||!info->StackSize||
       info->PhysicalAddressBits<36||info->PhysicalAddressBits>52||
       info->NxSupported>1||info->HasFramebuffer>1) {
        kernel_write("bob64 kernel: invalid boot information\r\n");
        kernel_halt();
    }
    if(kernel_heap_smoke_test(info)) {
        kernel_write("bob64 kernel: heap initialization failed\r\n");
        kernel_halt();
    }
    if(kernel_table_pool_smoke_test(info)) {
        kernel_write("bob64 kernel: page-table pool growth failed\r\n");
        kernel_halt();
    }
    kernel_write("bob64 kernel: dynamic page-table pool passed\r\n");
    if(kernel_privilege_stack_init()) {
        kernel_write("bob64 kernel: privilege stack initialization failed\r\n");
        kernel_halt();
    }
    int process_test=kernel_process_smoke_test(info);
    if(process_test<0) {
        kernel_write("bob64 kernel: application loader smoke test failed\r\n");
        kernel_halt();
    }
    if(!process_test)kernel_write("bob64 kernel: ring-3 bob! app passed\r\n");
    else kernel_write("bob64 kernel: app test skipped (NX unavailable)\r\n");
    if(bob64_fs_init(&kernel_filesystem,&kernel_heap)) {
        kernel_write("bob64 kernel: filesystem initialization failed\r\n");
        kernel_halt();
    }
    if(info->NxSupported&&kernel_install_bob_application(&kernel_filesystem))
        kernel_write("bob64 kernel: demo app install failed\r\n");
    if(info->NxSupported&&kernel_install_gui_application(&kernel_filesystem))
        kernel_write("bob64 kernel: desktop app install failed\r\n");
    if(info->NxSupported&&kernel_install_nested_application(&kernel_filesystem))
        kernel_write("bob64 kernel: nested smoke app install failed\r\n");
    if(info->NxSupported&&kernel_install_echo_application(&kernel_filesystem))
        kernel_write("bob64 kernel: echo app install failed\r\n");
    if(kernel_install_bob_source(&kernel_filesystem))
        kernel_write("bob64 kernel: C demo source install failed\r\n");
    if(info->NxSupported&&kernel_install_cat_application(&kernel_filesystem))
        kernel_write("bob64 kernel: cat app install failed\r\n");
    if(info->NxSupported&&kernel_install_ls_application(&kernel_filesystem))
        kernel_write("bob64 kernel: ls app install failed\r\n");
    if(info->NxSupported&&kernel_install_notes_application(&kernel_filesystem))
        kernel_write("bob64 kernel: notes app install failed\r\n");
    if(info->NxSupported&&kernel_install_info_application(&kernel_filesystem))
        kernel_write("bob64 kernel: info app install failed\r\n");
    if(info->NxSupported&&kernel_install_mouse_smoke_application(&kernel_filesystem))
        kernel_write("bob64 kernel: mouse smoke app install failed\r\n");
    if(info->NxSupported&&kernel_install_launcher_application(&kernel_filesystem))
        kernel_write("bob64 kernel: launcher app install failed\r\n");
    if(info->NxSupported&&kernel_install_display_application(&kernel_filesystem))
        kernel_write("bob64 kernel: graphics demo install failed\r\n");
    kernel_app_context.Filesystem=&kernel_filesystem;
    kernel_app_context.NxSupported=info->NxSupported;
    kernel_app_context.PhysicalAddressBits=info->PhysicalAddressBits;
    kernel_write("CR3=0x");kernel_hex64(info->PageTableRoot);
    kernel_write(" memory-map=0x");kernel_hex64(info->MemoryMapAddress);
    kernel_write(" bytes=0x");kernel_hex64(info->MemoryMapSize);
    kernel_write(" heap=0x");kernel_hex64(BOB64_KERNEL_HEAP_BASE);
    kernel_write(" mapped=0x");kernel_hex64((u64)bob64_heap_mapped_bytes(&kernel_heap));
    kernel_write("\r\n");
    kernel_write("bob64 kernel: memory checks passed\r\n");
    if(bob64_interrupts_enable_timer()) {
        kernel_write("bob64 kernel: timer IRQ unavailable\r\n");
        kernel_halt();
    }
    kernel_write("bob64 kernel: timer IRQ enabled\r\n");
    u64 initial_timer_ticks=bob64_timer_ticks();
    for(volatile u32 timer_wait=0;
        timer_wait<100000000u&&bob64_timer_ticks()==initial_timer_ticks;
        timer_wait++)
        __asm__ volatile("pause");
    if(bob64_timer_ticks()==initial_timer_ticks) {
        kernel_write("bob64 kernel: timer tick test failed\r\n");
        kernel_halt();
    }
    kernel_write("bob64 kernel: timer tick passed\r\n");
    bob64_keyboard_reset();
    int keyboard_irq_enabled=bob64_interrupts_enable_keyboard()==0;
    if(!keyboard_irq_enabled)
        kernel_write("bob64 kernel: keyboard IRQ unavailable; using polling\r\n");
    else kernel_write("bob64 kernel: keyboard IRQ enabled\r\n");
    if(info->NxSupported) {
        int started=0;
        s64 status=kernel_run_application(&kernel_app_context,"bob.b64e",0,0,&started);
        if(!started||status||(keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native streaming app test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native streaming app passed\r\n");
        started=0;
        status=kernel_run_application(&kernel_app_context,"nested.b64e",0,0,
                                      &started);
        if(!started||status||(keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: nested app return test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: nested app return passed\r\n");
        static const char *echo_arguments[]={"echo.b64e","bob!"};
        started=0;
        status=kernel_run_application(&kernel_app_context,"echo.b64e",2,
                                      echo_arguments,&started);
        if(!started||status||kernel_user_output_length!=5||
           !kernel_bytes_equal(kernel_user_output,"bob!\n")||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: echo argument test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native echo arguments passed\r\n");
        static const char *cat_arguments[]={"cat.b64e","cat-smoke.txt"};
        static const char cat_fixture[]="bob!";
        if(bob64_fs_write(&kernel_filesystem,"cat-smoke.txt",cat_fixture,
                          sizeof(cat_fixture)-1)) {
            kernel_write("bob64 kernel: cat fixture setup failed\r\n");
            kernel_halt();
        }
        started=0;
        status=kernel_run_application(&kernel_app_context,"cat.b64e",2,
                                      cat_arguments,&started);
        int cat_output_ok=kernel_user_output_length==5&&
                          kernel_bytes_equal(kernel_user_output,"bob!\n");
        if(bob64_fs_delete(&kernel_filesystem,"cat-smoke.txt")) {
            kernel_write("bob64 kernel: cat fixture cleanup failed\r\n");
            kernel_halt();
        }
        if(!started||status||!cat_output_ok||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native file-read app test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native file-read app passed\r\n");
        started=0;
        status=kernel_run_application(&kernel_app_context,"ls.b64e",0,0,
                                      &started);
        if(!started||status||kernel_user_output_truncated||
           !kernel_text_contains(kernel_user_output,"bob.b64e  ")||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native file-list app test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native file-list app passed\r\n");
        static const char *notes_put_arguments[]={"notes.b64e","put",
                                                   "notes-smoke.txt","bob!"};
        static const char *notes_show_arguments[]={"notes.b64e","show",
                                                    "notes-smoke.txt"};
        static const char *notes_delete_arguments[]={"notes.b64e","delete",
                                                      "notes-smoke.txt"};
        static const char *notes_copy_arguments[]={"notes.b64e","copy",
            "notes-large-source.txt","notes-large-copy.txt"};
        static u8 note_fixture_check[4];
        started=0;
        status=kernel_run_application(&kernel_app_context,"notes.b64e",4,
                                      notes_put_arguments,&started);
        if(!started||status||kernel_app_read_file(&kernel_app_context,
            "notes-smoke.txt",note_fixture_check,sizeof(note_fixture_check))!=4||
           !kernel_bytes_equal((const char *)note_fixture_check,"bob!")||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native notes write test failed\r\n");
            kernel_halt();
        }
        started=0;
        status=kernel_run_application(&kernel_app_context,"notes.b64e",3,
                                      notes_show_arguments,&started);
        if(!started||status||kernel_user_output_length!=5||
           !kernel_bytes_equal(kernel_user_output,"bob!\n")||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native notes show test failed\r\n");
            kernel_halt();
        }
        started=0;
        status=kernel_run_application(&kernel_app_context,"notes.b64e",3,
                                      notes_delete_arguments,&started);
        if(!started||status||kernel_app_read_file(&kernel_app_context,
            "notes-smoke.txt",note_fixture_check,sizeof(note_fixture_check))!=-2||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native notes delete test failed\r\n");
            kernel_halt();
        }
        static char large_note_fixture[12288];
        const char *large_note_copy=0;
        usize large_note_copy_length=0;
        for(usize i=0;i<sizeof(large_note_fixture);i++)
            large_note_fixture[i]=(i%80==79)?'\n':(char)('a'+i%26);
        if(bob64_fs_write(&kernel_filesystem,"notes-large-source.txt",
                          large_note_fixture,sizeof(large_note_fixture))) {
            kernel_write("bob64 kernel: large notes fixture setup failed\r\n");
            kernel_halt();
        }
        started=0;
        status=kernel_run_application(&kernel_app_context,"notes.b64e",4,
                                      notes_copy_arguments,&started);
        int large_note_match=!bob64_fs_read(&kernel_filesystem,
            "notes-large-copy.txt",&large_note_copy,&large_note_copy_length)&&
            large_note_copy_length==sizeof(large_note_fixture);
        for(usize i=0;large_note_match&&i<sizeof(large_note_fixture);i++)
            if(large_note_copy[i]!=large_note_fixture[i])large_note_match=0;
        if(!started||status||kernel_user_output_length!=8||
           !kernel_bytes_equal(kernel_user_output,"Copied.\n")||
           kernel_user_output_truncated||!large_note_match||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: large notes streaming test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: large notes streaming copy passed\r\n");
        if(bob64_fs_delete(&kernel_filesystem,"notes-large-source.txt")||
           bob64_fs_delete(&kernel_filesystem,"notes-large-copy.txt")) {
            kernel_write("bob64 kernel: large notes fixture cleanup failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native notes app passed\r\n");
        started=0;
        status=kernel_run_application(&kernel_app_context,"info.b64e",0,0,
                                      &started);
        if(!started||status||kernel_user_output_truncated||
           !kernel_text_contains(kernel_user_output,
                                 "bob64 system information\n")||
           !kernel_text_contains(kernel_user_output,"System call ABI: 12\n")||
           !kernel_text_contains(kernel_user_output,"Uptime: ")||
           !kernel_text_contains(kernel_user_output,"RAM files: ")||
           !kernel_text_contains(kernel_user_output,"RAM file data: ")||
           kernel_user_output_length<5||
           !kernel_bytes_equal(kernel_user_output+
               kernel_user_output_length-5,"bob!\n")||
           (keyboard_irq_enabled&&!kernel_interrupts_enabled())) {
            kernel_write("bob64 kernel: native info app test failed\r\n");
            kernel_halt();
        }
        kernel_write("bob64 kernel: native info app passed\r\n");
    }
    EFI_RUNTIME_SERVICES *runtime_services=kernel_runtime_services(info);
    int snapshot_restore=runtime_services?bob64_firmware_snapshot_restore(
        runtime_services,&kernel_filesystem):
        BOB64_FIRMWARE_SNAPSHOT_NOT_FOUND;
    if(!snapshot_restore) {
        kernel_write("bob64 kernel: persistent B64S snapshot restored\r\n");
        const char *persisted_contents;
        usize persisted_length;
        if(!bob64_fs_read(&kernel_filesystem,"persist.txt",&persisted_contents,
                          &persisted_length)&&persisted_length==8&&
           kernel_bytes_equal(persisted_contents,"survived"))
            kernel_write("bob64 kernel: persistent B64S smoke file passed\r\n");
    } else if(snapshot_restore<0)
        kernel_write("bob64 kernel: persistent B64S snapshot rejected\r\n");
    u32 display_width,display_height;
    if(!bob64_framebuffer_resolution(&display_width,&display_height)) {
        if(bob64_mouse_init(display_width,display_height))
            kernel_write("bob64 kernel: PS/2 mouse unavailable\r\n");
        else if(bob64_interrupts_enable_mouse())
            kernel_write("bob64 kernel: mouse IRQ unavailable; using polling\r\n");
        else kernel_write("bob64 kernel: PS/2 mouse IRQ enabled\r\n");
    }
    if(bob64_shell_init(&kernel_shell,&bob64_boot_page_allocator,&kernel_heap,
                        &kernel_filesystem,
                        shell_write,shell_clear,&kernel_app_context)) {
        kernel_write("bob64 kernel: shell initialization failed\r\n");
        kernel_halt();
    }
    bob64_shell_set_runner(&kernel_shell,kernel_run_application);
    bob64_shell_set_compiler(&kernel_shell,kernel_compile_application);
    bob64_shell_set_snapshot_storage(&kernel_shell,kernel_snapshot_save,
                                     kernel_snapshot_restore,(void *)info);
    for(;;) {
        int character=bob64_keyboard_poll();
        if(character<0)character=bob64_early_console_try_read();
        if(character>=0)bob64_shell_input(&kernel_shell,character);
        else __asm__ volatile("pause");
    }
}
