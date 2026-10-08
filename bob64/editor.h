#ifndef BOB64_EDITOR_H
#define BOB64_EDITOR_H

#include "abi.h"

#define BOB64_EDITOR_CAPACITY BOB64_SYSCALL_MAX_BUFFER

typedef struct {
    char Text[BOB64_EDITOR_CAPACITY+1];
    u32 Length;
    u32 Cursor;
    u32 ScrollRow;
    u8 Dirty;
} BOB64_EDITOR;

static inline void bob64_editor_init(BOB64_EDITOR *editor,const char *text,u32 length) {
    if(!editor)return;
    if(length>BOB64_EDITOR_CAPACITY)length=BOB64_EDITOR_CAPACITY;
    for(u32 i=0;i<length;i++)editor->Text[i]=text?text[i]:0;
    editor->Text[length]=0;editor->Length=length;editor->Cursor=length;
    editor->ScrollRow=0;editor->Dirty=0;
}

static inline int bob64_editor_insert(BOB64_EDITOR *editor,char character) {
    if(!editor||editor->Length>=BOB64_EDITOR_CAPACITY||
       !((character>=32&&character<=126)||character=='\n'))return -1;
    for(u32 i=editor->Length;i>editor->Cursor;i--)editor->Text[i]=editor->Text[i-1];
    editor->Text[editor->Cursor++]=character;
    editor->Length++;editor->Text[editor->Length]=0;editor->Dirty=1;
    return 0;
}

static inline int bob64_editor_backspace(BOB64_EDITOR *editor) {
    if(!editor||!editor->Cursor)return -1;
    for(u32 i=editor->Cursor-1;i<editor->Length-1;i++)editor->Text[i]=editor->Text[i+1];
    editor->Cursor--;editor->Length--;editor->Text[editor->Length]=0;editor->Dirty=1;
    return 0;
}

static inline int bob64_editor_delete(BOB64_EDITOR *editor) {
    if(!editor||editor->Cursor>=editor->Length)return -1;
    for(u32 i=editor->Cursor;i<editor->Length-1;i++)editor->Text[i]=editor->Text[i+1];
    editor->Length--;editor->Text[editor->Length]=0;editor->Dirty=1;
    return 0;
}

static inline void bob64_editor_move(BOB64_EDITOR *editor,s32 direction) {
    if(!editor)return;
    if(direction<0&&editor->Cursor)editor->Cursor--;
    else if(direction>0&&editor->Cursor<editor->Length)editor->Cursor++;
}

static inline void bob64_editor_move_vertical(BOB64_EDITOR *editor,s32 direction) {
    u32 line_start=0,column=0,target_start,target_end,target_column;
    if(!editor||!direction)return;
    for(u32 i=0;i<editor->Cursor;i++) {
        if(editor->Text[i]=='\n'){line_start=i+1;column=0;}else column++;
    }
    if(direction<0) {
        if(!line_start)return;
        target_start=0;
        for(u32 i=0;i<line_start-1;i++)if(editor->Text[i]=='\n')target_start=i+1;
        target_end=line_start-1;
    } else {
        target_start=editor->Cursor;
        while(target_start<editor->Length&&editor->Text[target_start]!='\n')target_start++;
        if(target_start==editor->Length)return;
        target_start++;
        target_end=target_start;
        while(target_end<editor->Length&&editor->Text[target_end]!='\n')target_end++;
    }
    target_column=column;
    if(target_column>target_end-target_start)target_column=target_end-target_start;
    editor->Cursor=target_start+target_column;
}

static inline void bob64_editor_visual_position(const BOB64_EDITOR *editor,
        u32 columns,u32 *row_out,u32 *column_out) {
    u32 row=0,column=0;
    if(!editor||!row_out||!column_out)return;
    if(!columns)columns=1;
    for(u32 i=0;i<editor->Cursor;i++) {
        if(column>=columns&&editor->Text[i]!='\n'){row++;column=0;}
        if(editor->Text[i]=='\n'){row++;column=0;}
        else column++;
    }
    if(column>=columns){row++;column=0;}
    *row_out=row;*column_out=column;
}

static inline void bob64_editor_handle_key(BOB64_EDITOR *editor,const BOB64_EVENT *event) {
    if(!editor||!event||event->Type!=BOB64_EVENT_KEY_DOWN)return;
    if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x4b))bob64_editor_move(editor,-1);
    else if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x4d))bob64_editor_move(editor,1);
    else if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x48))bob64_editor_move_vertical(editor,-1);
    else if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x50))bob64_editor_move_vertical(editor,1);
    else if(event->Key==(BOB64_EVENT_KEY_EXTENDED|0x53))bob64_editor_delete(editor);
    else if(event->Character=='\b')bob64_editor_backspace(editor);
    else if(event->Character=='\n')bob64_editor_insert(editor,'\n');
    else if(event->Character>=32&&event->Character<=126)
        bob64_editor_insert(editor,(char)event->Character);
}

#endif
