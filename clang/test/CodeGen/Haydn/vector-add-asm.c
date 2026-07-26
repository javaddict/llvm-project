// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -mllvm -global-isel-abort=1 -O2 -S -o - %s | FileCheck %s --check-prefix=ASM
// REQUIRES: haydn-registered-target
//
// Golden-lane e2e: C `vec + vec` / `vec - vec` on haydn_x2int32 and
// haydn_x4int16 must:
//   1) Lower to real LLVM vector IR (add/sub <2 x i32> / <4 x i16>)
//   2) Select X2ADD32 / X4ADD16 (and matching SUB) in the Haydn backend
//
// Builtin surface (haydn_x2add32 / haydn_x4add16) is also checked so the
// ExtVector ClangBuiltin path stays wired to the same mnemonics.

#include <haydn_types.h>

//===----------------------------------------------------------------------===//
// Operator form: a + b / a - b  (generic ISD::ADD / ISD::SUB)
//===----------------------------------------------------------------------===//

// IR-LABEL: @op_add_v2i32
// IR: add <2 x i32>
// ASM-LABEL: op_add_v2i32:
// ASM: x2add32
haydn_x2int32 op_add_v2i32(haydn_x2int32 a, haydn_x2int32 b) {
  return a + b;
}

// IR-LABEL: @op_add_v4i16
// IR: add <4 x i16>
// ASM-LABEL: op_add_v4i16:
// ASM: x4add16
haydn_x4int16 op_add_v4i16(haydn_x4int16 a, haydn_x4int16 b) {
  return a + b;
}

// IR-LABEL: @op_sub_v2i32
// IR: sub <2 x i32>
// ASM-LABEL: op_sub_v2i32:
// ASM: x2sub32
haydn_x2int32 op_sub_v2i32(haydn_x2int32 a, haydn_x2int32 b) {
  return a - b;
}

// IR-LABEL: @op_sub_v4i16
// IR: sub <4 x i16>
// ASM-LABEL: op_sub_v4i16:
// ASM: x4sub16
haydn_x4int16 op_sub_v4i16(haydn_x4int16 a, haydn_x4int16 b) {
  return a - b;
}

//===----------------------------------------------------------------------===//
// Builtin form: haydn_x2add32 / haydn_x4add16 (ClangBuiltin → llvm.haydn.*)
//===----------------------------------------------------------------------===//

// IR-LABEL: @bi_add_v2i32
// IR: call <2 x i32> @llvm.haydn.x2add32
// ASM-LABEL: bi_add_v2i32:
// ASM: x2add32
haydn_x2int32 bi_add_v2i32(haydn_x2int32 a, haydn_x2int32 b) {
  return __builtin_haydn_x2add32(a, b);
}

// IR-LABEL: @bi_add_v4i16
// IR: call <4 x i16> @llvm.haydn.x4add16
// ASM-LABEL: bi_add_v4i16:
// ASM: x4add16
haydn_x4int16 bi_add_v4i16(haydn_x4int16 a, haydn_x4int16 b) {
  return __builtin_haydn_x4add16(a, b);
}

// IR-LABEL: @bi_add_v2i32s
// IR: call <2 x i32> @llvm.haydn.x2add32s
// ASM-LABEL: bi_add_v2i32s:
// ASM: x2add32s
haydn_x2int32 bi_add_v2i32s(haydn_x2int32 a, haydn_x2int32 b) {
  return __builtin_haydn_x2add32s(a, b);
}

// IR-LABEL: @bi_add_v4i16s
// IR: call <4 x i16> @llvm.haydn.x4add16s
// ASM-LABEL: bi_add_v4i16s:
// ASM: x4add16s
haydn_x4int16 bi_add_v4i16s(haydn_x4int16 a, haydn_x4int16 b) {
  return __builtin_haydn_x4add16s(a, b);
}
