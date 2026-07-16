// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
// REQUIRES: haydn-registered-target

// REGRESSION TEST: SFR-writing builtins must NOT be marked Const (F19).
//
// Bug (F19 / consolidated-fix-list §2): the SFR compare/conditional-move
// builtins (x2seq32, x2slt32, x2sle32, x2movf32, x2movt32, x4seq16, x4slt16,
// x4sle16, x4movf16, x4movt16, slt64, sle64, movt64, movf64) were declared
// inside `let Attributes = [NoThrow, Const] in { ... }` in BuiltinsHaydn.td.
// Const tells the optimizer the call is pure, so a compare whose result is
// unused is DCE'd — but the SFR write is the whole point of the instruction.
//
// Test design: call an SFR compare and discard its result. With Const, the
// call is eliminated. With only NoThrow (the fix), the call is preserved
// because it has a side effect. The corresponding LLVM intrinsics carry
// IntrHasSideEffects; the builtin now carries only NoThrow.

#include <haydn_intrin.h>

// CHECK-LABEL: @sfr_compare_not_dced
// CHECK: call <2 x i32> @llvm.haydn.x2slt32
void sfr_compare_not_dced(long long a, long long b) {
  // Result is intentionally discarded. The SFR side-effect must keep the
  // call live. If this CHECK fails (no call emitted), F19 has regressed.
  (void)__haydn_x2slt32(a, b);
}

// CHECK-LABEL: @sfr_conditional_move_not_dced
// CHECK: call <2 x i32> @llvm.haydn.x2movt32
long long sfr_conditional_move_not_dced(long long dst, long long src) {
  // The move reads SFR; even if the result were dead it must remain.
  return __haydn_x2movt32(dst, src);
}
