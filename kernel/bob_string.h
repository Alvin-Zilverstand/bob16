#ifndef BOB_STRING_H
#define BOB_STRING_H

/* Small word-addressed string helpers for native bob applications. */
int bob_strlen(const char *text) {
    int length; length = 0;
    while (text[length]) length++;
    return length;
}

char *bob_strcpy(char *destination, const char *source) {
    int i; i = 0;
    while (source[i]) { destination[i] = source[i]; i++; }
    destination[i] = 0;
    return destination;
}

int bob_strcmp(const char *left, const char *right) {
    int i; i = 0;
    while (left[i] && left[i] == right[i]) i++;
    return left[i] - right[i];
}

char *bob_strcat(char *destination, const char *source) {
    int i; int j;
    i = bob_strlen(destination); j = 0;
    while (source[j]) destination[i++] = source[j++];
    destination[i] = 0;
    return destination;
}

char *bob_strchr(const char *text, int character) {
    while (*text && *text != character) text++;
    if (*text == character) return (char *)text;
    return 0;
}

int bob_isspace(int character) {
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\r' || character == '\v' || character == '\f';
}

/* Decimal and 0x-prefixed signed integers. valid is cleared on any error. */
int bob_parse_int(const char *text, int *valid) {
    int negative; int base; int digit; int digits;
    unsigned int value; unsigned int limit; unsigned int radix;
    const char *cursor;
    *valid = 0; cursor = text; negative = 0; base = 10; digits = 0; value = 0;
    while (bob_isspace(*cursor)) cursor++;
    if (*cursor == '+' || *cursor == '-') {
        negative = *cursor == '-'; cursor++;
    }
    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        base = 16; cursor += 2;
    }
    radix = (unsigned int)base;
    limit = negative ? 0x80000000U : 0x7fffffffU;
    while (*cursor) {
        if (bob_isspace(*cursor)) break;
        digit = *cursor;
        if (digit >= '0' && digit <= '9') digit -= '0';
        else if (digit >= 'a' && digit <= 'f') digit = digit - 'a' + 10;
        else if (digit >= 'A' && digit <= 'F') digit = digit - 'A' + 10;
        else return 0;
        if (digit >= base || value > limit / radix ||
            (value == limit / radix && (unsigned int)digit > limit % radix)) return 0;
        value = value * radix + (unsigned int)digit;
        digits++; cursor++;
    }
    while (bob_isspace(*cursor)) cursor++;
    if (!digits || *cursor) return 0;
    *valid = 1;
    if (!negative) return (int)value;
    if (value == 0x80000000U) return -2147483647 - 1;
    return -(int)value;
}

/* Returns the character count, or -1 when the destination is too small. */
int bob_format_int(int value, char *destination, int capacity) {
    char reverse[11];
    unsigned int magnitude; int negative; int count; int i;
    if (capacity < 1) return -1;
    negative = value < 0;
    magnitude = negative ? 0U - (unsigned int)value : (unsigned int)value;
    count = 0;
    do {
        reverse[count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude);
    if (count + negative + 1 > capacity) return -1;
    i = 0;
    if (negative) destination[i++] = '-';
    while (count) destination[i++] = reverse[--count];
    destination[i] = 0;
    return i;
}

/* In-place shell-like token reader: whitespace separates, quotes group, and
   backslash quotes the next character. Returns 1/token, 0/end, or -1/bad quote. */
int bob_token_next(char **cursor, char **token) {
    char *scan; char *write; int quote; int character;
    if (!cursor || !token || !*cursor) return -1;
    scan = *cursor;
    while (bob_isspace(*scan)) scan++;
    if (!*scan) { *cursor = scan; *token = 0; return 0; }
    write = scan; *token = write; quote = 0;
    while (*scan) {
        character = *scan;
        if (quote) {
            if (character == quote) { quote = 0; scan++; continue; }
            if (character == '\\' && scan[1]) scan++;
        } else {
            if (bob_isspace(character)) break;
            if (character == '"' || character == '\'') { quote = character; scan++; continue; }
            if (character == '\\' && scan[1]) scan++;
        }
        *write++ = *scan++;
    }
    if (quote) { *token = 0; *cursor = scan; return -1; }
    while (bob_isspace(*scan)) scan++;
    *write = 0; *cursor = scan;
    return 1;
}

#endif
