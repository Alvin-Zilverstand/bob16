#ifndef BOB64_PROCESS_H
#define BOB64_PROCESS_H

#include "abi.h"
#include "exec.h"
#include "paging.h"

#define BOB64_PROCESS_IMAGE_BASE 0x0000004000000000ULL
#define BOB64_PROCESS_IMAGE_LIMIT (16ULL*1024ULL*1024ULL)
#define BOB64_PROCESS_STACK_GUARD (BOB64_PROCESS_IMAGE_BASE+0x02000000ULL)
#define BOB64_PROCESS_STACK_BASE (BOB64_PROCESS_STACK_GUARD+BOB64_PAGE_SIZE)
#define BOB64_PROCESS_STACK_SIZE (64ULL*1024ULL)
#define BOB64_PROCESS_ISOLATED_SIZE (34ULL*1024ULL*1024ULL)
#define BOB64_PROCESS_PAGE_LIMIT ((BOB64_PROCESS_IMAGE_LIMIT/BOB64_PAGE_SIZE)+\
                                  (BOB64_PROCESS_STACK_SIZE/BOB64_PAGE_SIZE))
#define BOB64_PROCESS_MAX_ARGUMENTS 128

typedef u64 (*BOB64_PROCESS_ALLOCATE_PAGE)(void *context);
typedef int (*BOB64_PROCESS_FREE_PAGE)(void *context,u64 physical_address);
/* Map failure must leave the VA unmapped; writable_address is a kernel alias. */
typedef int (*BOB64_PROCESS_MAP_PAGE)(void *context,u64 virtual_address,
                                     u64 physical_address,u64 flags,
                                     u8 **writable_address);
/* Protect/unmap callbacks invalidate the active address-space TLB entry. */
typedef int (*BOB64_PROCESS_PROTECT_PAGE)(void *context,u64 virtual_address,u64 flags);
typedef int (*BOB64_PROCESS_UNMAP_PAGE)(void *context,u64 virtual_address);

typedef struct {
    BOB64_PROCESS_ALLOCATE_PAGE AllocatePage;
    BOB64_PROCESS_FREE_PAGE FreePage;
    BOB64_PROCESS_MAP_PAGE MapPage;
    BOB64_PROCESS_PROTECT_PAGE ProtectPage;
    BOB64_PROCESS_UNMAP_PAGE UnmapPage;
    void *Context;
    u8 NxSupported;
} BOB64_PROCESS_OPERATIONS;

typedef struct {
    u64 VirtualAddress;
    u64 PhysicalAddress;
    u64 Flags;
    u8 *WritableAddress;
    u8 Allocated;
    u8 Mapped;
    u8 Reserved[6];
} BOB64_PROCESS_PAGE;

typedef struct {
    BOB64_PROCESS_PAGE *Pages;
    usize PageCapacity,PageCount;
    BOB64_PROCESS_OPERATIONS Operations;
    u64 EntryAddress,StartupAddress,InitialStackPointer;
    u8 Loaded;
} BOB64_PROCESS;

int bob64_process_init(BOB64_PROCESS *process,BOB64_PROCESS_PAGE *page_storage,
                      usize page_capacity,const BOB64_PROCESS_OPERATIONS *operations);
/* Parse an untrusted B64E file before allocating any application pages. */
int bob64_process_load(BOB64_PROCESS *process,const void *file,usize file_size,
                       usize argument_count,const char *const *arguments);
int bob64_process_unload(BOB64_PROCESS *process);

#endif
