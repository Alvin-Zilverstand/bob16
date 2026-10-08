#include "libc.h"

void *bob64_memcpy(void *destination,const void *source,usize length) {
    u8 *out=(u8 *)destination;
    const u8 *in=(const u8 *)source;
    for(usize i=0;i<length;i++)out[i]=in[i];
    return destination;
}

void *bob64_memmove(void *destination,const void *source,usize length) {
    u8 *out=(u8 *)destination;
    const u8 *in=(const u8 *)source;
    if((uintptr_t)out<(uintptr_t)in)for(usize i=0;i<length;i++)out[i]=in[i];
    else if((uintptr_t)out>(uintptr_t)in)
        for(usize i=length;i;i--)out[i-1]=in[i-1];
    return destination;
}

void *bob64_memset(void *destination,int value,usize length) {
    u8 *out=(u8 *)destination;
    for(usize i=0;i<length;i++)out[i]=(u8)value;
    return destination;
}

int bob64_memcmp(const void *left,const void *right,usize length) {
    const u8 *a=(const u8 *)left,*b=(const u8 *)right;
    for(usize i=0;i<length;i++)if(a[i]!=b[i])return a[i]<b[i]?-1:1;
    return 0;
}

usize bob64_strlen(const char *text) {
    usize length=0;
    while(text[length])length++;
    return length;
}

int bob64_strcmp(const char *left,const char *right) {
    while(*left&&*left==*right){left++;right++;}
    return (u8)*left<(u8)*right?-1:(u8)*left>(u8)*right?1:0;
}
