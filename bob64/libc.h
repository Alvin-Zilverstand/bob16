#ifndef BOB64_LIBC_H
#define BOB64_LIBC_H

#include "types.h"

void *bob64_memcpy(void *destination,const void *source,usize length);
void *bob64_memmove(void *destination,const void *source,usize length);
void *bob64_memset(void *destination,int value,usize length);
int bob64_memcmp(const void *left,const void *right,usize length);
usize bob64_strlen(const char *text);
int bob64_strcmp(const char *left,const char *right);

#endif
