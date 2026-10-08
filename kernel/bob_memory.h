#ifndef BOB_MEMORY_H
#define BOB_MEMORY_H

/* Caller-owned word arena; allocation sizes are target words, not host bytes. */
int bob_arena_reset(int *used) { if(!used)return -1;*used=0;return 0; }
int *bob_arena_alloc(int *storage,int capacity,int *used,int words) {
    int *result;
    if(!storage||!used||capacity<=0||words<=0||*used<0||*used>capacity)return 0;
    if(words>capacity-*used)return 0;
    result=storage+*used;*used+=words;return result;
}
int bob_arena_used(int *used) { if(!used||*used<0)return -1;return *used; }
int bob_arena_remaining(int capacity,int *used) {
    if(!used||capacity<0||*used<0||*used>capacity)return -1;
    return capacity-*used;
}

#endif
