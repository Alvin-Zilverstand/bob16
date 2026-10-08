#ifndef BOB_EVENT_QUEUE_H
#define BOB_EVENT_QUEUE_H

#include "bob_event.h"

/* Caller-owned FIFO state is head,count; storage holds capacity four-word events. */
void bob_event_queue_init(int *state) {
    if(!state)return;
    state[0]=0;state[1]=0;
}

/* Push returns 1 on success, 0 if full, and -1 for invalid state or arguments. */
int bob_event_queue_push(int *state,int *storage,int capacity,const int *event) {
    int tail;
    if(!state||!storage||!event||capacity<1||state[0]<0||state[0]>=capacity||
       state[1]<0||state[1]>capacity)return -1;
    if(state[1]==capacity)return 0;
    tail=(state[0]+state[1])%capacity;
    for(int i=0;i<4;i++)storage[tail*4+i]=event[i];
    state[1]++;
    return 1;
}

/* Pop returns 1 with an event, 0 when empty, and -1 for invalid state or arguments. */
int bob_event_queue_pop(int *state,int *storage,int capacity,int *event) {
    int head;
    if(!state||!storage||!event||capacity<1||state[0]<0||state[0]>=capacity||
       state[1]<0||state[1]>capacity)return -1;
    if(!state[1])return 0;
    head=state[0];
    for(int i=0;i<4;i++)event[i]=storage[head*4+i];
    state[0]=(head+1)%capacity;state[1]--;
    return 1;
}

#endif
