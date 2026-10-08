#ifndef BOB64_CPU_H
#define BOB64_CPU_H

#include "types.h"

typedef struct {
    u8 PAE;
    u8 LongMode;
    u8 NX;
    u8 PhysicalAddressBits;
} BOB64_CPU_FEATURES;

int bob64_cpu_decode_features(u32 max_basic_leaf,u32 max_extended_leaf,
                              u32 basic_leaf1_edx,u32 extended_leaf1_edx,
                              u32 extended_leaf8_eax,BOB64_CPU_FEATURES *features);
int bob64_cpu_detect(BOB64_CPU_FEATURES *features);
int bob64_cpu_long_mode_active(const BOB64_CPU_FEATURES *features);
int bob64_cpu_four_level_paging_active(void);

#endif
