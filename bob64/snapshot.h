#ifndef BOB64_SNAPSHOT_H
#define BOB64_SNAPSHOT_H

#include "filesystem.h"

#define BOB64_SNAPSHOT_VERSION 1
#define BOB64_SNAPSHOT_HEADER_SIZE 24

int bob64_fs_snapshot_size(const BOB64_FILESYSTEM *filesystem,usize *size);
int bob64_fs_snapshot_write(const BOB64_FILESYSTEM *filesystem,
                            void *output,usize capacity,usize *written);
int bob64_fs_snapshot_restore(BOB64_FILESYSTEM *filesystem,
                              const void *snapshot,usize snapshot_size);

#endif
