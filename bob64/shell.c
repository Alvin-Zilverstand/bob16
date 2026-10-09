#include "shell.h"

static void output(BOB64_SHELL *shell,const char *text) {
    shell->Write(shell->Context,text);
}

static void prompt(BOB64_SHELL *shell);

static char lower_ascii(char value) {
    if(value>='A'&&value<='Z')return (char)(value-'A'+'a');
    return value;
}

static int equals(const char *left,const char *right) {
    while(*left&&*right&&lower_ascii(*left)==*right){left++;right++;}
    return !*left&&!*right;
}

static void print_hex64(BOB64_SHELL *shell,u64 value) {
    static const char digits[]="0123456789abcdef";
    char text[17];
    for(u32 i=0;i<16;i++)text[i]=digits[(value>>(60-i*4))&15];
    text[16]=0;output(shell,text);
}

static void print_decimal(BOB64_SHELL *shell,usize value) {
    char text[21];usize length=0;
    do {text[length++]=(char)('0'+value%10);value/=10;} while(value&&length<sizeof(text));
    while(length) {char digit[2]={text[--length],0};output(shell,digit);}
}

static void print_memory(BOB64_SHELL *shell) {
    u64 free_pages=0,allocated_pages=0;
    BOB64_PAGE_ALLOCATOR *allocator=shell->PageAllocator;
    if(allocator) {
        for(usize i=0;i<allocator->Count;i++)free_pages+=allocator->Extents[i].Pages;
        for(usize i=0;i<allocator->AllocatedCount;i++)
            allocated_pages+=allocator->Allocated[i].Pages;
    }
    output(shell,"free pages=0x");print_hex64(shell,free_pages);output(shell,"\r\n");
    output(shell,"allocated pages=0x");print_hex64(shell,allocated_pages);output(shell,"\r\n");
    output(shell,"heap mapped=0x");
    print_hex64(shell,(u64)bob64_heap_mapped_bytes(shell->Heap));output(shell," bytes\r\n");
}

static void print_file_data(BOB64_SHELL *shell,const char *data,usize length) {
    static const char digits[]="0123456789abcdef";
    char character[2]={0,0},escaped[5]={'\\','x','0','0',0};
    for(usize i=0;i<length;i++) {
        u8 value=(u8)data[i];
        if((value>=32&&value<=126)||value=='\r'||value=='\n'||value=='\t') {
            character[0]=(char)value;output(shell,character);
        } else {
            escaped[2]=digits[value>>4];escaped[3]=digits[value&15];
            output(shell,escaped);
        }
    }
}

static void run_file_command(BOB64_SHELL *shell,char *arguments) {
    char *argv[BOB64_SHELL_ARGUMENT_LIMIT];
    usize argc=0;
    char *cursor=arguments;
    int started=0;
    if(!shell->Run) {output(shell,"app execution unavailable\r\n");return;}
    while(*cursor==' ')cursor++;
    if(!*cursor) {output(shell,"usage: run NAME [ARG ...]\r\n");return;}
    while(*cursor) {
        if(argc==BOB64_SHELL_ARGUMENT_LIMIT) {
            output(shell,"too many arguments\r\n");return;
        }
        argv[argc++]=cursor;
        while(*cursor&&*cursor!=' ')cursor++;
        if(*cursor)*cursor++=0;
        while(*cursor==' ')cursor++;
    }
    s64 status=shell->Run(shell->Context,argv[0],argc,
                          (const char *const *)argv,&started);
    if(!started) {output(shell,"unable to start application\r\n");return;}
    output(shell,"application exit status=0x");
    print_hex64(shell,(u64)status);output(shell,"\r\n");
}

static void compile_file_command(BOB64_SHELL *shell,char *arguments) {
    char source[BOB64_FS_NAME_CAPACITY],destination[BOB64_FS_NAME_CAPACITY];
    usize source_length=0,destination_length=0,error_offset=0;
    char *cursor=arguments;
    int result;
    while(*cursor==' ')cursor++;
    while(cursor[source_length]&&cursor[source_length]!=' '&&
          source_length<sizeof(source)-1)source_length++;
    if(!source_length||(cursor[source_length]&&cursor[source_length]!=' ')) {
        output(shell,"usage: cc SOURCE [OUTPUT]\r\n");return;
    }
    for(usize i=0;i<source_length;i++)source[i]=cursor[i];
    source[source_length]=0;cursor+=source_length;
    while(*cursor==' ')cursor++;
    if(!*cursor) {
        static const char default_output[]="app.b64e";
        destination_length=sizeof(default_output)-1;
        for(usize i=0;i<destination_length;i++)destination[i]=default_output[i];
    } else {
        while(cursor[destination_length]&&cursor[destination_length]!=' '&&
              destination_length<sizeof(destination)-1)destination_length++;
        if(!destination_length||(cursor[destination_length]&&cursor[destination_length]!=' ')) {
            output(shell,"usage: cc SOURCE [OUTPUT]\r\n");return;
        }
        for(usize i=0;i<destination_length;i++)destination[i]=cursor[i];
        cursor+=destination_length;while(*cursor==' ')cursor++;
        if(*cursor) {output(shell,"usage: cc SOURCE [OUTPUT]\r\n");return;}
    }
    destination[destination_length]=0;
    if(equals(source,destination)) {output(shell,"source and output must differ\r\n");return;}
    if(!shell->Compile) {output(shell,"C compiler unavailable\r\n");return;}
    result=shell->Compile(shell->Context,source,destination,&error_offset);
    if(result==-2) {
        output(shell,"C compile error near character ");print_decimal(shell,error_offset);
        output(shell,". Supports int main(void), helper calls, void/char returns, pointers, and bob64_app_write.\r\n");
    } else if(result)output(shell,"C compile failed; check source and free filesystem space\r\n");
    else {output(shell,"compiled to ");output(shell,destination);output(shell,"\r\n");}
}

static int list_file(void *context,const char *name,usize length) {
    BOB64_SHELL *shell=(BOB64_SHELL *)context;
    output(shell,name);output(shell,"  0x");print_hex64(shell,(u64)length);
    output(shell," bytes\r\n");
    return 0;
}

static usize text_length(const char *text) {
    usize length=0;
    while(text[length])length++;
    return length;
}

static void redraw_line(BOB64_SHELL *shell,usize previous_length) {
    for(usize i=0;i<previous_length;i++)output(shell,"\b");
    output(shell,shell->Line);
    for(usize i=shell->Length;i<previous_length;i++)output(shell," \b");
}

static usize copy_text(char *destination,usize capacity,const char *source) {
    usize length=0;
    if(!destination||!capacity)return 0;
    while(source&&length+1<capacity&&source[length]) {
        destination[length]=source[length];length++;
    }
    destination[length]=0;
    return length;
}

static void history_store(BOB64_SHELL *shell) {
    if(!shell->Length)return;
    if(shell->HistoryCount) {
        usize i=0;
        while(i<shell->Length&&shell->History[shell->HistoryCount-1][i]==
              shell->Line[i])i++;
        if(i==shell->Length&&shell->History[shell->HistoryCount-1][i]==0)return;
    }
    if(shell->HistoryCount==BOB64_SHELL_HISTORY_LIMIT) {
        for(usize i=1;i<shell->HistoryCount;i++)
            copy_text(shell->History[i-1],sizeof(shell->History[i-1]),
                      shell->History[i]);
        shell->HistoryCount--;
    }
    copy_text(shell->History[shell->HistoryCount],
              sizeof(shell->History[shell->HistoryCount]),shell->Line);
    shell->HistoryCount++;
}

static void history_load(BOB64_SHELL *shell,const char *line) {
    usize previous=shell->Length;
    shell->Length=copy_text(shell->Line,sizeof(shell->Line),line);
    redraw_line(shell,previous);
}

static void history_up(BOB64_SHELL *shell) {
    if(!shell->HistoryCount)return;
    if(!shell->HistoryBrowsing) {
        shell->HistoryDraftLength=copy_text(shell->HistoryDraft,
            sizeof(shell->HistoryDraft),shell->Line);
        shell->HistoryBrowsing=1;
        shell->HistoryIndex=shell->HistoryCount;
    }
    if(shell->HistoryIndex)shell->HistoryIndex--;
    history_load(shell,shell->History[shell->HistoryIndex]);
}

static void history_down(BOB64_SHELL *shell) {
    if(!shell->HistoryBrowsing)return;
    if(shell->HistoryIndex+1<shell->HistoryCount) {
        shell->HistoryIndex++;
        history_load(shell,shell->History[shell->HistoryIndex]);
    } else {
        shell->HistoryBrowsing=0;
        history_load(shell,shell->HistoryDraft);
    }
}

typedef struct {
    const char *Prefix;
    usize PrefixLength,Count;
    const char *Names[16];
} BOB64_SHELL_COMPLETIONS;

static int starts_with_case_insensitive(const char *text,const char *prefix,
                                        usize length) {
    for(usize i=0;i<length;i++)
        if(!text[i]||lower_ascii(text[i])!=lower_ascii(prefix[i]))return 0;
    return 1;
}

static void completion_add(BOB64_SHELL_COMPLETIONS *matches,const char *name) {
    if(!starts_with_case_insensitive(name,matches->Prefix,matches->PrefixLength))
        return;
    if(matches->Count<sizeof(matches->Names)/sizeof(matches->Names[0]))
        matches->Names[matches->Count]=name;
    matches->Count++;
}

static int completion_add_file(void *context,const char *name,usize length) {
    BOB64_SHELL_COMPLETIONS *matches=(BOB64_SHELL_COMPLETIONS *)context;
    (void)length;
    completion_add(matches,name);
    return matches->Count>sizeof(matches->Names)/sizeof(matches->Names[0]);
}

static void shell_complete(BOB64_SHELL *shell) {
    static const char *commands[]={"help","clear","mem","heap","version",
        "save","restore","ls","cat","write","rm","run","cc","echo"};
    BOB64_SHELL_COMPLETIONS matches={0};
    const char *prefix=shell->Line;
    usize prefix_length=shell->Length;
    int complete_file=0;
    usize command_length=0;
    while(command_length<shell->Length&&shell->Line[command_length]!=' ')
        command_length++;
    if(command_length<shell->Length) {
        usize start=command_length;
        while(start<shell->Length&&shell->Line[start]==' ')start++;
        int file_command=(command_length==3&&lower_ascii(shell->Line[0])=='c'&&
            lower_ascii(shell->Line[1])=='a'&&lower_ascii(shell->Line[2])=='t')||
          (command_length==2&&lower_ascii(shell->Line[0])=='r'&&
            lower_ascii(shell->Line[1])=='m')||
          (command_length==3&&lower_ascii(shell->Line[0])=='r'&&
            lower_ascii(shell->Line[1])=='u'&&lower_ascii(shell->Line[2])=='n')||
          (command_length==2&&lower_ascii(shell->Line[0])=='c'&&
            lower_ascii(shell->Line[1])=='c');
        if(!file_command)return;
        for(usize i=start;i<shell->Length;i++)if(shell->Line[i]==' ')return;
        prefix=shell->Line+start;prefix_length=shell->Length-start;
        complete_file=1;
    }
    matches.Prefix=prefix;matches.PrefixLength=prefix_length;
    if(complete_file)bob64_fs_list(shell->Filesystem,completion_add_file,&matches);
    else if(prefix==shell->Line) {
        for(usize i=0;i<sizeof(commands)/sizeof(commands[0]);i++)
            completion_add(&matches,commands[i]);
    }
    if(!matches.Count)return;
    if(matches.Count==1) {
        const char *name=matches.Names[0];
        char suffix[BOB64_SHELL_LINE_CAPACITY];
        usize suffix_length=0;
        while(name[prefix_length+suffix_length]&&
              suffix_length+1<sizeof(suffix)) {
            suffix[suffix_length]=name[prefix_length+suffix_length];
            suffix_length++;
        }
        if(suffix_length>BOB64_SHELL_LINE_CAPACITY-1-shell->Length)return;
        suffix[suffix_length]=0;
        for(usize i=0;i<suffix_length;i++)shell->Line[shell->Length++]=suffix[i];
        shell->Line[shell->Length]=0;shell->HistoryBrowsing=0;
        output(shell,suffix);
        return;
    }
    output(shell,"\r\n");
    usize shown=matches.Count<sizeof(matches.Names)/sizeof(matches.Names[0])?
                matches.Count:sizeof(matches.Names)/sizeof(matches.Names[0]);
    for(usize i=0;i<shown;i++) {
        if(i)output(shell,"  ");
        output(shell,matches.Names[i]);
    }
    output(shell,"\r\n");prompt(shell);output(shell,shell->Line);
}

static void execute_file_command(BOB64_SHELL *shell,u8 operation,char *arguments) {
    BOB64_FILESYSTEM *filesystem=shell->Filesystem;
    char *name=arguments,*separator=arguments,*contents_argument;
    const char *contents;
    usize length;
    if(!filesystem) { output(shell,"filesystem unavailable\r\n");return; }
    if(operation==0) {
        if(!bob64_fs_list(filesystem,list_file,shell))output(shell,"no files\r\n");
        return;
    }
    while(*name==' ')name++;
    separator=name;
    while(*separator&&*separator!=' ')separator++;
    contents_argument=separator;
    if(*separator) {
        *separator++=0;
        while(*separator==' ')separator++;
    }
    if(!*name||((operation==1||operation==3)&&*contents_argument)) {
        output(shell,"usage: cat NAME | write NAME TEXT | rm NAME\r\n");return;
    }
    if(operation==1) {
        if(bob64_fs_read(filesystem,name,&contents,&length)) {
            output(shell,"file not found\r\n");return;
        }
        print_file_data(shell,contents,length);output(shell,"\r\n");return;
    }
    if(operation==2) {
        if(bob64_fs_write(filesystem,name,separator,text_length(separator)))
            output(shell,"write failed (name/data invalid or heap full)\r\n");
        else output(shell,"saved\r\n");
        return;
    }
    if(bob64_fs_delete(filesystem,name))output(shell,"file not found\r\n");
    else output(shell,"deleted\r\n");
}

static void execute(BOB64_SHELL *shell) {
    char *line=shell->Line;
    if(!shell->Length)return;
    line[shell->Length]=0;
    if(equals(line,"help")) {
        output(shell,"help  show commands\r\nclear clear screen\r\nmem   physical memory and heap\r\n");
        output(shell,"heap  heap usage\r\nls    list files\r\ncat NAME  read a file\r\n");
        output(shell,"write NAME TEXT  save text\r\nrm NAME  delete a file\r\n");
        output(shell,"run NAME [ARG ...]  launch a B64E application (try bob.b64e)\r\n");
        output(shell,"run desktop.b64e [FILE]  edit a RAM text file in the GUI\r\n");
        output(shell,"cc SOURCE [OUTPUT]  compile supported C to B64E (try bob.c)\r\n");
        output(shell,"Up/Down command history; Tab completes commands and files\r\n");
        output(shell,"save  checkpoint files in RAM and firmware storage\r\n");
        output(shell,"restore  restore firmware or RAM checkpoint\r\n");
        output(shell,"echo  print text\r\nversion show kernel version\r\n");
    } else if(equals(line,"clear")) {
        if(shell->Clear)shell->Clear(shell->Context);
        output(shell,"bob64! kernel shell\r\n");
    } else if(equals(line,"mem"))print_memory(shell);
    else if(equals(line,"heap")) {
        output(shell,"heap mapped=0x");
        print_hex64(shell,(u64)bob64_heap_mapped_bytes(shell->Heap));
        output(shell," bytes\r\n");
    } else if(equals(line,"version"))output(shell,"bob64 kernel 0.1\r\n");
    else if(equals(line,"save")) {
        usize size,written;
        if(bob64_fs_snapshot_size(shell->Filesystem,&size)) {
            output(shell,"snapshot size failed\r\n");return;
        }
        void *snapshot=bob64_heap_alloc(shell->Heap,size);
        if(!snapshot) {
            output(shell,"snapshot allocation failed (heap full)\r\n");return;
        }
        if(bob64_fs_snapshot_write(shell->Filesystem,snapshot,size,&written)) {
            if(snapshot)bob64_heap_free(shell->Heap,snapshot);
            output(shell,"snapshot serialization failed\r\n");return;
        }
        if(shell->Snapshot)bob64_heap_free(shell->Heap,shell->Snapshot);
        shell->Snapshot=snapshot;shell->SnapshotSize=written;
        if(shell->SnapshotSave&&
           !shell->SnapshotSave(shell->SnapshotContext,snapshot,written))
            output(shell,"saved B64S v1 checkpoint to firmware storage and RAM\r\n");
        else output(shell,"saved B64S v1 in RAM; firmware save failed\r\n");
    } else if(equals(line,"restore")) {
        if(shell->SnapshotRestore) {
            int persistent_result=shell->SnapshotRestore(shell->SnapshotContext,
                                                          shell->Filesystem);
            if(!persistent_result) {
                output(shell,"restored B64S v1 from firmware storage\r\n");return;
            }
            if(persistent_result<0) {
                output(shell,"firmware snapshot invalid; files unchanged\r\n");return;
            }
        }
        if(!shell->Snapshot||bob64_fs_snapshot_restore(shell->Filesystem,
           shell->Snapshot,shell->SnapshotSize)) {
            output(shell,"restore failed; files unchanged\r\n");return;
        }
        output(shell,"restored B64S v1 checkpoint\r\n");
    }
    else if(equals(line,"ls"))execute_file_command(shell,0,line+2);
    else if(shell->Length>4&&lower_ascii(line[0])=='c'&&
            lower_ascii(line[1])=='a'&&lower_ascii(line[2])=='t'&&line[3]==' ')
        execute_file_command(shell,1,line+4);
    else if(shell->Length>6&&lower_ascii(line[0])=='w'&&
            lower_ascii(line[1])=='r'&&lower_ascii(line[2])=='i'&&
            lower_ascii(line[3])=='t'&&lower_ascii(line[4])=='e'&&line[5]==' ')
        execute_file_command(shell,2,line+6);
    else if(shell->Length>3&&lower_ascii(line[0])=='r'&&
            lower_ascii(line[1])=='m'&&line[2]==' ')
        execute_file_command(shell,3,line+3);
    else if(shell->Length>4&&lower_ascii(line[0])=='r'&&
            lower_ascii(line[1])=='u'&&lower_ascii(line[2])=='n'&&line[3]==' ')
        run_file_command(shell,line+4);
    else if(shell->Length>3&&lower_ascii(line[0])=='c'&&
            lower_ascii(line[1])=='c'&&line[2]==' ')
        compile_file_command(shell,line+3);
    else if(shell->Length>=5&&lower_ascii(line[0])=='e'&&
            lower_ascii(line[1])=='c'&&lower_ascii(line[2])=='h'&&
            lower_ascii(line[3])=='o'&&line[4]==' ') {
        output(shell,line+5);output(shell,"\r\n");
    } else {
        output(shell,"unknown command: ");output(shell,line);output(shell,"\r\n");
    }
}

static void prompt(BOB64_SHELL *shell) {
    output(shell,"bob64> ");
}

int bob64_shell_init(BOB64_SHELL *shell,BOB64_PAGE_ALLOCATOR *allocator,
                     BOB64_HEAP *heap,BOB64_FILESYSTEM *filesystem,
                     BOB64_SHELL_WRITE write,
                     BOB64_SHELL_CLEAR clear,void *context) {
    if(!shell||!write)return -1;
    shell->Length=0;shell->IgnoreLineFeed=0;shell->Line[0]=0;
    shell->HistoryCount=0;shell->HistoryIndex=0;shell->HistoryDraftLength=0;
    shell->HistoryBrowsing=0;shell->HistoryDraft[0]=0;
    for(usize i=0;i<BOB64_SHELL_HISTORY_LIMIT;i++)shell->History[i][0]=0;
    shell->PageAllocator=allocator;shell->Heap=heap;shell->Filesystem=filesystem;
    shell->Snapshot=0;shell->SnapshotSize=0;
    shell->Write=write;shell->Clear=clear;shell->Context=context;
    shell->Run=0;
    shell->Compile=0;
    output(shell,"bob64! kernel shell; type help\r\n");prompt(shell);
    return 0;
}

void bob64_shell_set_runner(BOB64_SHELL *shell,BOB64_SHELL_RUN run) {
    if(shell)shell->Run=run;
}

void bob64_shell_set_compiler(BOB64_SHELL *shell,BOB64_SHELL_COMPILE compile) {
    if(shell)shell->Compile=compile;
}

void bob64_shell_set_snapshot_storage(BOB64_SHELL *shell,
        BOB64_SHELL_SNAPSHOT_SAVE save,BOB64_SHELL_SNAPSHOT_RESTORE restore,
        void *context) {
    if(!shell)return;
    shell->SnapshotSave=save;shell->SnapshotRestore=restore;
    shell->SnapshotContext=context;
}

void bob64_shell_input(BOB64_SHELL *shell,int character) {
    char echo[2];
    if(!shell||!shell->Write||character<0)return;
    if(character==BOB64_SHELL_INPUT_UP) {history_up(shell);return;}
    if(character==BOB64_SHELL_INPUT_DOWN) {history_down(shell);return;}
    if(character=='\t') {shell_complete(shell);return;}
    if(character=='\n'&&shell->IgnoreLineFeed) { shell->IgnoreLineFeed=0;return; }
    shell->IgnoreLineFeed=(u8)(character=='\r');
    if(character=='\r'||character=='\n') {
        history_store(shell);shell->HistoryBrowsing=0;
        output(shell,"\r\n");execute(shell);shell->Length=0;shell->Line[0]=0;
        prompt(shell);return;
    }
    if(character=='\b'||character==0x7f) {
        shell->HistoryBrowsing=0;
        if(shell->Length) {
            shell->Length--;shell->Line[shell->Length]=0;
            output(shell,"\b \b");
        }
        return;
    }
    if(character<32||character>126||shell->Length>=BOB64_SHELL_LINE_CAPACITY-1)return;
    shell->HistoryBrowsing=0;
    shell->Line[shell->Length++]=(char)character;
    shell->Line[shell->Length]=0;
    echo[0]=(char)character;echo[1]=0;output(shell,echo);
}
