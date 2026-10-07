/* Native 16-bit unsigned operations, expressed in the signed C foundation. */
int __u_lt(int a, int b) {
    if (a < 0) { if (b >= 0) return 0; }
    else if (b < 0) return 1;
    return a < b;
}
int __u_gt(int a, int b) { return __u_lt(b, a); }
int __u_le(int a, int b) { return !__u_lt(b, a); }
int __u_ge(int a, int b) { return !__u_lt(a, b); }
int __u_div(int a, int b) {
    int q = 0, r = 0, i = 0, carry, bit;
    if (b == 0) return 0;
    while (i < 16) {
        carry = r < 0; bit = a < 0;
        r = r + r + bit; a = a + a; q = q + q;
        if (carry || !__u_lt(r, b)) { r = r - b; q++; }
        i++;
    }
    return q;
}
int __u_mod(int a, int b) { if (b == 0) return 0; return a - __mul(__u_div(a, b), b); }
int __u_shr(int a, int b) {
    if (b < 0 || b > 15) return 0;
    while (b > 0) { a = __shr(a, 1) & 32767; b--; }
    return a;
}
