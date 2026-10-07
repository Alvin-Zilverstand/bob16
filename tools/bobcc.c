/* BOB C subset compiler. Host tool, written entirely in C.
   Input is C preprocessed by GCC; output can target bob16 or native bob32. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

enum { T_ID=256, T_NUM, T_STR, T_EQ, T_NE, T_LE, T_GE, T_AND, T_OR,
       T_INC, T_DEC, T_SHL, T_SHR, T_ADDEQ, T_SUBEQ, T_MULEQ, T_DIVEQ,
       T_MODEQ, T_ANDEQ, T_OREQ, T_XOREQ, T_SHLEQ, T_SHREQ };
enum { N_NUM=1, N_STR, N_ID, N_BINARY, N_UNARY, N_ASSIGN, N_CALL, N_INDEX,
       N_BLOCK, N_DECL, N_IF, N_WHILE, N_FOR, N_RETURN, N_BREAK, N_CONTINUE,
       N_EMPTY, N_CAST, N_FUNCTION, N_DO, N_CONDITIONAL, N_SIZEOF, N_LABEL, N_GOTO,
       N_SWITCH, N_CASE, N_DECL_GROUP, N_PROTOTYPE };
typedef struct Node Node;
struct Node { int kind, op, value, type, count, storage, readonly; char *name; Node *a,*b,*c,*d,*next; };
typedef struct { int type, number, line; char *text; } Token;
static Token tokens[65536];
static int ntokens, at;
static int expression_depth;
static int parsing_parameter;
static int parsing_file_object;
static int parsing_external;
static int unsigned_helpers;
static int parsing_switch_depth;
static int parsed_const_base;
static int parsed_const_object;
static int parsed_const_alias_object;
typedef struct {char *name;int type,alias,readonly,object_readonly;} ParserBinding;
static ParserBinding parser_bindings[4096];
static int parser_binding_count,parsing_types;
static int parser_scope_base,parser_body_scope=-1;
typedef struct {char *name;int type;} EnumTag;
static EnumTag enum_tags[4096];
static int enum_tag_count,enum_scope_base,enum_serial;
static void error(const char *fmt,...);
static int composite_type(int a,int b);
static int function_index(const char *name);
/* Interned pointer descriptors retain pointee type and indirection depth.
   Primitive IDs remain int=0, void=3, char=4; array objects still use kind 2. */
typedef struct { int kind,base,count,qualifiers; } PointerType;
typedef struct { int count,unspecified,parameters[32]; } FunctionType;
static PointerType pointer_types[4096];
static int npointer_types;
static FunctionType function_types[1024];
static int nfunction_types;
static int derived_type(int kind,int base,int count,int qualifiers) {
    for(int i=0;i<npointer_types;i++)if(pointer_types[i].kind==kind && pointer_types[i].base==base && pointer_types[i].count==count && pointer_types[i].qualifiers==qualifiers)return 16+i;
    if(npointer_types==4096){fprintf(stderr,"bobcc: too many pointer types\n");exit(1);}
    pointer_types[npointer_types]=(PointerType){kind,base,count,qualifiers};return 16+npointer_types++;
}
static int pointer_to(int base) {return derived_type(1,base,1,0);}
static int pointer_to_const(int base) {return derived_type(1,base,1,1);}
static int array_of(int base,int count) {return derived_type(2,base,count,0);}
static int array_of_const(int base,int count) {return derived_type(2,base,count,1);}
static int function_signature(const int *parameters,int count,int unspecified) {
    if(count>32)error("function pointer has more than 32 parameters");
    for(int i=0;i<nfunction_types;i++) {
        FunctionType *f=&function_types[i];
        if(f->count!=count || f->unspecified!=unspecified)continue;
        int same=1;for(int j=0;j<count;j++)if(f->parameters[j]!=parameters[j])same=0;
        if(same)return i;
    }
    if(nfunction_types==1024)error("too many function pointer signatures");
    int index=nfunction_types++;
    function_types[index].count=count;function_types[index].unspecified=unspecified;
    for(int i=0;i<count;i++)function_types[index].parameters[i]=parameters[i];
    return index;
}
static int function_type(int result,int signature) {return derived_type(4,result,signature,0);}
static int is_derived(int type) {return type>=16 && type<16+npointer_types;}
static int is_pointer(int type) {return is_derived(type) && pointer_types[type-16].kind==1;}
static int is_array(int type) {return is_derived(type) && pointer_types[type-16].kind==2;}
static int is_enum(int type) {return is_derived(type) && pointer_types[type-16].kind==3;}
static int is_function(int type) {return is_derived(type) && pointer_types[type-16].kind==4;}
static int is_function_pointer(int type) {return is_pointer(type) && is_function(pointer_types[type-16].base);}
static int type_size(int type);
static int expression_type(Node *n);
static void assignment_compatible(int target,Node *value);
static int decay_type(int type);
static int global_address(Node *n,const char **name,int *offset);
static void binary_typed(int op,int left,int right);
static Node *top, **top_tail=&top;
static uint32_t code[65536];
static int ncode, legacy, wide, serial, return_label;
static uint32_t origin=0x300;
static int wide_app;
enum { MAX_SYMBOLS=4096 };
typedef struct { char *name; int address; } Label;
static Label labels[MAX_SYMBOLS];
static int nlabels;
typedef struct { int index; char *name; int addend; } Fixup;
static Fixup fixups[65536];
static int nfixups;
typedef struct { int index, length, flags; char *target; } Jump;
static Jump jumps[65536];
static int njumps;
typedef struct { char *name; int type,count,offset; Node *init; int base,readonly; } Variable;
static Variable globals[MAX_SYMBOLS], locals[MAX_SYMBOLS];
static int nglobals, nlocals;
static int global_defined[MAX_SYMBOLS];
static int global_visible[MAX_SYMBOLS];
static int global_linkage[MAX_SYMBOLS];
static int scope_base, function_body;
static const char *current_function;
typedef struct { char *name; Node *node; int nargs, reachable; } Function;
static Function functions[MAX_SYMBOLS];
static int nfunctions;
typedef struct { char *name; char *value; int length; } String;
static String strings[4096];
static int nstrings;
static int loop_step[64], loop_end[64], loop_depth;
static int break_targets[64], break_depth;
static int compiling_compare_helper;
static int current_return_type;

static void error(const char *fmt, ...) {
    va_list args; fprintf(stderr,"bobcc: line %d: ", tokens[at].line);
    va_start(args,fmt); vfprintf(stderr,fmt,args); va_end(args);
    fputc('\n',stderr); exit(1);
}
static void *allocate(size_t size) { void *p=calloc(1,size); if(!p) error("out of memory"); return p; }
static char *copy(const char *s) { char *p=allocate(strlen(s)+1); strcpy(p,s); return p; }
static char *named(const char *prefix,const char *name) {
    char *s=allocate(strlen(prefix)+strlen(name)+1); strcpy(s,prefix); strcat(s,name); return s;
}
static char *label_name(int id) { char s[32]; snprintf(s,sizeof(s),".L%d",id); return copy(s); }
static Node *node(int kind,Node *a,Node *b) { Node *n=allocate(sizeof(*n)); n->kind=kind;n->a=a;n->b=b;return n; }
static int escaped(const char **p) {
    int ch=(unsigned char)*(*p)++;
    if(ch!='\\') return ch;
    if(!**p)error("unterminated escape");
    ch=(unsigned char)*(*p)++;
    if(ch=='a') return '\a';
    if(ch=='f') return '\f';
    if(ch=='v') return '\v';
    if(ch=='n') return '\n';
    if(ch=='r') return '\r';
    if(ch=='t') return '\t';
    if(ch=='b') return '\b';
    if(ch>='0'&&ch<='7') {
        int value=ch-'0'; for(int i=0;i<2&&**p>='0'&&**p<='7';i++) value=value*8+(*(*p)++-'0'); return value;
    }
    if(ch=='x') { int value=0, digits=0; while(isxdigit((unsigned char)**p)) {
        int c=tolower((unsigned char)*(*p)++),digit=isdigit(c)?c-'0':c-'a'+10;
        if(value>(255-digit)/16)error("escape outside byte range");
        value=value*16+digit;digits++;
    } if(!digits) error("invalid escape");return value; }
    if(ch=='\\'||ch=='\''||ch=='"'||ch=='?') return ch;
    error("unsupported escape"); return 0;
}
static int valid_integer_suffix(const char *suffix,size_t length) {
    static const char *valid[]={"u","l","ul","lu","ll","ull","llu"};
    for(size_t i=0;i<sizeof(valid)/sizeof(valid[0]);i++)if(strlen(valid[i])==length) {
        size_t j=0;while(j<length && tolower((unsigned char)suffix[j])==valid[i][j])j++;
        if(j==length)return 1;
    }
    return length==0;
}
static void lex(const char *source) {
    const char *p=source; int line=1;
    while(*p) {
        if(isspace((unsigned char)*p)) { if(*p=='\n')line++;p++;continue; }
        if(p[0]=='/'&&p[1]=='/') { while(*p&&*p!='\n')p++;continue; }
        if(p[0]=='/'&&p[1]=='*') { p+=2;while(*p&&!(p[0]=='*'&&p[1]=='/')) { if(*p=='\n')line++;p++; }
            if(!*p)error("unterminated comment");
            p+=2;continue; }
        if(ntokens>=65534)error("too many tokens");
        Token *t=&tokens[ntokens++]; t->line=line;
        if(isalpha((unsigned char)*p)||*p=='_') {
            const char *start=p++;while(isalnum((unsigned char)*p)||*p=='_')p++;
            t->text=allocate((size_t)(p-start)+1);memcpy(t->text,start,(size_t)(p-start));t->type=T_ID;
        } else if(isdigit((unsigned char)*p)) {
            char *end; unsigned long long value=strtoull(p,&end,0);
            if(end==p||value>(wide?UINT32_MAX:65535u))error("integer outside target range");
            p=end;t->type=T_NUM;t->number=(int32_t)value;
            const char *suffix=p;int unsigned_suffix=0,long_suffix=0;size_t suffix_length=0;
            while(*p=='u'||*p=='U'||*p=='l'||*p=='L') {
                if(*p=='u'||*p=='U')unsigned_suffix++;
                else long_suffix++;
                suffix_length++;p++;
            }
            if(!valid_integer_suffix(suffix,suffix_length))error("invalid integer suffix");
            if(!wide && long_suffix)error("long integer suffix requires --wide");
            if(unsigned_suffix || (wide && value>INT32_MAX)){t->text=copy("u");unsigned_helpers=1;}
            if(isalpha((unsigned char)*p)) error("integer suffixes unsupported");
            if(isdigit((unsigned char)*p) || *p=='_')error("malformed integer literal");
        } else if(*p=='"'||*p=='\'') {
            int quote=*p++, count=0;size_t capacity=64;char *buffer=allocate(capacity);
            while(*p&&*p!=quote) { if(*p=='\n' || *p=='\r')error("newline in literal");int ch=escaped(&p);
                if(ch<0||ch>255)error("escape outside byte range");
                if((size_t)count+1>=capacity) {
                    capacity*=2;char *grown=realloc(buffer,capacity);
                    if(!grown)error("out of memory");
                    buffer=grown;
                }
                buffer[count++]=(char)ch; }
            if(*p!=quote)error("unterminated string");
            p++;buffer[count]=0;
            if(quote=='\'') { if(count!=1)error("character literal must contain one character");t->type=T_NUM;t->number=(unsigned char)buffer[0];free(buffer); }
            else { t->type=T_STR;t->number=count;t->text=buffer; }
        } else {
            const char *pairs[]={"<<=",">>=","==","!=","<=",">=","&&","||","++","--","<<",">>","+=","-=","*=","/=","%=","&=","|=","^="};
            int kinds[]={T_SHLEQ,T_SHREQ,T_EQ,T_NE,T_LE,T_GE,T_AND,T_OR,T_INC,T_DEC,T_SHL,T_SHR,T_ADDEQ,T_SUBEQ,T_MULEQ,T_DIVEQ,T_MODEQ,T_ANDEQ,T_OREQ,T_XOREQ};
            int matched=0;for(int i=0;i<20;i++) {size_t length=strlen(pairs[i]);if(!strncmp(p,pairs[i],length)) { t->type=kinds[i];p+=length;matched=1;break; }}
            if(!matched)t->type=(unsigned char)*p++;
        }
    }
    tokens[ntokens].line=line;
}
static int tok(void) { return tokens[at].type; }
static int is(const char *s) { return tok()==T_ID&&!strcmp(tokens[at].text,s); }
static int take(int t) { if(tok()!=t)return 0;at++;return 1; }
static void expect(int t) { if(!take(t))error("expected token %d, got %d",t,tok()); }
static char *identifier(void) { if(tok()!=T_ID)error("expected identifier");return tokens[at++].text; }
static int typedef_type(void) {
    if(tok()!=T_ID)return -1;
    for(int i=parser_binding_count-1;i>=0;i--)if(!strcmp(parser_bindings[i].name,tokens[at].text))
        return parser_bindings[i].alias==1?parser_bindings[i].type:-1;
    return -1;
}
static int parse_enum_type(void);
static int type_start(void) { return is("int")||is("long")||is("char")||is("void")||is("_Bool")||is("signed")||is("unsigned")||is("const")||is("enum")||typedef_type()>=0; }
static int parse_base_type(void) {
    int qualified=0;parsed_const_alias_object=0;
    while(is("const")){at++;qualified=1;}
    if(is("int") || is("long") || is("signed") || is("unsigned")) {
        int seen_int=0,long_count=0,sign=0;
        while(is("int") || is("long") || is("signed") || is("unsigned") || is("const")) {
            if(is("const")){at++;qualified=1;continue;}
            if(is("int")){if(seen_int)error("duplicate int specifier");seen_int=1;}
            else if(is("long")){if(++long_count>2)error("too many long specifiers");}
            else {if(sign)error("duplicate or conflicting sign specifier");sign=is("unsigned")?2:1;}
            at++;
        }
        if(long_count && !wide)error("long integer types require --wide");
        if(is("char") || is("void") || is("_Bool"))error("unsupported integer specifier combination");
        parsed_const_base=qualified;
        if(sign==2){unsigned_helpers=1;return 6;}
        return 0;
    }
    if(is("enum")){int result=parse_enum_type();while(is("const")){at++;qualified=1;}parsed_const_base=qualified;return result;}
    int alias=typedef_type();if(alias>=0){
        int explicit_qualified=qualified;
        for(int i=parser_binding_count-1;i>=0;i--)if(!strcmp(parser_bindings[i].name,tokens[at].text)){
            if(parser_bindings[i].alias==1){
                qualified|=parser_bindings[i].readonly;
                parsed_const_alias_object=parser_bindings[i].object_readonly;
            }
            break;
        }
        at++;while(is("const")){at++;qualified=1;explicit_qualified=1;}
        if(explicit_qualified && (is_pointer(alias)||is_array(alias)))parsed_const_alias_object=1;
        parsed_const_base=qualified;return alias;
    }
    if(!is("int")&&!is("char")&&!is("void")&&!is("_Bool"))error("unsupported base type");
    int result=is("void")?3:is("char")?4:is("_Bool")?5:0;at++;while(is("const")){at++;qualified=1;}
    parsed_const_base=qualified;
    return result;
}
static int parse_declarator(int base,char **name,int abstract);
static int parse_type(void) {
    char *name=NULL;int type=parse_declarator(parse_base_type(),&name,1);
    if(name && *name)error("identifier in type name");
    return type;
}
static Node *expression(int minimum);
static Node *statement(void);
static Node *primary(void) {
    Node *n;
    if(is("sizeof")) {
        at++;
        if(take('(')) {
            if(type_start()) {
                int type=parse_type();if(type==3)error("sizeof void is invalid");expect(')');
                n=node(N_NUM,NULL,NULL);n->type=6;n->value=type_size(type);
            } else {n=node(N_SIZEOF,expression(0),NULL);n->type=6;expect(')');}
        } else {n=node(N_SIZEOF,expression(13),NULL);n->type=6;}
    }
    else if(tok()==T_NUM) { n=node(N_NUM,NULL,NULL);n->type=tokens[at].text?6:0;n->value=tokens[at++].number; }
    else if(tok()==T_STR) { n=node(N_STR,NULL,NULL);n->name=tokens[at].text;n->value=tokens[at++].number;
        if(tok()==T_STR) {
            size_t length=(size_t)n->value;int end=at;
            while(tokens[end].type==T_STR){length+=(size_t)tokens[end].number;end++;}
            char *joined=allocate(length+1);size_t position=(size_t)n->value;
            memcpy(joined,n->name,position);
            while(at<end){size_t piece=(size_t)tokens[at].number;memcpy(joined+position,tokens[at++].text,piece);position+=piece;}
            n->name=joined;n->value=(int)length;
        } }
    else if(tok()==T_ID) {
        n=node(N_ID,NULL,NULL);n->name=identifier();
        for(int i=parser_binding_count-1;i>=0;i--)if(!strcmp(parser_bindings[i].name,n->name)) {
            if(parser_bindings[i].alias==3){n->kind=N_NUM;n->value=parser_bindings[i].type;}
            break;
        }
    }
    else if(take('(')) {
        if(type_start()) { int type=parse_type();if(is_array(type))error("cast to array type");expect(')');n=node(N_CAST,expression(13),NULL);n->type=type; }
        else { n=expression(0);expect(')'); }
    } else if(tok()=='-'||tok()=='+'||tok()=='!'||tok()=='~'||tok()=='&'||tok()=='*'||tok()==T_INC||tok()==T_DEC) {
        int op=tokens[at++].type;n=node(N_UNARY,expression(13),NULL);n->op=op;
    } else { error("expected expression");return NULL; }
    while(1) {
        if(take('[')) { n=node(N_INDEX,n,expression(0));expect(']'); }
        else if(take('(')) {
            Node *call=node(N_CALL,NULL,NULL);int indirect=0;
            if(n->kind==N_ID) {
                for(int i=parser_binding_count-1;i>=0;i--)if(!strcmp(parser_bindings[i].name,n->name)) {
                    indirect=parser_bindings[i].alias==0 && is_function_pointer(parser_bindings[i].type);break;
                }
            } else indirect=is_function_pointer(decay_type(expression_type(n)));
            if(indirect)call->d=n;else if(n->kind==N_ID)call->name=n->name;
            else error("called expression is not a function pointer");
            Node **tail=&call->a;if(tok()!=')')do { *tail=expression(1);tail=&(*tail)->next; }while(take(','));
            expect(')');n=call;
        } else if(tok()==T_INC||tok()==T_DEC) { int op=tokens[at++].type;n=node(N_UNARY,n,NULL);n->op=op;n->value=1; }
        else break;
    }
    return n;
}
static int precedence(int op) {
    switch(op) {
        case '=':case T_ADDEQ:case T_SUBEQ:case T_MULEQ:case T_DIVEQ:case T_MODEQ:case T_ANDEQ:case T_OREQ:case T_XOREQ:case T_SHLEQ:case T_SHREQ:return 1;
        case ',':return 0;
        case T_OR:return 3;case T_AND:return 4;case '|':return 5;case '^':return 6;case '&':return 7;
        case T_EQ:case T_NE:return 8;case '<':case '>':case T_LE:case T_GE:return 9;
        case T_SHL:case T_SHR:return 10;case '+':case '-':return 11;case '*':case '/':case '%':return 12;
        default:return -1;
    }
}
static Node *expression(int minimum) {
    if(++expression_depth>256)error("expression nesting exceeds 256 levels");
    Node *left=primary();int op,level;
    while(1) {
        if(tok()=='?' && minimum<=2) {
            at++;Node *middle=expression(0);expect(':');
            Node *n=node(N_CONDITIONAL,left,middle);n->c=expression(2);left=n;continue;
        }
        level=precedence(op=tok());if(level<minimum || level<0)break;
        at++;Node *right=expression(level+(level==1?0:1));Node *n=node(level==1?N_ASSIGN:N_BINARY,left,right);n->op=op;left=n;
    }
    expression_depth--;return left;
}
static int constant(Node *n) {
    if(n->kind==N_NUM)return wide?(int32_t)(uint32_t)n->value:(int16_t)n->value;
    if(n->kind==N_SIZEOF)return type_size(expression_type(n->a));
    if(n->kind==N_CAST && n->type!=3){expression_type(n);
        if(n->type==5 && is_pointer(decay_type(expression_type(n->a)))) {
            const char *target=NULL;int offset=0;
            if(global_address(n->a,&target,&offset))return 1;
        }
        int value=constant(n->a);return n->type==5?value!=0:value;}
    if(n->kind==N_CONDITIONAL){expression_type(n);return constant(n->a)?constant(n->b):constant(n->c);}
    if(n->kind==N_UNARY) {
        expression_type(n);
        int a=constant(n->a);
        if(n->op=='-')return wide?(int32_t)(0u-(uint32_t)a):(int16_t)-a;
        if(n->op=='+')return a;
        if(n->op=='!')return !a;
        if(n->op=='~')return wide?(int32_t)~(uint32_t)a:(int16_t)~a;
    }
    if(n->kind==N_BINARY) {
        expression_type(n);
        int a=constant(n->a);
        if(n->op==T_AND && !a)return 0;
        if(n->op==T_OR && a)return 1;
        int b=constant(n->b);
        int left=decay_type(expression_type(n->a)),right=decay_type(expression_type(n->b));
        if(n->op=='+' || n->op=='-') {
            if(is_pointer(left) && is_pointer(right))
                return (int16_t)(a-b)/type_size(pointer_types[left-16].base);
            if(is_pointer(left))return (int16_t)(n->op=='+'?a+b*type_size(pointer_types[left-16].base):a-b*type_size(pointer_types[left-16].base));
            if(is_pointer(right))return (int16_t)(b+a*type_size(pointer_types[right-16].base));
        }
        int unsigned_result=expression_type(n->a)==6 || expression_type(n->b)==6;
        if(unsigned_result && (n->op=='/' || n->op=='%' || n->op=='<' || n->op=='>' || n->op==T_LE || n->op==T_GE)) {
            uint32_t x=wide?(uint32_t)a:(uint16_t)a,y=wide?(uint32_t)b:(uint16_t)b;
            if((n->op=='/' || n->op=='%') && !y)error("division by zero in constant expression");
            if(n->op=='/')return wide?(int32_t)(x/y):(int16_t)(x/y);
            if(n->op=='%')return wide?(int32_t)(x%y):(int16_t)(x%y);
            if(n->op=='<')return x<y;
            if(n->op=='>')return x>y;
            return n->op==T_LE?x<=y:x>=y;
        }
        switch(n->op) {
            case '+':return wide?(int32_t)((uint32_t)a+(uint32_t)b):(int16_t)(a+b);case '-':return wide?(int32_t)((uint32_t)a-(uint32_t)b):(int16_t)(a-b);
            case '*':return wide?(int32_t)((uint32_t)a*(uint32_t)b):(int16_t)(a*b);
            case '/':case '%':if(!b)error("division by zero in constant expression");return wide?(n->op=='/'?a/b:a%b):(int16_t)(n->op=='/'?a/b:a%b);
            case '&':return a&b;case '|':return a|b;case '^':return a^b;
            case T_EQ:return a==b;case T_NE:return a!=b;
            case '<':return a<b;case '>':return a>b;case T_LE:return a<=b;case T_GE:return a>=b;
            case T_AND:return !!b;case T_OR:return !!b;
            case T_SHL:case T_SHR:
                if(b<0 || b>=(wide?32:16))error("invalid constant shift count");
                if(n->op==T_SHL)return wide?(int32_t)((uint32_t)a<<b):(int16_t)((uint32_t)(uint16_t)a<<b);
                if(expression_type(n->a)==6)return wide?(int32_t)((uint32_t)a>>b):(int16_t)((uint16_t)a>>b);
                if(!b)return a;
                return (int16_t)(((uint16_t)a>>b)|(a<0?(0xffffu<<(16-b)):0));
            default:break;
        }
    }
    error("initializer/array size must be constant");return 0;
}
static int constant_integer_syntax(Node *n) {
    if(n->kind==N_NUM || n->kind==N_SIZEOF)return 1;
    if(n->kind==N_CAST) {
        if(is_pointer(n->type) || n->type==3)return 0;
        int source=decay_type(expression_type(n->a));
        return source!=3 && !is_pointer(source) && constant_integer_syntax(n->a);
    }
    if(n->kind==N_UNARY) {
        if(n->op!='+' && n->op!='-' && n->op!='!' && n->op!='~')return 0;
        int operand=decay_type(expression_type(n->a));
        return operand!=3 && !is_pointer(operand) && constant_integer_syntax(n->a);
    }
    if(n->kind==N_BINARY) {
        if(n->op==',')return 0;
        int left=decay_type(expression_type(n->a)),right=decay_type(expression_type(n->b));
        return left!=3 && right!=3 && !is_pointer(left) && !is_pointer(right) &&
            constant_integer_syntax(n->a) && constant_integer_syntax(n->b);
    }
    if(n->kind==N_CONDITIONAL) {
        int condition=decay_type(expression_type(n->a));
        int yes=decay_type(expression_type(n->b)),no=decay_type(expression_type(n->c));
        return condition!=3 && !is_pointer(condition) && yes!=3 && no!=3 && !is_pointer(yes) && !is_pointer(no) &&
            constant_integer_syntax(n->a) && constant_integer_syntax(n->b) && constant_integer_syntax(n->c);
    }
    return 0;
}
static int integer_constant(Node *n) {
    int type=decay_type(expression_type(n));
    if(type==3 || is_pointer(type) || !constant_integer_syntax(n))error("integer constant expression required");
    return constant(n);
}
static int parse_enum_type(void) {
    at++;char *tag=tok()==T_ID?identifier():NULL;
    if(tok()!='{') {
        if(tag)for(int i=enum_tag_count-1;i>=0;i--)if(!strcmp(enum_tags[i].name,tag))return enum_tags[i].type;
        error("enum tag must already be defined");
    }
    int type=derived_type(3,0,++enum_serial,0);
    if(tag) {
        for(int i=enum_scope_base;i<enum_tag_count;i++)if(!strcmp(enum_tags[i].name,tag))error("duplicate enum tag %s",tag);
        if(enum_tag_count==4096)error("too many enum tags");
        enum_tags[enum_tag_count++]=(EnumTag){tag,type};
    }
    expect('{');int next=0;
    if(tok()=='}')error("enum requires enumerators");
    do {
        char *name=identifier();int value;
        if(take('=')) {
            Node *initializer=expression(1);int type=decay_type(expression_type(initializer));
            value=integer_constant(initializer);
            if(type==6 && value<0)error("enum value exceeds signed int range");
        }
        else {if(next>32767)error("enum value exceeds signed int range");value=next;}
        for(int i=parser_scope_base;i<parser_binding_count;i++)if(!strcmp(parser_bindings[i].name,name))error("conflicting enumerator %s",name);
        if(parser_binding_count==4096)error("too many active parser bindings");
        parser_bindings[parser_binding_count++]=(ParserBinding){name,value,3,0,0};
        next=value+1;
    }while(take(',') && tok()!='}');
    expect('}');return type;
}
static int parse_function_signature(void) {
    int parameters[32],count=0,unspecified=0;
    expect('(');
    if(tok()==')') {unspecified=1;at++;}
    else if(is("void") && tokens[at+1].type==')') {at+=2;}
    else {
        do {
            if(count==32)error("function pointer has more than 32 parameters");
            int base=parse_base_type();char *parameter_name=NULL;
            int type=parse_declarator(base,&parameter_name,1);
            if(is_array(type))type=pointer_to(pointer_types[type-16].base);
            else if(is_function(type))type=pointer_to(type);
            if(type==3)error("void must be the only unnamed parameter");
            parameters[count++]=type;
        } while(take(','));
        expect(')');
    }
    return function_signature(parameters,count,unspecified);
}
typedef struct {int kind,count,readonly;} DeclaratorPart;
static void declarator_parts(DeclaratorPart *parts,int *length,char **name,int abstract,int depth) {
    if(depth>32)error("declarator nesting exceeds 32 levels");
    int pointers=0,pointer_const[64]={0},grouped=0;
    while(take('*')) {int index=pointers++;while(is("const")){at++;pointer_const[index]=1;}if(pointers>64)error("too many pointer levels");}
    if(tok()==T_ID) {*name=identifier();}
    else if(take('(')) {grouped=1;declarator_parts(parts,length,name,abstract,depth+1);expect(')');}
    else if(!abstract)error("expected declarator name");
    while(1) {
        if(take('[')) {
            int empty=tok()==']',bound=empty?0:integer_constant(expression(1));expect(']');
            if(!empty && (bound<1 || bound>4096))error("array length must be 1..4096");
            if(*length==64)error("declarator exceeds 64 derived types");
            parts[(*length)++]=(DeclaratorPart){2,bound,0};
        } else if(grouped && tok()=='(') {
            int signature=parse_function_signature();
            if(*length==64)error("declarator exceeds 64 derived types");
            parts[(*length)++]=(DeclaratorPart){4,signature,0};grouped=0;
        } else break;
    }
    while(pointers--) {
        if(*length==64)error("declarator exceeds 64 derived types");
        parts[(*length)++]=(DeclaratorPart){1,1,pointer_const[pointers]};
    }
}
static int parse_declarator(int base,char **name,int abstract) {
    DeclaratorPart parts[64];int length=0,dimensions=0;*name=NULL;
    int base_const=parsed_const_base && !is_pointer(base) && !is_array(base);
    declarator_parts(parts,&length,name,abstract,0);
    parsed_const_object=length && parts[0].kind==1 && parts[0].readonly;
    while(length--) {
        if(parts[length].kind==1) {base=base_const?pointer_to_const(base):pointer_to(base);base_const=0;}
        else if(parts[length].kind==4) {
            if(is_array(base) || is_function(base))error("function cannot return an array or function");
            base=function_type(base,parts[length].count);
        } else {
            if(++dimensions>16)error("too many array dimensions");
            if(base==3)error("array of void");
            if(is_function(base))error("array of functions");
            if(is_array(base) && !pointer_types[base-16].count)error("incomplete array element type");
            base=base_const?array_of_const(base,parts[length].count):array_of(base,parts[length].count);base_const=0;
        }
    }
    if(!*name)*name=copy("");
    return base;
}
static void initializer_append(Node ***tail,Node *value) {
    **tail=value;*tail=&value->next;
}
/* Flatten aggregate braces into storage order, preserving omitted subobjects. */
static int initializer_depth;
static int initializer_list(int element,int count,int aggregate,Node ***tail) {
    if(++initializer_depth>64)error("initializer nesting exceeds 64 levels");
    expect('{');
    int stride=type_size(element),position=0,total=count?count*stride:0,extent=0;
    if(stride>4096 || total>4096)error("initializer storage exceeds 4096 words");
    Node *values[4096]={0};
    while(tok()!='}') {
        int sub=element;
        int designated=tok()=='[';
        if(designated) {
            if(!aggregate)error("array designator requires an array");
            int current=array_of(element,count?count:4096/stride);position=0;
            do {
                if(!is_array(current))error("too many array designators");
                expect('[');int index=integer_constant(expression(1));expect(']');
                if(index<0 || index>=pointer_types[current-16].count)error("array designator out of bounds");
                current=pointer_types[current-16].base;position+=index*type_size(current);
            } while(tok()=='[');
            expect('=');sub=current;
        }
        if(position>=4096 || (total && position>=total))error("too many array initializers");
        /* Brace elision can leave us partway through a nested array. */
        if(!designated)while(is_array(sub) && position%type_size(sub))sub=pointer_types[sub-16].base;
        Node *items=NULL,**items_tail=&items;int length=0;
        if(tok()=='{') {
            if(is_array(sub)) {
                length=initializer_list(pointer_types[sub-16].base,pointer_types[sub-16].count,1,&items_tail);
                if(length!=type_size(sub))error("invalid aggregate initializer");
            } else length=initializer_list(sub,1,0,&items_tail);
        } else {
            Node *value=expression(1);
            if(value->kind==N_STR && ((is_array(sub) && pointer_types[sub-16].base==4) ||
                                      (!designated && aggregate && element==4 && position==0))) {
                length=is_array(sub)?pointer_types[sub-16].count:(count?count:value->value+1);
                if(value->value>length)error("character array too small for string");
                for(int i=0;i<length;i++) {
                    Node *item=node(N_NUM,NULL,NULL);
                    item->value=i<value->value?(unsigned char)value->name[i]:0;
                    initializer_append(&items_tail,item);
                }
            } else {initializer_append(&items_tail,value);length=1;}
        }
        if(position+length>4096 || (total && position+length>total))error("too many array initializers");
        while(items) {Node *next=items->next;items->next=NULL;values[position++]=items;items=next;}
        if(position>extent)extent=position;
        if(!take(','))break;
    }
    expect('}');
    if(total)extent=total;
    for(int i=0;i<extent;i++)initializer_append(tail,values[i]?values[i]:node(N_NUM,NULL,NULL));
    initializer_depth--;
    return extent;
}
static Node *declaration(int type,char *name) {
    Node *n=node(N_DECL,NULL,NULL);n->name=name;n->type=type;n->count=1;n->op=type;
    n->readonly=parsed_const_object || parsed_const_alias_object ||
        (is_array(type)?pointer_types[type-16].qualifiers:(parsed_const_base && !is_pointer(type)));
    n->storage=parsing_external;
    int inferred=0,braced=0;
    if(is_array(type)){n->type=2;n->count=pointer_types[type-16].count;n->op=pointer_types[type-16].base;inferred=!n->count;}
    if(type==3)error("void variable");
    int binding=-1;
    if(*name) {
        for(int i=parser_scope_base;i<parser_binding_count;i++)if((parser_bindings[i].alias==1 || parser_bindings[i].alias==3) && !strcmp(parser_bindings[i].name,name))error("object conflicts with type/constant %s",name);
        if(parser_binding_count==4096)error("too many active parser bindings");
        binding=parser_binding_count++;
        parser_bindings[binding]=(ParserBinding){name,type,0,n->readonly,0};
    }
    if(parsing_parameter) {
        if(tok()=='=')error("parameter cannot have an initializer");
        if(n->type==2){n->type=pointer_to(n->op);n->count=1;}
        if(binding>=0)parser_bindings[binding].type=n->type;
        return n;
    }
    if(take('=')) {
        if(parsing_external==1 && !parsing_file_object)error("block extern cannot have initializer");
        if(tok()=='{') { Node **tail=&n->a;braced=1;initializer_list(n->type==2?n->op:n->type,n->type==2?n->count:1,n->type==2,&tail); }
        else n->a=expression(1);
    }
    if(n->type==2 && n->a && n->a->kind==N_STR && !braced) {
        Node *string=n->a;
        if(n->op!=4)error("string initializer requires a character array");
        if(inferred)n->count=string->value+1;
        if(n->count<string->value)error("character array too small for string");
        Node **tail=&n->a;int length=string->value+1;if(length>n->count)length=n->count;
        for(int i=0;i<length;i++){*tail=node(N_NUM,NULL,NULL);(*tail)->value=(unsigned char)string->name[i];tail=&(*tail)->next;}
    } else if(inferred) {
        int elements=0;for(Node *value=n->a;value;value=value->next)elements++;
        int stride=type_size(n->op);n->count=(elements+stride-1)/stride;
    }
    if(inferred && (!n->a || n->count<1 || n->count>4096) && !((parsing_file_object || parsing_external==1) && !n->a))error("inferred array needs 1..4096 initialized elements");
    if(n->type==2 && n->count && type_size(array_of(n->op,n->count))>4096)error("array storage exceeds 4096 words");
    if(binding>=0) {
        int actual=n->type==2?(n->readonly?array_of_const(n->op,n->count):array_of(n->op,n->count)):n->type;
        if(parsing_file_object)for(int i=binding-1;i>=0;i--)if(!strcmp(parser_bindings[i].name,name)) {
            int merged=composite_type(parser_bindings[i].type,actual);
            if(merged<0)error("conflicting object type for %s",name);
            actual=merged;break;
        }
        parser_bindings[binding].type=actual;
    }
    return n;
}
static Node *declaration_group(int base) {
    Node *group=node(N_DECL_GROUP,NULL,NULL);Node **tail=&group->a;int qualified=parsed_const_base,alias_object=parsed_const_alias_object;
    do {
        char *name;int type=parse_declarator(base,&name,0);
        parsed_const_base=qualified;parsed_const_alias_object=alias_object;
        *tail=declaration(type,name);tail=&(*tail)->next;
    } while(take(','));
    return group;
}
static Node *statement(void) {
    if(is("typedef")) {
        at++;int base=parse_base_type(),qualified=parsed_const_base;
        do {
            char *name;int type=parse_declarator(base,&name,0);
            for(int i=parser_scope_base;i<parser_binding_count;i++)if(!strcmp(parser_bindings[i].name,name)) {
                if(parser_bindings[i].alias!=1 || parser_bindings[i].type!=type)error("conflicting typedef %s",name);
            }
            if(parser_binding_count==4096)error("too many active parser bindings");
            parser_bindings[parser_binding_count++]=(ParserBinding){name,type,1,qualified,parsed_const_object};
        }while(take(','));
        expect(';');return node(N_EMPTY,NULL,NULL);
    }
    if(is("extern") || is("static")) {
        parsing_external=is("extern")?1:2;at++;int base=parse_base_type();Node *n=declaration_group(base);
        expect(';');parsing_external=0;return n;
    }
    if(is("switch")) {at++;expect('(');Node *n=node(N_SWITCH,expression(0),NULL);expect(')');
        if(parsing_switch_depth==64)error("switch nesting exceeds 64 levels");
        parsing_switch_depth++;n->b=statement();parsing_switch_depth--;return n;}
    if(is("case") || is("default")) {
        if(!parsing_switch_depth)error("case/default outside switch");
        Node *n=node(N_CASE,NULL,NULL);n->type=is("default");at++;
        if(!n->type){Node *value=expression(1);n->count=integer_constant(value);}
        expect(':');n->b=statement();return n;
    }
    if(tok()==T_ID && tokens[at+1].type==':') {
        Node *n=node(N_LABEL,NULL,NULL);n->name=identifier();expect(':');n->b=statement();return n;
    }
    if(is("goto")) {at++;Node *n=node(N_GOTO,NULL,NULL);n->name=identifier();expect(';');return n;}
    if(take('{')) { int saved=parser_binding_count,base=parser_scope_base,tags=enum_tag_count,tagbase=enum_scope_base;Node *n=node(N_BLOCK,NULL,NULL);Node **tail=&n->a;
        if(parser_body_scope<0)enum_scope_base=tags;
        parser_scope_base=parser_body_scope>=0?parser_body_scope:saved;parser_body_scope=-1;
        while(tok()&&tok()!='}') { *tail=statement();tail=&(*tail)->next; }expect('}');parser_binding_count=saved;parser_scope_base=base;enum_tag_count=tags;enum_scope_base=tagbase;return n; }
    if(is("if")) { at++;expect('(');Node *n=node(N_IF,expression(0),NULL);expect(')');n->b=statement();if(is("else")){at++;n->c=statement();}return n; }
    if(is("while")) { at++;expect('(');Node *n=node(N_WHILE,expression(0),NULL);expect(')');n->b=statement();return n; }
    if(is("do")) {
        at++;Node *n=node(N_DO,NULL,NULL);n->b=statement();
        if(!is("while"))error("expected while after do body");
        at++;expect('(');n->a=expression(0);expect(')');expect(';');return n;
    }
    if(is("for")) {
        int saved=parser_binding_count,base=parser_scope_base,tags=enum_tag_count,tagbase=enum_scope_base;parser_scope_base=saved;enum_scope_base=tags;
        at++;expect('(');Node *n=node(N_FOR,NULL,NULL);
        if(type_start()) { int base=parse_base_type();n->a=declaration_group(base); }
        else if(tok()!=';')n->a=expression(0);
        expect(';');
        if(tok()!=';')n->b=expression(0);
        expect(';');
        if(tok()!=')')n->c=expression(0);
        expect(')');n->d=statement();parser_binding_count=saved;parser_scope_base=base;enum_tag_count=tags;enum_scope_base=tagbase;return n;
    }
    if(is("return")) { at++;Node *n=node(N_RETURN,NULL,NULL);if(tok()!=';')n->a=expression(0);expect(';');return n; }
    if(is("break")||is("continue")) { int kind=is("break")?N_BREAK:N_CONTINUE;at++;expect(';');return node(kind,NULL,NULL); }
    if(type_start()) {int base=parse_base_type();if(is_enum(base) && take(';'))return node(N_EMPTY,NULL,NULL);Node *n=declaration_group(base);expect(';');return n;}
    if(take(';'))return node(N_EMPTY,NULL,NULL);
    Node *n=expression(0);expect(';');return n;
}
static void parse(void) {
    parsing_types=1;
    while(tok()) {
        if(is("typedef")){statement();continue;}
        int storage=is("extern")?1:is("static")?2:0;if(storage)at++;
        int external=storage==1;
        int base=parse_base_type(),base_const=parsed_const_base,base_alias_object=parsed_const_alias_object;if(is_enum(base) && !storage && take(';'))continue;
        char *name;int type=parse_declarator(base,&name,0);parsed_const_base=base_const;parsed_const_alias_object=base_alias_object;Node *n;
        if(take('(')) {
            for(int i=parser_scope_base;i<parser_binding_count;i++)if((parser_bindings[i].alias==1 || parser_bindings[i].alias==3) && !strcmp(parser_bindings[i].name,name))error("function conflicts with type/constant %s",name);
            if(parser_binding_count==4096)error("too many active parser bindings");
            parser_bindings[parser_binding_count++]=(ParserBinding){name,0,2,0,0};
            int function_binding=parser_binding_count-1;
            int saved=parser_binding_count,scope=parser_scope_base,tags=enum_tag_count,tagbase=enum_scope_base;parser_scope_base=saved;enum_scope_base=tags;
            if(is_array(type))error("function cannot return an array");
            n=node(N_FUNCTION,NULL,NULL);n->name=name;n->type=type;n->storage=storage;Node **tail=&n->a;
            n->op=tok()==')';
            if(is("void")&&tokens[at+1].type==')')at++;
            else if(tok()!=')')do { char *p;int parameter_base=parse_base_type(),parameter_const=parsed_const_base,parameter_alias_object=parsed_const_alias_object;
                int t=parse_declarator(parameter_base,&p,1);parsed_const_base=parameter_const;parsed_const_alias_object=parameter_alias_object;
                if(t==3 && !*p && tail==&n->a && tok()==')')break;
                parsing_parameter=1;*tail=declaration(t,p);parsing_parameter=0;tail=&(*tail)->next; }while(take(','));
            expect(')');
            int parameters[32],parameter_count=0;
            for(Node *parameter=n->a;parameter;parameter=parameter->next) {
                if(parameter_count==32)error("function has more than 32 parameters");
                parameters[parameter_count++]=parameter->type;
            }
            n->count=function_signature(parameters,parameter_count,n->op);
            parser_bindings[function_binding].type=function_type(n->type,n->count);
            if(take(';'))n->kind=N_PROTOTYPE;
            else {parser_body_scope=saved;n->b=statement();if(n->b->kind!=N_BLOCK)error("expected function body");}
            parser_binding_count=saved;parser_scope_base=scope;enum_tag_count=tags;enum_scope_base=tagbase;
        } else {
            parsing_file_object=1;
            n=declaration(type,name);n->value=external;n->storage=storage;*top_tail=n;top_tail=&n->next;
            while(take(',')) {
                type=parse_declarator(base,&name,0);parsed_const_base=base_const;parsed_const_alias_object=base_alias_object;n=declaration(type,name);n->value=external;n->storage=storage;*top_tail=n;top_tail=&n->next;
            }
            expect(';');parsing_file_object=0;continue;
        }
        *top_tail=n;top_tail=&n->next;
    }
    parsing_types=0;
}
static void emit(int value) { if(ncode>=65536)error("code too large");code[ncode++]=(uint32_t)value; }
static void emit_relocation(const char *name,int offset) {
    if(ncode>=65536 || nfixups>=65536)error("code/relocation limit exceeded");
    fixups[nfixups++]=(Fixup){ncode,copy(name),offset};emit(0);
}
static void define(const char *name) {
    for(int i=0;i<nlabels;i++)if(!strcmp(labels[i].name,name))error("duplicate label %s",name);
    if(nlabels==MAX_SYMBOLS)error("too many labels");
    labels[nlabels++]=(Label){copy(name),origin+ncode};
}
static void mark(int id) { define(label_name(id)); }
static void literal(int reg,int value,const char *name) {
    if(!name)value=wide?(int32_t)(uint32_t)value:(int16_t)(uint16_t)value;
    if(!name && value>=-64 && value<=63) {
        emit(0x2180|reg<<9);
        if(value)emit(0x1180|reg<<9|(value&127));
        return;
    }
    emit(0x4001|reg<<9);emit(0xae01);
    if(name)emit_relocation(name,0);else emit(value);
}
static void move(int dst,int src) { emit(0x1080|dst<<9|src<<4); }
static void addi(int reg,int value) {
    if(value>=-64&&value<=63)emit(0x1180|reg<<9|(value&127));
    else { literal(2,value,NULL);emit(0x1000|reg<<9|reg<<4|2<<1); }
}
static void push(int reg) { addi(6,-1);emit(0x9000|reg<<9|6<<6); }
static void pop(int reg) { emit(0x6000|reg<<9|6<<6);addi(6,1); }
static Jump *add_jump(int index,int length,int flags,const char *target) {
    if(njumps>=65536)error("too many jumps");
    jumps[njumps]=(Jump){index,length,flags,copy(target)};
    return &jumps[njumps++];
}
static void jump_name(const char *name) {
    add_jump(ncode,4,7,name);
    literal(3,0,name);emit(0xb600);
}
static void jump(int id) {jump_name(label_name(id));}
static char *user_label(const char *name) {
    size_t size=strlen(current_function)+strlen(name)+16;char *label=allocate(size);
    snprintf(label,size,"user.%s.%s",current_function,name);return label;
}
static void branch(int flags,int id) {
    int start=ncode;
    emit(0xa000|((flags^7)<<9)|4);jump(id);
    jumps[njumps-1].index=start;jumps[njumps-1].length=5;jumps[njumps-1].flags=flags;
}
static void call(const char *name) {
    add_jump(ncode,4,8,name);
    literal(3,0,name);emit(legacy?0xcb00:0xc300);
}
static int symbol_address(const char *name) {
    for(int i=0;i<nlabels;i++)if(!strcmp(name,labels[i].name))return labels[i].address;
    error("unresolved symbol %s",name);return 0;
}
static void compact_jumps(void) {
    int changed;
    do {
        changed=0;
        for(int i=0;i<njumps;i++) {
            Jump *j=&jumps[i];if(j->length==1)continue;
            int start=j->index,old=j->length,removed=old-1;
            int target=symbol_address(j->target)-origin;
            if(target>start)target-=removed;
            int offset=target-start-1;
            int range=j->flags==8?1024:256;
            if(offset < -range || offset >= range)continue;
            memmove(code+start+1,code+start+old,(size_t)(ncode-start-old)*sizeof(*code));
            ncode-=removed;
            for(int k=0;k<nlabels;k++)if((uint32_t)labels[k].address>origin+(uint32_t)start)labels[k].address-=removed;
            int out=0;
            for(int k=0;k<nfixups;k++) {
                Fixup f=fixups[k];
                if(f.index>=start && f.index<start+old)continue;
                if(f.index>start)f.index-=removed;
                fixups[out++]=f;
            }
            nfixups=out;j->length=1;
            for(int k=0;k<njumps;k++)if(jumps[k].index>start)jumps[k].index-=removed;
            changed=1;
        }
    } while(changed);
    for(int i=0;i<njumps;i++)if(jumps[i].length==1) {
        Jump *j=&jumps[i];int offset=symbol_address(j->target)-origin-j->index-1;
        int range=j->flags==8?1024:256;
        if(offset < -range || offset >= range)error("relative transfer out of range");
        if(j->flags==8)code[j->index]=(uint32_t)((legacy?0xc000:0xc800)|(offset&2047));
        else code[j->index]=(uint32_t)(0xa000|j->flags<<9|(offset&511));
    }
}
static void negate(int reg) { emit(0x3100|reg<<9);addi(reg,1); }
static int fresh(void) {
    if(serial>=65535)error("generated label limit exceeded");
    return ++serial;
}
static Variable *variable_lookup(const char *name,int *global) {
    for(int i=nlocals-1;i>=0;i--)if(!strcmp(locals[i].name,name)){
        if(locals[i].offset>=65536){*global=1;return &globals[locals[i].offset-65536];}
        *global=0;return &locals[i];
    }
    for(int i=0;i<nglobals;i++)if((global_visible[i] || !strncmp(name,"static.",7)) && !strcmp(globals[i].name,name)){*global=1;return &globals[i];}
    return NULL;
}
static Variable *variable(const char *name,int *global) {
    Variable *v=variable_lookup(name,global);
    if(!v)error("unknown variable %s",name);
    return v;
}
static Function *function(const char *name) {
    for(int i=0;i<nfunctions;i++)if(!strcmp(functions[i].name,name))return &functions[i];
    error("undefined function %s",name);return NULL;
}
static void expr(Node *n);
static void address(Node *n) {
    if(n->kind==N_ID) { int global;Variable *v=variable(n->name,&global);
        if(global)literal(0,0,named("g.",v->name));else { move(0,5);addi(0,v->offset); } }
    else if(n->kind==N_STR)expr(n);
    else if(n->kind==N_INDEX) {
        int a=decay_type(expression_type(n->a)),b=decay_type(expression_type(n->b));
        if(!is_pointer(a) && !is_pointer(b))error("subscript requires pointer/array");
        expr(n->a);push(0);expr(n->b);pop(1);binary_typed('+',a,b);
    }
    else if(n->kind==N_UNARY&&n->op=='*')expr(n->a);
    else error("expected assignable variable or pointer");
}
static void compare(int op) {
    int yes=fresh(),end=fresh();
    if(op==T_EQ||op==T_NE) { negate(0);emit(0x1010);branch(op==T_EQ?2:5,yes); }
    else {
        int negative=fresh(),same=fresh(),opposite=fresh(),nonnegative=fresh();
        move(1,1);branch(4,negative);move(0,0);branch(4,opposite);jump(same);
        mark(negative);move(0,0);branch(3,nonnegative);jump(same);mark(opposite);
        if(op=='>'||op==T_GE)jump(yes);else {literal(0,0,NULL);jump(end);}
        mark(nonnegative);if(op=='<'||op==T_LE)jump(yes);else {literal(0,0,NULL);jump(end);}
        mark(same);negate(0);emit(0x1010);branch(op=='<'?4:op==T_LE?6:op=='>'?1:3,yes);
    }
    literal(0,0,NULL);jump(end);mark(yes);literal(0,1,NULL);mark(end);
}
static const char *helper(int op) {
    switch(op){case '*':return "__mul";case '/':return "__div";case '%':return "__mod";
        case '|':return "__or";case '^':return "__xor";case T_SHL:return "__shl";case T_SHR:return "__shr";default:return NULL;}
}
static void binary(int op) {
    if(op=='+')emit(0x1010);else if(op=='-'){negate(0);emit(0x1010);}
    else if(op=='&')emit(0x2010);
    else if(wide && (op=='|' || op=='^')) {push(0);push(1);call(named("fn.",op=='|'?"__or":"__xor"));addi(6,2);}
    else if(helper(op)) { push(0);push(1);call(named("fn.",helper(op)));addi(6,2); }
    else if(!compiling_compare_helper) {
        push(0);push(1);
        const char *name=op=='<'?"__cmp_lt":op=='>'?"__cmp_gt":op==T_LE?"__cmp_le":op==T_GE?"__cmp_ge":op==T_EQ?"__cmp_eq":"__cmp_ne";
        call(named("fn.",name));addi(6,2);
    } else compare(op);
}
static void convert_value(int type) {if(type==5){literal(1,0,NULL);compare(T_NE);}}
static void args_reverse(Node *arg,Node *parameter) {
    if(!arg)return;
    assignment_compatible(parameter->type,arg);
    args_reverse(arg->next,parameter->next);expr(arg);convert_value(parameter->type);push(0);
}
static void args_reverse_signature(Node *arg,FunctionType *signature,int index) {
    if(!arg)return;
    if(!signature->unspecified && index>=signature->count)error("wrong argument count for function pointer");
    int type=signature->unspecified?0:signature->parameters[index];
    if(!signature->unspecified)assignment_compatible(type,arg);
    args_reverse_signature(arg->next,signature,index+1);
    expr(arg);if(!signature->unspecified)convert_value(type);push(0);
}
static const char *string_label(Node *n) {
    for(int i=0;i<nstrings;i++)if(strings[i].length==n->value && !memcmp(strings[i].value,n->name,(size_t)n->value))return strings[i].name;
    if(nstrings==4096)error("too many strings");
    char *name=label_name(fresh());strings[nstrings++]=(String){name,n->name,n->value};return name;
}
static int global_address(Node *n,const char **name,int *offset);
static int global_object_address(Node *n,const char **name,int *offset) {
    if(n->kind==N_STR) {
        *name=string_label(n);*offset=0;return 1;
    } else if(n->kind==N_ID) {
        for(int i=0;i<nglobals;i++)if(!strcmp(globals[i].name,n->name)) {
            *name=named("g.",n->name);*offset=0;return 1;
        }
        if(function_index(n->name)>=0) {*name=named("fn.",n->name);*offset=0;return 1;}
    } else if(n->kind==N_INDEX) {
        Node *base=n->a,*index=n->b;
        int type=decay_type(expression_type(base));
        if(!is_pointer(type)) {base=n->b;index=n->a;type=decay_type(expression_type(base));}
        if(!is_pointer(type))return 0;
        if(global_address(base,name,offset)) {
            *offset+=constant(index)*type_size(pointer_types[type-16].base);return 1;
        }
    } else if(n->kind==N_UNARY && n->op=='*')return global_address(n->a,name,offset);
    return 0;
}
static int global_address(Node *n,const char **name,int *offset) {
    if(n->kind==N_STR){*name=string_label(n);*offset=0;return 1;}
    if(n->kind==N_ID && function_index(n->name)>=0) {*name=named("fn.",n->name);*offset=0;return 1;}
    if(n->kind==N_CONDITIONAL) {
        expression_type(n);
        return global_address(constant(n->a)?n->b:n->c,name,offset);
    }
    if(n->kind==N_CAST && is_pointer(n->type))return global_address(n->a,name,offset);
    if(n->kind==N_BINARY && (n->op=='+' || n->op=='-')) {
        if(global_address(n->a,name,offset)){
            int t=decay_type(expression_type(n->a));
            int delta=constant(n->b)*type_size(pointer_types[t-16].base);*offset+=n->op=='+'?delta:-delta;return 1;
        }
        if(n->op=='+' && global_address(n->b,name,offset)){
            int t=decay_type(expression_type(n->b));*offset+=constant(n->a)*type_size(pointer_types[t-16].base);return 1;
        }
        return 0;
    }
    if(n->kind==N_UNARY && n->op=='&')return global_object_address(n->a,name,offset);
    if((n->kind==N_ID || n->kind==N_INDEX || (n->kind==N_UNARY && n->op=='*')) &&
       is_array(expression_type(n)))return global_object_address(n,name,offset);
    return 0;
}
static int decay_type(int type) {
    if(is_derived(type) && pointer_types[type-16].kind==2)
        return pointer_types[type-16].qualifiers?pointer_to_const(pointer_types[type-16].base):pointer_to(pointer_types[type-16].base);
    if(is_function(type))return pointer_to(type);
    return type;
}
static int compound_operator(int op) {
    switch(op) {
        case T_ADDEQ:return '+';case T_SUBEQ:return '-';case T_MULEQ:return '*';
        case T_DIVEQ:return '/';case T_MODEQ:return '%';case T_ANDEQ:return '&';
        case T_OREQ:return '|';case T_XOREQ:return '^';case T_SHLEQ:return T_SHL;
        case T_SHREQ:return T_SHR;default:error("unknown compound assignment");return 0;
    }
}
static int modifiable_type(Node *n) {
    if(n->kind!=N_ID && n->kind!=N_INDEX && !(n->kind==N_UNARY && n->op=='*'))error("modifiable lvalue required");
    int type=expression_type(n);
    if(type==3 || is_array(type))error("object is not assignable");
    if(n->kind==N_UNARY) {
        int pointer=decay_type(expression_type(n->a));
        if(is_pointer(pointer) && pointer_types[pointer-16].qualifiers)error("modification through pointer to const object");
    } else if(n->kind==N_INDEX) {
        int pointer=decay_type(expression_type(n->a));
        if(!is_pointer(pointer))pointer=decay_type(expression_type(n->b));
        if(is_pointer(pointer) && pointer_types[pointer-16].qualifiers)error("modification through pointer to const object");
    }
    if(n->kind==N_ID) {
        int readonly=0;
        if(parsing_types) {
            for(int i=parser_binding_count-1;i>=0;i--)if(!strcmp(parser_bindings[i].name,n->name)) {
                readonly=parser_bindings[i].readonly;break;
            }
        } else {int global;readonly=variable(n->name,&global)->readonly;}
        if(readonly)error("assignment to const object %s",n->name);
    }
    return type;
}
static void compound_constraints(int op,int left,int right) {
    if(right==3)error("assignment from void expression");
    right=decay_type(right);
    if(left==3 || right==3)error("void operand in compound assignment");
    if(is_pointer(left)) {
        if((op!='+' && op!='-') || is_pointer(right))error("pointer compound assignment requires integer offset");
        type_size(pointer_types[left-16].base);
    } else if(is_pointer(right))error("integer compound assignment has pointer operand");
}
static int discards_pointer_qualifiers(int target,int source) {
    if(!is_pointer(target) || !is_pointer(source))return 0;
    PointerType to=pointer_types[target-16],from=pointer_types[source-16];
    if(from.qualifiers & ~to.qualifiers)return 1;
    if(is_pointer(to.base) && is_pointer(from.base))return discards_pointer_qualifiers(to.base,from.base);
    return 0;
}
static void assignment_compatible(int target,Node *value) {
    int source=decay_type(expression_type(value));
    if(source==3)error("assignment from void expression");
    if(is_pointer(target)) {
        if(is_pointer(source)) {
            if(is_function_pointer(target)!=is_function_pointer(source))
                error("cannot convert between function and object pointers");
            if(discards_pointer_qualifiers(target,source))
                error("pointer assignment discards const qualifier");
            if(pointer_types[target-16].base!=3 && pointer_types[source-16].base!=3 && composite_type(target,source)<0)
                error("assignment has incompatible pointer types in %s",current_function ? current_function : "initializer");
        } else if(!constant_integer_syntax(value) || constant(value)!=0)error("pointer assignment requires null integer constant");
    } else if(is_pointer(source) && target!=5)error("pointer assignment to integer requires a cast");
}
static int object_readonly(Node *n) {
    if(n->kind==N_ID) {
        if(parsing_types) {
            for(int i=parser_binding_count-1;i>=0;i--)if(!strcmp(parser_bindings[i].name,n->name))return parser_bindings[i].readonly;
            return 0;
        }
        int global;Variable *v=variable_lookup(n->name,&global);
        if(v)return v->readonly;
        if(function_index(n->name)>=0)return 0;
        error("unknown variable %s",n->name);
    }
    if(n->kind==N_UNARY && n->op=='*') {
        int pointer=decay_type(expression_type(n->a));
        return is_pointer(pointer) && pointer_types[pointer-16].qualifiers;
    }
    if(n->kind==N_INDEX) {
        int pointer=decay_type(expression_type(n->a));
        if(!is_pointer(pointer))pointer=decay_type(expression_type(n->b));
        return is_pointer(pointer) && pointer_types[pointer-16].qualifiers;
    }
    return 0;
}
static int expression_type(Node *n) {
    switch(n->kind) {
        case N_NUM:return n->type;
        case N_STR:return array_of(4,n->value+1);
        case N_ID:{
            if(parsing_types) {
                for(int i=parser_binding_count-1;i>=0;i--)if(!strcmp(parser_bindings[i].name,n->name)) {
                    if(parser_bindings[i].alias==1)error("typedef name used as an object");
                    if(parser_bindings[i].alias==2)return parser_bindings[i].type;
                    return parser_bindings[i].type;
                }
                error("undeclared object in constant sizeof: %s",n->name);
            }
            int global;Variable *v=variable_lookup(n->name,&global);
            if(v)return v->type==2?(v->readonly?array_of_const(v->base,v->count):array_of(v->base,v->count)):v->type;
            Function *f=function(n->name);return function_type(f->node->type,f->node->count);
        }
        case N_CAST:{
            int source=decay_type(expression_type(n->a));
            if(n->type!=3 && source==3)error("cannot cast void expression to a value");
            if(is_pointer(n->type) && is_pointer(source) && is_function_pointer(n->type)!=is_function_pointer(source))
                error("cannot cast between function and object pointers");
            return n->type;
        }
        case N_SIZEOF:type_size(expression_type(n->a));return 6;
        case N_CALL: {
            if(n->d) {
                int pointer=decay_type(expression_type(n->d));
                if(!is_function_pointer(pointer))error("called expression is not a function pointer");
                int ftype=pointer_types[pointer-16].base;
                FunctionType *signature=&function_types[pointer_types[ftype-16].count];
                Node *argument=n->a;int index=0;
                if(!signature->unspecified) {
                    while(argument && index<signature->count) {
                        assignment_compatible(signature->parameters[index++],argument);argument=argument->next;
                    }
                    if(argument || index!=signature->count)error("wrong argument count for function pointer");
                }
                return pointer_types[ftype-16].base;
            }
            Node *declaration=NULL;
            for(Node *d=top;d;d=d->next)if((d->kind==N_FUNCTION || d->kind==N_PROTOTYPE) && !strcmp(d->name,n->name)) {
                if(!declaration || d->kind==N_FUNCTION || (declaration->kind!=N_FUNCTION && d->a))declaration=d;
            }
            if(declaration) {
                Node *d=declaration;
                /* Check constraints even when sizeof suppresses evaluation. */
                if(!strcmp(n->name,"bob_address"))return d->type;
                Node *argument=n->a,*parameter=d->a;
                while(argument && parameter) {
                    assignment_compatible(parameter->type,argument);
                    argument=argument->next;parameter=parameter->next;
                }
                if(argument || parameter)error("wrong argument count for %s",n->name);
                return d->type;
            }
            error("unknown function in sizeof: %s",n->name);return 0;
        }
        case N_INDEX:{
            int t=decay_type(expression_type(n->a)),other=decay_type(expression_type(n->b));
            if(t==3 || other==3)error("subscript has a void operand");
            if(is_pointer(t) && is_pointer(other))error("subscript needs one integer operand");
            if(!is_pointer(t))t=other;
            if(!is_pointer(t))error("subscript requires pointer/array");
            type_size(pointer_types[t-16].base);
            return pointer_types[t-16].base;
        }
        case N_UNARY:{
            int t=expression_type(n->a);
            if(n->op=='&') {
                if(n->a->kind!=N_ID && n->a->kind!=N_INDEX && n->a->kind!=N_STR &&
                   !(n->a->kind==N_UNARY && n->a->op=='*'))error("address-of requires an lvalue");
                if(n->a->kind==N_UNARY && n->a->op=='*')return decay_type(expression_type(n->a->a));
                return object_readonly(n->a)?pointer_to_const(t):pointer_to(t);
            }
            t=decay_type(t);
            if(t==3)error("void expression has no scalar value");
        if(n->op=='*'){if(!is_pointer(t))error("dereference requires pointer");return pointer_types[t-16].base;}
            if((n->op=='+' || n->op=='-' || n->op=='~') && is_pointer(t))error("unary integer operator has a pointer operand");
            if(n->op==T_INC || n->op==T_DEC) {t=modifiable_type(n->a);if(is_pointer(t))type_size(pointer_types[t-16].base);return t;}
            return (n->op=='+' || n->op=='-' || n->op=='~') && t==6?6:0;
        }
        case N_ASSIGN:{int type=modifiable_type(n->a);if(n->op=='=')assignment_compatible(type,n->b);else compound_constraints(compound_operator(n->op),type,expression_type(n->b));return type;}
        case N_BINARY:{
            if(n->op==',')return decay_type(expression_type(n->b));
            int a=decay_type(expression_type(n->a)),b=decay_type(expression_type(n->b));
            if(a==3 || b==3)error("void expression has no scalar value");
            if((n->op==T_EQ || n->op==T_NE) && (is_pointer(a) || is_pointer(b))) {
                if(is_pointer(a) && is_pointer(b)) {
                    if(is_function_pointer(a)!=is_function_pointer(b))error("function and object pointers cannot be compared");
                    if(pointer_types[a-16].base!=3 && pointer_types[b-16].base!=3 && composite_type(a,b)<0)
                        error("equality has incompatible pointer types");
                } else {
                    Node *integer=is_pointer(a)?n->b:n->a;
                    if(!constant_integer_syntax(integer) || constant(integer)!=0)error("pointer equality requires null integer constant");
                }
            }
            if((n->op=='<' || n->op=='>' || n->op==T_LE || n->op==T_GE) && (is_pointer(a) || is_pointer(b))) {
                if(!is_pointer(a) || !is_pointer(b) || is_function_pointer(a) || is_function_pointer(b) ||
                   composite_type(a,b)<0 || pointer_types[a-16].base==3)
                    error("relational pointer operands require compatible object types");
            }
            if((n->op=='*' || n->op=='/' || n->op=='%' || n->op=='&' || n->op=='|' || n->op=='^' || n->op==T_SHL || n->op==T_SHR) &&
               (is_pointer(a) || is_pointer(b)))error("integer operator has a pointer operand");
            if(n->op=='+' || n->op=='-') {
                if(is_pointer(a) && is_pointer(b) && (n->op=='+' || composite_type(a,b)<0))error("invalid pointer arithmetic");
                if(n->op=='-' && !is_pointer(a) && is_pointer(b))error("cannot subtract pointer from integer");
                if(is_pointer(a))type_size(pointer_types[a-16].base);
                if(is_pointer(b))type_size(pointer_types[b-16].base);
            }
            if(n->op=='+' && is_pointer(b))return b;
            if((n->op=='+' || n->op=='-') && is_pointer(a) && !is_pointer(b))return a;
            if(n->op==T_SHL || n->op==T_SHR)return a==6?6:0;
            if(n->op=='+' || n->op=='-' || n->op=='*' || n->op=='/' || n->op=='%' || n->op=='&' || n->op=='|' || n->op=='^')return a==6 || b==6?6:0;
            return 0;
        }
        case N_CONDITIONAL:{
            if(expression_type(n->a)==3)error("void condition");
            int a=decay_type(expression_type(n->b)),b=decay_type(expression_type(n->c));
            if(a==3 || b==3){if(a!=b)error("conditional mixes void and value");return 3;}
            if(is_pointer(a) && is_pointer(b)) {
                if(pointer_types[a-16].base==3 || pointer_types[b-16].base==3)return pointer_to(3);
                int type=composite_type(a,b);if(type<0)error("conditional has incompatible pointer types");return type;
            }
            if(is_pointer(a) || is_pointer(b)) {
                Node *integer=is_pointer(a)?n->c:n->b;
                if(!constant_integer_syntax(integer) || constant(integer)!=0)error("conditional pointer requires null integer constant");
                return is_pointer(a)?a:b;
            }
            return a==6 || b==6?6:0;
        }
        default:error("unsupported sizeof operand");return 0;
    }
}
static int type_size(int type) {
    if(type==3)error("sizeof void expression is invalid");
    if(is_function(type))error("function type has no object size");
    if(is_array(type)) {
        int base=type_size(pointer_types[type-16].base),count=pointer_types[type-16].count;
        if(count<1)error("incomplete array has no storage size");
        if(count>65535/base)error("array object too large for address space");
        return count*base;
    }
    return 1;
}
static void binary_typed(int op,int left,int right) {
    left=decay_type(left);right=decay_type(right);
    if(left==3 || right==3)error("void expression has no scalar value");
    if((op=='<' || op=='>' || op==T_LE || op==T_GE) && (is_pointer(left) || is_pointer(right))) {
        if(!is_pointer(left) || !is_pointer(right) || composite_type(left,right)<0 || pointer_types[left-16].base==3)
            error("relational pointer operands require compatible object types");
        /* Address order is unsigned. Flipping the sign bit maps it onto the
           signed comparison helpers without adding another runtime library. */
        addi(0,-32768);addi(1,-32768);binary(op);return;
    }
    const char *unsigned_helper=NULL;
    if((left==6 || right==6) && (op=='/' || op=='%'))unsigned_helper=op=='/'?"__u_div":"__u_mod";
    if(left==6 && op==T_SHR)unsigned_helper="__u_shr";
    if((left==6 || right==6) && (op=='<' || op=='>' || op==T_LE || op==T_GE))
        unsigned_helper=op=='<'?"__u_lt":op=='>'?"__u_gt":op==T_LE?"__u_le":"__u_ge";
    if(unsigned_helper){if(is_pointer(left) || is_pointer(right))error("unsigned integer operator has pointer operand");push(0);push(1);call(named("fn.",unsigned_helper));addi(6,2);return;}
    if((op=='*' || op=='/' || op=='%' || op=='&' || op=='|' || op=='^' || op==T_SHL || op==T_SHR) &&
       (is_pointer(left) || is_pointer(right)))error("integer operator has a pointer operand");
    if((op=='+' || op=='-') && (is_pointer(left) || is_pointer(right))) {
        if(is_pointer(left) && is_pointer(right)) {
            if(op!='-' || composite_type(left,right)<0)error("invalid pointer arithmetic");
            type_size(pointer_types[right-16].base);
            int stride=type_size(pointer_types[left-16].base);binary('-');
            if(stride!=1){move(1,0);literal(0,stride,NULL);binary('/');}return;
        }
        int pointer=is_pointer(left)?left:right;
        if(op=='-' && !is_pointer(left))error("cannot subtract a pointer from an integer");
        int stride=type_size(pointer_types[pointer-16].base);
        if(stride!=1) {
            if(is_pointer(left)){push(1);move(1,0);literal(0,stride,NULL);binary('*');pop(1);}
            else {push(0);move(0,1);literal(1,stride,NULL);binary('*');pop(1);}
        }
    }
    binary(op);
}
static void expr(Node *n) {
    if(!n)error("missing expression");
    switch(n->kind) {
    case N_NUM:literal(0,n->value,NULL);break;
    case N_STR:literal(0,0,string_label(n));break;
    case N_ID: {
        int global;Variable *v=variable_lookup(n->name,&global);
        if(v){address(n);if(v->type!=2)emit(0x6000);}
        else {function(n->name);literal(0,0,named("fn.",n->name));}
        break;
    }
    case N_INDEX:address(n);if(!is_array(expression_type(n)))emit(0x6000);break;
    case N_CAST:expression_type(n);expr(n->a);convert_value(n->type);break;
    case N_SIZEOF:{
        literal(0,type_size(expression_type(n->a)),NULL);break;
    }
    case N_UNARY:
        expression_type(n);
        if(n->a->kind==N_NUM && (n->op=='-' || n->op=='~' || n->op=='!')) {
            int value=n->a->value;
            literal(0,n->op=='-'?-value:n->op=='~'?~value:!value,NULL);break;
        }
        if(n->op=='!' && n->a->kind==N_UNARY && n->a->op=='!') {
            expression_type(n);expr(n->a->a);literal(1,0,NULL);compare(T_NE);break;
        }
        if(n->op=='&') {
            if(n->a->kind==N_ID) {int global;if(!variable_lookup(n->a->name,&global))expr(n->a);else address(n->a);}
            else address(n->a);
        }
        else if(n->op==T_INC||n->op==T_DEC) {address(n->a);push(0);emit(0x6000);if(n->value)move(4,0);
            int type=expression_type(n->a);if(is_array(type))error("array is not modifiable");
            int stride=is_pointer(type)?type_size(pointer_types[type-16].base):1;
            addi(0,n->op==T_INC?stride:-stride);convert_value(type);pop(1);emit(0x9040);if(n->value)move(0,4);}
        else {if(n->op=='+' || n->op=='-' || n->op=='~' || n->op=='!')expression_type(n);expr(n->a);if(n->op=='*'){int t=expression_type(n);if(t==3)error("cannot load void");if(!is_array(t) && !is_function(t))emit(0x6000);}else if(n->op=='-')negate(0);else if(n->op=='~')emit(0x3100);
            else if(n->op=='!'){literal(1,0,NULL);compare(T_EQ);}else if(n->op!='+')error("unsupported unary operator");}
        break;
    case N_BINARY:
        expression_type(n);
        if(n->a->kind==N_NUM && n->b->kind==N_NUM && (n->op=='+' || n->op=='-' || n->op=='*')) {
            literal(0,constant(n),NULL);break;
        }
        if(n->op==','){expr(n->a);expr(n->b);}
        else if(n->op==T_AND||n->op==T_OR) {expression_type(n);int shortcut=fresh(),end=fresh();expr(n->a);move(0,0);branch(n->op==T_AND?2:5,shortcut);
            expr(n->b);literal(1,0,NULL);compare(T_NE);jump(end);mark(shortcut);literal(0,n->op==T_OR,NULL);mark(end);}
        else {expr(n->a);push(0);expr(n->b);pop(1);binary_typed(n->op,expression_type(n->a),expression_type(n->b));}break;
    case N_CONDITIONAL:{
        expression_type(n);
        int other=fresh(),end=fresh();expr(n->a);move(0,0);branch(2,other);
        expr(n->b);jump(end);mark(other);expr(n->c);mark(end);break;
    }
    case N_ASSIGN: {
        int left_type=modifiable_type(n->a);
        if(n->op=='=')assignment_compatible(left_type,n->b);
        if(n->op!='=')compound_constraints(compound_operator(n->op),left_type,expression_type(n->b));
        address(n->a);push(0);
        if(n->op!='='){emit(0x6000);push(0);}
        expr(n->b);
        if(n->op!='=') { pop(1);binary_typed(compound_operator(n->op),left_type,expression_type(n->b)); }
        convert_value(left_type);pop(1);emit(0x9040);break; }
    case N_CALL: {
        int count=0;for(Node *a=n->a;a;a=a->next)count++;
        if(n->d) {
            int pointer=decay_type(expression_type(n->d));
            if(!is_function_pointer(pointer))error("called expression is not a function pointer");
            int ftype=pointer_types[pointer-16].base;
            FunctionType *signature=&function_types[pointer_types[ftype-16].count];
            if(!signature->unspecified && count!=signature->count)error("wrong argument count for function pointer");
            expr(n->d);push(0);args_reverse_signature(n->a,signature,0);
            move(3,6);addi(3,count);emit(0x6000|(3<<9)|(3<<6));emit(legacy?0xcb00:0xc300);addi(6,count+1);break;
        }
        if(!strcmp(n->name,"bob_address")){if(count!=1||n->a->kind!=N_ID)error("bob_address requires a function name");function(n->a->name);literal(0,0,named("fn.",n->a->name));break;}
        if(!strcmp(n->name,"bob_call")){if(count!=1)error("bob_call needs one argument");assignment_compatible(0,n->a);expr(n->a);move(3,0);emit(legacy?0xcb00:0xc300);break;}
        int vector=-1,expected=0;
        if(!strcmp(n->name,"bob_halt"))vector=0;
        else if(!strcmp(n->name,"bob_putc")){vector=1;expected=1;}
        else if(!strcmp(n->name,"bob_puts")){vector=2;expected=1;}
        else if(!strcmp(n->name,"bob_gets")){vector=3;expected=2;}
        else if(!strcmp(n->name,"bob_getc"))vector=5;
        else if(!strcmp(n->name,"bob_run")){vector=6;expected=1;}
        else if(!strcmp(n->name,"bob_snapshot")){vector=7;expected=2;}
        else if(!strcmp(n->name,"bob_run_image")){vector=12;expected=1;}
        else if(!strcmp(n->name,"bob_run_native")){vector=13;expected=1;}
        else if(!strcmp(n->name,"bob_import_image")){vector=14;expected=1;}
        else if(!strcmp(n->name,"bob_key"))vector=8;
        else if(!strcmp(n->name,"bob_terminal"))vector=9;
        else if(!strcmp(n->name,"bob_columns"))vector=10;
        else if(!strcmp(n->name,"bob_rows"))vector=11;
        if(vector>=0) {if(count!=expected)error("wrong argument count for %s",n->name);
            int first_type=(vector==2 || vector==12)?pointer_to_const(4):vector==3?pointer_to(4):vector==7 || vector==13 || vector==14?pointer_to(0):0;
            if(count)assignment_compatible(first_type,n->a);
            if(expected==2)assignment_compatible(0,n->a->next);
            if(expected==2){expr(n->a->next);push(0);expr(n->a);pop(1);}else if(count)expr(n->a);
            emit(0xf000|vector<<8);break;}
        Function *f=function(n->name);if(count!=f->nargs)error("wrong argument count for %s",n->name);
        args_reverse(n->a,f->node->a);call(named("fn.",n->name));if(count)addi(6,count);break; }
    default:error("unsupported expression kind %d",n->kind);
    }
}
static void switch_cases(Node *n,Node **cases,int *count) {
    if(!n)return;
    if(n->kind==N_CASE) {
        if(*count==256)error("too many switch cases");
        for(int i=0;i<*count;i++) {
            Node *previous=cases[i];
            if(n->type && previous->type)error("duplicate default");
            if(!n->type && !previous->type && (uint16_t)n->count==(uint16_t)previous->count)error("duplicate case value");
        }
        n->value=fresh();cases[(*count)++]=n;
    }
    if(n->kind!=N_SWITCH) {
        switch_cases(n->a,cases,count);switch_cases(n->b,cases,count);
        switch_cases(n->c,cases,count);switch_cases(n->d,cases,count);
    }
    switch_cases(n->next,cases,count);
}
static void static_references(Node *n) {
    if(!n)return;
    if(n->kind==N_SIZEOF) {
        n->value=type_size(expression_type(n->a));n->kind=N_NUM;n->a=NULL;
    }
    if(n->kind==N_ID) {
        if(function_index(n->name)>=0)return;
        int global;Variable *v=variable(n->name,&global);
        if(!global)error("static initializer refers to automatic object %s",n->name);
        n->name=v->name;
    }
    static_references(n->a);static_references(n->b);static_references(n->c);static_references(n->d);static_references(n->next);
}
static void stmt(Node *n) {
    if(!n)return;
    switch(n->kind) {
    case N_DECL_GROUP:for(Node *d=n->a;d;d=d->next)stmt(d);break;
    case N_LABEL:define(user_label(n->name));stmt(n->b);break;
    case N_GOTO:jump_name(user_label(n->name));break;
    case N_CASE:if(!n->value)error("case/default outside switch");mark(n->value);stmt(n->b);break;
    case N_SWITCH:{
        int type=decay_type(expression_type(n->a));
        if(type==3 || is_pointer(type))error("switch requires integer condition");
        Node *cases[256];int count=0,end=fresh(),fallback=end;
        switch_cases(n->b,cases,&count);
        expr(n->a);push(0);
        for(int i=0;i<count;i++) {
            Node *c=cases[i];if(c->type){fallback=c->value;continue;}
            int next=fresh();literal(0,c->count,NULL);emit(0x6380);compare(T_EQ);
            move(0,0);branch(2,next);addi(6,1);jump(c->value);mark(next);
        }
        addi(6,1);jump(fallback);
        if(break_depth==64)error("break contexts nested too deeply");
        break_targets[break_depth++]=end;stmt(n->b);break_depth--;mark(end);break;
    }
    case N_BLOCK:{
        int saved=nlocals,base=scope_base;
        scope_base=function_body?0:nlocals;function_body=0;
        for(Node *s=n->a;s;s=s->next)stmt(s);
        nlocals=saved;scope_base=base;break;
    }
    case N_DECL:
        if(n->storage) {
            int index=n->storage==2?n->value:0;
            if(n->storage==1)while(index<nglobals && strcmp(globals[index].name,n->name))index++;
            if(index==nglobals)error("unregistered extern object");
            int repeated=0;
            for(int i=scope_base;i<nlocals;i++)if(!strcmp(locals[i].name,n->name)) {
                if(n->storage!=1 || locals[i].offset!=65536+index)error("conflicting local storage for %s",n->name);
                repeated=1;
            }
            if(!repeated){if(nlocals==MAX_SYMBOLS)error("too many locals");locals[nlocals++]=(Variable){n->name,n->type,n->count,65536+index,NULL,n->op,n->readonly};}
            if(n->storage==2)static_references(n->a);
            break;
        }
        for(int i=scope_base;i<nlocals;i++)if(!strcmp(locals[i].name,n->name))error("duplicate local %s",n->name);
        if(nlocals==MAX_SYMBOLS)error("too many locals");
        locals[nlocals++]=(Variable){n->name,n->type,n->count,n->value,n->a,n->op,n->readonly};
        if(n->a){
        Node v={0};v.kind=N_ID;v.name=n->name;
        if(n->type!=2){assignment_compatible(n->type,n->a);address(&v);push(0);expr(n->a);convert_value(n->type);pop(1);emit(0x9040);}
        else {
            int index=0,total=type_size(array_of(n->op,n->count)),element=n->op;
            while(is_array(element))element=pointer_types[element-16].base;
            for(Node *value=n->a;value;value=value->next) {
                if(index==total)error("too many array initializers");
                assignment_compatible(element,value);address(&v);addi(0,index++);push(0);expr(value);convert_value(element);pop(1);emit(0x9040);
            }
            if(index<total) {
                address(&v);addi(0,index);move(1,0);literal(2,total-index,NULL);literal(0,0,NULL);
                int loop=fresh();mark(loop);emit(0x9040);addi(1,1);addi(2,-1);branch(5,loop);
            }
        }
    }break;
    case N_RETURN:
        if(current_return_type==3 && n->a)error("void function returns an expression");
        if(current_return_type!=3 && !n->a)error("value-returning function needs a return expression");
        if(n->a){assignment_compatible(current_return_type,n->a);expr(n->a);}else literal(0,0,NULL);
        convert_value(current_return_type);jump(return_label);break;
    case N_IF:{if(expression_type(n->a)==3)error("void condition");int other=fresh(),end=fresh();expr(n->a);move(0,0);branch(2,other);stmt(n->b);jump(end);mark(other);stmt(n->c);mark(end);break;}
    case N_DO:{
        int start=fresh(),step=fresh(),end=fresh();
        if(loop_depth==64)error("loops nested too deeply");
        if(break_depth==64)error("break contexts nested too deeply");
        mark(start);loop_step[loop_depth]=step;loop_end[loop_depth++]=end;break_targets[break_depth++]=end;
        stmt(n->b);loop_depth--;break_depth--;mark(step);if(expression_type(n->a)==3)error("void condition");expr(n->a);move(0,0);branch(5,start);mark(end);break;
    }
    case N_WHILE:case N_FOR:{int start=fresh(),step=fresh(),end=fresh();
        int saved=nlocals,base=scope_base;
        if(n->kind==N_FOR)scope_base=nlocals;
        if(n->kind==N_FOR)stmt(n->a);
        mark(start);Node *condition=n->kind==N_FOR?n->b:n->a;
        if(condition){if(expression_type(condition)==3)error("void condition");expr(condition);move(0,0);branch(2,end);}if(loop_depth==64)error("loops nested too deeply");
        if(break_depth==64)error("break contexts nested too deeply");
        loop_step[loop_depth]=step;loop_end[loop_depth++]=end;break_targets[break_depth++]=end;
        stmt(n->kind==N_FOR?n->d:n->b);loop_depth--;break_depth--;
        mark(step);if(n->kind==N_FOR&&n->c)expr(n->c);jump(start);mark(end);
        nlocals=saved;scope_base=base;break;}
    case N_BREAK:if(!break_depth)error("break outside loop/switch");jump(break_targets[break_depth-1]);break;
    case N_CONTINUE:if(!loop_depth)error("continue outside loop");jump(loop_step[loop_depth-1]);break;
    case N_EMPTY:break;
    default:expr(n);
    }
}
static int frame_slots;
static void collect(Node *n) {
    if(!n)return;
    if(n->kind==N_DECL && !n->storage){frame_slots+=n->type==2?type_size(array_of(n->op,n->count)):1;n->value=-frame_slots;}
    collect(n->a);collect(n->b);collect(n->c);collect(n->d);collect(n->next);
}
/* Return the composite type, or -1 when the declarations conflict. */
static int composite_type(int a,int b) {
    if(a==b)return a;
    if(is_enum(a) && b==0)return a;
    if(is_enum(b) && a==0)return b;
    if(!is_derived(a) || !is_derived(b))return -1;
    PointerType x=pointer_types[a-16],y=pointer_types[b-16];
    if(x.kind!=y.kind)return -1;
    if(x.kind==3)return -1;
    if(x.kind==4) {
        int signature=x.count;
        if(x.count!=y.count) {
            FunctionType *left=&function_types[x.count],*right=&function_types[y.count];
            if(left->unspecified==right->unspecified)return -1;
            FunctionType *specified=left->unspecified?right:left;
            for(int i=0;i<specified->count;i++)
                if(specified->parameters[i]!=0 && specified->parameters[i]!=6 && !is_pointer(specified->parameters[i]))return -1;
            signature=left->unspecified?y.count:x.count;
        }
        int result=composite_type(x.base,y.base);
        return result<0?-1:function_type(result,signature);
    }
    int base=composite_type(x.base,y.base);
    if(base<0)return -1;
    if(x.kind==1)return derived_type(1,base,1,x.qualifiers|y.qualifiers);
    if(x.count && y.count && x.count!=y.count)return -1;
    return derived_type(2,base,x.count?x.count:y.count,x.qualifiers|y.qualifiers);
}
static int function_composite_type(int a,int b) {
    if(is_pointer(a) || is_pointer(b)) {
        if(!is_pointer(a) || !is_pointer(b))return -1;
        PointerType x=pointer_types[a-16],y=pointer_types[b-16];
        if(x.qualifiers!=y.qualifiers)return -1;
        int base=function_composite_type(x.base,y.base);
        return base<0?-1:derived_type(1,base,1,x.qualifiers);
    }
    if(is_array(a) || is_array(b)) {
        if(!is_array(a) || !is_array(b))return -1;
        PointerType x=pointer_types[a-16],y=pointer_types[b-16];
        if(x.count && y.count && x.count!=y.count)return -1;
        int base=function_composite_type(x.base,y.base);
        return base<0?-1:derived_type(2,base,x.count?x.count:y.count,x.qualifiers|y.qualifiers);
    }
    return composite_type(a,b);
}
static void compatible_function(Node *a,Node *b) {
    int result=function_composite_type(a->type,b->type);
    if(result<0)error("conflicting return type for %s",a->name);
    a->type=b->type=result;
    if((a->kind==N_PROTOTYPE && a->op) || (b->kind==N_PROTOTYPE && b->op))return;
    Node *x=a->a,*y=b->a;
    while(x && y){result=function_composite_type(x->type,y->type);if(result<0)error("conflicting parameter type for %s",a->name);x->type=y->type=result;x=x->next;y=y->next;}
    if(x || y)error("conflicting parameter count for %s",a->name);
}
static int register_object(Node *n,int defined,int visible) {
            int index=0;while(index<nglobals && strcmp(globals[index].name,n->name))index++;
            if(index==nglobals) {
                if(nglobals==MAX_SYMBOLS)error("too many globals");
                globals[nglobals++]=(Variable){n->name,n->type,n->count,0,n->a,n->op,n->readonly};
                global_linkage[index]=n->storage==2?2:1;
            } else {
                if(globals[index].readonly!=n->readonly)error("conflicting const qualification for %s",n->name);
                if(visible && ((n->storage==2 && global_linkage[index]!=2) ||
                               (n->storage==0 && global_linkage[index]==2)))error("conflicting linkage for object %s",n->name);
                Variable *g=&globals[index];
                int old=g->type==2?array_of(g->base,g->count):g->type;
                int next=n->type==2?array_of(n->op,n->count):n->type;
                int merged=composite_type(old,next);
                if(merged<0)error("conflicting object type for %s",n->name);
                if(g->init && n->a)error("multiple object definitions for %s",n->name);
                if(is_array(merged)){g->type=2;g->base=pointer_types[merged-16].base;g->count=pointer_types[merged-16].count;}
                else g->type=merged;
                if(n->a)g->init=n->a;
            }
            if(defined)global_defined[index]=1;
            if(visible && n->storage==2 && n->type==2 && !n->count)error("internal tentative array requires complete type");
            if(visible)global_visible[index]=1;
            return index;
}
static void collect_externs(Node *n) {
    if(!n)return;
    if(n->kind==N_DECL && n->storage) {
        if(n->storage==2) {Node object=*n;object.name=named("static.",label_name(fresh()));n->value=register_object(&object,1,0);}
        else register_object(n,0,0);
    }
    collect_externs(n->a);collect_externs(n->b);collect_externs(n->c);collect_externs(n->d);collect_externs(n->next);
}
static int function_index(const char *name) {
    for(int i=0;i<nfunctions;i++)if(!strcmp(functions[i].name,name))return i;
    return -1;
}
static void mark_function(const char *name);
static void mark_nodes(Node *n) {
    if(!n)return;
    if(n->kind==N_ID && function_index(n->name)>=0)mark_function(n->name);
    if(n->kind==N_CALL && n->name) {
        if(!strcmp(n->name,"bob_address") && n->a && n->a->kind==N_ID)mark_function(n->a->name);
        else mark_function(n->name);
    }
    if(n->kind==N_INDEX){mark_function("__mul");mark_function("__div");}
    if(n->kind==N_BINARY || n->kind==N_ASSIGN) {
        const char *helper_name=NULL;
        switch(n->op) {
        case '*':case T_MULEQ:helper_name="__mul";break;
        case '/':case T_DIVEQ:helper_name="__div";break;
        case '%':case T_MODEQ:helper_name="__mod";break;
        case '|':case T_OREQ:helper_name="__or";break;
        case '^':case T_XOREQ:helper_name="__xor";break;
        case T_SHL:case T_SHLEQ:helper_name="__shl";break;
        case T_SHR:case T_SHREQ:helper_name="__shr";break;
        case '<':helper_name="__cmp_lt";break;
        case '>':helper_name="__cmp_gt";break;
        case T_LE:helper_name="__cmp_le";break;
        case T_GE:helper_name="__cmp_ge";break;
        case T_EQ:helper_name="__cmp_eq";break;
        case T_NE:helper_name="__cmp_ne";break;
        default:break;
        }
        if(helper_name)mark_function(helper_name);
        if(n->op=='/' || n->op==T_DIVEQ || n->op=='%' || n->op==T_MODEQ)
            mark_function(n->op=='/' || n->op==T_DIVEQ?"__u_div":"__u_mod");
        if(n->op==T_SHR || n->op==T_SHREQ)mark_function("__u_shr");
        if(n->op=='<' || n->op=='>' || n->op==T_LE || n->op==T_GE) {
            mark_function(n->op=='<'?"__u_lt":n->op=='>'?"__u_gt":n->op==T_LE?"__u_le":"__u_ge");
        }
        if(n->op=='+' || n->op=='-') {mark_function("__mul");mark_function("__div");}
    }
    mark_nodes(n->a);mark_nodes(n->b);mark_nodes(n->c);mark_nodes(n->d);mark_nodes(n->next);
}
static void mark_function(const char *name) {
    int index=function_index(name);
    if(index<0 || functions[index].reachable)return;
    functions[index].reachable=1;
    mark_nodes(functions[index].node->b);
}
static void generate(void) {
    char *linkage_names[MAX_SYMBOLS];int linkage_values[MAX_SYMBOLS],linkage_count=0;
    for(Node *n=top;n;n=n->next) {
        if(n->kind==N_FUNCTION || n->kind==N_PROTOTYPE) {
            int i=0;while(i<linkage_count && strcmp(linkage_names[i],n->name))i++;
            if(i==linkage_count) {
                if(linkage_count==MAX_SYMBOLS)error("too many function declarations");
                linkage_names[i]=n->name;linkage_values[i]=n->storage==2?2:1;linkage_count++;
            } else if(n->storage==2 && linkage_values[i]!=2)error("conflicting linkage for function %s",n->name);
        }
        if(n->kind==N_FUNCTION){if(nfunctions==MAX_SYMBOLS)error("too many functions");int count=0;for(Node *a=n->a;a;a=a->next)count++;
            for(int i=0;i<nfunctions;i++)if(!strcmp(functions[i].name,n->name))error("duplicate function %s",n->name);
            functions[nfunctions++]=(Function){n->name,n,count,0};}
        else if(n->kind==N_DECL)register_object(n,!n->value || n->a,1);
    }
    for(int i=0;i<nfunctions;i++)collect_externs(functions[i].node->b);
    for(int i=0;i<nglobals;i++)if(global_defined[i] && globals[i].type==2 && !globals[i].count)globals[i].count=1;
    for(Node *n=top;n;n=n->next)if(n->kind==N_PROTOTYPE || n->kind==N_FUNCTION) {
        for(Node *other=n->next;other;other=other->next)
            if((other->kind==N_PROTOTYPE || other->kind==N_FUNCTION) && !strcmp(n->name,other->name))compatible_function(n,other);
        for(int i=0;i<nglobals;i++)if(!strcmp(n->name,globals[i].name))error("object/function name collision %s",n->name);
    }
    if(function("main")->nargs && (!wide_app || function("main")->nargs!=2))
        error("main must take no arguments, or a bob32 app may use main(int argc, char **argv)");
    for(Node *n=top;n;n=n->next)if(n->kind==N_DECL)mark_nodes(n->a);
    mark_function("main");
    define("entry");if(!wide_app)literal(6,0xf000,NULL);literal(5,0,NULL);
    if(wide_app && function("main")->nargs==2){push(1);push(0);}
    call("fn.main");emit(0xf000);
    for(int i=0;i<nfunctions;i++) {
        if(wide && !functions[i].reachable)continue;
        Function *f=&functions[i];current_function=f->name;current_return_type=f->node->type;nlocals=0;frame_slots=0;int arg=0;
        compiling_compare_helper=!strncmp(f->name,"__cmp_",6);
        for(Node *a=f->node->a;a;a=a->next){
            if(!*a->name||a->type==2)error("named scalar/pointer parameters required");
            for(int j=0;j<nlocals;j++)if(!strcmp(locals[j].name,a->name))error("duplicate parameter %s",a->name);
            locals[nlocals++]=(Variable){a->name,a->type,1,2+arg++,NULL,a->op,a->readonly};
        }
        collect(f->node->b);if(frame_slots>4096)error("frame too large");return_label=fresh();define(named("fn.",f->name));
        scope_base=0;function_body=1;
        int needs_frame=frame_slots || f->nargs;
        push(7);if(needs_frame){push(5);move(5,6);}if(frame_slots)addi(6,-frame_slots);
        stmt(f->node->b);literal(0,0,NULL);mark(return_label);
        if(needs_frame){move(6,5);pop(5);}pop(7);emit(0xe000);
    }
    nlocals=0;
    for(int i=0;i<nglobals;i++){if(!global_defined[i])continue;Variable *g=&globals[i];define(named("g.",g->name));Node *init=g->init;
        int total=g->type==2?type_size(array_of(g->base,g->count)):g->count;
        int element=g->type==2?g->base:g->type;
        while(is_array(element))element=pointer_types[element-16].base;
        for(int j=0;j<total;j++){
            const char *target=NULL;int offset=0;
            if(init)assignment_compatible(element,init);
            if(init && is_pointer(element) && global_address(init,&target,&offset)) {
                emit_relocation(target,offset);
            } else {int value;
                if(init && element==5 && is_pointer(decay_type(expression_type(init))) && global_address(init,&target,&offset))value=1;
                else value=init?constant(init):0;
                emit(element==5?value!=0:value);}
            if(init)init=init->next;
        }if(init)error("too many initializers for %s",g->name);}
    for(int i=0;i<nstrings;i++){define(strings[i].name);for(int j=0;j<strings[i].length;j++)emit((unsigned char)strings[i].value[j]);emit(0);}
    compact_jumps();
    if(!wide && origin+ncode>0xa000)error("kernel exceeds 0xA000 (code + data is %d words)",ncode);
    if(wide && (uint64_t)origin+(uint32_t)ncode>(uint64_t)UINT32_MAX+1)
        error("wide image exceeds the 32-bit address space");
    for(int i=0;i<nfixups;i++){int found=0;for(int j=0;j<nlabels;j++)if(!strcmp(fixups[i].name,labels[j].name)) {code[fixups[i].index]=(uint32_t)(labels[j].address+fixups[i].addend);found=1;break;}
        if(!found)error("unresolved symbol %s",fixups[i].name);}
}
static char *read_file(const char *path) {
    FILE *f=fopen(path,"rb");if(!f){perror(path);exit(1);}
    if(fseek(f,0,SEEK_END))error("input seek failure");
    long length=ftell(f);
    if(length<0||length>4000000)error("input too large");
    if(fseek(f,0,SEEK_SET))error("input seek failure");
    char *s=allocate((size_t)length+1);
    if(fread(s,1,(size_t)length,f)!=(size_t)length || ferror(f))error("read failure");
    if(fclose(f))error("input close failure");
    if(memchr(s,0,(size_t)length))error("zero byte in source");
    return s;
}
static void word(FILE *f,int value) { fputc(value&255,f);fputc((value>>8)&255,f); }
static void dword(FILE *f,uint32_t value) { word(f,(int)value);word(f,(int)(value>>16)); }
static void finish_output(FILE *file) {
    int failed=ferror(file);
    if(fclose(file))failed=1;
    if(failed)error("output write failure");
}
static int same_path(const char *a,const char *b) {
#ifdef _WIN32
    char absolute_a[4096],absolute_b[4096];
    if(!_fullpath(absolute_a,a,sizeof(absolute_a)) || !_fullpath(absolute_b,b,sizeof(absolute_b)))
        error("cannot resolve output path");
    return !_stricmp(absolute_a,absolute_b);
#else
    return !strcmp(a,b);
#endif
}
int main(int argc,char **argv) {
    if(argc<4||argc>5){fprintf(stderr,"Usage: bobcc input.i output.basm output.b16 [--legacy|--wide|--wide-kernel|--wide-app]\n");return 1;}
    if(argc==5){if(!strcmp(argv[4],"--legacy"))legacy=1;else if(!strcmp(argv[4],"--wide"))wide=1;else if(!strcmp(argv[4],"--wide-kernel")){wide=1;origin=0x10000;}else if(!strcmp(argv[4],"--wide-app")){wide=1;wide_app=1;origin=0x20000;}else error("unknown option");}
    if(wide&&legacy)error("--wide cannot be combined with --legacy");
    lex(read_file(argv[1]));parse();
    /* Parse helper bodies in a second token buffer, preserving the AST. */
    memset(tokens,0,sizeof(tokens));ntokens=at=0;lex(read_file(wide?"tools/arithmetic32.c":"tools/arithmetic.c"));parse();
    if(unsigned_helpers && !wide){memset(tokens,0,sizeof(tokens));ntokens=at=0;lex(read_file("tools/unsigned.c"));parse();}
    generate();
    char map_path[4096];
    if(strlen(argv[3])+5>=sizeof(map_path))error("output path too long");
    strcpy(map_path,argv[3]);char *base=map_path;
    for(char *p=map_path;*p;p++)if(*p=='/' || *p=='\\')base=p+1;
    char *extension=strrchr(base,'.');
    if(extension)strcpy(extension,".map");else strcat(map_path,".map");
    if(same_path(argv[2],argv[3]) || same_path(argv[2],map_path) || same_path(argv[3],map_path) ||
       same_path(argv[1],argv[2]) || same_path(argv[1],argv[3]) || same_path(argv[1],map_path))
        error("input and output paths must be distinct");
    FILE *basm=fopen(argv[2],"wb");if(!basm){perror(argv[2]);return 1;}
    fprintf(basm,"; bob%d C program, generated by tools/bobcc.c\n",wide?32:16);
    if(wide_app) {
        fprintf(basm,".org 0x%08x\n",origin);
    } else if(wide) {
        fprintf(basm,".fill 00004001\n.fill 0000ae01\n.fill %08x\n.fill 0000b000\n",origin);
        for(uint32_t i=4;i<origin;i++)fprintf(basm,".fill 00000000\n");
    } else {
        fprintf(basm,".fill 4601\n.fill b600\n.fill 0300\n");
        for(uint32_t i=3;i<origin;i++)fprintf(basm,".fill 0000\n");
    }
    for(int i=0;i<ncode;i++)fprintf(basm,wide?".fill %08x\n":".fill %04x\n",wide?code[i]:(code[i]&0xffffu));
    finish_output(basm);FILE *binary=fopen(argv[3],"wb");if(!binary){perror(argv[3]);return 1;}
    uint32_t sum=0;for(int i=0;i<ncode;i++)sum+=code[i];
    if(wide){fwrite("B32K",1,4,binary);word(binary,2);word(binary,24);dword(binary,origin);dword(binary,origin);dword(binary,(uint32_t)ncode);dword(binary,sum);}
    else {fwrite("B16K",1,4,binary);word(binary,1);word(binary,origin);word(binary,origin);word(binary,ncode);word(binary,(int)sum);}
    for(int i=0;i<ncode;i++)if(wide)dword(binary,code[i]);else word(binary,(int)code[i]);
    finish_output(binary);
    FILE *map=fopen(map_path,"w");if(!map){perror(map_path);return 1;}
    for(int i=0;i<nlabels;i++)fprintf(map,wide?"%08x %s\n":"%04x %s\n",labels[i].address,labels[i].name);
    finish_output(map);
    printf("Compiled %d bob%d words.\n",ncode,wide?32:16);return 0;
}
