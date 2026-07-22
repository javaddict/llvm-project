// RUN: %clang_cc1 -triple haydn-unknown-elf -ffreestanding -emit-llvm -o - %s | FileCheck %s
//
// D208 Phase 2 (D210): native __builtin_ae_* composed builtins.
// Each __builtin_ae_* bypasses ClangBuiltin auto-map and lowers through
// EmitHaydnBuiltinExpr to one or more llvm.haydn.* intrinsics.
//
// Golden lanes: lanewise Haydn intrinsics are <4 x i16>; AE bag surface is
// still long long and EmitHaydnBuiltinExpr bit-casts around the call.

typedef long long ae_int64;
typedef long long ae_int16x4; // AE bag surface (DR64)

//--- Native 1:1 callouts -----------------------------------------------------
ae_int16x4 mul16js_direct(ae_int16x4 a) {
  return __builtin_ae_mul16js(a);
}
// CHECK-LABEL: define dso_local i64 @mul16js_direct
// CHECK: bitcast i64 %{{.+}} to <4 x i16>
// CHECK: call <4 x i16> @llvm.haydn.x4mjswap16s(<4 x i16> %{{.+}})
// CHECK: bitcast <4 x i16> %{{.+}} to i64

ae_int16x4 conj16s_direct(ae_int16x4 a) {
  return __builtin_ae_conj16s(a);
}
// CHECK-LABEL: define dso_local i64 @conj16s_direct
// CHECK: call <4 x i16> @llvm.haydn.x4conj16s(<4 x i16> %{{.+}})

// AE_MULAFC16RAS → X4FCMULA16RS (v4i16, bag↔vector bitcasts)
ae_int16x4 mulafc16ras_direct(ae_int16x4 acc, ae_int16x4 a, ae_int16x4 b) {
  return __builtin_ae_mulafc16ras(acc, a, b);
}
// CHECK-LABEL: define dso_local i64 @mulafc16ras_direct
// CHECK: call <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16> %{{.+}}, <4 x i16> %{{.+}}, <4 x i16> %{{.+}})

//--- 1:N composed: AE_ADDANDSUBRNG16RAS --------------------------------------
ae_int16x4 addsub_direct(ae_int16x4 a, ae_int16x4 b) {
  return __builtin_ae_addandsubrng16ras_s0(a, b);
}
// CHECK-LABEL: define dso_local i64 @addsub_direct
// CHECK: call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})
// CHECK: call <4 x i16> @llvm.haydn.x4sub16s(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})
// CHECK: call void @llvm.haydn.movegpr2sfr(i32 5)
// CHECK: call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})

// Macro cutover parity: AE_* macros that are thin wrappers over __builtin_ae_*.
// Keep this section free of a full #include <haydn_dsp.h> (that header uses
// vector ae_int16x4 aliases). Local macros mirror the dsp cutover.

#define AE_MUL16JS(a) __builtin_ae_mul16js(a)
#define AE_CONJ16S(a) __builtin_ae_conj16s(a)
#define AE_MULAFC16RAS(acc, a, b) __builtin_ae_mulafc16ras(acc, a, b)
#define AE_ADDANDSUBRNG16RAS_S0(a, b) __builtin_ae_addandsubrng16ras_s0(a, b)

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
// CHECK: call <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16> %{{.+}}, <4 x i16> %{{.+}}, <4 x i16> %{{.+}})

ae_int16x4 addsub_macro(ae_int16x4 a, ae_int16x4 b) {
  return AE_ADDANDSUBRNG16RAS_S0(a, b);
}
// CHECK-LABEL: define dso_local i64 @addsub_macro
// CHECK: call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})
// CHECK: call <4 x i16> @llvm.haydn.x4sub16s(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})
// CHECK: call void @llvm.haydn.movegpr2sfr(i32 5)
// CHECK: call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %{{.+}}, <4 x i16> %{{.+}})
