#include "bob.h"
#include "bob_fs.h"
#include "bob_process.h"
#include "bob_string.h"

static int append(char *buffer,int *length,char value) {
    if(*length>=126)return 0;
    buffer[(*length)++]=value;buffer[*length]=0;return 1;
}

static void print_number(int value) {
    char digits[12];int count=0;
    if(value<0){bob_putc('-');value=-value;}
    do {digits[count++]=(char)('0'+value%10);value/=10;} while(value&&count<11);
    while(count)bob_putc(digits[--count]);
    bob_putc('\n');
}

int main(int argc,char **argv) {
    int i,size,kind,length=0,status,child_status=-1;
    char name[24],arguments[128];
    arguments[0]=0;
    if(argc<2) {
        bob_puts("Native apps:");
        for(i=0;i<8;i++) {
            kind=bob_file_list(i,name,24,&size);
            if(kind==3){bob_puts(name);bob_putc('\n');}
        }
        bob_puts("Usage: run launcher APP [ARG ...]");
        return 0;
    }
    for(i=2;i<argc;i++) {
        int j;
        if(i>2&&!append(arguments,&length,' '))return 1;
        if(!append(arguments,&length,'"'))return 1;
        for(j=0;argv[i][j];j++) {
            if((argv[i][j]=='"'||argv[i][j]=='\\')&&!append(arguments,&length,'\\'))return 1;
            if(!append(arguments,&length,argv[i][j]))return 1;
        }
        if(!append(arguments,&length,'"'))return 1;
    }
    status=bob_app_run(argv[1],arguments,&child_status);
    if(status==-1)bob_puts("App not found.");
    else if(status==-2)bob_puts("File is not a native bob32 app.");
    else if(status==-4)bob_puts("Invalid app or arguments.");
    else if(status==-5)bob_puts("Not enough emulator memory to launch app.");
    else if(status==-3)bob_puts("App timed out.");
    else if(status<-5)bob_puts("App could not be launched.");
    else {bob_puts("Child exit:");print_number(child_status);}
    return status<0?1:child_status;
}
