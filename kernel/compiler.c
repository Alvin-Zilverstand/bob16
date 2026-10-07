/* Resident recursive-descent compiler. It emits native instruction words in RAM. */
#define TOKEN_ID 256
#define TOKEN_NUMBER 257
#define TOKEN_STRING 258
#define TOKEN_EQ 259
#define TOKEN_NE 260
#define TOKEN_LE 261
#define TOKEN_GE 262
#define TOKEN_AND 263
#define TOKEN_OR 264
#define TOKEN_INC 265
#define TOKEN_DEC 266
#define TOKEN_SHL 267
#define TOKEN_SHR 268
#define TOKEN_ADD_ASSIGN 269
#define TOKEN_SUB_ASSIGN 270
#define TOKEN_MUL_ASSIGN 271
#define TOKEN_DIV_ASSIGN 272
#define TOKEN_MOD_ASSIGN 273
#define TOKEN_AND_ASSIGN 274
#define TOKEN_OR_ASSIGN 275
#define TOKEN_XOR_ASSIGN 276
#ifdef BOBC_WIDE
#define TOKEN_SHL_ASSIGN 277
#define TOKEN_SHR_ASSIGN 278
#endif
#if BOBC_LEGACY
#define CALL_R3 0xcb00
#else
#define CALL_R3 0xc300
#endif
#ifdef BOBC_WIDE
#define CC_PROGRAM_BASE WIDE_PROGRAM_BASE
#define CC_VARIABLE_BASE WIDE_VARIABLE_BASE
#else
#define CC_PROGRAM_BASE PROGRAM_BASE
#define CC_VARIABLE_BASE 0x9800
#endif
char *cc_source;
int cc_token;
int cc_value;
#define cc_text ((char *)0xe380)
int cc_error;
int cc_position;
/* Editor and compiler run separately, so they share reserved scratch RAM. */
#ifdef BOBC_WIDE
#define cc_code ((int *)WIDE_PROGRAM_BASE)
#else
#define cc_code ((int *)0xe000)
#endif
#define cc_names ((char *)0xe200)
int cc_variables;
int cc_error_offset;
int cc_depth;
char *cc_begin;
#ifdef BOBC_WIDE
int cc_scope_depth;
int cc_scope_floor[33];
#endif

int cc_alpha(int ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'; }
int cc_digit(int ch) { return ch >= '0' && ch <= '9'; }
void cc_fail(void) {
    if (!cc_error) cc_error_offset = (int)(cc_source - cc_begin);
    cc_error = 1; cc_token = 0;
}
void cc_next(void) {
    int ch; int count; int digit; int base;
    while (1) {
        while (*cc_source == ' ' || *cc_source == '\t' || *cc_source == '\n' || *cc_source == '\r') cc_source++;
        if (cc_source[0] == '/' && cc_source[1] == '/') {
            while (*cc_source && *cc_source != '\n') cc_source++;
        } else if (cc_source[0] == '/' && cc_source[1] == '*') {
            cc_source = cc_source + 2;
            while (*cc_source && !(cc_source[0] == '*' && cc_source[1] == '/')) cc_source++;
            if (!*cc_source) { cc_fail(); return; }
            cc_source = cc_source + 2;
        } else break;
    }
    ch = *cc_source; cc_text[0] = 0;
    if (ch == 0) { cc_token = 0; return; }
    if (cc_alpha(ch)) {
        count = 0;
        while (cc_alpha(*cc_source) || cc_digit(*cc_source)) {
            if (count == 63) { cc_fail(); return; }
            cc_text[count++] = *cc_source++;
        }
        cc_text[count] = 0; cc_token = TOKEN_ID; return;
    }
    if (cc_digit(ch)) {
        cc_value = 0; count = 0; base = 10;
        if (cc_source[0] == '0' && (cc_source[1] == 'x' || cc_source[1] == 'X')) { base = 16; cc_source = cc_source + 2; }
        else if(ch == '0') base = 8;
        while (1) {
            digit = *cc_source;
            if (cc_digit(digit)) digit = digit - '0';
            else if (base == 16 && digit >= 'a' && digit <= 'f') digit = digit - 'a' + 10;
            else if (base == 16 && digit >= 'A' && digit <= 'F') digit = digit - 'A' + 10;
            else break;
            if(digit >= base) cc_fail();
#ifdef BOBC_WIDE
            if ((base == 10 && (cc_value > 214748364 || (cc_value == 214748364 && digit > 7))) ||
                (base == 16 && (count == 8 || (count == 0 && digit > 7))) ||
                (base == 8 && (cc_value > 268435455 || (cc_value == 268435455 && digit > 7)))) { cc_fail(); return; }
#else
            if ((base == 10 && (cc_value > 3276 || (cc_value == 3276 && digit > 7))) ||
                (base == 16 && count == 4) || (base == 8 && (cc_value & -8192))) { cc_fail(); return; }
#endif
            cc_value = cc_value * base + digit; cc_source++; count++;
        }
        if (!count) cc_fail();
        cc_token = TOKEN_NUMBER; return;
    }
    if (ch == '"' || ch == '\'') {
        cc_source++; count = 0;
        while (*cc_source && *cc_source != ch) {
            digit = *cc_source++;
            if(digit == '\n' || digit == '\r') cc_fail();
            if (digit == '\\') {
                if (!*cc_source) { cc_fail(); break; }
                digit = *cc_source++;
                if (digit == 'n') digit = '\n';
                else if (digit == 't') digit = '\t';
                else if (digit == 'r') digit = '\r';
                else if (digit == 'b') digit = '\b';
                else if (digit == 'a') digit = '\a';
                else if (digit == 'f') digit = '\f';
                else if (digit == 'v') digit = '\v';
                else if (digit != '\\' && digit != '"' && digit != '\'' && digit != '?') { cc_fail(); break; }
            }
            if (count == 63 || digit == 0) { cc_fail(); break; }
            cc_text[count++] = digit;
        }
        if (*cc_source != ch) cc_fail(); else cc_source++;
        cc_text[count] = 0;
        if (ch == '\'') { if (count != 1) cc_fail(); cc_value = cc_text[0]; cc_token = TOKEN_NUMBER; }
        else cc_token = TOKEN_STRING;
        return;
    }
    cc_text[0] = ch; cc_text[1] = 0;
    cc_source++; cc_token = ch;
    if (*cc_source == ch) {
        if (ch == '&') cc_token = TOKEN_AND;
        else if (ch == '|') cc_token = TOKEN_OR;
        else if (ch == '+') cc_token = TOKEN_INC;
        else if (ch == '-') cc_token = TOKEN_DEC;
        else if (ch == '<') cc_token = TOKEN_SHL;
        else if (ch == '>') cc_token = TOKEN_SHR;
        else if (ch != '=') return;
        if (ch != '=') {
            cc_source++;
#ifdef BOBC_WIDE
            if ((cc_token == TOKEN_SHL || cc_token == TOKEN_SHR) && *cc_source == '=') {
                cc_token = cc_token == TOKEN_SHL ? TOKEN_SHL_ASSIGN : TOKEN_SHR_ASSIGN;
                cc_source++;
            }
#endif
            return;
        }
    }
    if (*cc_source == '=') {
        if (ch == '=') cc_token = TOKEN_EQ;
        else if (ch == '!') cc_token = TOKEN_NE;
        else if (ch == '<') cc_token = TOKEN_LE;
        else if (ch == '>') cc_token = TOKEN_GE;
        else if (ch == '+') cc_token = TOKEN_ADD_ASSIGN;
        else if (ch == '-') cc_token = TOKEN_SUB_ASSIGN;
        else if (ch == '*') cc_token = TOKEN_MUL_ASSIGN;
        else if (ch == '/') cc_token = TOKEN_DIV_ASSIGN;
        else if (ch == '%') cc_token = TOKEN_MOD_ASSIGN;
        else if (ch == '&') cc_token = TOKEN_AND_ASSIGN;
        else if (ch == '|') cc_token = TOKEN_OR_ASSIGN;
        else if (ch == '^') cc_token = TOKEN_XOR_ASSIGN;
        else return;
        cc_source++;
    }
}
int cc_is(char *name) { return cc_token == TOKEN_ID && strcmp(cc_text, name) == 0; }
void cc_expect(int token) { if (cc_token != token) { cc_fail(); return; } cc_next(); }
void cg_emit(int word) {
    if(cc_error)return;
#ifdef BOBC_WIDE
    if (cc_position >= FILE_WORDS - 1) { cc_fail(); return; }
#else
    if (cc_position >= 511) { cc_fail(); return; }
#endif
    cc_code[cc_position++] = word;
}
void cg_literal(int reg, int value) { cg_emit(0x4001 + reg * 512); cg_emit(0xae01); cg_emit(value); }
void cg_push(void) { cg_emit(0x1dff); cg_emit(0x9180); }
void cg_pop(void) { cg_emit(0x6380); cg_emit(0x1d81); }
int cg_jump(int target) {
    int patch; patch = cc_position + 2;
    cg_literal(3, CC_PROGRAM_BASE + target); cg_emit(0xb600); return patch;
}
void cg_patch(int patch) { if (!cc_error) cc_code[patch] = CC_PROGRAM_BASE + cc_position; }
void cg_call(int address, int args) {
    cg_literal(3, address); cg_emit(CALL_R3); cg_emit(0x1d80 + args);
}
int cg_if_zero(void) { cg_emit(0x1080); cg_emit(0xaa04); return cg_jump(0); }
int cc_variable(char *name) {
    int i;
#ifdef BOBC_WIDE
    for (i = cc_variables - 1; i >= 0; i--) if (strcmp(cc_names + i * NAME_WORDS, name) == 0) return i;
#else
    for (i = 0; i < cc_variables; i++) if (strcmp(cc_names + i * NAME_WORDS, name) == 0) return i;
#endif
    return -1;
}
#ifdef BOBC_WIDE
int cc_variable_current_scope(char *name) {
    int i;
    for (i = cc_scope_floor[cc_scope_depth]; i < cc_variables; i++)
        if (strcmp(cc_names + i * NAME_WORDS, name) == 0) return i;
    return -1;
}
#endif
#ifdef BOBC_WIDE
int cc_function_find(char *name) {
    int i;
    for (i = 0; i < cc_function_count; i++) if (strcmp(cc_function_names[i], name) == 0) return i;
    if (cc_function_count == 16 || strlen(name) >= 24) { cc_fail(); return -1; }
    i = cc_function_count++; memcpy(cc_function_names[i], name, strlen(name) + 1);
    cc_function_addresses[i] = -1; cc_function_parameters[i] = -1;
    cc_function_call_parameters[i] = -1;
    cc_function_parameter_array_mask[i] = 0;
    cc_function_parameter_array_known[i] = 0;
    cc_function_call_array_mask[i] = 0;
    cc_function_call_array_known[i] = 0;
    cc_function_call_array_conflicts[i] = 0;
    cc_function_defined[i] = 0; cc_function_calls[i] = -1;
    return i;
}
int cc_function_lookup(char *name) {
    for (int i = 0; i < cc_function_count; i++)
        if (strcmp(cc_function_names[i], name) == 0) return i;
    return -1;
}
int cc_parameter_full_mask(int count) { return count ? (1 << count) - 1 : 0; }
int cc_function_array_signature_matches(int function, int count, int array_mask) {
    int all; all = cc_parameter_full_mask(count);
    if ((cc_function_parameter_array_known[function] & all) == all &&
        ((cc_function_parameter_array_mask[function] ^ array_mask) & all)) return 0;
    if (cc_function_call_array_conflicts[function] & all) return 0;
    if (((cc_function_call_array_mask[function] ^ array_mask) & cc_function_call_array_known[function] & all)) return 0;
    return 1;
}
int cc_function_record_array_argument(int function, int parameter, int is_array) {
    int bit; int is_known; int was_array;
    bit = 1 << parameter;
    if ((cc_function_parameter_array_known[function] & bit) &&
        !!(cc_function_parameter_array_mask[function] & bit) != is_array) return 0;
    is_known = (cc_function_call_array_known[function] & bit) != 0;
    was_array = (cc_function_call_array_mask[function] & bit) != 0;
    if (is_known && was_array != is_array) cc_function_call_array_conflicts[function] |= bit;
    else {
        cc_function_call_array_known[function] |= bit;
        if (is_array) cc_function_call_array_mask[function] |= bit;
    }
    return 1;
}
int cc_builtin_function(char *name) {
    int function;
    int address;
    if (strcmp(name, "print") == 0) address = bob_address(print);
    else if (strcmp(name, "println") == 0) address = bob_address(println);
    else if (strcmp(name, "print_dec") == 0) address = bob_address(print_dec);
    else if (strcmp(name, "print_hex") == 0) address = bob_address(print_hex);
    else return -1;
    function = cc_function_find(name);
    if (function < 0) return -1;
    cc_function_addresses[function] = address;
    cc_function_parameters[function] = 1;
    cc_function_parameter_array_known[function] = 1;
    cc_function_defined[function] = 2;
    return function;
}
void cc_function_value(int function) {
    int patch;
    cg_literal(0, cc_function_defined[function] == 2 ? cc_function_addresses[function] :
               cc_function_defined[function] ? CC_PROGRAM_BASE + cc_function_addresses[function] : 0);
    if (!cc_function_defined[function]) {
        patch = cc_position - 1;
        cc_code[patch] = cc_function_calls[function];
        cc_function_calls[function] = patch;
    }
}
#endif
void cc_load_variable(int variable) {
#ifdef BOBC_WIDE
    cg_emit(0x6140 | (cc_variable_offsets[variable] & 63));
#else
    cg_literal(0, CC_VARIABLE_BASE + variable); cg_emit(0x6000);
#endif
}
void cc_store_variable(int variable) {
#ifdef BOBC_WIDE
    cg_emit(0x9140 | (cc_variable_offsets[variable] & 63));
#else
    cg_literal(1, CC_VARIABLE_BASE + variable); cg_emit(0x9040);
#endif
}
#ifdef BOBC_WIDE
void cc_array_index_address(int variable) {
    cg_push();
    if (cc_array_flags[variable] == 2) {
        cc_load_variable(variable);
        cg_literal(1, 0);
        cg_emit(0x1402);
    } else {
        cg_literal(1, 0);
        cg_emit(0x1452);
        cg_emit(0x15d0 | (cc_variable_offsets[variable] & 0x7f));
    }
    cg_pop();
    cg_emit(0x1422);
}
#endif
int cc_less(int a, int b) { return a < b; }
int cc_greater(int a, int b) { return a > b; }
int cc_less_equal(int a, int b) { return a <= b; }
int cc_greater_equal(int a, int b) { return a >= b; }
int cc_equal(int a, int b) { return a == b; }
int cc_not_equal(int a, int b) { return a != b; }
int cc_multiply(int a, int b) { return a * b; }
int cc_divide(int a, int b) { return a / b; }
int cc_modulo(int a, int b) { return a % b; }
int cc_negate(int a) { return -a; }
int cc_not(int a) { return !a; }
int cc_bit_or(int a, int b) { return a | b; }
int cc_bit_xor(int a, int b) { return a ^ b; }
int cc_shift_left(int a, int b) { return a << b; }
int cc_shift_right(int a, int b) { return a >> b; }
int cc_truth(int a) { return !!a; }
int cc_loop_depth;
int cc_expression_is_array;
#ifdef BOBC_WIDE
int cc_variable_offsets[16];
int cc_array_flags[16];
int cc_function_pointer_flags[16];
int cc_function_pointer_parameters[16];
int cc_function_count;
int cc_function_addresses[16];
int cc_function_parameters[16];
int cc_function_parameter_array_mask[16];
int cc_function_parameter_array_known[16];
int cc_function_call_parameters[16];
int cc_function_call_array_mask[16];
int cc_function_call_array_known[16];
int cc_function_call_array_conflicts[16];
int cc_function_defined[16];
int cc_function_calls[16];
int cc_return_chain;
int cc_local_count;
char cc_function_names[16][24];
#endif
void cc_function_pointer_rhs(int variable) {
#ifdef BOBC_WIDE
    int source_variable; int source_function; char source_name[64];
    if (cc_token == TOKEN_NUMBER && cc_value == 0) { cg_literal(0, 0); cc_next(); return; }
    if (cc_token != TOKEN_ID || strlen(cc_text) >= sizeof(source_name)) { cc_fail(); return; }
    memcpy(source_name, cc_text, strlen(cc_text) + 1);
    source_variable = cc_variable(source_name);
    if (source_variable >= 0) {
        if (!cc_function_pointer_flags[source_variable] ||
            cc_function_pointer_parameters[source_variable] != cc_function_pointer_parameters[variable]) { cc_fail(); return; }
        cc_load_variable(source_variable); cc_next(); return;
    }
    source_function = cc_function_lookup(source_name);
    if (source_function < 0 || cc_function_parameters[source_function] < 0 ||
        cc_function_parameters[source_function] != cc_function_pointer_parameters[variable]) { cc_fail(); return; }
    cc_function_value(source_function); cc_next();
    if (cc_token == '(') cc_fail();
#else
    (void)variable;
#endif
}
#define cc_break_chain ((int *)0xe3c0)
#define cc_continue_chain ((int *)0xe3d0)

int cc_precedence(int op) {
    if (op == TOKEN_OR) return 1;
    if (op == TOKEN_AND) return 2;
    if (op == '|') return 3;
    if (op == '^') return 4;
    if (op == '&') return 5;
    if (op == TOKEN_EQ || op == TOKEN_NE) return 6;
    if (op == '<' || op == '>' || op == TOKEN_LE || op == TOKEN_GE) return 7;
    if (op == TOKEN_SHL || op == TOKEN_SHR) return 8;
    if (op == '+' || op == '-') return 9;
    if (op == '*' || op == '/' || op == '%') return 10;
    return 0;
}
void cc_operation(int op) {
    int address;
    if (op == '+' || op == '-') {
        if (op == '-') { cg_emit(0x3100); cg_emit(0x1181); }
        cg_emit(0x1010); return;
    }
    if (op == '&') { cg_emit(0x2010); return; }
    cg_push(); cg_emit(0x1dff); cg_emit(0x9380);
    if (op == '*') address = bob_address(cc_multiply);
    else if (op == '/') address = bob_address(cc_divide);
    else if (op == '%') address = bob_address(cc_modulo);
    else if (op == '|') address = bob_address(cc_bit_or);
    else if (op == '^') address = bob_address(cc_bit_xor);
    else if (op == TOKEN_SHL) address = bob_address(cc_shift_left);
    else if (op == TOKEN_SHR) address = bob_address(cc_shift_right);
    else if (op == TOKEN_EQ) address = bob_address(cc_equal);
    else if (op == TOKEN_NE) address = bob_address(cc_not_equal);
    else if (op == '<') address = bob_address(cc_less);
    else if (op == '>') address = bob_address(cc_greater);
    else if (op == TOKEN_LE) address = bob_address(cc_less_equal);
    else address = bob_address(cc_greater_equal);
    cg_call(address, 2);
}

void cc_expression(void);
void cc_primary(void) {
    int variable; int patch; int start; int count; int op; int indexed_array;
#ifdef BOBC_WIDE
    int indirect_function; int argument_count;
#endif
    if (cc_error) return;
    cc_depth++;
    if (cc_depth > 32) { cc_fail(); return; }
    cc_expression_is_array = 0;
    if (cc_token == TOKEN_NUMBER) { cg_literal(0, cc_value); cc_next(); }
    else if (cc_token == TOKEN_STRING) {
        /* Inline string data is skipped by an absolute jump. */
        patch = cg_jump(0); start = cc_position;
        do {
            count = strlen(cc_text);
            for (variable = 0; variable < count; variable++) cg_emit(cc_text[variable]);
            cc_next();
        } while (cc_token == TOKEN_STRING);
        cg_emit(0); cg_patch(patch); cg_literal(0, CC_PROGRAM_BASE + start);
    } else if (cc_token == '(') { cc_next(); cc_expression(); cc_expect(')'); }
    else if (cc_token == '-' || cc_token == '!' || cc_token == '+' || cc_token == '~') {
        op = cc_token; cc_next(); cc_primary(); cg_push();
        if (op == '-') cg_call(bob_address(cc_negate), 1);
        else if (op == '!') cg_call(bob_address(cc_not), 1);
        else { cg_emit(0x6180); cg_emit(0x1d81); if (op == '~') cg_emit(0x3100); }
        cc_expression_is_array = 0;
    } else if (cc_token == TOKEN_INC || cc_token == TOKEN_DEC) {
        op = cc_token; cc_next(); variable = cc_variable(cc_text);
        if (cc_token != TOKEN_ID || variable < 0) { cc_fail(); return; }
#ifdef BOBC_WIDE
        if (cc_array_flags[variable]) {
            cc_next();
            if (cc_token != '[') { cc_fail(); return; }
            cc_next(); cc_expression(); cc_expect(']');
            cc_array_index_address(variable); cg_emit(0x6080);
            if (op == TOKEN_INC) cg_emit(0x1181); else cg_emit(0x11ff);
            cg_emit(0x9080); cc_depth--; return;
        }
#endif
        cc_next(); cc_load_variable(variable);
        if (op == TOKEN_INC) cg_emit(0x1181); else cg_emit(0x11ff);
        cc_store_variable(variable);
    } else if (cc_token == TOKEN_ID) {
        variable = cc_variable(cc_text);
        if (variable >= 0) {
            indexed_array = 0;
#ifdef BOBC_WIDE
            indirect_function = 0;
#endif
            cc_next();
#ifdef BOBC_WIDE
            if (cc_array_flags[variable]) {
                if (cc_token == '[') {
                    indexed_array = 1;
                    cc_next(); cc_expression(); cc_expect(']');
                    cc_array_index_address(variable); cg_emit(0x6080);
                    cc_expression_is_array = 0;
                } else {
                    if (cc_token == TOKEN_INC || cc_token == TOKEN_DEC) cc_fail();
                    if (cc_array_flags[variable] == 2) cc_load_variable(variable);
                    else {
                        cg_literal(0, 0); cg_emit(0x1050);
                        cg_emit(0x11d0 | (cc_variable_offsets[variable] & 0x7f));
                    }
                    cc_expression_is_array = 1;
                }
            } else { cc_load_variable(variable); cc_expression_is_array = 0; }
#else
            cc_load_variable(variable);
#endif
#ifdef BOBC_WIDE
            if (!cc_error && cc_function_pointer_flags[variable] && cc_token == '(') {
                indirect_function = 1; cg_push(); cc_next(); argument_count = 0;
                if (cc_token != ')') {
                    do {
                        if (argument_count == 16) { cc_fail(); return; }
                        cc_expression(); cg_push(); argument_count++;
                        if (cc_token != ',') break;
                        cc_next();
                    } while (!cc_error);
                }
                cc_expect(')');
                if (argument_count != cc_function_pointer_parameters[variable]) cc_fail();
                cg_emit(0x6780 | (argument_count + 1));
                cg_emit(CALL_R3); cg_emit(0x1d80 + argument_count + 1);
                cc_expression_is_array = 0;
            }
#endif
            if (!cc_error && indexed_array && (cc_token == TOKEN_INC || cc_token == TOKEN_DEC)) {
                op = cc_token; cc_next(); cg_push();
                if (op == TOKEN_INC) cg_emit(0x1181); else cg_emit(0x11ff);
                cg_emit(0x9080); cg_pop(); cg_emit(0x5020); cg_emit(0x1002);
            }
#ifdef BOBC_WIDE
            if (!cc_error && cc_function_pointer_flags[variable] &&
                (cc_token == TOKEN_INC || cc_token == TOKEN_DEC)) cc_fail();
#endif
            if (!cc_error && !indexed_array
#ifdef BOBC_WIDE
                && !indirect_function && !cc_function_pointer_flags[variable]
#endif
                && (cc_token == TOKEN_INC || cc_token == TOKEN_DEC)) {
                op = cc_token; cc_next(); cg_push();
                if (op == TOKEN_INC) cg_emit(0x1181); else cg_emit(0x11ff);
                cc_store_variable(variable);
                cg_emit(0x6180); cg_emit(0x1d81);
            }
        }
#ifdef BOBC_WIDE
        else {
            char name[64]; int function; int argument_count; int call_patch;
            memcpy(name, cc_text, strlen(cc_text) + 1);
            function = cc_function_lookup(name);
            if (function < 0) function = cc_builtin_function(name);
            if (function < 0) function = cc_function_find(name);
            if (function < 0) return;
            cc_next();
            if (cc_token != '(') {
                if (cc_function_parameters[function] < 0) { cc_fail(); return; }
                call_patch = cc_position + 2;
                cg_literal(0, cc_function_defined[function] ? CC_PROGRAM_BASE + cc_function_addresses[function] : 0);
                if (!cc_function_defined[function]) {
                    cc_code[call_patch] = cc_function_calls[function]; cc_function_calls[function] = call_patch;
                }
            } else {
                cc_next(); argument_count = 0;
                if (cc_token != ')') {
                    do {
                        if (argument_count == 16) { cc_fail(); return; }
                        cc_expression();
                        if (!cc_function_record_array_argument(function, argument_count, cc_expression_is_array)) cc_fail();
                        cg_push(); argument_count++;
                        if (cc_token != ',') break;
                        cc_next();
                    } while (!cc_error);
                }
                cc_expect(')');
                if (cc_function_parameters[function] == -2) {
                    if (cc_function_defined[function]) {
                        if (cc_function_parameters[function] != argument_count) cc_fail();
                    } else if (cc_function_call_parameters[function] < 0)
                        cc_function_call_parameters[function] = argument_count;
                    else if (cc_function_call_parameters[function] != argument_count) cc_fail();
                } else if (cc_function_parameters[function] < 0) cc_function_parameters[function] = argument_count;
                else if (cc_function_parameters[function] != argument_count) cc_fail();
                call_patch = cc_position + 2;
                cg_literal(3, cc_function_defined[function] == 2 ? cc_function_addresses[function] :
                           cc_function_defined[function] ? CC_PROGRAM_BASE + cc_function_addresses[function] : 0);
                cg_emit(CALL_R3); cg_emit(0x1d80 + argument_count);
                if (!cc_function_defined[function]) {
                    cc_code[call_patch] = cc_function_calls[function]; cc_function_calls[function] = call_patch;
                }
                cc_expression_is_array = 0;
            }
        }
#else
        else cc_fail();
#endif
    } else cc_fail();
    cc_depth--;
}
void cc_binary(int minimum) {
    int op; int level; int shortcut; int end;
    cc_primary(); level = cc_precedence(cc_token);
    while (!cc_error && level >= minimum && level) {
        op = cc_token; cc_next();
        cc_expression_is_array = 0;
        if (op == TOKEN_AND || op == TOKEN_OR) {
            cg_emit(0x1080);
            if (op == TOKEN_AND) cg_emit(0xaa04); else cg_emit(0xa404);
            shortcut = cg_jump(0); cc_binary(level + 1);
            cg_push(); cg_call(bob_address(cc_truth), 1); end = cg_jump(0);
            cg_patch(shortcut); cg_literal(0, op == TOKEN_OR); cg_patch(end);
        } else {
            cg_push(); cc_binary(level + 1); cg_pop(); cc_operation(op);
        }
        level = cc_precedence(cc_token);
    }
}
#ifdef BOBC_WIDE
void cc_expression(void) {
    int branch; int end; int variable; int op; int saved_value; int compound;
    char name[64]; char *saved_source;
    if (!cc_error && cc_token == TOKEN_ID) {
        memcpy(name, cc_text, strlen(cc_text) + 1); variable = cc_variable(name);
        saved_source = cc_source; saved_value = cc_value; cc_next(); op = cc_token;
        if (!cc_error && (op == '=' || (op >= TOKEN_ADD_ASSIGN && op <= TOKEN_XOR_ASSIGN) ||
                          op == TOKEN_SHL_ASSIGN || op == TOKEN_SHR_ASSIGN)) {
            if (variable < 0) { cc_fail(); return; }
            cc_depth++;
            if (cc_depth > 32) { cc_fail(); cc_depth--; return; }
            if (cc_function_pointer_flags[variable]) {
                if (op != '=') { cc_fail(); cc_depth--; return; }
                cc_next(); cc_function_pointer_rhs(variable);
                if (!cc_error) cc_store_variable(variable);
                cc_expression_is_array = 0;
                cc_depth--; return;
            }
            compound = op != '=';
            if (compound) { cc_load_variable(variable); cg_push(); }
            cc_next(); cc_expression();
            if (compound) {
                cg_pop();
                if (op == TOKEN_ADD_ASSIGN) op = '+';
                else if (op == TOKEN_SUB_ASSIGN) op = '-';
                else if (op == TOKEN_MUL_ASSIGN) op = '*';
                else if (op == TOKEN_DIV_ASSIGN) op = '/';
                else if (op == TOKEN_MOD_ASSIGN) op = '%';
                else if (op == TOKEN_AND_ASSIGN) op = '&';
                else if (op == TOKEN_OR_ASSIGN) op = '|';
                else if (op == TOKEN_XOR_ASSIGN) op = '^';
                else if (op == TOKEN_SHL_ASSIGN) op = TOKEN_SHL;
                else op = TOKEN_SHR;
                cc_operation(op);
            }
            cc_store_variable(variable);
            cc_expression_is_array = 0;
            cc_depth--; return;
        }
        if (cc_error) return;
        cc_source = saved_source; cc_token = TOKEN_ID; cc_value = saved_value;
        memcpy(cc_text, name, strlen(name) + 1);
    }
    cc_binary(1);
    if (!cc_error && cc_token == '?') {
        cc_depth++;
        if (cc_depth > 32) { cc_fail(); cc_depth--; return; }
        cc_next(); branch = cg_if_zero();
        cc_expression(); cc_expect(':');
        end = cg_jump(0); cg_patch(branch);
        cc_expression(); cg_patch(end);
        cc_expression_is_array = 0;
        cc_depth--;
    }
}
#else
void cc_expression(void) { cc_binary(1); }
#endif
void cc_patch_chain(int chain, int target) {
    int next;
    while (chain >= 0 && !cc_error) {
        next = cc_code[chain]; cc_code[chain] = CC_PROGRAM_BASE + target; chain = next;
    }
}
void cc_simple(void) {
    char name[24]; int variable; int address; int op;
#ifdef BOBC_WIDE
    char *saved_source;
#endif
    if (cc_token == TOKEN_INC || cc_token == TOKEN_DEC) { cc_primary(); return; }
    if (cc_token != TOKEN_ID || strlen(cc_text) >= NAME_WORDS) { cc_fail(); return; }
    memcpy(name, cc_text, strlen(cc_text) + 1);
#ifdef BOBC_WIDE
    saved_source = cc_source;
#endif
    cc_next(); op = cc_token;
#ifdef BOBC_WIDE
    if (op == '[') {
        variable = cc_variable(name);
        if (variable < 0 || !cc_array_flags[variable]) { cc_fail(); return; }
        cc_next(); cc_expression(); cc_expect(']');
        cc_array_index_address(variable);
        op = cc_token;
        if (op == TOKEN_INC || op == TOKEN_DEC) {
            cg_emit(0x1dff); cg_emit(0x9580); cg_emit(0x6080);
            if (op == TOKEN_INC) cg_emit(0x1181); else cg_emit(0x11ff);
            cg_pop(); cg_emit(0x9040); cc_next();
        } else if (op == '=') {
            cg_emit(0x1dff); cg_emit(0x9580); cc_next(); cc_expression();
            cg_pop(); cg_emit(0x9040);
        } else if (op >= TOKEN_ADD_ASSIGN && op <= TOKEN_SHR_ASSIGN) {
            cg_emit(0x1dff); cg_emit(0x9580); cg_emit(0x6080); cg_push();
            cc_next(); cc_expression(); cg_pop();
            if (op == TOKEN_ADD_ASSIGN) op = '+';
            else if (op == TOKEN_SUB_ASSIGN) op = '-';
            else if (op == TOKEN_MUL_ASSIGN) op = '*';
            else if (op == TOKEN_DIV_ASSIGN) op = '/';
            else if (op == TOKEN_MOD_ASSIGN) op = '%';
            else if (op == TOKEN_AND_ASSIGN) op = '&';
            else if (op == TOKEN_OR_ASSIGN) op = '|';
            else if (op == TOKEN_XOR_ASSIGN) op = '^';
            else if (op == TOKEN_SHL_ASSIGN) op = TOKEN_SHL;
            else op = TOKEN_SHR;
            cc_operation(op); cg_pop(); cg_emit(0x9040);
        } else cc_fail();
    }
    else
#endif
    if (op == TOKEN_INC || op == TOKEN_DEC) {
        variable = cc_variable(name); if (variable < 0) { cc_fail(); return; }
#ifdef BOBC_WIDE
        if (cc_array_flags[variable]) { cc_fail(); return; }
#endif
        cc_next(); cc_load_variable(variable);
        if (op == TOKEN_INC) cg_emit(0x1181); else cg_emit(0x11ff);
        cc_store_variable(variable);
    } else if (op == '=' || (op >= TOKEN_ADD_ASSIGN && op <= TOKEN_XOR_ASSIGN)
#ifdef BOBC_WIDE
               || op == TOKEN_SHL_ASSIGN || op == TOKEN_SHR_ASSIGN
#endif
               ) {
        variable = cc_variable(name); if (variable < 0) { cc_fail(); return; }
#ifdef BOBC_WIDE
        if (cc_array_flags[variable]) { cc_fail(); return; }
        if (cc_function_pointer_flags[variable]) {
            if (op != '=') { cc_fail(); return; }
            cc_next(); cc_function_pointer_rhs(variable);
            if (!cc_error) cc_store_variable(variable);
            return;
        }
#endif
        if (op != '=') { cc_load_variable(variable); cg_push(); }
        cc_next(); cc_expression();
        if (op != '=') {
            cg_pop();
            if (op == TOKEN_ADD_ASSIGN) op = '+';
            else if (op == TOKEN_SUB_ASSIGN) op = '-';
            else if (op == TOKEN_MUL_ASSIGN) op = '*';
            else if (op == TOKEN_DIV_ASSIGN) op = '/';
            else if (op == TOKEN_MOD_ASSIGN) op = '%';
            else if (op == TOKEN_AND_ASSIGN) op = '&';
            else if (op == TOKEN_OR_ASSIGN) op = '|';
#ifdef BOBC_WIDE
            else if (op == TOKEN_XOR_ASSIGN) op = '^';
            else if (op == TOKEN_SHL_ASSIGN) op = TOKEN_SHL;
            else if (op == TOKEN_SHR_ASSIGN) op = TOKEN_SHR;
#else
            else op = '^';
#endif
            cc_operation(op);
        }
        cc_store_variable(variable);
    } else if (op == '(') {
        address = 0;
        if (strcmp(name, "print") == 0) address = bob_address(print);
        else if (strcmp(name, "println") == 0) address = bob_address(println);
        else if (strcmp(name, "print_dec") == 0) address = bob_address(print_dec);
        else if (strcmp(name, "print_hex") == 0) address = bob_address(print_hex);
        else if (strcmp(name, "bob_putc") == 0) address = 1;
        if (!address) {
#ifdef BOBC_WIDE
            cc_source = saved_source; cc_token = TOKEN_ID; memcpy(cc_text, name, strlen(name) + 1);
            cc_expression(); return;
#else
            cc_fail(); return;
#endif
        }
        cc_next(); cc_expression(); cc_expect(')');
        if (address == 1) cg_emit(0xf100);
        else { cg_push(); cg_call(address, 1); }
    } else cc_fail();
}
void cc_statement(void) {
    int variable; int start; int patch; int end; int step; int body;
#ifdef BOBC_WIDE
    int saved_variables;
    int opened_scope;
    int function_pointer;
    int function_pointer_parameters;
#endif
    char declaration_name[NAME_WORDS];
    if (cc_error) return;
    cc_depth++;
    if (cc_depth > 32) { cc_fail(); return; }
    if (cc_token == '{') {
#ifdef BOBC_WIDE
        saved_variables = cc_variables;
        opened_scope = 1;
        if (cc_scope_depth == 32) { cc_fail(); cc_depth--; return; }
        cc_scope_depth++;
        cc_scope_floor[cc_scope_depth] = cc_scope_depth == 1 ? 0 : cc_variables;
#endif
        cc_next();
        while (!cc_error && cc_token && cc_token != '}') cc_statement();
        cc_expect('}');
#ifdef BOBC_WIDE
        cc_variables = saved_variables;
        if (opened_scope) cc_scope_depth--;
#endif
    } else if (cc_is("int")) {
        cc_next();
        do {
#ifdef BOBC_WIDE
            function_pointer = 0; function_pointer_parameters = 0;
            if (cc_token == '(') {
                function_pointer = 1; cc_next(); cc_expect('*');
                if (cc_token != TOKEN_ID || strlen(cc_text) >= NAME_WORDS) { cc_fail(); return; }
                memcpy(declaration_name, cc_text, strlen(cc_text) + 1); cc_next(); cc_expect(')'); cc_expect('(');
                if (cc_is("void")) { cc_next(); cc_expect(')'); }
                else if (cc_token == ')') cc_next();
                else {
                    while (!cc_error) {
                        if (!cc_is("int") || function_pointer_parameters == 16) { cc_fail(); break; }
                        cc_next(); function_pointer_parameters++;
                        if (cc_token == TOKEN_ID) cc_next();
                        if (cc_token != ',') break;
                        cc_next();
                    }
                    cc_expect(')');
                }
            } else
#else
            if (cc_token != TOKEN_ID || strlen(cc_text) >= NAME_WORDS) { cc_fail(); return; }
            memcpy(declaration_name, cc_text, strlen(cc_text) + 1); cc_next();
#endif
#ifdef BOBC_WIDE
            if (!function_pointer) {
                if (cc_token != TOKEN_ID || strlen(cc_text) >= NAME_WORDS) { cc_fail(); return; }
                memcpy(declaration_name, cc_text, strlen(cc_text) + 1); cc_next();
            }
#endif
            if (cc_variables == 16
#ifdef BOBC_WIDE
                || cc_variable_current_scope(declaration_name) >= 0
#else
                || cc_variable(declaration_name) >= 0
#endif
                ) { cc_fail(); return; }
            variable = cc_variables++;
            memcpy(cc_names + variable * NAME_WORDS, declaration_name, strlen(declaration_name) + 1);
#ifdef BOBC_WIDE
            cc_array_flags[variable] = 0;
            cc_function_pointer_flags[variable] = function_pointer;
            cc_function_pointer_parameters[variable] = function_pointer_parameters;
            if (cc_token == '[') {
                int array_size; int base_offset; int initialized;
                cc_next();
                if (cc_token != TOKEN_NUMBER || cc_value < 1 || cc_value > 32 - cc_local_count) { cc_fail(); return; }
                array_size = cc_value; cc_next(); cc_expect(']');
                base_offset = -(cc_local_count + array_size);
                cc_local_count += array_size;
                cc_variable_offsets[variable] = base_offset;
                cc_array_flags[variable] = 1;
                for (int i = 0; i < array_size; i++) {
                    cg_literal(0, 0);
                    cg_emit(0x9140 | ((base_offset + i) & 63));
                }
                if (cc_token == '=') {
                    cc_next(); cc_expect('{'); initialized = 0;
                    while (!cc_error && cc_token != '}') {
                        if (initialized == array_size) { cc_fail(); break; }
                        cc_expression();
                        cg_emit(0x9140 | ((base_offset + initialized) & 63));
                        initialized++;
                        if (cc_token != ',') break;
                        cc_next();
                        if (cc_token == '}') break;
                    }
                    cc_expect('}');
                }
            } else {
                if (cc_local_count == 32) { cc_fail(); return; }
                cc_variable_offsets[variable] = -(++cc_local_count);
                if (cc_token == '=') {
                    if (function_pointer) {
                        cc_next(); cc_function_pointer_rhs(variable);
                    } else { cc_next(); cc_expression(); }
                }
                else cg_literal(0, 0);
                cc_store_variable(variable);
            }
#else
            if (cc_token == '=') { cc_next(); cc_expression(); }
            else cg_literal(0, 0);
            cc_store_variable(variable);
#endif
            if (cc_token != ',') break;
            cc_next();
        } while (!cc_error);
        cc_expect(';');
    } else if (cc_is("return")) {
        cc_next();
        if (cc_token == ';') cg_literal(0, 0); else cc_expression();
#ifdef BOBC_WIDE
        patch = cg_jump(0); cc_code[patch] = cc_return_chain; cc_return_chain = patch;
#else
        cg_emit(0x1cd0); cg_emit(0x6b80); cg_emit(0x1d81); cg_emit(0x6f80); cg_emit(0x1d81); cg_emit(0xe000);
#endif
        cc_expect(';');
    } else if (cc_is("break") || cc_is("continue")) {
        variable = cc_is("continue");
        if (!cc_loop_depth) { cc_fail(); return; }
        cc_next(); patch = cg_jump(0);
        if (variable) {
            cc_code[patch] = cc_continue_chain[cc_loop_depth - 1]; cc_continue_chain[cc_loop_depth - 1] = patch;
        } else {
            cc_code[patch] = cc_break_chain[cc_loop_depth - 1]; cc_break_chain[cc_loop_depth - 1] = patch;
        }
        cc_expect(';');
    }
#ifdef BOBC_WIDE
    else if (cc_is("do")) {
        if (cc_loop_depth == 16) { cc_fail(); return; }
        cc_next(); start = cc_position;
        cc_break_chain[cc_loop_depth] = -1; cc_continue_chain[cc_loop_depth] = -1; cc_loop_depth++;
        cc_statement(); cc_loop_depth--; step = cc_position;
        cc_patch_chain(cc_continue_chain[cc_loop_depth], step);
        if (!cc_is("while")) cc_fail(); else cc_next();
        cc_expect('('); cc_expression(); cc_expect(')'); cc_expect(';');
        patch = cg_if_zero(); cg_jump(start); cg_patch(patch);
        cc_patch_chain(cc_break_chain[cc_loop_depth], cc_position);
    }
#endif
    else if (cc_is("for")) {
        if (cc_loop_depth == 16) { cc_fail(); return; }
#ifdef BOBC_WIDE
        saved_variables = cc_variables;
        if (cc_scope_depth == 32) { cc_fail(); cc_depth--; return; }
        cc_scope_depth++;
        cc_scope_floor[cc_scope_depth] = cc_variables;
#endif
        cc_next(); cc_expect('(');
        if (cc_token == ';') cc_next(); else cc_statement();
        start = cc_position;
        if (cc_token == ';') cg_literal(0, 1); else cc_expression();
        cc_expect(';'); patch = cg_if_zero(); body = cg_jump(0); step = cc_position;
        if (cc_token != ')') cc_simple();
        cc_expect(')'); cg_jump(start); cg_patch(body);
        cc_break_chain[cc_loop_depth] = -1; cc_continue_chain[cc_loop_depth] = -1; cc_loop_depth++;
        cc_statement(); cc_loop_depth--; cg_jump(step); cg_patch(patch);
        cc_patch_chain(cc_break_chain[cc_loop_depth], cc_position);
        cc_patch_chain(cc_continue_chain[cc_loop_depth], step);
#ifdef BOBC_WIDE
        cc_variables = saved_variables;
        cc_scope_depth--;
#endif
    } else if (cc_is("if") || cc_is("while")) {
        variable = cc_is("while"); cc_next(); cc_expect('(');
        start = cc_position; cc_expression(); cc_expect(')');
        patch = cg_if_zero();
        if (variable) {
            if (cc_loop_depth == 16) { cc_fail(); return; }
            cc_break_chain[cc_loop_depth] = -1; cc_continue_chain[cc_loop_depth] = -1; cc_loop_depth++;
        }
        cc_statement();
        if (variable) {
            cc_loop_depth--; cg_jump(start); cg_patch(patch);
            cc_patch_chain(cc_break_chain[cc_loop_depth], cc_position);
            cc_patch_chain(cc_continue_chain[cc_loop_depth], start);
        }
        else if (cc_is("else")) { end = cg_jump(0); cg_patch(patch); cc_next(); cc_statement(); cg_patch(end); }
        else cg_patch(patch);
    } else if (cc_token == TOKEN_ID || cc_token == TOKEN_INC || cc_token == TOKEN_DEC) {
        cc_simple(); cc_expect(';');
    } else if (cc_token == ';') cc_next();
    else cc_fail();
    cc_depth--;
}
#ifdef BOBC_WIDE
void cc_compile_functions(void) {
    int entry_patch; int main_address; int function; int parameter_count; int local_adjust; int epilogue;
    int has_unnamed; int unspecified_parameters; int declared_parameters; int parameter_array_mask; int is_array;
    char function_name[24];
    entry_patch = cg_jump(0); main_address = -1;
    while (!cc_error && cc_token) {
        if (!cc_is("int")) { cc_fail(); break; }
        cc_next();
        if (cc_token != TOKEN_ID || strlen(cc_text) >= sizeof(function_name)) { cc_fail(); break; }
        memcpy(function_name, cc_text, strlen(cc_text) + 1); cc_next(); cc_expect('(');
        cc_variables = 0; parameter_count = 0; memset(cc_names, 0, 384); cc_local_count = 0;
        has_unnamed = 0; unspecified_parameters = 0;
        parameter_array_mask = 0;
        memset(cc_array_flags, 0, sizeof(cc_array_flags));
        memset(cc_function_pointer_flags, 0, sizeof(cc_function_pointer_flags));
        if (cc_is("void")) { cc_next(); cc_expect(')'); }
        else if (cc_token == ')') { unspecified_parameters = 1; cc_next(); }
        else {
            while (!cc_error && cc_token != ')') {
                if (!cc_is("int")) { cc_fail(); break; }
                cc_next();
                if (parameter_count == 16) { cc_fail(); break; }
                is_array = 0;
                if (cc_token == TOKEN_ID) {
                    if (strlen(cc_text) >= NAME_WORDS || cc_variables == 16 || cc_variable(cc_text) >= 0) { cc_fail(); break; }
                    memcpy(cc_names + cc_variables * NAME_WORDS, cc_text, strlen(cc_text) + 1);
                    cc_variable_offsets[cc_variables] = 2; cc_variables++; parameter_count++;
                    cc_next();
                } else { has_unnamed = 1; parameter_count++; }
                if (cc_token == '[') {
                    is_array = 1; cc_next();
                    if (cc_token != ']') {
                        if (cc_token != TOKEN_NUMBER || cc_value < 1) { cc_fail(); break; }
                        cc_next();
                    }
                    cc_expect(']');
                    if (cc_token == '[') { cc_fail(); break; }
                }
                if (is_array) parameter_array_mask |= 1 << (parameter_count - 1);
                if (cc_token != ',') break;
                cc_next();
            }
            cc_expect(')');
        }
        function = cc_function_find(function_name);
        if (function < 0) break;
        if (cc_token == ';') {
            declared_parameters = unspecified_parameters ? -2 : parameter_count;
            if (declared_parameters >= 0 && cc_function_parameters[function] >= 0 &&
                cc_function_parameters[function] != declared_parameters) { cc_fail(); break; }
            if (declared_parameters >= 0) {
                if (!cc_function_array_signature_matches(function, parameter_count, parameter_array_mask)) { cc_fail(); break; }
                cc_function_parameter_array_mask[function] = parameter_array_mask;
                cc_function_parameter_array_known[function] = cc_parameter_full_mask(parameter_count);
            }
            if (declared_parameters >= 0 || cc_function_parameters[function] < 0)
                cc_function_parameters[function] = declared_parameters;
            cc_next(); continue;
        }
        if (cc_token != '{' || has_unnamed) { cc_fail(); break; }
        if (cc_function_defined[function] || (cc_function_parameters[function] >= 0 && cc_function_parameters[function] != parameter_count) ||
            (cc_function_call_parameters[function] >= 0 && cc_function_call_parameters[function] != parameter_count)) { cc_fail(); break; }
        if (!cc_function_array_signature_matches(function, parameter_count, parameter_array_mask)) { cc_fail(); break; }
        cc_function_parameters[function] = parameter_count; cc_function_defined[function] = 1;
        cc_function_parameter_array_mask[function] = parameter_array_mask;
        cc_function_parameter_array_known[function] = cc_parameter_full_mask(parameter_count);
        for (int i = 0; i < parameter_count; i++) {
            cc_array_flags[i] = (parameter_array_mask & (1 << i)) ? 2 : 0;
        }
        cc_function_addresses[function] = cc_position;
        if (strcmp(function_name, "main") == 0) {
            if (main_address >= 0 || parameter_count) { cc_fail(); break; }
            main_address = cc_position;
        }
        cc_patch_chain(cc_function_calls[function], cc_position);
        cc_function_calls[function] = -1;
        for (int i = 0; i < parameter_count; i++) cc_variable_offsets[i] = 2 + parameter_count - 1 - i;
        cc_local_count = 0; cc_return_chain = -1; cc_loop_depth = 0; cc_depth = 0;
        cc_scope_depth = 0; cc_scope_floor[0] = 0;
        /* Save r7 and the caller's frame pointer; local slots grow below r5. */
        cg_emit(0x1dff); cg_emit(0x9f80); cg_emit(0x1dff); cg_emit(0x9b80); cg_emit(0x1ae0);
        local_adjust = cc_position; cg_emit(0x1d80);
        cc_statement();
        if (cc_error) break;
        cg_literal(0, 0);
        epilogue = cc_position; cc_patch_chain(cc_return_chain, epilogue);
        cg_emit(0x1cd0); cg_emit(0x6b80); cg_emit(0x1d81); cg_emit(0x6f80); cg_emit(0x1d81); cg_emit(0xe000);
        cc_code[local_adjust] = 0x1d80 | ((-cc_local_count) & 0x7f);
    }
    for (int i = 0; i < cc_function_count; i++) if (cc_function_calls[i] >= 0) cc_fail();
    if (main_address < 0) cc_fail();
    if (!cc_error) cc_code[entry_patch] = CC_PROGRAM_BASE + main_address;
}
#endif
int compile_command(char *source_name, char *output_name) {
    int slot; int result; int i; int line; int column;
    slot = file_find(source_name);
    if (slot < 0 || file_kinds[slot]) { println("C source file not found."); return 0; }
    if (!*output_name || strlen(output_name) >= NAME_WORDS) { println("Usage: cc SOURCE OUTPUT"); return 0; }
    if (strcmp(source_name, output_name) == 0) { println("Output must differ from source; choose another name."); return 0; }
    cc_source = (char *)file_content(slot); cc_begin = cc_source;
    cc_error = 0; cc_position = 0; cc_variables = 0; cc_error_offset = 0; cc_depth = 0; cc_loop_depth = 0;
    memset(cc_names, 0, 384); cc_next();
#ifdef BOBC_WIDE
    cc_function_count = 0; memset(cc_function_names, 0, sizeof(cc_function_names));
    memset(cc_function_addresses, 0, sizeof(cc_function_addresses));
    memset(cc_function_parameters, 0xff, sizeof(cc_function_parameters));
    memset(cc_function_call_parameters, 0xff, sizeof(cc_function_call_parameters));
    memset(cc_function_defined, 0, sizeof(cc_function_defined));
    memset(cc_function_calls, 0xff, sizeof(cc_function_calls));
    cc_compile_functions();
#else
    if (!cc_is("int")) cc_fail(); else cc_next();
    if (!cc_is("main")) cc_fail(); else cc_next();
    cc_expect('('); if (cc_is("void")) cc_next(); cc_expect(')');
    if (cc_token != '{') cc_fail();
    /* Preserve the return address and caller frame; runProgram provides a stack. */
    cg_emit(0x1dff); cg_emit(0x9f80); cg_emit(0x1dff); cg_emit(0x9b80); cg_emit(0x1ae0);
    cc_statement();
    if (cc_token) cc_fail();
    cg_literal(0, 0);
    cg_emit(0x1cd0); cg_emit(0x6b80); cg_emit(0x1d81); cg_emit(0x6f80); cg_emit(0x1d81); cg_emit(0xe000);
#endif
    if (cc_error) {
        print("C compile error near character "); print_dec(cc_error_offset);
        print(". Unsupported syntax or compiler limit exceeded near token '"); print(cc_text); println("'.");
        line = 1; column = 1;
        for (i = 0; i < cc_error_offset; i++) {
            if (cc_begin[i] == '\n') { line++; column = 1; }
            else column++;
        }
        print(source_name); print(": line "); print_dec(line);
        print(", column "); print_dec(column); println(". Use edit to fix the source.");
        return 0;
    }
#ifdef BOBC_WIDE
    result = file_write(output_name, cc_code, cc_position, 2);
    if (result < 0) println("Cannot save compiled program; free RAM filesystem space and retry.");
#else
    result = file_write(output_name, cc_code, cc_position, 1);
    if (result < 0) println("Cannot save compiled program (max 511 words).");
#endif
    if (result >= 0) { print("Compiled "); print_dec(cc_position); println(" words."); }
    return result >= 0;
}
