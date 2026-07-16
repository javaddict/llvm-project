// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s

// Test function call ABI for Haydn (ILP32)

// Simple function with i32 arguments
int add_ints(int a, int b, int c, int d) {
    // CHECK: define dso_local i32 @add_ints(i32 noundef %a, i32 noundef %b, i32 noundef %c, i32 noundef %d)
    return a + b + c + d;
}

// Function with i64 arguments (use DR64 registers)
long long add_longs(long long a, long long b) {
    // CHECK: define dso_local i64 @add_longs(i64 noundef %a, i64 noundef %b)
    return a + b;
}

// Function with many i32 arguments (tests register spill)
int many_args(int a, int b, int c, int d, int e, int f, int g, int h, int i) {
    // CHECK: define dso_local i32 @many_args(i32 noundef %a, i32 noundef %b, i32 noundef %c, i32 noundef %d, i32 noundef %e, i32 noundef %f, i32 noundef %g, i32 noundef %h, i32 noundef %i)
    return a + b + c + d + e + f + g + h + i;
}

// Function returning i64
long long return_long(void) {
    // CHECK: define dso_local i64 @return_long()
    return 0x1234567890ABCDEFLL;
}

// Function with float arguments
float add_floats(float a, float b) {
    // CHECK: define dso_local float @add_floats(float noundef %a, float noundef %b)
    return a + b;
}

// Function with double arguments
double add_doubles(double a, double b) {
    // CHECK: define dso_local double @add_doubles(double noundef %a, double noundef %b)
    return a + b;
}

// Test function call
void test_calls(void) {
    int x = add_ints(1, 2, 3, 4);
    long long y = add_longs(100LL, 200LL);
    float f = add_floats(1.5f, 2.5f);
    double d = add_doubles(1.5, 2.5);
    // CHECK: define dso_local void @test_calls()
}
