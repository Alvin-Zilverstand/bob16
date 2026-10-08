#ifndef BOB_WM_H
#define BOB_WM_H

#include "bob_event.h"

/* App-owned window records contain x,y,w,h,visible,dirty,allocated. */
#define BOB_WM_MAX_WINDOWS 8
#define BOB_WM_WINDOW_WORDS 7
#define BOB_WM_STATE_WORDS 10

/* State contains max,count,focus,drag,offset-x,offset-y,screen-w,screen-h,
   full-dirty,draw-cursor. Z-order is an app-owned bottom-to-top ID array. */
static int bob_wm_window_valid(const int *state,const int *windows,int id) {
    return state&&windows&&id>=0&&id<state[0]&&windows[id*BOB_WM_WINDOW_WORDS+6];
}

static int bob_wm_intersects(const int *a,const int *b) {
    return a[0]<b[0]+b[2]&&b[0]<a[0]+a[2]&&a[1]<b[1]+b[3]&&b[1]<a[1]+a[3];
}

static void bob_wm_clamp(int *window,const int *state) {
    int max_x=state[6]-window[2],max_y=state[7]-window[3];
    if(max_x<0)max_x=0;if(max_y<0)max_y=0;
    if(window[0]<0)window[0]=0;if(window[0]>max_x)window[0]=max_x;
    if(window[1]<0)window[1]=0;if(window[1]>max_y)window[1]=max_y;
}

int bob_wm_init(int *state,int *windows,int *order,int maximum,int screen_width,int screen_height) {
    if(!state||!windows||!order||maximum<1||maximum>BOB_WM_MAX_WINDOWS||
       screen_width<1||screen_height<1)return -1;
    for(int i=0;i<BOB_WM_STATE_WORDS;i++)state[i]=0;
    state[0]=maximum;state[2]=-1;state[3]=-1;state[6]=screen_width;state[7]=screen_height;state[8]=1;
    for(int i=0;i<maximum*BOB_WM_WINDOW_WORDS;i++)windows[i]=0;
    for(int i=0;i<maximum;i++)order[i]=-1;
    return 0;
}

static void bob_wm_mark_all(int *state,int *windows,const int *order) {
    for(int i=0;i<state[1];i++) {
        int id=order[i];
        if(bob_wm_window_valid(state,windows,id)&&windows[id*BOB_WM_WINDOW_WORDS+4])
            windows[id*BOB_WM_WINDOW_WORDS+5]=1;
    }
    state[8]=1;
}

int bob_wm_create(int *state,int *windows,int *order,int x,int y,int width,int height) {
    int id=-1;
    if(!state||!windows||!order||width<4||height<3||width>state[6]||height>state[7])return -1;
    for(int i=0;i<state[0];i++)if(!windows[i*BOB_WM_WINDOW_WORDS+6]){id=i;break;}
    if(id<0||state[1]>=state[0])return -1;
    int *window=windows+id*BOB_WM_WINDOW_WORDS;
    window[0]=x;window[1]=y;window[2]=width;window[3]=height;
    window[4]=1;window[5]=1;window[6]=1;bob_wm_clamp(window,state);
    order[state[1]++]=id;state[2]=id;state[3]=-1;
    for(int i=0;i<state[1]-1;i++)windows[order[i]*BOB_WM_WINDOW_WORDS+5]=1;
    state[8]=1;return id;
}

int bob_wm_raise(int *state,int *windows,int *order,int id) {
    int index=-1;
    if(!bob_wm_window_valid(state,windows,id)||!windows[id*BOB_WM_WINDOW_WORDS+4])return -1;
    for(int i=0;i<state[1];i++)if(order[i]==id){index=i;break;}
    if(index<0)return -1;
    if(index==state[1]-1)return 0;
    for(int i=index;i<state[1]-1;i++)order[i]=order[i+1];
    order[state[1]-1]=id;bob_wm_mark_all(state,windows,order);return 1;
}

int bob_wm_focus(int *state,int *windows,int *order,int id) {
    int old;
    if(!bob_wm_window_valid(state,windows,id)||!windows[id*BOB_WM_WINDOW_WORDS+4])return -1;
    old=state[2];
    bob_wm_raise(state,windows,order,id);
    if(old>=0&&old<state[0])windows[old*BOB_WM_WINDOW_WORDS+5]=1;
    state[2]=id;windows[id*BOB_WM_WINDOW_WORDS+5]=1;
    return 0;
}

int bob_wm_move(int *state,int *windows,const int *order,int id,int x,int y) {
    int *window;
    if(!bob_wm_window_valid(state,windows,id))return -1;
    window=windows+id*BOB_WM_WINDOW_WORDS;
    window[0]=x;window[1]=y;bob_wm_clamp(window,state);
    bob_wm_mark_all(state,windows,order);return 0;
}

int bob_wm_show(int *state,int *windows,int *order,int id,int visible) {
    int *window;
    if(!bob_wm_window_valid(state,windows,id))return -1;
    window=windows+id*BOB_WM_WINDOW_WORDS;visible=visible!=0;
    if(window[4]==visible)return 0;
    window[4]=visible;
    if(visible)bob_wm_focus(state,windows,order,id);
    else if(state[3]==id)state[3]=-1;
    else if(state[2]==id) {
        state[2]=-1;
        for(int i=state[1]-1;i>=0;i--)if(windows[order[i]*BOB_WM_WINDOW_WORDS+4]){state[2]=order[i];break;}
        if(state[2]>=0)windows[state[2]*BOB_WM_WINDOW_WORDS+5]=1;
    }
    bob_wm_mark_all(state,windows,order);return 1;
}

int bob_wm_destroy(int *state,int *windows,int *order,int id) {
    int index=-1,was_focused;
    if(!bob_wm_window_valid(state,windows,id))return -1;
    for(int i=0;i<state[1];i++)if(order[i]==id){index=i;break;}
    if(index<0)return -1;
    was_focused=state[2]==id;
    if(state[3]==id)state[3]=-1;
    for(int i=index;i<state[1]-1;i++)order[i]=order[i+1];
    order[--state[1]]=-1;
    for(int i=0;i<BOB_WM_WINDOW_WORDS;i++)windows[id*BOB_WM_WINDOW_WORDS+i]=0;
    if(was_focused) {
        state[2]=-1;
        for(int i=state[1]-1;i>=0;i--)if(windows[order[i]*BOB_WM_WINDOW_WORDS+4]){state[2]=order[i];break;}
    }
    if(state[2]>=0)windows[state[2]*BOB_WM_WINDOW_WORDS+5]=1;
    bob_wm_mark_all(state,windows,order);return 0;
}

int bob_wm_window_at(const int *state,const int *windows,const int *order,int x,int y) {
    if(!state||!windows||!order)return -1;
    for(int i=state[1]-1;i>=0;i--) {
        int id=order[i];int *window=(int *)(windows+id*BOB_WM_WINDOW_WORDS);
        if(bob_wm_window_valid(state,windows,id)&&window[4]&&
           x>=window[0]&&y>=window[1]&&x<window[0]+window[2]&&y<window[1]+window[3])return id;
    }
    return -1;
}

/* Returns a recipient ID or -1; routed mouse coordinates become window-local. */
int bob_wm_dispatch(int *state,int *windows,int *order,const int *event,int *routed) {
    int type,target=-1,x,y;
    if(!state||!windows||!order||!event||!routed)return -1;
    type=event[0];for(int i=0;i<4;i++)routed[i]=event[i];
    if(type==BOB_EVENT_KEY_DOWN||type==BOB_EVENT_KEY_UP||type==BOB_EVENT_CHAR) {
        target=state[2];
        if(!bob_wm_window_valid(state,windows,target)||!windows[target*BOB_WM_WINDOW_WORDS+4])return -1;
        return target;
    }
    if(type!=BOB_EVENT_MOUSE_MOVE&&type!=BOB_EVENT_MOUSE_BUTTON_DOWN&&
       type!=BOB_EVENT_MOUSE_BUTTON_UP&&type!=BOB_EVENT_MOUSE_WHEEL)return -1;
    x=event[2];y=event[3];
    if(type==BOB_EVENT_MOUSE_MOVE&&bob_wm_window_valid(state,windows,state[3])) {
        target=state[3];bob_wm_move(state,windows,order,target,x-state[4],y-state[5]);
    } else if(type==BOB_EVENT_MOUSE_BUTTON_UP&&bob_wm_window_valid(state,windows,state[3])) {
        target=state[3];bob_wm_move(state,windows,order,target,x-state[4],y-state[5]);state[3]=-1;
    } else {
        target=bob_wm_window_at(state,windows,order,x,y);
        if(type==BOB_EVENT_MOUSE_BUTTON_DOWN&&target>=0) {
            bob_wm_focus(state,windows,order,target);
            int *window=windows+target*BOB_WM_WINDOW_WORDS;
            if((event[1]&BOB_MOUSE_BUTTON_LEFT)&&y==window[1]) {
                state[3]=target;state[4]=x-window[0];state[5]=y-window[1];
            }
        }
    }
    if(target<0)return -1;
    routed[2]=x-windows[target*BOB_WM_WINDOW_WORDS];
    routed[3]=y-windows[target*BOB_WM_WINDOW_WORDS+1];
    return target;
}

void bob_wm_invalidate(int *state,int *windows,int *order,int id) {
    if(!bob_wm_window_valid(state,windows,id))return;
    windows[id*BOB_WM_WINDOW_WORDS+5]=1;
    for(int i=0;i<state[1];i++) {
        int other=order[i];
        if(other==id)continue;
        if(bob_wm_window_valid(state,windows,other)&&windows[other*BOB_WM_WINDOW_WORDS+4]&&
           bob_wm_intersects(windows+id*BOB_WM_WINDOW_WORDS,windows+other*BOB_WM_WINDOW_WORDS))
            windows[other*BOB_WM_WINDOW_WORDS+5]=1;
    }
}

/* Prepare a redraw pass and return dirty visible IDs in bottom-to-top order. */
int bob_wm_begin_draw(int *state,int *windows,const int *order) {
    if(!state||!windows||!order)return -1;
    if(state[8])bob_wm_mark_all(state,windows,order);
    state[8]=0;state[9]=0;return 0;
}

int bob_wm_next_dirty(int *state,int *windows,const int *order) {
    if(!state||!windows||!order)return -1;
    while(state[9]<state[1]) {
        int id=order[state[9]++];int *window=windows+id*BOB_WM_WINDOW_WORDS;
        if(bob_wm_window_valid(state,windows,id)&&window[4]&&window[5]){window[5]=0;return id;}
    }
    return -1;
}

#endif
