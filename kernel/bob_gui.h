#ifndef BOB_GUI_H
#define BOB_GUI_H

#include "bob_gfx.h"
#include "bob_event.h"
#include "bob_wm.h"

/* Rectangles are x, y, width, height; drag state is active, offset-x, offset-y. */
int bob_gui_window_hit(int x,int y,const int *rect) {
    return x>=rect[0]&&y>=rect[1]&&x<rect[0]+rect[2]&&y<rect[1]+rect[3];
}

/* Rectangles are packed x,y,width,height; order is bottom-to-top window IDs. */
int bob_gui_window_at(int x,int y,const int *rectangles,const int *order,int count) {
    for(int i=count-1;i>=0;i--) {
        int id=order[i];
        if(id>=0&&id<count&&bob_gui_window_hit(x,y,rectangles+id*4))return id;
    }
    return -1;
}

int bob_gui_raise_window(int *order,int count,int id) {
    int index=-1;
    for(int i=0;i<count;i++)if(order[i]==id){index=i;break;}
    if(index<0)return 0;
    for(int i=index;i<count-1;i++)order[i]=order[i+1];
    order[count-1]=id;return 1;
}

int bob_gui_drag_start(const int *rect,int *drag,int x,int y) {
    if(x<rect[0]||x>=rect[0]+rect[2]||y!=rect[1])return 0;
    drag[0]=1;drag[1]=x-rect[0];drag[2]=y-rect[1];return 1;
}

int bob_gui_drag_update(int *rect,int *drag,int x,int y,int screen_width,int screen_height) {
    int max_x,max_y;
    if(!drag[0])return 0;
    max_x=screen_width-rect[2];max_y=screen_height-rect[3];
    if(max_x<0)max_x=0;if(max_y<0)max_y=0;
    rect[0]=x-drag[1];rect[1]=y-drag[2];
    if(rect[0]<0)rect[0]=0;if(rect[0]>max_x)rect[0]=max_x;
    if(rect[1]<0)rect[1]=0;if(rect[1]>max_y)rect[1]=max_y;
    return 1;
}

int bob_gui_drag_stop(int *drag) {
    int was_dragging=drag[0];drag[0]=0;return was_dragging;
}

/* Button state is hovered, pressed; returns one only for an inside release. */
int bob_gui_button_event(const int *rect,int *state,const int *event) {
    int inside=bob_gui_window_hit(event[2],event[3],rect);
    if(event[0]==BOB_EVENT_MOUSE_MOVE)state[0]=inside;
    else if(event[0]==BOB_EVENT_MOUSE_BUTTON_DOWN) {
        state[0]=inside;state[1]=inside&&((event[1]&BOB_MOUSE_BUTTON_LEFT)!=0);
    } else if(event[0]==BOB_EVENT_MOUSE_BUTTON_UP) {
        state[0]=inside;
        if(state[1]&&inside&&!(event[1]&BOB_MOUSE_BUTTON_LEFT)){state[1]=0;return 1;}
        state[1]=0;
    }
    return 0;
}

int bob_gui_button_draw(int x,int y,int width,int height,const char *label,
                        int hovered,int pressed) {
    int color=pressed?14:(hovered?11:7);
    int label_length=0,label_start=0,label_x,label_width;
    if(width<3||height<3)return -1;
    while(label[label_length])label_length++;
    bob_gfx_fill_rect(x,y,width,height,' ',color);
    bob_gfx_line(x+1,y,x+width-2,y,pressed?'=':'-',color);
    bob_gfx_line(x+1,y+height-1,x+width-2,y+height-1,pressed?'=':'-',color);
    bob_gfx_line(x,y+1,x,y+height-2,'|',color);
    bob_gfx_line(x+width-1,y+1,x+width-1,y+height-2,'|',color);
    bob_gfx_pixel(x,y,'+',color);bob_gfx_pixel(x+width-1,y,'+',color);
    bob_gfx_pixel(x,y+height-1,'+',color);bob_gfx_pixel(x+width-1,y+height-1,'+',color);
    label_width=width-2;
    if(label_length>label_width){label_start=(label_length-label_width)/2;label_length=label_width;}
    label_x=x+1+(label_width-label_length)/2;
    for(int i=0;i<label_length;i++)bob_gfx_pixel(label_x+i,y+height/2,label[label_start+i],15);
    return 0;
}

/* Draws a one-row-per-item popup; items is a fixed-stride label array. */
int bob_gui_menu_draw(const int *rect,const char *items,int count,int stride,int selected) {
    int label_width=rect[2]-4;
    char label[80];
    if(count<1||stride<1||rect[2]<6||rect[2]>80||rect[3]!=count+2||
       rect[0]<0||rect[1]<0||rect[0]+rect[2]>80||rect[1]+rect[3]>25)return -1;
    bob_gfx_fill_rect(rect[0],rect[1],rect[2],rect[3],' ',7);
    bob_gfx_line(rect[0]+1,rect[1],rect[0]+rect[2]-2,rect[1],'-',7);
    bob_gfx_line(rect[0]+1,rect[1]+rect[3]-1,rect[0]+rect[2]-2,rect[1]+rect[3]-1,'-',7);
    bob_gfx_line(rect[0],rect[1]+1,rect[0],rect[1]+rect[3]-2,'|',7);
    bob_gfx_line(rect[0]+rect[2]-1,rect[1]+1,rect[0]+rect[2]-1,rect[1]+rect[3]-2,'|',7);
    bob_gfx_pixel(rect[0],rect[1],'+',7);bob_gfx_pixel(rect[0]+rect[2]-1,rect[1],'+',7);
    bob_gfx_pixel(rect[0],rect[1]+rect[3]-1,'+',7);bob_gfx_pixel(rect[0]+rect[2]-1,rect[1]+rect[3]-1,'+',7);
    for(int row=0;row<count;row++) {
        int color=row==selected?4:7;int length=0;const char *source=items+row*stride;
        bob_gfx_fill_rect(rect[0]+1,rect[1]+1+row,rect[2]-2,1,' ',color);
        bob_gfx_pixel(rect[0]+1,rect[1]+1+row,row==selected?'>':' ',color);
        while(length<label_width&&length<stride&&source[length]){label[length]=source[length];length++;}
        label[length]=0;bob_gfx_text(rect[0]+2,rect[1]+1+row,label,row==selected?15:8);
    }
    return 0;
}

/* Places a popup below its anchor and clamps it inside the screen rectangle. */
int bob_gui_menu_place(int *rect,int anchor_x,int anchor_y,int width,int count,
                       int screen_width,int screen_height) {
    int height=count+2;
    if(width<6||count<1||screen_width<width||screen_height<height)return -1;
    rect[0]=anchor_x;rect[1]=anchor_y;rect[2]=width;rect[3]=height;
    if(rect[0]+width>screen_width)rect[0]=screen_width-width;
    if(rect[0]<0)rect[0]=0;
    if(rect[1]+height>screen_height)rect[1]=screen_height-height;
    if(rect[1]<0)rect[1]=0;
    return 0;
}

/* Returns the activated row, -1 for no action, or -2 when Escape closes it. */
int bob_gui_menu_event(const int *rect,int count,int *state,const int *event) {
    int row=-1;
    if(count<1||rect[3]!=count+2)return -1;
    if(event[2]>=rect[0]+1&&event[2]<rect[0]+rect[2]-1&&
       event[3]>=rect[1]+1&&event[3]<rect[1]+1+count)row=event[3]-rect[1]-1;
    if(event[0]==BOB_EVENT_MOUSE_MOVE)state[0]=row;
    else if(event[0]==BOB_EVENT_MOUSE_BUTTON_DOWN) {
        state[0]=row;state[1]=row>=0&&(event[1]&BOB_MOUSE_BUTTON_LEFT)?row:-1;
    } else if(event[0]==BOB_EVENT_MOUSE_BUTTON_UP) {
        int activated=row>=0&&state[1]==row&&!(event[1]&BOB_MOUSE_BUTTON_LEFT)?row:-1;
        state[0]=row;state[1]=-1;return activated;
    } else if(event[0]==BOB_EVENT_KEY_DOWN) {
        if(event[1]==256)state[0]=state[0]<=0?count-1:state[0]-1;
        else if(event[1]==257)state[0]=state[0<0||state[0]>=count-1?0:state[0]+1];
    } else if(event[0]==BOB_EVENT_CHAR) {
        if(event[1]==27)return -2;
        if((event[1]==10||event[1]==13)&&state[0]>=0&&state[0]<count)return state[0];
    }
    return -1;
}

/* Text state is focused, caret index, horizontal scroll offset. */
int bob_gui_text_field_init(char *buffer,int *state,int capacity) {
    int length=0;
    if(capacity<1)return -1;
    while(length<capacity-1&&buffer[length])length++;
    state[0]=0;state[1]=length;state[2]=0;return length;
}

int bob_gui_text_field_event(const int *rect,char *buffer,int *state,int capacity,const int *event) {
    int length=0,visible,changed=0;
    while(length<capacity-1&&buffer[length])length++;
    visible=rect[2]-2;
    if(visible<1||capacity<1)return 0;
    if(event[0]==BOB_EVENT_MOUSE_BUTTON_DOWN) {
        state[0]=bob_gui_window_hit(event[2],event[3],rect);
        if(state[0]) {
            state[1]=state[2]+event[2]-rect[0]-1;
            if(state[1]<0)state[1]=0;if(state[1]>length)state[1]=length;
        }
    } else if(state[0]&&event[0]==BOB_EVENT_CHAR) {
        if(event[1]>=32&&event[1]<127&&length<capacity-1) {
            for(int i=length;i>state[1];i--)buffer[i]=buffer[i-1];
            buffer[state[1]++]=event[1];buffer[++length]=0;changed=1;
        } else if((event[1]==8||event[1]==127)&&state[1]>0) {
            for(int i=state[1]-1;i<length;i++)buffer[i]=buffer[i+1];
            state[1]--;length--;changed=1;
        }
    } else if(state[0]&&event[0]==BOB_EVENT_KEY_DOWN) {
        if(event[1]==258&&state[1]>0)state[1]--;
        else if(event[1]==259&&state[1]<length)state[1]++;
        else if(event[1]==260)state[1]=0;
        else if(event[1]==261)state[1]=length;
        else if(event[1]==262&&state[1]<length) {
            for(int i=state[1];i<length;i++)buffer[i]=buffer[i+1];
            length--;changed=1;
        }
    }
    if(state[1]<state[2])state[2]=state[1];
    if(state[1]>=state[2]+visible)state[2]=state[1]-visible+1;
    return changed;
}

int bob_gui_text_field_draw(const int *rect,const char *buffer,const int *state) {
    int visible=rect[2]-2;
    char shown[80];
    int i=0;
    if(rect[2]<3||rect[2]>80||rect[3]<3)return -1;
    bob_gfx_fill_rect(rect[0],rect[1],rect[2],rect[3],' ',state[0]?15:8);
    bob_gfx_line(rect[0]+1,rect[1],rect[0]+rect[2]-2,rect[1],'-',state[0]?15:8);
    bob_gfx_line(rect[0]+1,rect[1]+rect[3]-1,rect[0]+rect[2]-2,rect[1]+rect[3]-1,'-',state[0]?15:8);
    bob_gfx_line(rect[0],rect[1]+1,rect[0],rect[1]+rect[3]-2,'|',state[0]?15:8);
    bob_gfx_line(rect[0]+rect[2]-1,rect[1]+1,rect[0]+rect[2]-1,rect[1]+rect[3]-2,'|',state[0]?15:8);
    for(i=0;i<visible&&buffer[state[2]+i];i++)shown[i]=buffer[state[2]+i];
    shown[i]=0;bob_gfx_text(rect[0]+1,rect[1]+1,shown,15);
    if(state[0]&&state[1]-state[2]>=0&&state[1]-state[2]<visible)
        bob_gfx_pixel(rect[0]+1+state[1]-state[2],rect[1]+1,'|',14);
    return 0;
}

/* Text-area state is focused, byte cursor, first visible line, reserved. */
int bob_gui_text_area_init(char *buffer,int *state,int capacity) {
    int length=0;
    if(capacity<1)return -1;
    while(length<capacity-1&&buffer[length]) {
        length++;
    }
    state[0]=0;state[1]=length;state[2]=0;state[3]=0;
    return length;
}

static int bob_gui_text_area_insert(char *buffer,int *length,int *cursor,int capacity,char value) {
    if(*length>=capacity-1)return 0;
    for(int i=*length;i>*cursor;i--)buffer[i]=buffer[i-1];
    buffer[*cursor]=value;(*cursor)++;(*length)++;buffer[*length]=0;
    return 1;
}

static void bob_gui_text_area_position(const char *buffer,int cursor,int *line,int *column) {
    *line=0;*column=0;
    for(int i=0;i<cursor;i++) {
        if(buffer[i]=='\n'){(*line)++;*column=0;}
        else (*column)++;
    }
}

static void bob_gui_text_area_scroll(const char *buffer,int cursor,int *state,const int *rect) {
    int line,column,visible=rect[3]-2;
    bob_gui_text_area_position(buffer,cursor,&line,&column);
    if(line<state[2])state[2]=line;
    if(line>=state[2]+visible)state[2]=line-visible+1;
}

int bob_gui_text_area_reveal(const char *buffer,int *state,const int *rect) {
    bob_gui_text_area_scroll(buffer,state[1],state,rect);
    return 0;
}

int bob_gui_text_area_event(const int *rect,char *buffer,int *state,int capacity,const int *event) {
    int length=0,changed=0,line,column,start,end,next_start,next_end,target,visible,total_lines,max_scroll,old_scroll;
    while(length<capacity-1&&buffer[length])length++;
    if(capacity<1||rect[2]<3||rect[3]<3)return 0;
    if(event[0]==BOB_EVENT_MOUSE_WHEEL&&bob_gui_window_hit(event[2],event[3],rect)) {
        state[0]=1;old_scroll=state[2];visible=rect[3]-2;total_lines=1;
        for(int i=0;i<length;i++)if(buffer[i]=='\n')total_lines++;
        max_scroll=total_lines-visible;if(max_scroll<0)max_scroll=0;
        if(event[1]>0&&state[2]>0)state[2]--;
        else if(event[1]<0&&state[2]<max_scroll)state[2]++;
        return state[2]!=old_scroll;
    } else if(event[0]==BOB_EVENT_MOUSE_BUTTON_DOWN) {
        state[0]=bob_gui_window_hit(event[2],event[3],rect);
        if(state[0]) {
            target=state[2]+event[3]-rect[1]-1;
            column=event[2]-rect[0]-1;
            start=0;line=0;
            while(start<length&&line<target)if(buffer[start++]=='\n')line++;
            while(start<length&&buffer[start]!='\n'&&column>0){start++;column--;}
            state[1]=start;
        }
    } else if(state[0]&&event[0]==BOB_EVENT_CHAR) {
        if(event[1]==8||event[1]==127) {
            if(state[1]>0) {
                for(int i=state[1]-1;i<length;i++)buffer[i]=buffer[i+1];
                state[1]--;length--;changed=1;
            }
        } else if(event[1]==10||event[1]==13) {
            changed=bob_gui_text_area_insert(buffer,&length,&state[1],capacity,'\n');
        } else if(event[1]>=32&&event[1]<127) {
            changed=bob_gui_text_area_insert(buffer,&length,&state[1],capacity,(char)event[1]);
        }
    } else if(state[0]&&event[0]==BOB_EVENT_KEY_DOWN) {
        bob_gui_text_area_position(buffer,state[1],&line,&column);
        if(event[1]==258&&state[1]>0)state[1]--;
        else if(event[1]==259&&state[1]<length)state[1]++;
        else if(event[1]==260) {
            start=state[1];while(start>0&&buffer[start-1]!='\n')start--;state[1]=start;
        } else if(event[1]==261) {
            end=state[1];while(end<length&&buffer[end]!='\n')end++;state[1]=end;
        } else if(event[1]==262&&state[1]<length) {
            for(int i=state[1];i<length;i++)buffer[i]=buffer[i+1];
            length--;changed=1;
        } else if(event[1]==256||event[1]==257) {
            start=state[1];while(start>0&&buffer[start-1]!='\n')start--;
            end=state[1];while(end<length&&buffer[end]!='\n')end++;
            if(event[1]==256&&start>0) {
                next_end=start-1;next_start=next_end;
                while(next_start>0&&buffer[next_start-1]!='\n')next_start--;
                target=next_start+column;if(target>next_end)target=next_end;state[1]=target;
            } else if(event[1]==257&&end<length) {
                next_start=end+1;next_end=next_start;
                while(next_end<length&&buffer[next_end]!='\n')next_end++;
                target=next_start+column;if(target>next_end)target=next_end;state[1]=target;
            }
        }
    }
    bob_gui_text_area_scroll(buffer,state[1],state,rect);
    return changed;
}

int bob_gui_text_area_draw(const int *rect,const char *buffer,const int *state) {
    int width=rect[2]-2,height=rect[3]-2,length=0,line=0,column=0;
    int position=0,row,row_start,row_end,offset,shown_length,cursor_line,cursor_column;
    char shown[80];
    if(rect[2]<3||rect[2]>80||rect[3]<3)return -1;
    while(buffer[length])length++;
    bob_gfx_fill_rect(rect[0],rect[1],rect[2],rect[3],' ',state[0]?15:8);
    bob_gfx_line(rect[0]+1,rect[1],rect[0]+rect[2]-2,rect[1],'-',state[0]?15:8);
    bob_gfx_line(rect[0]+1,rect[1]+rect[3]-1,rect[0]+rect[2]-2,rect[1]+rect[3]-1,'-',state[0]?15:8);
    bob_gfx_line(rect[0],rect[1]+1,rect[0],rect[1]+rect[3]-2,'|',state[0]?15:8);
    bob_gfx_line(rect[0]+rect[2]-1,rect[1]+1,rect[0]+rect[2]-1,rect[1]+rect[3]-2,'|',state[0]?15:8);
    bob_gui_text_area_position(buffer,state[1],&cursor_line,&cursor_column);
    while(position<length&&line<state[2])if(buffer[position++]=='\n')line++;
    for(row=0;row<height&&position<=length;row++) {
        row_start=position;row_end=position;
        while(row_end<length&&buffer[row_end]!='\n')row_end++;
        offset=0;
        if(line==cursor_line&&cursor_column>=width)offset=cursor_column-width+1;
        shown_length=0;
        while(shown_length<width&&row_start+offset+shown_length<row_end) {
            shown[shown_length]=buffer[row_start+offset+shown_length];shown_length++;
        }
        shown[shown_length]=0;
        bob_gfx_text(rect[0]+1,rect[1]+1+row,shown,15);
        if(state[0]&&line==cursor_line&&cursor_column>=offset&&cursor_column-offset<width)
            bob_gfx_pixel(rect[0]+1+cursor_column-offset,rect[1]+1+row,'|',14);
        if(row_end>=length)break;
        position=row_end+1;line++;
    }
    return 0;
}

/* Reusable character-cell window frame and content area. */
int bob_gui_window(int x,int y,int width,int height,const char *title,
                   const char *body,const char *message,int focused) {
    int edge=focused?14:8;
    int title_color=focused?15:7;
    if(width<4||height<4)return -1;
    bob_gfx_fill_rect(x,y,width,height,' ',edge);
    bob_gfx_line(x+1,y,x+width-2,y,'-',edge);
    bob_gfx_line(x+1,y+height-1,x+width-2,y+height-1,'-',edge);
    bob_gfx_line(x,y+1,x,y+height-2,'|',edge);
    bob_gfx_line(x+width-1,y+1,x+width-1,y+height-2,'|',edge);
    bob_gfx_pixel(x,y,'+',edge);bob_gfx_pixel(x+width-1,y,'+',edge);
    bob_gfx_pixel(x,y+height-1,'+',edge);bob_gfx_pixel(x+width-1,y+height-1,'+',edge);
    bob_gfx_fill_rect(x+1,y+1,width-2,1,' ',focused?4:8);
    bob_gfx_text(x+2,y+1,title,title_color);
    bob_gfx_text(x+2,y+3,body,edge);
    if(message)bob_gfx_text(x+2,y+height-2,message,focused?10:7);
    return 0;
}

#endif
