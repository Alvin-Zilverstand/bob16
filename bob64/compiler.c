#include "compiler.h"

#define COMPILER_FUNCTION_LIMIT 16u
#define COMPILER_CALL_LIMIT 64u
#define COMPILER_ARGUMENT_LIMIT 16u
#define COMPILER_GLOBAL_LIMIT 16u
#define COMPILER_STRUCT_LIMIT 8u
#define COMPILER_FIELD_LIMIT 16u

typedef struct {
    char Name[32];
    u16 CodeOffset;
    u8 ReturnType,ArgumentCount,ArgumentTypes[COMPILER_ARGUMENT_LIMIT],Defined;
} BOB64_C_FUNCTION;

typedef struct {
    char Name[32];
    u16 DisplacementOffset;
    u8 ArgumentCount,ArgumentTypes[COMPILER_ARGUMENT_LIMIT];
} BOB64_C_CALL;

typedef struct {
    u16 ContinueTarget,BreakBranches[32],ContinueBranches[32];
    u8 BreakCount,ContinueCount;
} BOB64_C_LOOP;

typedef struct {
    u16 LocalBytes;
    u8 VariableCount;
} BOB64_C_SCOPE;

typedef struct {
    char Name[32];
    u16 DataOffset,ArrayLength,InitTarget;
    u8 Type,InitKind;
} BOB64_C_GLOBAL;

typedef struct {
    char Name[32];
    u16 Offset;
    u8 Type;
} BOB64_C_FIELD;

typedef struct {
    char Name[32];
    u16 Size;
    u8 Alignment,FieldCount;
    BOB64_C_FIELD Fields[COMPILER_FIELD_LIMIT];
} BOB64_C_STRUCT;

typedef struct {
    const char *Source;
    usize Length,Position,ErrorOffset;
    u8 Code[BOB64_PAGE_SIZE];
    u8 Data[BOB64_PAGE_SIZE];
    usize CodePosition,DataLength;
    char VariableNames[16][32];
    s16 VariableDisplacement[16];
    u8 VariableType[16],VariableArrayLength[16],VariableCount;
    u16 LocalBytes,StackBytes;
    BOB64_C_FUNCTION Functions[COMPILER_FUNCTION_LIMIT];
    BOB64_C_GLOBAL Globals[COMPILER_GLOBAL_LIMIT];
    BOB64_C_STRUCT Structs[COMPILER_STRUCT_LIMIT];
    BOB64_C_CALL Calls[COMPILER_CALL_LIMIT];
    BOB64_C_LOOP Loops[16];
    BOB64_C_SCOPE Scopes[32];
    u16 FunctionSourceStart[COMPILER_FUNCTION_LIMIT];
    u16 FunctionSourceEnd[COMPILER_FUNCTION_LIMIT];
    u8 FunctionCount,GlobalCount,StructCount,CallCount,LoopDepth;
    u8 FunctionSourceCount,ScopeDepth;
    u64 IntegerValue;
    u8 ExpressionType,IntegerType;
    u8 ReturnType,Return64,Failed,ControlDepth;
} BOB64_C_COMPILER;

static int compiler_emit(BOB64_C_COMPILER *compiler,u8 value);
static int compiler_emit_u32(BOB64_C_COMPILER *compiler,u32 value);

#define COMPILER_TYPE_INT 1u
#define COMPILER_TYPE_LONG_LONG 2u
#define COMPILER_TYPE_INT_POINTER 3u
#define COMPILER_TYPE_INT_ARRAY 4u
#define COMPILER_TYPE_CHAR 5u
#define COMPILER_TYPE_CHAR_POINTER 6u
#define COMPILER_TYPE_CHAR_ARRAY 7u
#define COMPILER_TYPE_SHORT 8u
#define COMPILER_TYPE_SHORT_POINTER 9u
#define COMPILER_TYPE_SHORT_ARRAY 10u
#define COMPILER_TYPE_USIZE 11u
#define COMPILER_TYPE_VOID 12u
#define COMPILER_TYPE_ISIZE_POINTER 13u
#define COMPILER_TYPE_USIZE_POINTER 14u
#define COMPILER_TYPE_LONG_LONG_ARRAY 15u
#define COMPILER_TYPE_STRUCT_BASE 16u
#define COMPILER_TYPE_USIZE_ARRAY 24u
#define COMPILER_TYPE_STRUCT_POINTER_BASE 64u

static int compiler_is_pointer(u8 type) {
    return type==COMPILER_TYPE_INT_POINTER||type==COMPILER_TYPE_CHAR_POINTER||
           type==COMPILER_TYPE_SHORT_POINTER||type==COMPILER_TYPE_ISIZE_POINTER||
           type==COMPILER_TYPE_USIZE_POINTER||
           (type>=COMPILER_TYPE_STRUCT_POINTER_BASE&&
            type<COMPILER_TYPE_STRUCT_POINTER_BASE+COMPILER_STRUCT_LIMIT);
}

static int compiler_type_is_struct_pointer(u8 type) {
    return type>=COMPILER_TYPE_STRUCT_POINTER_BASE&&
           type<COMPILER_TYPE_STRUCT_POINTER_BASE+COMPILER_STRUCT_LIMIT;
}

static int compiler_type_is_array(u8 type) {
    return type==COMPILER_TYPE_INT_ARRAY||type==COMPILER_TYPE_CHAR_ARRAY||
           type==COMPILER_TYPE_SHORT_ARRAY||type==COMPILER_TYPE_LONG_LONG_ARRAY||
           type==COMPILER_TYPE_USIZE_ARRAY;
}

static u32 compiler_array_element_size(u8 type) {
    if(type==COMPILER_TYPE_CHAR_ARRAY)return 1u;
    if(type==COMPILER_TYPE_SHORT_ARRAY)return 2u;
    if(type==COMPILER_TYPE_LONG_LONG_ARRAY||type==COMPILER_TYPE_USIZE_ARRAY)return 8u;
    return 4u;
}

static u32 compiler_pointer_scale(BOB64_C_COMPILER *compiler,u8 type) {
    if(type==COMPILER_TYPE_CHAR_POINTER)return 1u;
    if(type==COMPILER_TYPE_SHORT_POINTER)return 2u;
    if(type==COMPILER_TYPE_ISIZE_POINTER||type==COMPILER_TYPE_USIZE_POINTER)return 8u;
    if(compiler_type_is_struct_pointer(type))
        return compiler->Structs[type-COMPILER_TYPE_STRUCT_POINTER_BASE].Size;
    return 4u;
}

static u8 compiler_pointer_type(u8 value_type) {
    if(value_type==COMPILER_TYPE_CHAR||value_type==COMPILER_TYPE_CHAR_ARRAY)
        return COMPILER_TYPE_CHAR_POINTER;
    if(value_type==COMPILER_TYPE_SHORT||value_type==COMPILER_TYPE_SHORT_ARRAY)
        return COMPILER_TYPE_SHORT_POINTER;
    if(value_type==COMPILER_TYPE_LONG_LONG||value_type==COMPILER_TYPE_LONG_LONG_ARRAY)
        return COMPILER_TYPE_ISIZE_POINTER;
    if(value_type==COMPILER_TYPE_USIZE||value_type==COMPILER_TYPE_USIZE_ARRAY)
        return COMPILER_TYPE_USIZE_POINTER;
    return COMPILER_TYPE_INT_POINTER;
}

static int compiler_emit_scale_register(BOB64_C_COMPILER *compiler,u32 scale,
                                        u8 register_id) {
    if(scale==1)return 1;
    if(scale==2||scale==4||scale==8) {
        u8 shift=scale==2?1:(scale==4?2:3);
        return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0xc1)&&
               compiler_emit(compiler,register_id?0xe1:0xe0)&&
               compiler_emit(compiler,shift);
    }
    return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x69)&&
           compiler_emit(compiler,register_id?0xc9:0xc0)&&
           compiler_emit_u32(compiler,scale);
}

static void compiler_skip(BOB64_C_COMPILER *compiler) {
    for(;;) {
        while(compiler->Position<compiler->Length&&
              (compiler->Source[compiler->Position]==' '||
               compiler->Source[compiler->Position]=='\t'||
               compiler->Source[compiler->Position]=='\r'||
               compiler->Source[compiler->Position]=='\n'))compiler->Position++;
        if(compiler->Position+1<compiler->Length&&
           compiler->Source[compiler->Position]=='/'&&
           compiler->Source[compiler->Position+1]=='/') {
            while(compiler->Position<compiler->Length&&
                  compiler->Source[compiler->Position]!='\n')compiler->Position++;
        } else if(compiler->Position+1<compiler->Length&&
                  compiler->Source[compiler->Position]=='/'&&
                  compiler->Source[compiler->Position+1]=='*') {
            compiler->Position+=2;
            while(compiler->Position+1<compiler->Length&&
                  !(compiler->Source[compiler->Position]=='*'&&
                    compiler->Source[compiler->Position+1]=='/'))compiler->Position++;
            if(compiler->Position+1>=compiler->Length) {
                compiler->Failed=1;compiler->ErrorOffset=compiler->Position;return;
            }
            compiler->Position+=2;
        } else return;
    }
}

static int compiler_take(BOB64_C_COMPILER *compiler,char value) {
    compiler_skip(compiler);
    if(compiler->Failed||compiler->Position>=compiler->Length||
       compiler->Source[compiler->Position]!=value)return 0;
    compiler->Position++;return 1;
}

static int compiler_word(BOB64_C_COMPILER *compiler,const char *word) {
    usize start,length=0;
    compiler_skip(compiler);start=compiler->Position;
    while(word[length])length++;
    if(compiler->Failed||length>compiler->Length-start)return 0;
    for(usize i=0;i<length;i++)if(compiler->Source[start+i]!=word[i])return 0;
    if(start+length<compiler->Length) {
        char next=compiler->Source[start+length];
        if((next>='a'&&next<='z')||(next>='A'&&next<='Z')||
           (next>='0'&&next<='9')||next=='_')return 0;
    }
    compiler->Position+=length;return 1;
}

static u8 compiler_long_type(BOB64_C_COMPILER *compiler) {
    if(!compiler_word(compiler,"long"))return 0;
    /* bob64 follows LLP64: long is 32-bit; long long is 64-bit. */
    return compiler_word(compiler,"long")?COMPILER_TYPE_LONG_LONG:COMPILER_TYPE_INT;
}

static u8 compiler_integer_type(BOB64_C_COMPILER *compiler) {
    if(compiler_word(compiler,"int"))return COMPILER_TYPE_INT;
    if(compiler_word(compiler,"short"))return COMPILER_TYPE_SHORT;
    if(compiler_word(compiler,"usize")||compiler_word(compiler,"uintptr_t"))
        return COMPILER_TYPE_USIZE;
    if(compiler_word(compiler,"isize")||compiler_word(compiler,"intptr_t"))
        return COMPILER_TYPE_LONG_LONG;
    return compiler_long_type(compiler);
}

static int compiler_identifier(BOB64_C_COMPILER *compiler,char *name,usize capacity) {
    usize length=0;
    compiler_skip(compiler);
    if(compiler->Position>=compiler->Length)return 0;
    char first=compiler->Source[compiler->Position];
    if(!((first>='a'&&first<='z')||(first>='A'&&first<='Z')||first=='_'))return 0;
    while(compiler->Position<compiler->Length) {
        char next=compiler->Source[compiler->Position];
        if(!((next>='a'&&next<='z')||(next>='A'&&next<='Z')||
             (next>='0'&&next<='9')||next=='_'))break;
        if(length+1>=capacity)return 0;
        name[length++]=next;compiler->Position++;
    }
    name[length]=0;return 1;
}

static int compiler_struct_find(BOB64_C_COMPILER *compiler,const char *name);

static int compiler_function_signature(BOB64_C_COMPILER *compiler,char *name,
        usize name_capacity,u8 *return_type,u8 *argument_count,
        u8 argument_types[COMPILER_ARGUMENT_LIMIT],
        char parameter_names[COMPILER_ARGUMENT_LIMIT][32],int require_names) {
    u32 count=0;
    int void_type=compiler_word(compiler,"void");
    int character_type=!void_type&&compiler_word(compiler,"char");
    int struct_type=-1;
    u8 value_type=void_type?COMPILER_TYPE_VOID:
                  (character_type?COMPILER_TYPE_CHAR:compiler_integer_type(compiler));
    if(!void_type&&!character_type&&!value_type) {
        if(!compiler_word(compiler,"struct"))return 0;
        char struct_name[32];
        if(!compiler_identifier(compiler,struct_name,sizeof(struct_name)))return 0;
        struct_type=compiler_struct_find(compiler,struct_name);
        if(struct_type<0)return 0;
    }
    int return_pointer=compiler_take(compiler,'*');
    if((struct_type>=0&&!return_pointer)||(void_type&&return_pointer))return 0;
    *return_type=struct_type>=0?
        (u8)(COMPILER_TYPE_STRUCT_POINTER_BASE+(u32)struct_type):
        (return_pointer?compiler_pointer_type(value_type):value_type);
    if(!*return_type)return 0;
    if(!compiler_identifier(compiler,name,name_capacity)||!compiler_take(compiler,'('))return 0;
    if(!compiler_take(compiler,')')) {
        if(compiler_word(compiler,"void")) {
            if(!compiler_take(compiler,')'))return 0;
        } else {
            for(;;) {
                int pointer,has_name;
                if(count>=COMPILER_ARGUMENT_LIMIT)return 0;
                int character_type=compiler_word(compiler,"char");
                u8 value_type=character_type?COMPILER_TYPE_CHAR:
                              compiler_integer_type(compiler);
                int struct_type=-1;
                if(!character_type&&!value_type) {
                    if(!compiler_word(compiler,"struct"))return 0;
                    char struct_name[32];
                    if(!compiler_identifier(compiler,struct_name,sizeof(struct_name)))return 0;
                    struct_type=compiler_struct_find(compiler,struct_name);
                    if(struct_type<0)return 0;
                }
                pointer=compiler_take(compiler,'*');
                if(struct_type>=0&&!pointer)return 0;
                compiler_skip(compiler);
                has_name=compiler_identifier(compiler,parameter_names[count],
                                              sizeof(parameter_names[count]));
                if(require_names&&!has_name)return 0;
                if(!has_name)parameter_names[count][0]=0;
                if(compiler_take(compiler,'[')) {
                    if(pointer||!compiler_take(compiler,']'))return 0;
                    pointer=1;
                }
                argument_types[count++]=struct_type>=0?
                    (u8)(COMPILER_TYPE_STRUCT_POINTER_BASE+(u32)struct_type):
                     (pointer?compiler_pointer_type(value_type):value_type);
                if(compiler_take(compiler,')'))break;
                if(!compiler_take(compiler,','))return 0;
            }
        }
    }
    *argument_count=(u8)count;
    return 1;
}

static int compiler_variable_find(BOB64_C_COMPILER *compiler,const char *name) {
    for(u32 i=compiler->VariableCount;i>0;i--) {
        usize position=0;
        u32 variable=i-1;
        while(compiler->VariableNames[variable][position]&&
              compiler->VariableNames[variable][position]==name[position])position++;
        if(!compiler->VariableNames[variable][position]&&!name[position])
            return (int)variable;
    }
    for(u32 i=0;i<compiler->GlobalCount;i++) {
        usize position=0;
        while(compiler->Globals[i].Name[position]&&
              compiler->Globals[i].Name[position]==name[position])position++;
        if(!compiler->Globals[i].Name[position]&&!name[position])return (int)(0x8000u|i);
    }
    return -1;
}

static int compiler_scope_push(BOB64_C_COMPILER *compiler) {
    if(compiler->ScopeDepth>=sizeof(compiler->Scopes)/sizeof(compiler->Scopes[0]))
        return 0;
    BOB64_C_SCOPE *scope=&compiler->Scopes[compiler->ScopeDepth++];
    scope->VariableCount=compiler->VariableCount;
    scope->LocalBytes=compiler->LocalBytes;
    return 1;
}

static void compiler_scope_pop(BOB64_C_COMPILER *compiler) {
    if(!compiler->ScopeDepth)return;
    BOB64_C_SCOPE *scope=&compiler->Scopes[--compiler->ScopeDepth];
    compiler->VariableCount=scope->VariableCount;
    compiler->LocalBytes=scope->LocalBytes;
}

static int compiler_struct_find(BOB64_C_COMPILER *compiler,const char *name) {
    for(u32 i=0;i<compiler->StructCount;i++) {
        usize position=0;
        while(compiler->Structs[i].Name[position]&&
              compiler->Structs[i].Name[position]==name[position])position++;
        if(!compiler->Structs[i].Name[position]&&!name[position])return (int)i;
    }
    return -1;
}

static int compiler_type_is_struct(u8 type) {
    return type>=COMPILER_TYPE_STRUCT_BASE&&
           type<COMPILER_TYPE_STRUCT_BASE+COMPILER_STRUCT_LIMIT;
}

static u32 compiler_type_size(BOB64_C_COMPILER *compiler,u8 type) {
    if(compiler_type_is_struct(type))
        return compiler->Structs[type-COMPILER_TYPE_STRUCT_BASE].Size;
    if(type==COMPILER_TYPE_CHAR)return 1;
    if(type==COMPILER_TYPE_SHORT)return 2;
    if(type==COMPILER_TYPE_SHORT_ARRAY)return 2;
    if(type==COMPILER_TYPE_INT||type==COMPILER_TYPE_INT_ARRAY)return 4;
    if(type==COMPILER_TYPE_LONG_LONG_ARRAY||type==COMPILER_TYPE_USIZE_ARRAY)return 8;
    return 8;
}

static u32 compiler_type_alignment(BOB64_C_COMPILER *compiler,u8 type) {
    if(compiler_type_is_struct(type))
        return compiler->Structs[type-COMPILER_TYPE_STRUCT_BASE].Alignment;
    u32 size=compiler_type_size(compiler,type);
    return size>=8?8:(size>=4?4:(size>=2?2:1));
}

static int compiler_field_find(BOB64_C_COMPILER *compiler,u8 struct_type,
                               const char *name) {
    if(!compiler_type_is_struct(struct_type))return -1;
    BOB64_C_STRUCT *structure=&compiler->Structs[struct_type-COMPILER_TYPE_STRUCT_BASE];
    for(u32 i=0;i<structure->FieldCount;i++) {
        usize position=0;
        while(structure->Fields[i].Name[position]&&
              structure->Fields[i].Name[position]==name[position])position++;
        if(!structure->Fields[i].Name[position]&&!name[position])return (int)i;
    }
    return -1;
}

static int compiler_variable_is_global(u32 variable) {
    return (variable&0x8000u)!=0;
}

static u8 compiler_variable_type(BOB64_C_COMPILER *compiler,u32 variable) {
    return compiler_variable_is_global(variable)?
        compiler->Globals[variable&0x7fffu].Type:compiler->VariableType[variable];
}

static u32 compiler_variable_array_length(BOB64_C_COMPILER *compiler,u32 variable) {
    return compiler_variable_is_global(variable)?
        compiler->Globals[variable&0x7fffu].ArrayLength:
        compiler->VariableArrayLength[variable];
}

static int compiler_emit_global_displacement(BOB64_C_COMPILER *compiler,u32 variable) {
    BOB64_C_GLOBAL *global=&compiler->Globals[variable&0x7fffu];
    s64 displacement=(s64)(BOB64_PAGE_SIZE+global->DataOffset)-
                     (s64)(compiler->CodePosition+4u);
    if(displacement<(-2147483647LL-1)||displacement>2147483647)return 0;
    return compiler_emit_u32(compiler,(u32)(s32)displacement);
}

static int compiler_emit_data_displacement(BOB64_C_COMPILER *compiler,u16 data_offset) {
    s64 displacement=(s64)(BOB64_PAGE_SIZE+data_offset)-
                     (s64)(compiler->CodePosition+4u);
    if(displacement<(-2147483647LL-1)||displacement>2147483647)return 0;
    return compiler_emit_u32(compiler,(u32)(s32)displacement);
}

static int compiler_emit_global_initializers(BOB64_C_COMPILER *compiler) {
    for(u32 i=0;i<compiler->GlobalCount;i++) {
        BOB64_C_GLOBAL *global=&compiler->Globals[i];
        if(!global->InitKind)continue;
        if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x8d)||
           !compiler_emit(compiler,0x05)||
           !compiler_emit_data_displacement(compiler,global->InitTarget)||
           !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x89)||
           !compiler_emit(compiler,0x05)||
           !compiler_emit_global_displacement(compiler,0x8000u|i))return 0;
    }
    return 1;
}

static int compiler_function_find(BOB64_C_COMPILER *compiler,const char *name) {
    for(u32 i=0;i<compiler->FunctionCount;i++) {
        usize position=0;
        while(compiler->Functions[i].Name[position]&&
              compiler->Functions[i].Name[position]==name[position])position++;
        if(!compiler->Functions[i].Name[position]&&!name[position])return (int)i;
    }
    return -1;
}

static int compiler_function_declare(BOB64_C_COMPILER *compiler,const char *name,
        u16 code_offset,u8 return_type,u8 argument_count,
        const u8 *argument_types,int defined) {
    if(argument_count>COMPILER_ARGUMENT_LIMIT)return 0;
    int existing=compiler_function_find(compiler,name);
    if(existing>=0) {
        BOB64_C_FUNCTION *function=&compiler->Functions[existing];
        if(function->ReturnType!=return_type||function->ArgumentCount!=argument_count||
           (defined&&function->Defined))return 0;
        for(u32 i=0;i<argument_count;i++)
            if(function->ArgumentTypes[i]!=argument_types[i])return 0;
        if(defined) {
            function->CodeOffset=code_offset;
            function->Defined=1;
        }
        return 1;
    }
    if(compiler->FunctionCount>=COMPILER_FUNCTION_LIMIT)return 0;
    u32 index=compiler->FunctionCount++;
    BOB64_C_FUNCTION *function=&compiler->Functions[index];
    usize position=0;
    while(name[position]&&position+1<sizeof(function->Name)) {
        function->Name[position]=name[position];position++;
    }
    if(name[position])return 0;
    function->Name[position]=0;
    function->CodeOffset=code_offset;
    function->ReturnType=return_type;
    function->ArgumentCount=argument_count;
    function->Defined=(u8)defined;
    for(u32 i=0;i<argument_count;i++)function->ArgumentTypes[i]=argument_types[i];
    return 1;
}

static int compiler_variable_load(BOB64_C_COMPILER *compiler,u32 variable) {
    if(compiler_variable_is_global(variable)) {
        u8 type=compiler_variable_type(compiler,variable);
        if(type==COMPILER_TYPE_CHAR)
            return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x0f)&&
                   compiler_emit(compiler,0xbe)&&compiler_emit(compiler,0x05)&&
                   compiler_emit_global_displacement(compiler,variable);
        if(type==COMPILER_TYPE_SHORT)
            return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x0f)&&
                   compiler_emit(compiler,0xbf)&&compiler_emit(compiler,0x05)&&
                   compiler_emit_global_displacement(compiler,variable);
        if(type==COMPILER_TYPE_LONG_LONG||type==COMPILER_TYPE_USIZE||
           compiler_is_pointer(type))
            return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x8b)&&
                   compiler_emit(compiler,0x05)&&
                   compiler_emit_global_displacement(compiler,variable);
        return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x63)&&
               compiler_emit(compiler,0x05)&&
               compiler_emit_global_displacement(compiler,variable);
    }
    s8 displacement=(s8)compiler->VariableDisplacement[variable];
    if(compiler->VariableType[variable]==COMPILER_TYPE_CHAR)
        return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x0f)&&
               compiler_emit(compiler,0xbe)&&compiler_emit(compiler,0x45)&&
               compiler_emit(compiler,(u8)displacement);
    if(compiler->VariableType[variable]==COMPILER_TYPE_SHORT)
        return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x0f)&&
               compiler_emit(compiler,0xbf)&&compiler_emit(compiler,0x45)&&
               compiler_emit(compiler,(u8)displacement);
    if(compiler->VariableType[variable]==COMPILER_TYPE_LONG_LONG||
       compiler->VariableType[variable]==COMPILER_TYPE_USIZE||
       compiler_is_pointer(compiler->VariableType[variable]))
        return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x8b)&&
               compiler_emit(compiler,0x45)&&compiler_emit(compiler,(u8)displacement);
    return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x63)&&
           compiler_emit(compiler,0x45)&&compiler_emit(compiler,(u8)displacement);
}

static int compiler_variable_store(BOB64_C_COMPILER *compiler,u32 variable) {
    if(compiler_variable_is_global(variable)) {
        u8 type=compiler_variable_type(compiler,variable);
        if(type==COMPILER_TYPE_CHAR)
            return compiler_emit(compiler,0x88)&&compiler_emit(compiler,0x05)&&
                   compiler_emit_global_displacement(compiler,variable);
        if(type==COMPILER_TYPE_SHORT)
            return compiler_emit(compiler,0x66)&&compiler_emit(compiler,0x89)&&
                   compiler_emit(compiler,0x05)&&
                   compiler_emit_global_displacement(compiler,variable);
        if(type==COMPILER_TYPE_LONG_LONG||type==COMPILER_TYPE_USIZE||
           compiler_is_pointer(type))
            return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x89)&&
                   compiler_emit(compiler,0x05)&&
                   compiler_emit_global_displacement(compiler,variable);
        return compiler_emit(compiler,0x89)&&compiler_emit(compiler,0x05)&&
               compiler_emit_global_displacement(compiler,variable);
    }
    s8 displacement=(s8)compiler->VariableDisplacement[variable];
    if(compiler->VariableType[variable]==COMPILER_TYPE_CHAR)
        return compiler_emit(compiler,0x88)&&compiler_emit(compiler,0x45)&&
               compiler_emit(compiler,(u8)displacement);
    if(compiler->VariableType[variable]==COMPILER_TYPE_SHORT)
        return compiler_emit(compiler,0x66)&&compiler_emit(compiler,0x89)&&
               compiler_emit(compiler,0x45)&&compiler_emit(compiler,(u8)displacement);
    if(compiler->VariableType[variable]==COMPILER_TYPE_LONG_LONG||
       compiler->VariableType[variable]==COMPILER_TYPE_USIZE||
       compiler_is_pointer(compiler->VariableType[variable]))
        return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x89)&&
               compiler_emit(compiler,0x45)&&compiler_emit(compiler,(u8)displacement);
    return compiler_emit(compiler,0x89)&&compiler_emit(compiler,0x45)&&
           compiler_emit(compiler,(u8)displacement);
}

static int compiler_variable_address(BOB64_C_COMPILER *compiler,u32 variable,
                                     u8 destination_register) {
    if(compiler_variable_is_global(variable))
        return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x8d)&&
               compiler_emit(compiler,(u8)(0x05u|(destination_register<<3)))&&
               compiler_emit_global_displacement(compiler,variable);
    s8 displacement=(s8)compiler->VariableDisplacement[variable];
    return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x8d)&&
           compiler_emit(compiler,(u8)(0x45u|(destination_register<<3)))&&
           compiler_emit(compiler,(u8)displacement);
}

static int compiler_member_address(BOB64_C_COMPILER *compiler,u32 variable,
                                   const BOB64_C_FIELD *field) {
    if(!compiler_variable_address(compiler,variable,0))return 0;
    if(!field->Offset)return 1;
    return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x05)&&
           compiler_emit_u32(compiler,field->Offset);
}

static int compiler_member_load(BOB64_C_COMPILER *compiler,u8 type) {
    if(type==COMPILER_TYPE_CHAR)
        return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x0f)&&
               compiler_emit(compiler,0xbe)&&compiler_emit(compiler,0x00);
    if(type==COMPILER_TYPE_SHORT)
        return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x0f)&&
               compiler_emit(compiler,0xbf)&&compiler_emit(compiler,0x00);
    if(type==COMPILER_TYPE_INT)
        return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x63)&&
               compiler_emit(compiler,0x00);
    return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x8b)&&
           compiler_emit(compiler,0x00);
}

static int compiler_member_store(BOB64_C_COMPILER *compiler,u8 type) {
    if(type==COMPILER_TYPE_CHAR)
        return compiler_emit(compiler,0x88)&&compiler_emit(compiler,0x01);
    if(type==COMPILER_TYPE_SHORT)
        return compiler_emit(compiler,0x66)&&compiler_emit(compiler,0x89)&&
               compiler_emit(compiler,0x01);
    if(type==COMPILER_TYPE_INT)
        return compiler_emit(compiler,0x89)&&compiler_emit(compiler,0x01);
    return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x89)&&
           compiler_emit(compiler,0x01);
}

static int compiler_add_variable(BOB64_C_COMPILER *compiler,const char *name,
        u8 type,u32 array_length,u32 *variable_out) {
    usize length=0;
    u32 bytes;
    u32 scope_start=compiler->ScopeDepth?
        compiler->Scopes[compiler->ScopeDepth-1].VariableCount:0;
    for(u32 i=scope_start;i<compiler->VariableCount;i++) {
        usize position=0;
        while(compiler->VariableNames[i][position]&&
              compiler->VariableNames[i][position]==name[position])position++;
        if(!compiler->VariableNames[i][position]&&!name[position])return 0;
    }
    if(compiler->VariableCount>=16)return 0;
    if(type==COMPILER_TYPE_INT_ARRAY) {
        if(!array_length||array_length>32)return 0;
        bytes=array_length*4;
        compiler->LocalBytes=(u16)((compiler->LocalBytes+3u)&~3u);
    } else if(type==COMPILER_TYPE_SHORT_ARRAY) {
        if(!array_length||array_length>64)return 0;
        bytes=array_length*2;
        compiler->LocalBytes=(u16)((compiler->LocalBytes+1u)&~1u);
    } else if(type==COMPILER_TYPE_CHAR_ARRAY) {
        if(!array_length||array_length>128)return 0;
        bytes=array_length;
    } else if(type==COMPILER_TYPE_LONG_LONG_ARRAY||type==COMPILER_TYPE_USIZE_ARRAY) {
        if(!array_length||array_length>16)return 0;
        bytes=array_length*8u;
        compiler->LocalBytes=(u16)((compiler->LocalBytes+7u)&~7u);
    } else if(compiler_type_is_struct(type)) {
        u32 alignment=compiler_type_alignment(compiler,type);
        compiler->LocalBytes=(u16)((compiler->LocalBytes+alignment-1u)&~(alignment-1u));
        bytes=compiler_type_size(compiler,type);
    } else {
        bytes=8;
        compiler->LocalBytes=(u16)((compiler->LocalBytes+7u)&~7u);
    }
    if(bytes>128u-compiler->LocalBytes)return 0;
    compiler->LocalBytes=(u16)(compiler->LocalBytes+bytes);
    u32 variable=compiler->VariableCount++;
    while(name[length])length++;
    for(usize i=0;i<=length;i++)compiler->VariableNames[variable][i]=name[i];
    compiler->VariableType[variable]=type;
    compiler->VariableArrayLength[variable]=(u8)array_length;
    compiler->VariableDisplacement[variable]=-(s16)compiler->LocalBytes;
    if(variable_out)*variable_out=variable;
    return 1;
}

static int compiler_integer(BOB64_C_COMPILER *compiler,u64 *value) {
    u64 number=0;u32 base=10;usize digits=0;
    int has_unsigned=0,long_count=0,unsigned_after_long=0;
    compiler_skip(compiler);
    if(compiler->Position+1<compiler->Length&&compiler->Source[compiler->Position]=='0'&&
       (compiler->Source[compiler->Position+1]=='x'||
        compiler->Source[compiler->Position+1]=='X')) {
        base=16;compiler->Position+=2;
    }
    while(compiler->Position<compiler->Length) {
        char character=compiler->Source[compiler->Position];
        u32 digit;
        if(character>='0'&&character<='9')digit=(u32)(character-'0');
        else if(base==16&&character>='a'&&character<='f')digit=(u32)(character-'a'+10);
        else if(base==16&&character>='A'&&character<='F')digit=(u32)(character-'A'+10);
        else break;
        if(digit>=base||number>(~(u64)0-digit)/base)return 0;
        number=number*base+digit;digits++;compiler->Position++;
    }
    if(!digits)return 0;
    while(compiler->Position<compiler->Length) {
        char suffix=compiler->Source[compiler->Position];
        if((suffix=='u'||suffix=='U')&&!has_unsigned) {
            has_unsigned=1;if(long_count)unsigned_after_long=1;compiler->Position++;
        } else if((suffix=='l'||suffix=='L')&&long_count<2) {
            if(unsigned_after_long)return 0;
            long_count++;compiler->Position++;
        } else break;
    }
    if(compiler->Position<compiler->Length) {
        char next=compiler->Source[compiler->Position];
        if((next>='a'&&next<='z')||(next>='A'&&next<='Z')||next=='_')return 0;
    }
    *value=number;
    compiler->IntegerType=has_unsigned?COMPILER_TYPE_USIZE:
        ((long_count||number>0x7fffffffu)?COMPILER_TYPE_LONG_LONG:COMPILER_TYPE_INT);
    return 1;
}

static int compiler_string(BOB64_C_COMPILER *compiler,usize *offset,usize *length) {
    compiler_skip(compiler);
    if(compiler->Position>=compiler->Length||compiler->Source[compiler->Position]!='"')return 0;
    compiler->Position++;*offset=compiler->DataLength;*length=0;
    while(compiler->Position<compiler->Length&&compiler->Source[compiler->Position]!='"') {
        u8 value=(u8)compiler->Source[compiler->Position++];
        if(value=='\\') {
            if(compiler->Position>=compiler->Length)return 0;
            value=(u8)compiler->Source[compiler->Position++];
            if(value=='n')value='\n';else if(value=='r')value='\r';
            else if(value=='t')value='\t';
            else if(value!='\\'&&value!='"'&&value!='\'')return 0;
        }
        if(!value||*length>=BOB64_SYSCALL_MAX_BUFFER||
           compiler->DataLength>=sizeof(compiler->Data))return 0;
        compiler->Data[compiler->DataLength++]=value;(*length)++;
    }
    if(compiler->Position>=compiler->Length)return 0;
    compiler->Position++;
    if(compiler->DataLength>=sizeof(compiler->Data))return 0;
    compiler->Data[compiler->DataLength++]=0;
    return 1;
}

static int compiler_character(BOB64_C_COMPILER *compiler,u64 *value) {
    compiler_skip(compiler);
    if(compiler->Position>=compiler->Length||compiler->Source[compiler->Position]!='\'')
        return 0;
    compiler->Position++;
    if(compiler->Position>=compiler->Length)return 0;
    u8 character=(u8)compiler->Source[compiler->Position++];
    if(character=='\\') {
        if(compiler->Position>=compiler->Length)return 0;
        character=(u8)compiler->Source[compiler->Position++];
        if(character=='n')character='\n';else if(character=='r')character='\r';
        else if(character=='t')character='\t';else if(character=='0')character=0;
        else if(character!='\\'&&character!='\''&&character!='"')return 0;
    }
    if(compiler->Position>=compiler->Length||
       compiler->Source[compiler->Position++]!='\'')return 0;
    *value=(u64)(s64)(s8)character;
    return 1;
}

static int compiler_emit(BOB64_C_COMPILER *compiler,u8 value) {
    if(compiler->CodePosition>=sizeof(compiler->Code))return 0;
    compiler->Code[compiler->CodePosition++]=value;return 1;
}

static int compiler_emit_u32(BOB64_C_COMPILER *compiler,u32 value) {
    for(u32 i=0;i<4;i++)if(!compiler_emit(compiler,(u8)(value>>(i*8))))return 0;
    return 1;
}

static int compiler_emit_u64(BOB64_C_COMPILER *compiler,u64 value) {
    for(u32 i=0;i<8;i++)if(!compiler_emit(compiler,(u8)(value>>(i*8))))return 0;
    return 1;
}

static int compiler_emit_integer(BOB64_C_COMPILER *compiler,u64 value) {
    return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0xb8)&&
           compiler_emit_u64(compiler,value);
}

static int compiler_expression(BOB64_C_COMPILER *compiler);
static int compiler_emit_branch(BOB64_C_COMPILER *compiler,u8 opcode,
                                u16 *displacement_offset);
static int compiler_patch_branch(BOB64_C_COMPILER *compiler,u16 displacement_offset,
                                 usize target);
static int compiler_for_post(BOB64_C_COMPILER *compiler);

static int compiler_emit_call(BOB64_C_COMPILER *compiler,const char *name) {
    u8 argument_types[COMPILER_ARGUMENT_LIMIT];
    u32 argument_count=0;
    u16 saved_stack_bytes=compiler->StackBytes;
    if(!compiler_take(compiler,'('))return 0;
    if(!compiler_take(compiler,')')) {
        for(;;) {
            if(argument_count>=COMPILER_ARGUMENT_LIMIT||
               !compiler_expression(compiler)||
               compiler->ExpressionType==COMPILER_TYPE_VOID)return 0;
            argument_types[argument_count++]=compiler->ExpressionType;
            if(!compiler_emit(compiler,0x50))return 0;
            compiler->StackBytes+=8;
            if(compiler_take(compiler,')'))break;
            if(!compiler_take(compiler,','))return 0;
        }
    }
    if(compiler->CallCount>=COMPILER_CALL_LIMIT)return 0;
    u32 stack_argument_bytes=argument_count>4?(argument_count-4u)*8u:0u;
    u32 outgoing_bytes=32u+stack_argument_bytes;
    u32 allocation=outgoing_bytes+
        ((16u-((compiler->StackBytes+outgoing_bytes)&15u))&15u);
    if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x83)||
       !compiler_emit(compiler,0xec)||!compiler_emit(compiler,(u8)allocation))return 0;
    compiler->StackBytes=(u16)(compiler->StackBytes+allocation);
    for(u32 i=0;i<argument_count&&i<4;i++) {
        u32 displacement=allocation+(argument_count-1u-i)*8u;
        if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x8b)||
           !compiler_emit(compiler,0x44)||!compiler_emit(compiler,0x24)||
           !compiler_emit(compiler,(u8)displacement))return 0;
        static const u8 move_argument[4][3]={
            {0x48,0x89,0xc1},{0x48,0x89,0xc2},
            {0x49,0x89,0xc0},{0x49,0x89,0xc1}
        };
        for(u32 byte=0;byte<3;byte++)
            if(!compiler_emit(compiler,move_argument[i][byte]))return 0;
    }
    for(u32 i=4;i<argument_count;i++) {
        u32 source_displacement=allocation+(argument_count-1u-i)*8u;
        u32 target_displacement=32u+(i-4u)*8u;
        if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x8b)||
           !compiler_emit(compiler,0x44)||!compiler_emit(compiler,0x24)||
           !compiler_emit(compiler,(u8)source_displacement)||
           !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x89)||
           !compiler_emit(compiler,0x44)||!compiler_emit(compiler,0x24)||
           !compiler_emit(compiler,(u8)target_displacement))return 0;
    }
    BOB64_C_CALL *call=&compiler->Calls[compiler->CallCount++];
    usize name_length=0;
    while(name[name_length]&&name_length+1<sizeof(call->Name)) {
        call->Name[name_length]=name[name_length];name_length++;
    }
    if(name[name_length])return 0;
    call->Name[name_length]=0;
    call->ArgumentCount=(u8)argument_count;
    for(u32 i=0;i<argument_count;i++)call->ArgumentTypes[i]=argument_types[i];
    if(!compiler_emit(compiler,0xe8))return 0;
    call->DisplacementOffset=(u16)compiler->CodePosition;
    if(!compiler_emit_u32(compiler,0))return 0;
    u32 cleanup=allocation+argument_count*8u;
    if(cleanup<=127u) {
        if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x83)||
           !compiler_emit(compiler,0xc4)||!compiler_emit(compiler,(u8)cleanup))return 0;
    } else if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x81)||
              !compiler_emit(compiler,0xc4)||
              !compiler_emit_u32(compiler,cleanup))return 0;
    /* Argument evaluation and the outgoing call frame are both temporary.
       Restore the depth that was present before evaluating the arguments;
       subtracting cleanup from the tracked depth underflows for ordinary calls. */
    compiler->StackBytes=saved_stack_bytes;
    return 1;
}

static int compiler_index_address(BOB64_C_COMPILER *compiler,u32 variable) {
    u8 type=compiler_variable_type(compiler,variable);
    if(compiler_type_is_struct_pointer(type))return 0;
    if(compiler_type_is_array(type)) {
        if(!compiler_variable_address(compiler,variable,2))return 0;
    } else if(compiler_is_pointer(type)) {
        if(!compiler_variable_load(compiler,variable)||
           !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x89)||
           !compiler_emit(compiler,0xc2))return 0;
    } else return 0;
    if(!compiler_expression(compiler)||!compiler_take(compiler,']'))return 0;
    if(compiler_is_pointer(compiler->ExpressionType))return 0;
    /* Scale pointer offsets by the pointed-to element width. */
    u8 pointer_type=compiler_type_is_array(type)?compiler_pointer_type(type):type;
    u32 scale=compiler_pointer_scale(compiler,pointer_type);
    if(scale==4) {
        if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x8d)||
           !compiler_emit(compiler,0x04)||!compiler_emit(compiler,0x82))return 0;
    } else {
        if(!compiler_emit_scale_register(compiler,scale,0)||
           !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x01)||
           !compiler_emit(compiler,0xc2)||!compiler_emit(compiler,0x48)||
           !compiler_emit(compiler,0x89)||!compiler_emit(compiler,0xd0))return 0;
    }
    compiler->ExpressionType=pointer_type;
    return 1;
}

static int compiler_primary(BOB64_C_COMPILER *compiler) {
    u64 value;
    char name[32];
    if(compiler_take(compiler,'(')) {
        if(!compiler_expression(compiler)||!compiler_take(compiler,')'))return 0;
        return 1;
    }
    if(compiler_integer(compiler,&value)) {
        compiler->ExpressionType=compiler->IntegerType;
        return compiler_emit_integer(compiler,value);
    }
    if(compiler_character(compiler,&value)) {
        compiler->ExpressionType=COMPILER_TYPE_INT;
        return compiler_emit_integer(compiler,value);
    }
    usize string_offset,string_length;
    if(compiler_string(compiler,&string_offset,&string_length)) {
        (void)string_length;
        s64 displacement=(s64)(BOB64_PAGE_SIZE+string_offset)-
                         (s64)(compiler->CodePosition+7);
        if(displacement<(-2147483647LL-1)||displacement>2147483647||
           !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x8d)||
           !compiler_emit(compiler,0x05)||
           !compiler_emit_u32(compiler,(u32)(s32)displacement))return 0;
        compiler->ExpressionType=COMPILER_TYPE_CHAR_POINTER;
        return 1;
    }
    if(!compiler_identifier(compiler,name,sizeof(name)))return 0;
    compiler_skip(compiler);
    if(compiler->Position<compiler->Length&&compiler->Source[compiler->Position]=='(') {
        int function=compiler_function_find(compiler,name);
        if(compiler_variable_find(compiler,name)>=0||
           !compiler_emit_call(compiler,name))return 0;
        compiler->ExpressionType=function>=0?
            compiler->Functions[function].ReturnType:COMPILER_TYPE_INT;
        if(compiler->ExpressionType==COMPILER_TYPE_VOID)return 0;
        return 1;
    }
    int variable=compiler_variable_find(compiler,name);
    if(variable<0)return 0;
    u8 variable_type=compiler_variable_type(compiler,(u32)variable);
    int pointer_member=compiler_type_is_struct_pointer(variable_type);
    if(compiler_type_is_struct(variable_type)||
       (pointer_member&&compiler->Position+1<compiler->Length&&
        compiler->Source[compiler->Position]=='-'&&
        compiler->Source[compiler->Position+1]=='>')) {
        char field_name[32];
        if((pointer_member?(!compiler_take(compiler,'-')||!compiler_take(compiler,'>')):
                           !compiler_take(compiler,'.'))||
           !compiler_identifier(compiler,field_name,sizeof(field_name)))return 0;
        u8 struct_type=pointer_member?
            (u8)(COMPILER_TYPE_STRUCT_BASE+variable_type-COMPILER_TYPE_STRUCT_POINTER_BASE):
            variable_type;
        int field_index=compiler_field_find(compiler,struct_type,field_name);
        if(field_index<0)return 0;
        BOB64_C_FIELD *field=&compiler->Structs[struct_type-COMPILER_TYPE_STRUCT_BASE].Fields[field_index];
        int address_ok=pointer_member?
            (compiler_variable_load(compiler,(u32)variable)&&
             (!field->Offset||(compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x05)&&
                               compiler_emit_u32(compiler,field->Offset)))):
            compiler_member_address(compiler,(u32)variable,field);
        if(!address_ok||
           !compiler_member_load(compiler,field->Type))return 0;
        compiler->ExpressionType=field->Type;
        return 1;
    }
    if(compiler_take(compiler,'[')) {
        if(!compiler_index_address(compiler,(u32)variable))return 0;
        if(compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_CHAR_POINTER||
           compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_CHAR_ARRAY) {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x0f)||
               !compiler_emit(compiler,0xbe)||!compiler_emit(compiler,0x00))return 0;
            compiler->ExpressionType=COMPILER_TYPE_CHAR;
        } else if(compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_SHORT_POINTER||
                  compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_SHORT_ARRAY) {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x0f)||
               !compiler_emit(compiler,0xbf)||!compiler_emit(compiler,0x00))return 0;
            compiler->ExpressionType=COMPILER_TYPE_SHORT;
        } else if(compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_USIZE_POINTER||
                  compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_ISIZE_POINTER||
                  compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_USIZE_ARRAY||
                  compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_LONG_LONG_ARRAY) {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x8b)||
               !compiler_emit(compiler,0x00))return 0;
            compiler->ExpressionType=
                (compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_USIZE_POINTER||
                 compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_USIZE_ARRAY)?
                COMPILER_TYPE_USIZE:COMPILER_TYPE_LONG_LONG;
        } else {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x63)||
               !compiler_emit(compiler,0x00))return 0;
            compiler->ExpressionType=COMPILER_TYPE_INT;
        }
        return 1;
    }
    if(compiler_type_is_array(compiler_variable_type(compiler,(u32)variable))) {
        compiler->ExpressionType=compiler_pointer_type(
            compiler_variable_type(compiler,(u32)variable));
        return compiler_variable_address(compiler,(u32)variable,0);
    }
    compiler->ExpressionType=compiler_variable_type(compiler,(u32)variable);
    return compiler_variable_load(compiler,(u32)variable);
}

static int compiler_unary(BOB64_C_COMPILER *compiler) {
    if(compiler_take(compiler,'&')) {
        char name[32];
        if(!compiler_identifier(compiler,name,sizeof(name)))return 0;
        int variable=compiler_variable_find(compiler,name);
        u8 type=variable>=0?compiler_variable_type(compiler,(u32)variable):0;
        if(variable<0||(type!=COMPILER_TYPE_INT&&type!=COMPILER_TYPE_CHAR&&
           type!=COMPILER_TYPE_SHORT&&type!=COMPILER_TYPE_LONG_LONG&&
           type!=COMPILER_TYPE_USIZE&&type!=COMPILER_TYPE_INT_ARRAY&&
           type!=COMPILER_TYPE_CHAR_ARRAY&&type!=COMPILER_TYPE_SHORT_ARRAY&&
           type!=COMPILER_TYPE_LONG_LONG_ARRAY&&type!=COMPILER_TYPE_USIZE_ARRAY&&
           !compiler_type_is_struct(type)))return 0;
        compiler->ExpressionType=compiler_type_is_struct(type)?
            (u8)(COMPILER_TYPE_STRUCT_POINTER_BASE+type-COMPILER_TYPE_STRUCT_BASE):
            compiler_pointer_type(type==COMPILER_TYPE_CHAR_ARRAY?COMPILER_TYPE_CHAR:
                (type==COMPILER_TYPE_SHORT_ARRAY?COMPILER_TYPE_SHORT:type));
        return compiler_variable_address(compiler,(u32)variable,0);
    }
    if(compiler_take(compiler,'*')) {
        if(!compiler_unary(compiler)||!compiler_is_pointer(compiler->ExpressionType)||
           compiler_type_is_struct_pointer(compiler->ExpressionType))
            return 0;
        if(compiler->ExpressionType==COMPILER_TYPE_CHAR_POINTER) {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x0f)||
               !compiler_emit(compiler,0xbe)||!compiler_emit(compiler,0x00))return 0;
            compiler->ExpressionType=COMPILER_TYPE_CHAR;
        } else if(compiler->ExpressionType==COMPILER_TYPE_SHORT_POINTER) {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x0f)||
               !compiler_emit(compiler,0xbf)||!compiler_emit(compiler,0x00))return 0;
            compiler->ExpressionType=COMPILER_TYPE_SHORT;
        } else if(compiler->ExpressionType==COMPILER_TYPE_USIZE_POINTER||
                  compiler->ExpressionType==COMPILER_TYPE_ISIZE_POINTER) {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x8b)||
               !compiler_emit(compiler,0x00))return 0;
            compiler->ExpressionType=compiler->ExpressionType==COMPILER_TYPE_USIZE_POINTER?
                                     COMPILER_TYPE_USIZE:COMPILER_TYPE_LONG_LONG;
        } else {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x63)||
               !compiler_emit(compiler,0x00))return 0;
            compiler->ExpressionType=COMPILER_TYPE_INT;
        }
        return 1;
    }
    if(compiler_take(compiler,'!')) {
        if(!compiler_unary(compiler)||!compiler_emit(compiler,0x48)||
           !compiler_emit(compiler,0x85)||!compiler_emit(compiler,0xc0)||
           !compiler_emit(compiler,0x0f)||!compiler_emit(compiler,0x94)||
           !compiler_emit(compiler,0xc0)||!compiler_emit(compiler,0x0f)||
           !compiler_emit(compiler,0xb6)||!compiler_emit(compiler,0xc0))return 0;
        compiler->ExpressionType=COMPILER_TYPE_INT;
        return 1;
    }
    if(compiler_take(compiler,'-')) {
        if(!compiler_unary(compiler)||compiler_is_pointer(compiler->ExpressionType)||
           !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0xf7)||
           !compiler_emit(compiler,0xd8))return 0;
        if(compiler->ExpressionType!=COMPILER_TYPE_USIZE)
            compiler->ExpressionType=COMPILER_TYPE_INT;
        return 1;
    }
    return compiler_primary(compiler);
}

static int compiler_term(BOB64_C_COMPILER *compiler) {
    if(!compiler_unary(compiler))return 0;
    for(;;) {
        u8 left_type=compiler->ExpressionType;
        compiler_skip(compiler);
        if(compiler->Position>=compiler->Length||compiler->Source[compiler->Position]!='*')return 1;
        compiler->Position++;
        if(compiler_is_pointer(compiler->ExpressionType)||
           !compiler_emit(compiler,0x50))return 0;
        compiler->StackBytes+=8;
        if(!compiler_unary(compiler)||
           compiler_is_pointer(compiler->ExpressionType)||
           !compiler_emit(compiler,0x59)||!compiler_emit(compiler,0x48)||
           !compiler_emit(compiler,0x0f)||!compiler_emit(compiler,0xaf)||
           !compiler_emit(compiler,0xc1))return 0;
        compiler->StackBytes-=8;
        compiler->ExpressionType=left_type==COMPILER_TYPE_USIZE||
            compiler->ExpressionType==COMPILER_TYPE_USIZE?
            COMPILER_TYPE_USIZE:
            (left_type==COMPILER_TYPE_LONG_LONG||
             compiler->ExpressionType==COMPILER_TYPE_LONG_LONG?
             COMPILER_TYPE_LONG_LONG:COMPILER_TYPE_INT);
    }
}

static int compiler_additive(BOB64_C_COMPILER *compiler) {
    if(!compiler_term(compiler))return 0;
    for(;;) {
        char operation;
        u8 left_type=compiler->ExpressionType,right_type;
        compiler_skip(compiler);
        if(compiler->Position>=compiler->Length)return 1;
        operation=compiler->Source[compiler->Position];
        if(operation!='+'&&operation!='-')return 1;
        compiler->Position++;
        if(!compiler_emit(compiler,0x50))return 0;
        compiler->StackBytes+=8;
        if(!compiler_term(compiler))return 0;
        right_type=compiler->ExpressionType;
        if(compiler_is_pointer(left_type)&&compiler_is_pointer(right_type)) {
            if(left_type!=right_type)return 0;
            if(operation!='-'||!compiler_emit(compiler,0x59)||
               !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x29)||
               !compiler_emit(compiler,0xc1)||!compiler_emit(compiler,0x48)||
               !compiler_emit(compiler,0x89)||!compiler_emit(compiler,0xc8))return 0;
            u32 scale=compiler_pointer_scale(compiler,left_type);
            if(scale==2||scale==4||scale==8) {
                u8 shift=scale==2?1:(scale==4?2:3);
                if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0xc1)||
                   !compiler_emit(compiler,0xf8)||!compiler_emit(compiler,shift))return 0;
            } else if(scale!=1&&
               (!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x99)||
                !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0xb9)||
                !compiler_emit_u64(compiler,scale)||!compiler_emit(compiler,0x48)||
                !compiler_emit(compiler,0xf7)||!compiler_emit(compiler,0xf9)))return 0;
            compiler->StackBytes-=8;
            compiler->ExpressionType=COMPILER_TYPE_LONG_LONG;
            continue;
        }
        if(compiler_is_pointer(left_type)) {
            if(!compiler_emit_scale_register(compiler,
                   compiler_pointer_scale(compiler,left_type),0))return 0;
        } else if(compiler_is_pointer(right_type)) {
            if(operation!='+'||!compiler_emit(compiler,0x59)||
               !compiler_emit_scale_register(compiler,
                   compiler_pointer_scale(compiler,right_type),1)||
               !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x01)||
               !compiler_emit(compiler,0xc8))return 0;
            compiler->StackBytes-=8;
            compiler->ExpressionType=right_type;
            continue;
        }
        if(!compiler_emit(compiler,0x59))return 0;
        compiler->StackBytes-=8;
        if(operation=='+') {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x01)||
               !compiler_emit(compiler,0xc8))return 0;
        } else if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x29)||
                  !compiler_emit(compiler,0xc1)||!compiler_emit(compiler,0x48)||
                  !compiler_emit(compiler,0x89)||!compiler_emit(compiler,0xc8))return 0;
        compiler->ExpressionType=compiler_is_pointer(left_type)?left_type:
            (left_type==COMPILER_TYPE_USIZE||right_type==COMPILER_TYPE_USIZE?
             COMPILER_TYPE_USIZE:
             (left_type==COMPILER_TYPE_LONG_LONG||right_type==COMPILER_TYPE_LONG_LONG?
              COMPILER_TYPE_LONG_LONG:COMPILER_TYPE_INT));
    }
}

static int compiler_comparison(BOB64_C_COMPILER *compiler) {
    typedef struct { char First,Second;u8 Length,SetCondition; } COMPILER_COMPARISON;
    static const COMPILER_COMPARISON operations[]={
        {'=', '=',2,0x94},{'!', '=',2,0x95},
        {'<', '=',2,0x9e},{'>', '=',2,0x9d},
        {'<', 0,1,0x9c},{'>', 0,1,0x9f}
    };
    if(!compiler_additive(compiler))return 0;
    for(;;) {
        compiler_skip(compiler);
        if(compiler->Position>=compiler->Length)return 1;
        int operation=-1;
        for(u32 i=0;i<sizeof(operations)/sizeof(operations[0]);i++) {
            const COMPILER_COMPARISON *candidate=&operations[i];
            if(compiler->Source[compiler->Position]!=candidate->First)continue;
            if(candidate->Length==2) {
                if(compiler->Position+1>=compiler->Length||
                   compiler->Source[compiler->Position+1]!=candidate->Second)continue;
            } else if(compiler->Position+1<compiler->Length&&
                      compiler->Source[compiler->Position+1]=='=')continue;
            operation=(int)i;break;
        }
        if(operation<0)return 1;
        u8 left_type=compiler->ExpressionType;
        compiler->Position+=operations[operation].Length;
        if(!compiler_emit(compiler,0x50))return 0;
        compiler->StackBytes+=8;
        if(!compiler_additive(compiler))return 0;
        u8 right_type=compiler->ExpressionType;
        int pointer_comparison=compiler_is_pointer(left_type)||
                               compiler_is_pointer(right_type);
        if(pointer_comparison&&
           (left_type!=right_type||!compiler_is_pointer(left_type)||
            operation>1))return 0;
        u8 condition=operations[operation].SetCondition;
        if(left_type==COMPILER_TYPE_USIZE||right_type==COMPILER_TYPE_USIZE) {
            static const u8 unsigned_conditions[6]={0x94,0x95,0x96,0x93,0x92,0x97};
            condition=unsigned_conditions[operation];
        }
        if(!compiler_emit(compiler,0x59)||!compiler_emit(compiler,0x48)||
           !compiler_emit(compiler,0x39)||!compiler_emit(compiler,0xc1)||
           !compiler_emit(compiler,0x0f)||
           !compiler_emit(compiler,condition)||
           !compiler_emit(compiler,0xc0)||!compiler_emit(compiler,0x0f)||
           !compiler_emit(compiler,0xb6)||!compiler_emit(compiler,0xc0))return 0;
        compiler->StackBytes-=8;
        compiler->ExpressionType=COMPILER_TYPE_INT;
    }
}

static int compiler_logical_and(BOB64_C_COMPILER *compiler) {
    if(!compiler_comparison(compiler))return 0;
    for(;;) {
        u16 false_branch,end_branch;
        compiler_skip(compiler);
        if(compiler->Position+1>=compiler->Length||
           compiler->Source[compiler->Position]!='&'||
           compiler->Source[compiler->Position+1]!='&')return 1;
        compiler->Position+=2;
        if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x85)||
           !compiler_emit(compiler,0xc0)||!compiler_emit(compiler,0x0f)||
           !compiler_emit_branch(compiler,0x84,&false_branch)||
           !compiler_comparison(compiler)||
           !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x85)||
           !compiler_emit(compiler,0xc0)||!compiler_emit(compiler,0x0f)||
           !compiler_emit(compiler,0x95)||!compiler_emit(compiler,0xc0)||
           !compiler_emit(compiler,0x0f)||!compiler_emit(compiler,0xb6)||
           !compiler_emit(compiler,0xc0)||
           !compiler_emit_branch(compiler,0xe9,&end_branch)||
           !compiler_patch_branch(compiler,false_branch,compiler->CodePosition)||
           !compiler_emit(compiler,0x31)||!compiler_emit(compiler,0xc0)||
           !compiler_patch_branch(compiler,end_branch,compiler->CodePosition))return 0;
        compiler->ExpressionType=COMPILER_TYPE_INT;
    }
}

static int compiler_expression(BOB64_C_COMPILER *compiler) {
    if(!compiler_logical_and(compiler))return 0;
    for(;;) {
        u16 true_branch,end_branch;
        compiler_skip(compiler);
        if(compiler->Position+1>=compiler->Length||
           compiler->Source[compiler->Position]!='|'||
           compiler->Source[compiler->Position+1]!='|')return 1;
        compiler->Position+=2;
        if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x85)||
           !compiler_emit(compiler,0xc0)||!compiler_emit(compiler,0x0f)||
           !compiler_emit_branch(compiler,0x85,&true_branch)||
           !compiler_logical_and(compiler)||
           !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x85)||
           !compiler_emit(compiler,0xc0)||!compiler_emit(compiler,0x0f)||
           !compiler_emit(compiler,0x95)||!compiler_emit(compiler,0xc0)||
           !compiler_emit(compiler,0x0f)||!compiler_emit(compiler,0xb6)||
           !compiler_emit(compiler,0xc0)||
           !compiler_emit_branch(compiler,0xe9,&end_branch)||
           !compiler_patch_branch(compiler,true_branch,compiler->CodePosition)||
           !compiler_emit(compiler,0xb8)||!compiler_emit_u32(compiler,1)||
           !compiler_patch_branch(compiler,end_branch,compiler->CodePosition))return 0;
        compiler->ExpressionType=COMPILER_TYPE_INT;
    }
}

static int compiler_array_element_address(BOB64_C_COMPILER *compiler,
                                          u32 variable,u32 element) {
    u32 type=compiler->VariableType[variable];
    u32 scale=compiler_array_element_size((u8)type);
    s16 displacement=(s16)(compiler->VariableDisplacement[variable]+(s16)(element*scale));
    if(element>=compiler->VariableArrayLength[variable]||
       displacement< -128||displacement>127)return 0;
    return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x8d)&&
           compiler_emit(compiler,0x45)&&compiler_emit(compiler,(u8)(s8)displacement);
}

static int compiler_array_initializer(BOB64_C_COMPILER *compiler,u32 variable) {
    u32 element=0;
    if(compiler->VariableType[variable]==COMPILER_TYPE_CHAR_ARRAY) {
        usize offset,length;
        if(compiler_string(compiler,&offset,&length)) {
            u32 array_length=compiler_variable_array_length(compiler,variable);
            if(length>array_length)return 0;
            for(u32 i=0;i<array_length;i++) {
                u8 value=i<length?compiler->Data[offset+i]:0;
                if(!compiler_array_element_address(compiler,variable,i)||
                   !compiler_emit(compiler,0xc6)||!compiler_emit(compiler,0x45)||
                   !compiler_emit(compiler,(u8)(s8)(compiler->VariableDisplacement[variable]+(s16)i))||
                   !compiler_emit(compiler,value))return 0;
            }
            return compiler_take(compiler,';');
        }
    }
    if(!compiler_take(compiler,'{'))return 0;
    for(;;) {
        if(compiler_take(compiler,'}'))break;
        if(element>=compiler_variable_array_length(compiler,variable)||
           !compiler_array_element_address(compiler,variable,element)||
           !compiler_emit(compiler,0x50))return 0;
        compiler->StackBytes+=8;
        if(!compiler_expression(compiler)||
           compiler_is_pointer(compiler->ExpressionType)||
           !compiler_emit(compiler,0x59))return 0;
        if(compiler->VariableType[variable]==COMPILER_TYPE_CHAR_ARRAY) {
            if(!compiler_emit(compiler,0x88)||!compiler_emit(compiler,0x01))return 0;
        } else if(compiler->VariableType[variable]==COMPILER_TYPE_SHORT_ARRAY) {
            if(!compiler_emit(compiler,0x66)||!compiler_emit(compiler,0x89)||
               !compiler_emit(compiler,0x01))return 0;
        } else if(compiler->VariableType[variable]==COMPILER_TYPE_LONG_LONG_ARRAY||
                  compiler->VariableType[variable]==COMPILER_TYPE_USIZE_ARRAY) {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x89)||
               !compiler_emit(compiler,0x01))return 0;
        } else if(!compiler_emit(compiler,0x89)||!compiler_emit(compiler,0x01))return 0;
        compiler->StackBytes-=8;
        element++;
        if(compiler_take(compiler,'}'))break;
        if(!compiler_take(compiler,','))return 0;
    }
    while(element<compiler_variable_array_length(compiler,variable)) {
        if(!compiler_array_element_address(compiler,variable,element++)||
           !compiler_emit(compiler,0x50))return 0;
        compiler->StackBytes+=8;
        if(!compiler_emit(compiler,0x31)||
           !compiler_emit(compiler,0xc0)||!compiler_emit(compiler,0x59))return 0;
        if(compiler->VariableType[variable]==COMPILER_TYPE_CHAR_ARRAY) {
            if(!compiler_emit(compiler,0x88)||!compiler_emit(compiler,0x01))return 0;
        } else if(compiler->VariableType[variable]==COMPILER_TYPE_SHORT_ARRAY) {
            if(!compiler_emit(compiler,0x66)||!compiler_emit(compiler,0x89)||
               !compiler_emit(compiler,0x01))return 0;
        } else if(compiler->VariableType[variable]==COMPILER_TYPE_LONG_LONG_ARRAY||
                  compiler->VariableType[variable]==COMPILER_TYPE_USIZE_ARRAY) {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x89)||
               !compiler_emit(compiler,0x01))return 0;
        } else if(!compiler_emit(compiler,0x89)||!compiler_emit(compiler,0x01))return 0;
        compiler->StackBytes-=8;
    }
    return compiler_take(compiler,';');
}

/* Return 1 for a consumed local declaration/assignment, 0 for another statement. */
static int compiler_local_statement(BOB64_C_COMPILER *compiler) {
    usize saved=compiler->Position;
    char name[32];
    u8 long_type=0;
    int is_char=0,is_short=0,is_pointer=0,is_struct=0,is_struct_pointer=0,variable;
    u8 declared_type=0;
    if(compiler_word(compiler,"struct")) {
        char struct_name[32];
        if(!compiler_identifier(compiler,struct_name,sizeof(struct_name)))return -1;
        int structure=compiler_struct_find(compiler,struct_name);
        if(structure<0)return -1;
        declared_type=(u8)(COMPILER_TYPE_STRUCT_BASE+(u32)structure);
        is_struct=1;
    } else if(compiler_word(compiler,"char"))is_char=1;
    else if((long_type=compiler_integer_type(compiler))!=0) {
        is_short=long_type==COMPILER_TYPE_SHORT;
    } else {
        compiler->Position=saved;
        if(compiler_take(compiler,'*')) {
            if(!compiler_identifier(compiler,name,sizeof(name))) {
                compiler->Position=saved;return 0;
            }
            variable=compiler_variable_find(compiler,name);
            if(variable<0||!compiler_is_pointer(compiler_variable_type(compiler,(u32)variable))||
               !compiler_take(compiler,'=')) {
                compiler->Position=saved;return 0;
            }
            if(!compiler_variable_load(compiler,(u32)variable)||
               !compiler_emit(compiler,0x50))return -1;
            compiler->StackBytes+=8;
            if(!compiler_expression(compiler)||
               compiler_is_pointer(compiler->ExpressionType)||
               !compiler_take(compiler,';')||!compiler_emit(compiler,0x59)||
               (compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_CHAR_POINTER?
                (!compiler_emit(compiler,0x88)||!compiler_emit(compiler,0x01)):
                (compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_SHORT_POINTER?
                 (!compiler_emit(compiler,0x66)||!compiler_emit(compiler,0x89)||
                  !compiler_emit(compiler,0x01)):
                 ((compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_USIZE_POINTER||
                   compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_ISIZE_POINTER)?
                  (!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x89)||
                   !compiler_emit(compiler,0x01)):
                  (!compiler_emit(compiler,0x89)||!compiler_emit(compiler,0x01))))))return -1;
            compiler->StackBytes-=8;
            return 1;
        }
        if(!compiler_identifier(compiler,name,sizeof(name))) {
            compiler->Position=saved;return 0;
        }
        variable=compiler_variable_find(compiler,name);
        if(variable<0) {
            compiler->Position=saved;return 0;
        }
        u8 variable_type=compiler_variable_type(compiler,(u32)variable);
        int pointer_member=compiler_type_is_struct_pointer(variable_type);
        if(compiler_type_is_struct(variable_type)||pointer_member) {
            char field_name[32];
            if((pointer_member?(!compiler_take(compiler,'-')||!compiler_take(compiler,'>')):
                               !compiler_take(compiler,'.'))||
               !compiler_identifier(compiler,field_name,sizeof(field_name)))return -1;
            u8 struct_type=pointer_member?
                (u8)(COMPILER_TYPE_STRUCT_BASE+variable_type-COMPILER_TYPE_STRUCT_POINTER_BASE):
                variable_type;
            int field_index=compiler_field_find(compiler,struct_type,field_name);
            if(field_index<0)return -1;
            BOB64_C_FIELD *field=&compiler->Structs[
                struct_type-COMPILER_TYPE_STRUCT_BASE].Fields[field_index];
            int address_ok=pointer_member?
                (compiler_variable_load(compiler,(u32)variable)&&
                 (!field->Offset||(compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x05)&&
                                   compiler_emit_u32(compiler,field->Offset)))):
                compiler_member_address(compiler,(u32)variable,field);
            if(!compiler_take(compiler,'=')||!address_ok||
               !compiler_emit(compiler,0x50))return -1;
            compiler->StackBytes+=8;
            if(!compiler_expression(compiler)||
               (compiler_is_pointer(field->Type)!=compiler_is_pointer(compiler->ExpressionType))||
               (compiler_is_pointer(field->Type)&&field->Type!=compiler->ExpressionType)||
               !compiler_take(compiler,';')||!compiler_emit(compiler,0x59)||
               !compiler_member_store(compiler,field->Type))return -1;
            compiler->StackBytes-=8;
            return 1;
        }
        if(compiler_take(compiler,'[')) {
        if((!compiler_type_is_array(compiler_variable_type(compiler,(u32)variable))&&
            !compiler_is_pointer(compiler_variable_type(compiler,(u32)variable)))||
               !compiler_index_address(compiler,(u32)variable)||
               !compiler_take(compiler,'=')||!compiler_emit(compiler,0x50))return -1;
            compiler->StackBytes+=8;
            if(!compiler_expression(compiler)||
               compiler_is_pointer(compiler->ExpressionType)||
               !compiler_take(compiler,';')||!compiler_emit(compiler,0x59))return -1;
            if(compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_CHAR_POINTER||
               compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_CHAR_ARRAY) {
                if(!compiler_emit(compiler,0x88)||!compiler_emit(compiler,0x01))return -1;
            } else if(compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_SHORT_POINTER||
                      compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_SHORT_ARRAY) {
                if(!compiler_emit(compiler,0x66)||!compiler_emit(compiler,0x89)||
                   !compiler_emit(compiler,0x01))return -1;
            } else if(compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_USIZE_POINTER||
                      compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_ISIZE_POINTER||
                      compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_USIZE_ARRAY||
                      compiler_variable_type(compiler,(u32)variable)==COMPILER_TYPE_LONG_LONG_ARRAY) {
                if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x89)||
                   !compiler_emit(compiler,0x01))return -1;
            } else if(!compiler_emit(compiler,0x89)||!compiler_emit(compiler,0x01))return -1;
            compiler->StackBytes-=8;
            return 1;
        }
        if(compiler_type_is_array(compiler_variable_type(compiler,(u32)variable))||
           !compiler_take(compiler,'=')) {
            compiler->Position=saved;return 0;
        }
        if(!compiler_expression(compiler)||
           (compiler_is_pointer(compiler_variable_type(compiler,(u32)variable))!=
            compiler_is_pointer(compiler->ExpressionType)||
            (compiler_is_pointer(compiler_variable_type(compiler,(u32)variable))&&
             compiler_variable_type(compiler,(u32)variable)!=compiler->ExpressionType))||
           !compiler_take(compiler,';')||
           !compiler_variable_store(compiler,(u32)variable))return -1;
        return 1;
    }
    is_pointer=compiler_take(compiler,'*');
    if(is_struct&&is_pointer) {
        declared_type=(u8)(COMPILER_TYPE_STRUCT_POINTER_BASE+
                           declared_type-COMPILER_TYPE_STRUCT_BASE);
        is_struct=0;is_struct_pointer=1;
    }
    if(!compiler_identifier(compiler,name,sizeof(name)))return -1;
    if(is_struct) {
        if(!compiler_take(compiler,';')||
           !compiler_add_variable(compiler,name,declared_type,0,0))return -1;
        return 1;
    }
    if(compiler_take(compiler,'[')) {
        u64 array_length;
        u8 array_type=long_type==COMPILER_TYPE_USIZE?COMPILER_TYPE_USIZE_ARRAY:
                      (long_type==COMPILER_TYPE_LONG_LONG?COMPILER_TYPE_LONG_LONG_ARRAY:
                       (is_char?COMPILER_TYPE_CHAR_ARRAY:
                        (is_short?COMPILER_TYPE_SHORT_ARRAY:COMPILER_TYPE_INT_ARRAY)));
        u32 array_limit=(array_type==COMPILER_TYPE_LONG_LONG_ARRAY||
                         array_type==COMPILER_TYPE_USIZE_ARRAY)?16u:
                        (is_char?128u:(is_short?64u:32u));
        if(is_pointer||!compiler_integer(compiler,&array_length)||
           array_length>array_limit||!compiler_take(compiler,']')||
           !compiler_add_variable(compiler,name,array_type,
                                  (u32)array_length,0))return -1;
        variable=compiler_variable_find(compiler,name);
        if(compiler_take(compiler,';'))return 1;
        if(!compiler_take(compiler,'=')||
           !compiler_array_initializer(compiler,(u32)variable))return -1;
        return 1;
    }
    if(!compiler_take(compiler,'='))return -1;
    u8 type=is_struct_pointer?declared_type:
            (is_pointer?compiler_pointer_type(is_char?COMPILER_TYPE_CHAR:long_type):
             (is_char?COMPILER_TYPE_CHAR:long_type));
    if(!compiler_add_variable(compiler,name,type,0,0))return -1;
    variable=compiler_variable_find(compiler,name);
    if(!compiler_expression(compiler)||
       (compiler_is_pointer(type)!=compiler_is_pointer(compiler->ExpressionType)||
        (compiler_is_pointer(type)&&type!=compiler->ExpressionType))||
       !compiler_take(compiler,';')||
       !compiler_variable_store(compiler,(u32)variable))return -1;
    return 1;
}

static int compiler_emit_write(BOB64_C_COMPILER *compiler,usize data_offset,
                               usize data_length) {
    s64 displacement=(s64)(BOB64_PAGE_SIZE+data_offset)-
                     (s64)(compiler->CodePosition+7);
    if(displacement<(-2147483647LL-1)||displacement>2147483647)return 0;
    return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x8d)&&
       compiler_emit(compiler,0x0d)&&compiler_emit_u32(compiler,(u32)(s32)displacement)&&
       compiler_emit(compiler,0xba)&&compiler_emit_u32(compiler,(u32)data_length)&&
       compiler_emit(compiler,0xb8)&&compiler_emit_u32(compiler,BOB64_SYSCALL_WRITE_BUFFER)&&
       compiler_emit(compiler,0xcd)&&compiler_emit(compiler,0x80);
}

static int compiler_function_prologue(BOB64_C_COMPILER *compiler) {
    return compiler_emit(compiler,0x55)&&compiler_emit(compiler,0x48)&&
       compiler_emit(compiler,0x89)&&compiler_emit(compiler,0xe5)&&
       compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x81)&&
       compiler_emit(compiler,0xec)&&compiler_emit_u32(compiler,128);
}

static int compiler_function_epilogue(BOB64_C_COMPILER *compiler) {
    if(compiler->ReturnType==COMPILER_TYPE_CHAR) {
        if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x0f)||
           !compiler_emit(compiler,0xbe)||!compiler_emit(compiler,0xc0))return 0;
    } else if(compiler->ReturnType==COMPILER_TYPE_SHORT) {
        if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x0f)||
           !compiler_emit(compiler,0xbf)||!compiler_emit(compiler,0xc0))return 0;
    } else if(compiler->ReturnType!=COMPILER_TYPE_VOID&&!compiler->Return64&&
              (!compiler_emit(compiler,0x48)||
              !compiler_emit(compiler,0x63)||!compiler_emit(compiler,0xc0)))return 0;
    return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x89)&&
       compiler_emit(compiler,0xec)&&compiler_emit(compiler,0x5d)&&
       compiler_emit(compiler,0xc3);
}

static int compiler_return_type_matches(BOB64_C_COMPILER *compiler) {
    if(compiler->ReturnType==COMPILER_TYPE_VOID||
       compiler->ExpressionType==COMPILER_TYPE_VOID)return 0;
    int result_pointer=compiler_is_pointer(compiler->ReturnType);
    int expression_pointer=compiler_is_pointer(compiler->ExpressionType);
    if(result_pointer)return expression_pointer&&
        compiler->ReturnType==compiler->ExpressionType;
    return !expression_pointer||compiler->Return64;
}

static int compiler_statement(BOB64_C_COMPILER *compiler);

static int compiler_emit_branch(BOB64_C_COMPILER *compiler,u8 opcode,
                                u16 *displacement_offset) {
    if(!compiler_emit(compiler,opcode))return 0;
    *displacement_offset=(u16)compiler->CodePosition;
    return compiler_emit_u32(compiler,0);
}

static int compiler_patch_branch(BOB64_C_COMPILER *compiler,u16 displacement_offset,
                                 usize target) {
    s64 displacement=(s64)target-(s64)(displacement_offset+4u);
    if(displacement<(-2147483647LL-1)||displacement>2147483647||
       displacement_offset+4u>compiler->CodePosition)return 0;
    u32 encoded=(u32)(s32)displacement;
    for(u32 byte=0;byte<4;byte++)
        compiler->Code[displacement_offset+byte]=(u8)(encoded>>(byte*8));
    return 1;
}

static int compiler_loop_push(BOB64_C_COMPILER *compiler,usize continue_target) {
    if(compiler->LoopDepth>=sizeof(compiler->Loops)/sizeof(compiler->Loops[0])||
       continue_target>0xffffu)return 0;
    BOB64_C_LOOP *loop=&compiler->Loops[compiler->LoopDepth++];
    loop->ContinueTarget=(u16)continue_target;
    loop->BreakCount=0;loop->ContinueCount=0;
    return 1;
}

static int compiler_loop_finish(BOB64_C_COMPILER *compiler,usize break_target) {
    if(!compiler->LoopDepth)return 0;
    BOB64_C_LOOP *loop=&compiler->Loops[--compiler->LoopDepth];
    for(u32 i=0;i<loop->BreakCount;i++)
        if(!compiler_patch_branch(compiler,loop->BreakBranches[i],break_target))return 0;
    for(u32 i=0;i<loop->ContinueCount;i++)
        if(!compiler_patch_branch(compiler,loop->ContinueBranches[i],loop->ContinueTarget))return 0;
    return 1;
}

static int compiler_statement(BOB64_C_COMPILER *compiler) {
    usize saved=compiler->Position;
    if(compiler_word(compiler,"return")) {
        if(compiler->ReturnType==COMPILER_TYPE_VOID) {
            if(compiler->StackBytes||!compiler_take(compiler,';')||
               !compiler_function_epilogue(compiler))return -1;
            return 1;
        }
        if(compiler->StackBytes||!compiler_expression(compiler)||
           !compiler_return_type_matches(compiler)||
           !compiler_take(compiler,';')||!compiler_function_epilogue(compiler))return -1;
        return 1;
    }
    compiler->Position=saved;
    if(compiler_word(compiler,"break")) {
        if(!compiler->LoopDepth||!compiler_take(compiler,';'))return -1;
        BOB64_C_LOOP *loop=&compiler->Loops[compiler->LoopDepth-1];
        if(loop->BreakCount>=sizeof(loop->BreakBranches)/sizeof(loop->BreakBranches[0])||
           !compiler_emit_branch(compiler,0xe9,&loop->BreakBranches[loop->BreakCount]))return -1;
        loop->BreakCount++;
        return 1;
    }
    compiler->Position=saved;
    if(compiler_word(compiler,"continue")) {
        if(!compiler->LoopDepth||!compiler_take(compiler,';'))return -1;
        BOB64_C_LOOP *loop=&compiler->Loops[compiler->LoopDepth-1];
        if(loop->ContinueCount>=sizeof(loop->ContinueBranches)/sizeof(loop->ContinueBranches[0])||
           !compiler_emit_branch(compiler,0xe9,
                                 &loop->ContinueBranches[loop->ContinueCount]))return -1;
        loop->ContinueCount++;
        return 1;
    }
    compiler->Position=saved;
    if(compiler_word(compiler,"if")) {
        u16 false_branch,end_branch;
        if(compiler->ControlDepth>=32||!compiler_take(compiler,'(')||
           !compiler_expression(compiler)||!compiler_take(compiler,')')||
           !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x85)||
           !compiler_emit(compiler,0xc0)||!compiler_emit(compiler,0x0f)||
           !compiler_emit_branch(compiler,0x84,&false_branch))return -1;
        compiler->ControlDepth++;
        if(compiler_statement(compiler)<=0)return -1;
        compiler->ControlDepth--;
        if(compiler_word(compiler,"else")) {
            if(!compiler_emit_branch(compiler,0xe9,&end_branch)||
               !compiler_patch_branch(compiler,false_branch,compiler->CodePosition))return -1;
            compiler->ControlDepth++;
            if(compiler_statement(compiler)<=0)return -1;
            compiler->ControlDepth--;
            if(!compiler_patch_branch(compiler,end_branch,compiler->CodePosition))return -1;
        } else if(!compiler_patch_branch(compiler,false_branch,compiler->CodePosition))return -1;
        return 1;
    }
    compiler->Position=saved;
    if(compiler_word(compiler,"for")) {
        u16 loop_exit,body_branch,loop_back;
        usize loop_start,post_start,body_start;
        if(compiler->ControlDepth>=32||!compiler_take(compiler,'(')||
           !compiler_scope_push(compiler))return -1;
        if(!compiler_take(compiler,';')) {
            int init=compiler_local_statement(compiler);
            if(init<=0)return -1;
        }
        loop_start=compiler->CodePosition;
        if(compiler_take(compiler,';')) {
            if(!compiler_emit_integer(compiler,1))return -1;
        } else if(!compiler_expression(compiler)||!compiler_take(compiler,';'))return -1;
        if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x85)||
           !compiler_emit(compiler,0xc0)||!compiler_emit(compiler,0x0f)||
           !compiler_emit_branch(compiler,0x84,&loop_exit)||
           !compiler_emit_branch(compiler,0xe9,&body_branch))return -1;
        post_start=compiler->CodePosition;
        if(!compiler_take(compiler,')')&&
           (!compiler_for_post(compiler)||!compiler_take(compiler,')')))return -1;
        if(!compiler_emit_branch(compiler,0xe9,&loop_back)||
           !compiler_patch_branch(compiler,loop_back,loop_start))return -1;
        body_start=compiler->CodePosition;
        if(!compiler_patch_branch(compiler,body_branch,body_start))return -1;
        if(!compiler_loop_push(compiler,post_start))return -1;
        compiler->ControlDepth++;
        if(compiler_statement(compiler)<=0)return -1;
        compiler->ControlDepth--;
        u16 body_back;
        if(!compiler_emit_branch(compiler,0xe9,&body_back)||
           !compiler_patch_branch(compiler,body_back,post_start)||
           !compiler_patch_branch(compiler,loop_exit,compiler->CodePosition)||
           !compiler_loop_finish(compiler,compiler->CodePosition))return -1;
        compiler_scope_pop(compiler);
        return 1;
    }
    compiler->Position=saved;
    if(compiler_word(compiler,"while")) {
        usize loop_start=compiler->CodePosition;
        u16 loop_exit,loop_back;
        if(compiler->ControlDepth>=32||!compiler_take(compiler,'(')||
           !compiler_expression(compiler)||!compiler_take(compiler,')')||
           !compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x85)||
           !compiler_emit(compiler,0xc0)||!compiler_emit(compiler,0x0f)||
           !compiler_emit_branch(compiler,0x84,&loop_exit))return -1;
        if(!compiler_loop_push(compiler,loop_start))return -1;
        compiler->ControlDepth++;
        if(compiler_statement(compiler)<=0)return -1;
        compiler->ControlDepth--;
        if(!compiler_emit_branch(compiler,0xe9,&loop_back)||
           !compiler_patch_branch(compiler,loop_back,loop_start)||
           !compiler_patch_branch(compiler,loop_exit,compiler->CodePosition)||
           !compiler_loop_finish(compiler,compiler->CodePosition))return -1;
        return 1;
    }
    compiler->Position=saved;
    if(compiler_take(compiler,'{')) {
        if(compiler->ControlDepth>=32||!compiler_scope_push(compiler))return -1;
        compiler->ControlDepth++;
        for(;;) {
            if(compiler_take(compiler,'}'))break;
            if(compiler_statement(compiler)<=0)return -1;
        }
        compiler->ControlDepth--;
        compiler_scope_pop(compiler);
        return 1;
    }
    compiler->Position=saved;
    char call_name[32];
    if(compiler_identifier(compiler,call_name,sizeof(call_name))) {
        compiler_skip(compiler);
        if(compiler_function_find(compiler,call_name)>=0&&
           compiler_variable_find(compiler,call_name)<0&&
           compiler->Position<compiler->Length&&
           compiler->Source[compiler->Position]=='(') {
            compiler->Position=saved;
            if(!compiler_identifier(compiler,call_name,sizeof(call_name))||
               !compiler_emit_call(compiler,call_name)||
               !compiler_take(compiler,';'))return -1;
            return 1;
        }
    }
    compiler->Position=saved;
    if(compiler_word(compiler,"bob64_app_write")) {
        usize string_offset,string_length;
        if(!compiler_take(compiler,'(')||
           !compiler_string(compiler,&string_offset,&string_length)||
           !compiler_take(compiler,',')||!compiler_integer(compiler,&compiler->IntegerValue)||
           compiler->IntegerValue!=string_length||
           !compiler_take(compiler,')')||!compiler_take(compiler,';')||
           !compiler_emit_write(compiler,string_offset,string_length))return -1;
        return 1;
    }
    compiler->Position=saved;
    if(compiler_take(compiler,';'))return 1;
    return compiler_local_statement(compiler);
}

static int compiler_for_post(BOB64_C_COMPILER *compiler) {
    char name[32];
    if(!compiler_identifier(compiler,name,sizeof(name)))return 0;
    int variable=compiler_variable_find(compiler,name);
    if(variable<0||compiler_type_is_array(compiler_variable_type(compiler,(u32)variable))||
       !compiler_take(compiler,'=')||!compiler_expression(compiler)||
       (compiler_is_pointer(compiler_variable_type(compiler,(u32)variable))!=
        compiler_is_pointer(compiler->ExpressionType)||
        (compiler_is_pointer(compiler_variable_type(compiler,(u32)variable))&&
         compiler_variable_type(compiler,(u32)variable)!=compiler->ExpressionType))||
       !compiler_variable_store(compiler,(u32)variable))return 0;
    return 1;
}

static int compiler_function_body(BOB64_C_COMPILER *compiler) {
    for(;;) {
        usize saved=compiler->Position;
        if(compiler_word(compiler,"return")) {
            compiler->Position=saved;
            break;
        }
        compiler->Position=saved;
        int statement=compiler_statement(compiler);
        if(statement<0)return 0;
        if(statement>0)continue;
        break;
    }
    if(compiler->ReturnType==COMPILER_TYPE_VOID) {
        if(compiler->StackBytes)return 0;
        if(compiler_word(compiler,"return")) {
            if(!compiler_take(compiler,';')||!compiler_take(compiler,'}'))return 0;
        } else if(!compiler_take(compiler,'}'))return 0;
        return compiler_function_epilogue(compiler);
    }
    if(compiler->StackBytes||!compiler_word(compiler,"return")||
       !compiler_expression(compiler)||
       !compiler_return_type_matches(compiler)||
       !compiler_take(compiler,';')||!compiler_take(compiler,'}')||
       !compiler_function_epilogue(compiler))return 0;
    return 1;
}

static int compiler_store_parameter(BOB64_C_COMPILER *compiler,u32 variable,
                                    u32 argument) {
    s8 displacement=(s8)compiler->VariableDisplacement[variable];
    u8 type=compiler->VariableType[variable];
    if(argument>=4) {
        u32 incoming=16u+argument*8u;
        u8 load_modrm=incoming<=127u?0x45:0x85;
        if(type==COMPILER_TYPE_SHORT)
            return compiler_emit(compiler,0x0f)&&compiler_emit(compiler,0xbf)&&
                   compiler_emit(compiler,load_modrm)&&
                   (incoming<=127u?compiler_emit(compiler,(u8)incoming):
                    compiler_emit_u32(compiler,incoming))&&
                   compiler_emit(compiler,0x66)&&compiler_emit(compiler,0x89)&&
                   compiler_emit(compiler,0x45)&&compiler_emit(compiler,(u8)displacement);
        if(type==COMPILER_TYPE_CHAR)
            return compiler_emit(compiler,0x0f)&&compiler_emit(compiler,0xbe)&&
                   compiler_emit(compiler,load_modrm)&&
                   (incoming<=127u?compiler_emit(compiler,(u8)incoming):
                    compiler_emit_u32(compiler,incoming))&&
                   compiler_emit(compiler,0x88)&&compiler_emit(compiler,0x45)&&
                   compiler_emit(compiler,(u8)displacement);
        if(compiler_is_pointer(type)||type==COMPILER_TYPE_USIZE||
           type==COMPILER_TYPE_LONG_LONG) {
            if(!compiler_emit(compiler,0x48)||!compiler_emit(compiler,0x8b)||
               !compiler_emit(compiler,load_modrm)||
               !(incoming<=127u?compiler_emit(compiler,(u8)incoming):
                 compiler_emit_u32(compiler,incoming)))return 0;
            return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x89)&&
                   compiler_emit(compiler,0x45)&&compiler_emit(compiler,(u8)displacement);
        }
        if(!compiler_emit(compiler,0x8b)||!compiler_emit(compiler,load_modrm)||
           !(incoming<=127u?compiler_emit(compiler,(u8)incoming):
             compiler_emit_u32(compiler,incoming)))return 0;
        return compiler_emit(compiler,0x89)&&compiler_emit(compiler,0x45)&&
               compiler_emit(compiler,(u8)displacement);
    }
    if(compiler_is_pointer(type)||type==COMPILER_TYPE_USIZE||
       type==COMPILER_TYPE_LONG_LONG) {
        if(argument<2) {
            u8 modrm=argument==0?0x4d:0x55;
            return compiler_emit(compiler,0x48)&&compiler_emit(compiler,0x89)&&
                   compiler_emit(compiler,modrm)&&compiler_emit(compiler,(u8)displacement);
        }
        return compiler_emit(compiler,0x4c)&&compiler_emit(compiler,0x89)&&
               compiler_emit(compiler,(u8)(argument==2?0x45:0x4d))&&
               compiler_emit(compiler,(u8)displacement);
    }
    if(type==COMPILER_TYPE_CHAR) {
        if(argument==0)return compiler_emit(compiler,0x88)&&compiler_emit(compiler,0x4d)&&
                               compiler_emit(compiler,(u8)displacement);
        if(argument==1)return compiler_emit(compiler,0x88)&&compiler_emit(compiler,0x55)&&
                               compiler_emit(compiler,(u8)displacement);
        return compiler_emit(compiler,0x41)&&compiler_emit(compiler,0x88)&&
               compiler_emit(compiler,(u8)(argument==2?0x45:0x4d))&&
               compiler_emit(compiler,(u8)displacement);
    }
    if(type==COMPILER_TYPE_SHORT) {
        static const u8 short_stores[4][4]={
            {0x66,0x89,0x4d,0},{0x66,0x89,0x55,0},
            {0x66,0x44,0x89,0x45},{0x66,0x44,0x89,0x4d}
        };
        u32 instruction_bytes=argument<2?3u:4u;
        for(u32 byte=0;byte<instruction_bytes;byte++)
            if(!compiler_emit(compiler,short_stores[argument][byte]))return 0;
        return compiler_emit(compiler,(u8)displacement);
    }
    if(argument==0)return compiler_emit(compiler,0x89)&&compiler_emit(compiler,0x4d)&&
                           compiler_emit(compiler,(u8)displacement);
    if(argument==1)return compiler_emit(compiler,0x89)&&compiler_emit(compiler,0x55)&&
                           compiler_emit(compiler,(u8)displacement);
    return compiler_emit(compiler,0x44)&&compiler_emit(compiler,0x89)&&
           compiler_emit(compiler,(u8)(argument==2?0x45:0x4d))&&
           compiler_emit(compiler,(u8)displacement);
}

static int compiler_function_definition(BOB64_C_COMPILER *compiler) {
    char name[32],parameter_names[COMPILER_ARGUMENT_LIMIT][32];
    u8 argument_types[COMPILER_ARGUMENT_LIMIT],return_type,argument_count=0;
    if(!compiler_function_signature(compiler,name,sizeof(name),&return_type,
           &argument_count,argument_types,parameter_names,1))return 0;
    int is_main=name[0]=='m'&&name[1]=='a'&&name[2]=='i'&&name[3]=='n'&&!name[4];
    if((is_main&&(argument_count||return_type==COMPILER_TYPE_VOID))||
       !compiler_take(compiler,'{')||
       !compiler_function_declare(compiler,name,(u16)compiler->CodePosition,
           return_type,(u8)argument_count,argument_types,1))return 0;
    compiler->VariableCount=0;compiler->LocalBytes=0;compiler->ScopeDepth=0;
    compiler->ReturnType=return_type;
    compiler->Return64=return_type==COMPILER_TYPE_LONG_LONG||
                       return_type==COMPILER_TYPE_USIZE||
                       compiler_is_pointer(return_type);
    compiler->StackBytes=0;compiler->ControlDepth=0;
    if(!compiler_function_prologue(compiler)||
       (is_main&&!compiler_emit_global_initializers(compiler)))return 0;
    for(u32 i=0;i<argument_count;i++) {
        u32 variable;
        if(!compiler_add_variable(compiler,parameter_names[i],argument_types[i],0,
                                  &variable)||
           !compiler_store_parameter(compiler,variable,i))return 0;
    }
    return compiler_function_body(compiler);
}

static int compiler_resolve_calls(BOB64_C_COMPILER *compiler) {
    for(u32 i=0;i<compiler->CallCount;i++) {
        BOB64_C_CALL *call=&compiler->Calls[i];
        int function=compiler_function_find(compiler,call->Name);
        if(function<0||!compiler->Functions[function].Defined||
           compiler->Functions[function].ArgumentCount!=call->ArgumentCount)
            return 0;
        for(u32 argument=0;argument<call->ArgumentCount;argument++) {
            u8 expected=compiler->Functions[function].ArgumentTypes[argument];
            u8 actual=call->ArgumentTypes[argument];
            if(compiler_is_pointer(expected)!=compiler_is_pointer(actual)||
               (compiler_is_pointer(expected)&&expected!=actual))return 0;
        }
        s64 displacement=(s64)compiler->Functions[function].CodeOffset-
                         (s64)(call->DisplacementOffset+4u);
        if(displacement<(-2147483647LL-1)||displacement>2147483647)return 0;
        u32 encoded=(u32)(s32)displacement;
        for(u32 byte=0;byte<4;byte++)
            compiler->Code[call->DisplacementOffset+byte]=(u8)(encoded>>(byte*8));
    }
    return 1;
}

static u32 compiler_crc32(const u8 *bytes,usize length) {
    u32 value=0xffffffffu;
    for(usize i=0;i<length;i++) {
        value^=bytes[i];
        for(u32 bit=0;bit<8;bit++)
            value=(value>>1)^(0xedb88320u&-(value&1u));
    }
    return ~value;
}

static void compiler_write16(u8 *bytes,u16 value) {
    bytes[0]=(u8)value;bytes[1]=(u8)(value>>8);
}

static void compiler_write32(u8 *bytes,u32 value) {
    for(u32 i=0;i<4;i++)bytes[i]=(u8)(value>>(i*8));
}

static void compiler_write64(u8 *bytes,u64 value) {
    compiler_write32(bytes,(u32)value);compiler_write32(bytes+4,(u32)(value>>32));
}

static int compiler_scan_skip_quote(BOB64_C_COMPILER *compiler,usize *position) {
    char quote=compiler->Source[(*position)++];
    while(*position<compiler->Length) {
        char value=compiler->Source[(*position)++];
        if(value=='\\') {
            if(*position>=compiler->Length)return 0;
            (*position)++;
        } else if(value==quote)return 1;
    }
    return 0;
}

static int compiler_scan_skip_comment(BOB64_C_COMPILER *compiler,usize *position) {
    if(*position+1>=compiler->Length||compiler->Source[*position]!='/')return 0;
    if(compiler->Source[*position+1]=='/') {
        *position+=2;
        while(*position<compiler->Length&&compiler->Source[*position]!='\n')(*position)++;
        return 1;
    }
    if(compiler->Source[*position+1]=='*') {
        *position+=2;
        while(*position+1<compiler->Length&&
              !(compiler->Source[*position]=='*'&&compiler->Source[*position+1]=='/'))
            (*position)++;
        if(*position+1>=compiler->Length)return -1;
        *position+=2;
        return 1;
    }
    return 0;
}

static int compiler_global_add(BOB64_C_COMPILER *compiler,const char *name,
        u8 type,u32 array_length,u32 bytes,u32 *global_index) {
    usize name_length=0;
    if(compiler->GlobalCount>=COMPILER_GLOBAL_LIMIT||
       compiler_variable_find(compiler,name)>=0||bytes>sizeof(compiler->Data)-compiler->DataLength)
        return 0;
    u32 alignment=compiler_type_is_struct(type)?compiler_type_alignment(compiler,type):
        ((type==COMPILER_TYPE_LONG_LONG||type==COMPILER_TYPE_USIZE||
          type==COMPILER_TYPE_LONG_LONG_ARRAY||type==COMPILER_TYPE_USIZE_ARRAY||
          compiler_is_pointer(type))?8u:
         (type==COMPILER_TYPE_INT||type==COMPILER_TYPE_INT_ARRAY?4u:
          (type==COMPILER_TYPE_SHORT||type==COMPILER_TYPE_SHORT_ARRAY?2u:1u)));
    compiler->DataLength=(compiler->DataLength+alignment-1u)&~(alignment-1u);
    if(bytes>sizeof(compiler->Data)-compiler->DataLength)return 0;
    u32 index=compiler->GlobalCount++;
    BOB64_C_GLOBAL *global=&compiler->Globals[index];
    while(name[name_length]&&name_length+1<sizeof(global->Name)) {
        global->Name[name_length]=name[name_length];name_length++;
    }
    if(name[name_length])return 0;
    global->Name[name_length]=0;
    global->Type=type;
    global->ArrayLength=(u16)array_length;
    global->InitKind=0;global->InitTarget=0;
    global->DataOffset=(u16)compiler->DataLength;
    for(u32 i=0;i<bytes;i++)compiler->Data[compiler->DataLength+i]=0;
    compiler->DataLength+=bytes;
    if(global_index)*global_index=index;
    return 1;
}

static int compiler_struct_definition(BOB64_C_COMPILER *compiler,const char *name) {
    usize name_length=0;
    if(compiler->StructCount>=COMPILER_STRUCT_LIMIT||
       compiler_struct_find(compiler,name)>=0)return 0;
    u32 index=compiler->StructCount++;
    BOB64_C_STRUCT *structure=&compiler->Structs[index];
    while(name[name_length]&&name_length+1<sizeof(structure->Name)) {
        structure->Name[name_length]=name[name_length];name_length++;
    }
    if(name[name_length])return 0;
    structure->Name[name_length]=0;structure->Size=0;
    structure->Alignment=1;structure->FieldCount=0;
    u32 offset=0;
    for(;;) {
        if(compiler_take(compiler,'}'))break;
        int is_char=compiler_word(compiler,"char"),struct_index=-1;
        u8 long_type=is_char?COMPILER_TYPE_CHAR:compiler_integer_type(compiler);
        if(!long_type) {
            if(compiler_word(compiler,"struct")) {
                char field_struct_name[32];
                if(!compiler_identifier(compiler,field_struct_name,
                                       sizeof(field_struct_name)))return 0;
                struct_index=compiler_struct_find(compiler,field_struct_name);
                if(struct_index<0)return 0;
            } else return 0;
        }
        int pointer=compiler_take(compiler,'*');
        if(struct_index>=0&&!pointer)return 0;
        char field_name[32];
        if(!compiler_identifier(compiler,field_name,sizeof(field_name))||
           !compiler_take(compiler,';')||structure->FieldCount>=COMPILER_FIELD_LIMIT||
           compiler_field_find(compiler,(u8)(COMPILER_TYPE_STRUCT_BASE+index),field_name)>=0)
            return 0;
        u8 field_type=struct_index>=0?
                      (u8)(COMPILER_TYPE_STRUCT_POINTER_BASE+(u32)struct_index):
                      (pointer?compiler_pointer_type(is_char?COMPILER_TYPE_CHAR:long_type):
                       long_type);
        u32 alignment=compiler_type_alignment(compiler,field_type);
        u32 size=compiler_type_size(compiler,field_type);
        offset=(offset+alignment-1u)&~(alignment-1u);
        BOB64_C_FIELD *field=&structure->Fields[structure->FieldCount++];
        usize field_length=0;
        while(field_name[field_length]) {
            field->Name[field_length]=field_name[field_length];field_length++;
        }
        field->Name[field_length]=0;field->Offset=(u16)offset;field->Type=field_type;
        offset+=size;
        if(alignment>structure->Alignment)structure->Alignment=(u8)alignment;
        if(offset>2048)return 0;
    }
    if(!structure->FieldCount)return 0;
    structure->Size=(u16)((offset+structure->Alignment-1u)&~(structure->Alignment-1u));
    return compiler_take(compiler,';');
}

static int compiler_global_declaration(BOB64_C_COMPILER *compiler) {
    char name[32];
    u8 type;
    u32 array_length=0,bytes,index;
    int is_char=0,is_short=0,is_pointer=0,is_struct=0,is_struct_pointer=0;
    u8 long_type=0;
    if(compiler_word(compiler,"static")) { }
    if(compiler_word(compiler,"struct")) {
        char struct_name[32];
        if(!compiler_identifier(compiler,struct_name,sizeof(struct_name)))return 0;
        if(compiler_take(compiler,'{'))return compiler_struct_definition(compiler,struct_name);
        int structure=compiler_struct_find(compiler,struct_name);
        if(structure<0)return 0;
        type=(u8)(COMPILER_TYPE_STRUCT_BASE+(u32)structure);
        is_struct=1;
    } else if(compiler_word(compiler,"char"))is_char=1;
    else if((long_type=compiler_integer_type(compiler))==0)return 0;
    is_short=long_type==COMPILER_TYPE_SHORT;
    is_pointer=compiler_take(compiler,'*');
    if(is_struct&&is_pointer) {
        type=(u8)(COMPILER_TYPE_STRUCT_POINTER_BASE+
                  type-COMPILER_TYPE_STRUCT_BASE);
        is_struct=0;is_struct_pointer=1;
    }
    if(!compiler_identifier(compiler,name,sizeof(name)))return 0;
    int array=compiler_take(compiler,'[');
    if(array) {
        u64 size;
        if(is_pointer||is_struct||!compiler_integer(compiler,&size)||!size||
           !compiler_take(compiler,']'))return 0;
        type=long_type==COMPILER_TYPE_USIZE?COMPILER_TYPE_USIZE_ARRAY:
             (long_type==COMPILER_TYPE_LONG_LONG?COMPILER_TYPE_LONG_LONG_ARRAY:
              (is_char?COMPILER_TYPE_CHAR_ARRAY:
               (is_short?COMPILER_TYPE_SHORT_ARRAY:COMPILER_TYPE_INT_ARRAY)));
        u32 element_size=compiler_array_element_size(type);
        u32 maximum=sizeof(compiler->Data)/element_size;
        if(size>maximum)return 0;
        array_length=(u32)size;
        bytes=array_length*element_size;
    } else {
        if(!is_struct&&!is_struct_pointer)
            type=is_pointer?compiler_pointer_type(is_char?COMPILER_TYPE_CHAR:long_type):
             (is_char?COMPILER_TYPE_CHAR:long_type);
        bytes=is_struct?compiler_type_size(compiler,type):
              (is_pointer?8u:compiler_type_size(compiler,type));
    }
    if(!compiler_global_add(compiler,name,type,array_length,bytes,&index))return 0;
    BOB64_C_GLOBAL *global=&compiler->Globals[index];
    if(compiler_take(compiler,'=')) {
        if(is_struct)return 0;
        if(is_pointer) {
            usize string_offset,string_length;
            if(is_char&&compiler_string(compiler,&string_offset,&string_length)) {
                (void)string_length;
                global->InitKind=2;
                global->InitTarget=(u16)string_offset;
            } else {
                int address=compiler_take(compiler,'&');
                char target_name[32];
                if(compiler_identifier(compiler,target_name,sizeof(target_name))) {
                    int target=compiler_variable_find(compiler,target_name);
                    if(target<0||!compiler_variable_is_global((u32)target))return 0;
                    BOB64_C_GLOBAL *target_global=&compiler->Globals[(u32)target&0x7fffu];
                    u8 target_type=target_global->Type;
                    u8 target_pointer_type;
                    if(compiler_type_is_struct(target_type))
                        target_pointer_type=(u8)(COMPILER_TYPE_STRUCT_POINTER_BASE+
                            target_type-COMPILER_TYPE_STRUCT_BASE);
                    else if(compiler_type_is_array(target_type))
                        target_pointer_type=compiler_pointer_type(target_type);
                    else if(address)
                        target_pointer_type=compiler_pointer_type(target_type);
                    else target_pointer_type=0;
                    if(target_pointer_type!=type||
                       (compiler_type_is_struct(target_type)&&!address))return 0;
                    global->InitKind=1;
                    global->InitTarget=target_global->DataOffset;
                } else {
                    u64 value;
                    if(!compiler_integer(compiler,&value)||value!=0)return 0;
                }
            }
        } else if(array&&is_char) {
            usize string_offset,string_length;
            if(compiler_string(compiler,&string_offset,&string_length)) {
                if(string_length>array_length)return 0;
                for(u32 i=0;i<string_length;i++)
                    compiler->Data[global->DataOffset+i]=compiler->Data[string_offset+i];
            } else {
                if(!compiler_take(compiler,'{'))return 0;
                u32 element=0;
                for(;;) {
                    if(compiler_take(compiler,'}'))break;
                    u64 value;
                    if(element>=array_length||
                       (!compiler_character(compiler,&value)&&
                        !compiler_integer(compiler,&value)))return 0;
                    compiler->Data[global->DataOffset+element++]=(u8)value;
                    if(compiler_take(compiler,'}'))break;
                    if(!compiler_take(compiler,','))return 0;
                }
            }
        } else if(array) {
            if(!compiler_take(compiler,'{'))return 0;
            u32 element=0;
            for(;;) {
                if(compiler_take(compiler,'}'))break;
                u64 value;
                if(element>=array_length||
                   (!compiler_character(compiler,&value)&&
                    !compiler_integer(compiler,&value)))return 0;
                u32 element_size=compiler_array_element_size((u8)type);
                u32 offset=global->DataOffset+element++*element_size;
                for(u32 byte=0;byte<element_size;byte++)
                    compiler->Data[offset+byte]=(u8)(value>>(byte*8));
                if(compiler_take(compiler,'}'))break;
                if(!compiler_take(compiler,','))return 0;
            }
        } else {
            u64 value;
            if(!compiler_character(compiler,&value)&&
               !compiler_integer(compiler,&value))return 0;
            for(u32 byte=0;byte<bytes;byte++)
                compiler->Data[global->DataOffset+byte]=(u8)(value>>(byte*8));
        }
    }
    return compiler_take(compiler,';');
}

static int compiler_collect_function_sources(BOB64_C_COMPILER *compiler,
                                             u32 *main_source_index) {
    usize scan=0;
    u32 main_count=0;
    compiler->FunctionSourceCount=0;
    while(scan<compiler->Length) {
        compiler->Position=scan;
        compiler_skip(compiler);
        if(compiler->Failed)return 0;
        if(compiler->Position>=compiler->Length)break;
        usize start=compiler->Position;
        char name[32],parameter_names[COMPILER_ARGUMENT_LIMIT][32];
        u8 return_type,argument_count,argument_types[COMPILER_ARGUMENT_LIMIT];
        if(!compiler_function_signature(compiler,name,sizeof(name),&return_type,
             &argument_count,argument_types,parameter_names,0)) {
            compiler->Position=start;
            if(!compiler_global_declaration(compiler))return 0;
            scan=compiler->Position;
            continue;
        }
        if(!compiler_function_declare(compiler,name,0,return_type,argument_count,
                                      argument_types,0))return 0;
        compiler_skip(compiler);
        if(compiler->Position>=compiler->Length)return 0;
        if(compiler->Source[compiler->Position]==';') {
            compiler->Position++;
            scan=compiler->Position;
            continue;
        }
        if(compiler->Source[compiler->Position]!='{'||
           compiler->FunctionSourceCount>=COMPILER_FUNCTION_LIMIT)return 0;
        compiler->Position++;
        usize position=compiler->Position;
        u32 depth=1;
        while(position<compiler->Length&&depth) {
            char value=compiler->Source[position];
            if(value=='/'&&position+1<compiler->Length) {
                usize next=position;
                int skipped=compiler_scan_skip_comment(compiler,&next);
                if(skipped<0)return 0;
                if(skipped) { position=next;continue; }
            }
            if(value=='\''||value=='"') {
                if(!compiler_scan_skip_quote(compiler,&position))return 0;
                continue;
            }
            if(value=='{')depth++;
            else if(value=='}')depth--;
            position++;
        }
        if(depth)return 0;
        usize end=position;
        u32 source_index=compiler->FunctionSourceCount++;
        compiler->FunctionSourceStart[source_index]=(u16)start;
        compiler->FunctionSourceEnd[source_index]=(u16)end;
        if(name[0]=='m'&&name[1]=='a'&&name[2]=='i'&&name[3]=='n'&&!name[4]) {
            *main_source_index=source_index;
            main_count++;
        }
        scan=end;
    }
    compiler->Position=0;
    return main_count==1;
}

int bob64_compile_c(const char *source,usize source_length,void *output,
                    usize output_capacity,usize *output_length,
                    usize *error_offset) {
    /* The initial kernel is single-tasked; a later compiler job queue needs per-job state. */
    static BOB64_C_COMPILER compiler;
    static u8 payload[2*BOB64_PAGE_SIZE];
    u8 *file=(u8 *)output;
    u32 main_source_index=0;
    if(output_length)*output_length=0;
    if(error_offset)*error_offset=0;
    if(!source||!output||!output_length||source_length>BOB64_COMPILER_SOURCE_LIMIT||
       output_capacity<BOB64_COMPILER_IMAGE_LIMIT)return -1;
    compiler.Source=source;compiler.Length=source_length;compiler.Position=0;
    compiler.ErrorOffset=0;compiler.CodePosition=0;compiler.DataLength=0;
    compiler.IntegerValue=0;compiler.Return64=0;compiler.Failed=0;
    compiler.VariableCount=0;
    compiler.FunctionCount=0;compiler.GlobalCount=0;compiler.StructCount=0;
    compiler.CallCount=0;compiler.LoopDepth=0;
    compiler.FunctionSourceCount=0;
    for(usize i=0;i<sizeof(compiler.Data);i++)compiler.Data[i]=0;
    for(usize i=0;i<sizeof(compiler.VariableNames);i++)
        ((u8 *)compiler.VariableNames)[i]=0;
    for(usize i=0;i<sizeof(compiler.VariableDisplacement);i++)
        ((u8 *)compiler.VariableDisplacement)[i]=0;
    for(usize i=0;i<sizeof(compiler.VariableType);i++)compiler.VariableType[i]=0;
    for(usize i=0;i<sizeof(compiler.VariableArrayLength);i++)
        compiler.VariableArrayLength[i]=0;
    compiler.LocalBytes=0;compiler.StackBytes=0;compiler.ControlDepth=0;
    if(!compiler_collect_function_sources(&compiler,&main_source_index))goto syntax_error;
    /* The entry shim follows Microsoft x64: shadow space, call, then syscall exit. */
    compiler.CodePosition=0;
    if(!compiler_emit(&compiler,0x48)||!compiler_emit(&compiler,0x83)||
       !compiler_emit(&compiler,0xec)||!compiler_emit(&compiler,0x28)||
       !compiler_emit(&compiler,0xe8)||!compiler_emit_u32(&compiler,32-9)||
       !compiler_emit(&compiler,0x48)||!compiler_emit(&compiler,0x89)||
       !compiler_emit(&compiler,0xc1)||!compiler_emit(&compiler,0xb8)||
       !compiler_emit_u32(&compiler,BOB64_SYSCALL_EXIT)||
       !compiler_emit(&compiler,0xcd)||!compiler_emit(&compiler,0x80)||
       !compiler_emit(&compiler,0x0f)||!compiler_emit(&compiler,0x0b))goto syntax_error;
    while(compiler.CodePosition<32)if(!compiler_emit(&compiler,0x90))goto syntax_error;
    compiler.Position=compiler.FunctionSourceStart[main_source_index];
    if(!compiler_function_definition(&compiler)||
       compiler.Position!=compiler.FunctionSourceEnd[main_source_index])goto syntax_error;
    for(u32 function=0;function<compiler.FunctionSourceCount;function++) {
        if(function==main_source_index)continue;
        compiler.Position=compiler.FunctionSourceStart[function];
        if(!compiler_function_definition(&compiler)||
           compiler.Position!=compiler.FunctionSourceEnd[function])goto syntax_error;
    }
    if(compiler.Failed||!compiler_resolve_calls(&compiler))goto syntax_error;
    for(usize i=0;i<sizeof(compiler.Code);i++)
        payload[i]=i<compiler.CodePosition?compiler.Code[i]:0x90;
    for(usize i=0;i<sizeof(compiler.Data);i++)payload[BOB64_PAGE_SIZE+i]=compiler.Data[i];
    for(usize i=0;i<BOB64_EXEC_HEADER_SIZE;i++)file[i]=0;
    file[0]='B';file[1]='6';file[2]='4';file[3]='E';
    compiler_write16(file+4,BOB64_EXEC_VERSION);
    compiler_write16(file+6,BOB64_EXEC_HEADER_SIZE);
    compiler_write32(file+8,BOB64_APP_ABI_VERSION);
    compiler_write64(file+16,sizeof(payload));
    compiler_write64(file+24,sizeof(payload));
    compiler_write64(file+32,0);
    compiler_write64(file+40,BOB64_PAGE_SIZE);
    compiler_write32(file+48,compiler_crc32(payload,sizeof(payload)));
    for(usize i=0;i<sizeof(payload);i++)file[BOB64_EXEC_HEADER_SIZE+i]=payload[i];
    *output_length=BOB64_COMPILER_IMAGE_LIMIT;
    return 0;
syntax_error:
    if(error_offset)*error_offset=compiler.Position<compiler.Length?
                                  compiler.Position:compiler.Length;
    return -2;
}
