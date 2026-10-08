#include "bob.h"
#include "bob_gfx.h"
#include "bob_font.h"
#include "bob_event.h"
#include "bob_gui.h"
#include "bob_fs.h"
#include "bob_process.h"
#include "bob_string.h"

static void gui_select_file(int row,const int *slots,int count,int *selected,
                            char *status,char *preview,int *preview_length,int *preview_scroll) {
    int size,kind;char name[24],size_text[12];
    if(row<0||row>=count)return;
    *selected=row;
    kind=bob_file_list(slots[row],name,24,&size);
    bob_format_int(size,size_text,12);
    bob_strcpy(status,name);bob_strcat(status," ");bob_strcat(status,size_text);
    preview[0]=0;*preview_length=-1;*preview_scroll=0;
    if(kind==0)*preview_length=bob_file_read(name,preview,512);
}

static int gui_preview_offset(const char *preview,int length,int lines) {
    int position=0,column;
    while(lines>0&&position<length) {
        column=0;
        while(column<34&&position<length&&preview[position]!='\n'){position++;column++;}
        if(position<length&&preview[position]=='\n')position++;
        lines--;
    }
    return position;
}

static int gui_delete_selected(int row,const int *slots,int count,char *name) {
    int size,kind;
    if(row<0||row>=count)return -1;
    kind=bob_file_list(slots[row],name,24,&size);
    if(kind!=0)return -1;
    return bob_file_delete(name);
}

static void gui_wm_sync_rects(int *rects,const int *managed) {
    for(int id=0;id<2;id++)for(int i=0;i<4;i++)
        rects[id*4+i]=managed[id*BOB_WM_WINDOW_WORDS+i];
}

static void gui_place_editor_widgets(int *windows,int *name_field,int *text_area,int *button) {
    int *rect=windows;
    name_field[0]=rect[0]+2;name_field[1]=rect[1]+5;
    text_area[0]=rect[0]+2;text_area[1]=rect[1]+9;
    button[0]=rect[0]+3;button[1]=rect[1]+16;
}

static int gui_open_selected_text(int row,const int *slots,int count,char *name,
                                  char *filename,char *text,int *text_state,const int *text_area,
                                  int *wm_state,int *wm_windows,int *wm_order,char *status) {
    int size,kind,length;
    if(row<0||row>=count){bob_strcpy(status,"Select a text file.");return 0;}
    kind=bob_file_list(slots[row],name,24,&size);
    if(kind!=BOB_FILE_KIND_TEXT){bob_strcpy(status,"Text files only.");return 0;}
    length=bob_file_read(name,text,512);
    if(length<0){bob_strcpy(status,"Could not read file.");return 0;}
    bob_strcpy(filename,name);
    bob_gui_text_area_init(text,text_state,512);
    bob_gui_text_area_reveal(text,text_state,text_area);
    text_state[0]=1;bob_wm_focus(wm_state,wm_windows,wm_order,0);
    bob_strcpy(status,"Editing selected file.");
    return 1;
}

static void gui_launch_app(int row,const char *names,int count,char *message) {
    char number[12];
    int child_status=-1,status;
    if(row<0||row>=count){bob_strcpy(message,"No app selected.");return;}
    status=bob_app_run(names+row*24,"",&child_status);
    if(status==-1)bob_strcpy(message,"App not found.");
    else if(status==-2)bob_strcpy(message,"Not a native app.");
    else if(status==-4)bob_strcpy(message,"Invalid app image.");
    else if(status==-5)bob_strcpy(message,"Not enough memory.");
    else if(status<0)bob_strcpy(message,"App launch failed.");
    else {
        bob_strcpy(message,"App exit ");bob_format_int(child_status,number,12);
        bob_strcat(message,number);
    }
}

int main(void) {
    int event[4];
    int mouse_x=-1;
    int mouse_y=-1;
    int wm_state[10];
    int wm_windows[14];
    int wm_order[2];
    int routed_event[4];
    int target_window=-1;
    int redraw_full=0;
    int overlay_was_open=0;
    int draw_id=-1;
    int windows[8];
    int button[4]={6,19,31,3};
    int button_state[2]={0,0};
    int delete_yes[4]={43,17,13,3};
    int delete_no[4]={59,17,13,3};
    int delete_yes_state[2]={0,0};
    int delete_no_state[2]={0,0};
    int name_field[4]={5,8,33,3};
    int text_area[4]={5,12,33,7};
    int name_state[3];
    int text_state[4];
    int length_b=0;
    int file_slots[8];
    char app_names[192];
    int app_count=0;
    int app_selected=0;
    int launcher_open=0;
    int help_open=0;
    int help_close[4]={32,18,16,3};
    int help_close_state[2]={0,0};
    int launch_button[4]={42,18,16,3};
    int close_button[4]={22,18,14,3};
    int launch_button_state[2]={0,0};
    int close_button_state[2]={0,0};
    int file_count=0;
    int selected_file=-1;
    int selection_initialized=0;
    int file_scroll=0;
    int delete_confirm=0;
    int menu_open=0;
    int menu_rect[4]={43,7,30,5};
    int menu_state[2]={0,-1};
    int file_size;
    int file_kind;
    int preview_length=-1;
    int preview_scroll=0;
    int preview_row,preview_column,preview_position;
    char preview[512];
    char preview_line[35];
    char file_name[24];
    char file_name_text[24]="new.txt";
    char file_text[512]="bob!";
    char message_b[32]="";
    char menu_items[72];
    int running=1;
    bob_strcpy(message_b,"N/P select; E edit; X delete");
    bob_strcpy(menu_items,"Open selected text");
    bob_strcpy(menu_items+24,"Delete selected");
    bob_strcpy(menu_items+48,"Close menu");
    bob_gui_text_field_init(file_name_text,name_state,24);
    bob_gui_text_area_init(file_text,text_state,512);
    name_state[0]=1;
    if(bob_wm_init(wm_state,wm_windows,wm_order,2,80,25))return 1;
    if(bob_wm_create(wm_state,wm_windows,wm_order,3,3,37,20)!=0||
       bob_wm_create(wm_state,wm_windows,wm_order,41,3,37,20)!=1)return 1;
    bob_wm_focus(wm_state,wm_windows,wm_order,0);
    if(bob_gfx_enter())return 1;
    while(running) {
        gui_wm_sync_rects(windows,wm_windows);
        gui_place_editor_widgets(windows,name_field,text_area,button);
        bob_gui_menu_place(menu_rect,windows[4]+2,windows[5]+4,30,3,80,25);
        file_count=0;
        app_count=0;
        for(int i=0;i<8;i++) {
            file_kind=bob_file_list(i,file_name,24,&file_size);
            if(file_kind>=0)file_slots[file_count++]=i;
            if(file_kind==BOB_FILE_KIND_NATIVE_APP) {
                bob_strcpy(app_names+app_count*24,file_name);app_count++;
            }
        }
        if(app_selected>=app_count)app_selected=app_count?app_count-1:0;
        if(!selection_initialized&&file_count>0) {
            gui_select_file(0,file_slots,file_count,&selected_file,message_b,preview,&preview_length,&preview_scroll);
            selection_initialized=1;
        }
        if(selected_file>=file_count) {
            selected_file=-1;preview_length=-1;preview_scroll=0;delete_confirm=0;
            bob_strcpy(message_b,"Selection changed.");
        }
        if(file_scroll>file_count-6)file_scroll=file_count>6?file_count-6:0;
        if(selected_file>=0) {
            if(selected_file<file_scroll)file_scroll=selected_file;
            if(selected_file>=file_scroll+6)file_scroll=selected_file-5;
        }
        if(menu_open||launcher_open||help_open||overlay_was_open)wm_state[8]=1;
        redraw_full=wm_state[8];
        bob_wm_begin_draw(wm_state,wm_windows,wm_order);
        if(redraw_full) {
            bob_gfx_clear('.',1);
            bob_gfx_text(2,0,"BOB32 DESKTOP  ?: HELP  TAB: FOCUS  CTRL+S: SAVE  ESC: EXIT",15);
        }
        while((draw_id=bob_wm_next_dirty(wm_state,wm_windows,wm_order))>=0) {
            int id=draw_id;int *rect=windows+id*4;
            if(id==0)bob_gui_window(rect[0],rect[1],rect[2],rect[3],"Text editor",
                       "E opens text. Tab changes focus.","Ctrl+S saves; Enter starts a new line.",wm_state[2]==0);
            else {
                bob_gui_window(rect[0],rect[1],rect[2],rect[3],"Window B","File menu: M or click File",message_b,wm_state[2]==1);
                for(int row=0;row<6&&file_scroll+row<file_count;row++) {
                    int slot=file_slots[file_scroll+row];
                    bob_file_list(slot,file_name,24,&file_size);
                    bob_gfx_text(rect[0]+2,rect[1]+4+row,
                                 file_scroll+row==selected_file?"> ":"  ",wm_state[2]==1?15:8);
                    bob_gfx_text(rect[0]+4,rect[1]+4+row,file_name,wm_state[2]==1?15:7);
                }
                if(delete_confirm) {
                    bob_gfx_text(rect[0]+2,rect[1]+11,"Delete selected text file?",wm_state[2]==1?14:8);
                    delete_yes[0]=rect[0]+2;delete_yes[1]=rect[1]+14;
                    delete_no[0]=rect[0]+19;delete_no[1]=rect[1]+14;
                    bob_gui_button_draw(delete_yes[0],delete_yes[1],delete_yes[2],delete_yes[3],"Yes, delete",delete_yes_state[0],delete_yes_state[1]);
                    bob_gui_button_draw(delete_no[0],delete_no[1],delete_no[2],delete_no[3],"Cancel",delete_no_state[0],delete_no_state[1]);
                } else {
                    bob_gfx_text(rect[0]+2,rect[1]+11,"Preview:",wm_state[2]==1?14:8);
                    if(preview_length<0)bob_gfx_text(rect[0]+2,rect[1]+12,"No text preview.",wm_state[2]==1?7:8);
                    preview_position=gui_preview_offset(preview,preview_length,preview_scroll);
                    for(preview_row=0;preview_row<6&&preview_position<preview_length;preview_row++) {
                        preview_column=0;
                        while(preview_column<34&&preview_position<preview_length&&preview[preview_position]!='\n')
                            preview_line[preview_column++]=preview[preview_position++];
                        if(preview_position<preview_length&&preview[preview_position]=='\n')preview_position++;
                        preview_line[preview_column]=0;
                        bob_gfx_text(rect[0]+2,rect[1]+12+preview_row,preview_line,wm_state[2]==1?15:7);
                    }
                }
            }
            if(id==0) {
                gui_place_editor_widgets(windows,name_field,text_area,button);
                bob_gfx_text(rect[0]+2,rect[1]+4,"File name",wm_state[2]==0?14:8);
                bob_gui_text_field_draw(name_field,file_name_text,name_state);
                bob_gfx_text(rect[0]+2,rect[1]+8,"Text",wm_state[2]==0?14:8);
                bob_gui_text_area_draw(text_area,file_text,text_state);
                bob_gui_button_draw(button[0],button[1],button[2],button[3],"Save file",button_state[0],button_state[1]);
            }
        }
        if(menu_open)bob_gui_menu_draw(menu_rect,menu_items,3,24,menu_state[0]);
        if(launcher_open||help_open)bob_gfx_fill_rect(0,1,80,24,'.',1);
        if(launcher_open) {
            bob_gui_window(18,3,44,19,"Application launcher",
                           "N/P or arrows select; R runs; Esc closes.","",1);
            for(int row=0;row<app_count&&row<8;row++) {
                int icon='A'+((row%14+1)<<8);
                if(row==app_selected)bob_gfx_fill_rect(21,7+row,38,1,' ',1);
                bob_gfx_blit_color(22,7+row,1,1,&icon);
                bob_gfx_text(25,7+row,app_names+row*24,row==app_selected?15:7);
            }
            bob_gui_button_draw(close_button[0],close_button[1],close_button[2],close_button[3],"Close",close_button_state[0],close_button_state[1]);
            bob_gui_button_draw(launch_button[0],launch_button[1],launch_button[2],launch_button[3],"Launch",launch_button_state[0],launch_button_state[1]);
        }
        if(help_open) {
            bob_gui_window(18,3,44,19,"Desktop help","","",1);
            bob_gfx_bitmap_text(28,6,"HELP",14);
            bob_gfx_text(23,14,"Tab moves through fields and files.",15);
            bob_gfx_text(23,15,"E edits a selected text file; X deletes.",15);
            bob_gfx_text(23,16,"Ctrl+S saves; Esc closes this help.",15);
            bob_gfx_text(23,17,"Wheel scrolls text; list wheel selects.",15);
            bob_gui_button_draw(help_close[0],help_close[1],help_close[2],help_close[3],"Close",help_close_state[0],help_close_state[1]);
        }
        overlay_was_open=menu_open||launcher_open||help_open;
        bob_gfx_present();
        if(bob_event_wait(event)!=1)break;
        if(menu_open) {
            int action=bob_gui_menu_event(menu_rect,3,menu_state,event);
            if(action==-2)menu_open=0;
            else if(event[0]==BOB_EVENT_MOUSE_BUTTON_DOWN&&
                    !bob_gui_window_hit(event[2],event[3],menu_rect))menu_open=0;
            else if(action==0) {
                gui_open_selected_text(selected_file,file_slots,file_count,file_name,
                                       file_name_text,file_text,text_state,text_area,wm_state,wm_windows,wm_order,message_b);
                menu_open=0;
            } else if(action==1) {
                int size,kind;
                if(selected_file>=0&&selected_file<file_count) {
                    kind=bob_file_list(file_slots[selected_file],file_name,24,&size);
                    if(kind==BOB_FILE_KIND_TEXT){delete_confirm=1;bob_strcpy(message_b,"Delete? Y/N");}
                    else bob_strcpy(message_b,"Text files only.");
                } else bob_strcpy(message_b,"No file selected.");
                menu_open=0;
            } else if(action==2)menu_open=0;
            continue;
        }
        if(help_open) {
            if(event[0]==BOB_EVENT_MOUSE_MOVE)bob_gui_button_event(help_close,help_close_state,event);
            else if(event[0]==BOB_EVENT_MOUSE_BUTTON_DOWN)bob_gui_button_event(help_close,help_close_state,event);
            else if(event[0]==BOB_EVENT_MOUSE_BUTTON_UP) {
                if(bob_gui_button_event(help_close,help_close_state,event))help_open=0;
            } else if(event[0]==BOB_EVENT_CHAR||event[0]==BOB_EVENT_KEY_DOWN)help_open=0;
            continue;
        }
        if(launcher_open) {
            if(event[0]==BOB_EVENT_MOUSE_MOVE) {
                bob_gui_button_event(close_button,close_button_state,event);
                bob_gui_button_event(launch_button,launch_button_state,event);
            } else if(event[0]==BOB_EVENT_MOUSE_BUTTON_DOWN) {
                int row=event[3]-7;
                if(event[2]>=21&&event[2]<59&&row>=0&&row<app_count&&row<8)app_selected=row;
                bob_gui_button_event(close_button,close_button_state,event);
                bob_gui_button_event(launch_button,launch_button_state,event);
            } else if(event[0]==BOB_EVENT_MOUSE_BUTTON_UP) {
                int close=bob_gui_button_event(close_button,close_button_state,event);
                int launch=bob_gui_button_event(launch_button,launch_button_state,event);
                if(close)launcher_open=0;
                else if(launch){gui_launch_app(app_selected,app_names,app_count,message_b);launcher_open=0;}
            } else if(event[0]==BOB_EVENT_KEY_DOWN) {
                if(event[1]==256&&app_selected>0)app_selected--;
                else if(event[1]==257&&app_selected+1<app_count&&app_selected<7)app_selected++;
            } else if(event[0]==BOB_EVENT_CHAR) {
                if(event[1]==27)launcher_open=0;
                else if((event[1]=='n'||event[1]=='j')&&app_selected+1<app_count&&app_selected<7)app_selected++;
                else if((event[1]=='p'||event[1]=='k')&&app_selected>0)app_selected--;
                else if(event[1]=='r'){gui_launch_app(app_selected,app_names,app_count,message_b);launcher_open=0;}
            }
            continue;
        }
        if(event[0]==BOB_EVENT_CHAR&&event[1]==27){running=0;break;}
        target_window=bob_wm_dispatch(wm_state,wm_windows,wm_order,event,routed_event);
        if(target_window<0)continue;
        for(int i=0;i<4;i++)event[i]=routed_event[i];
        gui_wm_sync_rects(windows,wm_windows);
        gui_place_editor_widgets(windows,name_field,text_area,button);
        bob_wm_invalidate(wm_state,wm_windows,wm_order,target_window);
        if(event[0]==BOB_EVENT_MOUSE_MOVE||event[0]==BOB_EVENT_MOUSE_BUTTON_DOWN||
           event[0]==BOB_EVENT_MOUSE_BUTTON_UP||event[0]==BOB_EVENT_MOUSE_WHEEL) {
            event[2]+=windows[target_window*4];event[3]+=windows[target_window*4+1];
        }
        if(event[0]==BOB_EVENT_MOUSE_MOVE) {
            mouse_x=event[2];mouse_y=event[3];
            if(wm_state[2]==0)bob_gui_button_event(button,button_state,event);
            else button_state[0]=0;
            if(wm_state[2]==1&&delete_confirm) {
                bob_gui_button_event(delete_yes,delete_yes_state,event);
                bob_gui_button_event(delete_no,delete_no_state,event);
            } else {delete_yes_state[0]=0;delete_no_state[0]=0;}
            continue;
        }
        if(event[0]==BOB_EVENT_MOUSE_WHEEL) {
            mouse_x=event[2];mouse_y=event[3];
            if(wm_state[2]==0&&bob_gui_window_hit(mouse_x,mouse_y,text_area)) {
                name_state[0]=0;bob_wm_focus(wm_state,wm_windows,wm_order,0);
                bob_gui_text_area_event(text_area,file_text,text_state,512,event);
            } else if(wm_state[2]==1&&bob_gui_window_hit(mouse_x,mouse_y,windows+4)) {
                if(mouse_y>=windows[5]+4&&mouse_y<windows[5]+10) {
                    int row=event[1]>0?selected_file-1:selected_file+1;
                    if(row>=0&&row<file_count)
                        gui_select_file(row,file_slots,file_count,&selected_file,message_b,preview,&preview_length,&preview_scroll);
                } else if(!delete_confirm&&mouse_y>=windows[5]+11&&mouse_y<windows[5]+18) {
                    if(event[1]>0&&preview_scroll>0)preview_scroll--;
                    else if(event[1]<0&&gui_preview_offset(preview,preview_length,preview_scroll+1)<preview_length)preview_scroll++;
                }
            }
            continue;
        }
        if(event[0]==BOB_EVENT_MOUSE_BUTTON_DOWN) {
            int hit=target_window;
            mouse_x=event[2];mouse_y=event[3];
            if(hit==1&&mouse_y==windows[5]+3&&mouse_x>=windows[4]+2&&mouse_x<windows[4]+6) {
                menu_open=1;menu_state[0]=0;menu_state[1]=-1;continue;
            }
            if(hit==1&&mouse_y>=windows[5]+4&&mouse_y<windows[5]+10) {
                int row=file_scroll+mouse_y-(windows[5]+4);
                if(row<file_count) {
                    delete_confirm=0;
                    gui_select_file(row,file_slots,file_count,&selected_file,message_b,preview,&preview_length,&preview_scroll);
                }
            }
            if(wm_state[2]==1&&delete_confirm) {
                bob_gui_button_event(delete_yes,delete_yes_state,event);
                bob_gui_button_event(delete_no,delete_no_state,event);
            }
            if(wm_state[2]==0) {
                bob_gui_button_event(button,button_state,event);
                bob_gui_text_field_event(name_field,file_name_text,name_state,24,event);
                bob_gui_text_area_event(text_area,file_text,text_state,512,event);
            } else {button_state[0]=0;button_state[1]=0;name_state[0]=0;text_state[0]=0;}
            continue;
        }
        if(event[0]==BOB_EVENT_MOUSE_BUTTON_UP) {
            if(delete_confirm&&wm_state[2]==1&&bob_gui_button_event(delete_yes,delete_yes_state,event)) {
                if(gui_delete_selected(selected_file,file_slots,file_count,file_name)==0) {
                    selected_file=-1;selection_initialized=1;preview_length=-1;preview_scroll=0;
                    bob_strcpy(message_b,"Deleted.");
                } else bob_strcpy(message_b,"Delete failed.");
                delete_confirm=0;
            } else if(delete_confirm&&wm_state[2]==1&&bob_gui_button_event(delete_no,delete_no_state,event)) {
                delete_confirm=0;
                if(selected_file>=0)gui_select_file(selected_file,file_slots,file_count,&selected_file,message_b,preview,&preview_length,&preview_scroll);
            }
            if(wm_state[2]==0&&bob_gui_button_event(button,button_state,event)) {
                if(bob_file_write(file_name_text,file_text,bob_strlen(file_text))==0) {
                    bob_strcpy(message_b,"Saved file.");selected_file=-1;selection_initialized=0;
                } else bob_strcpy(message_b,"Save failed; check name.");
            }
            continue;
        }
        if(event[0]==BOB_EVENT_KEY_DOWN) {
            if(wm_state[2]==0) {
                if(name_state[0])bob_gui_text_field_event(name_field,file_name_text,name_state,24,event);
                else if(text_state[0])bob_gui_text_area_event(text_area,file_text,text_state,512,event);
            }
            else if(event[1]==256&&preview_scroll>0)preview_scroll--;
            else if(event[1]==257&&gui_preview_offset(preview,preview_length,preview_scroll+1)<preview_length)preview_scroll++;
            continue;
        }
        if(event[0]!=BOB_EVENT_CHAR)continue;
        if(event[1]==27)running=0;
        else if(event[1]=='\t') {
            if(wm_state[2]==0&&name_state[0]){name_state[0]=0;text_state[0]=1;text_state[1]=bob_strlen(file_text);}
            else if(wm_state[2]==0&&text_state[0]){text_state[0]=0;bob_wm_focus(wm_state,wm_windows,wm_order,1);}
            else {bob_wm_focus(wm_state,wm_windows,wm_order,0);name_state[0]=1;name_state[1]=bob_strlen(file_name_text);text_state[0]=0;}
        }
        else if(event[1]=='?') {
            help_open=1;help_close_state[0]=0;help_close_state[1]=0;
        }
        else if(wm_state[2]==1&&event[1]=='a') {
            launcher_open=1;app_selected=0;launch_button_state[0]=0;launch_button_state[1]=0;
            close_button_state[0]=0;close_button_state[1]=0;
            bob_strcpy(message_b,app_count?"Choose an app.":"No stored apps.");
        }
        else if(wm_state[2]==1&&event[1]=='m') {menu_open=1;menu_state[0]=0;menu_state[1]=-1;}
        else if(event[1]==19) {
            if(bob_file_write(file_name_text,file_text,bob_strlen(file_text))==0) {
                bob_strcpy(message_b,"Saved file.");selected_file=-1;selection_initialized=0;
            } else bob_strcpy(message_b,"Save failed; check name.");
        }
        else if(wm_state[2]==1&&event[1]=='e') {
            gui_open_selected_text(selected_file,file_slots,file_count,file_name,
                                   file_name_text,file_text,text_state,text_area,wm_state,wm_windows,wm_order,message_b);
        }
        else if(wm_state[2]==1&&delete_confirm&&event[1]=='y') {
            if(gui_delete_selected(selected_file,file_slots,file_count,file_name)==0) {
                selected_file=-1;selection_initialized=1;preview_length=-1;preview_scroll=0;
                bob_strcpy(message_b,"Deleted.");
            } else bob_strcpy(message_b,"Delete failed.");
            delete_confirm=0;
        } else if(wm_state[2]==1&&delete_confirm&&event[1]=='n') {
            delete_confirm=0;
            if(selected_file>=0)gui_select_file(selected_file,file_slots,file_count,&selected_file,message_b,preview,&preview_length,&preview_scroll);
            bob_strcpy(message_b,"Delete cancelled.");
        } else if(wm_state[2]==1&&delete_confirm) {
        }
        else if(wm_state[2]==1&&event[1]=='n'&&selected_file+1<file_count) {
            gui_select_file(selected_file+1,file_slots,file_count,&selected_file,message_b,preview,&preview_length,&preview_scroll);
        } else if(wm_state[2]==1&&event[1]=='p'&&selected_file>0) {
            gui_select_file(selected_file-1,file_slots,file_count,&selected_file,message_b,preview,&preview_length,&preview_scroll);
        } else if(wm_state[2]==1&&event[1]=='x'&&selected_file>=0) {
            int selected_size,selected_kind;
            selected_kind=bob_file_list(file_slots[selected_file],file_name,24,&selected_size);
            if(selected_kind==0){delete_confirm=1;delete_yes_state[0]=0;delete_yes_state[1]=0;delete_no_state[0]=0;delete_no_state[1]=0;bob_strcpy(message_b,"Delete? Y/N");}
            else bob_strcpy(message_b,"Text files only.");
        } else if(wm_state[2]==1&&event[1]=='u'&&preview_scroll>0)preview_scroll--;
        else if(wm_state[2]==1&&event[1]=='d'&&gui_preview_offset(preview,preview_length,preview_scroll+1)<preview_length)preview_scroll++;
        else if(wm_state[2]==0) {
            if(name_state[0])bob_gui_text_field_event(name_field,file_name_text,name_state,24,event);
            else bob_gui_text_area_event(text_area,file_text,text_state,512,event);
        }
        else if(event[1]==8||event[1]==127) {
            if(wm_state[2]==1&&length_b>0)message_b[--length_b]=0;
        } else if(event[1]>=32&&event[1]<127) {
            if(wm_state[2]==1&&length_b<30) {
                message_b[length_b++]=event[1];message_b[length_b]=0;
            }
        }
    }
    if(bob_gfx_leave())return 2;
    bob_puts("bob!");
    return 0;
}
