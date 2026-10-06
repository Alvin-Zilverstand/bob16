/* Resident recursive-descent compiler. It emits bob16 machine words in RAM. */
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
#if BOBC_LEGACY
#define CALL_R3 0xcb00
#else
#define CALL_R3 0xc300
#endif
char *cc_source;
int cc_token;
int cc_value;
#define cc_text ((char *)0xe380)
int cc_error;
int cc_position;
/* Editor and compiler run separately, so they share reserved scratch RAM. */
#define cc_code ((int *)0xe000)
#define cc_names ((char *)0xe200)
int cc_variables;
int cc_error_offset;
int cc_depth;
char *cc_begin;

int cc_alpha(int ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'; }
int cc_digit(int ch) { return ch >= '0' && ch <= '9'; }
void cc_fail(void) {
    if (!cc_error) cc_error_offset = (int)(cc_source - cc_begin);
    cc_error = 1;
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
            if (!*cc_source) { cc_fail(); cc_token = 0; return; }
            cc_source = cc_source + 2;
        } else break;
    }
    ch = *cc_source; cc_text[0] = 0;
    if (ch == 0) { cc_token = 0; return; }
    if (cc_alpha(ch)) {
        count = 0;
        while (cc_alpha(*cc_source) || cc_digit(*cc_source)) {
            if (count == 63) { cc_fail(); cc_token = 0; return; }
            cc_text[count++] = *cc_source++;
        }
        cc_text[count] = 0; cc_token = TOKEN_ID; return;
    }
    if (cc_digit(ch)) {
        cc_value = 0; count = 0; base = 10;
        if (cc_source[0] == '0' && (cc_source[1] == 'x' || cc_source[1] == 'X')) { base = 16; cc_source = cc_source + 2; }
        while (1) {
            digit = *cc_source;
            if (cc_digit(digit)) digit = digit - '0';
            else if (base == 16 && digit >= 'a' && digit <= 'f') digit = digit - 'a' + 10;
            else if (base == 16 && digit >= 'A' && digit <= 'F') digit = digit - 'A' + 10;
            else break;
            if ((base == 10 && (cc_value > 3276 || (cc_value == 3276 && digit > 7))) ||
                (base == 16 && count == 4)) { cc_fail(); cc_token = 0; return; }
            cc_value = cc_value * base + digit; cc_source++; count++;
        }
        if (!count) cc_fail();
        cc_token = TOKEN_NUMBER; return;
    }
    if (ch == '"' || ch == '\'') {
        cc_source++; count = 0;
        while (*cc_source && *cc_source != ch) {
            digit = *cc_source++;
            if (digit == '\\') {
                if (!*cc_source) { cc_fail(); break; }
                digit = *cc_source++;
                if (digit == 'n') digit = '\n';
                else if (digit == 't') digit = '\t';
                else if (digit != '\\' && digit != '"' && digit != '\'') { cc_fail(); break; }
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
    cc_source++; cc_token = ch;
    if (*cc_source == ch) {
        if (ch == '&') cc_token = TOKEN_AND;
        else if (ch == '|') cc_token = TOKEN_OR;
        else if (ch == '+') cc_token = TOKEN_INC;
        else if (ch == '-') cc_token = TOKEN_DEC;
        else if (ch == '<') cc_token = TOKEN_SHL;
        else if (ch == '>') cc_token = TOKEN_SHR;
        else if (ch != '=') return;
        if (ch != '=') { cc_source++; return; }
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
    if (cc_position >= 511) { cc_fail(); return; }
    cc_code[cc_position++] = word;
}
void cg_literal(int reg, int value) { cg_emit(0x4001 + reg * 512); cg_emit(0xae01); cg_emit(value); }
void cg_push(void) { cg_emit(0x1dff); cg_emit(0x9180); }
void cg_pop(void) { cg_emit(0x6380); cg_emit(0x1d81); }
int cg_jump(int target) {
    int patch; patch = cc_position + 2;
    cg_literal(3, PROGRAM_BASE + target); cg_emit(0xb600); return patch;
}
void cg_patch(int patch) { if (!cc_error) cc_code[patch] = PROGRAM_BASE + cc_position; }
void cg_call(int address, int args) {
    cg_literal(3, address); cg_emit(CALL_R3); cg_emit(0x1d80 + args);
}
int cg_if_zero(void) { cg_emit(0x1080); cg_emit(0xaa04); return cg_jump(0); }
int cc_variable(char *name) {
    int i;
    for (i = 0; i < cc_variables; i++) if (strcmp(cc_names + i * NAME_WORDS, name) == 0) return i;
    return -1;
}
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
    int variable; int patch; int start; int count; int op;
    if (cc_error) return;
    cc_depth++;
    if (cc_depth > 32) { cc_fail(); return; }
    if (cc_token == TOKEN_NUMBER) { cg_literal(0, cc_value); cc_next(); }
    else if (cc_token == TOKEN_STRING) {
        count = strlen(cc_text);
        /* Inline string data is skipped by an absolute jump. */
        patch = cg_jump(0); start = cc_position;
        for (variable = 0; variable <= count; variable++) cg_emit(cc_text[variable]);
        cg_patch(patch); cg_literal(0, PROGRAM_BASE + start); cc_next();
    } else if (cc_token == '(') { cc_next(); cc_expression(); cc_expect(')'); }
    else if (cc_token == '-' || cc_token == '!' || cc_token == '+' || cc_token == '~') {
        op = cc_token; cc_next(); cc_primary(); cg_push();
        if (op == '-') cg_call(bob_address(cc_negate), 1);
        else if (op == '!') cg_call(bob_address(cc_not), 1);
        else { cg_emit(0x6180); cg_emit(0x1d81); if (op == '~') cg_emit(0x3100); }
    } else if (cc_token == TOKEN_INC || cc_token == TOKEN_DEC) {
        op = cc_token; cc_next(); variable = cc_variable(cc_text);
        if (cc_token != TOKEN_ID || variable < 0) { cc_fail(); return; }
        cc_next(); cg_literal(0, 0x9800 + variable); cg_emit(0x6000);
        if (op == TOKEN_INC) cg_emit(0x1181); else cg_emit(0x11ff);
        cg_literal(1, 0x9800 + variable); cg_emit(0x9040);
    } else if (cc_token == TOKEN_ID) {
        variable = cc_variable(cc_text);
        if (variable < 0) { cc_fail(); return; }
        cg_literal(0, 0x9800 + variable); cg_emit(0x6000); cc_next();
        if (cc_token == TOKEN_INC || cc_token == TOKEN_DEC) {
            op = cc_token; cc_next(); cg_push();
            if (op == TOKEN_INC) cg_emit(0x1181); else cg_emit(0x11ff);
            cg_literal(1, 0x9800 + variable); cg_emit(0x9040);
            cg_emit(0x6180); cg_emit(0x1d81);
        }
    } else cc_fail();
    cc_depth--;
}
void cc_binary(int minimum) {
    int op; int level; int shortcut; int end;
    cc_primary(); level = cc_precedence(cc_token);
    while (!cc_error && level >= minimum && level) {
        op = cc_token; cc_next();
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
void cc_expression(void) { cc_binary(1); }
void cc_patch_chain(int chain, int target) {
    int next;
    while (chain >= 0 && !cc_error) {
        next = cc_code[chain]; cc_code[chain] = PROGRAM_BASE + target; chain = next;
    }
}
void cc_simple(void) {
    char name[24]; int variable; int address; int op;
    if (cc_token == TOKEN_INC || cc_token == TOKEN_DEC) { cc_primary(); return; }
    if (cc_token != TOKEN_ID || strlen(cc_text) >= NAME_WORDS) { cc_fail(); return; }
    memcpy(name, cc_text, strlen(cc_text) + 1); cc_next(); op = cc_token;
    if (op == TOKEN_INC || op == TOKEN_DEC) {
        variable = cc_variable(name); if (variable < 0) { cc_fail(); return; }
        cc_next(); cg_literal(0, 0x9800 + variable); cg_emit(0x6000);
        if (op == TOKEN_INC) cg_emit(0x1181); else cg_emit(0x11ff);
        cg_literal(1, 0x9800 + variable); cg_emit(0x9040);
    } else if (op == '=' || (op >= TOKEN_ADD_ASSIGN && op <= TOKEN_XOR_ASSIGN)) {
        variable = cc_variable(name); if (variable < 0) { cc_fail(); return; }
        if (op != '=') { cg_literal(0, 0x9800 + variable); cg_emit(0x6000); cg_push(); }
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
            else op = '^';
            cc_operation(op);
        }
        cg_literal(1, 0x9800 + variable); cg_emit(0x9040);
    } else if (op == '(') {
        address = 0;
        if (strcmp(name, "print") == 0) address = bob_address(print);
        else if (strcmp(name, "println") == 0) address = bob_address(println);
        else if (strcmp(name, "print_dec") == 0) address = bob_address(print_dec);
        else if (strcmp(name, "print_hex") == 0) address = bob_address(print_hex);
        else if (strcmp(name, "bob_putc") == 0) address = 1;
        if (!address) { cc_fail(); return; }
        cc_next(); cc_expression(); cc_expect(')');
        if (address == 1) cg_emit(0xf100);
        else { cg_push(); cg_call(address, 1); }
    } else cc_fail();
}
void cc_statement(void) {
    int variable; int start; int patch; int end; int step; int body;
    if (cc_error) return;
    cc_depth++;
    if (cc_depth > 32) { cc_fail(); return; }
    if (cc_token == '{') {
        cc_next();
        while (!cc_error && cc_token && cc_token != '}') cc_statement();
        cc_expect('}');
    } else if (cc_is("int")) {
        cc_next();
        if (cc_token != TOKEN_ID || strlen(cc_text) >= NAME_WORDS || cc_variables == 16 || cc_variable(cc_text) >= 0) { cc_fail(); return; }
        variable = cc_variables++; memcpy(cc_names + variable * NAME_WORDS, cc_text, strlen(cc_text) + 1);
        cc_next();
        if (cc_token == '=') { cc_next(); cc_expression(); }
        else cg_literal(0, 0);
        cg_literal(1, 0x9800 + variable); cg_emit(0x9040); cc_expect(';');
    } else if (cc_is("return")) {
        cc_next();
        if (cc_token == ';') cg_literal(0, 0); else cc_expression();
        cg_emit(0x1cd0); cg_emit(0x6b80); cg_emit(0x1d81); cg_emit(0x6f80); cg_emit(0x1d81); cg_emit(0xe000);
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
    } else if (cc_is("for")) {
        if (cc_loop_depth == 16) { cc_fail(); return; }
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
int compile_command(char *source_name, char *output_name) {
    int slot; int result; int i; int line; int column;
    slot = file_find(source_name);
    if (slot < 0 || file_kinds[slot]) { println("C source file not found."); return 0; }
    if (!*output_name || strlen(output_name) >= NAME_WORDS) { println("Usage: cc SOURCE OUTPUT"); return 0; }
    if (strcmp(source_name, output_name) == 0) { println("Output must differ from source; choose another name."); return 0; }
    cc_source = file_content(slot); cc_begin = cc_source;
    cc_error = 0; cc_position = 0; cc_variables = 0; cc_error_offset = 0; cc_depth = 0; cc_loop_depth = 0;
    memset(cc_names, 0, 384); cc_next();
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
    if (cc_error) {
        print("C compile error near character "); print_dec(cc_error_offset);
        println(". Unsupported syntax or source/code/nesting limit exceeded.");
        line = 1; column = 1;
        for (i = 0; i < cc_error_offset; i++) {
            if (cc_begin[i] == '\n') { line++; column = 1; }
            else column++;
        }
        print(source_name); print(": line "); print_dec(line);
        print(", column "); print_dec(column); println(". Use edit to fix the source.");
        return 0;
    }
    result = file_write(output_name, cc_code, cc_position, 1);
    if (result < 0) println("Cannot save compiled program (max 511 words).");
    else { print("Compiled "); print_dec(cc_position); println(" words."); }
    return result >= 0;
}
