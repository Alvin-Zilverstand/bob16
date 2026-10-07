#ifndef BOB_RUNTIME_H
#define BOB_RUNTIME_H
#include "bob.h"
#if BOBC_WIDE
#define HEAP_BASE 0xd000
#define HEAP_WORDS 4096
#define PROGRAM_BASE 0xc200
#define PROGRAM_WORDS 3584
#else
#define HEAP_BASE 0xc000
#define HEAP_WORDS 8192
#define PROGRAM_BASE 0xa000
#define PROGRAM_WORDS 8192
#endif
#define WIDE_PROGRAM_BASE 0x20000
#define WIDE_VARIABLE_BASE 0xf0000
#define FILE_COUNT 8
#define TEXT_WORDS 512
#if BOBC_WIDE
#define FILE_WORDS 4096
#else
#define FILE_WORDS 512
#endif
#define NAME_WORDS 24
int strlen(char *text);
int strcmp(char *a, char *b);
int file_find(char *name);
int file_prepare_native(char *name);
char *file_name(int slot);
int *file_content(int slot);
#if BOBC_WIDE
void memcpy(void *dst, void *src, int count);
void memset(void *dst, int value, int count);
#else
void word_copy(int *dst, int *src, int count);
void word_fill(int *dst, int value, int count);
#define memcpy(dst, src, count) word_copy((int *)(dst), (int *)(src), (count))
#define memset(dst, value, count) word_fill((int *)(dst), (value), (count))
#endif
void print(char *text);
void println(char *text);
void print_dec(int number);
void print_hex(int number);
int read_line(char *buffer, int capacity);
int *alloc(int words);
int bob_address(int function);
int bob_call(int entry);
#endif
