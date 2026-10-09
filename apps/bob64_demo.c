typedef int count_t;
typedef char byte_t;
typedef int *int_ptr;

int sum_values(int values[], int count) {
    count_t sum = 0;
    for (int i = 0; i < count; i = i + 1)
        sum = sum + values[i];
    return sum;
}

count_t text_length(byte_t text[]) {
    count_t length = 0;
    while (text[length] != '\0')
        length = length + 1;
    return length;
}

int_ptr first_value(int_ptr values) {
    return values;
}

usize wide_division(usize value) {
    return value / 10 + value % 10;
}

int sum_sixteen(int a, int b, int c, int d, int e, int f, int g, int h,
                int i, int j, int k, int l, int m, int n, int o, int p) {
    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p;
}

int stack_argument_test(void) {
    return sum_sixteen(1, 2, 3, 4, 5, 6, 7, 8,
                       9, 10, 11, 12, 13, 14, 15, 16);
}

int main(void) {
    count_t values[64];
    byte_t message[5] = "bob!";
    int_ptr value_pointer = first_value(values);
    int v0 = 0;
    int v1 = 1;
    int v2 = 2;
    int v3 = 3;
    int v4 = 4;
    int v5 = 5;
    int v6 = 6;
    int v7 = 7;
    int v8 = 8;
    int v9 = 9;
    int v10 = 10;
    int v11 = 11;
    int v12 = 12;
    int v13 = 13;
    int v14 = 14;
    int v15 = 15;
    int v16 = 16;
    int v17 = 17;
    int v18 = 18;
    int v19 = 19;
    int v20 = 20;
    int v21 = 21;
    int v22 = 22;
    int v23 = 23;
    int v24 = 24;
    int v25 = 25;
    int v26 = 26;
    int v27 = 27;
    int v28 = 28;
    int v29 = 29;
    int v30 = 30;
    int v31 = 31;
    int v32 = 32;
    int v33 = 33;
    int v34 = 34;
    int v35 = 35;
    int v36 = 36;
    int v37 = 37;
    int v38 = 38;
    int v39 = 39;
    values[0] = 12;
    values[1] = 13;
    values[2] = 17;
    values[63] = 42;
    if (sum_values(values, 3) == 42 && text_length(message) == 4 &&
        stack_argument_test() == 136 && values[63] == 42 &&
        value_pointer[63] == 42 && v0 + v39 == 39 &&
        20 / 3 == 6 && 20 % 3 == 2 &&
        -20 / 3 == -6 && -20 % 3 == -2 &&
        wide_division(0x100000001ULL) == 429496736 &&
        (0x100000001ULL >> 32) == 1 &&
        ((0x5a ^ 0x3c) & 0xff) == 0x66 &&
        (1 | 2 ^ 3 & 1) == 3 && (-8 >> 2) == -2) {
        bob64_app_write(message, text_length(message));
        return 0;
    }
    return 1;
}
