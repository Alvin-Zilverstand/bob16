/* Included by main.c after ram is defined. Durable file device; guest owns shell. */
enum { SNAP_WORDS = 4312 };
static uint32_t kernelIdentity;
static uint32_t snapshotHash(const uint16_t *words, unsigned count) {
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<count;i++) {
        hash=(hash^(words[i]&255))*16777619u;
        hash=(hash^(words[i]>>8))*16777619u;
    }
    return hash;
}
static int snapshotValid(const uint16_t *words) {
    for(unsigned i=0;i<8;i++) {
        unsigned length=words[192+i],kind=words[200+i],used=words[208+i];
        if(used>1 || kind>1 || length>511)return 0;
        if(!used)continue;
        const uint16_t *name=words+i*24;unsigned n=0;
        while(n<24 && name[n]) {if(name[n]<33 || name[n]>126)return 0;n++;}
        if(!n || n==24 || words[216+i*512+length])return 0;
        if(!kind)for(unsigned k=0;k<length;k++)if(!words[216+i*512+k] || words[216+i*512+k]>255)return 0;
        for(unsigned j=0;j<i;j++)if(words[208+j]) {
            unsigned k=0;while(k<24 && name[k]==words[j*24+k] && name[k])k++;
            if(k<24 && name[k]==words[j*24+k])return 0;
        }
    }
    return 1;
}
static int snapshotService(unsigned descriptor, int operation) {
    if(programMode || descriptor<0x300 || descriptor+4>0xf000)return -3;
    unsigned addresses[4],sizes[4]={192,8,8,8};
    for(unsigned i=0;i<4;i++) {
        addresses[i]=(uint16_t)ram.memory[descriptor+i];
        if(addresses[i]<0x300 || addresses[i]+sizes[i]>0x9000)return -3;
        for(unsigned j=0;j<i;j++)if(addresses[i]<addresses[j]+sizes[j] && addresses[j]<addresses[i]+sizes[i])return -3;
    }
    const char *path=getenv("BOB16_STORAGE");if(!path || !*path)path="bob-files.b16";
    uint16_t words[SNAP_WORDS];unsigned position=0;
    if(operation==0) {
        for(unsigned i=0;i<4;i++)for(unsigned j=0;j<sizes[i];j++)words[position++]=(uint16_t)ram.memory[addresses[i]+j];
        for(unsigned i=0;i<4096;i++)words[position++]=(uint16_t)ram.memory[0xf000+i];
        if(!snapshotValid(words))return -2;
        size_t size=strlen(path)+5;char *temporary=malloc(size);if(!temporary)return -1;
        snprintf(temporary,size,"%s.tmp",path);
        FILE *file=fopen(temporary,"wb");if(!file){free(temporary);return -1;}
        uint32_t hash=snapshotHash(words,SNAP_WORDS);
        unsigned char header[16]={'B','1','6','S',1,0,0,0};
        for(unsigned i=0;i<4;i++){header[8+i]=(unsigned char)(kernelIdentity>>(8*i));header[12+i]=(unsigned char)(hash>>(8*i));}
        int okay=fwrite(header,1,16,file)==16;
        for(unsigned i=0;i<SNAP_WORDS;i++)if(fputc(words[i]&255,file)==EOF || fputc(words[i]>>8,file)==EOF)okay=0;
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
    unsigned char header[16];int valid=fread(header,1,16,file)==16;
    for(unsigned i=0;i<SNAP_WORDS;i++) {
        int lo=fgetc(file),hi=fgetc(file);if(lo==EOF || hi==EOF){valid=0;break;}
        words[i]=(uint16_t)(lo|(hi<<8));
    }
    if(fgetc(file)!=EOF)valid=0;
    fclose(file);
    if(!valid || memcmp(header,"B16S\1\0\0\0",8))return -2;
    uint32_t identity=0,hash=0;
    for(unsigned i=0;i<4;i++){identity|=(uint32_t)header[8+i]<<(8*i);hash|=(uint32_t)header[12+i]<<(8*i);}
    if(hash!=snapshotHash(words,SNAP_WORDS) || !snapshotValid(words))return -2;
    int changed=identity!=kernelIdentity;
    if(changed)for(unsigned i=0;i<8;i++)if(words[200+i])words[208+i]=0;
    position=0;
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<sizes[i];j++)ram.memory[addresses[i]+j]=(int16_t)words[position++];
    for(unsigned i=0;i<4096;i++)ram.memory[0xf000+i]=(int16_t)words[position++];
    return changed?1:0;
}
