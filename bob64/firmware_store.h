#ifndef BOB64_FIRMWARE_STORE_H
#define BOB64_FIRMWARE_STORE_H

#include "efi.h"
#include "filesystem.h"
#include "heap.h"

#define BOB64_FIRMWARE_SNAPSHOT_LIMIT 61440u
#define BOB64_FIRMWARE_SNAPSHOT_HEADER_SIZE 24u
#define BOB64_FIRMWARE_SNAPSHOT_NOT_FOUND 1

int bob64_firmware_snapshot_save(EFI_RUNTIME_SERVICES *services,BOB64_HEAP *heap,
                                 const void *snapshot,usize snapshot_size,
                                 EFI_STATUS *firmware_status,
                                 UINTN *maximum_variable_size,
                                 UINTN *remaining_storage_size);
int bob64_firmware_snapshot_restore(EFI_RUNTIME_SERVICES *services,
                                    BOB64_FILESYSTEM *filesystem);

#endif
