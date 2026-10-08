#ifndef BOB64_TYPES_H
#define BOB64_TYPES_H

/* bob64's freestanding C types. Addresses and pointers are always 64-bit. */
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned long long u64;
typedef signed long long s64;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __SIZE_TYPE__ usize;
typedef __PTRDIFF_TYPE__ isize;

_Static_assert(sizeof(u8)==1,"u8 must be 8 bits");
_Static_assert(sizeof(u16)==2,"u16 must be 16 bits");
_Static_assert(sizeof(u32)==4,"u32 must be 32 bits");
_Static_assert(sizeof(u64)==8,"u64 must be 64 bits");
_Static_assert(sizeof(uintptr_t)==8,"bob64 requires 64-bit pointers");
_Static_assert(sizeof(usize)==8,"bob64 requires 64-bit sizes");
#ifdef BOB64_UEFI_ABI
_Static_assert(sizeof(long)==4,"x64 UEFI target uses the LLP64 data model");
#endif

#endif
