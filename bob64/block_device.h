#ifndef BOB64_BLOCK_DEVICE_H
#define BOB64_BLOCK_DEVICE_H

#include "types.h"

#define BOB64_BLOCK_MAX_TRANSFER_BYTES 4096u

typedef int (*BOB64_BLOCK_READ)(void *context,u64 lba,u32 block_count,
                                void *buffer);
typedef int (*BOB64_BLOCK_WRITE)(void *context,u64 lba,u32 block_count,
                                 const void *buffer);
typedef int (*BOB64_BLOCK_FLUSH)(void *context);

typedef struct {
    void *Context;
    BOB64_BLOCK_READ Read;
    BOB64_BLOCK_WRITE Write;
    BOB64_BLOCK_FLUSH Flush;
    u64 BlockCount;
    u32 BlockSize,MaxTransferBlocks;
    u8 ReadOnly;
} BOB64_BLOCK_DEVICE;

int bob64_block_device_init(BOB64_BLOCK_DEVICE *device,void *context,
        u64 block_count,u32 block_size,int read_only,BOB64_BLOCK_READ read,
        BOB64_BLOCK_WRITE write,BOB64_BLOCK_FLUSH flush);
int bob64_block_read(BOB64_BLOCK_DEVICE *device,u64 lba,u32 block_count,
                     void *buffer);
int bob64_block_write(BOB64_BLOCK_DEVICE *device,u64 lba,u32 block_count,
                      const void *buffer);
int bob64_block_flush(BOB64_BLOCK_DEVICE *device);

#endif
