#ifndef BOB64_RUNTIME_H
#define BOB64_RUNTIME_H

#include "memory.h"
#include "bootstrap.h"
#include "descriptors.h"

/* Boot-owned state remains in the mapped EFI image and is adopted after EBS. */
extern BOB64_PAGE_EXTENT bob64_boot_page_extents[BOB64_PAGE_EXTENT_LIMIT];
extern BOB64_PAGE_ALLOCATOR bob64_boot_page_allocator;
extern BOB64_BOOTSTRAP_SPACE bob64_bootstrap_space;
extern BOB64_TSS bob64_kernel_tss;

#endif
