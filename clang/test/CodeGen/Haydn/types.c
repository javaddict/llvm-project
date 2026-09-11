// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s

// Test data type sizes and alignments for Haydn (ILP32 ABI)

// CHECK: target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:64-v128:64-a:0:32-n32-S64"

#include <stdint.h>

// Check type sizes (ilp32 ABI: int/long/pointer are 32-bit; see D102-ilp32-long-width.md)
_Static_assert(sizeof(char) == 1, "char is 1 byte");
_Static_assert(sizeof(short) == 2, "short is 2 bytes");
_Static_assert(sizeof(int) == 4, "int is 4 bytes");
_Static_assert(sizeof(long) == 4, "long is 4 bytes (ilp32)");
_Static_assert(sizeof(long long) == 8, "long long is 8 bytes");
_Static_assert(sizeof(void*) == 4, "pointer is 4 bytes");
_Static_assert(sizeof(float) == 4, "float is 4 bytes");
_Static_assert(sizeof(double) == 8, "double is 8 bytes");

// Check type sizes via types
_Static_assert(sizeof(int8_t) == 1, "int8_t is 1 byte");
_Static_assert(sizeof(int16_t) == 2, "int16_t is 2 bytes");
_Static_assert(sizeof(int32_t) == 4, "int32_t is 4 bytes");
_Static_assert(sizeof(int64_t) == 8, "int64_t is 8 bytes");
_Static_assert(sizeof(uintptr_t) == 4, "uintptr_t is 4 bytes");

// C1.1 / G-PRED-SSA: haydn_pred2_t / haydn_pred4_t are i32-width SSA values.
#include <haydn_types.h>
_Static_assert(sizeof(haydn_pred2_t) == 4, "haydn_pred2_t is 4 bytes");
_Static_assert(sizeof(haydn_pred4_t) == 4, "haydn_pred4_t is 4 bytes");
_Static_assert(sizeof(haydn_x2int32) == 8, "haydn_x2int32 is one DR64");
_Static_assert(sizeof(haydn_x4int16) == 8, "haydn_x4int16 is one DR64");

// Check basic type operations
int test_int_sizes(void) {
    int x = 0;
    long y = 0;
    long long z = 0;
    void *p = (void*)0;
    haydn_pred2_t p2 = 0;
    haydn_pred4_t p4 = 0;

    // CHECK: define dso_local i32 @test_int_sizes()
    return x + (int)y + (int)z + (int)(intptr_t)p + (int)p2 + (int)p4;
}
