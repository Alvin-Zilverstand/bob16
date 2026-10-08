#include "cpu.h"

#define CPUID_PAE (1u<<6)
#define CPUID_LONG_MODE (1u<<29)
#define CPUID_NX (1u<<20)

int bob64_cpu_decode_features(u32 max_basic_leaf,u32 max_extended_leaf,
                              u32 basic_leaf1_edx,u32 extended_leaf1_edx,
                              u32 extended_leaf8_eax,BOB64_CPU_FEATURES *features) {
    u32 physical_bits;
    if(!features)return -1;
    features->PAE=(u8)(max_basic_leaf>=1&&(basic_leaf1_edx&CPUID_PAE)!=0);
    features->LongMode=(u8)(max_extended_leaf>=0x80000001u&&
                           (extended_leaf1_edx&CPUID_LONG_MODE)!=0);
    features->NX=(u8)(max_extended_leaf>=0x80000001u&&
                     (extended_leaf1_edx&CPUID_NX)!=0);
    if(max_extended_leaf>=0x80000008u) {
        physical_bits=extended_leaf8_eax&0xffu;
        if(physical_bits<36||physical_bits>52)return -1;
    } else physical_bits=36;
    features->PhysicalAddressBits=(u8)physical_bits;
    return 0;
}

static void cpuid(u32 leaf,u32 *eax,u32 *ebx,u32 *ecx,u32 *edx) {
    u32 a=leaf,b=0,c=0,d=0;
    __asm__ volatile("cpuid":"+a"(a),"=b"(b),"=c"(c),"=d"(d));
    if(eax)*eax=a;
    if(ebx)*ebx=b;
    if(ecx)*ecx=c;
    if(edx)*edx=d;
}

int bob64_cpu_detect(BOB64_CPU_FEATURES *features) {
    u32 max_basic=0,max_extended=0,leaf1_edx=0,extended1_edx=0,extended8_eax=0;
    cpuid(0,&max_basic,0,0,0);
    cpuid(0x80000000u,&max_extended,0,0,0);
    if(max_basic>=1)cpuid(1,0,0,0,&leaf1_edx);
    if(max_extended>=0x80000001u)cpuid(0x80000001u,0,0,0,&extended1_edx);
    if(max_extended>=0x80000008u)cpuid(0x80000008u,&extended8_eax,0,0,0);
    return bob64_cpu_decode_features(max_basic,max_extended,leaf1_edx,
                                     extended1_edx,extended8_eax,features);
}

int bob64_cpu_long_mode_active(const BOB64_CPU_FEATURES *features) {
    u32 efer_low,efer_high;
    u32 ecx=0xc0000080u;
    if(!features||!features->PAE||!features->LongMode)return 0;
    __asm__ volatile("rdmsr":"=a"(efer_low),"=d"(efer_high):"c"(ecx));
    (void)efer_high;
    return (efer_low&(1u<<10))!=0;
}

int bob64_cpu_four_level_paging_active(void) {
    u64 cr4;
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4));
    return (cr4&(1ULL<<5))!=0&&!(cr4&(1ULL<<12)); /* PAE set, LA57 clear. */
}
