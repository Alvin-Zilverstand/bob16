#include "libc.h"
#include <stdarg.h>

void *bob64_memcpy(void *destination,const void *source,usize length) {
    u8 *out=(u8 *)destination;
    const u8 *in=(const u8 *)source;
    for(usize i=0;i<length;i++)out[i]=in[i];
    return destination;
}

void *bob64_memmove(void *destination,const void *source,usize length) {
    u8 *out=(u8 *)destination;
    const u8 *in=(const u8 *)source;
    if((uintptr_t)out<(uintptr_t)in)for(usize i=0;i<length;i++)out[i]=in[i];
    else if((uintptr_t)out>(uintptr_t)in)
        for(usize i=length;i;i--)out[i-1]=in[i-1];
    return destination;
}

void *bob64_memset(void *destination,int value,usize length) {
    u8 *out=(u8 *)destination;
    for(usize i=0;i<length;i++)out[i]=(u8)value;
    return destination;
}

int bob64_memcmp(const void *left,const void *right,usize length) {
    const u8 *a=(const u8 *)left,*b=(const u8 *)right;
    for(usize i=0;i<length;i++)if(a[i]!=b[i])return a[i]<b[i]?-1:1;
    return 0;
}

void *bob64_memchr(const void *memory,int value,usize length) {
    const u8 *bytes=(const u8 *)memory;
    for(usize i=0;i<length;i++)
        if(bytes[i]==(u8)value)return (void *)(uintptr_t)(bytes+i);
    return 0;
}

usize bob64_strlen(const char *text) {
    usize length=0;
    while(text[length])length++;
    return length;
}

usize bob64_strnlen(const char *text,usize limit) {
    usize length=0;
    while(length<limit&&text[length])length++;
    return length;
}

int bob64_strcmp(const char *left,const char *right) {
    while(*left&&*left==*right){left++;right++;}
    return (u8)*left<(u8)*right?-1:(u8)*left>(u8)*right?1:0;
}

int bob64_strncmp(const char *left,const char *right,usize limit) {
    for(usize i=0;i<limit;i++) {
        u8 a=(u8)left[i],b=(u8)right[i];
        if(a!=b)return a<b?-1:1;
        if(!a)return 0;
    }
    return 0;
}

char *bob64_strcpy(char *destination,const char *source) {
    char *result=destination;
    while((*destination++=*source++)!=0) { }
    return result;
}

char *bob64_strncpy(char *destination,const char *source,usize count) {
    char *result=destination;
    usize i=0;
    for(;i<count&&source[i];i++)destination[i]=source[i];
    for(;i<count;i++)destination[i]=0;
    return result;
}

char *bob64_strcat(char *destination,const char *source) {
    char *result=destination;
    destination+=bob64_strlen(destination);
    while((*destination++=*source++)!=0) { }
    return result;
}

char *bob64_strncat(char *destination,const char *source,usize count) {
    char *result=destination;
    destination+=bob64_strlen(destination);
    for(usize i=0;i<count&&source[i];i++)destination[i]=source[i];
    usize appended=bob64_strnlen(source,count);
    destination[appended]=0;
    return result;
}

char *bob64_strchr(const char *text,int character) {
    u8 wanted=(u8)character;
    for(;;text++) {
        if((u8)*text==wanted)return (char *)(uintptr_t)text;
        if(!*text)return 0;
    }
}

char *bob64_strrchr(const char *text,int character) {
    u8 wanted=(u8)character;
    const char *match=0;
    for(;;text++) {
        if((u8)*text==wanted)match=text;
        if(!*text)return (char *)(uintptr_t)match;
    }
}

char *bob64_strstr(const char *text,const char *needle) {
    if(!*needle)return (char *)(uintptr_t)text;
    for(;*text;text++) {
        usize i=0;
        while(needle[i]&&text[i]==needle[i])i++;
        if(!needle[i])return (char *)(uintptr_t)text;
    }
    return 0;
}

typedef struct {
    char *Buffer;
    usize Capacity,Length;
    int Failed;
} BOB64_FORMAT_WRITER;

static void format_put(BOB64_FORMAT_WRITER *writer,char character) {
    if(!writer||writer->Failed)return;
    if(writer->Length==(usize)-1) {writer->Failed=1;return;}
    if(writer->Capacity&&writer->Length<writer->Capacity-1)
        writer->Buffer[writer->Length]=character;
    writer->Length++;
}

static void format_repeat(BOB64_FORMAT_WRITER *writer,char character,
                          usize count) {
    if(!writer||writer->Failed)return;
    if(count>(usize)-1-writer->Length) {writer->Failed=1;return;}
    usize writable=0;
    if(writer->Capacity&&writer->Length<writer->Capacity-1) {
        writable=writer->Capacity-1-writer->Length;
        if(writable>count)writable=count;
        for(usize i=0;i<writable;i++)writer->Buffer[writer->Length+i]=character;
    }
    writer->Length+=count;
}

static void format_text(BOB64_FORMAT_WRITER *writer,const char *text,
                        usize length,usize width,int left) {
    usize padding=width>length?width-length:0;
    if(!left)format_repeat(writer,' ',padding);
    for(usize i=0;i<length;i++)format_put(writer,text[i]);
    if(left)format_repeat(writer,' ',padding);
}

static void format_integer(BOB64_FORMAT_WRITER *writer,u64 value,int negative,
        u32 base,int uppercase,usize width,int left,int zero,int alternate,
        int precision_set,usize precision,int force_pointer_prefix,
        int show_plus,int show_space) {
    static const char lower_digits[]="0123456789abcdef";
    static const char upper_digits[]="0123456789ABCDEF";
    const char *digits=uppercase?upper_digits:lower_digits;
    char reversed[65];
    usize digit_count=0,zero_count=0,sign_count=negative||show_plus||show_space;
    const char *prefix="";
    usize prefix_length=0,body_length,padding;
    if(value||!precision_set||precision) {
        do {reversed[digit_count++]=digits[value%base];value/=base;}
        while(value);
    }
    if(precision_set&&precision>digit_count)zero_count=precision-digit_count;
    if(force_pointer_prefix||((alternate)&&digit_count&&base==16)) {
        prefix=uppercase?"0X":"0x";prefix_length=2;
    } else if(alternate&&base==8&&(!digit_count||zero_count==0||
                                  reversed[digit_count-1]!='0')) {
        prefix="0";prefix_length=1;
    }
    body_length=sign_count+prefix_length+zero_count+digit_count;
    padding=width>body_length?width-body_length:0;
    if(!left&&(!zero||precision_set))format_repeat(writer,' ',padding);
    if(negative)format_put(writer,'-');
    else if(show_plus)format_put(writer,'+');
    else if(show_space)format_put(writer,' ');
    for(usize i=0;i<prefix_length;i++)format_put(writer,prefix[i]);
    if(!left&&zero&&!precision_set)format_repeat(writer,'0',padding);
    format_repeat(writer,'0',zero_count);
    while(digit_count)format_put(writer,reversed[--digit_count]);
    if(left)format_repeat(writer,' ',padding);
}

static int format_parse_number(const char **format,usize *number) {
    usize result=0;
    int present=0;
    while(**format>='0'&&**format<='9') {
        usize digit=(usize)(**format-'0');
        if(result>((usize)0x7fffffff-digit)/10)return -1;
        result=result*10+digit;(*format)++;present=1;
    }
    *number=result;
    return present;
}

int bob64_snprintf(char *destination,usize capacity,const char *format,...) {
    BOB64_FORMAT_WRITER writer={destination,capacity,0,0};
    va_list arguments;
    if((capacity&&!destination)||!format)return -1;
    va_start(arguments,format);
    while(*format&&!writer.Failed) {
        if(*format!='%') {format_put(&writer,*format++);continue;}
        format++;
        if(*format=='%') {format_put(&writer,*format++);continue;}
        int left=0,plus=0,space=0,alternate=0,zero=0;
        int flags=1;
        while(flags) {
            switch(*format) {
            case '-':left=1;format++;break;
            case '+':plus=1;format++;break;
            case ' ':space=1;format++;break;
            case '#':alternate=1;format++;break;
            case '0':zero=1;format++;break;
            default:flags=0;break;
            }
        }
        usize width=0,precision=0;
        int number=format_parse_number(&format,&width);
        if(number<0) {writer.Failed=1;break;}
        if(*format=='*') {
            int requested=va_arg(arguments,int);format++;
            if(requested<0) {left=1;width=(usize)(-(s64)requested);}
            else width=(usize)requested;
        }
        int precision_set=0;
        if(*format=='.') {
            format++;precision_set=1;
            number=format_parse_number(&format,&precision);
            if(number<0) {writer.Failed=1;break;}
            if(*format=='*') {
                int requested=va_arg(arguments,int);format++;
                if(requested<0)precision_set=0;
                else precision=(usize)requested;
            }
        }
        enum { FORMAT_INT,FORMAT_LONG,FORMAT_LLONG,FORMAT_SIZE,
               FORMAT_SHORT,FORMAT_CHAR } length=FORMAT_INT;
        if(*format=='h') {
            format++;length=FORMAT_SHORT;
            if(*format=='h') {format++;length=FORMAT_CHAR;}
        } else if(*format=='l') {
            format++;length=FORMAT_LONG;
            if(*format=='l') {format++;length=FORMAT_LLONG;}
        } else if(*format=='z'||*format=='t') {
            format++;length=FORMAT_SIZE;
        } else if(*format=='j') {
            format++;length=FORMAT_LLONG;
        }
        char conversion=*format;
        if(!conversion) {writer.Failed=1;break;}
        format++;
        if(conversion=='s') {
            const char *text=va_arg(arguments,const char *);
            if(!text)text="(null)";
            usize count=precision_set?bob64_strnlen(text,precision):
                                      bob64_strlen(text);
            format_text(&writer,text,count,width,left);
        } else if(conversion=='c') {
            char character=(char)va_arg(arguments,int);
            usize padding=width>1?width-1:0;
            if(!left)format_repeat(&writer,' ',padding);
            format_put(&writer,character);
            if(left)format_repeat(&writer,' ',padding);
        } else if(conversion=='d'||conversion=='i') {
            s64 signed_value;
            if(length==FORMAT_LONG)signed_value=(s64)va_arg(arguments,long);
            else if(length==FORMAT_LLONG)signed_value=va_arg(arguments,long long);
            else if(length==FORMAT_SIZE)signed_value=(s64)va_arg(arguments,isize);
            else if(length==FORMAT_SHORT)
                signed_value=(short)va_arg(arguments,int);
            else if(length==FORMAT_CHAR)
                signed_value=(signed char)va_arg(arguments,int);
            else signed_value=va_arg(arguments,int);
            u64 magnitude=signed_value<0?0-(u64)signed_value:(u64)signed_value;
            format_integer(&writer,magnitude,signed_value<0,10,0,width,left,
                zero,0,precision_set,precision,0,plus,space);
        } else if(conversion=='u'||conversion=='o'||conversion=='x'||
                  conversion=='X'||conversion=='p') {
            u64 value;
            u32 base=conversion=='o'?8u:
                     (conversion=='x'||conversion=='X'||conversion=='p')?16u:10u;
            if(conversion=='p')value=(u64)(uintptr_t)va_arg(arguments,void *);
            else if(length==FORMAT_LONG)value=(u64)va_arg(arguments,unsigned long);
            else if(length==FORMAT_LLONG)value=va_arg(arguments,unsigned long long);
            else if(length==FORMAT_SIZE)value=(u64)va_arg(arguments,usize);
            else if(length==FORMAT_SHORT)value=(u16)va_arg(arguments,int);
            else if(length==FORMAT_CHAR)value=(u8)va_arg(arguments,int);
            else value=va_arg(arguments,unsigned int);
            format_integer(&writer,value,0,base,conversion=='X',width,left,
                zero,alternate||conversion=='p',precision_set,precision,
                conversion=='p',0,0);
        } else writer.Failed=1;
    }
    va_end(arguments);
    if(capacity)destination[writer.Length<capacity?writer.Length:capacity-1]=0;
    if(writer.Failed||writer.Length>(usize)0x7fffffff)return -1;
    return (int)writer.Length;
}
