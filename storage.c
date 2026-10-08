/* Included by main.c after ram is defined. Durable file device; guest owns shell. */
enum { SNAP_WORDS=4312, SNAP_WIDE_WORDS=8408, SNAP_META_WORDS=216,
       SNAP_OLD_FILE_WORDS=4096, SNAP_FILE_WORDS=8192 };
static uint32_t kernelIdentity;
static uint32_t snapshotHash(const uint32_t *words,unsigned count) {
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<count;i++) {
        hash=(hash^(words[i]&255))*16777619u;
        hash=(hash^((words[i]>>8)&255))*16777619u;
        if(cpu32) {hash=(hash^((words[i]>>16)&255))*16777619u;hash=(hash^(words[i]>>24))*16777619u;}
    }
    return hash;
}
/* Layout 0: fixed 512-word slots; 1: packed 4096-word B32S v2; 2: packed v3. */
static int snapshotValidFormat(const uint32_t *words,int layout) {
    unsigned capacity=layout==2?SNAP_FILE_WORDS:SNAP_OLD_FILE_WORDS;
    unsigned packedOffset=0;int packed=layout!=0;
    for(unsigned i=0;i<8;i++) {
        unsigned length=words[192+i],kind=words[200+i],used=words[208+i];
        if(used>1 || kind>(cpu32?(packed?3u:2u):1u) || length>=(packed?capacity:512u))return 0;
        if(!used)continue;
        const uint32_t *name=words+i*24;unsigned n=0;
        while(n<24&&name[n]) {if(name[n]<33||name[n]>126)return 0;n++;}
        unsigned contentOffset=packed?packedOffset:i*512;
        if(!n||n==24||contentOffset+length>=capacity||words[SNAP_META_WORDS+contentOffset+length])return 0;
        if(!kind)for(unsigned k=0;k<length;k++)if(!words[SNAP_META_WORDS+contentOffset+k]||words[SNAP_META_WORDS+contentOffset+k]>255)return 0;
        if(packed)packedOffset+=length+1;
        for(unsigned j=0;j<i;j++)if(words[208+j]) {
            unsigned k=0;while(k<24&&name[k]==words[j*24+k]&&name[k])k++;
            if(k<24&&name[k]==words[j*24+k])return 0;
        }
    }
    return !packed||packedOffset<=capacity;
}
static int snapshotValid(const uint32_t *words) { return snapshotValidFormat(words,2); }
static int snapshotService(unsigned descriptor,int operation) {
    if(programMode||descriptor<0x300||descriptor>0xf000-4)return -3;
    unsigned addresses[4],sizes[4]={192,8,8,8};
    for(unsigned i=0;i<4;i++) {
        addresses[i]=cpu32?(uint32_t)memoryRead(descriptor+i):(uint16_t)ram.memory[descriptor+i];
        uint64_t end=(uint64_t)addresses[i]+sizes[i];
        int lowArray=addresses[i]>=0x300&&end<=(cpu32?0xc000u:0xa000u);
        uint32_t kernelStart=bootMode?bootOrigin:(assembledWide?assembledOrigin:0);
        uint64_t kernelEnd=bootMode?(uint64_t)bootOrigin+bootWords:(assembledWide?(uint64_t)assembledOrigin+assembledWords:0);
        int kernelArray=cpu32&&addresses[i]>=kernelStart&&end<=kernelEnd;
        if(!lowArray&&!kernelArray)return -3;
        for(unsigned j=0;j<i;j++)if(addresses[i]<addresses[j]+sizes[j]&&addresses[j]<addresses[i]+sizes[i])return -3;
    }
    const char *path=getenv("BOB16_STORAGE");if(!path||!*path)path=cpu32?"bob-files.b32":"bob-files.b16";
    uint32_t words[SNAP_WIDE_WORDS];memset(words,0,sizeof(words));unsigned position=0;
    if(operation==0) {
        unsigned fileWords=cpu32?SNAP_FILE_WORDS:SNAP_OLD_FILE_WORDS;
        unsigned snapshotWords=SNAP_META_WORDS+fileWords;
        for(unsigned i=0;i<4;i++)for(unsigned j=0;j<sizes[i];j++)words[position++]=(uint32_t)wordValue(memoryRead(addresses[i]+j));
        for(unsigned i=0;i<fileWords;i++)words[position++]=(uint32_t)wordValue(cpu32?memoryRead(0x1e000+i):ram.memory[0xf000+i]);
        if(!cpu32)for(unsigned i=0;i<snapshotWords;i++)words[i]&=65535u;
        if(!(cpu32?snapshotValid(words):snapshotValidFormat(words,0)))return -2;
        size_t size=strlen(path)+5;char *temporary=malloc(size);if(!temporary)return -1;
        snprintf(temporary,size,"%s.tmp",path);
        FILE *file=fopen(temporary,"wb");if(!file){free(temporary);return -1;}
        uint32_t hash=snapshotHash(words,snapshotWords);
        unsigned char header[16]={'B','1','6','S',1,0,0,0};
        if(cpu32){header[1]='3';header[2]='2';header[4]=3;}
        for(unsigned i=0;i<4;i++){header[8+i]=(unsigned char)(kernelIdentity>>(8*i));header[12+i]=(unsigned char)(hash>>(8*i));}
        int okay=fwrite(header,1,16,file)==16;
        for(unsigned i=0;i<snapshotWords;i++)for(unsigned b=0;b<(cpu32?4u:2u);b++)if(fputc((words[i]>>(8*b))&255,file)==EOF)okay=0;
        if(fclose(file))okay=0;
        if(okay) {
#ifdef _WIN32
            okay=MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
            okay=rename(temporary,path)==0;
#endif
        }
        if(!okay)remove(temporary);
        free(temporary);return okay?0:-1;
    }
    if(operation!=1)return -3;
    FILE *file=fopen(path,"rb");if(!file)return -1;
    unsigned char header[16]={0};int valid=fread(header,1,16,file)==16,layout=-1;
    unsigned snapshotWords=0,wordBytes=cpu32?4:2;
    if(valid) {
        if(!cpu32&&!memcmp(header,"B16S\1\0\0\0",8)){layout=0;snapshotWords=SNAP_WORDS;}
        else if(cpu32&&!memcmp(header,"B32S\1\0\0\0",8)){layout=0;snapshotWords=SNAP_WORDS;}
        else if(cpu32&&!memcmp(header,"B32S\2\0\0\0",8)){layout=1;snapshotWords=SNAP_WORDS;}
        else if(cpu32&&!memcmp(header,"B32S\3\0\0\0",8)){layout=2;snapshotWords=SNAP_WIDE_WORDS;}
        else valid=0;
    }
    for(unsigned i=0;valid&&i<snapshotWords;i++) {
        for(unsigned b=0;b<wordBytes;b++){int byte=fgetc(file);if(byte==EOF){valid=0;break;}words[i]|=(uint32_t)byte<<(8*b);}
    }
    if(fgetc(file)!=EOF)valid=0;
    if(ferror(file))valid=0;
    if(fclose(file))valid=0;
    if(!valid)return -2;
    uint32_t identity=0,hash=0;
    for(unsigned i=0;i<4;i++){identity|=(uint32_t)header[8+i]<<(8*i);hash|=(uint32_t)header[12+i]<<(8*i);}
    if(hash!=snapshotHash(words,snapshotWords)||!snapshotValidFormat(words,layout))return -2;
    int changed=identity!=kernelIdentity;
    if(cpu32&&layout==0) {
        unsigned destinationOffset=0;
        for(unsigned i=0;i<8;i++)if(words[208+i]) {
            unsigned length=words[192+i];
            for(unsigned j=0;j<=length;j++)words[SNAP_META_WORDS+destinationOffset+j]=words[SNAP_META_WORDS+i*512+j];
            destinationOffset+=length+1;
        }
        layout=1;
    }
    if(changed&&cpu32) {
        unsigned sourceOffset=0,destinationOffset=0;
        for(unsigned i=0;i<8;i++)if(words[208+i]) {
            unsigned length=words[192+i];
            if(words[200+i])words[208+i]=0;
            else {
                for(unsigned j=0;j<=length;j++)words[SNAP_META_WORDS+destinationOffset+j]=words[SNAP_META_WORDS+sourceOffset+j];
                destinationOffset+=length+1;
            }
            sourceOffset+=length+1;
        }
    } else if(changed)for(unsigned i=0;i<8;i++)if(words[200+i])words[208+i]=0;
    position=0;
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<sizes[i];j++)memoryWrite(addresses[i]+j,wordValue(words[position++]));
    if(cpu32) {
        for(unsigned i=0;i<SNAP_FILE_WORDS;i++)memoryWrite(0x1e000+i,i<snapshotWords-SNAP_META_WORDS?wordValue(words[SNAP_META_WORDS+i]):0);
    } else for(unsigned i=0;i<SNAP_OLD_FILE_WORDS;i++)ram.memory[0xf000+i]=wordValue(words[SNAP_META_WORDS+i]);
    return changed?1:0;
}
