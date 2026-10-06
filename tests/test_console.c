/* Native Windows console integration, using a hidden test-owned console. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef BOB_VERIFY
#define BOB "build\\bob-next.exe --os"
#else
#define BOB "bob.exe --os"
#endif
static HANDLE input,output,child;
static int checks;
static void fail(const char *message) {
    if(child)TerminateProcess(child,1);
    FreeConsole();fprintf(stderr,"Console check failed: %s (%lu)\n",message,GetLastError());exit(1);
}
static void check(int condition,const char *message) {checks++;if(!condition)fail(message);}
static char screen[65536];
static void capture(void) {
    CONSOLE_SCREEN_BUFFER_INFO info;DWORD read;
    if(!GetConsoleScreenBufferInfo(output,&info))fail("screen info");
    unsigned size=(unsigned)info.dwSize.X*info.dwSize.Y;
    if(size>=sizeof(screen))size=sizeof(screen)-1;
    COORD start={0,0};
    if(!ReadConsoleOutputCharacterA(output,screen,size,start,&read))fail("screen capture");
    unsigned out=0,width=(unsigned)info.dwSize.X;
    for(unsigned i=0;i<read;i+=width) {
        unsigned end=i+width;if(end>read)end=read;
        while(end>i && screen[end-1]==' ')end--;
        memmove(screen+out,screen+i,end-i);out+=end-i;screen[out++]='\n';
    }
    screen[out]=0;
}
static int lines(const char *text) {
    capture();int count=0;size_t length=strlen(text);
    for(char *p=screen;*p;) {
        char *end=strchr(p,'\n');if(!end)break;
        if((size_t)(end-p)==length && !memcmp(p,text,length))count++;
        p=end+1;
    }
    return count;
}
static void wait_text(const char *text) {
    for(int i=0;i<200;i++){capture();if(strstr(screen,text))return;Sleep(10);}
    FILE *file=fopen("build/console-screen.txt","wb");if(file){fputs(screen,file);fclose(file);}
    fail(text);
}
static void wait_lines(const char *text,int count) {
    for(int i=0;i<200;i++){if(lines(text)>=count){checks++;return;}Sleep(10);}
    fail("expected output line");
}
static void key(WORD vk,char character) {
    INPUT_RECORD records[2]={0};DWORD written;
    records[0].EventType=KEY_EVENT;records[0].Event.KeyEvent.bKeyDown=TRUE;
    records[0].Event.KeyEvent.wRepeatCount=1;records[0].Event.KeyEvent.wVirtualKeyCode=vk;
    records[0].Event.KeyEvent.wVirtualScanCode=(WORD)MapVirtualKeyA(vk,MAPVK_VK_TO_VSC);
    records[0].Event.KeyEvent.uChar.AsciiChar=character;
    records[1]=records[0];records[1].Event.KeyEvent.bKeyDown=FALSE;
    if(!WriteConsoleInputA(input,records,2,&written) || written!=2)fail("key injection");
}
static void type(const char *text) {
    for(;*text;text++) {
        WORD vk=*text=='\n'?VK_RETURN:*text=='\t'?VK_TAB:(WORD)(VkKeyScanA(*text)&255);
        key(vk,*text=='\n'?'\r':*text);
    }
}
int main(void) {
    HANDLE oldIn=GetStdHandle(STD_INPUT_HANDLE),oldOut=GetStdHandle(STD_OUTPUT_HANDLE),oldErr=GetStdHandle(STD_ERROR_HANDLE);
    STARTUPINFOA startup={0};PROCESS_INFORMATION process={0};startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    char command[]=BOB;
    check(CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_NEW_CONSOLE,NULL,NULL,&startup,&process),"create hidden console emulator");
    child=process.hProcess;CloseHandle(process.hThread);FreeConsole();
    int attached=0;for(int i=0;i<200 && !attached;i++){attached=AttachConsole(process.dwProcessId);if(!attached)Sleep(10);}
    check(attached,"attach test-owned console");
    input=CreateFileA("CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    output=CreateFileA("CONOUT$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    check(input!=INVALID_HANDLE_VALUE && output!=INVALID_HANDLE_VALUE,"console handles");
    wait_text("bob>");int count=lines("bob!");
    type("echo bb!");key(VK_LEFT,0);key(VK_LEFT,0);type("o\n");wait_lines("bob!",++count);
    key(VK_UP,0);type("\n");wait_lines("bob!",++count);
    type("rea\t bo\t\n");wait_text("int main(void)");checks++;
    type("g\t bo\t\n");wait_text("Compiled ");wait_lines("bob!",++count);wait_text("Exit 0");checks++;
    type("xxecho bob!");key(VK_HOME,0);key(VK_DELETE,0);key(VK_DELETE,0);key(VK_END,0);type("\n");wait_lines("bob!",++count);
    type("wrong");key('U',21);type("echo bob!\n");wait_lines("bob!",++count);
    SMALL_RECT window={0,0,39,19};COORD dimensions={40,200};
    check(SetConsoleWindowInfo(output,TRUE,&window) && SetConsoleScreenBufferSize(output,dimensions),"narrow console");
    type("echo bob!");for(int i=0;i<118;i++)type(" ");type("\n");wait_lines("bob!",++count);
    for(int i=0;i<128;i++){type("x");}
    type("\n");wait_text("Command too long.");checks++;
    type("echo bob!\n");wait_lines("bob!",++count);
    type("edit note\n");wait_text("bob edit: note");wait_text("^O Save");checks++;
    type("bb!");key(VK_LEFT,0);key(VK_LEFT,0);type("o");key(VK_END,0);type("\nbob!");
    key('O',15);wait_text("Saved to RAM");checks++;
    key('X',24);type("read note\n");wait_lines("bob!",2);
    type("edit note\n");wait_text("bob edit: note");type("x");key('X',24);wait_text("Save? Y=yes");checks++;
    key('C',3);wait_text("bob edit: note *");key('Z',26);wait_text("Undo/redo applied.");checks++;
    key('Z',26);key('X',24);type("nread note\n");wait_lines("bob!",2);
    type("edit note\n");wait_text("bob edit: note");key(VK_DOWN,0);key(VK_HOME,0);key(VK_DELETE,0);type("b");key('X',24);
    type("echo bob!\n");wait_lines("bob!",1);
    type("edit fresh\n");wait_text("bob edit: fresh");type("bob!");key('X',24);wait_text("Save? Y=yes");
    type("yread fresh\n");wait_lines("bob!",1);
    type("edit limit\n");wait_text("bob edit: limit");
    for(int i=0;i<512;i++){type("b");}
    wait_text("Full: 511 chars.");checks++;
    key('O',15);wait_text("Saved to RAM");key('X',24);type("ls\n");wait_text("limit  511 chars (text)");checks++;
    type("write a bob!\nwrite b bob!\nwrite c bob!\nedit extra\n");wait_text("bob edit: extra");
    type("bob!");key('O',15);wait_text("Save failed; changes kept.");checks++;
    key('X',24);wait_text("Save? Y=yes");type("y");wait_text("bob edit: extra *");
    key('X',24);type("nls\n");wait_text("Used 8/8 slots");checks++;
    type("halt\n");check(WaitForSingleObject(child,5000)==WAIT_OBJECT_0,"console halt");
    DWORD exitCode;check(GetExitCodeProcess(child,&exitCode) && exitCode==0,"console exit status");
    CloseHandle(input);CloseHandle(output);CloseHandle(child);child=NULL;FreeConsole();
    SetStdHandle(STD_INPUT_HANDLE,oldIn);SetStdHandle(STD_OUTPUT_HANDLE,oldOut);SetStdHandle(STD_ERROR_HANDLE,oldErr);
    printf("bob!\n%d native console checks passed.\n",checks);return 0;
}
