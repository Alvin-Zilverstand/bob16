#ifndef BOB64_KERNEL_H
#define BOB64_KERNEL_H

#include "types.h"
#include "descriptors.h"

typedef struct {
    u64 PageTableRoot;
    u64 MemoryMapAddress;
    u64 MemoryMapSize;
    u64 MemoryDescriptorSize;
    u64 StackBase;
    u64 StackSize;
    u64 FramebufferBase;
    u64 FramebufferSize;
    u32 FramebufferWidth;
    u32 FramebufferHeight;
    u32 FramebufferPixelsPerScanLine;
    u32 FramebufferPixelFormat;
    u8 PhysicalAddressBits;
    u8 NxSupported;
    u8 HasFramebuffer;
    u8 Reserved[5];
    u64 RuntimeServices;
} BOB64_KERNEL_BOOT_INFO;

_Static_assert(sizeof(BOB64_KERNEL_BOOT_INFO)==96,"bob64 kernel boot information ABI");
_Static_assert(__builtin_offsetof(BOB64_KERNEL_BOOT_INFO,NxSupported)==81,
               "handoff assembly NX-support offset mismatch");
_Static_assert(__builtin_offsetof(BOB64_KERNEL_BOOT_INFO,RuntimeServices)==88,
               "UEFI runtime-service pointer ABI offset");

__attribute__((noreturn)) void bob64_kernel_main(const BOB64_KERNEL_BOOT_INFO *info);
__attribute__((noreturn)) void bob64_enter_kernel(
    u64 page_table_root,u64 stack_top,
    const BOB64_DESCRIPTOR_TABLE_POINTER *gdtr,
    const BOB64_DESCRIPTOR_TABLE_POINTER *idtr,
    const BOB64_KERNEL_BOOT_INFO *info);

#endif
