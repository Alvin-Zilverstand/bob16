#ifndef BOB_RUNTIME_H
#define BOB_RUNTIME_H
#include "bob.h"
#define HEAP_BASE 0xc000
#define HEAP_WORDS 8192
#define PROGRAM_BASE 0x9000
#define PROGRAM_WORDS 12288
#define FILE_COUNT 8
#define FILE_WORDS 512
#define NAME_WORDS 24
int strlen(char *text);
int strcmp(char *a, char *b);
void memcpy(int *dst, int *src, int count);
void memset(int *dst, int value, int count);
void print(char *text);
void println(char *text);
void print_dec(int number);
void print_hex(int number);
int read_line(char *buffer, int capacity);
int *alloc(int words);
int bob_address(int function);
int bob_call(int entry);
#endif
