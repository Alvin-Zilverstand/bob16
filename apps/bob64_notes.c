#include "../bob64/app.h"
#include "../bob64/editor.h"
#include "../bob64/gfx.h"

static char note_buffer[BOB64_EDITOR_CAPACITY+1];
static u32 note_pixels[640u*420u];
static BOB64_EDITOR note_editor;
static BOB64_GFX note_graphics;
static char note_status[32]="CTRL S SAVE  ESC CLOSE";
static const char note_filename[]="notes.txt";

static usize text_length(const char *text) {
    usize length=0;
    if(!text)return 0;
    while(text[length])length++;
    return length;
}

static int same_text(const char *left,const char *right) {
    usize i=0;
    if(!left||!right)return 0;
    while(left[i]&&left[i]==right[i])i++;
    return left[i]==right[i];
}

static void write_text(const char *text) {
    (void)bob64_app_write(text,text_length(text));
}

static void note_copy(char *destination,const char *source,usize capacity) {
    usize i=0;
    if(!destination||!capacity)return;
    if(source)while(i+1<capacity&&source[i]) {
        destination[i]=source[i];i++;
    }
    destination[i]=0;
}

static s64 read_note(const char *name,usize name_length,usize *length) {
    s64 result=bob64_app_file_read_all(name,name_length,note_buffer,
                                       BOB64_EDITOR_CAPACITY);
    if(result<0)return result;
    if(bob64_editor_is_binary_text(note_buffer,(usize)result))return -84;
    *length=(usize)result;
    return result;
}

static int append_arguments(const BOB64_APP_STARTUP *startup,u64 first,
                            usize *length) {
    for(u64 i=first;i<startup->ArgumentCount;i++) {
        const char *argument=startup->Arguments[i];
        usize argument_length=text_length(argument);
        usize separator=i>first?1:0;
        if(!argument||argument_length>BOB64_EDITOR_CAPACITY-*length||
           separator>BOB64_EDITOR_CAPACITY-*length-argument_length)
            return -1;
        if(separator)note_buffer[(*length)++]=' ';
        for(usize j=0;j<argument_length;j++)
            note_buffer[(*length)++]=argument[j];
    }
    note_buffer[*length]=0;
    return 0;
}

static int save_note(const char *name,usize name_length,usize length) {
    s64 result=bob64_app_file_write_all(name,name_length,note_buffer,length);
    if(result!=(s64)length) {
        write_text("cannot save note\n");
        return -1;
    }
    write_text("saved\n");
    return 0;
}

static void note_gui_draw(void) {
    const u32 width=640,height=420;
    const u32 columns=96,visible_rows=29;
    u32 row=0,column=0,cursor_row,cursor_column;
    bob64_gfx_fill_rect(&note_graphics,0,0,width,height,0x0009111bu);
    bob64_gfx_fill_rect(&note_graphics,2,2,width-4,height-4,0x001a2a3cu);
    bob64_gfx_fill_rect(&note_graphics,0,0,width,26,0x00335d8cu);
    bob64_gfx_line(&note_graphics,0,0,width-1,0,0x00a4c8ffu);
    bob64_gfx_line(&note_graphics,0,height-1,width-1,height-1,0x00a4c8ffu);
    bob64_gfx_line(&note_graphics,0,0,0,height-1,0x00a4c8ffu);
    bob64_gfx_line(&note_graphics,width-1,0,width-1,height-1,0x00a4c8ffu);
    bob64_gfx_text(&note_graphics,12,8,"NOTES",0x00ffffffu,1);
    bob64_gfx_text(&note_graphics,14,36,note_filename,0x009eb3c7u,1);
    bob64_editor_visual_position(&note_editor,columns,&cursor_row,&cursor_column);
    if(cursor_row<note_editor.ScrollRow)note_editor.ScrollRow=cursor_row;
    else if(cursor_row>=note_editor.ScrollRow+visible_rows)
        note_editor.ScrollRow=cursor_row-visible_rows+1;
    for(u32 i=0;i<=note_editor.Length;i++) {
        if(column>=columns&&(i==note_editor.Length||note_editor.Text[i]!='\n')) {
            row++;column=0;
        }
        if(i==note_editor.Cursor) {
            if(row>=note_editor.ScrollRow&&row<note_editor.ScrollRow+visible_rows) {
                s32 x=14+(s32)(column*6),y=54+(s32)((row-note_editor.ScrollRow)*10);
                bob64_gfx_line(&note_graphics,x,y,x,y+7,0x00ffffffu);
            }
        }
        if(i==note_editor.Length)break;
        if(note_editor.Text[i]=='\n') {row++;column=0;continue;}
        if(row>=note_editor.ScrollRow&&row<note_editor.ScrollRow+visible_rows) {
            char character[2]={note_editor.Text[i],0};
            bob64_gfx_text(&note_graphics,14+(s32)(column*6),
                54+(s32)((row-note_editor.ScrollRow)*10),character,
                0x00e2ebf5u,1);
        }
        column++;
    }
    (void)cursor_column;
    bob64_gfx_line(&note_graphics,8,390,width-8,390,0x0044576au);
    bob64_gfx_text(&note_graphics,12,398,note_status,0x008fc8ffu,1);
}

static s64 note_gui_run(void) {
    const u32 width=640,height=420;
    u32 display_width,display_height;
    u64 handle;
    usize length=0;
    s64 result;
    if(bob64_app_get_display(&display_width,&display_height)||
       display_width<width||display_height<height) {
        write_text("display is too small for notes\n");
        return -1;
    }
    result=read_note(note_filename,sizeof(note_filename)-1,&length);
    if(result<0&&result!=-2) {
        write_text(result==-84?"notes.txt is not a text file\n":
                                  "cannot open notes.txt\n");
        return -1;
    }
    bob64_editor_init(&note_editor,result>=0?note_buffer:0,(u32)length);
    note_graphics.Pixels=note_pixels;
    note_graphics.Width=width;note_graphics.Height=height;
    note_graphics.Capacity=sizeof(note_pixels)/sizeof(note_pixels[0]);
    handle=bob64_app_window_create((s32)((display_width-width)/2),
        (s32)((display_height-height)/2),width,height);
    if(!handle) {
        write_text("cannot open notes window\n");
        return -1;
    }
    for(;;) {
        BOB64_EVENT event;
        note_gui_draw();
        if(bob64_app_window_present(handle,note_pixels,width,height)<0) {
            bob64_app_window_destroy(handle);
            return -2;
        }
        if(bob64_app_wait_event(&event)) {
            bob64_app_window_destroy(handle);
            return -3;
        }
        if(event.Type!=BOB64_EVENT_KEY_DOWN)continue;
        if(event.Key==0x01)break;
        if((event.Modifiers&BOB64_EVENT_MOD_CONTROL)&&
           (event.Character=='s'||event.Character=='S')) {
            usize saved_length=note_editor.Length;
            for(usize i=0;i<saved_length;i++)note_buffer[i]=note_editor.Text[i];
            if(bob64_app_file_write_all(note_filename,sizeof(note_filename)-1,
                                        note_buffer,saved_length)==(s64)saved_length)
                note_copy(note_status,"SAVED",sizeof(note_status));
            else note_copy(note_status,"SAVE FAILED",sizeof(note_status));
            continue;
        }
        bob64_editor_handle_key(&note_editor,&event);
        note_copy(note_status,"CTRL S SAVE  ESC CLOSE",sizeof(note_status));
    }
    if(bob64_app_window_destroy(handle))return -4;
    return 0;
}

s64 bob64_app_main(const BOB64_APP_STARTUP *startup) {
    const char *command,*name;
    usize name_length,length=0;
    s64 result;
    if(!startup||startup->StructSize!=sizeof(*startup)||
       startup->AbiVersion!=BOB64_APP_ABI_VERSION||
       (startup->ArgumentCount&&!startup->Arguments))return -1;
    if(!startup->ArgumentCount)return note_gui_run();
    if(startup->ArgumentCount<2||!startup->Arguments[1]) {
        write_text("usage: notes show NAME | copy SOURCE DEST | put NAME TEXT | add NAME TEXT | delete NAME\n");
        return 1;
    }
    command=startup->Arguments[1];
    if(startup->ArgumentCount<3||!startup->Arguments[2]) {
        write_text("usage: notes show NAME | copy SOURCE DEST | put NAME TEXT | add NAME TEXT | delete NAME\n");
        return 1;
    }
    name=startup->Arguments[2];
    name_length=text_length(name);
    if(!name_length||name_length>BOB64_SYSCALL_MAX_FILENAME) {
        write_text("invalid note name\n");
        return 1;
    }
    if(same_text(command,"show")&&startup->ArgumentCount==3) {
        result=read_note(name,name_length,&length);
        if(result<0) {
            write_text(result==-84?"not a text note\n":"note not found or too large\n");
            return 1;
        }
        for(usize offset=0;offset<length;) {
            usize chunk=length-offset;
            if(chunk>BOB64_SYSCALL_MAX_BUFFER)chunk=BOB64_SYSCALL_MAX_BUFFER;
            if(bob64_app_write(note_buffer+offset,chunk)!=(s64)chunk)return -2;
            offset+=chunk;
        }
        if(bob64_app_write_char('\n')<0)return -2;
        return 0;
    }
    if(same_text(command,"copy")&&startup->ArgumentCount==4&&
       startup->Arguments[3]) {
        const char *destination=startup->Arguments[3];
        usize destination_length=text_length(destination);
        if(!destination_length||destination_length>BOB64_SYSCALL_MAX_FILENAME||
           same_text(name,destination)) {
            write_text("invalid copy destination\n");
            return 1;
        }
        if(destination_length>=5&&destination[destination_length-5]=='.'&&
           destination[destination_length-4]=='b'&&
           destination[destination_length-3]=='6'&&
           destination[destination_length-2]=='4'&&
           destination[destination_length-1]=='e') {
            write_text("refusing to overwrite an app\n");
            return 1;
        }
        result=read_note(destination,destination_length,&length);
        if(result<0&&result!=-2) {
            write_text("destination is not a text note\n");
            return 1;
        }
        result=read_note(name,name_length,&length);
        if(result<0) {
            write_text(result==-84?"source is not a text note\n":
                                    "source note not found or too large\n");
            return 1;
        }
        result=bob64_app_file_write_all(destination,destination_length,
                                        note_buffer,length);
        if(result!=(s64)length) {
            write_text("cannot copy note\n");
            return 1;
        }
        write_text("Copied.\n");
        return 0;
    }
    if((same_text(command,"put")||same_text(command,"edit"))&&
       startup->ArgumentCount>=4) {
        result=read_note(name,name_length,&length);
        if(result<0&&result!=-2) {
            write_text(result==-84?"not a text note\n":"cannot read note\n");
            return 1;
        }
        length=0;
        if(append_arguments(startup,3,&length)) {
            write_text("note is too large\n");
            return 1;
        }
        return save_note(name,name_length,length);
    }
    if(same_text(command,"add")&&startup->ArgumentCount>=4) {
        result=read_note(name,name_length,&length);
        if(result<0&&result!=-2) {
            write_text(result==-84?"not a text note\n":"cannot read note\n");
            return 1;
        }
        if(length&&note_buffer[length-1]!='\n') {
            if(length>=BOB64_EDITOR_CAPACITY) {
                write_text("note is too large\n");
                return 1;
            }
            note_buffer[length++]='\n';
        }
        if(append_arguments(startup,3,&length)) {
            write_text("note is too large\n");
            return 1;
        }
        return save_note(name,name_length,length);
    }
    if(same_text(command,"delete")&&startup->ArgumentCount==3) {
        result=read_note(name,name_length,&length);
        if(result<0) {
            write_text(result==-84?"not a text note\n":"note not found\n");
            return 1;
        }
        if(bob64_app_delete_file(name,name_length)) {
            write_text("cannot delete note\n");
            return 1;
        }
        write_text("deleted\n");
        return 0;
    }
    write_text("usage: notes show NAME | copy SOURCE DEST | put NAME TEXT | add NAME TEXT | delete NAME\n");
    return 1;
}
