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
#ifdef BOB_VERIFY
#define BOB32 "build\\bob32-next.exe"
#else
#define BOB32 "bob32.exe"
#endif
#define COMPILER "build\\bobcc.exe"
#else
#define BOB "./bob"
#define BOB32 "./bob32"
#define COMPILER "./build/bobcc"
#endif
static int checks;
static void storage_path(const char *path);
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
static void guest32(const char *source) {
    write_text("build/wide-lib.c",source);
    check(system("gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -I kernel build/wide-lib.c -o build/wide-lib.i > build/compiler.log 2>&1")==0,"bob32 library guest preprocessing");
    check(system(COMPILER " build/wide-lib.i build/wide-lib.basm build/wide-lib.b32 --wide > build/compiler.log 2>&1")==0,"bob32 library guest compilation");
    write_text("build/check.in","");
    check(system(BOB32 " --boot build/wide-lib.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"bob32 library guest execution");
    char *output=read_text("build/check.out");check(!strcmp(output,"bob!\n"),"bob32 library success marker");free(output);
}
static void native_string_library(void) {
    guest32(
        "#include \"bob.h\"\n"
        "#include \"bob_string.h\"\n"
        "int main(void){\n"
        "char text[64]=\"Bob\";char output[16];char command[64]=\"notes \\\"my note\\\" escaped\\\\ token\";\n"
        "char malformed[24]=\"broken \\\"quote\";char *found;char *cursor;char *token;int valid;int parsed;\n"
        "if(bob_strlen(text)!=3||bob_strcpy(text,\"Bob\")!=text)return 1;\n"
        "if(bob_strcat(text,\"!\")!=text||bob_strcmp(text,\"Bob!\"))return 2;\n"
        "found=bob_strchr(text,'!');if(!found||found-text!=3)return 3;\n"
        "if(!bob_isspace('\\t')||bob_isspace('x'))return 4;\n"
        "parsed=bob_parse_int(\" \\t-2147483648 \",&valid);if(!valid||parsed!=(-2147483647-1))return 5;\n"
        "if(bob_format_int(parsed,output,16)!=11||bob_strcmp(output,\"-2147483648\"))return 6;\n"
        "if(bob_format_int(-42,output,3)!=-1)return 7;\n"
        "if(bob_parse_int(\"0x2a\",&valid)!=42||!valid)return 8;\n"
        "bob_parse_int(\"2147483648\",&valid);if(valid)return 9;\n"
        "bob_parse_int(\"12x\",&valid);if(valid)return 10;\n"
        "cursor=command;if(bob_token_next(&cursor,&token)!=1||bob_strcmp(token,\"notes\"))return 11;\n"
        "if(bob_token_next(&cursor,&token)!=1||bob_strcmp(token,\"my note\"))return 12;\n"
        "if(bob_token_next(&cursor,&token)!=1||bob_strcmp(token,\"escaped token\"))return 13;\n"
        "if(bob_token_next(&cursor,&token)!=0)return 14;\n"
        "cursor=malformed;if(bob_token_next(&cursor,&token)!=1)return 15;\n"
        "if(bob_token_next(&cursor,&token)!=-1)return 16;\n"
        "bob_puts(\"bob!\");return 0;}\n");
    write_text("build/string-app.c",
        "#include \"bob.h\"\n#include \"bob_string.h\"\n"
        "int main(void){char text[16]=\"Bob\";bob_strcat(text,\"!\");"
        "if(bob_strlen(text)==4&&!bob_strcmp(text,\"Bob!\")){bob_puts(\"bob!\");return 0;}return 1;}\n");
    check(system("gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -I kernel build/string-app.c -o build/string-app.i > build/compiler.log 2>&1")==0,"bob32 string app preprocessing");
    check(system(COMPILER " build/string-app.i build/string-app.basm build/string-app.b32 --wide-app > build/compiler.log 2>&1")==0,"bob32 string app compilation");
    storage_path("build/string-library-snapshot.b32");
    write_text("build/check.in","import32 build/string-app.b32 stringlib\nrun stringlib\nsave\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"bob32 stores and runs a native string-library app");
    char *output=read_text("build/check.out");contains(output,"Imported native app as stringlib");contains(output,"bob> bob!\nExit 0");contains(output,"Files saved to bob-files.b32");free(output);
    write_text("build/check.in","restore yes\nrun stringlib\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"bob32 string-library app survives snapshot restore");
    output=read_text("build/check.out");contains(output,"Files restored;");contains(output,"bob> bob!\nExit 0");free(output);
    write_text("build/argv-app.c",
        "#include \"bob.h\"\n#include \"bob_string.h\"\n"
        "int main(int argc,char **argv){if(argc!=5)return argc;"
        "if(bob_strcmp(argv[1],\"Bob OS\")||bob_strcmp(argv[2],\"escaped token\")||bob_strcmp(argv[3],\"\"))return 2;"
        "bob_puts(\"bob!\");return 0;}\n");
    check(system("gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -I kernel build/argv-app.c -o build/argv-app.i > build/compiler.log 2>&1")==0,"bob32 argv app preprocessing");
    check(system(COMPILER " build/argv-app.i build/argv-app.basm build/argv-app.b32 --wide-app > build/compiler.log 2>&1")==0,"bob32 argv app compilation");
    write_text("build/check.in","import32 build/argv-app.b32 argvapp\nrun argvapp \"Bob OS\" escaped\\ token \"\"\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"bob32 passes quoted arguments to native apps");
    output=read_text("build/check.out");contains(output,"bob> bob!\nExit 0");free(output);
    storage_path("build/snapshot.b16");
}
static void wide_guest(void) {
    write_text("build/wide-check.c",
        "#include \"bob.h\"\n"
        "#include <stdbool.h>\n"
        "#include <stddef.h>\n"
        "int combine_values(int a,int b){return a+b;}\n"
        "int double_value(int value){return value*2;}\n"
        "int (*combine)(int,int)=combine_values;\n"
        "int apply_value(int (*operation)(int),int value){return operation(value);}\n"
        "int main(void) { int x=0x12345678; if(x+1!=0x12345679)return 1; "
        "bool enabled=true; if(!enabled || false)return 10; enabled=false; if(enabled)return 11; "
        "int values[3];size_t words=sizeof values;ptrdiff_t distance=&values[2]-&values[0];int *empty=NULL; "
        "if(words!=3 || distance!=2 || empty!=NULL || (sizeof(int)-2)!=(size_t)-1)return 12; "
        "if(combine(4,5)!=9)return 13; "
        "int (*scale)(int (*)(int),int)=apply_value; if(scale(double_value,7)!=14)return 14; "
        "if(-x!=-305419896)return 2; if((0x70000000+0x70000000)!=0xe0000000)return 3; "
        "if(0x70000000*2!=0xe0000000)return 4; "
        "if(0x70000000/7!=0x10000000 || 0x70000000%7)return 5; "
        "if(-123456789/12345!=-10000 || -123456789%12345!=-6789)return 6; "
        "if(0xf0000000u/3u!=0x50000000u || 0xf0000000u%3u)return 7; "
        "if(0x80000000u>>1!=0x40000000u || (-8>>2)!=-2)return 8; "
        "if((0xf0000000u>0x10000000u)==0)return 9; "
        "bob_puts(\"bob!\"); return 0; }\n");
    check(system("gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -I kernel build/wide-check.c -o build/wide-check.i > build/compiler.log 2>&1")==0,"wide guest preprocessing");
    check(system(COMPILER " build/wide-check.i build/wide-check.basm build/wide-check.b32 --wide > build/compiler.log 2>&1")==0,"wide guest compilation");
    FILE *image=fopen("build/wide-check.b32","rb");if(!image)fail("cannot read wide image");
    char magic[4];check(fread(magic,1,4,image)==4 && !memcmp(magic,"B32K",4),"wide compiler emits B32K");
    check(fgetc(image)==2 && fgetc(image)==0,"wide compiler emits B32K v2");fclose(image);
    write_text("build/check.in","");
    check(system(BOB " --boot build/wide-check.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"wide guest execution");
    char *output=read_text("build/check.out");check(!strcmp(output,"bob!\n"),"wide guest success marker");free(output);
    write_text("build/wide-small.c","#include \"bob.h\"\nint main(void){bob_puts(\"bob!\");return 0;}\n");
    check(system("gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -I kernel build/wide-small.c -o build/wide-small.i > build/compiler.log 2>&1")==0,"small wide guest preprocessing");
    check(system(COMPILER " build/wide-small.i build/wide-small.basm build/wide-small.b32 --wide > build/compiler.log 2>&1")==0,"small wide guest compilation");
    image=fopen("build/wide-small.b32","rb");if(!image)fail("cannot read small wide image");
    check(fseek(image,0,SEEK_END)==0 && ftell(image)<=24+511*4,"reachable helpers keep small bob32 app within one OS file");fclose(image);
    check(system(BOB " --boot build/wide-small.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"small wide guest execution");
    output=read_text("build/check.out");check(!strcmp(output,"bob!\n"),"small wide guest success marker");free(output);
    check(system(BOB32 " --asm32 build/wide-small.basm < build/check.in > build/check.out 2> build/check.err")==0,"wide BASM direct assembly and execution");
    output=read_text("build/check.out");check(!strcmp(output,"bob!\n"),"wide BASM success marker");free(output);
    write_text("build/wide-types.c",
        "#include \"bob.h\"\n"
        "int main(void){long a=7L;unsigned long b=0xffffffffUL;long long c=3LL;"
        "if(sizeof(long)!=1 || sizeof(a)!=1 || sizeof(unsigned long*)!=1 || sizeof(char)!=1 || sizeof(int[3])!=3)return 1;"
        "if(a+c!=10 || b>>1!=0x7fffffffUL)return 2;bob_puts(\"bob!\");return 0;}\n");
    check(system("gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -I kernel build/wide-types.c -o build/wide-types.i > build/compiler.log 2>&1")==0,"wide type guest preprocessing");
    check(system(COMPILER " build/wide-types.i build/wide-types.basm build/wide-types.b32 --wide > build/compiler.log 2>&1")==0,"wide long type compilation");
    check(system(BOB " --boot build/wide-types.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"wide type guest execution");
    output=read_text("build/check.out");check(!strcmp(output,"bob!\n"),"wide long and sizeof semantics");free(output);
    write_text("build/wide-bad.c","int main(void){return 1lul;}\n");
    check(system("gcc -E -P -nostdinc -undef build/wide-bad.c -o build/wide-bad.i > build/compiler.log 2>&1")==0,"invalid wide suffix preprocessing");
    check(system(COMPILER " build/wide-bad.i build/wide-bad.basm build/wide-bad.b32 --wide > build/compiler.log 2>&1")!=0,"invalid mixed integer suffix rejected");
    output=read_text("build/compiler.log");contains(output,"invalid integer suffix");free(output);
    write_text("build/wide-app.c","#include \"bob.h\"\nint main(void){bob_puts(\"bob!\");return 0;}\n");
    check(system("gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -I kernel build/wide-app.c -o build/wide-app.i > build/compiler.log 2>&1")==0,"wide OS app preprocessing");
    check(system(COMPILER " build/wide-app.i build/wide-app.basm build/wide-app.b32 --wide-app > build/compiler.log 2>&1")==0,"wide OS app compilation");
    image=fopen("build/wide-app.b32","rb");if(!image)fail("cannot read wide OS app image");
    unsigned char app_header[24];check(fread(app_header,1,sizeof(app_header),image)==sizeof(app_header),"wide OS app header read");fclose(image);
    check(!memcmp(app_header,"B32K",4) && app_header[8]==0 && app_header[9]==0 && app_header[10]==2 && app_header[11]==0,"wide OS app uses supervised high-memory origin");
    image=fopen("build/wide-app.b32","rb");if(!image)fail("cannot reopen wide app image");
    fseek(image,0,SEEK_END);long image_size=ftell(image);rewind(image);unsigned char *image_bytes=malloc((size_t)image_size);if(!image_bytes)fail("wide app allocation");
    check(fread(image_bytes,1,(size_t)image_size,image)==(size_t)image_size,"read wide app for large fixture");fclose(image);
    uint32_t image_words=(uint32_t)image_bytes[16]|((uint32_t)image_bytes[17]<<8)|((uint32_t)image_bytes[18]<<16)|((uint32_t)image_bytes[19]<<24);
    image_words+=600;for(int i=0;i<4;i++)image_bytes[16+i]=(unsigned char)(image_words>>(8*i));
    check(image_words>511,"native B32 fixture exceeds the former per-file limit");
    image=fopen("build/wide-large.b32","wb");if(!image)fail("create large B32 image");
    check(fwrite(image_bytes,1,(size_t)image_size,image)==(size_t)image_size,"write large image prefix");
    unsigned char zeros[2400]={0};check(fwrite(zeros,1,sizeof(zeros),image)==sizeof(zeros),"append large image payload");fclose(image);free(image_bytes);
    image=fopen("build/wide-large.b32","rb");if(!image)fail("open large B32 image for truncated fixture");
    unsigned char short_header[24];check(fread(short_header,1,sizeof(short_header),image)==sizeof(short_header),"read truncated image header");fclose(image);
    image=fopen("build/wide-truncated.b32","wb");if(!image)fail("create truncated B32 image");
    check(fwrite(short_header,1,sizeof(short_header),image)==sizeof(short_header),"write truncated image header");fclose(image);
    write_text("build/check.in","");
    check(system(BOB32 " --asm32 build/wide-app.basm < build/check.in > build/check.out 2> build/check.err")==0,"wide .org BASM assembly and direct application execution");
    output=read_text("build/check.out");check(!strcmp(output,"bob!\n"),"wide-origin BASM application output");free(output);
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
    output=shell("write abcdefghijklmnopqrstuvwx bob!\nalloc 65536\npoke 0xdfff 0xffff\npeek 0xdfff\nhalt\n");
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
    char oversized[2048];strcpy(oversized,"cc bob.c keep\nedit large.c\nint main(void){\n");
    for(int i=0;i<28;i++)strcat(oversized,"println(\"bob!\");\n");
    strcat(oversized,"return 0;}\n.\ncc large.c keep\nrun keep\ngo bob.c\nhalt\n");
    char *bounded=shell(oversized);contains(bounded,"C compile error near character");
    contains(bounded,"bob!\nExit 0");not_contains(bounded,"Program fault");free(bounded);
    char *joined=shell("edit joined.c\nint main(void){println(\"bo\" /* join */ \"b\" \"\"\n\"!\");return 0;}\n.\ngo joined.c\nhalt\n");
    not_contains(joined,"compile error");contains(joined,"bob!\nExit 0");free(joined);
    char *broken=shell("edit broken.c\nint main(void){println(\"bob!\n\");return 0;}\n.\ngo broken.c\n"
                       "edit broken.c\n:r 1 int main(void){int x='\n:r 2 ';return 0;}\n.\ngo broken.c\ngo bob.c\nhalt\n");
    int errors=0;const char *error=broken;while((error=strstr(error,"C compile error near character"))){errors++;error++;}
    check(errors==2,"resident rejects literal line breaks");contains(broken,"bob!\nExit 0");free(broken);
    char *escapes=shell("edit escapes.c\nint main(void){if('\\a'!=7 || '\\b'!=8 || '\\f'!=12)return 1;\n"
                       "if('\\r'!=13 || '\\v'!=11 || '\\?'!=63)return 2;println(\"bob!\");return 0;}\n.\ngo escapes.c\nhalt\n");
    not_contains(escapes,"compile error");contains(escapes,"bob!\nExit 0");free(escapes);
    char *octal=shell("write octal.c int main(void){if(010!=8 || 077!=63 || 0177777!=0xffff)return 1;println(\"bob!\");return 0;}\ngo octal.c\nhalt\n");
    not_contains(octal,"compile error");contains(octal,"bob!\nExit 0");free(octal);
    octal=shell("write bad.c int main(void){return 09;}\ngo bad.c\nwrite bad.c int main(void){return 0200000;}\ngo bad.c\ngo bob.c\nhalt\n");
    contains(octal,"C compile error near character");contains(octal,"bob!\nExit 0");free(octal);
    char *output=shell("cc bob.c bob\nrun bob\necho bob!\nhalt\n");
    contains(output,"Compiled 42 words.");contains(output,"bob> bob!\nExit 0\nbob> bob!\n");free(output);
    output=shell("edit calc.c\nint main(void) { int n = 0; while (n < 3) { n = n + 1; }\nif (n == 3) { println(\"bob!\"); } else { return 1; } return 0; }\n.\ncc calc.c calc\nrun calc\nhalt\n");
    not_contains(output,"compile error");contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("write math.c int main(void) { int a = 6 * 7; if (a / 2 == 21) println(\"bob!\"); return a % 7; }\ncc math.c math\nrun math\nhalt\n");
    not_contains(output,"compile error");contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("edit product.c\nint main(void){int high=32767;int low=-32767-1;int i=0;\n"
                 "while(i<64){if(high*high!=1)return 1;if(low*high!=low)return 2;i++;}\n"
                 "if(low*2!=0 || (-7)*9!=-63)return 3;println(\"bob!\");return 0;}\n.\ngo product.c\nhalt\n");
    not_contains(output,"compile error");not_contains(output,"Program timed out");contains(output,"bob!\nExit 0");free(output);
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
    output=shell("load bad 0x1001\nrun bad\nload spin 0x4601 0xb600 0xa000\nrun spin\necho bob!\nhalt\n");
    contains(output,"Program fault; shell restored.");contains(output,"Program timed out; shell restored.");contains(output,"bob> bob!\n");free(output);
    /* Load a program that tries to store to kernel address zero. */
    output=shell("load guard 0x4201 0xae01 0 0x9040 0xe000\nrun guard\ncc bob.c bob\nrun bob\nhalt\n");
    contains(output,"Program fault; shell restored.");contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("load stop 0xf000\nrun stop\necho bob!\nhalt\n");contains(output,"bob> bob!\n");free(output);
    output=shell("load inputfault 0xf300\nrun inputfault\nload diskfault 0xf400\nrun diskfault\necho bob!\nhalt\n");
    contains(output,"Program fault; shell restored.");contains(output,"bob> bob!\n");free(output);
}
static void host_compiler_and_runtime(void) {
    guest("#include <stdbool.h>\n#include \"bob.h\"\nint main(void){bool enabled=true;if(!enabled||false)return 1;enabled=false;if(enabled)return 2;bob_puts(\"bob!\");return 0;}");
    guest("#include <stddef.h>\n#include \"bob.h\"\nint main(void){int values[3];size_t words=sizeof values;ptrdiff_t distance=&values[2]-&values[0];int *empty=NULL;if(words!=3||distance!=2||empty!=NULL||(sizeof(int)-2)!=(size_t)-1)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint sum(int a,int b){return a+b;}int (*combine)(int,int)=sum;int twice(int n){return n*2;}int apply(int (*fn)(int),int n){return fn(n);}int main(void){int (*operation)(int,int)=&sum;int (*scale)(int)=twice;int (*oldstyle)()=twice;if(combine(4,5)!=9||operation(2,3)!=5||scale(6)!=12||(*scale)(8)!=16||apply(*scale,7)!=14||oldstyle(5)!=10)return 1;operation=combine;if(operation(7,8)!=15)return 2;bob_puts(\"bob!\");return 0;}");
    char *joined_literal=malloc(40000);if(!joined_literal)fail("allocate joined literal fixture");
    char *joined_cursor=joined_literal;joined_cursor+=sprintf(joined_cursor,"#include \"bob.h\"\nchar *text=");
    for(int i=0;i<9000;i++){memcpy(joined_cursor,"\"b\"",3);joined_cursor+=3;}
    strcpy(joined_cursor,";int main(void){if(text[8999]!='b' || text[9000]!=0)return 1;bob_puts(\"bob!\");return 0;}");
    guest(joined_literal);free(joined_literal);
    char *large_literal=malloc(10000);if(!large_literal)fail("allocate large literal fixture");
    char *literal_cursor=large_literal;literal_cursor+=sprintf(literal_cursor,"#include \"bob.h\"\nchar *text=\"");
    memset(literal_cursor,'b',9000);literal_cursor+=9000;
    strcpy(literal_cursor,"\";int main(void){if(text[8999]!='b' || text[9000]!=0)return 1;bob_puts(\"bob!\");return 0;}");
    guest(large_literal);free(large_literal);
    guest("#include \"bob.h\"\nint x;_Bool a=&x,b=\"bob!\",c=(_Bool)&x,d=(int*)0; "
          "_Bool values[2]={&x,(void*)0};int main(void){static _Bool s=&x; "
          "if(!a || !b || !c || d || !values[0] || values[1] || !s)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint (*p)[3]=(int (*)[3])0xc000+2; "
          "int (*q)[3]=2+(int (*)[3])0xc000;int (*r)[3]=(int (*)[3])0xc009-1; "
          "int distance=(int (*)[3])0xc009-(int (*)[3])0xc000; "
          "int main(void){int (*base)[3]=(int (*)[3])0xc000; "
          "if(p!=base+2 || q!=p || r!=p || distance!=3)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint a=7,b=9;int *p=1?&a:&b;int *q=0?&a:&b; "
          "int *nil=0?&a:0;char *text=sizeof(a)==1?\"bob!\":\"bad\"; "
          "int main(void){static int *s=1?(0?&b:&a):0; "
          "if(*p!=7 || *q!=9 || nil!=0 || *s!=7)return 1;bob_puts(text);return 0;}");
    guest("#include \"bob.h\"\nchar (*word)[5]=&\"bob!\";char (*end)[5]=&\"bob!\"+1; "
          "char *last=&\"bob!\"[3];int main(void){static char (*saved)[5]=&\"bob!\"; "
          "if(sizeof(*word)!=5 || (*word)[4]!=0 || (*saved)[3]!='!' || *last!='!')return 1; "
          "if((*(end-1))[0]!='b')return 2;bob_puts(*saved);return 0;}");
    guest("#include \"bob.h\"\nint main(void){char (*p)[5]=&\"bob!\";int a[2]; "
          "if(sizeof(&a)!=1 || sizeof(*p)!=5 || (*p)[3]!='!')return 1;bob_puts(*p);return 0;}");
    guest("#include \"bob.h\"\nint main(void){int x=0;int a[3]; "
          "if(sizeof(sizeof(x++))!=1 || sizeof(sizeof(a))!=1 || x!=0)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){int a[2][3]={{1,2,3},{4,5,6}};unsigned int i=1; "
          "if(i[a][2]!=6 || sizeof(i[a])!=3 || sizeof(2[a[0]])!=1)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint count;int f(int *p,int x){count++;return x;} "
          "int main(void){int x;void *p=&x;if(sizeof(f(p,count++))!=1 || count!=0)return 1; "
          "if(sizeof(bob_run(count++))!=1 || count!=0)return 2;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){char word[]=\"bob!\";void *p=word;bob_puts(p);return 0;}");
    guest("#include \"bob.h\"\nint value=7;void *shared=&value;int *items[]={&value,1-1}; "
          "int main(void){void *v=&value;int *p=v;_Bool b=p;int *a[2]={p,0}; "
          "static int *saved=&value;if(*p!=7 || !b || a[1]!=0 || *saved!=7)return 1; "
          "if(shared!=p || items[0]!=p || items[1]!=0)return 2;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint read(int *p){return *p;}int null(int *p){return p==0;} "
          "int generic(void *p){return read(p);}_Bool truth(_Bool b){return b;} "
          "int main(void){int x=7;void *v=&x;if(read(v)!=7 || generic(&x)!=7 || !null(1-1))return 1; "
          "if(!truth(&x) || truth((int*)0))return 2;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint value=7;void *generic(void){return &value;} "
          "int *returned(void){return generic();}int *empty(void){return 1-1;} "
          "_Bool truth(void){return &value;}_Bool zero(void){return (int*)0;} "
          "int main(void){if(*returned()!=7 || empty()!=0 || truth()!=1 || zero()!=0)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){int x=7;int *p;void *v;_Bool flag; "
          "p=&x;v=p;p=v;flag=p;if(*p!=7 || flag!=1)return 1; "
          "p=1-1;flag=p;if(p!=0 || flag!=0 || sizeof(p=0)!=1)return 2; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){int x=7;int *p=&x,*empty=0;void *v=p; "
          "if(p!=v || v!=p || p==0 || 0==p || empty!=1-1)return 1; "
          "if(sizeof(p==v)!=1 || p!=p || empty!=(void*)0)return 2;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){int low=-32768,high=32767,one=1,minus=-1; "
          "for(int i=0;i<64;i++){if(low/one!=low || high/one!=high || high/minus!=-32767)return 1;} "
          "if(-43/7!=-6 || 43/-7!=-6 || -43/-7!=6 || 43/7!=6)return 2; "
          "if(-43%7!=-1 || 43%-7!=1 || -43%-7!=-1 || low%one!=0)return 3; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint depth=5;int leaf(void){return 7;} "
          "int recurse(void){if(depth==0)return leaf();depth--;return recurse()+1;} "
          "int main(void){int saved=42;int result=recurse(); "
          "if(result!=12 || saved!=42 || leaf()!=7)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){int n=0;int *p=&n; "
          "if(!!n!=0 || !!p!=1 || !!(-32768)!=1 || !!0xffff!=1)return 1; "
          "if(!!n++!=0 || n!=1 || !!n++!=1 || n!=2)return 2;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){int high=32767,low=-32768;unsigned all=65535U; "
          "for(int i=0;i<64;i++){if(high*high!=1 || low*high!=-32768)return 1;} "
          "if(low*2!=0 || low*(-1)!=low || all*all!=1U || (-7)*9!=-63)return 2; "
          "if(high*0!=0 || 0*high!=0 || (-1)*(-1)!=1)return 3;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){int rows[2][2];int (*p)[]=&rows[0];int (*q)[2]=&rows[1]; "
          "if(sizeof(*(1?p:q))!=2 || sizeof(*(0?q:p))!=2)return 1; "
          "int x=7;int *v=1?&x:1-1;int *w=0?0:&x;void *generic=0?(void*)0:v; "
          "if(*v!=7 || *w!=7 || generic!=v || (1?0:v)!=0)return 2;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nenum E{ZERO,ONE};int main(void){enum E rows[2][2]; "
          "enum E (*first)[2]=&rows[0];int (*last)[2]=(int (*)[2])&rows[1]; "
          "if(last-first!=1 || first-last!=-1 || sizeof(last-first)!=1)return 1; "
          "enum E *a=&rows[0][0];int *b=(int*)&rows[0][1]; "
          "if(b-a!=1 || a-b!=-1)return 2;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\ntypedef int Word;enum E{ITEM=3};int const global=7; "
          "unsigned const int high=65535U;char const message[]=\"bob!\"; "
          "int inspect(int const value){Word const local=value;enum E const e=ITEM; "
          "const const int *const const p=&global;return local+e+*p;} "
          "int main(void){if(inspect(2)!=12 || high/3U!=21845U || sizeof(int const)!=1)return 1; "
          "if(sizeof(unsigned const int)!=1 || sizeof(Word const)!=1)return 2; "
          "bob_puts(message);return 0;}");
    guest("#include \"bob.h\"\nenum Choice{FIRST=1,SECOND=2};int main(void){unsigned value=65535U;int score=0; "
          "switch(value){case 65535U:score++;break;default:return 1;} "
          "enum Choice choice=SECOND;switch(choice){case FIRST:return 2;case SECOND:score++;break;} "
          "_Bool flag=7;switch(flag){case 0:return 3;case 1:score++;break;} "
          "if(score!=3)return 4;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\ntypedef const int ReadOnly;const int global=7; "
          "int inspect(ReadOnly value){ReadOnly local=value;return local+global;} "
          "int main(void){if(inspect(2)!=9)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint value=7;int * const pointer=&value; "
          "int main(void){*pointer=8;if(value!=8)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\ntypedef const int *ReadPointer;int first=7,second=8; "
          "int main(void){ReadPointer pointer=&first;pointer=&second;if(*pointer!=8)return 1; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint value=7;int main(void){int *writable=&value;const int *read_only=writable; "
          "read_only=&value;if(*read_only!=7)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){const int values[2]={4,5};const int matrix[2][2]={{1,2},{3,4}}; "
          "if(values[1]!=5 || matrix[1][0]!=3)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint inspect(int * const value);int inspect(int *value){return *value;} "
          "int main(void){int value=7;if(inspect(&value)!=7)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint calls;void visit(void){calls++;} "
          "int main(void){(void)42;visit();1?visit():visit();0?(void)7:visit(); "
          "if((visit(),calls)!=4)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){int rows[2][2];int (*p)[]=&rows[0];int (*q)[2]=&rows[1]; "
          "if(!(p<q) || !(q>p) || p>=q || q<=p || sizeof(p<q)!=1)return 1; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint pad0[4096],pad1[4096],pad2[4096],pad3[4096],pad4[4096],pad5[4096],pad6[4096];int data[4096]; "
          "int main(void){int *low=&data[0],*high=&data[4095]; "
          "if((int)low<0 || (int)high>=0)return 1; "
          "if(!(low<high) || !(high>low) || !(low<=high) || !(high>=low))return 2; "
          "if(high<low || low>high || !(low<=low) || !(high>=high))return 3; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nchar bytes[]=\"\\a\\f\\v\\?\\xff\\377\"; "
          "int main(void){if(sizeof(bytes)!=7 || bytes[0]!=7 || bytes[1]!=12 || bytes[2]!=11 || bytes[3]!='?')return 1; "
          "if(bytes[4]!=255 || bytes[5]!=255 || '\\xff'!=255 || '\\377'!=255)return 2; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\ntypedef int unsigned Word;typedef signed Integer; "
          "signed int echo(int signed value);int signed echo(signed value){return value;} "
          "int unsigned divide(unsigned int a,int unsigned b){return a/b;} "
          "int main(void){Integer negative=-3;Word high=65535U;int signed a[2]={-1,-2}; "
          "if(echo(negative)!=-3 || divide(high,3U)!=21845U || a[1]!=-2)return 1; "
          "if(sizeof(signed)!=1 || sizeof(int unsigned)!=1 || ((int unsigned)-1>>15)!=1U)return 2; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nunsigned int constant=65535U/3U;unsigned folded=65535U>>15; "
          "unsigned mixed=((-1)<1U);typedef unsigned Word; "
          "Word divide(Word a,Word b){return a/b;} "
          "int main(void){Word high=65535U,low=3U;int negative=-1; "
          "if(high<low || !(high>low) || high<=low || !(high>=low) || !(low<high))return 1; "
          "if(negative<1U || mixed!=0 || constant!=21845U || folded!=1U)return 2; "
          "if(divide(high,low)!=21845U || high%low!=0U || divide(32768U,65535U)!=0U)return 3; "
          "if(divide(65535U,32768U)!=1U || 65535U%32768U!=32767U)return 4; "
          "Word shift=32769U;if((shift>>1)!=16384U)return 5;shift>>=15;if(shift!=1U)return 6; "
          "high++;if(high!=0U)return 7;high--;if(high!=65535U || sizeof(Word)!=1)return 8; "
          "if((Word)-1!=65535U || ((Word)negative/3U)!=21845U)return 9;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\ntypedef _Bool Flag;Flag truth=42,zero=0,table[2][2]={{-2,0},{7,0}}; "
          "Flag argument(Flag a,Flag b){return a==1 && b==0;}Flag returned(void){return -9;} "
          "int main(void){Flag local=-7;Flag a[3]={3,0,-1};static Flag stored=99; "
          "if(truth!=1 || zero!=0 || table[1][0]!=1 || table[0][1]!=0 || local!=1 || stored!=1)return 1; "
          "if(a[0]!=1 || a[1]!=0 || a[2]!=1 || argument(42,0)!=1 || returned()!=1)return 2; "
          "if((_Bool)-8!=1 || (_Bool)0!=0 || (_Bool)&local!=1 || sizeof(Flag)!=1)return 3; "
          "if((local=17)!=1 || (local*=3)!=1 || (local-=1)!=0)return 4; "
          "if(local++!=0 || local!=1 || ++local!=1 || local--!=1 || local!=0)return 5; "
          "if(--local!=1 || local!=1)return 6;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nenum Mode{FIRST=-2,SECOND,WIDTH=3,LAST=WIDTH+2}; "
          "typedef enum Mode Mode;enum Mode selected=LAST;int values[WIDTH]={1,2,3}; "
          "int identify(Mode m){return m;} "
          "int main(void){Mode m=FIRST;if(m!=-2 || SECOND!=-1 || LAST!=5 || sizeof(Mode)!=1)return 1; "
          "if(identify(selected)!=5 || sizeof(values)!=3)return 2; "
          "{enum Mode{FIRST=7,WIDTH=2};enum Mode local=FIRST;int a[WIDTH];if(local!=7 || sizeof(a)!=2)return 3;} "
          "if(FIRST!=-2 || WIDTH!=3)return 4; "
          "for(enum {BOUND=2} i=0;i<BOUND;i++){enum {INNER=BOUND+1};int a[INNER];if(sizeof(a)!=3)return 5;} "
          "switch(selected){case LAST:bob_puts(\"bob!\");break;default:return 6;}return 0;}");
    guest("#include \"bob.h\"\ntypedef int Word,*Pointer,Row[3];typedef int Word; "
          "typedef Row *Rows;typedef char Text[];typedef void Nothing;Word answer(Nothing);Word answer(Nothing){return 1;}Row matrix[2]={{1,2,3},{4,5,6}}; "
          "Rows rows=matrix;Text message=\"bob!\"; "
          "Word sum(Row p[]){return p[0][0]+p[1][2];} "
          "int main(void){Word n=7;Pointer p=&n; "
          "if(sizeof(Row)!=3 || sizeof(Rows)!=1 || sum(rows)!=7 || *p!=7)return 1; "
          "{typedef char Word;Word local[2];if(sizeof(local)!=2)return 2;} "
          "{int Word=9;if(Word!=9 || sizeof(Word)!=1)return 3;} "
          "Word restored=8;for(Word i=0;i<1;i++){typedef Row Block;Block a={1,2,3};if(a[2]!=3)return 4;} "
          "if(restored!=8 || ((Rows)matrix)[1][1]!=5 || answer()!=1)return 5;bob_puts(message);return 0;}");
    guest("#include \"bob.h\"\nint original[3];extern int original[];int copy[sizeof(original)]; "
          "int f(int p[][3]){int a[sizeof(*p)];return sizeof(a);} "
          "int main(void){int a[4];int b[sizeof(a)+1]; "
          "{char a[2];int inner[sizeof(a)];if(sizeof(inner)!=2)return 1;} "
          "int c[sizeof(a)];if(sizeof(copy)!=3 || sizeof(b)!=5 || sizeof(c)!=4)return 2; "
          "for(int a[2]={0,0};a[0]<1;a[0]++){int d[sizeof(a)];if(sizeof(d)!=2)return 3;} "
          "switch(4){case sizeof(a):if(f((int (*)[3])original)!=3)return 4;bob_puts(\"bob!\");break;default:return 5;}return 0;}");
    guest("#include \"bob.h\"\nint a[sizeof(\"bob!\")];int length=sizeof(a); "
          "int constants[]={sizeof(\"bob!\"),sizeof(1+2),sizeof(int[2][3])}; "
          "int main(void){int local[3];int side=0; "
          "static int count=sizeof(local),quiet=sizeof(side++),zero[sizeof(\"bob!\")]; "
          "if(length!=5 || constants[0]!=5 || constants[1]!=1 || constants[2]!=6)return 1; "
          "if(count!=3 || quiet!=1 || side!=0 || sizeof(zero)!=5 || zero[4]!=0)return 2; "
          "switch(5){case sizeof(\"bob!\"):bob_puts(\"bob!\");break;default:return 3;}return 0;}");
    guest("#include \"bob.h\"\nstatic int count;extern int count;static int count; "
          "static int values[2]={3,4},*pointer=values; "
          "static int next(void);extern int next(void);int next(void){return ++count;} "
          "static int read(void){extern int count;return count+pointer[1];} "
          "int main(void){if(next()!=1 || next()!=2 || read()!=6)return 1; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint next(void){static int count=3;static int *p=&count;return (*p)++;} "
          "int other(void){static int count;return ++count;} "
          "int array(void){static int a[][2]={{1,2},{3,4}};static char word[]=\"bob!\"; "
          "a[1][1]++;if(a[1][1]==6)bob_puts(word);return a[1][1];} "
          "int main(void){if(next()!=3 || next()!=4 || other()!=1 || other()!=2)return 1; "
          "if(array()!=5 || array()!=6)return 2; "
          "{static int count=7;if(count++!=7)return 3;}{static int count=9;if(count!=9)return 4;}return 0;}");
    guest("#include \"bob.h\"\nint value=7;int rows[2][3]={{1,2,3},{4,5,6}}; "
          "int change(void){int value=99;{extern int value;extern int value;value=8; "
          "extern int rows[][3];if(sizeof(rows)!=6 || rows[1][2]!=6)return 100;} "
          "if(value!=99)return 101;return 0;} "
          "int main(void){extern int value;extern int unused; "
          "if(change()!=0 || value!=8)return 1; "
          "{extern int rows[2][3];int (*p)[3]=rows;p[1][2]=9;} "
          "if(rows[1][2]!=9)return 2;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nextern int value;int value;int value=7;extern int value; "
          "extern int rows[][3];int rows[2][3]={{1,2,3},{4,5,6}};int rows[][3]; "
          "int zeros[];int zeros[];extern int unused;extern int incomplete[]; "
          "extern int *pointer;int *pointer=&value;extern int *pointer; "
          "extern int helper(void);int helper(void){return *pointer;} "
          "int main(void){if(value!=7 || helper()!=7 || sizeof(rows)!=6 || rows[1][2]!=6)return 1; "
          "if(sizeof(zeros)!=1 || zeros[0]!=0)return 2;*pointer=8;if(value!=8)return 3; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){int a[2][3];int (*p)[3]=a;int i=0; "
          "if(sizeof(p+=1)!=1 || sizeof(p-=1)!=1 || sizeof(a[i++][0]+=2)!=1 || sizeof(++i)!=1)return 1; "
          "if(p!=a || i!=0)return 2;p+=1;if(p-a!=1)return 3; "
          "p-=1;(*p)[0]=7;if(a[0][0]!=7)return 4;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint first(int (*)[]);int first(int (*)[3]); "
          "int first(int (*p)[3]){return (*p)[2];} "
          "int inspect(int (**)[3]);int inspect(int (**)[]); "
          "int inspect(int (**p)[3]){return (**p)[1];} "
          "int main(void){int rows[1][3]={{1,2,3}};int (*p)[3]=rows; "
          "if(first(rows)!=3 || inspect(&p)!=2)return 1;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){int a[3]={3,-16,0};int i=0,n=2,x=1,y=2; "
          "if((a[i++]<<=n++)!=12 || i!=1 || n!=3 || a[0]!=12)return 1; "
          "if((a[i++]>>=2)!=-4 || i!=2 || a[1]!=-4)return 2; "
          "x<<=y<<=1;if(x!=16 || y!=4)return 3; "
          "x>>=1+1;if(x!=4)return 4;x<<=0;if(x!=4)return 5; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint matrix[2][3]={{1,2,3},{4,5,6}}; "
          "int (*rows)[3]=matrix;int (*whole)[2][3]=&matrix;int (*incomplete)[]=(int (*)[])matrix; "
          "int (*selected[2])[3]={&matrix[1],matrix}; "
          "int sum(int (*p)[3]);int sum(int p[][3]){return (*p)[0]+p[1][2];} "
          "int main(void){int (scalar)=7;int (*local)[3]=rows;int * (pointers[2])={&scalar,&matrix[1][1]}; "
          "if(sizeof(int[2][3])!=6 || sizeof(int (*)[3])!=1 || sizeof(int *[2])!=2 || sizeof(incomplete)!=1)return 1; "
          "if(sizeof(*whole)!=6 || sizeof(*rows)!=3 || (*whole)[1][2]!=6)return 2; "
          "if(sum(local)!=7 || (*selected[0])[1]!=5 || *pointers[0]!=7 || *pointers[1]!=5)return 3; "
          "local++;if((*local)[0]!=4 || local-rows!=1)return 4; "
          "if(((int (*)[3])matrix)[1][1]!=5)return 5;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint values[3]={1,2,3}; "
          "int *selected[][2]={[1][1]=&values[2],[0]={values,&values[1]},[1][1]=values}; "
          "char *messages[]={[2]=\"bob!\",[0]=\"other\"}; "
          "int main(void){int *local[3]={[2]=&values[1],[0]=values}; "
          "if(sizeof(selected)!=4 || selected[1][0]!=0 || *selected[1][1]!=1 || *selected[0][1]!=2)return 1; "
          "if(local[1]!=0 || *local[2]!=2 || messages[1]!=0)return 2;bob_puts(messages[2]);return 0;}");
    guest("#include \"bob.h\"\nint sparse[]={[5]=6,[1]=2,3,[5]=7}; "
          "int matrix[][3]={[2][1]=8,9,[0]={1,2},[1][2]=6}; "
          "char words[3][5]={[2]=\"bob!\",[0]={\"bob!\"}}; "
          "int main(void){int local[5]={[3]=4,[1]=2,3,[3]=8}; "
          "int rows[2][3]={[1]={4,5,6},[1][1]=9,[0][2]=3}; "
          "if(sizeof(sparse)!=6 || sparse[0]!=0 || sparse[1]!=2 || sparse[2]!=3 || sparse[5]!=7)return 1; "
          "if(sizeof(matrix)!=9 || matrix[0][2]!=0 || matrix[1][2]!=6 || matrix[2][2]!=9)return 2; "
          "if(local[0]!=0 || local[2]!=3 || local[3]!=8 || local[4]!=0)return 3; "
          "if(rows[0][2]!=3 || rows[1][0]!=4 || rows[1][1]!=9 || rows[1][2]!=6)return 4; "
          "if(words[1][0]!=0 || words[0][3]!='!')return 5;bob_puts(words[2]);return 0;}");
    guest("#include \"bob.h\"\nint numbers[2][2]={{1,2},{3,4}}; "
          "int *pointers[][2]={{&numbers[0][0],numbers[1]},{&numbers[1][1]}}; "
          "char *messages[]={\"bob!\",\"no\"}; "
          "int main(void){if(*pointers[0][0]!=1 || pointers[0][1][1]!=4 || *pointers[1][0]!=4 || pointers[1][1]!=0)return 1; "
          "bob_puts(messages[0]);return 0;}");
    guest("#include \"bob.h\"\nint cube[2][2][3]={{{1,2,3},{4,5,6}},{{7,8,9},{10,11,12}}}; "
          "int *nested=&cube[1][1][2],*row=cube[1][0],*reversed=&2[1[cube][1]]; "
          "int *indirect=&*(&cube[0][1][1]),*offset=cube[1][1]+1; "
          "int main(void){if(*nested!=12 || row[2]!=9 || *reversed!=12 || *indirect!=5 || *offset!=11)return 1; "
          "*nested=42;if(cube[1][1][2]!=42)return 2;bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint matrix[][3]={{1},{2,3},{4,5,6}}; "
          "int cube[2][2][2]={{{1},{2,3}},{{4,5},{6}}}; "
          "char words[][5]={\"bob!\",{\"bob!\"}};char exact[4]={\"bob!\"}; "
          "int main(void){int local[][2]={{7},{8,9}};int mixed[2][2]={1,2,{3}}; "
          "int scalar={42};char inferred[]={\"bob!\"}; "
          "if(sizeof(matrix)!=9 || matrix[0][1]!=0 || matrix[1][1]!=3 || matrix[2][2]!=6)return 1; "
          "if(cube[0][0][1]!=0 || cube[0][1][1]!=3 || cube[1][1][1]!=0)return 2; "
          "if(local[0][1]!=0 || local[1][1]!=9 || mixed[1][1]!=0 || scalar!=42)return 3; "
          "if(sizeof(words)!=10 || words[0][4]!=0 || words[1][3]!='!' || exact[3]!='!')return 4; "
          "if(sizeof(inferred)!=5)return 5;bob_puts(inferred);return 0;}");
    guest("#include \"bob.h\"\nint matrix[2][3]={1,2,3,4,5,6};int inferred[][2]={7,8,9}; "
          "int sum(int rows[][3]);int sum(int rows[9][3]){int s=0; "
          "if(sizeof(rows)!=1 || sizeof(*rows)!=3)return 100; "
          "for(int i=0;i<2;i++)for(int j=0;j<3;j++)s+=rows[i][j];return s;} "
          "int walk(int rows[][3]){int *first=*rows++;if(first[2]!=3 || (*rows)[0]!=4)return 100; "
          "rows-=1;rows+=1;return (*--rows)[1];} "
          "int main(void){int local[2][2];local[1][1]=8; "
          "if(sizeof(matrix)!=6 || sizeof(matrix[0])!=3 || sizeof(inferred)!=4 || inferred[1][1]!=0)return 1; "
          "if(sum(matrix)!=21 || walk(matrix)!=2 || local[1][1]!=8)return 2; "
          "if((&matrix[1]-&matrix[0])!=1 || (&matrix[0]-&matrix[1])!=-1)return 3; "
          "if(((&matrix+1)-&matrix)!=1 || *(*(matrix+1)+2)!=6)return 4; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void){int a[4];int *p[2];int x=0; "
          "if(sizeof(*&a)!=4 || sizeof((*&a)[2])!=1 || sizeof(1[a])!=1)return 1; "
          "if(sizeof(p)!=2 || sizeof(*p)!=1 || sizeof(**p)!=1)return 2; "
          "if(sizeof((x++,a))!=1 || sizeof(1?a:a)!=1 || x!=0)return 3; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint read(int **);char *same(char *);int *object(int *); "
          "int read(int **p){return **p;}char *same(char a[]){return a;} "
          "int *object(int *a){return a;} "
          "int x=7;int *p=&x;int **q=&p; "
          "int main(void){char word[]=\"bob!\"; "
          "if(read(q)!=7 || *object(p)!=7 || sizeof(int**)!=1)return 1; "
          "bob_puts(same(word));return 0;}");
    guest("#include \"bob.h\"\nint sum(int [],int); int sum(int *values,int count); "
          "int old();int unused(int);char *text(char []); "
          "int main(void){int a[]={2,3,4};char word[]=\"bob!\"; "
          "if(sum(a,3)!=9 || old(7)!=7)return 1;bob_puts(text(word));return 0;} "
          "int sum(int values[10],int count){int result=0; "
          "if(sizeof(values)!=1)return 100;for(int i=0;i<count;i++)result+=values[i];return result;} "
          "int old(int x){return x;}char *text(char word[]){return word;}");
    guest("#include \"bob.h\"\nint x=7;int *p=&x;int data[]={3,4,5}; "
          "int *middle=&data[1],*last=data+2,*also=1+data;char *message=\"bob!\"; "
          "char *part=\"a\\0x\"+2; "
          "int main(void){if(*p!=7 || *middle!=4 || *last!=5 || *also!=4 || *part!='x')return 1; "
          "*p=9;if(x!=9)return 2;bob_puts(message);return 0;}");
    guest("#include \"bob.h\"\nint global=5, other=6, *pointer; char word[]=\"bob!\", pad[7]=\"bob!\"; "
          "int main(void){int x=2,*p=&x,y=x+3,a[]={7,8};char text[]=\"bob!\",*s=text; "
          "int total=0;pointer=&global;*pointer=9; "
          "for(int i=0,j=3;i<3;i++,j--){total+=i+j;} "
          "{int x=4,y=x+1; if(y!=5)return 1;} "
          "if(x!=2 || *p!=2 || y!=5 || a[1]!=8 || total!=9 || global!=9 || other!=6)return 2; "
          "if(s[3]!='!' || pad[6]!=0)return 3;bob_puts(word);return 0;}");
    guest("#include \"bob.h\"\nchar global[]=\"bob!\"; int values[]={3,4,5}; "
          "int main(void){char text[]=\"bo\\0b!\" \"x\";char padded[8]=\"bob!\"; "
          "char exact[4]=\"bob!\";char *a=\"a\\0x\";char *b=\"a\\0y\"; "
          "int local[]={8,9}; "
          "if(sizeof(global)!=5 || sizeof(values)!=3 || values[2]!=5)return 1; "
          "if(sizeof(text)!=7 || text[2]!=0 || text[3]!='b' || text[5]!='x' || text[6]!=0)return 2; "
          "if(sizeof(\"a\\0x\" \"b\")!=5 || a[2]!='x' || b[2]!='y')return 3; "
          "if(sizeof(exact)!=4 || exact[3]!='!' || padded[7]!=0 || local[1]!=9)return 4; "
          "bob_puts(global);return 0;}");
    guest("#include \"bob.h\"\nint table[2+2]={1+2,8/2,1<<3}; "
          "int main(void){int x=0;int sum=0;switch(++x){default:sum=99;break; "
          "case 1:sum=2;case 2:sum+=3;break;}if(sum!=5 || x!=1)return 1; "
          "switch(9){case 0:sum=0;break;default:sum=7;}if(sum!=7)return 2; "
          "for(x=0;x<4;x++){switch(x){case 1:continue;case 2:break; "
          "default:switch(x){case 3:sum+=10;break;default:sum++;} }sum++;} "
          "if(sum!=21 || table[0]!=3 || table[1]!=4 || table[2]!=8 || table[3]!=0)return 3; "
          "switch(4){case 1+3:sum++;break;default:return 4;} "
          "switch(99){} if(sum!=22)return 5; bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint f(void){goto done;return 8;done:return 3;} "
          "int main(void){int x=0;again:x++;if(x<3)goto again;goto done;return 1; "
          "done:if(x!=3 || f()!=3)return 2;{int y=4;goto inside;return 3; "
          "inside:if(y!=4)return 4;} bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint x=11; int f(int x){int total=x; "
          "{int x=7; total+=x;} {int x=8;total+=x;} return total+x;} "
          "int main(void){int x=2;int total=0; {int x=3; {int x=4; total+=x;} total+=x;} "
          "if(x!=2 || total!=7 || f(5)!=25) return 1; "
          "for(int x=0;x<3;x++){int total=x; if(total!=x)return 2;} "
          "if(x!=2 || total!=7)return 3; bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint pair(int a,int b){return a*10+b;} "
          "int main(void){int x=0;int y=0;int a[4]={1}; "
          "int n=1?7:++x; if(n!=7 || x) return 1; "
          "n=0?++x:1?8:9; if(n!=8 || x) return 2; "
          "n=0||1?3:4; if(n!=3) return 3; "
          "n=(x=2,++x,x+4); if(n!=7 || x!=3) return 4; "
          "if(pair((x=1,x+1),3)!=23 || x!=1) return 5; "
          "for(x=0,y=0;x<3;x++,y+=2){} if(y!=6) return 6; "
          "if(sizeof(int)!=1 || sizeof(char)!=1 || sizeof(int*)!=1 || sizeof(a)!=4) return 7; "
          "if(sizeof(\"bob!\")!=5 || sizeof(++x)!=1 || x!=3) return 8; "
          "n=1?(x=5,x+1):0; if(n!=6 || x!=5) return 9; "
          "bob_puts(\"bob!\");return 0;}");
    guest("#include \"bob.h\"\nint main(void) { int i=0; int calls=0; int a[4]={7,8}; "
          "do { i++; if(i==2) continue; calls++; if(i==4) break; } while(i<10); "
          "if(i!=4 || calls!=3 || a[0]!=7 || a[1]!=8 || a[2]!=0 || a[3]!=0) return 1; "
          "do { calls++; } while(0); if(calls!=4) return 2; bob_puts(\"bob!\"); return 0; }");
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
    contains(output,"near token '@'.");
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
static void storage_path(const char *path) {
#ifdef _WIN32
    check(_putenv_s("BOB16_STORAGE",path)==0,"set snapshot fixture path");
#else
    check(setenv("BOB16_STORAGE",path,1)==0,"set snapshot fixture path");
#endif
}
static void wide_kernel_workflow(void) {
    storage_path("build/wide-snapshot.b32");remove("build/wide-snapshot.b32");
    write_text("build/check.in","mem\nalloc 4\npoke 0xd000 77\npeek 0xd000\npoke 0xc000 88\npeek 0xc000\ngo bob.c\nedit compat.c\nint main(void) {\nint n=0x12345678;\nif(n+1!=0x12345679 || n*2!=0x2468ACF0 || n/3!=101806632 || n%3)return 1;\nprintln(\"bob!\");\nreturn 0;\n}\n.\ngo compat.c\ndelete compat.c\nedit tern.c\nint main(void) {\nint a=0;\nint b=1?2:3;\nint c=0?4:1?5:6;\nint d=1?7:a++;\nif(b==2&&c==5&&d==7&&a==0)println(\"bob!\");\nreturn 0;\n}\n.\ngo tern.c\ndelete tern.c\nedit loops.c\nint main(void) {\nint i=0;\nint calls=0;\ndo { i++; if(i==2)continue; calls++; if(i==4)break; } while(i<10);\ndo { calls++; } while(0);\nif(calls==4)println(\"bob!\");\nreturn 0;\n}\n.\ngo loops.c\ndelete loops.c\nedit assign.c\nint main(void) {\nint a=0; int b=1; int i=0;\nint x=(a=b=3);\na+=2; b<<=2; b>>=1;\nif(x==3&&a==5&&b==6&&((i=i+1)==1))println(\"bob!\");\nreturn 0;\n}\n.\ngo assign.c\ndelete assign.c\nedit funcs.c\nint main(void) {\nannounce();\nif(add(fact(5),2)==122)println(\"bob!\");\nreturn 0;\n}\nint announce(void) {\nprintln(\"bob!\");\nreturn 0;\n}\nint fact(int n) {\nif(n<2)return 1;\nreturn n*fact(n-1);\n}\nint add(int x,int y) {\nint value=x+y;\nreturn value;\n}\n.\ngo funcs.c\ndelete funcs.c\nedit scope.c\nint main(void) {\nint value=3;\n{int value=8;if(value!=8)return 1;}\n{int value=9;if(value!=9)return 2;}\nint count=99;\nfor(int count=0;count<2;count++){int inner=count;if(inner!=count)return 3;}\nif(value==3&&count==99)println(\"bob!\");\nreturn 0;\n}\n.\ngo scope.c\ndelete scope.c\nwrite hex.c int main(void){print_hex(4660);println(\"bob!\");return 0;}\ngo hex.c\nload old 0xD002 0xF200 0xF000 0x0062 0x006F 0x0062 0x0021 0x0000\nrun old\nedit note\nbob!\n.\nread note\nls\ncc bob.c app\nrun app\nsave\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"bob32 OS shell workflow");
    char *output=read_text("build/check.out");int successful_apps=0;const char *success_cursor=output;
    while((success_cursor=strstr(success_cursor,"bob!\nExit 0"))){successful_apps++;success_cursor+=11;}
    check(successful_apps==9,"B32 resident programs all complete successfully");
    contains(output,"bob32 OS:");contains(output,"Compiled 45 words.\nbob!\nExit 0");
    contains(output,"bob!\nExit 0");
    contains(output,"0x00001234bob!\nExit 0");
    contains(output,"Loaded.\nbob> bob!\nExit 33");
    contains(output,"0x0000D000: 0x0000004D (77)");contains(output,"poke is limited to heap RAM.");
    contains(output,"bob!\n\n");contains(output,"Files saved to bob-files.b32");free(output);
    FILE *snapshot=fopen("build/wide-snapshot.b32","rb");if(!snapshot)fail("bob32 snapshot missing");
    char magic[4];check(fread(magic,1,4,snapshot)==4 && !memcmp(magic,"B32S",4),"bob32 OS writes native snapshot format");fclose(snapshot);
    write_text("build/check.in","restore yes\nread note\nrun app\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"bob32 snapshot restore workflow");
    output=read_text("build/check.out");contains(output,"Files restored;");contains(output,"bob!\n");contains(output,"bob> bob!\nExit 0");free(output);
    write_text("build/check.in","go bob.c\nhalt\n");
    check(system(BOB32 " --asm32 build/kernel32.basm < build/check.in > build/check.out 2> build/check.err")==0,"full bob32 kernel BASM assembly and execution");
    output=read_text("build/check.out");contains(output,"bob32 OS:");contains(output,"Compiled 45 words.\nbob!\nExit 0");free(output);
    write_text("build/check.in","run32 build/wide-app.b32\nrun32 build/missing-app.b32\ngo bob.c\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"bob32 OS runs a native wide C application");
    output=read_text("build/check.out");contains(output,"bob> bob!\nExit 0");contains(output,"Cannot read B32 app image.");contains(output,"bob!\nExit 0");free(output);
    write_text("build/check.in","import32 build/wide-large.b32 native\nls\nrun native\nrun32 native\nsave\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"import, run and snapshot a native app from bob32 RAM files");
    output=read_text("build/check.out");contains(output,"Imported native app as native");contains(output,"native  ");contains(output,"words (native bob32 image)");check(strstr(output,"bob> bob!\nExit 0") && strstr(strstr(output,"bob> bob!\nExit 0")+1,"bob!\nExit 0"),"run and run32 accept native guest filenames");contains(output,"Files saved to bob-files.b32");free(output);
    write_text("build/check.in","restore yes\nrun native\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"restore and run native app stored in B32S");
    output=read_text("build/check.out");contains(output,"Files restored;");contains(output,"bob> bob!\nExit 0");free(output);
    write_text("build/check.in","import32 build/missing.b32 missing\nimport32 build/wide-bad.b32 corrupt\nimport32 build/wide-truncated.b32 short\nrun absent\nhalt\n");
    write_text("build/wide-bad.b32","not a B32 image");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"native image import errors preserve the shell");
    output=read_text("build/check.out");contains(output,"Cannot read host app image path.");contains(output,"Invalid, truncated, corrupt, or unsupported B32K app image.");contains(output,"Program not found.");free(output);
    storage_path("build/snapshot.b16");
}
static void wide_local_offset_limit(void) {
    char source[4096] = "int main(void){";
    char input[8192] = "edit offsets.c\n";
    char piece[80];
    for (int i = 0; i < 33; i++) {
        snprintf(piece, sizeof(piece), "{int v%d=%d;}\n", i, i);
        strcat(source, piece);
    }
    strcat(source, "return 0;}\n");
    strcat(input, source);
    strcat(input, ".\ncc offsets.c rejected\nhalt\n");
    write_text("build/check.in", input);
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"B32 local offset limit shell workflow");
    char *output = read_text("build/check.out");
    contains(output, "C compile error");
    check(strstr(output, "Compiled ") == NULL, "B32 local frame offset overflow rejected");
    free(output);
}
static void wide_local_arrays(void) {
    write_text("build/check.in",
        "edit array.c\n"
        "int main(void) {\n"
        "int values[4]={1,2,},sum=0;\n"
        "if(values[0]!=1||values[2]!=0)return 2;\n"
        "for(int i=0;i<4;i++)values[i]=i+1;\n"
        "values[0]++;\n"
        "values[1]+=2;\n"
        "values[2]<<=1;\n"
        "for(int i=0;i<4;i++)sum+=values[i];\n"
        "if(sum==16)println(\"bob!\");\n"
        "return 0;\n"
        "}\n"
        ".\ncc array.c array\nrun array\n"
        "edit inc.c\n"
        "int main(void) {\n"
        "int a[2]={4,7},old=a[0]++;\n"
        "if(old!=4||a[0]!=5)return 1;\n"
        "if(++a[1]!=8)return 2;\n"
        "int down=a[1]--;\n"
        "if(down!=8||a[1]!=7)return 3;\n"
        "if(--a[1]!=6)return 4;\n"
        "println(\"bob!\");\n"
        "return 0;\n"
        "}\n"
        ".\ncc inc.c inc\nrun inc\n"
        "edit excess.c\n"
        "int main(void){int a[2]={1,2,3};return 0;}\n"
        ".\ncc excess.c rejected\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"B32 local array shell workflow");
    char *output=read_text("build/check.out");
    contains(output,"Compiled ");contains(output,"bob!\nExit 0");
    char *first_success=strstr(output,"bob!\nExit 0");
    check(first_success && strstr(first_success+1,"bob!\nExit 0"),"B32 array prefix and postfix increment expressions execute");
    contains(output,"C compile error");free(output);
}
static void wide_array_parameters(void) {
    write_text("build/check.in",
        "edit arrays.c\n"
        "int sum(int values[],int length);\n"
        "int bump(int values[],int length);\n"
        "int main(void){int values[3]={1,2,3};\n"
        "int first=sum(values,3);\n"
        "int next=bump(values,3);\n"
        "if(first==6&&next==9&&sum(values,3)==9)println(\"bob!\");\n"
        "return 0;}\n"
        "int sum(int values[],int length){\n"
        "int total=0;for(int i=0;i<length;i++)total+=values[i];\n"
        "return total;}\n"
        "int bump(int values[],int length){\n"
        "for(int i=0;i<length;i++)values[i]++;\n"
        "return sum(values,length);}\n"
        ".\ncc arrays.c arrays\nrun arrays\n"
        "edit scalar_to_array.c\n"
        "int read(int values[],int length);\n"
        "int read(int values[],int length){return values[0];}\n"
        "int main(void){int value=4;return read(value,1);}\n"
        ".\ncc scalar_to_array.c rejected1\n"
        "edit array_to_scalar.c\n"
        "int identity(int value);\n"
        "int identity(int value){return value;}\n"
        "int main(void){int values[2];return identity(values);}\n"
        ".\ncc array_to_scalar.c rejected2\n"
        "edit conflicting_array.c\n"
        "int f(int values[]);\n"
        "int f(int values);\n"
        "int main(void){return 0;}\n"
        ".\ncc conflicting_array.c rejected3\n"
        "edit multidimensional.c\n"
        "int bad(int values[][]);\n"
        "int main(void){return 0;}\n"
        ".\ncc multidimensional.c rejected4\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"B32 array parameter shell workflow");
    char *output=read_text("build/check.out");
    contains(output,"bob> bob!\nExit 0");
    char *error=strstr(output,"C compile error");
    check(error && strstr(error+1,"C compile error") && strstr(strstr(error+1,"C compile error")+1,"C compile error") &&
          strstr(strstr(strstr(error+1,"C compile error")+1,"C compile error")+1,"C compile error"),
          "B32 rejects array argument and declaration mismatches");
    free(output);
}
static void wide_resident_large_output(void) {
    char input[8192] = "edit large.c\nint main(void){int a[1]={0};";
    for (int i = 0; i < 60; i++) {
        strcat(input, "a[0]++;");
        if ((i == 6) || (i > 6 && (i - 6) % 9 == 8 && i < 59)) strcat(input, "\n");
    }
    strcat(input, "\n");
    strcat(input, "if(a[0]==60)println(\"bob!\");return 0;}\n.\ncc large.c large\nrun large\nsave\nhalt\n");
    storage_path("build/wide-resident-large.b32");remove("build/wide-resident-large.b32");
    write_text("build/check.in", input);
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"B32 resident compiler creates and snapshots large app");
    char *output=read_text("build/check.out");
    char *compiled=strstr(output,"Compiled ");
    check(compiled && atoi(compiled+9)>511,"B32 resident compiler output exceeds 511 words");
    contains(output,"bob> bob!\nExit 0");contains(output,"Files saved to bob-files.b32");free(output);
    write_text("build/check.in","restore yes\nrun large\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"B32 large resident app restores and runs from snapshot");
    output=read_text("build/check.out");contains(output,"Files restored;");contains(output,"bob> bob!\nExit 0");free(output);
    storage_path("build/snapshot.b16");
}
static void wide_function_prototypes(void) {
    write_text("build/check.in",
        "write bobs.c int main(void){println(\"Bob!\");return 0;}\n"
        "go bobs.c\n"
        "edit proto.c\n"
        "int add(int a,int b);\n"
        "int loose();\n"
        "int unused(int);\n"
        "int main(void){if(add(2,3)==5&&loose(4,5)==9)println(\"bob!\");return 0;}\n"
        "int add(int a,int b){return a+b;}\n"
        "int loose(int a,int b){return a+b;}\n"
        ".\ncc proto.c proto\nrun proto\n"
        "edit mismatch.c\n"
        "int f(int a);\n"
        "int f(int a,int b);\n"
        "int main(void){return 0;}\n"
        ".\ncc mismatch.c rejected\n"
        "edit oldstyle_mismatch.c\n"
        "int sum();\n"
        "int main(void){return sum(1);}\n"
        "int sum(int a,int b){return a+b;}\n"
        ".\ncc oldstyle_mismatch.c rejected\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"B32 function prototype shell workflow");
    char *output=read_text("build/check.out");
    contains(output,"Compiled ");contains(output,"Bob!\nExit 0");contains(output,"bob!\nExit 0");
    char *errors=strstr(output,"C compile error");
    check(errors && strstr(errors+1,"C compile error"),"B32 no-prototype declaration checks eventual definition arity");
    free(output);
}
static void wide_function_pointers(void) {
    write_text("build/check.in",
        "edit pointer.c\n"
        "int sum(int a,int b);\n"
        "int main(void) {\n"
        "int (*operation)(int,int)=sum;\n"
        "int (*other)(int,int)=0;\n"
        "other=operation;\n"
        "operation=other;\n"
        "if(operation(4,5)==9)println(\"bob!\");\n"
        "return 0;\n"
        "}\n"
        "int sum(int a,int b){return a+b;}\n"
        ".\ncc pointer.c pointer\nrun pointer\n"
        "edit mismatch.c\n"
        "int one(int a);\n"
        "int two(int a,int b);\n"
        "int main(void){int (*operation)(int)=two;return 0;}\n"
        ".\ncc mismatch.c rejected\n"
        "edit integer.c\n"
        "int main(void){int value=7;int (*operation)(int)=value;return 0;}\n"
        ".\ncc integer.c rejected\n"
        "edit mismatch_assign.c\n"
        "int one(int a);int two(int a,int b);\n"
        "int main(void){int (*operation)(int)=one;operation=two;return 0;}\n"
        ".\ncc mismatch_assign.c rejected\n"
        "edit integer_assign.c\n"
        "int main(void){int value=7;int (*operation)(int)=0;operation=value;return 0;}\n"
        ".\ncc integer_assign.c rejected\nhalt\n");
    check(system(BOB32 " --boot build/kernel32.b32 --max-cycles 50000000 < build/check.in > build/check.out 2> build/check.err")==0,"B32 function pointer shell workflow");
    char *output=read_text("build/check.out");
    contains(output,"Compiled ");contains(output,"bob!\nExit 0");
    char *errors=strstr(output,"C compile error");
    int error_count=0;
    while(errors){error_count++;errors=strstr(errors+1,"C compile error");}
    check(error_count==4,"B32 function pointer rejects incompatible and non-function initializers and assignments");
    free(output);
}
static void snapshot_mutate(int offset, int rehash) {
    FILE *file=fopen("build/snapshot.b16","rb");if(!file)fail("snapshot missing");
    unsigned char bytes[8640];size_t count=fread(bytes,1,sizeof(bytes),file);fclose(file);
    check(count==8640,"snapshot exact size");bytes[offset]^=(offset==401?2:1);
    if(rehash) {
        uint32_t hash=2166136261u;
        for(size_t i=16;i<count;i++)hash=(hash^bytes[i])*16777619u;
        for(unsigned i=0;i<4;i++)bytes[12+i]=(unsigned char)(hash>>(8*i));
    }
    file=fopen("build/snapshot.b16","wb");if(!file)fail("snapshot mutate");
    check(fwrite(bytes,1,count,file)==count,"snapshot fixture write");fclose(file);
}
static void persistence(void) {
    storage_path("build/snapshot.b16");remove("build/snapshot.b16");
    char *output=shell("write note bob!\ncc bob.c saved\nsave\nwrite note changed\nrestore\nread note\nrestore yes\nread note\nrun saved\nhalt\n");
    contains(output,"Files saved to");contains(output,"restore yes replaces all RAM files.");
    contains(output,"bob> changed\n");contains(output,"Files restored;");contains(output,"bob> bob!\nExit 0");free(output);
    output=shell("restore yes\nread note\nrun saved\nhalt\n");
    contains(output,"Files restored;");contains(output,"bob> bob!\nExit 0");free(output);
    write_text("build/check.in","kernel.basm\nrestore yes\nrun saved\nhalt\n");
    check(system(BOB " < build/check.in > build/check.out 2> build/check.err")==0,"snapshot BASM restart");
    output=read_text("build/check.out");contains(output,"bob> bob!\nExit 0");not_contains(output,"Kernel changed:");free(output);
    /* A checksum failure, valid-checksum metadata failure and truncation must
       leave an unrelated current file available in the same shell. */
    snapshot_mutate(448,0);
    output=shell("write current bob!\nrestore yes\nread current\nhalt\n");
    contains(output,"Invalid saved files; RAM files unchanged.");contains(output,"bob> bob!\n");free(output);
    output=shell("save\nhalt\n");free(output);
    snapshot_mutate(401,1); /* length[0] high byte -> out of range */
    output=shell("write current bob!\nrestore yes\nread current\nhalt\n");
    contains(output,"Invalid saved files; RAM files unchanged.");contains(output,"bob> bob!\n");free(output);
    write_text("build/snapshot.b16","B16S");
    output=shell("write current bob!\nrestore yes\nread current\nhalt\n");
    contains(output,"Invalid saved files; RAM files unchanged.");contains(output,"bob> bob!\n");free(output);
    output=shell("write note bob!\ncc bob.c saved\nsave\nhalt\n");free(output);
    snapshot_mutate(8,0); /* kernel identity is separate from payload checksum */
    output=shell("restore yes\nread note\nrun saved\ngo bob.c\nhalt\n");
    contains(output,"Kernel changed: old programs omitted.");contains(output,"Program not found.");contains(output,"bob!\nExit 0");free(output);
    storage_path("build/no-such-directory/snapshot.b16");
    output=shell("write note bob!\nsave\nrestore yes\nread note\nhalt\n");
    contains(output,"Storage unavailable.");contains(output,"bob> bob!\n");free(output);
    storage_path("build/snapshot.b16");
    output=shell("load blocked 0xf700 0xe000\nrun blocked\necho bob!\nhalt\n");
    contains(output,"Program fault; shell restored.");contains(output,"bob> bob!\n");free(output);
    storage_path("");
}
static void shell_editing(void) {
    char *output=shell("echo bob!\n\033[A\n\033[A\033[Becho bob!\ncle\t\nrea\t bo\t\nwrite backup bob!\nread ba\t\nread b\t\necho bb!\033[D\033[Do\nxxecho bob!\033[H\033[3~\033[3~\nwrong\025echo bob!\nabc\001\033[3~\033[3~\033[3~echo bob!\005\nhalt\n");
    contains(output,"\033[2J\033[H");
    contains(output,"int main(void) { println(\"bob!\"); return 0; }");
    contains(output,"File not found."); /* read b is ambiguous: no completion */
    int markers=0;for(char *p=output;(p=strstr(p,"bob> bob!\n"))!=NULL;p+=10)markers++;
    check(markers==8,"history, insertion, delete, home/end and clear line");free(output);
    /* History keeps four commands and does not mutate recalled entries. */
    output=shell("echo 1\necho 2\necho 3\necho 4\necho bob!\n\033[A\033[A\033[A\033[A\033[A\n\033[A\033[F\b\b\b\b\b\b\025echo bob!\nhalt\n");
    contains(output,"bob> 2\n");contains(output,"bob> bob!\n");free(output);
    char long_command[512];memset(long_command,'x',128);strcpy(long_command+128,"\t\033[A\necho bob!\nhalt\n");
    output=shell(long_command);contains(output,"Command too long.");contains(output,"bob> bob!\n");free(output);
    output=shell("poke 49152 65535\npeek 49152\npeek 65535\npeek 65536\npoke 49152 -32769\nalloc 32768\nls\nhalt\n");
    contains(output,"0xC000: 0xFFFF (-1)");contains(output,"0xFFFF:");
    contains(output,"Invalid address. Use hex");contains(output,"Invalid value. Use");
    contains(output,"Allocation failed. Use a positive size");contains(output,"Used 1/8 slots");free(output);
}
static void host_language_rejections(void) {
    write_text("build/check.i","int main(void){return 0;}");
    write_text("build/check.basm","bob!\n");write_text("build/check.b16","bob!\n");write_text("build/check.map","bob!\n");
    const char *collisions[]={
        COMPILER " build/check.i build/check.b16 build/check.b16",
        COMPILER " build/check.i build/check.map build/check.b16",
        COMPILER " build/check.i build/check.basm build/check.map",
        COMPILER " build/check.i build/check.i build/check.b16",
        COMPILER " build/check.i build/check.basm build/check.i"
#ifdef _WIN32
        ,COMPILER " build/check.i build/./check.b16 build/check.b16"
        ,COMPILER " build/check.i build/CHECK.B16 build/check.b16"
#endif
    };
    for(unsigned i=0;i<sizeof(collisions)/sizeof(collisions[0]);i++) {
        check(system(collisions[i])!=0,"colliding output paths rejected");
        char *text=read_text("build/check.i");check(!strcmp(text,"int main(void){return 0;}"),"path rejection preserves source");free(text);
        text=read_text("build/check.basm");check(!strcmp(text,"bob!\n"),"path rejection preserves assembly");free(text);
        text=read_text("build/check.b16");check(!strcmp(text,"bob!\n"),"path rejection preserves binary");free(text);
        text=read_text("build/check.map");check(!strcmp(text,"bob!\n"),"path rejection preserves map");free(text);
    }
    /* Feed malformed lexer input directly: a preprocessor would reject it first. */
    const char *unterminated[]={"char *p=\"bob!\\", "int x='\\"};
    for(unsigned i=0;i<sizeof(unterminated)/sizeof(unterminated[0]);i++) {
        write_text("build/check.i",unterminated[i]);
        write_text("build/check.basm","bob!\n");write_text("build/check.b16","bob!\n");
        check(system(COMPILER " build/check.i build/check.basm build/check.b16 > build/compiler.log 2>&1")!=0,"unterminated escape rejected");
        char *log=read_text("build/compiler.log");contains(log,"unterminated escape");free(log);
        char *prior=read_text("build/check.basm");check(!strcmp(prior,"bob!\n"),"lexer failure preserves assembly");free(prior);
        prior=read_text("build/check.b16");check(!strcmp(prior,"bob!\n"),"lexer failure preserves binary");free(prior);
    }
    const char *lexical[]={"char *p=\"bob!\n\";", "int x='\n';", "int x=08;", "int x=1u2;", "int x=1_foo;"};
    const char *diagnostics[]={"newline in literal", "newline in literal", "malformed integer literal", "malformed integer literal", "malformed integer literal"};
    for(unsigned i=0;i<sizeof(lexical)/sizeof(lexical[0]);i++) {
        write_text("build/check.i",lexical[i]);
        check(system(COMPILER " build/check.i build/check.basm build/check.b16 > build/compiler.log 2>&1")!=0,"malformed literal rejected");
        char *log=read_text("build/compiler.log");contains(log,diagnostics[i]);free(log);
    }
    FILE *input=fopen("build/check.i","wb");if(!input)fail("cannot create zero-byte source");
    fputs("int main(void){return 0;}",input);fputc(0,input);fputs("invalid trailing source",input);fclose(input);
    check(system(COMPILER " build/check.i build/check.basm build/check.b16 > build/compiler.log 2>&1")!=0,"source zero byte rejects silent truncation");
    char *log=read_text("build/compiler.log");contains(log,"zero byte in source");free(log);
    char *prior=read_text("build/check.basm");check(!strcmp(prior,"bob!\n"),"input failures preserve assembly");free(prior);
    prior=read_text("build/check.b16");check(!strcmp(prior,"bob!\n"),"input failures preserve binary");free(prior);
    char *relocation_limit=malloc(20000);if(!relocation_limit)fail("allocate relocation limit fixture");
    char *cursor=relocation_limit;cursor+=sprintf(cursor,"int x;");
    for(int i=0;i<15;i++)cursor+=sprintf(cursor,"int a%d[4096];",i);
    cursor+=sprintf(cursor,"int *p[4096]={");
    for(int i=0;i<4096;i++){memcpy(cursor,"&x,",3);cursor+=3;}
    strcpy(cursor,"};int main(void){return 0;}");
    char *deep_expression=malloc(1024);if(!deep_expression)fail("allocate expression nesting fixture");
    cursor=deep_expression;cursor+=sprintf(cursor,"int a[");
    for(int i=0;i<400;i++)*cursor++='(';
    *cursor++='1';
    for(int i=0;i<400;i++)*cursor++=')';
    strcpy(cursor,"];int main(void){return 0;}");
    const char *sources[]={
        relocation_limit,
        deep_expression,
        "int a[(int*)3];int main(void){return 0;}",
        "int main(void){const int value=1;value=2;return 0;}",
        "int main(void){const int value=1;value++;return 0;}",
        "typedef const int ReadOnly;int main(void){ReadOnly value=1;value=2;return 0;}",
        "int main(void){const int value=1;return sizeof(value=2);}",
        "int update(const int value){value=2;return value;}int main(void){return 0;}",
        "const int value=1;int main(void){value=2;return 0;}",
        "int main(void){int value=1,other=2;int * const pointer=&value;pointer=&other;return 0;}",
        "int main(void){int value=1;int * const pointer=&value;pointer++;return 0;}",
        "int update(int * const pointer){int value=1;pointer=&value;return 0;}int main(void){return 0;}",
        "typedef int * const Pointer;int main(void){int value=1,other=2;Pointer pointer=&value;pointer=&other;return 0;}",
        "typedef int * Pointer;int main(void){int value=1,other=2;const Pointer pointer=&value;pointer=&other;return 0;}",
        "int main(void){int value=1;const int *pointer=&value;*pointer=2;return 0;}",
        "int main(void){int value=1;const int *pointer=&value;pointer[0]++;return 0;}",
        "int main(void){int value=1;const int *pointer=&value;int *writable=pointer;return 0;}",
        "int main(void){int value=1;const int **pointer=0;int **writable=pointer;return value;}",
        "int inspect(int *value);int inspect(const int *value);int main(void){return 0;}",
        "int *inspect(void);const int *inspect(void);int main(void){return 0;}",
        "int main(void){const int values[2]={1,2};values[1]=3;return 0;}",
        "int main(void){const int values[2]={1,2};values[1]++;return 0;}",
        "int main(void){const int values[2]={1,2};int *writable=values;return 0;}",
        "int main(void){const int values[2]={1,2};return sizeof(values[1]=3);}",
        "const int value=1;int *writable=&value;int main(void){return 0;}",
        "int main(void){const int value=1;int *writable=&value;return 0;}",
        "int main(void){int * const pointer=0;int **writable=&pointer;return 0;}",
        "int main(void){int value=1;const int *readonly=&value;int *writable=1?readonly:&value;return 0;}",
        "enum E{value=(int*)3};int main(void){return 0;}",
        "enum E{value=32768U};int main(void){return 0;}",
        "int a[3]={[ (int*)1 ]=7};int main(void){return 0;}",
        "int value(void);int a[value()];int main(void){return 0;}",
        "int a[sizeof((int)(void)0)];int main(void){return 0;}",
        "enum E{value=sizeof((int)(void)0)};int main(void){return 0;}",
        "int main(void){int value=0;int a[2]={[value++]=1};return 0;}",
        "int value(void);int main(void){switch(1){case value():return 0;}}",
        "int main(void){int value=0;switch(1){case value++:return 0;}}",
        "int main(void){void *p=(void*)0xc000;++*p;return 0;}",
        "int main(void){void *p=(void*)0xc000;(*p)++;return 0;}",
        "int main(void){int a[2];++a;return 0;}",
        "int x=1,a;int *p=x?&a:0;int main(void){return 0;}",
        "int a;char b;int *p=1?&a:&b;int main(void){return 0;}",
        "int main(void){return sizeof(&1);}",
        "int main(void){int x;return sizeof(&(x+1));}",
        "int main(void){int x;return sizeof(&x++);}",
        "int main(void){int x;return sizeof(&(int)x);}",
        "int main(void){return sizeof(sizeof((void)0));}",
        "int f(int x){return x;}int main(void){return sizeof(sizeof(f()));}",
        "int main(void){void *p=0;return sizeof(sizeof(&p[0]));}",
        "int main(void){int *p;return sizeof(sizeof(p=7));}",
        "void f(void){}int main(void){int a[2];return sizeof(a[f()]);}",
        "void f(void){}int main(void){int a[2];return sizeof(f()[a]);}",
        "int main(void){void *p=0;return sizeof(&p[0]);}",
        "int main(void){int (*p)[]=0;return sizeof(&p[0]);}",
        "int f(int *p){return 0;}int main(void){return sizeof(f(7));}",
        "int f(int x){return x;}int main(void){return sizeof(f());}",
        "int f(void){return 0;}int main(void){return sizeof(f(1));}",
        "int f(int *p){return 0;}int main(void){char x;return sizeof(f(&x));}",
        "#include \"bob.h\"\nint main(void){int x;return sizeof(bob_run(&x));}",
        "int main(void){bob_puts(7);return 0;}",
        "int main(void){int x;bob_puts(&x);return 0;}",
        "int main(void){char a[2];bob_gets(a,a);return 0;}",
        "int main(void){char a[2];bob_snapshot(a,0);return 0;}",
        "int main(void){int x;bob_putc(&x);return 0;}",
        "int main(void){int x;return bob_run(&x);}",
        "int main(void){int x;return bob_call(&x);}",
        "int main(void){int *p=7;return 0;}",
        "int main(void){int x;char *p=&x;return 0;}",
        "int main(void){int x;int n=&x;return 0;}",
        "int main(void){int *p[2]={0,7};return 0;}",
        "int *p=7;int main(void){return 0;}",
        "int x;char *p=&x;int main(void){return 0;}",
        "int *p[2]={0,7};int main(void){return 0;}",
        "int main(void){static int *p=7;return 0;}",
        "void f(void){}int main(void){int x=f();return 0;}",
        "int f(int *p){return 0;}int main(void){return f(7);}",
        "int f(char *p){return 0;}int main(void){int x;return f(&x);}",
        "int f(int n){return n;}int main(void){int x;return f(&x);}",
        "int *f(void){return 7;}int main(void){return 0;}",
        "int value;char *f(void){return &value;}int main(void){return 0;}",
        "int value;int f(void){return &value;}int main(void){return 0;}",
        "int main(void){int *p;p=7;return 0;}",
        "int main(void){int *p;char *q;p=q;return 0;}",
        "int main(void){int *p;int x;x=p;return 0;}",
        "int main(void){int *p;return sizeof(p=7);}",
        "int main(void){int *p;return p==7;}",
        "int main(void){int *p;int zero=0;return p!=zero;}",
        "int main(void){int *p;char *q;return p==q;}",
        "int main(void){int *p;return sizeof(p==7);}",
        "int x=1?1:(void)0;int main(void){return 0;}",
        "int x=1||(void)0;int main(void){return 0;}",
        "int x=0&&(void)0;int main(void){return 0;}",
        "int x=(int)(void)0;int main(void){return 0;}",
        "int main(void){int *p;char *q;1?p:q;return 0;}",
        "int main(void){int *p;1?p:7;return 0;}",
        "int main(void){int *p;int zero=0;1?p:zero;return 0;}",
        "int main(void){int *p;char *q;return sizeof(1?p:q);}",
        "enum A{X};enum B{Y};int main(void){enum A *a;enum B *b;return a-b;}",
        "int main(void){int (*a)[2];int (*b)[3];return a-b;}",
        "int main(void){int (*a)[2];int (*b)[];return a-b;}",
        "int main(void){int *p;switch(p){}return 0;}",
        "int main(void){int a[2];switch(a){}return 0;}",
        "void f(void){}int main(void){switch(f()){}return 0;}",
        "int main(void){switch(1){case (int*)0:break;}return 0;}",
        "int main(void){switch(1){case (void)0:break;}return 0;}",
        "void f(void){return 1;}int main(void){return 0;}",
        "void f(void){}void g(void){return f();}int main(void){return 0;}",
        "int f(void){return;}int main(void){return 0;}",
        "void f(void){}int main(void){return f();}",
        "void f(void){}int g(int x){return x;}int main(void){return g(f());}",
        "void f(void){}void bob_putc(int);int main(void){bob_putc(f());return 0;}",
        "void f(void){}int main(void){return f()+1;}",
        "void f(void){}int main(void){return (int)f();}",
        "void f(void){}int main(void){return sizeof((int)f());}",
        "void f(void){}int main(void){return !f();}",
        "void f(void){}int main(void){return f()&&1;}",
        "void f(void){}int main(void){return 1||f();}",
        "void f(void){}int main(void){int x;x=f();return 0;}",
        "void f(void){}int main(void){int x;return sizeof(x=f());}",
        "void f(void){}int main(void){return sizeof(f()+1);}",
        "void f(void){}int main(void){1?f():1;return 0;}",
        "void f(void){}int main(void){f()?1:2;return 0;}",
        "void f(void){}int main(void){if(f())return 1;return 0;}",
        "void f(void){}int main(void){while(f()){}return 0;}",
        "void f(void){}int main(void){do{}while(f());return 0;}",
        "void f(void){}int main(void){for(;f();){}return 0;}",
        "int main(void){int *p;return p<1;}",
        "int main(void){int *p;char *q;return p<q;}",
        "int main(void){void *p,*q;return p<q;}",
        "int main(void){int *p;return sizeof(p<1);}",
        "char *p=\"\\x100\";int main(void){return 0;}",
        "char *p=\"\\x100000000000000000000000000000\";int main(void){return 0;}",
        "char *p=\"\\400\";int main(void){return 0;}",
        "int int x;int main(void){return 0;}",
        "unsigned unsigned x;int main(void){return 0;}",
        "signed unsigned x;int main(void){return 0;}",
        "unsigned signed x;int main(void){return 0;}",
        "int signed int x;int main(void){return 0;}",
        "int void x;int main(void){return 0;}",
        "unsigned f(unsigned);unsigned f(int x){return x;}int main(void){return 0;}",
        "unsigned x;int x;int main(void){return 0;}",
        "int main(void){int *p;return (int)(p/2U);}",
        "_Bool f(_Bool);_Bool f(int x){return x;}int main(void){return 0;}",
        "_Bool x;int x;int main(void){return 0;}",
        "enum E{A,A};int main(void){return 0;}",
        "enum E{A};enum E{B};int main(void){return 0;}",
        "enum E{};int main(void){return 0;}",
        "enum E{A=32767,B};int main(void){return 0;}",
        "enum E{A};int A;int main(void){return 0;}",
        "int A;enum E{A};int main(void){return 0;}",
        "int main(void){{enum E{A};}int a[A];return 0;}",
        "int main(void){{enum E{A};}enum E e;return 0;}",
        "enum E{A};int main(void){A=1;return 0;}",
        "enum E{A};enum F{B};int f(enum E);int f(enum F x){return 0;}int main(void){return 0;}",
        "typedef int T;typedef char T;int main(void){return 0;}",
        "typedef int T;int T;int main(void){return 0;}",
        "int T;typedef int T;int main(void){return 0;}",
        "typedef int T;int T(void){return 0;}int main(void){return 0;}",
        "int f(void);typedef int f;int main(void){return 0;}",
        "int f(int x){typedef int x;return 0;}int main(void){return 0;}",
        "int main(void){{typedef int Gone;}Gone x;return 0;}",
        "typedef void Nothing;int main(void){Nothing x;return 0;}",
        "int a[sizeof(later)];int later[3];int main(void){return 0;}",
        "int main(void){{int hidden[3];}int a[sizeof(hidden)];return 0;}",
        "int main(void){for(int hidden[2];0;){}int a[sizeof(hidden)];return 0;}",
        "int main(void){int a[sizeof(a)];return 0;}",
        "int x;static int x;int main(void){return 0;}",
        "static int x;int x;int main(void){return 0;}",
        "extern int x;static int x;int main(void){return 0;}",
        "int f(void);static int f(void){return 0;}int main(void){return 0;}",
        "extern int f(void);static int f(void);int main(void){return 0;}",
        "static int a[];int main(void){return 0;}",
        "int main(void){int x=1;static int y=x;return 0;}",
        "int main(void){int x;static int *p=&x;return 0;}",
        "int f(void){return 1;}int main(void){static int x=f();return 0;}",
        "int main(void){static int x;static int x;return 0;}",
        "int main(void){{static int hidden;}return hidden;}",
        "int main(void){static int a[];return 0;}",
        "int main(void){extern int missing;return missing;}",
        "int x;int main(void){extern char x;return 0;}",
        "int x;int main(void){int x;extern int x;return 0;}",
        "int x;int main(void){extern int x;int x;return 0;}",
        "int main(void){extern int x=1;return 0;}",
        "int main(void){{extern int hidden;}return hidden;}",
        "int main(void){extern int hidden;return 0;}int f(void){return hidden;}",
        "extern int x;char x;int main(void){return 0;}",
        "int x;int x=1;int x=2;int main(void){return 0;}",
        "extern int x;int main(void){return x;}",
        "extern int a[];int main(void){return sizeof(a);}",
        "int a[2];extern int a[3];int main(void){return 0;}",
        "int main(void){int *p,*q;p-=q;return 0;}",
        "int main(void){int *p;int x=0;x+=p;return 0;}",
        "int main(void){int *p,*q;return sizeof(p-=q);}",
        "int main(void){int *p;return sizeof(p<<=1);}",
        "int main(void){int a[2];return sizeof(a=0);}",
        "int main(void){int a[2];return sizeof(a++);}",
        "int main(void){return sizeof(1=2);}",
        "int main(void){return sizeof(++1);}",
        "int main(void){void *p;return sizeof(p+=1);}",
        "int f(int (*)[]);int f(int (*)[2]);int f(int (*)[3]);int main(void){return 0;}",
        "int f(int (*)[]);int f(char (*)[]);int main(void){return 0;}",
        "int f(int (**)[]);int f(int (*)[]);int main(void){return 0;}",
        "int main(void){int *p;p<<=1;return 0;}",
        "int main(void){int *p;p>>=1;return 0;}",
        "int main(void){int *p;return (int)(p<<1);}",
        "int main(void){int *p;return sizeof(p>>1);}",
        "int main(void){int *p;return (int)(p*2);}",
        "int main(void){int *p;return sizeof(p|1);}",
        "int main(void){int *p;return sizeof(~p);}",
        "void a[2];int main(void){return 0;}",
        "int a[2][];int main(void){return 0;}",
        "int main(void){return sizeof(int[]);}",
        "int main(void){int (*p)[];return sizeof(*p);}",
        "int main(void){int (*p)[];p++;return 0;}",
        "int main(void){return sizeof(int[3][]);}",
        "int main(void){return (int[2])1;}",
        "int f(int (*)[2]);int f(int (*p)[3]){return 0;}int main(void){return 0;}",
        "int (*a)[3];int (*a)[2];int main(void){return 0;}",
        "int a[2]={[2]=1};int main(void){return 0;}",
        "int a[2]={[-1]=1};int main(void){return 0;}",
        "int a[2][3]={[0][3]=1};int main(void){return 0;}",
        "int a[2]={[0][0]=1};int main(void){return 0;}",
        "int a={[0]=1};int main(void){return 0;}",
        "int a[]={[4096]=1};int main(void){return 0;}",
        "int main(void){int n=1;int a[2]={[n]=1};return 0;}",
        "int a[2]={[1] 1};int main(void){return 0;}",
        "int a[2][2]={{1,2,3},{4}};int main(void){return 0;}",
        "int a[1][2]={{1},{2}};int main(void){return 0;}",
        "int main(void){int a[2][2]={{1},{2,3,4}};return 0;}",
        "char a[4]={\"bob!\",1};int main(void){return 0;}",
        "char a[1][3]={\"bob!\"};int main(void){return 0;}",
        "int main(void){int a[1]={1,2};return 0;}",
        "int main(void){return sizeof(void);}",
        "void f(void){} int main(void){return sizeof(f());}",
        "int main(void){return 1 ? 2;}",
        "int main(void){return sizeof((void)0);}",
        "int main(void){goto missing;return 0;}",
        "int main(void){same:;same:return 0;}",
        "int main(void){int x=1;int x=2;return x;}",
        "int f(int x){int x=2;return x;} int main(void){return f(1);}",
        "int main(void){{int x=1;}return x;}",
        "int main(void){return x;int x=1;}",
        "int main(void){for(int x=0;x<1;x++){}return x;}",
        "int f(int x,int x){return x;}int main(void){return f(1,2);}",
        "int main(void){switch(1){case 1:;case 1:return 0;}}",
        "int main(void){switch(1){default:;default:return 0;}}",
        "case 1:return 0;",
        "int main(void){case 1:return 0;}",
        "int main(void){switch(1){default:continue;}return 0;}",
        "int a[1/0];int main(void){return 0;}",
        "int a[1<<16];int main(void){return 0;}",
        "int main(void){char text[3]=\"bob!\";return 0;}",
        "int main(void){int text[]=\"bob!\";return 0;}",
        "int main(void){char text[];return 0;}",
        "int main(void){int x=1,x=2;return x;}",
        "int x=1,x=2;int main(void){return x;}",
        "int x;int *p=&x[1];int main(void){return 0;}",
        "int x;int *p=&x;int *q=p;int main(void){return 0;}",
        "int f(int);char f(int x){return x;}int main(void){return 0;}",
        "int f(int,int);int f(int x){return x;}int main(void){return 0;}",
        "int f(char);int f(int x){return x;}int main(void){return 0;}",
        "int f(int);int f(int,int);int main(void){return 0;}",
        "int f(int x=1){return x;}int main(void){return 0;}",
        "int f;int f(void){return 0;}int main(void){return 0;}",
        "int f(int*);int f(char *p){return 0;}int main(void){return 0;}",
        "int f(int**);int f(int *p){return 0;}int main(void){return 0;}",
        "char *f(void);int *f(void){return 0;}int main(void){return 0;}",
        "int f(char []);int f(int *p){return 0;}int main(void){return 0;}",
        "int main(void){void *p;return sizeof(*p);}",
        "int main(void){int x;return sizeof(*x);}",
        "int main(void){int x;return sizeof(x[0]);}",
        "int main(void){int a[2][];return 0;}",
        "int main(void){int a[4096][2];return 0;}",
        "int main(void){int a[2],b[2];return (int)(a+b);}",
        "int main(void){int a[2];a=0;return 0;}",
        "int main(void){int a[2];a++;return 0;}",
        "int main(void){int a[4096][4096];return 0;}",
        "int main(void){int a[2],b[2];return sizeof(a+b);}",
        "int main(void){void *p;return sizeof(p+1);}",
        "int main(void){int a[2];return sizeof(a[a]);}",
        "int one(int x){return x;}int two(int x,int y){return x+y;}int main(void){int (*p)(int)=two;return 0;}",
        "int one(int x){return x;}int main(void){int (*p)(int)=one;return p(1,2);}",
        "int main(void){int x=1;int (*p)(int)=&x;return 0;}",
        "int main(void){int x;int (*p)(int)=(int (*)(int))&x;return 0;}"
    };
    for(unsigned i=0;i<sizeof(sources)/sizeof(sources[0]);i++) {
        write_text("build/check.c",sources[i]);
        write_text("build/check.basm","bob!\n");write_text("build/check.b16","bob!\n");
        check(system("gcc -E -P -nostdinc -undef -I kernel build/check.c -o build/check.i > build/compiler.log 2>&1")==0,"language rejection preprocessing");
        check(system(COMPILER " build/check.i build/check.basm build/check.b16 > build/compiler.log 2>&1")!=0,"invalid C must fail compilation");
        if(i==0){char *diagnostic=read_text("build/compiler.log");contains(diagnostic,"code/relocation limit exceeded");free(diagnostic);}
        if(i==1){char *diagnostic=read_text("build/compiler.log");contains(diagnostic,"expression nesting exceeds 256 levels");free(diagnostic);}
        char *prior=read_text("build/check.basm");check(!strcmp(prior,"bob!\n"),"failed compile preserves BASM output");free(prior);
        prior=read_text("build/check.b16");check(!strcmp(prior,"bob!\n"),"failed compile preserves binary output");free(prior);
    }
    free(deep_expression);free(relocation_limit);
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
    compiler_extensions();wide_guest();editor_and_aliases();
    program_loading_and_recovery();native_string_library();host_compiler_and_runtime();host_language_rejections();file_management();compile_workflow();usability();persistence();shell_editing();image_validation();basm_route();wide_kernel_workflow();wide_local_arrays();wide_array_parameters();wide_resident_large_output();wide_function_prototypes();wide_function_pointers();wide_local_offset_limit();
    printf("bob!\n%d checks passed.\n",checks);return 0;
}
