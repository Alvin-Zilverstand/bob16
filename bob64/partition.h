#ifndef BOB64_PARTITION_H
#define BOB64_PARTITION_H

#include "block_device.h"

#define BOB64_GPT_GUID_SIZE 16u
#define BOB64_GPT_BOB_DATA_TYPE_GUID_INITIALIZER \
    {0x64,0x62,0x61,0x7b,0x30,0x30,0x34,0x30, \
     0x9a,0x21,0x42,0x4f,0x42,0x36,0x34,0x01}

typedef struct {
    u64 FirstLba,BlockCount;
    u8 TypeGuid[BOB64_GPT_GUID_SIZE];
    u8 UniqueGuid[BOB64_GPT_GUID_SIZE];
} BOB64_PARTITION;

extern const u8 bob64_gpt_bob_data_type_guid[BOB64_GPT_GUID_SIZE];

/* Returns 0 when found, 1 when absent, and a negative value for invalid GPT. */
int bob64_gpt_find_partition(BOB64_BLOCK_DEVICE *device,
        const u8 type_guid[BOB64_GPT_GUID_SIZE],BOB64_PARTITION *partition);

#endif
