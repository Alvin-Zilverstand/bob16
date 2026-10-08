#include "filesystem.h"

static usize string_length(const char *text) {
    usize length=0;
    while(text[length])length++;
    return length;
}

static int valid_name(const char *name,usize *length) {
    usize size;
    if(!name)return 0;
    size=string_length(name);
    if(!size||size>=BOB64_FS_NAME_CAPACITY)return 0;
    for(usize i=0;i<size;i++) {
        char value=name[i];
        if(!((value>='a'&&value<='z')||(value>='A'&&value<='Z')||
             (value>='0'&&value<='9')||value=='.'||value=='_'||value=='-'))return 0;
    }
    if(length)*length=size;
    return 1;
}

static BOB64_FS_FILE *find_file(const BOB64_FILESYSTEM *filesystem,const char *name,
                                BOB64_FS_FILE **previous) {
    BOB64_FS_FILE *before=0;
    if(!filesystem||!name)return 0;
    for(BOB64_FS_FILE *file=filesystem->First;file;file=file->Next) {
        const char *left=file->Name,*right=name;
        while(*left&&*right&&*left==*right){left++;right++;}
        if(!*left&&!*right) {
            if(previous)*previous=before;
            return file;
        }
        before=file;
    }
    if(previous)*previous=before;
    return 0;
}

int bob64_fs_init(BOB64_FILESYSTEM *filesystem,BOB64_HEAP *heap) {
    if(!filesystem||!heap)return -1;
    filesystem->First=0;filesystem->Heap=heap;
    filesystem->FileCount=0;filesystem->BytesUsed=0;
    return 0;
}

int bob64_fs_write(BOB64_FILESYSTEM *filesystem,const char *name,
                   const void *data,usize length) {
    usize name_length;
    BOB64_FS_FILE *file;
    char *new_data,*new_name=0;
    if(!filesystem||!filesystem->Heap||!valid_name(name,&name_length)||
       (!data&&length)||length==~(usize)0)return -1;
    const u8 *source=(const u8 *)data;
    file=find_file(filesystem,name,0);
    new_data=(char *)bob64_heap_alloc(filesystem->Heap,length+1);
    if(!new_data)return -1;
    for(usize i=0;i<length;i++)new_data[i]=(char)source[i];
    new_data[length]=0;
    if(!file) {
        file=(BOB64_FS_FILE *)bob64_heap_calloc(filesystem->Heap,1,sizeof(*file));
        new_name=(char *)bob64_heap_alloc(filesystem->Heap,name_length+1);
        if(!file||!new_name) {
            if(file)bob64_heap_free(filesystem->Heap,file);
            if(new_name)bob64_heap_free(filesystem->Heap,new_name);
            bob64_heap_free(filesystem->Heap,new_data);
            return -1;
        }
        for(usize i=0;i<name_length;i++)new_name[i]=name[i];
        new_name[name_length]=0;
        file->Name=new_name;file->Next=filesystem->First;
        filesystem->First=file;filesystem->FileCount++;
    } else {
        filesystem->BytesUsed-=file->Length;
        bob64_heap_free(filesystem->Heap,file->Data);
    }
    file->Data=new_data;file->Length=length;
    filesystem->BytesUsed+=length;
    return 0;
}

int bob64_fs_read(const BOB64_FILESYSTEM *filesystem,const char *name,
                  const char **data,usize *length) {
    BOB64_FS_FILE *file;
    if(!filesystem||!data||!length||!valid_name(name,0))return -1;
    file=find_file(filesystem,name,0);
    if(!file)return -1;
    *data=file->Data;*length=file->Length;
    return 0;
}

int bob64_fs_read_at(const BOB64_FILESYSTEM *filesystem,const char *name,
                     u64 offset,void *buffer,usize capacity,usize *read_count) {
    BOB64_FS_FILE *file;
    usize count;
    if(!filesystem||!read_count||(!buffer&&capacity)||!valid_name(name,0))return -1;
    file=find_file(filesystem,name,0);
    if(!file)return -1;
    if(offset>=file->Length){*read_count=0;return 0;}
    count=file->Length-(usize)offset;
    if(count>capacity)count=capacity;
    for(usize i=0;i<count;i++)((u8 *)buffer)[i]=(u8)file->Data[(usize)offset+i];
    *read_count=count;
    return 0;
}

int bob64_fs_write_at(BOB64_FILESYSTEM *filesystem,const char *name,
                      u64 offset,const void *buffer,usize length) {
    BOB64_FS_FILE *file;
    usize end,new_length;
    char *replacement;
    if(!filesystem||!filesystem->Heap||(!buffer&&length)||
       offset>(u64)(~(usize)0)||length>(usize)(~(usize)0)-(usize)offset||
       !valid_name(name,0))return -1;
    end=(usize)offset+length;
    file=find_file(filesystem,name,0);
    if(!file) {
        if(offset)return -1;
        return bob64_fs_write(filesystem,name,buffer,length);
    }
    new_length=file->Length>end?file->Length:end;
    if(new_length==~(usize)0)return -1;
    replacement=(char *)bob64_heap_alloc(filesystem->Heap,new_length+1);
    if(!replacement)return -1;
    for(usize i=0;i<new_length;i++)replacement[i]=i<file->Length?file->Data[i]:0;
    for(usize i=0;i<length;i++)replacement[(usize)offset+i]=((const char *)buffer)[i];
    replacement[new_length]=0;
    filesystem->BytesUsed-=file->Length;
    filesystem->BytesUsed+=new_length;
    bob64_heap_free(filesystem->Heap,file->Data);
    file->Data=replacement;file->Length=new_length;
    return 0;
}

int bob64_fs_delete(BOB64_FILESYSTEM *filesystem,const char *name) {
    BOB64_FS_FILE *previous=0,*file;
    if(!filesystem||!filesystem->Heap||!valid_name(name,0))return -1;
    file=find_file(filesystem,name,&previous);
    if(!file)return -1;
    if(previous)previous->Next=file->Next;
    else filesystem->First=file->Next;
    filesystem->BytesUsed-=file->Length;filesystem->FileCount--;
    bob64_heap_free(filesystem->Heap,file->Name);
    bob64_heap_free(filesystem->Heap,file->Data);
    bob64_heap_free(filesystem->Heap,file);
    return 0;
}

void bob64_fs_clear(BOB64_FILESYSTEM *filesystem) {
    if(!filesystem)return;
    while(filesystem->First) {
        char name[BOB64_FS_NAME_CAPACITY];
        usize length=string_length(filesystem->First->Name);
        for(usize i=0;i<length;i++)name[i]=filesystem->First->Name[i];
        name[length]=0;
        if(bob64_fs_delete(filesystem,name))return;
    }
}

usize bob64_fs_list(const BOB64_FILESYSTEM *filesystem,
                    BOB64_FS_LIST_CALLBACK callback,void *context) {
    usize count=0;
    if(!filesystem)return 0;
    for(BOB64_FS_FILE *file=filesystem->First;file;file=file->Next) {
        count++;
        if(callback&&callback(context,file->Name,file->Length))break;
    }
    return count;
}
