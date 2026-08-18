// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
// REQUIRES: haydn-registered-target
//
// Overaligned C types. Aggregates stay Indirect so the pointed-to object
// carries the C alignment (FrameLowering realigns MaxAlign > 8).
// Overaligned scalars stay Direct: bits travel in a register / 8-byte
// slot and are copied into an aligned local. Do not invent a second CC.

typedef int A16 __attribute__((aligned(16)));
typedef long long A32 __attribute__((aligned(32)));

struct S16 {
  int x;
} __attribute__((aligned(16)));

// CHECK: define dso_local i32 @take_a16(i32 noundef %{{[^)]+}})
int take_a16(A16 x) { return x; }

// CHECK: define dso_local i64 @take_a32(i64 noundef %{{[^)]+}})
long long take_a32(A32 x) { return x; }

// CHECK: define dso_local i32 @take_s16(ptr {{[^,]*}}%{{[^)]+}})
// CHECK-NOT: byval
int take_s16(struct S16 s) { return s.x; }

// CHECK: define dso_local i32 @ret_a16()
A16 ret_a16(void) { return 1; }

// CHECK: define dso_local void @ret_s16(ptr {{[^,]*}}sret(%struct.S16){{[^,]*}}align 16
struct S16 ret_s16(void) {
  struct S16 s = {1};
  return s;
}

// Default-aligned SIMD stays Direct (DR / GPR).
typedef int v2i32 __attribute__((vector_size(8)));
// CHECK: define dso_local <2 x i32> @ret_simd(<2 x i32> noundef %{{[^)]+}})
v2i32 ret_simd(v2i32 a) { return a; }
