#ifndef BOB64_DISK_STORE_H
#define BOB64_DISK_STORE_H

#include "partition.h"
#include "snapshot.h"

#define BOB64_DISK_STORE_NOT_FOUND 1

/* Two-slot transactional B64S persistence inside the dedicated GPT partition. */
int bob64_disk_store_save(BOB64_BLOCK_DEVICE *device,
        const BOB64_PARTITION *partition,BOB64_FILESYSTEM *filesystem,
        BOB64_HEAP *heap);
int bob64_disk_store_restore(BOB64_BLOCK_DEVICE *device,
        const BOB64_PARTITION *partition,BOB64_FILESYSTEM *filesystem,
        BOB64_HEAP *heap);

#endif
