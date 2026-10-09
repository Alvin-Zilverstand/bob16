#include "lz4.h"

#define LZ4_HASH_BITS 16u
#define LZ4_HASH_SIZE (1u<<LZ4_HASH_BITS)
#define LZ4_MIN_MATCH 4u
#define LZ4_LAST_LITERALS 5u
#define LZ4_MATCH_FIND_LIMIT 12u

static u32 read32le(const u8 *bytes) {
    return (u32)bytes[0]|((u32)bytes[1]<<8)|((u32)bytes[2]<<16)|
           ((u32)bytes[3]<<24);
}

static u32 hash_sequence(u32 sequence) {
    return (sequence*2654435761u)>>(32-LZ4_HASH_BITS);
}

usize bob64_lz4_compress_bound(usize input_size) {
    usize overhead=input_size/255+16;
    return input_size>~(usize)0-overhead?0:input_size+overhead;
}

static int write_length(u8 *output,usize capacity,usize *offset,usize length) {
    while(length>=255) {
        if(*offset>=capacity)return -1;
        output[(*offset)++]=255;
        length-=255;
    }
    if(*offset>=capacity)return -1;
    output[(*offset)++]=(u8)length;
    return 0;
}

int bob64_lz4_compress(const void *input,usize input_size,void *output,
                       usize output_capacity,usize *output_size,
                       void *workspace,usize workspace_size) {
    const u8 *source=(const u8 *)input;
    u8 *destination=(u8 *)output;
    u32 *table=(u32 *)workspace;
    usize position=0,anchor=0,written=0;
    if(!source||!destination||!output_size||!workspace||
       workspace_size<BOB64_LZ4_WORKSPACE_SIZE||
       ((uintptr_t)workspace&(sizeof(u32)-1))||input_size>0xffffffffu)
        return -1;
    *output_size=0;
    for(u32 i=0;i<LZ4_HASH_SIZE;i++)table[i]=0xffffffffu;
    while(position+LZ4_MATCH_FIND_LIMIT<=input_size) {
        u32 sequence=read32le(source+position);
        u32 hash=hash_sequence(sequence);
        u32 reference=table[hash];
        table[hash]=(u32)position;
        if(reference!=0xffffffffu&&position-reference<=65535u&&
           read32le(source+reference)==sequence) {
            usize match_start=position;
            usize match_length=LZ4_MIN_MATCH;
            usize literal_length=match_start-anchor;
            usize token_offset=written;
            if(written>=output_capacity)return -1;
            destination[written++]=0;
            if(literal_length>=15&&write_length(destination,output_capacity,
                  &written,literal_length-15))return -1;
            if(literal_length>output_capacity-written)return -1;
            for(usize i=0;i<literal_length;i++)
                destination[written++]=source[anchor+i];
            if(output_capacity-written<2)return -1;
            u32 distance=(u32)(match_start-reference);
            destination[written++]=(u8)distance;
            destination[written++]=(u8)(distance>>8);
            while(match_start+match_length<input_size-LZ4_LAST_LITERALS&&
                  source[reference+match_length]==source[match_start+match_length])
                match_length++;
            usize encoded_match=match_length-LZ4_MIN_MATCH;
            if(encoded_match>=15&&write_length(destination,output_capacity,
                  &written,encoded_match-15))return -1;
            destination[token_offset]=(u8)((literal_length<15?literal_length:15)<<4)|
                (u8)(encoded_match<15?encoded_match:15);
            position=match_start+match_length;
            anchor=position;
            usize insert=match_start+1;
            while(insert+LZ4_MIN_MATCH<=position&&
                  insert+LZ4_MIN_MATCH<=input_size) {
                table[hash_sequence(read32le(source+insert))]=(u32)insert;
                insert++;
            }
        } else position++;
    }
    usize literal_length=input_size-anchor;
    if(written>=output_capacity)return -1;
    usize token_offset=written++;
    destination[token_offset]=(u8)((literal_length<15?literal_length:15)<<4);
    if(literal_length>=15&&write_length(destination,output_capacity,&written,
                                        literal_length-15))return -1;
    if(literal_length>output_capacity-written)return -1;
    for(usize i=0;i<literal_length;i++)destination[written++]=source[anchor+i];
    *output_size=written;
    return 0;
}

static int read_length(const u8 *source,usize source_size,usize *position,
                       usize *length) {
    u8 extension;
    do {
        if(*position>=source_size)return -1;
        extension=source[(*position)++];
        if(*length>~(usize)0-extension)return -1;
        *length+=extension;
    } while(extension==255);
    return 0;
}

int bob64_lz4_decompress(const void *input,usize input_size,void *output,
                         usize output_capacity,usize expected_output_size) {
    const u8 *source=(const u8 *)input;
    u8 *destination=(u8 *)output;
    usize input_position=0,output_position=0;
    if((!source&&input_size)||(!destination&&expected_output_size)||
       expected_output_size>output_capacity)return -1;
    while(input_position<input_size) {
        u8 token=source[input_position++];
        usize literal_length=token>>4;
        if(literal_length==15&&read_length(source,input_size,&input_position,
                                           &literal_length))return -1;
        if(literal_length>input_size-input_position||
           literal_length>output_capacity-output_position)return -1;
        for(usize i=0;i<literal_length;i++)
            destination[output_position++]=source[input_position++];
        if(input_position==input_size)break;
        if(input_size-input_position<2)return -1;
        usize distance=(usize)source[input_position]|((usize)source[input_position+1]<<8);
        input_position+=2;
        if(!distance||distance>output_position)return -1;
        usize match_length=(token&15)+LZ4_MIN_MATCH;
        if((token&15)==15&&read_length(source,input_size,&input_position,
                                        &match_length))return -1;
        if(match_length>output_capacity-output_position)return -1;
        for(usize i=0;i<match_length;i++) {
            destination[output_position]=destination[output_position-distance];
            output_position++;
        }
    }
    return input_position==input_size&&output_position==expected_output_size?0:-1;
}
