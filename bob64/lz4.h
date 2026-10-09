#ifndef BOB64_LZ4_H
#define BOB64_LZ4_H

#include "types.h"

#define BOB64_LZ4_WORKSPACE_SIZE (65536u*sizeof(u32))
usize bob64_lz4_compress_bound(usize input_size);
int bob64_lz4_compress(const void *input,usize input_size,void *output,
                       usize output_capacity,usize *output_size,
                       void *workspace,usize workspace_size);
int bob64_lz4_decompress(const void *input,usize input_size,void *output,
                         usize output_capacity,usize expected_output_size);

#endif
