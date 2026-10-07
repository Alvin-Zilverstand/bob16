int __cmp_lt(int a, int b) { return a < b; }
int __cmp_gt(int a, int b) { return a > b; }
int __cmp_le(int a, int b) { return a <= b; }
int __cmp_ge(int a, int b) { return a >= b; }
int __cmp_eq(int a, int b) { return a == b; }
int __cmp_ne(int a, int b) { return a != b; }
int __mul(int a, int b) {
    int result; int bit; result = 0; bit = 1;
    /* The guest uses wrapping 16-bit words. Each bit contributes its shifted
       multiplicand; the mask wraps to zero after exactly sixteen steps. */
    while (bit) {
        if (b & bit) result = result + a;
        a = a + a; bit = bit + bit;
    }
    return result;
}
int __divneg(int a, int b) {
    if(b == -1)return -a;
    int q; q = 0;
    while (a <= b) { a = a - b; q++; }
    return q;
}
int __div(int a, int b) {
    int negative; int q;
    if (b == 0) return 0;
    negative = (a ^ b) < 0;
    if (a > 0) a = -a;
    if (b > 0) b = -b;
    q = __divneg(a, b);
    if (negative) return -q;
    return q;
}
int __mod(int a, int b) { if (b == 0) return 0; return a - __mul(__div(a, b), b); }
int __or(int a, int b) { return ~(~a & ~b); }
int __xor(int a, int b) { return __or(a, b) & ~(a & b); }
int __shl(int a, int b) { if (b < 0 || b > 15) return 0; while (b > 0) { a = a + a; b--; } return a; }
int __shr(int a, int b) {
    int low;
    if (b < 0 || b > 15) return 0;
    while (b > 0) {
        low = a & 1;
        if (a < 0) a = -16384 + ((a & 32767) - low) / 2;
        else a = (a - low) / 2;
        b--;
    }
    return a;
}
