#ifndef BOB64_LIBC_H
#define BOB64_LIBC_H

#include "types.h"

void *bob64_memcpy(void *destination,const void *source,usize length);
void *bob64_memmove(void *destination,const void *source,usize length);
void *bob64_memset(void *destination,int value,usize length);
int bob64_memcmp(const void *left,const void *right,usize length);
void *bob64_memchr(const void *memory,int value,usize length);
usize bob64_strlen(const char *text);
usize bob64_strnlen(const char *text,usize limit);
int bob64_strcmp(const char *left,const char *right);
int bob64_strncmp(const char *left,const char *right,usize limit);
char *bob64_strcpy(char *destination,const char *source);
char *bob64_strncpy(char *destination,const char *source,usize count);
char *bob64_strcat(char *destination,const char *source);
char *bob64_strncat(char *destination,const char *source,usize count);
char *bob64_strchr(const char *text,int character);
char *bob64_strrchr(const char *text,int character);
char *bob64_strstr(const char *text,const char *needle);
int bob64_snprintf(char *destination,usize capacity,const char *format,...);

#endif
