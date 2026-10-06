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
       N_EMPTY, N_CAST, N_FUNCTION };
typedef struct Node Node;
struct Node { int kind, op, value, type, count; char *name; Node *a,*b,*c,*d,*next; };
typedef struct { int type, number, line; char *text; } Token;
static Token tokens[65536];
static int ntokens, at;
static Node *top, **top_tail=&top;
static uint16_t code[65536];
static int ncode, legacy, serial, return_label;
enum { ORIGIN=0x300, MAX_SYMBOLS=4096 };
typedef struct { char *name; int address; } Label;
static Label labels[MAX_SYMBOLS];
static int nlabels;
typedef struct { int index; char *name; } Fixup;
static Fixup fixups[65536];
static int nfixups;
typedef struct { int index, length, flags; char *target; } Jump;
static Jump jumps[65536];
static int njumps;
typedef struct { char *name; int type,count,offset; Node *init; } Variable;
static Variable globals[MAX_SYMBOLS], locals[MAX_SYMBOLS];
static int nglobals, nlocals;
typedef struct { char *name; Node *node; int nargs; } Function;
static Function functions[MAX_SYMBOLS];
static int nfunctions;
typedef struct { char *name; char *value; } String;
static String strings[4096];
static int nstrings;
static int loop_step[64], loop_end[64], loop_depth;
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
            else { t->type=T_STR;t->text=copy(buffer); }
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
static int parse_type(void) {
    if(is("const"))at++;
    if(!is("int")&&!is("char")&&!is("void"))error("only int, char, void and pointers supported");
    int result=is("void")?3:0;at++;
    while(take('*')) { result=1;if(is("const"))at++; }
    return result;
}
static Node *expression(int minimum);
static Node *statement(void);
static Node *primary(void) {
    Node *n;
    if(tok()==T_NUM) { n=node(N_NUM,NULL,NULL);n->value=tokens[at++].number; }
    else if(tok()==T_STR) { n=node(N_STR,NULL,NULL);n->name=tokens[at++].text;
        while(tok()==T_STR) { size_t a=strlen(n->name),b=strlen(tokens[at].text);char *s=allocate(a+b+1);
            memcpy(s,n->name,a);memcpy(s+a,tokens[at++].text,b+1);n->name=s; } }
    else if(tok()==T_ID) { n=node(N_ID,NULL,NULL);n->name=identifier(); }
    else if(take('(')) {
        if(type_start()) { int type=parse_type();expect(')');n=node(N_CAST,expression(13),NULL);n->type=type; }
        else { n=expression(1);expect(')'); }
    } else if(tok()=='-'||tok()=='+'||tok()=='!'||tok()=='~'||tok()=='&'||tok()=='*'||tok()==T_INC||tok()==T_DEC) {
        int op=tokens[at++].type;n=node(N_UNARY,expression(13),NULL);n->op=op;
    } else { error("expected expression");return NULL; }
    while(1) {
        if(take('[')) { n=node(N_INDEX,n,expression(1));expect(']'); }
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
        case T_OR:return 2;case T_AND:return 3;case '|':return 4;case '^':return 5;case '&':return 6;
        case T_EQ:case T_NE:return 7;case '<':case '>':case T_LE:case T_GE:return 8;
        case T_SHL:case T_SHR:return 9;case '+':case '-':return 10;case '*':case '/':case '%':return 11;
        default:return 0;
    }
}
static Node *expression(int minimum) {
    Node *left=primary();int op,level;
    while((level=precedence(op=tok()))>=minimum&&level) {
        at++;Node *right=expression(level+(level==1?0:1));Node *n=node(level==1?N_ASSIGN:N_BINARY,left,right);n->op=op;left=n;
    }
    return left;
}
static int constant(Node *n) {
    if(n->kind==N_NUM)return n->value;
    if(n->kind==N_UNARY&&n->op=='-')return -constant(n->a);
    error("initializer/array size must be constant");return 0;
}
static Node *declaration(int type,char *name) {
    Node *n=node(N_DECL,NULL,NULL);n->name=name;n->type=type;n->count=1;
    if(take('[')) { n->count=constant(expression(1));expect(']');n->type=2;
        if(n->count<1||n->count>4096)error("array length must be 1..4096"); }
    if(type==3)error("void variable");
    if(take('=')) {
        if(take('{')) { Node **tail=&n->a;do { *tail=expression(1);tail=&(*tail)->next; }while(take(',')&&tok()!='}');expect('}'); }
        else n->a=expression(1);
    }
    return n;
}
static Node *statement(void) {
    if(take('{')) { Node *n=node(N_BLOCK,NULL,NULL);Node **tail=&n->a;
        while(tok()&&tok()!='}') { *tail=statement();tail=&(*tail)->next; }expect('}');return n; }
    if(is("if")) { at++;expect('(');Node *n=node(N_IF,expression(1),NULL);expect(')');n->b=statement();if(is("else")){at++;n->c=statement();}return n; }
    if(is("while")) { at++;expect('(');Node *n=node(N_WHILE,expression(1),NULL);expect(')');n->b=statement();return n; }
    if(is("for")) {
        at++;expect('(');Node *n=node(N_FOR,NULL,NULL);
        if(type_start()) { int t=parse_type();n->a=declaration(t,identifier()); }
        else if(tok()!=';')n->a=expression(1);
        expect(';');
        if(tok()!=';')n->b=expression(1);
        expect(';');
        if(tok()!=')')n->c=expression(1);
        expect(')');n->d=statement();return n;
    }
    if(is("return")) { at++;Node *n=node(N_RETURN,NULL,NULL);if(tok()!=';')n->a=expression(1);expect(';');return n; }
    if(is("break")||is("continue")) { int kind=is("break")?N_BREAK:N_CONTINUE;at++;expect(';');return node(kind,NULL,NULL); }
    if(type_start()) { int t=parse_type();Node *n=declaration(t,identifier());expect(';');return n; }
    if(take(';'))return node(N_EMPTY,NULL,NULL);
    Node *n=expression(1);expect(';');return n;
}
static void parse(void) {
    while(tok()) {
        int type=parse_type();char *name=identifier();Node *n;
        if(take('(')) {
            n=node(N_FUNCTION,NULL,NULL);n->name=name;n->type=type;Node **tail=&n->a;
            if(is("void")&&tokens[at+1].type==')')at++;
            else if(tok()!=')')do { int t=parse_type();char *p=tok()==T_ID?identifier():copy("");
                *tail=declaration(t,p);tail=&(*tail)->next; }while(take(','));
            expect(')');if(take(';'))continue;n->b=statement();if(n->b->kind!=N_BLOCK)error("expected function body");
        } else { n=declaration(type,name);expect(';'); }
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
    if(name) { fixups[nfixups++]=(Fixup){ncode,copy(name)};emit(0); }else emit(value);
}
static void move(int dst,int src) { emit(0x1080|dst<<9|src<<4); }
static void addi(int reg,int value) {
    if(value>=-64&&value<=63)emit(0x1180|reg<<9|(value&127));
    else { literal(2,value,NULL);emit(0x1000|reg<<9|reg<<4|2<<1); }
}
static void push(int reg) { addi(6,-1);emit(0x9000|reg<<9|6<<6); }
static void pop(int reg) { emit(0x6000|reg<<9|6<<6);addi(6,1); }
static void jump(int id) {
    jumps[njumps++]=(Jump){ncode,4,7,copy(label_name(id))};
    literal(3,0,label_name(id));emit(0xb600);
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
    for(int i=0;i<nlocals;i++)if(!strcmp(locals[i].name,name)){*global=0;return &locals[i];}
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
    else if(n->kind==N_INDEX) { expr(n->a);push(0);expr(n->b);pop(1);emit(0x1010); }
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
static void expr(Node *n) {
    if(!n)error("missing expression");
    switch(n->kind) {
    case N_NUM:literal(0,n->value,NULL);break;
    case N_STR: {
        int existing=-1;for(int i=0;i<nstrings;i++)if(!strcmp(strings[i].value,n->name)){existing=i;break;}
        if(existing>=0){literal(0,0,strings[existing].name);break;}
        if(nstrings==4096)error("too many strings");
        char *name=label_name(fresh());strings[nstrings++]=(String){name,n->name};literal(0,0,name);break; }
    case N_ID: { int global;Variable *v=variable(n->name,&global);address(n);if(v->type!=2)emit(0x6000);break; }
    case N_INDEX:address(n);emit(0x6000);break;
    case N_CAST:expr(n->a);break;
    case N_UNARY:
        if(n->op=='&')address(n->a);
        else if(n->op==T_INC||n->op==T_DEC) {address(n->a);push(0);emit(0x6000);if(n->value)move(4,0);
            addi(0,n->op==T_INC?1:-1);pop(1);emit(0x9040);if(n->value)move(0,4);}
        else {expr(n->a);if(n->op=='*')emit(0x6000);else if(n->op=='-')negate(0);else if(n->op=='~')emit(0x3100);
            else if(n->op=='!'){literal(1,0,NULL);compare(T_EQ);}else if(n->op!='+')error("unsupported unary operator");}
        break;
    case N_BINARY:
        if(n->op==T_AND||n->op==T_OR) {int shortcut=fresh(),end=fresh();expr(n->a);move(0,0);branch(n->op==T_AND?2:5,shortcut);
            expr(n->b);literal(1,0,NULL);compare(T_NE);jump(end);mark(shortcut);literal(0,n->op==T_OR,NULL);mark(end);}
        else {expr(n->a);push(0);expr(n->b);pop(1);binary(n->op);}break;
    case N_ASSIGN: {
        address(n->a);push(0);
        if(n->op!='='){emit(0x6000);push(0);}
        expr(n->b);
        if(n->op!='=') { pop(1);int op=n->op==T_ADDEQ?'+':n->op==T_SUBEQ?'-':n->op==T_MULEQ?'*':n->op==T_DIVEQ?'/':n->op==T_MODEQ?'%':n->op==T_ANDEQ?'&':n->op==T_OREQ?'|':'^';binary(op); }
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
        if(vector>=0) {if(count!=expected)error("wrong argument count for %s",n->name);
            if(vector==3){expr(n->a->next);push(0);expr(n->a);pop(1);}else if(count)expr(n->a);
            emit(0xf000|vector<<8);break;}
        Function *f=function(n->name);if(count!=f->nargs)error("wrong argument count for %s",n->name);
        args_reverse(n->a);call(named("fn.",n->name));addi(6,count);break; }
    default:error("unsupported expression kind %d",n->kind);
    }
}
static void stmt(Node *n) {
    if(!n)return;
    switch(n->kind) {
    case N_BLOCK:for(Node *s=n->a;s;s=s->next)stmt(s);break;
    case N_DECL:if(n->a){if(n->type==2)error("local array initializers unsupported");Node v={0};v.kind=N_ID;v.name=n->name;address(&v);push(0);expr(n->a);pop(1);emit(0x9040);}break;
    case N_RETURN:if(n->a)expr(n->a);else literal(0,0,NULL);jump(return_label);break;
    case N_IF:{int other=fresh(),end=fresh();expr(n->a);move(0,0);branch(2,other);stmt(n->b);jump(end);mark(other);stmt(n->c);mark(end);break;}
    case N_WHILE:case N_FOR:{int start=fresh(),step=fresh(),end=fresh();
        if(n->kind==N_FOR)stmt(n->a);
        mark(start);Node *condition=n->kind==N_FOR?n->b:n->a;
        if(condition){expr(condition);move(0,0);branch(2,end);}if(loop_depth==64)error("loops nested too deeply");
        loop_step[loop_depth]=step;loop_end[loop_depth++]=end;stmt(n->kind==N_FOR?n->d:n->b);loop_depth--;
        mark(step);if(n->kind==N_FOR&&n->c)expr(n->c);jump(start);mark(end);break;}
    case N_BREAK:case N_CONTINUE:if(!loop_depth)error("break/continue outside loop");jump(n->kind==N_BREAK?loop_end[loop_depth-1]:loop_step[loop_depth-1]);break;
    case N_EMPTY:break;
    default:expr(n);
    }
}
static int frame_slots;
static void collect(Node *n) {
    if(!n)return;
    if(n->kind==N_DECL){for(int i=0;i<nlocals;i++)if(!strcmp(locals[i].name,n->name))error("shadowing/duplicate local %s",n->name);
        if(nlocals==MAX_SYMBOLS)error("too many locals");
        frame_slots+=n->count;locals[nlocals++]=(Variable){n->name,n->type,n->count,-frame_slots,n->a};}
    collect(n->a);collect(n->b);collect(n->c);collect(n->d);collect(n->next);
}
static void generate(void) {
    for(Node *n=top;n;n=n->next) {
        if(n->kind==N_FUNCTION){if(nfunctions==MAX_SYMBOLS)error("too many functions");int count=0;for(Node *a=n->a;a;a=a->next)count++;
            for(int i=0;i<nfunctions;i++)if(!strcmp(functions[i].name,n->name))error("duplicate function %s",n->name);
            functions[nfunctions++]=(Function){n->name,n,count};}
        else {if(nglobals==MAX_SYMBOLS)error("too many globals");for(int i=0;i<nglobals;i++)if(!strcmp(globals[i].name,n->name))error("duplicate global %s",n->name);
            globals[nglobals++]=(Variable){n->name,n->type,n->count,0,n->a};}
    }
    if(function("main")->nargs)error("main must take no arguments");
    define("entry");literal(6,0xf000,NULL);literal(5,0,NULL);call("fn.main");emit(0xf000);
    for(int i=0;i<nfunctions;i++) {
        Function *f=&functions[i];nlocals=0;frame_slots=0;int arg=0;
        compiling_compare_helper=!strncmp(f->name,"__cmp_",6);
        for(Node *a=f->node->a;a;a=a->next){if(!*a->name||a->type==2)error("named scalar/pointer parameters required");locals[nlocals++]=(Variable){a->name,a->type,1,2+arg++,NULL};}
        collect(f->node->b);if(frame_slots>4096)error("frame too large");return_label=fresh();define(named("fn.",f->name));
        push(7);push(5);move(5,6);addi(6,-frame_slots);stmt(f->node->b);literal(0,0,NULL);mark(return_label);move(6,5);pop(5);pop(7);emit(0xe000);
    }
    for(int i=0;i<nglobals;i++){Variable *g=&globals[i];define(named("g.",g->name));Node *init=g->init;
        for(int j=0;j<g->count;j++){emit(init?constant(init):0);if(init)init=init->next;}if(init)error("too many initializers for %s",g->name);}
    for(int i=0;i<nstrings;i++){define(strings[i].name);for(char *p=strings[i].value;*p;p++)emit((unsigned char)*p);emit(0);}
    compact_jumps();
    if(ORIGIN+ncode>0x9000)error("kernel exceeds 0x9000 (code + data is %d words)",ncode);
    for(int i=0;i<nfixups;i++){int found=0;for(int j=0;j<nlabels;j++)if(!strcmp(fixups[i].name,labels[j].name)) {code[fixups[i].index]=(uint16_t)labels[j].address;found=1;break;}
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
