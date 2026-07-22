// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
// REQUIRES: haydn-registered-target

// REGRESSION TEST: SFR-writing builtins must NOT be marked Const (F19).
//
// Bug (F19 / consolidated-fix-list §2): the SFR compare/conditional-move
// builtins were declared Const, so a compare whose result is unused is DCE'd
// — but the SFR write is the whole point of the instruction.
//
// Golden lanes: public API takes haydn_x2int32 (ExtVector).

#include <haydn.h>

// CHECK-LABEL: @sfr_compare_not_dced
// CHECK: call <2 x i32> @llvm.haydn.x2slt32
void sfr_compare_not_dced(haydn_x2int32 a, haydn_x2int32 b) {
  // Result is intentionally discarded. The SFR side-effect must keep the
  // call live. If this CHECK fails (no call emitted), F19 has regressed.
  (void)haydn_x2slt32(a, b);
}

// CHECK-LABEL: @sfr_conditional_move_not_dced
// CHECK: call <2 x i32> @llvm.haydn.x2movt32
haydn_x2int32 sfr_conditional_move_not_dced(haydn_x2int32 dst,
                                            haydn_x2int32 src) {
  // The move reads SFR; even if the result were dead it must remain.
  return haydn_x2movt32(dst, src);
}
