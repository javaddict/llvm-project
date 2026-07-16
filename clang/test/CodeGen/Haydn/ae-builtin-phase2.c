// RUN: %clang_cc1 -triple haydn-unknown-elf -ffreestanding -emit-llvm -o - %s | FileCheck %s
//
// D208 Phase 2 (D210): native __builtin_ae_* composed builtins.
// Each __builtin_ae_* bypasses the ClangBuiltin<> auto-map (HaydnAeBuiltin
// class has no ClangBuiltin<> annotation) and lowers through
// EmitHaydnBuiltinExpr to one or more llvm.haydn.* intrinsics.
//
// This test pins the IR shape: it does NOT enter CodeGen (no -O0 llc), so it
// does not depend on selector coverage for the component intrinsics. It only
// verifies that the right intrinsic calls are emitted in the right order with
// the right operands — which is what the EmitHaydnBuiltinExpr cases produce.
//
// Type model: ae_int16x4 is `long long` (i64) at the C level, but the lanewise
// Haydn intrinsics operate on <4 x i16>. EmitHaydnBuiltinExpr bit-casts i64
// <-> v4i16 around the lanewise calls (mirroring the auto-map cast logic).
// i64-typed intrinsics (x4fcmula16rs, movegpr2sfr) take i64 directly.
//
// Test strategy: exercise each builtin BOTH directly (the __builtin_ae_*
// surface) AND through the haydn_dsp.h macro (the cutover surface) to confirm
// both paths emit identical IR. The macros must produce the same intrinsic
// sequence as the raw builtins — that is the D208 Phase 2 contract.

typedef long long ae_int64;
typedef long long ae_int16x4;  // DR64 at the C level, matching haydn_dsp.h.

//--- Native 1:1 callouts -----------------------------------------------------
// AE_MUL16JS: multiply-by-j = X4MJSWAP16S (1 lanewise intrinsic + 2 casts).
ae_int16x4 mul16js_direct(ae_int16x4 a) {
  return __builtin_ae_mul16js(a);
}
// CHECK-LABEL: define dso_local i64 @mul16js_direct
// CHECK: bitcast i64 %{{.+}} to <4 x i16>
// CHECK: call <4 x i16> @llvm.haydn.x4mjswap16s(<4 x i16> %{{.+}})
// CHECK: bitcast <4 x i16> %{{.+}} to i64

// AE_CONJ16S: complex conjugate = X4CONJ16S (lanewise).
ae_int16x4 conj16s_direct(ae_int16x4 a) {
  return __builtin_ae_conj16s(a);
}
// CHECK-LABEL: define dso_local i64 @conj16s_direct
// CHECK: call <4 x i16> @llvm.haydn.x4conj16s(<4 x i16> %{{.+}})

// AE_MULAFC16RAS: 16-bit complex MAC, accumulate = X4FCMULA16RS (ternary i64,
// no cast — intrinsic is i64-typed per D211 accumulator-arity fix).
ae_int16x4 mulafc16ras_direct(ae_int16x4 acc, ae_int16x4 a, ae_int16x4 b) {
  return __builtin_ae_mulafc16ras(acc, a, b);
}
// CHECK-LABEL: define dso_local i64 @mulafc16ras_direct
// CHECK: call i64 @llvm.haydn.x4fcmula16rs(i64 %{{.+}}, i64 %{{.+}}, i64 %{{.+}})

//--- 1:N composed: AE_ADDANDSUBRNG16RAS --------------------------------------
// Alternating interleave {add3,sub2,add1,sub0}, composed via SFR (D210/ISA-39):
//   add = x4add16s(a,b); sub = x4sub16s(a,b);
//   movegpr2sfr(0b0101); x4movt16(add, sub).
// The _s0/_s1/_s2 slot variants emit identical IR; test one representative.
ae_int16x4 addsub_direct(ae_int16x4 a, ae_int16x4 b) {
  return __builtin_ae_addandsubrng16ras_s0(a, b);
}
// CHECK-LABEL: define dso_local i64 @addsub_direct
// CHECK: call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})
// CHECK: call <4 x i16> @llvm.haydn.x4sub16s(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})
// CHECK: call void @llvm.haydn.movegpr2sfr(i32 5)
// CHECK: call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})

//--- Macro cutover parity ----------------------------------------------------
// The haydn_dsp.h macros must produce the SAME IR as the direct builtins.
// This is the D208 Phase 2 cutover contract: the macros now route through the
// new __builtin_ae_* (retiring the broken __haydn_x4addsub16s decomposition).
#include <haydn_dsp.h>

ae_int16x4 mul16js_macro(ae_int16x4 a) {
  return AE_MUL16JS(a);
}
// CHECK-LABEL: define dso_local i64 @mul16js_macro
// CHECK: call <4 x i16> @llvm.haydn.x4mjswap16s(<4 x i16> %{{.+}})

ae_int16x4 conj16s_macro(ae_int16x4 a) {
  return AE_CONJ16S(a);
}
// CHECK-LABEL: define dso_local i64 @conj16s_macro
// CHECK: call <4 x i16> @llvm.haydn.x4conj16s(<4 x i16> %{{.+}})

ae_int16x4 mulafc16ras_macro(ae_int16x4 acc, ae_int16x4 a, ae_int16x4 b) {
  return AE_MULAFC16RAS(acc, a, b);
}
// CHECK-LABEL: define dso_local i64 @mulafc16ras_macro
// CHECK: call i64 @llvm.haydn.x4fcmula16rs(i64 %{{.+}}, i64 %{{.+}}, i64 %{{.+}})

ae_int16x4 addsub_macro(ae_int16x4 a, ae_int16x4 b) {
  return AE_ADDANDSUBRNG16RAS_S0(a, b);
}
// CHECK-LABEL: define dso_local i64 @addsub_macro
// CHECK: call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})
// CHECK: call <4 x i16> @llvm.haydn.x4sub16s(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})
// CHECK: call void @llvm.haydn.movegpr2sfr(i32 5)
// CHECK: call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})
