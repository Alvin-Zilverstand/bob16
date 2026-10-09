typedef int count_t;
typedef char byte_t;
typedef int *int_ptr;
typedef unsigned long long uint64_value;

struct size_probe { char tag; int value; long long wide; };
int global_value = 42;
byte_t global_message[5] = "bob!";
void *global_value_address = &global_value;
void *global_message_address = global_message;
void *global_literal_address = "bob!";

int sum_values(int values[], int count) {
    count_t sum = 0;
    for (int i = 0; i < count; i++)
        sum = sum + values[i];
    return sum;
}

count_t text_length(byte_t text[]) {
    count_t length = 0;
    while (text[length] != '\0')
        length++;
    return length;
}

int_ptr first_value(int_ptr values) {
    int_ptr first = values;
    return first++;
}

void *opaque_identity(int marker, void *value) {
    int *typed_value = value;
    if (marker != 42)
        *typed_value = 0;
    return value;
}

usize wide_division(usize value) {
    return value / 10 + value % 10;
}

uint64_value add_unsigned_wide(uint64_value value,
                               unsigned long long int amount) {
    return value + amount;
}

int sum_sixteen(int a, int b, int c, int d, int e, int f, int g, int h,
                int i, int j, int k, int l, int m, int n, int o, int p) {
    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p;
}

int stack_argument_test(void) {
    return sum_sixteen(1, 2, 3, 4, 5, 6, 7, 8,
                       9, 10, 11, 12, 13, 14, 15, 16);
}

int do_while_test(void) {
    int count = 0;
    int total = 0;
    do {
        count++;
        if (count == 2) continue;
        total = total + count;
    } while (count < 4);
    return total;
}

enum SwitchValue { SWITCH_ONE = 1, SWITCH_TWO, SWITCH_THREE };

int switch_test(enum SwitchValue value) {
    int total = 0;
    switch (value) {
        case SWITCH_ONE: total = 10; break;
        case SWITCH_TWO: total = 20;
        case SWITCH_THREE: total = total + 3; break;
        default: total = 99;
    }
    return total;
}

int inspect_mixed_arguments(int a, int b, int c, int d, int *direct,
                            int e, void *opaque, int f) {
    int *converted = opaque;
    return direct[0] + converted[1] + a + b + c + d + e + f;
}

void *identity_with_stack_pointer(int a, int b, int c, int d, void *value,
                                  int marker) {
    int *typed_value = value;
    if (a + b + c + d != 10 || marker != 42)
        typed_value[0] = -1;
    return value;
}

int main(void) {
    count_t values[64];
    byte_t message[5] = "bob!";
    int_ptr value_pointer = first_value(values);
    void *opaque_values = values;
    void *opaque_pointer = opaque_identity(42, value_pointer);
    int_ptr restored_pointer = opaque_pointer;
    uintptr_t raw_global_address = (uintptr_t)&global_value;
    void *cast_opaque_address = (void *)raw_global_address;
    int *cast_global_pointer = (int *)cast_opaque_address;
    short cast_short = (short)0x10034;
    char cast_character = (char)0x142;
    short cast_negative = (short)0xffff;
    int cast_int = (int)0x10000002aULL;
    unsigned long long unsigned_wide = 0xffffffffffffffffULL;
    int_ptr global_value_pointer = global_value_address;
    byte_t *global_message_pointer = global_message_address;
    byte_t *global_literal_pointer = global_literal_address;
    int v0;
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
    int v39;
    v0 = 0;
    v39 = 39;
    v0++;
    ++v0;
    v39--;
    ++v39;
    values[0] = 12;
    values[1] = 13;
    values[2] = 17;
    values[63] = 42;
    if (sum_values(values, 3) == 42 && text_length(message) == 4 &&
        stack_argument_test() == 136 && do_while_test() == 8 &&
        switch_test(2) == 23 &&
        inspect_mixed_arguments(1, 2, 3, 4, values, 5,
                                identity_with_stack_pointer(1, 2, 3, 4,
                                                            opaque_values, 42),
                                6) == 46 &&
        values[0] == 12 && values[63] == 42 &&
        value_pointer[63] == 42 && restored_pointer[63] == 42 &&
        *cast_global_pointer == 42 && cast_short == 0x34 &&
        cast_character == 'B' && cast_negative == -1 && cast_int == 42 &&
        *global_value_pointer == 42 && global_message_pointer[1] == 'o' &&
        global_literal_pointer[0] == 'b' && v0 + v39 == 41 &&
        20 / 3 == 6 && 20 % 3 == 2 &&
        -20 / 3 == -6 && -20 % 3 == -2 &&
        wide_division(0x100000001ULL) == 429496736 &&
        sizeof(unsigned long long) == 8 &&
        add_unsigned_wide(unsigned_wide, 1ULL) == 0 &&
        unsigned_wide > 0x7fffffffffffffffULL &&
        (0x100000001ULL >> 32) == 1 &&
        ((0x5a ^ 0x3c) & 0xff) == 0x66 &&
        (1 | 2 ^ 3 & 1) == 3 && (-8 >> 2) == -2 &&
        sizeof(char) == 1 && sizeof(short) == 2 && sizeof(int) == 4 &&
        sizeof(long) == 4 && sizeof(long long) == 8 && sizeof(usize) == 8 &&
        sizeof(void *) == 8 && sizeof(int *) == 8 &&
        sizeof(count_t[64]) == 256 &&
        sizeof(struct size_probe) == 16 && sizeof(values) == 256 &&
        sizeof(message) == 5 && sizeof(value_pointer) == 8 &&
        sizeof(values[0]) == 4 && sizeof(text_length(message)) == 4) {
        bob64_app_write(message, text_length(message));
        return 0;
    }
    return 1;
}
