// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
// REQUIRES: haydn-registered-target
//
// G-ABI-VEC residual: aggregates are Indirect (plain pointer), never byval.
// Large aggregate returns use a hidden sret pointer. 64b SIMD stays Direct.
//
// Hard constraint: HaydnCallingConv.td has no CCIfByVal; stack slots are
// pointer-sized. Emitting byval makes GlobalISel memcpy aggregate contents
// into 8-byte slots (va-arg-22 overlap / garbage pointers).

struct Tiny {
  int x; // 4 bytes — still Indirect (not coerced into R1)
};

struct Big {
  long long a, b, c, d; // 32 bytes — does not fit in R1–R2 / D0
};

// T-ABI7 current law: ALL C aggregates are Indirect, including a single int.
// CHECK: define dso_local i32 @take_tiny(ptr {{[^,]*}}%{{[^)]+}})
// CHECK-NOT: byval
int take_tiny(struct Tiny s) { return s.x; }

// CHECK: define dso_local i32 @take_big(ptr {{[^,]*}}%{{[^)]+}})
// CHECK-NOT: byval
int take_big(struct Big s) {
  return (int)(s.a + s.b + s.c + s.d);
}

// CHECK: define dso_local void @make_big(ptr {{[^,]*}}sret(%struct.Big){{[^,]*}}%{{[^,]+}}, i64 noundef %{{[^)]+}})
struct Big make_big(long long x) {
  struct Big r = {x, x + 1, x + 2, x + 3};
  return r;
}

// CHECK: define dso_local i32 @call_big()
int call_big(void) {
  struct Big b = {1, 2, 3, 4};
  // Call passes a plain pointer (no byval attribute on the call site).
  // CHECK: call i32 @take_big(ptr {{[^)]*}}%{{[^)]+}})
  // CHECK-NOT: byval
  return take_big(b);
}

// 64b SIMD remains Direct (DR), not sret/Indirect.
typedef int v2i32 __attribute__((vector_size(8)));
// CHECK: define dso_local <2 x i32> @ret_simd(<2 x i32> noundef %{{[^)]+}})
v2i32 ret_simd(v2i32 a) { return a; }
