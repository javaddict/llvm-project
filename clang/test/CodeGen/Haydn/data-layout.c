// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
// REQUIRES: haydn-registered-target

// REGRESSION TEST: ilp32 ABI requires sizeof(long) == 4 (F05).
//
// Bug (F05 / consolidated-fix-list §2): HaydnTargetInfo declared setABI("ilp32")
// but set LongWidth = 64. The "ilp32" name mandates int/long/pointer = 32-bit.
// HiFi/NatureDSP headers assume long is 32-bit, so ported kernels saw surprising
// type widths. The fix sets LongWidth = LongAlign = 32 while keeping
// LongLongWidth = 64 (so 64-bit accumulator arithmetic is still available).
// See D102-ilp32-long-width.md.
//
// Test design: this asserts the widths at compile time. If LongWidth regresses
// back to 64, the _Static_assert for sizeof(long) == 4 fires.

// CHECK: target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:64-v128:64-a:0:32-n32-S64"

_Static_assert(sizeof(int) == 4, "int is 4 bytes");
_Static_assert(sizeof(long) == 4, "ilp32: long is 4 bytes (F05)");
_Static_assert(sizeof(long long) == 8, "long long stays 8 bytes");
_Static_assert(sizeof(void *) == 4, "pointer is 4 bytes");

// CHECK: define dso_local i32 @test_long_width
int test_long_width(long a, long b) {
  // CHECK: ret i32
  return (int)(a + b);
}
