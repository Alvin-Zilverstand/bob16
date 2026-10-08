#include "bob.h"
#include "bob_fs.h"

static int same_text(const char *left,const char *right) {
    int i=0;while(left[i]&&left[i]==right[i])i++;
    return left[i]==right[i];
}

/* 1=text note, 0=missing, -1=name belongs to a non-text file. */
static int note_kind(const char *wanted) {
    int kind=bob_file_kind(wanted);
    if(kind<0)return 0;
    return kind==BOB_FILE_KIND_TEXT?1:-1;
}

static int join_arguments(char *text,int *length,int argc,char **argv,int start) {
    *length=0;
    for(int i=start;i<argc;i++) {
        if(i>start) {
            if(*length>=511)return 0;
            text[(*length)++]=' ';
        }
        for(int j=0;argv[i][j];j++) {
            if(*length>=511)return 0;
            text[(*length)++]=argv[i][j];
        }
    }
    text[*length]=0;
    return 1;
}

int main(int argc, char **argv) {
    char text[512];
    int length;
    int i;
    if (argc < 2) {
        bob_puts("Usage: notes show NAME | put/edit NAME [TEXT] | add NAME [TEXT] | delete NAME");
        return 1;
    }
    if (same_text(argv[1], "show") && argc == 3) {
        if(note_kind(argv[2])<0){bob_puts("Not a text note.");return 1;}
        length = bob_file_read(argv[2], text, 512);
        if (length < 0) { bob_puts("Note not found."); return 1; }
        for (i = 0; i < length; i++) bob_putc(text[i]);
        bob_putc('\n');
        return 0;
    }
    if ((same_text(argv[1], "put")||same_text(argv[1], "edit")) && argc >= 3) {
        if(note_kind(argv[2])<0){bob_puts("Not a text note.");return 1;}
        if (argc == 3) {
            bob_puts("Note text:");
            bob_gets(text, 512);
            length=0;while(text[length])length++;
        } else if(!join_arguments(text,&length,argc,argv,3)) {
            bob_puts("Note is too long (511 characters maximum).");return 1;
        }
        if (bob_file_write(argv[2], text, length)) { bob_puts("Cannot save note."); return 1; }
        bob_puts("Saved.");
        return 0;
    }
    if (same_text(argv[1], "add") && argc >= 3) {
        int kind=note_kind(argv[2]);
        if(kind<0){bob_puts("Not a text note.");return 1;}
        if(kind) {
            length=bob_file_read(argv[2],text,512);
            if(length<0){bob_puts("Cannot read note.");return 1;}
        } else {length=0;text[0]=0;}
        if(argc==3) {
            if(length&&text[length-1]!='\n') {
                if(length>=511){bob_puts("Note is too long (511 characters maximum).");return 1;}
                text[length++]='\n';
            }
            bob_puts("Add text:");bob_gets(text+length,512-length);
            while(text[length])length++;
        } else {
            if(length&&text[length-1]!='\n') {
                if(length>=511){bob_puts("Note is too long (511 characters maximum).");return 1;}
                text[length++]='\n';
            }
            for(i=3;i<argc;i++) {
                if(i>3) {
                    if(length>=511){bob_puts("Note is too long (511 characters maximum).");return 1;}
                    text[length++]=' ';
                }
                for(int j=0;argv[i][j];j++) {
                    if(length>=511){bob_puts("Note is too long (511 characters maximum).");return 1;}
                    text[length++]=argv[i][j];
                }
            }
            text[length]=0;
        }
        if(bob_file_write(argv[2],text,length)){bob_puts("Cannot save note.");return 1;}
        bob_puts("Saved.");return 0;
    }
    if (same_text(argv[1], "delete") && argc == 3) {
        if(note_kind(argv[2])!=1){bob_puts("Not a text note.");return 1;}
        if (bob_file_delete(argv[2])) { bob_puts("Note not found."); return 1; }
        bob_puts("Deleted.");
        return 0;
    }
    bob_puts("Usage: notes show NAME | put/edit NAME [TEXT] | add NAME [TEXT] | delete NAME");
    return 1;
}
