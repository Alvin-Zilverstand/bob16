#ifndef BOB64_COMPILER_H
#define BOB64_COMPILER_H

#include "exec.h"
#include "paging.h"

#define BOB64_COMPILER_SOURCE_LIMIT (4u*BOB64_PAGE_SIZE)
#define BOB64_COMPILER_CODE_CAPACITY (4u*BOB64_PAGE_SIZE)
#define BOB64_COMPILER_DATA_CAPACITY (4u*BOB64_PAGE_SIZE)
#define BOB64_COMPILER_IMAGE_LIMIT (BOB64_EXEC_HEADER_SIZE+\
    BOB64_COMPILER_CODE_CAPACITY+BOB64_COMPILER_DATA_CAPACITY)

/* Compile bob64's bounded resident C subset directly to a B64E application. */
int bob64_compile_c(const char *source,usize source_length,void *output,
                    usize output_capacity,usize *output_length,
                    usize *error_offset);

#endif
