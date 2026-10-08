#ifndef BOB64_ABI_H
#define BOB64_ABI_H

#include "types.h"

#define BOB64_APP_ABI_VERSION 1u
#define BOB64_SYSCALL_ABI_VERSION 10u
#define BOB64_SYSCALL_QUERY_ABI 0u
#define BOB64_SYSCALL_WRITE_CHAR 1u
#define BOB64_SYSCALL_WRITE_BUFFER 2u
#define BOB64_SYSCALL_EXIT 3u
#define BOB64_SYSCALL_READ_FILE 4u
#define BOB64_SYSCALL_WRITE_FILE 5u
#define BOB64_SYSCALL_GET_DISPLAY 6u
#define BOB64_SYSCALL_PRESENT 7u
#define BOB64_SYSCALL_WAIT_EVENT 8u
#define BOB64_SYSCALL_LIST_FILES 9u
#define BOB64_SYSCALL_DELETE_FILE 10u
#define BOB64_SYSCALL_CREATE_WINDOW 11u
#define BOB64_SYSCALL_DESTROY_WINDOW 12u
#define BOB64_SYSCALL_PRESENT_WINDOW 13u
#define BOB64_SYSCALL_FOCUS_WINDOW 14u
#define BOB64_SYSCALL_OPEN_FILE 15u
#define BOB64_SYSCALL_READ_HANDLE 16u
#define BOB64_SYSCALL_WRITE_HANDLE 17u
#define BOB64_SYSCALL_SEEK_HANDLE 18u
#define BOB64_SYSCALL_CLOSE_HANDLE 19u
#define BOB64_SYSCALL_GET_TICKS 20u
#define BOB64_FILE_OPEN_READ 0x01u
#define BOB64_FILE_OPEN_WRITE 0x02u
#define BOB64_FILE_OPEN_CREATE 0x04u
#define BOB64_FILE_OPEN_TRUNCATE 0x08u
#define BOB64_FILE_OPEN_APPEND 0x10u
#define BOB64_FILE_OPEN_FLAGS 0x1fu
#define BOB64_SYSCALL_MAX_BUFFER 4096u
#define BOB64_SYSCALL_MAX_FILES 32u
#define BOB64_SYSCALL_MAX_SURFACE_BYTES (16u*1024u*1024u)
#define BOB64_SYSCALL_COPY_CHUNK_BYTES 4096u
#define BOB64_SYSCALL_MAX_FILENAME 63u
_Static_assert(BOB64_SYSCALL_MAX_SURFACE_BYTES%sizeof(u32)==0,
               "pixel-surface size is a whole number of pixels");
_Static_assert(BOB64_SYSCALL_COPY_CHUNK_BYTES%sizeof(u32)==0,
               "syscall copy chunk is a whole number of pixels");

typedef struct {
    char Name[64];
    u64 Size;
} BOB64_FILE_INFO;

_Static_assert(sizeof(BOB64_FILE_INFO)==72,"bob64 file-info ABI size");

/* The application startup block and argv strings live in the app address space. */
typedef struct {
    u32 StructSize;
    u32 AbiVersion;
    u64 ArgumentCount;
    const char *const *Arguments;
    u64 Flags;
} BOB64_APP_STARTUP;

_Static_assert(sizeof(BOB64_APP_STARTUP)==32,"bob64 application startup ABI size");
_Static_assert(__builtin_offsetof(BOB64_APP_STARTUP,ArgumentCount)==8,
               "bob64 startup argument-count offset");
_Static_assert(__builtin_offsetof(BOB64_APP_STARTUP,Arguments)==16,
               "bob64 startup argv offset");
_Static_assert(__builtin_offsetof(BOB64_APP_STARTUP,Flags)==24,
               "bob64 startup flags offset");

/* GCC on non-Windows x86-64 hosts needs an explicit attribute to test the ABI. */
#if defined(__x86_64__) && !defined(_WIN32) && defined(__GNUC__)
#define BOB64_MS_ABI __attribute__((ms_abi))
#else
#define BOB64_MS_ABI
#endif

/* Entry receives the app-owned startup block in RCX and returns status in RAX. */
typedef s64 (BOB64_MS_ABI *BOB64_APP_ENTRY)(const BOB64_APP_STARTUP *startup);

#endif
