#ifndef BOB64_EXEC_H
#define BOB64_EXEC_H

#include "abi.h"

/* B64E is flat/PIC; code ends on a 4 KiB boundary. Offsets are bytes. */
#define BOB64_EXEC_VERSION 1u
#define BOB64_EXEC_ABI_VERSION BOB64_APP_ABI_VERSION
#define BOB64_EXEC_HEADER_SIZE 64u

typedef struct {
    const u8 *Image;
    u64 FileSize;
    u64 MemorySize;
    u64 EntryOffset;
    u64 CodeSize;
    u32 AbiVersion;
    u32 Flags;
} BOB64_EXEC_IMAGE;

/* Parse and validate an exact B64E file. No allocations or state changes. */
int bob64_exec_parse(const void *file,usize file_size,BOB64_EXEC_IMAGE *image);

/* Copy a validated image, zero its memory tail, and return its full-width entry. */
int bob64_exec_load(const BOB64_EXEC_IMAGE *image,u64 load_address,
                    void *destination,usize destination_capacity,
                    u64 *entry_address);

#endif
