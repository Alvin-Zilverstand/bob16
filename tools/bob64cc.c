/* Build a small freestanding C program into bob64's versioned B64E format. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#define APP_EXE ".exe"
#else
#include <sys/stat.h>
#define make_dir(path) mkdir(path,0777)
#define APP_EXE ""
#endif

#define APP_BASE 0x0000004000000000ULL
#define APP_LIMIT (16ULL*1024ULL*1024ULL)
#define PAGE_SIZE 4096u
#define HEADER_SIZE 64u
#define APP_ABI_VERSION 1u
#define EXEC_VERSION 1u

static uint16_t read16(const unsigned char *p) {
    return (uint16_t)(p[0]|((uint16_t)p[1]<<8));
}
static uint32_t read32(const unsigned char *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint64_t read64(const unsigned char *p) {
    return (uint64_t)read32(p)|((uint64_t)read32(p+4)<<32);
}
static void write16(unsigned char *p,uint16_t v) {
    p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);
}
static void write32(unsigned char *p,uint32_t v) {
    for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));
}
static void write64(unsigned char *p,uint64_t v) {
    write32(p,(uint32_t)v);write32(p+4,(uint32_t)(v>>32));
}
static uint32_t crc32(const unsigned char *p,size_t n) {
    uint32_t value=0xffffffffu;
    for(size_t i=0;i<n;i++) {
        value^=p[i];
        for(unsigned bit=0;bit<8;bit++)
            value=(value>>1)^(0xedb88320u&-(value&1u));
    }
    return ~value;
}
static unsigned char *read_file(const char *path,size_t *length) {
    FILE *f=fopen(path,"rb");
    unsigned char *bytes;
    long size;
    if(!f){perror(path);return NULL;}
    if(fseek(f,0,SEEK_END)||(size=ftell(f))<0||size>64*1024*1024L||
       fseek(f,0,SEEK_SET)) {fclose(f);fprintf(stderr,"bob64cc: invalid input size\n");return NULL;}
    bytes=(unsigned char *)malloc((size_t)size? (size_t)size:1);
    if(!bytes){fclose(f);fprintf(stderr,"bob64cc: out of memory\n");return NULL;}
    if(fread(bytes,1,(size_t)size,f)!=(size_t)size||fclose(f)) {
        free(bytes);fprintf(stderr,"bob64cc: cannot read %s\n",path);return NULL;
    }
    *length=(size_t)size;return bytes;
}

/* Put a trailing zero-fill section after PE's final file-backed sections. */
static int bss_layout_arguments(const unsigned char *pe,size_t pe_size,
        char *arguments,size_t capacity) {
    uint32_t pe_offset,alignment,bss_rva=0,bss_size=0,bss_raw=0;
    uint32_t idata_rva=0,idata_size=0,idata_raw=0;
    uint16_t section_count,optional_size;
    uint64_t image_base,idata_address,bss_address;
    size_t section_table;
    if(!pe||!arguments||!capacity||pe_size<64||pe[0]!='M'||pe[1]!='Z')return -1;
    arguments[0]=0;pe_offset=read32(pe+0x3c);
    if((uint64_t)pe_offset+24>pe_size||memcmp(pe+pe_offset,"PE\0\0",4))return -1;
    const unsigned char *coff=pe+pe_offset+4;
    section_count=read16(coff+2);optional_size=read16(coff+16);
    if(!section_count||section_count>32||optional_size<112||
       (uint64_t)pe_offset+24+optional_size>pe_size)return -1;
    const unsigned char *optional=coff+20;
    image_base=read64(optional+24);alignment=read32(optional+32);
    if(read16(optional)!=0x20b||!alignment||
       (uint64_t)pe_offset+24+optional_size>pe_size)return -1;
    section_table=(size_t)pe_offset+24+optional_size;
    if(section_count>(pe_size-section_table)/40)return -1;
    for(uint16_t i=0;i<section_count;i++) {
        const unsigned char *section=pe+section_table+(size_t)i*40;
        uint32_t virtual_size=read32(section+8),rva=read32(section+12);
        uint32_t raw_size=read32(section+16);
        if(!memcmp(section,".bss",4)) {
            bss_rva=rva;bss_size=virtual_size;bss_raw=raw_size;
        } else if(!memcmp(section,".idata",6)) {
            idata_rva=rva;idata_size=virtual_size;idata_raw=raw_size;
        }
    }
    if(!bss_size||bss_raw||!idata_size||idata_rva<=bss_rva)return 0;
    if((idata_size>alignment?idata_size:idata_raw)>alignment||
       image_base>UINT64_MAX-bss_rva-alignment)return -1;
    idata_address=image_base+bss_rva;
    bss_address=idata_address+alignment;
    int length=snprintf(arguments,capacity,
        "-Wl,--section-start,.idata=0x%llx -Wl,--section-start,.bss=0x%llx",
        (unsigned long long)idata_address,(unsigned long long)bss_address);
    return length<0||(size_t)length>=capacity?-1:1;
}
static int write_b64e(const char *path,const unsigned char *pe,size_t pe_size,
                      const unsigned char *payload,size_t payload_size) {
    uint32_t pe_offset,entry_rva,section_alignment,size_of_image;
    uint64_t image_base,lowest_rva=UINT64_MAX,highest_virtual=0,code_end=0;
    uint64_t entry_offset,code_size,memory_size;
    uint16_t section_count,optional_size;
    size_t section_table;
    int executable_at_lowest=0;
    unsigned char *image;
    FILE *out;
    if(pe_size<64||pe[0]!='M'||pe[1]!='Z')goto bad_pe;
    pe_offset=read32(pe+0x3c);
    if((uint64_t)pe_offset+24>pe_size||memcmp(pe+pe_offset,"PE\0\0",4))goto bad_pe;
    const unsigned char *coff=pe+pe_offset+4;
    if(read16(coff)!=0x8664)goto bad_pe;
    section_count=read16(coff+2);optional_size=read16(coff+16);
    if(!section_count||section_count>32||optional_size<112||
       (uint64_t)pe_offset+24+optional_size>pe_size)goto bad_pe;
    const unsigned char *optional=coff+20;
    if(read16(optional)!=0x20b)goto bad_pe;
    entry_rva=read32(optional+16);image_base=read64(optional+24);
    section_alignment=read32(optional+32);size_of_image=read32(optional+56);
    if(section_alignment!=PAGE_SIZE||!entry_rva||
       !size_of_image||size_of_image>APP_LIMIT+PAGE_SIZE)goto bad_pe;
    if((read16(optional+70)&0x40u)||optional_size<160||
       read32(optional+112+5*8)||read32(optional+112+5*8+4))goto bad_pe;
    section_table=(size_t)pe_offset+24+optional_size;
    if(section_count>(pe_size-section_table)/40)goto bad_pe;
    for(uint16_t i=0;i<section_count;i++) {
        const unsigned char *section=pe+section_table+(size_t)i*40;
        uint32_t virtual_size=read32(section+8),rva=read32(section+12);
        uint32_t raw_size=read32(section+16),raw_offset=read32(section+20);
        uint32_t characteristics=read32(section+36);
        uint64_t span=virtual_size>raw_size?virtual_size:raw_size;
        if((uint64_t)rva+span>APP_LIMIT+PAGE_SIZE||
           (raw_size&&(uint64_t)raw_offset+raw_size>pe_size))goto bad_pe;
        if(span&&rva<lowest_rva)lowest_rva=rva;
        if(span&&(uint64_t)rva+virtual_size>highest_virtual)
            highest_virtual=(uint64_t)rva+virtual_size;
        if(characteristics&0x20000000u) {
            if(rva<lowest_rva)lowest_rva=rva;
            if((uint64_t)rva+span>code_end)code_end=(uint64_t)rva+span;
            if(rva==lowest_rva)executable_at_lowest=1;
        }
    }
    if(lowest_rva==UINT64_MAX||image_base>UINT64_MAX-lowest_rva||
       image_base+lowest_rva!=APP_BASE||
       !executable_at_lowest||entry_rva<lowest_rva||
       entry_rva>=code_end||code_end<=lowest_rva)goto bad_pe;
    entry_offset=(uint64_t)entry_rva-lowest_rva;
    code_size=(code_end-lowest_rva+PAGE_SIZE-1)&~(uint64_t)(PAGE_SIZE-1);
    memory_size=(highest_virtual-lowest_rva+PAGE_SIZE-1)&~(uint64_t)(PAGE_SIZE-1);
    if(payload_size>memory_size)memory_size=(payload_size+PAGE_SIZE-1)&~(uint64_t)(PAGE_SIZE-1);
    if(!code_size||code_size>payload_size||payload_size>APP_LIMIT||
       memory_size>APP_LIMIT||entry_offset>=code_size)goto bad_pe;
    image=(unsigned char *)calloc(1,HEADER_SIZE+payload_size);
    if(!image){fprintf(stderr,"bob64cc: out of memory\n");return -1;}
    image[0]='B';image[1]='6';image[2]='4';image[3]='E';
    write16(image+4,EXEC_VERSION);write16(image+6,HEADER_SIZE);
    write32(image+8,APP_ABI_VERSION);write64(image+16,payload_size);
    write64(image+24,memory_size);write64(image+32,entry_offset);
    write64(image+40,code_size);memcpy(image+HEADER_SIZE,payload,payload_size);
    write32(image+48,crc32(image+HEADER_SIZE,payload_size));
    out=fopen(path,"wb");
    if(!out){perror(path);free(image);return -1;}
    int failed=fwrite(image,1,HEADER_SIZE+payload_size,out)!=HEADER_SIZE+payload_size;
    if(fclose(out))failed=1;
    free(image);
    if(failed){fprintf(stderr,"bob64cc: failed writing %s\n",path);return -1;}
    printf("Built B64E v1: entry=0x%llx code=%llu file=%llu memory=%llu bytes\n",
           (unsigned long long)entry_offset,(unsigned long long)code_size,
           (unsigned long long)payload_size,(unsigned long long)memory_size);
    return 0;
bad_pe:
    fprintf(stderr,"bob64cc: compiler output is not a supported bob64 app image\n");
    return -1;
}
static int quote(char *out,size_t capacity,const char *value) {
    size_t length=strlen(value);
    if(length+3>capacity||strchr(value,'"'))return -1;
    out[0]='"';memcpy(out+1,value,length);out[length+1]='"';out[length+2]=0;
    return 0;
}
int main(int argc,char **argv) {
    const char *compiler=getenv("BOB64_CC"),*objcopy=getenv("BOB64_OBJCOPY");
    char q_source[4096],q_output[4096];
    char command[16384],section_arguments[192],exe_path[256],bin_path[256];
    size_t pe_size=0,payload_size=0;
    unsigned char *pe=NULL,*payload=NULL;
    int result=1;
    if(argc!=3) {
        fprintf(stderr,"Usage: bob64cc SOURCE.c OUTPUT.b64e\n");return 2;
    }
    if(!strcmp(argv[1],argv[2])) {
        fprintf(stderr,"bob64cc: source and output paths must be different\n");return 2;
    }
    if(!compiler||!*compiler)compiler="x86_64-w64-mingw32-gcc";
    if(!objcopy||!*objcopy)objcopy="objcopy";
    if(quote(q_source,sizeof(q_source),argv[1])||
       quote(q_output,sizeof(q_output),argv[2])||strchr(compiler,'"')||
       strchr(objcopy,'"')) {
        fprintf(stderr,"bob64cc: invalid or overly long path\n");return 2;
    }
    if(make_dir("build")&&errno!=EEXIST) {perror("build");return 1;}
    if(make_dir("build/bob64-app")&&errno!=EEXIST) {perror("build/bob64-app");return 1;}
    snprintf(exe_path,sizeof(exe_path),"build/bob64-app/image%s",APP_EXE);
    snprintf(bin_path,sizeof(bin_path),"build/bob64-app/image.bin");
    char q_exe[512],q_bin[512],q_entry[512];
    if(quote(q_exe,sizeof(q_exe),exe_path)||quote(q_bin,sizeof(q_bin),bin_path)||
       quote(q_entry,sizeof(q_entry),"bob64/app_entry.S")) {
        fprintf(stderr,"bob64cc: temporary path too long\n");return 2;
    }
    section_arguments[0]=0;
    snprintf(command,sizeof(command),
        "%s -I. -DBOB64_UEFI_ABI -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding "
        "-fno-builtin -fno-stack-protector -fno-stack-check -fno-unwind-tables "
        "-fno-asynchronous-unwind-tables -fPIC -mno-red-zone -nostdlib bob64/libc.c %s %s "
        "%s -Wl,--image-base,0x3ffffff000 -Wl,--disable-dynamicbase "
        "-Wl,--disable-reloc-section -Wl,-e,bob64_app_entry -o %s",
        compiler,q_entry,q_source,section_arguments,q_exe);
    if(system(command)!=0)goto done;
    pe=read_file(exe_path,&pe_size);
    int layout_result=pe?bss_layout_arguments(pe,pe_size,section_arguments,
                                             sizeof(section_arguments)):-1;
    if(layout_result<0)goto done;
    if(layout_result>0) {
        free(pe);pe=NULL;
        snprintf(command,sizeof(command),
            "%s -I. -DBOB64_UEFI_ABI -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding "
            "-fno-builtin -fno-stack-protector -fno-stack-check -fno-unwind-tables "
            "-fno-asynchronous-unwind-tables -fPIC -mno-red-zone -nostdlib bob64/libc.c %s %s "
            "%s -Wl,--image-base,0x3ffffff000 -Wl,--disable-dynamicbase "
            "-Wl,--disable-reloc-section -Wl,-e,bob64_app_entry -o %s",
            compiler,q_entry,q_source,section_arguments,q_exe);
        if(system(command)!=0)goto done;
        pe=read_file(exe_path,&pe_size);
        if(!pe)goto done;
    }
    snprintf(command,sizeof(command),"%s -O binary %s %s",objcopy,q_exe,q_bin);
    if(system(command)!=0)goto done;
    payload=read_file(bin_path,&payload_size);
    if(!pe||!payload||write_b64e(argv[2],pe,pe_size,payload,payload_size))goto done;
    result=0;
done:
    free(pe);free(payload);remove(exe_path);remove(bin_path);
    return result;
}
