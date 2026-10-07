/* bob32 arithmetic helpers. All results use native 32-bit wraparound. */
int __cmp_lt(int a, int b) { return a < b; }
int __cmp_gt(int a, int b) { return a > b; }
int __cmp_le(int a, int b) { return a <= b; }
int __cmp_ge(int a, int b) { return a >= b; }
int __cmp_eq(int a, int b) { return a == b; }
int __cmp_ne(int a, int b) { return a != b; }
int __mul(int a, int b) {
    int result=0, bit=1, i=0;
    while(i<32) {
        if(b & bit) result=result+a;
        a=a+a; bit=bit+bit; i++;
    }
    return result;
}
int __u_lt(int a, int b) {
    if(a<0) { if(b>=0) return 0; }
    else if(b<0) return 1;
    return a<b;
}
int __u_gt(int a, int b) { return __u_lt(b,a); }
int __u_le(int a, int b) { return !__u_lt(b,a); }
int __u_ge(int a, int b) { return !__u_lt(a,b); }
int __u_div(int a, int b) {
    int q=0, r=0, i=0, carry, bit;
    if(b==0) return 0;
    while(i<32) {
        carry=r<0; bit=a<0;
        r=r+r+bit; a=a+a; q=q+q;
        if(carry || !__u_lt(r,b)) { r=r-b; q++; }
        i++;
    }
    return q;
}
int __u_mod(int a, int b) {
    if(b==0) return 0;
    return a-__mul(__u_div(a,b),b);
}
int __div(int a, int b) {
    int negative=(a<0)!=(b<0), ua, ub, q;
    if(b==0) return 0;
    ua=a<0?0-a:a; ub=b<0?0-b:b;
    q=__u_div(ua,ub);
    return negative?0-q:q;
}
int __mod(int a, int b) {
    int ua, ub, r;
    if(b==0) return 0;
    ua=a<0?0-a:a; ub=b<0?0-b:b;
    r=__u_mod(ua,ub);
    return a<0?0-r:r;
}
int __or(int a, int b) { return ~(~a & ~b); }
int __xor(int a, int b) { return __or(a,b) & ~(a & b); }
int __shl(int a, int b) {
    if(b<0 || b>31) return 0;
    while(b>0) { a=a+a; b--; }
    return a;
}
int __shr(int a, int b) {
    int low;
    if(b<0 || b>31) return 0;
    while(b>0) {
        low=a&1;
        if(a<0) a=-1073741824+((a&2147483647)-low)/2;
        else a=(a-low)/2;
        b--;
    }
    return a;
}
int __u_shr(int a, int b) {
    if(b<0 || b>31) return 0;
    while(b>0) { a=__shr(a,1)&2147483647; b--; }
    return a;
}
