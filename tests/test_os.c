/* End-to-end tests implemented in C. Success prints bob!, never hello world. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#ifdef BOB_VERIFY
#define BOB "build\\bob-next.exe"
#else
#define BOB "bob.exe"
#endif
#define COMPILER "build\\bobcc.exe"
#else
#define BOB "./bob"
#define COMPILER "./build/bobcc"
#endif
static int checks;
static void fail(const char *message) { fprintf(stderr,"Check failed: %s\n",message);exit(1); }
static void check(int condition,const char *message) { checks++;if(!condition)fail(message); }
static void write_text(const char *path,const char *text) {
    FILE *f=fopen(path,"wb");if(!f)fail("cannot create fixture");fputs(text,f);fclose(f);
}
static char *read_text(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)fail("cannot read result");fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    if(size<0||size>2000000)fail("invalid result size");
    char *text=calloc((size_t)size+1,1);if(!text)fail("out of memory");
    size_t n=fread(text,1,(size_t)size,f);fclose(f);
    /* Normalize console CRLF so assertions work on Windows and Linux. */
    size_t out=0;for(size_t i=0;i<n;i++)if(text[i]!='\r')text[out++]=text[i];text[out]=0;return text;
}
static char *shell(const char *commands) {
    write_text("build/check.in",commands);
    int status=system(BOB " --boot build/kernel.b16 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err");
    check(status==0,"shell must halt cleanly");return read_text("build/check.out");
}
static void contains(const char *output,const char *expected) { check(strstr(output,expected)!=NULL,expected); }
static void not_contains(const char *output,const char *unexpected) { check(strstr(output,unexpected)==NULL,unexpected); }
static void guest(const char *source) {
    write_text("build/check.c",source);
    check(system("gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -I kernel build/check.c -o build/check.i > build/compiler.log 2>&1")==0,"guest preprocessing");
    check(system(COMPILER " build/check.i build/check.basm build/check.b16 > build/compiler.log 2>&1")==0,"guest compilation");
    write_text("build/check.in","");
    check(system(BOB " --boot build/check.b16 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"guest execution");
    char *output=read_text("build/check.out");check(!strcmp(output,"bob!\n"),"guest success marker");free(output);
}
static void console_and_commands(void) {
    char *output=shell("help\necho bob!\nclear\nmem\nalloc 4\npoke 0xc000 -32768\npeek 0xc000\npoke 0 1\nhalt\n");
    contains(output,"bob!\nbob16 OS:");contains(output,"help | echo TEXT");contains(output,"bob> bob!\n");
    contains(output,"\033[2J\033[H");contains(output,"free 8192 words");contains(output,"bob> 0xC000\n");
    contains(output,"0xC000: 0x8000 (-32768)");contains(output,"poke is limited to 0xC000..0xDFFF.");free(output);
}
static void input_bounds(void) {
    char commands[400];memset(commands,'x',200);strcpy(commands+200,"\necho bob!\nhalt\n");
    char *output=shell(commands);contains(output,"Command too long.");contains(output,"bob> bob!\n");
    not_contains(output,"Unknown command");free(output);
    output=shell("");check(!strcmp(output,"bob!\nbob16 OS: help COMMAND for usage; go bob.c runs the example.\nFiles are in RAM; halt exits. Type help to get started.\nbob> \n"),"EOF exits shell");free(output);
    output=shell("echo bxo\b\bbob!\nhalt\n");contains(output,"bob> bbob!\n");free(output);
}
static void allocator_and_files(void) {
    char *output=shell("alloc 0\nalloc -1\nalloc 8192\nalloc 1\nmem\nwrite note bob!\nread note\nwrite note bob!\nlist\nwrite a bob!\nwrite b bob!\nwrite c bob!\nwrite d bob!\nwrite e bob!\nwrite f bob!\nwrite g bob!\nread missing\nhalt\n");
    contains(output,"Allocation failed.");contains(output,"free 0 words");contains(output,"note  4 chars (text)");
    contains(output,"bob> bob!\n");contains(output,"Cannot write file.");contains(output,"File not found.");free(output);
    output=shell("edit note\nbob!\n.\nread note\nlist\nhalt\n");
    contains(output,"Saved.");contains(output,"bob!\n\n");contains(output,"note  5 chars (text)");free(output);
    output=shell("poke 0xc000 33\nalloc 4\npeek 0xc000\nalloc 4\nmem\nhalt\n");
    contains(output,"0xC000: 0x0000 (0)");contains(output,"bob> 0xC004\n");contains(output,"free 8184 words");free(output);
    output=shell("write abcdefghijklmnopqrstuvwx bob!\nalloc 32768\npoke 0xdfff 0xffff\npeek 0xdfff\nhalt\n");
    contains(output,"Cannot write file.");contains(output,"Invalid allocation size.");contains(output,"0xDFFF: 0xFFFF (-1)");free(output);
    char large[800];strcpy(large,"write note bob!\nedit note\n");
    size_t n=strlen(large);
    for(int i=0;i<5;i++){memset(large+n,'b',120);n+=120;large[n++]='\n';}
    strcpy(large+n,".\nread note\nhalt\n");
    output=shell(large);contains(output,"File too large; change rejected.");free(output);
    strcpy(large,"edit full\n");n=strlen(large);
    for(int i=0;i<4;i++){memset(large+n,'b',126);n+=126;large[n++]='\n';}
    strcpy(large+n,"bb\n.\nlist\nhalt\n");
    output=shell(large);contains(output,"full  511 chars (text)");free(output);
}
static void resident_compiler(void) {
    char *output=shell("cc bob.c bob\nrun bob\necho bob!\nhalt\n");
    contains(output,"Compiled 42 words.");contains(output,"bob> bob!\nExit 0\nbob> bob!\n");free(output);
    output=shell("edit calc.c\nint main(void) { int n = 0; while (n < 3) { n = n + 1; }\nif (n == 3) { println(\"bob!\"); } else { return 1; } return 0; }\n.\ncc calc.c calc\nrun calc\nhalt\n");
    not_contains(output,"compile error");contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("write math.c int main(void) { int a = 6 * 7; if (a / 2 == 21) println(\"bob!\"); return a % 7; }\ncc math.c math\nrun math\nhalt\n");
    not_contains(output,"compile error");contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("write bad.c int main(void) { int a = ; }\ncc bad.c bad\nrun bad\ncc bob.c bob\nrun bob\nhalt\n");
    contains(output,"C compile error near character");contains(output,"Program not found.");contains(output,"bob> bob!\nExit 0");free(output);
    /* Verify both APIs and signed numeric formatting in resident-generated code. */
    output=shell("write num.c int main(void) { print(\"bob!\"); print_dec(-32767); print_hex(0xffff); return 0; }\ncc num.c num\nrun num\nhalt\n");
    contains(output,"bob> bob!-327670xFFFFExit 0");free(output);
    output=shell("cc bob.c bob\nwrite bad.c int main(void) { @ }\ncc bad.c bob\nrun bob\nhalt\n");
    contains(output,"C compile error near character");contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("write bad.c int main(void) println(\"bob!\");\ncc bad.c bad\nwrite ok.c int main() { /* bob! */ println(\"bob!\"); return -1; }\ncc ok.c ok\nrun ok\nhalt\n");
    contains(output,"C compile error near character");contains(output,"bob> bob!\nExit -1");free(output);
    char nested[500];strcpy(nested,"write deep.c int main(void) { return ");
    size_t n=strlen(nested);for(int i=0;i<35;i++)nested[n++]='(';
    nested[n++]='0';for(int i=0;i<35;i++)nested[n++]=')';
    strcpy(nested+n,"; }\ncc deep.c deep\ncc bob.c bob\nrun bob\nhalt\n");
    output=shell(nested);contains(output,"C compile error near character");contains(output,"bob> bob!\nExit 0");free(output);
}
static void compiler_extensions(void) {
    char *output=shell("edit loop.c\nint main(void) { int s=0; for(int i=0; i<4; i++) {\nif(i==1) continue; if(i==3) break; s+=i; }\nif(s==2 && (3<<2)==12) println(\"bob!\"); return 0; }\n.\ncc loop.c loop\nrun loop\nhalt\n");
    not_contains(output,"compile error");contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("edit bits.c\nint main(void) { int a=5; a|=2; a^=1; a&=6;\nif(a==6 && (~0 & 1)==1 && (-8>>2)==-2) println(\"bob!\"); return 0; }\n.\ncc bits.c bits\nrun bits\nhalt\n");
    not_contains(output,"compile error");contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("edit short.c\nint main(void) { int a=0; int b=1; if(0 && a++) return 1;\nif(1 || a++) { ++b; } if(a==0 && b==2) println(\"bob!\"); return 0; }\n.\ncc short.c short\nrun short\nhalt\n");
    not_contains(output,"compile error");contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("edit cmp.c\nint main(void) { if(1==2<3) println(\"bob!\"); return 0; }\n.\ncc cmp.c cmp\nrun cmp\nhalt\n");
    contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("write bad.c int main(void) { break; }\ncc bad.c bad\ncc bob.c bob\nrun bob\nhalt\n");
    contains(output,"C compile error near character");contains(output,"bob> bob!\nExit 0");free(output);
}
static void editor_and_aliases(void) {
    char *output=shell("write note bob!\nls\ndir\nedit note\n:r 1 bob!\n:i 1 bob!\n:d 2\n:p\n:wq\nread note\nlist\nhalt\n");
    contains(output,"note  4 chars (text)");contains(output,"1: bob!");contains(output,"note  5 chars (text)");free(output);
    output=shell("write note bob!\nedit note\n:a bob!\n:u\n:wq\nlist\nedit note\n:r 1 discard\n:q!\nread note\nhalt\n");
    contains(output,"note  4 chars (text)");contains(output,"Discarded unsaved changes.");contains(output,"bob> bob!\n");free(output);
    output=shell("edit note\n:i 1 bob!\n:r 20 bob!\n:d -1\n:u\n:p\n:q\ncc bob.c bob\nedit bob\nhalt\n");
    contains(output,"Invalid line number.");contains(output,"(empty)");contains(output,"Cannot edit a binary program.");free(output);
}
static void program_loading_and_recovery(void) {
    /* Native bob16: load each character, trap 1, then return through r7. */
    char *output=shell("load raw 0x4001 0xae01 98 0xf100 0x4001 0xae01 111 0xf100 0x4001 0xae01 98 0xf100 0x4001 0xae01 33 0xf100 0x4001 0xae01 10 0xf100 0x4001 0xae01 0 0xe000\nrun raw\nhalt\n");
    /* This intentionally exceeds a shell line: it must reject, not partly load. */
    contains(output,"Command too long.");contains(output,"Program not found.");free(output);
    output=shell("load raw 0x4001 0xae01 98 0xf100 0x4001 0xae01 111 0xf100 0x4001 0xae01 98 0xf100 0x4001 0xae01 33 0xf100 0xe000\nrun raw\nhalt\n");
    contains(output,"Loaded.");contains(output,"bob> bob!Exit 33");free(output);
    output=shell("load bad 0x1001\nrun bad\nload spin 0x4601 0xb600 0x9000\nrun spin\necho bob!\nhalt\n");
    contains(output,"Program fault; shell restored.");contains(output,"Program timed out; shell restored.");contains(output,"bob> bob!\n");free(output);
    /* Load a program that tries to store to kernel address zero. */
    output=shell("load guard 0x4201 0xae01 0 0x9040 0xe000\nrun guard\ncc bob.c bob\nrun bob\nhalt\n");
    contains(output,"Program fault; shell restored.");contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("load stop 0xf000\nrun stop\necho bob!\nhalt\n");contains(output,"bob> bob!\n");free(output);
    output=shell("load inputfault 0xf300\nrun inputfault\nload diskfault 0xf400\nrun diskfault\necho bob!\nhalt\n");
    contains(output,"Program fault; shell restored.");contains(output,"bob> bob!\n");free(output);
}
static void host_compiler_and_runtime(void) {
    guest("#include \"runtime.h\"\n#include \"runtime.c\"\n"
          "int sum(int n) { if (n == 0) return 0; return n + sum(n - 1); }\n"
          "int main(void) { int data[4]; int other[4]; int *p; int i; "
          "memset(data, 7, 4); memcpy(other, data, 4); p = &other[0]; "
          "if (*p != 7 || sum(3) != 6 || strlen(\"bob!\") != 4 || strcmp(\"bob!\",\"bob!\")) return 1; "
          "if(strlen(\"\") != 0 || strcmp(\"bob\",\"bob!\") >= 0 || strcmp(\"bob!\",\"bob\") <= 0) return 1; "
          "for(i=0;i<4;i++){ if(data[i] != 7 || other[i] != 7) return 1; } "
          "for(i = 0; i < 4; i++) { other[i] += i; } if(other[3] != 10) return 1; println(\"bob!\"); return 0; }");
    guest("#include \"bob.h\"\nint main(void) { int a; int b; a=-32768; b=32767; "
          "if(!(a < b) || !(b > a) || a != a || !(a <= a) || !(b >= b)) return 1; "
          "if(6*7 != 42 || -43/7 != -6 || -43%7 != -1) return 1; "
          "if((5|2) != 7 || (5^3) != 6 || (3<<4) != 48 || (-8>>2) != -2) return 1; "
          "bob_puts(\"bob!\"); return 0; }");
    write_text("build/check.c","struct nope { int x; }; int main(void) { return 0; }");
    check(system("gcc -E -P -nostdinc -undef -I kernel build/check.c -o build/check.i > build/compiler.log 2>&1")==0,"bad source preprocessing");
    check(system(COMPILER " build/check.i build/check.basm build/check.b16 > build/compiler.log 2>&1")!=0,"unsupported source rejected");
    /* Exact signed decimal/hex console output, including the minimum value. */
    write_text("build/check.c","#include \"runtime.h\"\n#include \"runtime.c\"\nint main(void) { print(\"bob!\"); print_dec(-32768); print_dec(32767); print_dec(0); print_hex(0xffff); return 0; }");
    check(system("gcc -E -P -nostdinc -undef -I kernel build/check.c -o build/check.i > build/compiler.log 2>&1")==0,"numeric preprocessing");
    check(system(COMPILER " build/check.i build/check.basm build/check.b16 > build/compiler.log 2>&1")==0,"numeric compilation");
    check(system(BOB " --boot build/check.b16 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"numeric execution");
    char *output=read_text("build/check.out");check(!strcmp(output,"bob!-327683276700xFFFF"),"exact decimal/hex output");free(output);
}
static void file_management(void) {
    char *output=shell("write note bob!\ncopy note backup\nrename backup saved\nread saved\ncopy note saved\nrename note saved\nread note\ndelete saved\nread saved\ncopy missing new\ncopy note\ndelete note extra\nread note\nhalt\n");
    contains(output,"Copied.");contains(output,"Renamed.");
    contains(output,"Destination exists; choose another name or delete it first.");
    contains(output,"Deleted.");contains(output,"File not found. Use ls to see names.");
    contains(output,"Destination needs 1..23 characters.");
    contains(output,"Usage: copy OLD NEW | rename OLD NEW | delete NAME");
    contains(output,"bob> bob!\n");free(output);
    output=shell("cc bob.c bob\ncopy bob spare\nrename spare renamed\nrun renamed\nwrite a bob!\nwrite b bob!\nwrite c bob!\nwrite d bob!\nwrite e bob!\ncopy bob another\nrename bob original\nrun original\ndelete a\ncopy original another\nrun another\nhalt\n");
    contains(output,"File slots full (8). Delete a file and retry.");
    contains(output,"Renamed.");contains(output,"Deleted.");
    contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("write note bob!\ncopy note abcdefghijklmnopqrstuvwx\ncopy note note\ndelete note\nwrite note bob!\nls\nhalt\n");
    contains(output,"Destination needs 1..23 characters.");
    contains(output,"Destination exists; choose another name or delete it first.");
    contains(output,"note  4 chars (text)");free(output);
}
static void compile_workflow(void) {
    char *output=shell("go bob.c\ncc bob.c\nrun app\ngo bob.c named\nrun named\ncc bob.c bob.c\nread bob.c\ngo missing\ngo bob.c named extra\nedit broken.c\nint main(void) {\nreturn @;\n}\n.\ngo broken.c\necho bob!\nhalt\n");
    contains(output,"bob> bob!\nExit 0");
    contains(output,"Output must differ from source; choose another name.");
    contains(output,"int main(void) { println(\"bob!\"); return 0; }");
    contains(output,"C source file not found.");
    contains(output,"Usage: cc SOURCE [OUTPUT] | go SOURCE [OUTPUT]");
    contains(output,"broken.c: line 2, column");
    contains(output,"Use edit to fix the source.");
    /* Four successful runs above; neither failed go may execute stale app. */
    int exits=0;for(char *p=output;(p=strstr(p,"Exit 0"))!=NULL;p+=6)exits++;
    check(exits==4,"go failure must not execute old output");free(output);
}
static void usability(void) {
    char *output=shell("help edit\nhelp go\nhelp write\nhelp read\nhelp ls\nhelp copy\nhelp run\nhelp load\nhelp poke\nhelp alloc\nhelp echo\nhelp clear\nhelp halt\nhelp help\nhelp unknown\nwrite note bob!\nedit note\n:r 1 bob!bob!\n:q\n:p 1\n:u\n:p 1\n:u\n:p 1\n:w\n:q\nread note\nedit note\n:r 1 discarded\n:q!\nread note\nhalt\n");
    contains(output,"help COMMAND for usage");contains(output,"go SOURCE [OUTPUT]");
    contains(output,"No help for that command.");
    contains(output,"Unsaved changes. Use :wq to save or :q! to discard.");
    contains(output,"1: bob!bob!\n");contains(output,"1: bob!\n");
    contains(output,"Undo/redo applied.");contains(output,"bob> bob!bob!\n\n");
    not_contains(output,"bob> discarded");free(output);
    write_text("build/check.in","go bob.c\nhalt\n");
    check(system(BOB " --os < build/check.in > build/check.out 2> build/check.err")==0,"easy OS startup");
    output=read_text("build/check.out");contains(output,"bob16 OS:");contains(output,"bob!\nExit 0");
    not_contains(output,"Enter filename");free(output);
}
static void image_validation(void) {
    FILE *in=fopen("build/kernel.b16","rb");if(!in)fail("kernel image missing");
    FILE *out=fopen("build/bad.b16","wb");if(!out)fail("bad image fixture");int ch;long count=0;
    while((ch=fgetc(in))!=EOF){if(count==14)ch^=1;fputc(ch,out);count++;}fclose(in);fclose(out);
    check(system(BOB " --boot build/bad.b16 > build/check.out 2> build/check.err")!=0,"checksum corruption rejected");
    write_text("build/bad.b16","B16K");check(system(BOB " --boot build/bad.b16 > build/check.out 2> build/check.err")!=0,"truncated image rejected");
}
static void basm_route(void) {
    write_text("build/check.in","kernel.basm\ncc bob.c bob\nrun bob\nhalt\n");
    check(system(BOB " < build/check.in > build/check.out 2> build/check.err")==0,"interactive BASM route");
    char *output=read_text("build/check.out");contains(output,"Starting execution!\nbob!\nbob16 OS:");contains(output,"bob> bob!\nExit 0");free(output);
    write_text("build/check.in","demo.basm\n");
    check(system(BOB " < build/check.in > build/check.out 2> build/check.err")==0,"original BASM demo");
    output=read_text("build/check.out");contains(output,"bob!\n");free(output);
}
int main(void) {
    console_and_commands();input_bounds();allocator_and_files();resident_compiler();
    compiler_extensions();editor_and_aliases();
    program_loading_and_recovery();host_compiler_and_runtime();file_management();compile_workflow();usability();image_validation();basm_route();
    printf("bob!\n%d checks passed.\n",checks);return 0;
}
