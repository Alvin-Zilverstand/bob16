int heap_used;
#ifdef BOBC_WIDE
int bit_masks[32] = {(-2147483647-1),1073741824,536870912,268435456,134217728,67108864,33554432,16777216,
    8388608,4194304,2097152,1048576,524288,262144,131072,65536,32768,16384,8192,4096,2048,1024,512,256,128,64,32,16,8,4,2,1};
#else
int bit_masks[16] = {-32768,16384,8192,4096,2048,1024,512,256,128,64,32,16,8,4,2,1};
#endif
int bit_weights[4] = {8,4,2,1};

int strlen(char *text) { int n; n = 0; while (text[n]) n++; return n; }
int strcmp(char *a, char *b) {
    int i; i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] - b[i];
}
#ifdef BOBC_WIDE
void memcpy(void *dst, void *src, int count) {
    int i; for (i = 0; i < count; i++) ((int *)dst)[i] = ((int *)src)[i];
}
void memset(void *dst, int value, int count) {
    int i; for (i = 0; i < count; i++) ((int *)dst)[i] = value;
}
#else
void word_copy(int *dst, int *src, int count) {
    int i; for (i = 0; i < count; i++) dst[i] = src[i];
}
void word_fill(int *dst, int value, int count) {
    int i; for (i = 0; i < count; i++) dst[i] = value;
}
#endif
void print(char *text) { int i; i = 0; while (text[i]) { bob_putc(text[i]); i++; } }
void println(char *text) { print(text); bob_putc('\n'); }
void decimal_negative(int number) {
    int quotient; int digit;
    quotient = number / 10;
    digit = -(number - quotient * 10);
    if (quotient) decimal_negative(quotient);
    bob_putc('0' + digit);
}
void print_dec(int number) {
    if (number < 0) bob_putc('-');
    else number = -number;
    decimal_negative(number);
}
void print_hex(int number) {
    int nibble; int bit; int digit;
    print("0x");
#ifdef BOBC_WIDE
    for (nibble = 0; nibble < 8; nibble++) {
#else
    for (nibble = 0; nibble < 4; nibble++) {
#endif
        digit = 0;
        for (bit = 0; bit < 4; bit++) {
            if (number & bit_masks[nibble * 4 + bit]) digit = digit + bit_weights[bit];
        }
        if (digit < 10) bob_putc('0' + digit);
        else bob_putc('A' + digit - 10);
    }
}
/* Consume the complete line, even when the destination fills. -2 means EOF. */
int read_line(char *buffer, int capacity) {
    int length; int character; int truncated;
    if (capacity < 1) return -1;
    length = 0; truncated = 0;
    while (1) {
        character = bob_getc();
        if (character == -1 || character == '\n') break;
        if (character == '\b' || character == 127) { if (length) length--; }
        else if (character != '\r') {
            if (length < capacity - 1) buffer[length++] = character;
            else truncated = 1;
        }
    }
    buffer[length] = 0;
    if (truncated) return -1;
    if (character == -1 && length == 0) return -2;
    return length;
}
int *alloc(int words) {
    int *address;
    if (words < 1 || words > HEAP_WORDS - heap_used) return 0;
    address = (int *)(HEAP_BASE + heap_used);
    heap_used = heap_used + words;
    memset(address, 0, words);
    return address;
}
