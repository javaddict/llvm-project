// RUN: %clang_cc1 -triple haydn-unknown-elf -O1 -emit-llvm -o - %s | FileCheck %s
//
// Phase 0 test for D208 (native AE_* builtins + vector types).
//
// Verifies the haydn_types.h vector typedefs lower to real <N x M> LLVM IR
// types (not opaque i64), so lanewise arithmetic can reach the Haydn GISel
// vector pipeline (X4ADD16, X2ADD32, ...). The companion backend selection
// (<4 x i16> add -> x4add16) is covered by llvm/test/CodeGen/Haydn/.
//
// If this test regresses (vector ops become scalar/bitcast i64 sequences),
// the D208 type layer is broken: ae_int16x4 must stay a real vector type,
// not collapse back to the pre-D208 opaque long long typedef.

#include <haydn_types.h>

// CHECK: define dso_local noundef <4 x i16> @add_v4i16(<4 x i16> noundef %a, <4 x i16> noundef %b)
// CHECK: %add = add <4 x i16> %{{.*}}, %{{.*}}
ae_int16x4 add_v4i16(ae_int16x4 a, ae_int16x4 b) {
  return a + b;
}

// CHECK: define dso_local noundef <2 x i32> @add_v2i32(<2 x i32> noundef %a, <2 x i32> noundef %b)
// CHECK: %add = add <2 x i32> %{{.*}}, %{{.*}}
ae_int32x2 add_v2i32(ae_int32x2 a, ae_int32x2 b) {
  return a + b;
}

// CHECK: define dso_local noundef <4 x i16> @mul_v4i16(<4 x i16> noundef %a, <4 x i16> noundef %b)
// CHECK: %mul = mul <4 x i16> %{{.*}}, %{{.*}}
ae_int16x4 mul_v4i16(ae_int16x4 a, ae_int16x4 b) {
  return a * b;
}

// CHECK: define dso_local noundef <2 x i32> @sub_v2i32(<2 x i32> noundef %a, <2 x i32> noundef %b)
// CHECK: %sub = sub <2 x i32> %{{.*}}, %{{.*}}
ae_int32x2 sub_v2i32(ae_int32x2 a, ae_int32x2 b) {
  return a - b;
}

// Size checks: every vector type must be exactly 8 bytes (one DR64 register).
_Static_assert(sizeof(ae_int16x4) == 8, "ae_int16x4 is one DR64 (8 bytes)");
_Static_assert(sizeof(ae_int32x2) == 8, "ae_int32x2 is one DR64 (8 bytes)");
_Static_assert(sizeof(ae_f32x2) == 8, "ae_f32x2 is one DR64 (8 bytes)");
_Static_assert(sizeof(ae_f16x4) == 8, "ae_f16x4 is one DR64 (8 bytes)");
_Static_assert(sizeof(ae_int64) == 8, "ae_int64 is one DR64 (8 bytes)");
_Static_assert(sizeof(ae_f64) == 8, "ae_f64 is one DR64 (8 bytes)");

// Haydn-native vector type names produce the same IR and are cross-assignable
// with the HiFi ae_* aliases (they are the same underlying type).
// CHECK: define dso_local noundef <4 x i16> @native_add16(<4 x i16> noundef %a, <4 x i16> noundef %b)
// CHECK: %add = add <4 x i16> %{{.*}}, %{{.*}}
haydn_4xint16 native_add16(haydn_4xint16 a, haydn_4xint16 b) {
  return a + b;
}

// CHECK: define dso_local noundef <2 x i32> @native_add32(<2 x i32> noundef %a, <2 x i32> noundef %b)
// CHECK: %add = add <2 x i32> %{{.*}}, %{{.*}}
haydn_2xint32 native_add32(haydn_2xint32 a, haydn_2xint32 b) {
  return a + b;
}

// Cross-assignable: ae_int16x4 and haydn_4xint16 are the same type.
// CHECK: define dso_local noundef <4 x i16> @cross_assign(<4 x i16> noundef returned %x)
haydn_4xint16 cross_assign(ae_int16x4 x) {
  return x;
}

_Static_assert(sizeof(haydn_4xint16) == 8, "haydn_4xint16 is 8 bytes");
_Static_assert(sizeof(haydn_2xint32) == 8, "haydn_2xint32 is 8 bytes");
_Static_assert(sizeof(haydn_4xfract16) == 8, "haydn_4xfract16 is 8 bytes");
_Static_assert(sizeof(haydn_2xfract32) == 8, "haydn_2xfract32 is 8 bytes");
_Static_assert(sizeof(haydn_2xfloat32) == 8, "haydn_2xfloat32 is 8 bytes");
