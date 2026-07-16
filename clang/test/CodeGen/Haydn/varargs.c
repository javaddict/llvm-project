// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s

// Test varargs for Haydn (AArch64-style structured va_list: D178 / L146).
// __builtin_va_list is a 5-field struct {__stack, __gr_top, __vr_top,
// __gr_offs, __vr_offs} so two register banks (GPR R1-R7, DR D0-D3) can be
// walked independently. These checks only verify the emitted function
// signatures; the va_list struct layout and va_arg bank-selection are
// exercised by llvm/test/CodeGen/Haydn/varargs-two-bank-va-list.ll.

#include <stdarg.h>

// Simple varargs function
int sum_ints(int count, ...) {
    // CHECK: define dso_local i32 @sum_ints(i32 noundef %{{[a-zA-Z0-9]+}}, ...)
    va_list args;
    va_start(args, count);
    int sum = 0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int);
    }
    va_end(args);
    return sum;
}

// Varargs with long long
long long sum_longs(int count, ...) {
    // CHECK: define dso_local i64 @sum_longs(i32 noundef %{{[a-zA-Z0-9]+}}, ...)
    va_list args;
    va_start(args, count);
    long long sum = 0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, long long);
    }
    va_end(args);
    return sum;
}

// Varargs with mixed types
double mixed_args(int count, ...) {
    // CHECK: define dso_local double @mixed_args(i32 noundef %{{[a-zA-Z0-9]+}}, ...)
    va_list args;
    va_start(args, count);
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        if (i % 3 == 0) {
            sum += va_arg(args, int);
        } else if (i % 3 == 1) {
            sum += va_arg(args, long long);
        } else {
            sum += va_arg(args, double);
        }
    }
    va_end(args);
    return sum;
}

// Test varargs
void test_varargs(void) {
    int s = sum_ints(5, 1, 2, 3, 4, 5);
    long long l = sum_longs(3, 100LL, 200LL, 300LL);
    double d = mixed_args(6, 1, 2LL, 3.0, 4, 5LL, 6.0);
    // CHECK: define dso_local void @test_varargs()
}
