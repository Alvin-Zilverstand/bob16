/* BOB-16 C subset compiler. Host tool, written entirely in C.
   Input is C preprocessed by GCC; output is native bob16 BASM and B16K. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

enum { T_ID=256, T_NUM, T_STR, T_EQ, T_NE, T_LE, T_GE, T_AND, T_OR,
       T_INC, T_DEC, T_SHL, T_SHR, T_ADDEQ, T_SUBEQ, T_MULEQ, T_DIVEQ,
       T_MODEQ, T_ANDEQ, T_OREQ, T_XOREQ };
enum { N_NUM=1, N_STR, N_ID, N_BINARY, N_UNARY, N_ASSIGN, N_CALL, N_INDEX,
       N_BLOCK, N_DECL, N_IF, N_WHILE, N_FOR, N_RETURN, N_BREAK, N_CONTINUE,
       N_EMPTY, N_CAST, N_FUNCTION, N_DO, N_CONDITIONAL, N_SIZEOF, N_LABEL, N_GOTO,
       N_SWITCH, N_CASE, N_DECL_GROUP, N_PROTOTYPE };
typedef struct Node Node;
struct Node { int kind, op, value, type, count; char *name; Node *a,*b,*c,*d,*next; };
typedef struct { int type, number, line; char *text; } Token;
static Token tokens[65536];
static int ntokens, at;
static int parsing_parameter;
/* Interned pointer descriptors retain pointee type and indirection depth.
   Primitive IDs remain int=0, void=3, char=4; array objects still use kind 2. */
typedef struct { int kind,base,count; } PointerType;
static PointerType pointer_types[4096];
static int npointer_types;
static int derived_type(int kind,int base,int count) {
    for(int i=0;i<npointer_types;i++)if(pointer_types[i].kind==kind && pointer_types[i].base==base && pointer_types[i].count==count)return 16+i;
    if(npointer_types==4096){fprintf(stderr,"bobcc: too many pointer types\n");exit(1);}
    pointer_types[npointer_types]=(PointerType){kind,base,count};return 16+npointer_types++;
}
static int pointer_to(int base) {return derived_type(1,base,1);}
static int array_of(int base,int count) {return derived_type(2,base,count);}
static int is_derived(int type) {return type>=16 && type<16+npointer_types;}
static int is_pointer(int type) {return is_derived(type) && pointer_types[type-16].kind==1;}
static int is_array(int type) {return is_derived(type) && pointer_types[type-16].kind==2;}
static int type_size(int type);
static int expression_type(Node *n);
static int decay_type(int type);
static void binary_typed(int op,int left,int right);
static Node *top, **top_tail=&top;
static uint16_t code[65536];
static int ncode, legacy, serial, return_label;
enum { ORIGIN=0x300, MAX_SYMBOLS=4096 };
typedef struct { char *name; int address; } Label;
static Label labels[MAX_SYMBOLS];
static int nlabels;
typedef struct { int index; char *name; int addend; } Fixup;
static Fixup fixups[65536];
static int nfixups;
typedef struct { int index, length, flags; char *target; } Jump;
static Jump jumps[65536];
static int njumps;
typedef struct { char *name; int type,count,offset; Node *init; int base; } Variable;
static Variable globals[MAX_SYMBOLS], locals[MAX_SYMBOLS];
static int nglobals, nlocals;
static int scope_base, function_body;
static const char *current_function;
typedef struct { char *name; Node *node; int nargs; } Function;
static Function functions[MAX_SYMBOLS];
static int nfunctions;
typedef struct { char *name; char *value; int length; } String;
static String strings[4096];
static int nstrings;
static int loop_step[64], loop_end[64], loop_depth;
static int break_targets[64], break_depth;
static int compiling_compare_helper;

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
    ch=(unsigned char)*(*p)++;
    if(ch=='n') return '\n';
    if(ch=='r') return '\r';
    if(ch=='t') return '\t';
    if(ch=='b') return '\b';
    if(ch>='0'&&ch<='7') {
        int value=ch-'0'; for(int i=0;i<2&&**p>='0'&&**p<='7';i++) value=value*8+(*(*p)++-'0'); return value;
    }
    if(ch=='x') { int value=0, digits=0; while(isxdigit((unsigned char)**p)) {
        int c=tolower((unsigned char)*(*p)++);value=value*16+(isdigit(c)?c-'0':c-'a'+10);digits++;
    } if(!digits) error("invalid escape");return value; }
    if(ch=='\\'||ch=='\''||ch=='"') return ch;
    error("unsupported escape"); return 0;
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
            char *end; unsigned long value=strtoul(p,&end,0);
            if(end==p||value>65535)error("integer outside 16-bit range");
            p=end;t->type=T_NUM;t->number=(int)value;
            if(isalpha((unsigned char)*p)) error("integer suffixes unsupported");
        } else if(*p=='"'||*p=='\'') {
            int quote=*p++, count=0; char buffer[8192];
            while(*p&&*p!=quote) { if(count>=8190)error("string too long");int ch=escaped(&p);
                if(ch<0||ch>127)error("only ASCII strings supported");
                buffer[count++]=(char)ch; }
            if(*p!=quote)error("unterminated string");
            p++;buffer[count]=0;
            if(quote=='\'') { if(count!=1)error("character literal must contain one character");t->type=T_NUM;t->number=(unsigned char)buffer[0]; }
            else { t->type=T_STR;t->number=count;t->text=allocate((size_t)count+1);memcpy(t->text,buffer,(size_t)count+1); }
        } else {
            const char *pairs[]={"==","!=","<=",">=","&&","||","++","--","<<",">>","+=","-=","*=","/=","%=","&=","|=","^="};
            int kinds[]={T_EQ,T_NE,T_LE,T_GE,T_AND,T_OR,T_INC,T_DEC,T_SHL,T_SHR,T_ADDEQ,T_SUBEQ,T_MULEQ,T_DIVEQ,T_MODEQ,T_ANDEQ,T_OREQ,T_XOREQ};
            int matched=0;for(int i=0;i<18;i++)if(p[0]==pairs[i][0]&&p[1]==pairs[i][1]) { t->type=kinds[i];p+=2;matched=1;break; }
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
static int type_start(void) { return is("int")||is("char")||is("void")||is("const"); }
static int parse_base_type(void) {
    if(is("const"))at++;
    if(!is("int")&&!is("char")&&!is("void"))error("only int, char, void and pointers supported");
    int result=is("void")?3:is("char")?4:0;at++;
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
                n=node(N_NUM,NULL,NULL);n->value=type_size(type);
            } else {n=node(N_SIZEOF,expression(0),NULL);expect(')');}
        } else n=node(N_SIZEOF,expression(13),NULL);
    }
    else if(tok()==T_NUM) { n=node(N_NUM,NULL,NULL);n->value=tokens[at++].number; }
    else if(tok()==T_STR) { n=node(N_STR,NULL,NULL);n->name=tokens[at].text;n->value=tokens[at++].number;
        while(tok()==T_STR) { size_t a=(size_t)n->value,b=(size_t)tokens[at].number;char *s=allocate(a+b+1);
            memcpy(s,n->name,a);memcpy(s+a,tokens[at++].text,b+1);n->name=s;n->value=(int)(a+b); } }
    else if(tok()==T_ID) { n=node(N_ID,NULL,NULL);n->name=identifier(); }
    else if(take('(')) {
        if(type_start()) { int type=parse_type();if(is_array(type))error("cast to array type");expect(')');n=node(N_CAST,expression(13),NULL);n->type=type; }
        else { n=expression(0);expect(')'); }
    } else if(tok()=='-'||tok()=='+'||tok()=='!'||tok()=='~'||tok()=='&'||tok()=='*'||tok()==T_INC||tok()==T_DEC) {
        int op=tokens[at++].type;n=node(N_UNARY,expression(13),NULL);n->op=op;
    } else { error("expected expression");return NULL; }
    while(1) {
        if(take('[')) { n=node(N_INDEX,n,expression(0));expect(']'); }
        else if(take('(')) {
            if(n->kind!=N_ID)error("function pointers unsupported");
            Node *call=node(N_CALL,NULL,NULL);call->name=n->name;
            Node **tail=&call->a;if(tok()!=')')do { *tail=expression(1);tail=&(*tail)->next; }while(take(','));
            expect(')');n=call;
        } else if(tok()==T_INC||tok()==T_DEC) { int op=tokens[at++].type;n=node(N_UNARY,n,NULL);n->op=op;n->value=1; }
        else break;
    }
    return n;
}
static int precedence(int op) {
    switch(op) {
        case '=':case T_ADDEQ:case T_SUBEQ:case T_MULEQ:case T_DIVEQ:case T_MODEQ:case T_ANDEQ:case T_OREQ:case T_XOREQ:return 1;
        case ',':return 0;
        case T_OR:return 3;case T_AND:return 4;case '|':return 5;case '^':return 6;case '&':return 7;
        case T_EQ:case T_NE:return 8;case '<':case '>':case T_LE:case T_GE:return 9;
        case T_SHL:case T_SHR:return 10;case '+':case '-':return 11;case '*':case '/':case '%':return 12;
        default:return -1;
    }
}
static Node *expression(int minimum) {
    Node *left=primary();int op,level;
    while(1) {
        if(tok()=='?' && minimum<=2) {
            at++;Node *middle=expression(0);expect(':');
            Node *n=node(N_CONDITIONAL,left,middle);n->c=expression(2);left=n;continue;
        }
        level=precedence(op=tok());if(level<minimum || level<0)break;
        at++;Node *right=expression(level+(level==1?0:1));Node *n=node(level==1?N_ASSIGN:N_BINARY,left,right);n->op=op;left=n;
    }
    return left;
}
static int constant(Node *n) {
    if(n->kind==N_NUM)return (int16_t)n->value;
    if(n->kind==N_CAST && n->type!=3)return constant(n->a);
    if(n->kind==N_CONDITIONAL)return constant(n->a)?constant(n->b):constant(n->c);
    if(n->kind==N_UNARY) {
        int a=constant(n->a);
        if(n->op=='-')return (int16_t)-a;
        if(n->op=='+')return a;
        if(n->op=='!')return !a;
        if(n->op=='~')return (int16_t)~a;
    }
    if(n->kind==N_BINARY) {
        int a=constant(n->a);
        if(n->op==T_AND && !a)return 0;
        if(n->op==T_OR && a)return 1;
        int b=constant(n->b);
        switch(n->op) {
            case '+':return (int16_t)(a+b);case '-':return (int16_t)(a-b);
            case '*':return (int16_t)(a*b);
            case '/':case '%':if(!b)error("division by zero in constant expression");return (int16_t)(n->op=='/'?a/b:a%b);
            case '&':return a&b;case '|':return a|b;case '^':return a^b;
            case T_EQ:return a==b;case T_NE:return a!=b;
            case '<':return a<b;case '>':return a>b;case T_LE:return a<=b;case T_GE:return a>=b;
            case T_AND:return !!b;case T_OR:return !!b;
            case T_SHL:case T_SHR:
                if(b<0 || b>=16)error("invalid constant shift count");
                if(n->op==T_SHL)return (int16_t)((uint32_t)(uint16_t)a<<b);
                if(!b)return a;
                return (int16_t)(((uint16_t)a>>b)|(a<0?(0xffffu<<(16-b)):0));
            default:break;
        }
    }
    error("initializer/array size must be constant");return 0;
}
typedef struct {int kind,count;} DeclaratorPart;
static void declarator_parts(DeclaratorPart *parts,int *length,char **name,int abstract,int depth) {
    if(depth>32)error("declarator nesting exceeds 32 levels");
    int pointers=0;
    while(take('*')) {pointers++;if(is("const"))at++;if(pointers>64)error("too many pointer levels");}
    if(tok()==T_ID) {*name=identifier();}
    else if(take('(')) {declarator_parts(parts,length,name,abstract,depth+1);expect(')');}
    else if(!abstract)error("expected declarator name");
    while(take('[')) {
        int empty=tok()==']',bound=empty?0:constant(expression(1));expect(']');
        if(!empty && (bound<1 || bound>4096))error("array length must be 1..4096");
        if(*length==64)error("declarator exceeds 64 derived types");
        parts[(*length)++]=(DeclaratorPart){2,bound};
    }
    while(pointers--) {
        if(*length==64)error("declarator exceeds 64 derived types");
        parts[(*length)++]=(DeclaratorPart){1,1};
    }
}
static int parse_declarator(int base,char **name,int abstract) {
    DeclaratorPart parts[64];int length=0,dimensions=0;*name=NULL;
    declarator_parts(parts,&length,name,abstract,0);
    while(length--) {
        if(parts[length].kind==1)base=pointer_to(base);
        else {
            if(++dimensions>16)error("too many array dimensions");
            if(base==3)error("array of void");
            if(is_array(base) && !pointer_types[base-16].count)error("incomplete array element type");
            base=array_of(base,parts[length].count);
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
                expect('[');int index=constant(expression(1));expect(']');
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
    int inferred=0,braced=0;
    if(is_array(type)){n->type=2;n->count=pointer_types[type-16].count;n->op=pointer_types[type-16].base;inferred=!n->count;}
    if(type==3)error("void variable");
    if(parsing_parameter) {
        if(tok()=='=')error("parameter cannot have an initializer");
        if(n->type==2){n->type=pointer_to(n->op);n->count=1;}
        return n;
    }
    if(take('=')) {
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
    if(inferred && (!n->a || n->count<1 || n->count>4096))error("inferred array needs 1..4096 initialized elements");
    if(n->type==2 && type_size(array_of(n->op,n->count))>4096)error("array storage exceeds 4096 words");
    return n;
}
static Node *declaration_group(int base) {
    Node *group=node(N_DECL_GROUP,NULL,NULL);Node **tail=&group->a;
    do {
        char *name;int type=parse_declarator(base,&name,0);
        *tail=declaration(type,name);tail=&(*tail)->next;
    } while(take(','));
    return group;
}
static Node *statement(void) {
    if(is("switch")) {at++;expect('(');Node *n=node(N_SWITCH,expression(0),NULL);expect(')');n->b=statement();return n;}
    if(is("case") || is("default")) {
        Node *n=node(N_CASE,NULL,NULL);n->type=is("default");at++;
        if(!n->type)n->count=constant(expression(1));
        expect(':');n->b=statement();return n;
    }
    if(tok()==T_ID && tokens[at+1].type==':') {
        Node *n=node(N_LABEL,NULL,NULL);n->name=identifier();expect(':');n->b=statement();return n;
    }
    if(is("goto")) {at++;Node *n=node(N_GOTO,NULL,NULL);n->name=identifier();expect(';');return n;}
    if(take('{')) { Node *n=node(N_BLOCK,NULL,NULL);Node **tail=&n->a;
        while(tok()&&tok()!='}') { *tail=statement();tail=&(*tail)->next; }expect('}');return n; }
    if(is("if")) { at++;expect('(');Node *n=node(N_IF,expression(0),NULL);expect(')');n->b=statement();if(is("else")){at++;n->c=statement();}return n; }
    if(is("while")) { at++;expect('(');Node *n=node(N_WHILE,expression(0),NULL);expect(')');n->b=statement();return n; }
    if(is("do")) {
        at++;Node *n=node(N_DO,NULL,NULL);n->b=statement();
        if(!is("while"))error("expected while after do body");
        at++;expect('(');n->a=expression(0);expect(')');expect(';');return n;
    }
    if(is("for")) {
        at++;expect('(');Node *n=node(N_FOR,NULL,NULL);
        if(type_start()) { int base=parse_base_type();n->a=declaration_group(base); }
        else if(tok()!=';')n->a=expression(0);
        expect(';');
        if(tok()!=';')n->b=expression(0);
        expect(';');
        if(tok()!=')')n->c=expression(0);
        expect(')');n->d=statement();return n;
    }
    if(is("return")) { at++;Node *n=node(N_RETURN,NULL,NULL);if(tok()!=';')n->a=expression(0);expect(';');return n; }
    if(is("break")||is("continue")) { int kind=is("break")?N_BREAK:N_CONTINUE;at++;expect(';');return node(kind,NULL,NULL); }
    if(type_start()) {int base=parse_base_type();Node *n=declaration_group(base);expect(';');return n;}
    if(take(';'))return node(N_EMPTY,NULL,NULL);
    Node *n=expression(0);expect(';');return n;
}
static void parse(void) {
    while(tok()) {
        int base=parse_base_type();char *name;int type=parse_declarator(base,&name,0);Node *n;
        if(take('(')) {
            if(is_array(type))error("function cannot return an array");
            n=node(N_FUNCTION,NULL,NULL);n->name=name;n->type=type;Node **tail=&n->a;
            n->op=tok()==')';
            if(is("void")&&tokens[at+1].type==')')at++;
            else if(tok()!=')')do { char *p;int t=parse_declarator(parse_base_type(),&p,1);
                parsing_parameter=1;*tail=declaration(t,p);parsing_parameter=0;tail=&(*tail)->next; }while(take(','));
            expect(')');
            if(take(';'))n->kind=N_PROTOTYPE;
            else {n->b=statement();if(n->b->kind!=N_BLOCK)error("expected function body");}
        } else {
            n=declaration(type,name);*top_tail=n;top_tail=&n->next;
            while(take(',')) {
                type=parse_declarator(base,&name,0);n=declaration(type,name);*top_tail=n;top_tail=&n->next;
            }
            expect(';');continue;
        }
        *top_tail=n;top_tail=&n->next;
    }
}
static void emit(int value) { if(ncode>=65536)error("code too large");code[ncode++]=(uint16_t)value; }
static void define(const char *name) {
    for(int i=0;i<nlabels;i++)if(!strcmp(labels[i].name,name))error("duplicate label %s",name);
    if(nlabels==MAX_SYMBOLS)error("too many labels");
    labels[nlabels++]=(Label){copy(name),ORIGIN+ncode};
}
static void mark(int id) { define(label_name(id)); }
static void literal(int reg,int value,const char *name) {
    if(!name && value>=-64 && value<=63) {
        emit(0x2180|reg<<9);
        if(value)emit(0x1180|reg<<9|(value&127));
        return;
    }
    emit(0x4001|reg<<9);emit(0xae01);
    if(name) { fixups[nfixups++]=(Fixup){ncode,copy(name),0};emit(0); }else emit(value);
}
static void move(int dst,int src) { emit(0x1080|dst<<9|src<<4); }
static void addi(int reg,int value) {
    if(value>=-64&&value<=63)emit(0x1180|reg<<9|(value&127));
    else { literal(2,value,NULL);emit(0x1000|reg<<9|reg<<4|2<<1); }
}
static void push(int reg) { addi(6,-1);emit(0x9000|reg<<9|6<<6); }
static void pop(int reg) { emit(0x6000|reg<<9|6<<6);addi(6,1); }
static void jump_name(const char *name) {
    jumps[njumps++]=(Jump){ncode,4,7,copy(name)};
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
    jumps[njumps++]=(Jump){ncode,4,8,copy(name)};
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
            int target=symbol_address(j->target)-ORIGIN;
            if(target>start)target-=removed;
            int offset=target-start-1;
            int range=j->flags==8?1024:256;
            if(offset < -range || offset >= range)continue;
            memmove(code+start+1,code+start+old,(size_t)(ncode-start-old)*sizeof(*code));
            ncode-=removed;
            for(int k=0;k<nlabels;k++)if(labels[k].address>ORIGIN+start)labels[k].address-=removed;
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
        Jump *j=&jumps[i];int offset=symbol_address(j->target)-ORIGIN-j->index-1;
        int range=j->flags==8?1024:256;
        if(offset < -range || offset >= range)error("relative transfer out of range");
        if(j->flags==8)code[j->index]=(uint16_t)((legacy?0xc000:0xc800)|(offset&2047));
        else code[j->index]=(uint16_t)(0xa000|j->flags<<9|(offset&511));
    }
}
static void negate(int reg) { emit(0x3100|reg<<9);addi(reg,1); }
static int fresh(void) { return ++serial; }
static Variable *variable(const char *name,int *global) {
    for(int i=nlocals-1;i>=0;i--)if(!strcmp(locals[i].name,name)){*global=0;return &locals[i];}
    for(int i=0;i<nglobals;i++)if(!strcmp(globals[i].name,name)){*global=1;return &globals[i];}
    error("unknown variable %s",name);return NULL;
}
static Function *function(const char *name) {
    for(int i=0;i<nfunctions;i++)if(!strcmp(functions[i].name,name))return &functions[i];
    error("undefined function %s",name);return NULL;
}
static void expr(Node *n);
static void address(Node *n) {
    if(n->kind==N_ID) { int global;Variable *v=variable(n->name,&global);
        if(global)literal(0,0,named("g.",n->name));else { move(0,5);addi(0,v->offset); } }
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
    else if(helper(op)) { push(0);push(1);call(named("fn.",helper(op)));addi(6,2); }
    else if(!compiling_compare_helper) {
        push(0);push(1);
        const char *name=op=='<'?"__cmp_lt":op=='>'?"__cmp_gt":op==T_LE?"__cmp_le":op==T_GE?"__cmp_ge":op==T_EQ?"__cmp_eq":"__cmp_ne";
        call(named("fn.",name));addi(6,2);
    } else compare(op);
}
static void args_reverse(Node *arg) { if(!arg)return;args_reverse(arg->next);expr(arg);push(0); }
static const char *string_label(Node *n) {
    for(int i=0;i<nstrings;i++)if(strings[i].length==n->value && !memcmp(strings[i].value,n->name,(size_t)n->value))return strings[i].name;
    if(nstrings==4096)error("too many strings");
    char *name=label_name(fresh());strings[nstrings++]=(String){name,n->name,n->value};return name;
}
static int global_address(Node *n,const char **name,int *offset);
static int global_object_address(Node *n,const char **name,int *offset) {
    if(n->kind==N_ID) {
        for(int i=0;i<nglobals;i++)if(!strcmp(globals[i].name,n->name)) {
            *name=named("g.",n->name);*offset=0;return 1;
        }
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
    if(is_derived(type) && pointer_types[type-16].kind==2)return pointer_to(pointer_types[type-16].base);
    return type;
}
static int expression_type(Node *n) {
    switch(n->kind) {
        case N_NUM:return 0;
        case N_STR:return array_of(4,n->value+1);
        case N_ID:{int global;Variable *v=variable(n->name,&global);return v->type==2?array_of(v->base,v->count):v->type;}
        case N_CAST:return n->type;
        case N_SIZEOF:return 0;
        case N_CALL:
            for(Node *d=top;d;d=d->next)if((d->kind==N_FUNCTION || d->kind==N_PROTOTYPE) && !strcmp(d->name,n->name))return d->type;
            error("unknown function in sizeof: %s",n->name);return 0;
        case N_INDEX:{
            int t=decay_type(expression_type(n->a)),other=decay_type(expression_type(n->b));
            if(is_pointer(t) && is_pointer(other))error("subscript needs one integer operand");
            if(!is_pointer(t))t=other;
            if(!is_pointer(t))error("subscript requires pointer/array");
            return pointer_types[t-16].base;
        }
        case N_UNARY:{
            int t=expression_type(n->a);
            if(n->op=='&')return pointer_to(t);
            t=decay_type(t);
            if(n->op=='*'){if(!is_pointer(t))error("dereference requires pointer");return pointer_types[t-16].base;}
            if(n->op==T_INC || n->op==T_DEC)return t;
            return 0;
        }
        case N_ASSIGN:return decay_type(expression_type(n->a));
        case N_BINARY:{
            if(n->op==',')return decay_type(expression_type(n->b));
            int a=decay_type(expression_type(n->a)),b=decay_type(expression_type(n->b));
            if(n->op=='+' || n->op=='-') {
                if(is_pointer(a) && is_pointer(b) && (n->op=='+' || a!=b))error("invalid pointer arithmetic");
                if(n->op=='-' && !is_pointer(a) && is_pointer(b))error("cannot subtract pointer from integer");
                if(is_pointer(a))type_size(pointer_types[a-16].base);
                if(is_pointer(b))type_size(pointer_types[b-16].base);
            }
            if(n->op=='+' && is_pointer(b))return b;
            if((n->op=='+' || n->op=='-') && is_pointer(a) && !is_pointer(b))return a;
            return 0;
        }
        case N_CONDITIONAL:{
            int a=decay_type(expression_type(n->b)),b=decay_type(expression_type(n->c));
            if(a==3 || b==3)return 3;
            if(is_pointer(a))return a;
            if(is_pointer(b))return b;
            return 0;
        }
        default:error("unsupported sizeof operand");return 0;
    }
}
static int type_size(int type) {
    if(type==3)error("sizeof void expression is invalid");
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
    if((op=='+' || op=='-') && (is_pointer(left) || is_pointer(right))) {
        if(is_pointer(left) && is_pointer(right)) {
            if(op!='-' || left!=right)error("invalid pointer arithmetic");
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
    case N_ID: { int global;Variable *v=variable(n->name,&global);address(n);if(v->type!=2)emit(0x6000);break; }
    case N_INDEX:address(n);if(!is_array(expression_type(n)))emit(0x6000);break;
    case N_CAST:expr(n->a);break;
    case N_SIZEOF:{
        literal(0,type_size(expression_type(n->a)),NULL);break;
    }
    case N_UNARY:
        if(n->op=='&')address(n->a);
        else if(n->op==T_INC||n->op==T_DEC) {address(n->a);push(0);emit(0x6000);if(n->value)move(4,0);
            int type=expression_type(n->a);if(is_array(type))error("array is not modifiable");
            int stride=is_pointer(type)?type_size(pointer_types[type-16].base):1;
            addi(0,n->op==T_INC?stride:-stride);pop(1);emit(0x9040);if(n->value)move(0,4);}
        else {expr(n->a);if(n->op=='*'){int t=expression_type(n);if(t==3)error("cannot load void");if(!is_array(t))emit(0x6000);}else if(n->op=='-')negate(0);else if(n->op=='~')emit(0x3100);
            else if(n->op=='!'){literal(1,0,NULL);compare(T_EQ);}else if(n->op!='+')error("unsupported unary operator");}
        break;
    case N_BINARY:
        if(n->op==','){expr(n->a);expr(n->b);}
        else if(n->op==T_AND||n->op==T_OR) {int shortcut=fresh(),end=fresh();expr(n->a);move(0,0);branch(n->op==T_AND?2:5,shortcut);
            expr(n->b);literal(1,0,NULL);compare(T_NE);jump(end);mark(shortcut);literal(0,n->op==T_OR,NULL);mark(end);}
        else {expr(n->a);push(0);expr(n->b);pop(1);binary_typed(n->op,expression_type(n->a),expression_type(n->b));}break;
    case N_CONDITIONAL:{
        int other=fresh(),end=fresh();expr(n->a);move(0,0);branch(2,other);
        expr(n->b);jump(end);mark(other);expr(n->c);mark(end);break;
    }
    case N_ASSIGN: {
        if(is_array(expression_type(n->a)))error("array is not assignable");
        address(n->a);push(0);
        if(n->op!='='){emit(0x6000);push(0);}
        expr(n->b);
        if(n->op!='=') { pop(1);int op=n->op==T_ADDEQ?'+':n->op==T_SUBEQ?'-':n->op==T_MULEQ?'*':n->op==T_DIVEQ?'/':n->op==T_MODEQ?'%':n->op==T_ANDEQ?'&':n->op==T_OREQ?'|':'^';binary_typed(op,expression_type(n->a),expression_type(n->b)); }
        pop(1);emit(0x9040);break; }
    case N_CALL: {
        int count=0;for(Node *a=n->a;a;a=a->next)count++;
        if(!strcmp(n->name,"bob_address")){if(count!=1||n->a->kind!=N_ID)error("bob_address requires a function name");function(n->a->name);literal(0,0,named("fn.",n->a->name));break;}
        if(!strcmp(n->name,"bob_call")){if(count!=1)error("bob_call needs one argument");expr(n->a);move(3,0);emit(legacy?0xcb00:0xc300);break;}
        int vector=-1,expected=0;
        if(!strcmp(n->name,"bob_halt"))vector=0;
        else if(!strcmp(n->name,"bob_putc")){vector=1;expected=1;}
        else if(!strcmp(n->name,"bob_puts")){vector=2;expected=1;}
        else if(!strcmp(n->name,"bob_gets")){vector=3;expected=2;}
        else if(!strcmp(n->name,"bob_getc"))vector=5;
        else if(!strcmp(n->name,"bob_run")){vector=6;expected=1;}
        else if(!strcmp(n->name,"bob_snapshot")){vector=7;expected=2;}
        else if(!strcmp(n->name,"bob_key"))vector=8;
        else if(!strcmp(n->name,"bob_terminal"))vector=9;
        else if(!strcmp(n->name,"bob_columns"))vector=10;
        else if(!strcmp(n->name,"bob_rows"))vector=11;
        if(vector>=0) {if(count!=expected)error("wrong argument count for %s",n->name);
            if(expected==2){expr(n->a->next);push(0);expr(n->a);pop(1);}else if(count)expr(n->a);
            emit(0xf000|vector<<8);break;}
        Function *f=function(n->name);if(count!=f->nargs)error("wrong argument count for %s",n->name);
        args_reverse(n->a);call(named("fn.",n->name));addi(6,count);break; }
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
static void stmt(Node *n) {
    if(!n)return;
    switch(n->kind) {
    case N_DECL_GROUP:for(Node *d=n->a;d;d=d->next)stmt(d);break;
    case N_LABEL:define(user_label(n->name));stmt(n->b);break;
    case N_GOTO:jump_name(user_label(n->name));break;
    case N_CASE:if(!n->value)error("case/default outside switch");mark(n->value);stmt(n->b);break;
    case N_SWITCH:{
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
        for(int i=scope_base;i<nlocals;i++)if(!strcmp(locals[i].name,n->name))error("duplicate local %s",n->name);
        if(nlocals==MAX_SYMBOLS)error("too many locals");
        locals[nlocals++]=(Variable){n->name,n->type,n->count,n->value,n->a,n->op};
        if(n->a){
        Node v={0};v.kind=N_ID;v.name=n->name;
        if(n->type!=2){address(&v);push(0);expr(n->a);pop(1);emit(0x9040);}
        else {
            int index=0,total=type_size(array_of(n->op,n->count));
            for(Node *value=n->a;value;value=value->next) {
                if(index==total)error("too many array initializers");
                address(&v);addi(0,index++);push(0);expr(value);pop(1);emit(0x9040);
            }
            if(index<total) {
                address(&v);addi(0,index);move(1,0);literal(2,total-index,NULL);literal(0,0,NULL);
                int loop=fresh();mark(loop);emit(0x9040);addi(1,1);addi(2,-1);branch(5,loop);
            }
        }
    }break;
    case N_RETURN:if(n->a)expr(n->a);else literal(0,0,NULL);jump(return_label);break;
    case N_IF:{int other=fresh(),end=fresh();expr(n->a);move(0,0);branch(2,other);stmt(n->b);jump(end);mark(other);stmt(n->c);mark(end);break;}
    case N_DO:{
        int start=fresh(),step=fresh(),end=fresh();
        if(loop_depth==64)error("loops nested too deeply");
        if(break_depth==64)error("break contexts nested too deeply");
        mark(start);loop_step[loop_depth]=step;loop_end[loop_depth++]=end;break_targets[break_depth++]=end;
        stmt(n->b);loop_depth--;break_depth--;mark(step);expr(n->a);move(0,0);branch(5,start);mark(end);break;
    }
    case N_WHILE:case N_FOR:{int start=fresh(),step=fresh(),end=fresh();
        int saved=nlocals,base=scope_base;
        if(n->kind==N_FOR)scope_base=nlocals;
        if(n->kind==N_FOR)stmt(n->a);
        mark(start);Node *condition=n->kind==N_FOR?n->b:n->a;
        if(condition){expr(condition);move(0,0);branch(2,end);}if(loop_depth==64)error("loops nested too deeply");
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
    if(n->kind==N_DECL){frame_slots+=n->type==2?type_size(array_of(n->op,n->count)):1;n->value=-frame_slots;}
    collect(n->a);collect(n->b);collect(n->c);collect(n->d);collect(n->next);
}
static void compatible_function(Node *a,Node *b) {
    if(a->type!=b->type)error("conflicting return type for %s",a->name);
    if((a->kind==N_PROTOTYPE && a->op) || (b->kind==N_PROTOTYPE && b->op))return;
    Node *x=a->a,*y=b->a;
    while(x && y){if(x->type!=y->type)error("conflicting parameter type for %s",a->name);x=x->next;y=y->next;}
    if(x || y)error("conflicting parameter count for %s",a->name);
}
static void generate(void) {
    for(Node *n=top;n;n=n->next) {
        if(n->kind==N_FUNCTION){if(nfunctions==MAX_SYMBOLS)error("too many functions");int count=0;for(Node *a=n->a;a;a=a->next)count++;
            for(int i=0;i<nfunctions;i++)if(!strcmp(functions[i].name,n->name))error("duplicate function %s",n->name);
            functions[nfunctions++]=(Function){n->name,n,count};}
        else if(n->kind==N_DECL){if(nglobals==MAX_SYMBOLS)error("too many globals");for(int i=0;i<nglobals;i++)if(!strcmp(globals[i].name,n->name))error("duplicate global %s",n->name);
            globals[nglobals++]=(Variable){n->name,n->type,n->count,0,n->a,n->op};}
    }
    for(Node *n=top;n;n=n->next)if(n->kind==N_PROTOTYPE || n->kind==N_FUNCTION) {
        for(Node *other=n->next;other;other=other->next)
            if((other->kind==N_PROTOTYPE || other->kind==N_FUNCTION) && !strcmp(n->name,other->name))compatible_function(n,other);
        for(int i=0;i<nglobals;i++)if(!strcmp(n->name,globals[i].name))error("object/function name collision %s",n->name);
    }
    if(function("main")->nargs)error("main must take no arguments");
    define("entry");literal(6,0xf000,NULL);literal(5,0,NULL);call("fn.main");emit(0xf000);
    for(int i=0;i<nfunctions;i++) {
        Function *f=&functions[i];current_function=f->name;nlocals=0;frame_slots=0;int arg=0;
        compiling_compare_helper=!strncmp(f->name,"__cmp_",6);
        for(Node *a=f->node->a;a;a=a->next){
            if(!*a->name||a->type==2)error("named scalar/pointer parameters required");
            for(int j=0;j<nlocals;j++)if(!strcmp(locals[j].name,a->name))error("duplicate parameter %s",a->name);
            locals[nlocals++]=(Variable){a->name,a->type,1,2+arg++,NULL,a->op};
        }
        collect(f->node->b);if(frame_slots>4096)error("frame too large");return_label=fresh();define(named("fn.",f->name));
        scope_base=0;function_body=1;
        push(7);push(5);move(5,6);addi(6,-frame_slots);stmt(f->node->b);literal(0,0,NULL);mark(return_label);move(6,5);pop(5);pop(7);emit(0xe000);
    }
    nlocals=0;
    for(int i=0;i<nglobals;i++){Variable *g=&globals[i];define(named("g.",g->name));Node *init=g->init;
        int total=g->type==2?type_size(array_of(g->base,g->count)):g->count;
        int element=g->type==2?g->base:g->type;
        while(is_array(element))element=pointer_types[element-16].base;
        for(int j=0;j<total;j++){
            const char *target=NULL;int offset=0;
            if(init && is_pointer(element) && global_address(init,&target,&offset)) {
                fixups[nfixups++]=(Fixup){ncode,copy(target),offset};emit(0);
            } else emit(init?constant(init):0);
            if(init)init=init->next;
        }if(init)error("too many initializers for %s",g->name);}
    for(int i=0;i<nstrings;i++){define(strings[i].name);for(int j=0;j<strings[i].length;j++)emit((unsigned char)strings[i].value[j]);emit(0);}
    compact_jumps();
    if(ORIGIN+ncode>0x9000)error("kernel exceeds 0x9000 (code + data is %d words)",ncode);
    for(int i=0;i<nfixups;i++){int found=0;for(int j=0;j<nlabels;j++)if(!strcmp(fixups[i].name,labels[j].name)) {code[fixups[i].index]=(uint16_t)(labels[j].address+fixups[i].addend);found=1;break;}
        if(!found)error("unresolved symbol %s",fixups[i].name);}
}
static char *read_file(const char *path) {
    FILE *f=fopen(path,"rb");if(!f){perror(path);exit(1);}fseek(f,0,SEEK_END);long length=ftell(f);rewind(f);
    if(length<0||length>4000000)error("input too large");
    char *s=allocate((size_t)length+1);
    if(fread(s,1,(size_t)length,f)!=(size_t)length)error("read failure");
    fclose(f);return s;
}
static void word(FILE *f,int value) { fputc(value&255,f);fputc((value>>8)&255,f); }
int main(int argc,char **argv) {
    if(argc<4||argc>5){fprintf(stderr,"Usage: bobcc input.i output.basm output.b16 [--legacy]\n");return 1;}
    if(argc==5){if(strcmp(argv[4],"--legacy"))error("unknown option");legacy=1;}
    lex(read_file(argv[1]));parse();
    /* Parse helper bodies in a second token buffer, preserving the AST. */
    memset(tokens,0,sizeof(tokens));ntokens=at=0;lex(read_file("tools/arithmetic.c"));parse();generate();
    FILE *basm=fopen(argv[2],"wb");if(!basm){perror(argv[2]);return 1;}
    fprintf(basm,"; bob16 C kernel, generated by tools/bobcc.c\n.fill 4601\n.fill b600\n.fill 0300\n");
    for(int i=3;i<ORIGIN;i++)fprintf(basm,".fill 0000\n");
    for(int i=0;i<ncode;i++)fprintf(basm,".fill %04x\n",code[i]);
    fclose(basm);FILE *binary=fopen(argv[3],"wb");if(!binary){perror(argv[3]);return 1;}
    unsigned sum=0;for(int i=0;i<ncode;i++)sum+=code[i];fwrite("B16K",1,4,binary);word(binary,1);word(binary,ORIGIN);word(binary,ORIGIN);word(binary,ncode);word(binary,(int)sum);
    for(int i=0;i<ncode;i++)word(binary,code[i]);
    fclose(binary);
    char map_path[4096];
    if(strlen(argv[3])+5>=sizeof(map_path))error("output path too long");
    strcpy(map_path,argv[3]);char *extension=strrchr(map_path,'.');
    if(extension)strcpy(extension,".map");else strcat(map_path,".map");
    FILE *map=fopen(map_path,"w");if(map){for(int i=0;i<nlabels;i++)fprintf(map,"%04x %s\n",labels[i].address,labels[i].name);fclose(map);}
    printf("Compiled %d bob16 words.\n",ncode);return 0;
}
