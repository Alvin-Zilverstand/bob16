/* CPU migration checks; include the real implementation rather than a model. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define BOB_TESTING
#define main bob_emulator_main
#include "../main.c"
#undef main
#include <limits.h>
#define TEST_APP_BASE 0x20000u
static int checks;
static int storage_path(const char *path) {
#ifdef _WIN32
    return _putenv_s("BOB16_STORAGE",path);
#else
    return *path?setenv("BOB16_STORAGE",path,1):unsetenv("BOB16_STORAGE");
#endif
}
static void verify(int okay,const char *what) {
    checks++;if(!okay){fprintf(stderr,"CPU check failed: %s\n",what);exit(1);}
}
static void reset_cpu(int wide) {memoryReset();memset(&cpu,0,sizeof(cpu));memset(&ram,0,sizeof(ram));cpu32=wide;programMode=false;application32=false;legacyProgram32=false;}
static void step(unsigned instruction) {ram.memory[cpu.pc]=instruction;cpuCycle();}
static void put16(FILE *f,unsigned value) {fputc(value&255,f);fputc((value>>8)&255,f);}
static void put32(FILE *f,uint32_t value) {put16(f,value);put16(f,value>>16);}
static int assembly_text(const char *text) {
    FILE *f=tmpfile();if(!f)exit(1);fputs(text,f);rewind(f);
    reset_cpu(0);int result=assembleSource(f);fclose(f);return result;
}
static int assembly_bytes(const unsigned char *data,size_t length) {
    FILE *f=tmpfile();if(!f)exit(1);
    if(fwrite(data,1,length,f)!=length){fclose(f);exit(1);}
    rewind(f);reset_cpu(0);int result=assembleSource(f);fclose(f);return result;
}
static void bad_image(const char *executable,const unsigned char *bytes,size_t length) {
    FILE *f=fopen("build/cpu-bad.b32","wb");if(!f)exit(1);
    if(fwrite(bytes,1,length,f)!=length || fclose(f))exit(1);
    char command[4096];snprintf(command,sizeof(command),"\"%s\" --load-fixture build/cpu-bad.b32 > build/cpu-bad.out 2>&1",executable);
    verify(system(command)!=0,"invalid wide image rejected");
}
int main(int argc,char **argv) {
    if(argc>1 && !strcmp(argv[1],"--emulator"))return bob_emulator_main(argc-1,argv+1);
    if(argc==3 && !strcmp(argv[1],"--load-fixture")){bootImage(argv[2]);return 0;}
    if(argc==3 && !strcmp(argv[1],"--disk-argument-fixture")) {
        int argument=argv[2][0]-'0';if(argument<0 || argument>2)return 2;
        reset_cpu(1);bootMode=true;bootWords=1;bootDisk[0]=42;
        cpu.regFile[0]=argument==0?0x10000:0x300;cpu.regFile[1]=0;cpu.regFile[2]=1;
        cpu.regFile[argument]|=0x10000;step(0xf400);
        return argument==0 && memoryRead(0x10000)!=42?1:0;
    }
    if(argc==2 && !strcmp(argv[1],"--puts-fixture")) {
        reset_cpu(1);cpu.regFile[0]=0x10000;
        memoryWrite(0x10000,0x10000);memoryWrite(0x10001,'b');memoryWrite(0x10002,'o');
        memoryWrite(0x10003,'b');memoryWrite(0x10004,'!');step(0xf200);
        return cpu.ir==0xf200?0:1;
    }
    for(unsigned bits=1;bits<=16;bits++) {
        verify(sext((int16_t)((1u<<bits)-1),bits)==-1,"negative immediate");
        verify(sext((int16_t)((1u<<(bits-1))-1),bits)==(int)((1u<<(bits-1))-1),"positive immediate");
    }
    unsigned long limit=123;
    const char *invalid_limits[]={"","0","-1","+1"," 1","1 ","1x","999999999999999999999999999999999999999999"};
    for(unsigned i=0;i<sizeof(invalid_limits)/sizeof(invalid_limits[0]);i++) {
        verify(!parseCycleLimit(invalid_limits[i],&limit) && limit==123,"invalid cycle limit leaves output intact");
    }
    verify(parseCycleLimit("1",&limit) && limit==1,"minimum cycle limit");
    char maximum[64];snprintf(maximum,sizeof(maximum),"%lu",ULONG_MAX);
    verify(parseCycleLimit(maximum,&limit) && limit==ULONG_MAX,"maximum cycle limit");
    reset_cpu(0);verify(snapshotService(UINT_MAX,1)==-3,"snapshot descriptor rejects unsigned overflow");
    verify(snapshotService(0xf000-3,1)==-3,"snapshot descriptor end bound");
        reset_cpu(1);
    ram.memory[0x300]=0x400;ram.memory[0x301]=0x500;ram.memory[0x302]=0x510;ram.memory[0x303]=0x520;
    ram.memory[0x300]=0x1000400;
    verify(snapshotService(0x300,0)==-3,"wide snapshot rejects truncated address in descriptor");
    ram.memory[0x300]=0x400;
    cpu.regFile[0]=0x1000300;cpu.regFile[1]=0;step(0xf700);
    verify(cpu.regFile[0]==-3,"wide storage trap rejects truncated descriptor address");
    ram.memory[0x400]='b';ram.memory[0x401]='o';ram.memory[0x402]='b';ram.memory[0x403]='!';
    ram.memory[0x500]=1;ram.memory[0x510]=1;ram.memory[0x520]=1;ram.memory[0xf000]=0x12345678;ram.memory[0xf001]=0;
    const char *source_name="bobs.c",*source_text="bob!";
    for(unsigned i=0;source_name[i];i++)ram.memory[0x418+i]=source_name[i];
    ram.memory[0x501]=4;ram.memory[0x521]=1;
    for(unsigned i=0;source_text[i];i++)ram.memory[0xf002+i]=source_text[i];
    ram.memory[0xf006]=0;
    if(storage_path("build/cpu-snapshot.b32"))return 1;
    verify(snapshotService(0x300,0)==0,"wide snapshot save");
    ram.memory[0xf000]=0;
    verify(snapshotService(0x300,1)==0 && ram.memory[0xf000]==0x12345678,"wide snapshot restores all 32 bits");
        FILE *saved=fopen("build/cpu-snapshot.b32","r+b");if(!saved)return 1;
    if(fseek(saved,16+216*4+3,SEEK_SET))return 1;
    if(fputc(0x13,saved)==EOF || fclose(saved))return 1;
    ram.memory[0xf000]=42;
    verify(snapshotService(0x300,1)==-2 && ram.memory[0xf000]==42,"wide snapshot checksum protects upper byte and RAM");
    ram.memory[0xf000]=0x12345678;
    verify(snapshotService(0x300,0)==0,"wide snapshot resave");
    FILE *old_snapshot=fopen("build/cpu-snapshot.b32","r+b");if(!old_snapshot)return 1;
    uint32_t old_words[SNAP_WORDS];if(fseek(old_snapshot,16,SEEK_SET))return 1;
    for(unsigned i=0;i<SNAP_WORDS;i++){unsigned char b[4];if(fread(b,1,4,old_snapshot)!=4)return 1;old_words[i]=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
    for(unsigned i=0;i<=4;i++)old_words[216+512+i]=old_words[218+i];
    uint32_t old_hash=snapshotHash(old_words,SNAP_WORDS);if(fseek(old_snapshot,4,SEEK_SET)||fputc(1,old_snapshot)==EOF||fseek(old_snapshot,12,SEEK_SET))return 1;
    for(unsigned i=0;i<4;i++)if(fputc((old_hash>>(8*i))&255,old_snapshot)==EOF)return 1;
    if(fseek(old_snapshot,16,SEEK_SET))return 1;
    for(unsigned i=0;i<SNAP_WORDS;i++)for(unsigned b=0;b<4;b++)if(fputc((old_words[i]>>(8*b))&255,old_snapshot)==EOF)return 1;
    if(fclose(old_snapshot))return 1;
    ram.memory[0xf000]=0;ram.memory[0xf002]=0;
    verify(snapshotService(0x300,1)==0 && ram.memory[0xf000]==0x12345678 && ram.memory[0xf002]=='b',"legacy fixed-slot B32S snapshot converts to packed filesystem layout");
    cpu32=false;ram.memory[0xf000]=42;
    verify(snapshotService(0x300,1)==-2 && ram.memory[0xf000]==42,"legacy rejects wide snapshot without modifying RAM");
    cpu32=true;kernelIdentity++;
    verify(snapshotService(0x300,1)==1,"wide snapshot reports changed kernel identity");
    verify(ram.memory[0x520]==0 && ram.memory[0x521]==1 && ram.memory[0xf000]=='b' && ram.memory[0xf003]=='!',"wide kernel change drops binaries and preserves text");
    kernelIdentity--;
    if(storage_path(""))return 1;
    uint32_t snapshot[SNAP_WORDS]={0};
    verify(snapshotValid(snapshot),"empty snapshot metadata");
    snapshot[0]='b';snapshot[1]='o';snapshot[2]='b';snapshot[3]='!';snapshot[192]=4;snapshot[208]=1;
    snapshot[216]='b';snapshot[217]='o';snapshot[218]='b';snapshot[219]='!';
    verify(snapshotValid(snapshot),"valid text snapshot metadata");
    snapshot[218]=0;verify(!snapshotValid(snapshot),"text snapshot rejects embedded terminator");snapshot[218]='b';
    snapshot[220]=1;verify(!snapshotValid(snapshot),"snapshot rejects missing content terminator");snapshot[220]=0;
    snapshot[192]=512;verify(!snapshotValid(snapshot),"snapshot rejects excessive length");snapshot[192]=4;
    snapshot[208]=2;verify(!snapshotValid(snapshot),"snapshot rejects invalid used flag");snapshot[208]=1;
    snapshot[200]=2;verify(snapshotValid(snapshot),"bob32 snapshots accept native program files");
    snapshot[200]=4;verify(!snapshotValid(snapshot),"snapshot rejects invalid kind");snapshot[200]=0;
    memcpy(snapshot+24,snapshot,24*sizeof(uint32_t));snapshot[209]=1;
    verify(!snapshotValid(snapshot),"snapshot rejects duplicate names");snapshot[209]=0;
    verify(assembly_text(".stringz bob!\n.fill 1234\n")==0 && ram.memory[4]==0 && ram.memory[5]==0x1234,"string directive emits exactly one terminator");
    verify(assembly_text("add r0 r1 r2\r\n.fill ffff\r\n")==0 && ram.memory[1]==0xffff,"CRLF token separation");
    verify(assembly_text(".fill ffffg\n")<0,"fill rejects trailing text");
    const unsigned char embedded_nul[]={'.','f','i','l','l',' ','1',0,' ','x','\n'};
    verify(assembly_bytes(embedded_nul,sizeof(embedded_nul))<0,"assembly rejects embedded zero byte");
    verify(assembly_text(".fill 10000\n")<0,"fill rejects truncation");
    verify(assembly_text(".fill -1\n")<0,"fill rejects signed text");
    verify(assembly_text("nop\n.fill 1234\n")==0 && ram.memory[0]==0 && ram.memory[1]==0x1234,"documented nop assembles");
    verify(assembly_text("nop r0\n")<0,"nop rejects operands");
    char boundary_source[300];memset(boundary_source,' ',252);strcpy(boundary_source+252,"nop\r\n.fill 1234\n");
    verify(assembly_text(boundary_source)==0 && ram.memory[0]==0 && ram.memory[1]==0x1234,"exact-limit CRLF source line keeps following instruction");
    memset(boundary_source,' ',253);strcpy(boundary_source+253,"nop\r\n.fill 1234\n");
    verify(assembly_text(boundary_source)<0,"source line over the limit remains rejected");
    const char *bad_operands[]={"add r0 r1 r8\n","add r0 bogus\n","and r0 r1 1x\n","and r0 r9\n",
        "not r0 r8\n","not r0 r1 ignored\n","not\n","ld r0 bogus\n","ld r8 0\n","ld xx 0\n",
        "ldi r8 0\n","ldr r0 r8 0\n","ldr r8 r0 0\n","ldr r0 r1 --1\n",
        "st r8 0\n","st xx 0\n","st r0 1x\n","sti r8 0\n","sti r0 bogus\n",
        "str r8 r0 0\n","str r0 r8 0\n","str r0 r1 r8\n","br np 1x\n","jsr bogus\n","lea r0 bogus\n",
        "trap nonsense\n","trap 12\n","trap 15\n",
        "br nx 1\n","add r0 999999999999999999999\n"};
    for(unsigned i=0;i<sizeof(bad_operands)/sizeof(bad_operands[0]);i++)verify(assembly_text(bad_operands[i])<0,"invalid instruction operand rejected");
    verify(assembly_text("add r0 100\nadd r0 -100\njsr -1\njsrr r3\n")==0 && ram.memory[0]==0x11bf && ram.memory[1]==0x11c0 && ram.memory[2]==0xcfff && ram.memory[3]==0xc300,"decimal clamping and call encodings preserved");
    int immediate=7;
    verify(parseImmediate("+123",&immediate) && immediate==123,"signed decimal operand");
    verify(!parseImmediate("-",&immediate) && immediate==123,"missing digits reject without modifying result");
    char oversized[300];memset(oversized,' ',sizeof(oversized));memcpy(oversized,".fill 1",7);oversized[298]='\n';oversized[299]=0;
    verify(assembly_text(oversized)<0,"oversized physical line rejects before splitting");
    FILE *full=tmpfile();if(!full)return 1;
    for(unsigned i=0;i<65536;i++)fputs(".fill 0\n",full);
    rewind(full);reset_cpu(0);verify(assembleSource(full)==0,"full address-space assembly fits exactly");fclose(full);
    full=tmpfile();if(!full)return 1;
    for(unsigned i=0;i<65535;i++)fputs(".fill 0\n",full);
    fputs(".stringz bob!\n",full);rewind(full);reset_cpu(0);
    verify(assembleSource(full)==-7 && ram.memory[65535]==0,"oversized string rejected before writing");fclose(full);
    reset_cpu(0);cpu.regFile[0]=32767;step(0x1181);
    verify(cpu.regFile[0]==-32768 && cpu.cc[N],"bob16 wrap and negative flag");
    reset_cpu(1);cpu.regFile[0]=32767;step(0x1181);
    verify(cpu.regFile[0]==32768 && cpu.cc[P],"bob32 crosses 16-bit boundary");
    cpu.regFile[0]=INT32_MAX;step(0x1181);
    verify(cpu.regFile[0]==INT32_MIN && cpu.cc[N],"bob32 wraps at 32 bits");
    cpu.regFile[0]=-1;step(0x1181);
    verify(cpu.regFile[0]==0 && cpu.cc[Z],"32-bit zero flag");
    cpu.regFile[0]=0x12345678;cpu.regFile[1]=100;step(0x9040);
    verify(ram.memory[100]==0x12345678,"wide store");step(0x6440);
    verify(cpu.regFile[2]==0x12345678,"wide load");
    reset_cpu(0);cpu.pc=0x8000;step(0xc800);
    verify(cpu.regFile[7]==(int16_t)0x8001,"legacy signed return-address register");
    reset_cpu(1);cpu.pc=0x8000;step(0xc800);
    verify(cpu.regFile[7]==0x8001,"wide return-address register");
    reset_cpu(1);memoryWrite(TEST_APP_BASE,0x4001);memoryWrite(TEST_APP_BASE+1,0xe000);memoryWrite(TEST_APP_BASE+2,0x12345678);
    cpu.regFile[0]=TEST_APP_BASE;step(0xf600);
    verify(cpu.regFile[0]==0x12345678 && cpu.pc==1 && !programMode,"wide supervised return and parent restoration");
    reset_cpu(1);memoryWrite(TEST_APP_BASE,0x1000f000);application32=1;
    cpu.regFile[2]=42;
    verify(runProgram(TEST_APP_BASE)==-2 && cpu.regFile[2]==42 && cpu.pc==0 && !programMode,"wide instruction upper bits fault and restore parent");
    application32=0;
    reset_cpu(0);ram.memory[0xa000]=(int16_t)0xe000;
    verify(runProgram(0xa000)==0,"legacy signed instruction representation remains valid");
    reset_cpu(0);memoryWrite(0xa000,0x9200);memoryWrite(0xa001,0xf000);cpu.regFile[0]=0x9800;cpu.regFile[1]=42;
    verify(runProgram(0xa000)==0x9800 && memoryRead(0x9800)==42,"bob16 applications retain access to resident compiler variable slots");
    reset_cpu(0);memoryWrite(0xa000,0x9200);memoryWrite(0xa001,0xf000);cpu.regFile[0]=0x9810;cpu.regFile[1]=42;
    verify(runProgram(0xa000)==-2 && memoryRead(0x9810)!=42,"bob16 applications cannot write kernel memory around compiler slots");
    const unsigned malformed[]={0x0001,0x1001,0x2001,0x3120,0xc001,0xe001};
    reset_cpu(1);cpu.regFile[0]=0x1009000;cpu.regFile[2]=42;memoryWrite(0x10000,0xf600);cpu.pc=0x10000;cpuCycle();
    verify(cpu.regFile[0]==-2 && cpu.regFile[2]==42 && !programMode,"wide run trap rejects truncated entry address");
    for(unsigned argument=1;argument<2;argument++) {
        reset_cpu(1);memoryWrite(TEST_APP_BASE,0xf300);memoryWrite(TEST_APP_BASE+1,0xe000);memoryWrite(0x10010,77);application32=1;
        cpu.regFile[0]=0x10010;cpu.regFile[1]=2;cpu.regFile[argument]|=0x10000;
        verify(runProgram(TEST_APP_BASE)==-2 && memoryRead(0x10010)==77 && !programMode,"wide input trap rejects truncated arguments before writing");
        application32=0;
    }
    reset_cpu(1);memoryWrite(TEST_APP_BASE,0xf300);memoryWrite(TEST_APP_BASE+1,0xf000);application32=1;
    cpu.regFile[0]=TEST_APP_BASE;cpu.regFile[1]=2;
    int input_status=runProgram(TEST_APP_BASE);
    verify(input_status==TEST_APP_BASE && memoryRead(TEST_APP_BASE)==0,"wide input trap writes to full-width guest address");
    application32=0;
    reset_cpu(1);cpu.regFile[0]=0x10000;step(0xb000);
    verify(cpu.pc==0x10000,"wide JMP preserves 32-bit address");
    reset_cpu(1);cpu.regFile[0]=0x10000;step(0xc000);
    verify(cpu.pc==0x10000 && cpu.regFile[7]==1,"wide JSRR preserves 32-bit address");
    reset_cpu(1);cpu.regFile[7]=0x10000;step(0xe000);
    verify(cpu.pc==0x10000,"wide RET preserves 32-bit address");
    reset_cpu(1);cpu.regFile[0]=0x12345000;cpu.regFile[1]=0x12345678;step(0x9200);
    verify(memoryRead(0x12345000)==0x12345678,"wide STR writes above 64K words");
    step(0x6400);verify(cpu.regFile[2]==0x12345678,"wide LDR reads above 64K words");
    reset_cpu(1);memoryWrite(0x10000,0x12345678);memoryWrite(0x10100,0x5400);memoryWrite(0x10101,0x10000);
    cpu.pc=0x10100;cpuCycle();verify(cpu.regFile[2]==0x12345678,"wide LDI follows full-width pointer");
    memoryWrite(0x10200,0x8200);memoryWrite(0x10201,0x10000);cpu.regFile[1]=0x87654321;cpu.pc=0x10200;cpuCycle();
    verify(memoryRead(0x10000)==(int32_t)0x87654321u,"wide STI follows full-width pointer");
    reset_cpu(1);memoryWrite(TEST_APP_BASE,0x9200);memoryWrite(TEST_APP_BASE+1,0x6400);
    memoryWrite(TEST_APP_BASE+2,0x10a0);memoryWrite(TEST_APP_BASE+3,0xf000);
    cpu.regFile[0]=0x22345;cpu.regFile[1]=0x12345678;
    application32=1;
    verify(runProgram(TEST_APP_BASE)==0x12345678 && memoryRead(0x22345)==0x12345678,
           "supervised bob32 program reads and writes full-width data addresses");
    reset_cpu(1);memoryWrite(TEST_APP_BASE,0xf600);memoryWrite(TEST_APP_BASE+1,0x400e);
    memoryWrite(TEST_APP_BASE+2,0x9200);memoryWrite(TEST_APP_BASE+3,0xf000);
    memoryWrite(TEST_APP_BASE+0x10,TEST_APP_BASE+0x20);cpu.regFile[0]=TEST_APP_BASE;cpu.regFile[1]=42;application32=1;
    int nested_status=runProgram(TEST_APP_BASE);
    verify(nested_status==TEST_APP_BASE+0x20 && memoryRead(TEST_APP_BASE+0x20)==42,
           "rejected nested run leaves native application memory mode intact");
    application32=0;
    reset_cpu(1);memoryWrite(TEST_APP_BASE,0x9200);memoryWrite(TEST_APP_BASE+1,0xf000);
    cpu.regFile[0]=0xfffff;cpu.regFile[1]=0x12345678;application32=1;
    verify(runProgram(TEST_APP_BASE)==0xfffff && memoryRead(0xfffff)==0x12345678,
           "supervised bob32 program may write the last word in its region");
    const uint32_t forbidden_app_addresses[]={0xffff,0x10000,0x1ffff,0x100000,0x110000,0x12345000,UINT32_MAX};
    for(unsigned i=0;i<sizeof(forbidden_app_addresses)/sizeof(forbidden_app_addresses[0]);i++) {
        reset_cpu(1);memoryWrite(TEST_APP_BASE,0x9200);memoryWrite(TEST_APP_BASE+1,0xf000);
        cpu.regFile[0]=(int32_t)forbidden_app_addresses[i];cpu.regFile[1]=0x76543210;application32=1;
        verify(runProgram(TEST_APP_BASE)==-2 && memoryRead(forbidden_app_addresses[i])!=0x76543210,
               "supervised bob32 store rejects addresses outside app and stack region");
    }
    application32=0;
    reset_cpu(1);bootWideHeader=false;bootOrigin=0;bootWords=0;memoryWrite(0xc200,0xd002);memoryWrite(0xc201,0xf200);memoryWrite(0xc202,0xf000);
    memoryWrite(0xc203,'b');memoryWrite(0xc204,'o');memoryWrite(0xc205,'b');memoryWrite(0xc206,'!');memoryWrite(0xc207,0);
    verify(runProgram(0xc200)==33 && !programMode,"bob32 kernel runs a legacy 16-bit program outside its image");
    verify(runProgram(0x9000)==-1 && runProgram(0xb000)==-1 && runProgram(0xb800)==-1,"bob32 kernel rejects legacy entries that overlap kernel image or reserved gap");
    reset_cpu(1);bootWideHeader=false;bootOrigin=0;bootWords=0;memoryWrite(0xc200,0x9200);memoryWrite(0xc201,0xf000);
    cpu.regFile[0]=0xc200;cpu.regFile[1]=0x1234;
    verify(runProgram(0xc200)==0xc200 && memoryRead(0xc200)==0x1234,"bob32 legacy app may store at the first reserved program word");
    reset_cpu(1);memoryWrite(0xc200,0x9200);memoryWrite(0xc201,0xf000);cpu.regFile[0]=0xc1ff;cpu.regFile[1]=0x5678;
    verify(runProgram(0xc200)==-2 && memoryRead(0xc1ff)!=0x5678,"bob32 legacy app cannot write below its relocated region");
    reset_cpu(1);memoryWrite(0xc200,0x9200);memoryWrite(0xc201,0xf000);cpu.regFile[0]=0xd000;cpu.regFile[1]=0x5678;
    verify(runProgram(0xc200)==0xd000 && memoryRead(0xd000)==0x5678,"bob32 legacy app retains shared heap data access");
    reset_cpu(0);cpu.regFile[0]=0x10000;cpu.regFile[1]=42;step(0x9200);
    verify(ram.memory[0]==42,"legacy STR retains 16-bit address wrapping");
    for(unsigned wide=0;wide<2;wide++)for(unsigned i=0;i<sizeof(malformed)/sizeof(malformed[0]);i++) {
        reset_cpu(wide);if(wide)application32=1;cpu.regFile[2]=42;uint32_t entry=wide?TEST_APP_BASE:0xa000;memoryWrite(entry,wordValue(malformed[i]));
        verify(runProgram(entry)==-2 && cpu.regFile[2]==42 && cpu.pc==0 && !programMode,"reserved instruction bits fault and restore parent");
        application32=0;
    }
    char command[4096];
    snprintf(command,sizeof(command),"\"%s\" --disk-argument-fixture 0 > build/cpu-disk-argument.out 2>&1",argv[0]);
    verify(system(command)==0,"wide boot disk trap writes to full-width destination");
    for(unsigned argument=1;argument<3;argument++) {
        snprintf(command,sizeof(command),"\"%s\" --disk-argument-fixture %u > build/cpu-disk-argument.out 2>&1",argv[0],argument);
        verify(system(command)!=0,"wide boot disk trap rejects truncated arguments");
        FILE *fault=fopen("build/cpu-disk-argument.out","rb");if(!fault)return 1;
        char diagnostic[256];size_t used=fread(diagnostic,1,sizeof(diagnostic)-1,fault);fclose(fault);diagnostic[used]=0;
        verify(strstr(diagnostic,"Invalid boot disk argument")!=NULL,"disk argument fixture reaches the intended trap fault");
    }
    snprintf(command,sizeof(command),"\"%s\" --puts-fixture > build/cpu-puts.out",argv[0]);
    verify(system(command)==0,"wide string trap preserves instruction state");
    FILE *output=fopen("build/cpu-puts.out","rb");if(!output)return 1;
    unsigned char bytes[7];size_t length=fread(bytes,1,sizeof(bytes),output);fclose(output);
#ifdef _WIN32
    verify(length==7 && !memcmp(bytes,"\0bob!\r\n",7),"wide string terminator compares entire word");
#else
    verify(length==6 && !memcmp(bytes,"\0bob!\n",6),"wide string terminator compares entire word");
#endif
    FILE *f=fopen("build/cpu-wide.b32","wb");if(!f)return 1;
    uint32_t words[]={0x4001,0,0x12345678};unsigned sum=0;
    for(unsigned i=0;i<3;i++)sum+=words[i];
    fwrite("B32K",1,4,f);put16(f,1);put16(f,0x300);put16(f,0x300);put16(f,3);put16(f,sum);put16(f,sum>>16);
    for(unsigned i=0;i<3;i++){put16(f,words[i]);put16(f,words[i]>>16);}fclose(f);
    unsigned char image[29];f=fopen("build/cpu-wide.b32","rb");if(!f)return 1;
    verify(fread(image,1,28,f)==28,"wide fixture length");fclose(f);
    bad_image(argv[0],image,14);bad_image(argv[0],image,27);
    image[12]^=1;bad_image(argv[0],image,28);image[12]^=1;
    image[28]=0;bad_image(argv[0],image,29);
    unsigned char entry=image[8];image[8]=0xff;image[9]=0xff;bad_image(argv[0],image,28);image[8]=entry;image[9]=3;
    image[4]=2;bad_image(argv[0],image,28);image[4]=1;
    bootImage("build/cpu-wide.b32");
    verify(cpu32 && bootDisk[2]==0x12345678,"wide image loader");
    for(unsigned i=0;i<6;i++)cpuCycle();
    verify(cpu.pc==0x300 && ram.memory[0x302]==0x12345678,"wide guest boot disk transfer");
    cpuCycle();verify(cpu.regFile[0]==0x12345678,"wide image native execution");
    f=fopen("build/cpu-wide-v2.b32","wb");if(!f)return 1;
    uint32_t wide_words[]={0x1080,0x12345678};uint32_t wide_sum=wide_words[0]+wide_words[1];
    fwrite("B32K",1,4,f);put16(f,2);put16(f,24);put32(f,0x10000);put32(f,0x10000);
    put32(f,2);put32(f,wide_sum);put32(f,wide_words[0]);put32(f,wide_words[1]);fclose(f);
    bootImage("build/cpu-wide-v2.b32");
    verify(cpu32 && cpu.pc==0x10000 && memoryRead(0x10001)==0x12345678,"B32K v2 loads at a 32-bit origin and entry");
    cpuCycle();verify(cpu.pc==0x10001 && cpu.regFile[0]==0,"B32K v2 executes at a high address");
    unsigned char v2_image[33];f=fopen("build/cpu-wide-v2.b32","rb");if(!f)return 1;
    verify(fread(v2_image,1,32,f)==32,"B32K v2 fixture length");fclose(f);v2_image[32]=0;
    bad_image(argv[0],v2_image,23);
    v2_image[4]=3;bad_image(argv[0],v2_image,32);v2_image[4]=2;
    v2_image[6]=22;bad_image(argv[0],v2_image,32);v2_image[6]=24;
    for(unsigned i=8;i<16;i++)v2_image[i]=0xff;
    bad_image(argv[0],v2_image,32);
    v2_image[8]=0;v2_image[9]=0;v2_image[10]=1;v2_image[11]=0;
    v2_image[12]=0;v2_image[13]=0;v2_image[14]=1;v2_image[15]=0;
    v2_image[12]=0;v2_image[13]=0;v2_image[14]=2;v2_image[15]=0;bad_image(argv[0],v2_image,32);v2_image[12]=0;v2_image[13]=0;v2_image[14]=1;v2_image[15]=0;
    v2_image[16]=1;v2_image[17]=0;v2_image[18]=1;v2_image[19]=0;bad_image(argv[0],v2_image,32);v2_image[16]=2;v2_image[17]=0;v2_image[18]=0;v2_image[19]=0;
    v2_image[31]^=1;bad_image(argv[0],v2_image,32);v2_image[31]^=1;
    bad_image(argv[0],v2_image,33);
    for(unsigned i=8;i<16;i++)v2_image[i]=0xff;
    v2_image[8]=0xfe;v2_image[12]=0xfe;
    f=fopen("build/cpu-wide-v2-edge.b32","wb");if(!f)return 1;
    if(fwrite(v2_image,1,32,f)!=32 || fclose(f))return 1;
    bootImage("build/cpu-wide-v2-edge.b32");
    verify(cpu.pc==0xfffffffeu && memoryRead(0xffffffffu)==0x12345678,"B32K v2 accepts the final two addressable words");
    cpuCycle();verify(cpu.pc==0xffffffffu,"B32K v2 executes across the top of the address space");
    snprintf(command,sizeof(command),"\"%s\" --emulator --boot build/cpu-wide-v2-edge.b32 --max-cycles 1 > build/cpu-v2-limit.out 2>&1",argv[0]);
#ifdef _WIN32
    verify(system(command)==2,"command-line B32K v2 boot honors cycle limit");
#else
    verify(system(command)==512,"command-line B32K v2 boot honors cycle limit");
#endif
    snprintf(command,sizeof(command),"\"%s\" --emulator --boot build/cpu-wide.b32 --max-cycles 1 > build/cpu-limit.out 2>&1",argv[0]);
#ifdef _WIN32
    verify(system(command)==2,"command-line cycle limit exit status");
#else
    verify(system(command)==512,"command-line cycle limit exit status");
#endif
    f=fopen("build/cpu-page-fail.b32","wb");if(!f)return 1;
    fwrite("B32K",1,4,f);put16(f,2);put16(f,24);put32(f,0x20000);put32(f,0x20000);
    put32(f,1);put32(f,0xf000);put32(f,0xf000);if(fclose(f))return 1;
    reset_cpu(1);const char *page_fail_path="build/cpu-page-fail.b32";
    for(unsigned i=0;page_fail_path[i];i++)memoryWrite(0x100+i,page_fail_path[i]);
    failNextSparsePageAllocation=1;
    verify(runWideImageFromGuest(0x100)==-5 && memoryPage(0x20,0)==NULL && memoryRead(0x20000)==0,
           "B32 app image allocation failure leaves its sparse region untouched");
    reset_cpu(1);ram.memory[0x300]=0xf000;ram.memory[0x301]=7;
    ram.memory[0xf000]=0x4233324b;ram.memory[0xf001]=0x00180002;ram.memory[0xf002]=0x20000;ram.memory[0xf003]=0x20000;
    ram.memory[0xf004]=1;ram.memory[0xf005]=0xe000;ram.memory[0xf006]=0xe000;
    verify(runWidePackedFromGuest(0x300)==0,"native app loads and executes from packed guest RAM image");
    reset_cpu(1);ram.memory[0x300]=0xf000;ram.memory[0x301]=7;
    ram.memory[0xf000]=0x4233324b;ram.memory[0xf001]=0x00180002;ram.memory[0xf002]=0x20000;ram.memory[0xf003]=0x20000;
    ram.memory[0xf004]=1;ram.memory[0xf005]=0xe000;ram.memory[0xf006]=0;
    verify(runWidePackedFromGuest(0x300)==-4,"native app checksum corruption rejected before execution");
    reset_cpu(1);ram.memory[0x300]=0xf000;ram.memory[0x301]=7;
    ram.memory[0xf000]=0x4233324b;ram.memory[0xf001]=0x00180002;ram.memory[0xf002]=0x20000;ram.memory[0xf003]=0x20000;
    ram.memory[0xf004]=1;ram.memory[0xf005]=0xe000;ram.memory[0xf006]=0xe000;failNextSparsePageAllocation=1;
    verify(runWidePackedFromGuest(0x300)==-5 && memoryPage(0x10,0)==NULL,"native app reports insufficient sparse memory without partial load");
    f=fopen("build/cpu-import.b32","wb");if(!f)return 1;
    fwrite("B32K",1,4,f);put16(f,2);put16(f,24);put32(f,0x20000);put32(f,0x20000);put32(f,1);put32(f,0xe000);put32(f,0xe000);if(fclose(f))return 1;
    reset_cpu(1);const char *import_path="build/cpu-import.b32";
    for(unsigned i=0;import_path[i];i++)ram.memory[0x100+i]=import_path[i];
    ram.memory[0x300]=0x100;ram.memory[0x301]=0xf000;ram.memory[0x302]=7;ram.memory[0xf000]=99;
    verify(importWideImageFromGuest(0x300)==-5 && ram.memory[0xf000]==99,"native image import reports insufficient filesystem capacity without overwriting it");
    ram.memory[0x302]=8;
    verify(importWideImageFromGuest(0x300)==7 && ram.memory[0xf006]==0xe000,"native image import validates and packs a B32K v2 image");
    ram.memory[0x310]=0xf000;ram.memory[0x311]=7;
    verify(runWidePackedFromGuest(0x310)==0,"imported native image executes from packed filesystem storage");
    bootImage("build/kernel.b16");verify(!cpu32,"legacy image selects compatibility mode");
    f=fopen("build/cpu-input.basm","w");if(!f)return 1;
    fputs("lea r0 2\ntrap 2\ntrap 0\n.stringz bob!\n",f);fclose(f);
    f=fopen("build/cpu-input.in","wb");if(!f)return 1;
    for(int i=0;i<255;i++)fputc('x',f);
    fputs("\r\n",f);
    for(int i=0;i<300;i++)fputc('x',f);
    fputs("\nbuild/cpu-input.basm\n",f);fclose(f);
    snprintf(command,sizeof(command),"\"%s\" --emulator < build/cpu-input.in > build/cpu-input.out 2>&1",argv[0]);
    verify(system(command)==0,"assembly prompt recovers after oversized file name");
    f=fopen("build/cpu-input.out","rb");if(!f)return 1;
    char prompt_output[4096];size_t prompt_length=fread(prompt_output,1,sizeof(prompt_output)-1,f);fclose(f);prompt_output[prompt_length]=0;
    verify(strstr(prompt_output,"Source file name exceeds 255 characters!") && strstr(prompt_output,"Starting execution!") && strstr(prompt_output,"bob!"),"oversized file name is consumed before next input");
    char *first_long=strstr(prompt_output,"Source file name exceeds 255 characters!");
    verify(first_long && !strstr(first_long+1,"Source file name exceeds 255 characters!"),"exact-limit CRLF filename is not reported as oversized");
    puts("bob!");printf("%d CPU checks passed.\n",checks);return 0;
}
