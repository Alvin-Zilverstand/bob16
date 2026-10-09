#ifndef BOB64_APP_H
#define BOB64_APP_H

#include "abi.h"
#include "event.h"

/* The resident compiler and native GCC apps share Bob64's LLP64 data model. */
#if defined(__cplusplus)
static_assert(sizeof(void *)==8,"Bob64 apps require 64-bit pointers");
static_assert(sizeof(short)==2&&sizeof(int)==4&&sizeof(long)==4&&
              sizeof(long long)==8,"Bob64 apps require the LLP64 data model");
#else
_Static_assert(sizeof(void *)==8,"Bob64 apps require 64-bit pointers");
_Static_assert(sizeof(short)==2&&sizeof(int)==4&&sizeof(long)==4&&
               sizeof(long long)==8,"Bob64 apps require the LLP64 data model");
#endif

/* Native apps use int 0x80; only RAX is changed by a returning service. */
static inline s64 bob64_app_query_abi(void) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_QUERY_ABI;
    __asm__ volatile("int $0x80":"+a"(rax)::"memory","cc");
    return (s64)rax;
}

/* Number of 100 Hz PIT ticks since kernel initialization. */
static inline u64 bob64_app_ticks(void) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_GET_TICKS;
    __asm__ volatile("int $0x80":"+a"(rax)::"memory","cc");
    return rax;
}

/* Display size is returned as width in RAX and height in RDX. */
static inline s64 bob64_app_get_display(u32 *width,u32 *height) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_GET_DISPLAY;
    register u64 rdx __asm__("rdx");
    __asm__ volatile("int $0x80":"+a"(rax),"=d"(rdx)::"memory","cc");
    if((s64)rax<0)return (s64)rax;
    if(!width||!height)return -22;
    *width=(u32)rax;*height=(u32)rdx;
    return 0;
}

/* Present one packed 0x00RRGGBB pixel for every display pixel. */
static inline s64 bob64_app_present(const u32 *pixels,u32 width,u32 height) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_PRESENT;
    register u64 rcx __asm__("rcx")=(u64)(uintptr_t)pixels;
    register u64 rdx __asm__("rdx")=width;
    register u64 r8 __asm__("r8")=height;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx),"r"(r8):"memory","cc");
    return (s64)rax;
}

/* Wait for the next key press or release; the event is app-owned. */
static inline s64 bob64_app_wait_event(BOB64_EVENT *event) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_WAIT_EVENT;
    register u64 rcx __asm__("rcx")=(u64)(uintptr_t)event;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx):"memory","cc");
    return (s64)rax;
}

static inline s64 bob64_app_write_char(u8 character) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_WRITE_CHAR;
    register u64 rcx __asm__("rcx")=character;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx):"memory","cc");
    return (s64)rax;
}

static inline s64 bob64_app_write(const void *buffer,usize length) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_WRITE_BUFFER;
    register u64 rcx __asm__("rcx")=(u64)(uintptr_t)buffer;
    register u64 rdx __asm__("rdx")=(u64)length;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx):"memory","cc");
    return (s64)rax;
}

/* File services use a bounded name (1..63 bytes) and at most 4096 data bytes. */
static inline s64 bob64_app_read_file(const char *name,usize name_length,
                                      void *buffer,usize capacity) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_READ_FILE;
    register u64 rcx __asm__("rcx")=(u64)(uintptr_t)name;
    register u64 rdx __asm__("rdx")=(u64)name_length;
    register u64 r8 __asm__("r8")=(u64)(uintptr_t)buffer;
    register u64 r9 __asm__("r9")=(u64)capacity;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx),"r"(r8),"r"(r9):"memory","cc");
    return (s64)rax;
}

static inline s64 bob64_app_write_file(const char *name,usize name_length,
                                       const void *buffer,usize length) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_WRITE_FILE;
    register u64 rcx __asm__("rcx")=(u64)(uintptr_t)name;
    register u64 rdx __asm__("rdx")=(u64)name_length;
    register u64 r8 __asm__("r8")=(u64)(uintptr_t)buffer;
    register u64 r9 __asm__("r9")=(u64)length;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx),"r"(r8),"r"(r9):"memory","cc");
    return (s64)rax;
}

/* List at most 32 files into an app-owned array of fixed-width records. */
static inline s64 bob64_app_list_files(BOB64_FILE_INFO *entries,usize capacity) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_LIST_FILES;
    register u64 rcx __asm__("rcx")=(u64)(uintptr_t)entries;
    register u64 rdx __asm__("rdx")=(u64)capacity;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx):"memory","cc");
    return (s64)rax;
}

static inline s64 bob64_app_delete_file(const char *name,usize name_length) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_DELETE_FILE;
    register u64 rcx __asm__("rcx")=(u64)(uintptr_t)name;
    register u64 rdx __asm__("rdx")=(u64)name_length;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx):"memory","cc");
    return (s64)rax;
}

/* Streaming file handles keep a 64-bit position and transfer at most 4096 bytes per call. */
static inline s64 bob64_app_file_open(const char *name,usize name_length,u32 flags) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_OPEN_FILE;
    register u64 rcx __asm__("rcx")=(u64)(uintptr_t)name;
    register u64 rdx __asm__("rdx")=(u64)name_length;
    register u64 r8 __asm__("r8")=flags;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx),"r"(r8):"memory","cc");
    return (s64)rax;
}

static inline s64 bob64_app_file_read(s64 handle,void *buffer,usize capacity) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_READ_HANDLE;
    register u64 rcx __asm__("rcx")=(u64)handle;
    register u64 rdx __asm__("rdx")=(u64)(uintptr_t)buffer;
    register u64 r8 __asm__("r8")=(u64)capacity;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx),"r"(r8):"memory","cc");
    return (s64)rax;
}

static inline s64 bob64_app_file_write(s64 handle,const void *buffer,usize length) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_WRITE_HANDLE;
    register u64 rcx __asm__("rcx")=(u64)handle;
    register u64 rdx __asm__("rdx")=(u64)(uintptr_t)buffer;
    register u64 r8 __asm__("r8")=(u64)length;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx),"r"(r8):"memory","cc");
    return (s64)rax;
}

static inline s64 bob64_app_file_seek(s64 handle,u64 offset) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_SEEK_HANDLE;
    register u64 rcx __asm__("rcx")=(u64)handle;
    register u64 rdx __asm__("rdx")=offset;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx):"memory","cc");
    return (s64)rax;
}

static inline s64 bob64_app_file_close(s64 handle) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_CLOSE_HANDLE;
    register u64 rcx __asm__("rcx")=(u64)handle;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx):"memory","cc");
    return (s64)rax;
}

/* Transfer a whole app file with bounded streaming syscalls. */
static inline s64 bob64_app_file_read_all(const char *name,usize name_length,
                                          void *buffer,usize capacity) {
    if((!buffer&&capacity)||capacity>0x7fffffffffffffffULL)return -22;
    s64 handle=bob64_app_file_open(name,name_length,BOB64_FILE_OPEN_READ);
    if(handle<0)return handle;
    usize total=0;
    s64 result=0;
    while(total<capacity) {
        usize chunk=capacity-total;
        if(chunk>BOB64_SYSCALL_MAX_BUFFER)chunk=BOB64_SYSCALL_MAX_BUFFER;
        s64 count=bob64_app_file_read(handle,(u8 *)buffer+total,chunk);
        if(count<0) {result=count;goto close_file;}
        if(!count) {result=(s64)total;goto close_file;}
        if((u64)count>chunk) {result=-5;goto close_file;}
        total+=(usize)count;
    }
    {
        u8 extra;
        s64 count=bob64_app_file_read(handle,&extra,1);
        result=count<0?count:(count?-28:(s64)total);
    }
close_file:
    if(bob64_app_file_close(handle)<0&&result>=0)result=-5;
    return result;
}

static inline s64 bob64_app_file_write_all(const char *name,usize name_length,
                                           const void *buffer,usize length) {
    if((!buffer&&length)||length>0x7fffffffffffffffULL)return -22;
    if(length<=BOB64_SYSCALL_MAX_BUFFER)
        return bob64_app_write_file(name,name_length,buffer,length);
    s64 handle=bob64_app_file_open(name,name_length,
        BOB64_FILE_OPEN_WRITE|BOB64_FILE_OPEN_CREATE|BOB64_FILE_OPEN_TRUNCATE);
    if(handle<0)return handle;
    usize total=0;
    s64 result=0;
    while(total<length) {
        usize chunk=length-total;
        if(chunk>BOB64_SYSCALL_MAX_BUFFER)chunk=BOB64_SYSCALL_MAX_BUFFER;
        s64 count=bob64_app_file_write(handle,(const u8 *)buffer+total,chunk);
        if(count<0) {result=count;goto close_file;}
        if(!count||(u64)count>chunk) {result=-5;goto close_file;}
        total+=(usize)count;
    }
    result=(s64)total;
close_file:
    if(bob64_app_file_close(handle)<0&&result>=0)result=-5;
    return result;
}

/* Run a stored B64E app synchronously and restore this app's syscall context. */
static inline s64 bob64_app_run(const char *name,usize name_length,
                                usize argument_count,
                                const char *const *arguments,
                                s64 *exit_status) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_RUN_APPLICATION;
    register u64 rcx __asm__("rcx")=(u64)(uintptr_t)name;
    register u64 rdx __asm__("rdx")=(u64)name_length;
    register u64 r8 __asm__("r8")=(u64)(uintptr_t)exit_status;
    register u64 rsi __asm__("rsi")=(u64)argument_count;
    register u64 r9 __asm__("r9")=(u64)(uintptr_t)arguments;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx),"r"(r8),
                     "S"(rsi),"r"(r9):
                     "memory","cc");
    return (s64)rax;
}

/* Kernel-owned windows composite app-owned packed RGB pixel surfaces. */
static inline u64 bob64_app_window_create(s32 x,s32 y,u32 width,u32 height) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_CREATE_WINDOW;
    register u64 rcx __asm__("rcx")=(u64)(s64)x;
    register u64 rdx __asm__("rdx")=(u64)(s64)y;
    register u64 r8 __asm__("r8")=width;
    register u64 r9 __asm__("r9")=height;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx),"r"(r8),"r"(r9):"memory","cc");
    return rax;
}

static inline s64 bob64_app_window_destroy(u64 handle) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_DESTROY_WINDOW;
    register u64 rcx __asm__("rcx")=handle;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx):"memory","cc");
    return (s64)rax;
}

static inline s64 bob64_app_window_focus(u64 handle) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_FOCUS_WINDOW;
    register u64 rcx __asm__("rcx")=handle;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx):"memory","cc");
    return (s64)rax;
}

static inline s64 bob64_app_window_present(u64 handle,const u32 *pixels,
                                            u32 width,u32 height) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_PRESENT_WINDOW;
    register u64 rcx __asm__("rcx")=handle;
    register u64 rdx __asm__("rdx")=(u64)(uintptr_t)pixels;
    register u64 r8 __asm__("r8")=width;
    register u64 r9 __asm__("r9")=height;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx),"d"(rdx),"r"(r8),"r"(r9):"memory","cc");
    return (s64)rax;
}

/* Exit transfers control to the kernel; it does not return to this caller. */
static inline __attribute__((noreturn)) void bob64_app_exit(s64 status) {
    register u64 rax __asm__("rax")=BOB64_SYSCALL_EXIT;
    register u64 rcx __asm__("rcx")=(u64)status;
    __asm__ volatile("int $0x80":"+a"(rax):"c"(rcx):"memory","cc");
    __builtin_unreachable();
}

#endif
