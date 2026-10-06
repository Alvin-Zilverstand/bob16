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
    const char *sources[]={
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
        "int main(void){int a[2];return sizeof(a[a]);}"
    };
    for(unsigned i=0;i<sizeof(sources)/sizeof(sources[0]);i++) {
        write_text("build/check.c",sources[i]);
        write_text("build/check.basm","bob!\n");write_text("build/check.b16","bob!\n");
        check(system("gcc -E -P -nostdinc -undef -I kernel build/check.c -o build/check.i > build/compiler.log 2>&1")==0,"language rejection preprocessing");
        check(system(COMPILER " build/check.i build/check.basm build/check.b16 > build/compiler.log 2>&1")!=0,"invalid C must fail compilation");
        char *prior=read_text("build/check.basm");check(!strcmp(prior,"bob!\n"),"failed compile preserves BASM output");free(prior);
        prior=read_text("build/check.b16");check(!strcmp(prior,"bob!\n"),"failed compile preserves binary output");free(prior);
    }
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
    program_loading_and_recovery();host_compiler_and_runtime();host_language_rejections();file_management();compile_workflow();usability();persistence();shell_editing();image_validation();basm_route();
    printf("bob!\n%d checks passed.\n",checks);return 0;
}
