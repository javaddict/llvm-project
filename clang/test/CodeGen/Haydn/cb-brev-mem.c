// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
// REQUIRES: haydn-registered-target

// REGRESSION: CB / BREV memory intrinsics must not be DCE'd.
// BREV frexp pair model matches CB: loads return {data, new_ptr}.

#include <haydn.h>

// CHECK-LABEL: @cb_load_not_dced
// Frexp CB load: {data, new_ptr}; presence of the call is the DCE check.
// CHECK: call { i64, i32 } @llvm.haydn.ldw.cb.imm
void cb_load_not_dced(int ptr) {
  // ImmArg: cbr_sel + stride must be constants.
  (void)haydn_ldw_cb_imm(ptr, /*cbr_sel=*/0, /*stride=*/1);
}

// CHECK-LABEL: @cb_store_not_dced
// CB store returns updated pointer (i32); presence is the DCE check.
// CHECK: call i32 @llvm.haydn.sdw.cb.imm
void cb_store_not_dced(long long data, int ptr) {
  haydn_sdw_cb_imm(data, ptr, /*cbr_sel=*/0, /*stride=*/1);
}

// CHECK-LABEL: @brev_load_not_dced
// S_LW_BREV frexp pair {i32 data, i32 new_ptr}
// CHECK: call { i32, i32 } @llvm.haydn.lw.brev.imm
void brev_load_not_dced(int ptr) {
  (void)haydn_lw_brev_imm(ptr, 1);
}

// CHECK-LABEL: @brev_d_load_not_dced
// D_LDW_BREV frexp pair {i64 data, i32 new_ptr}
// CHECK: call { i64, i32 } @llvm.haydn.ldw.brev.imm
void brev_d_load_not_dced(int ptr) {
  (void)haydn_ldw_brev_imm(ptr, 1);
}

// CHECK-LABEL: @brev_store_not_dced
// S_SW_BREV: (data, ptr, stride) -> new_ptr
// CHECK: call i32 @llvm.haydn.sw.brev.imm
void brev_store_not_dced(int data, int ptr) {
  (void)haydn_sw_brev_imm(data, ptr, 1);
}

// CHECK-LABEL: @brev_d_store_not_dced
// D_SDW_BREV: (i64 data, ptr, stride) -> new_ptr
// CHECK: call i32 @llvm.haydn.sdw.brev.imm
void brev_d_store_not_dced(long long data, int ptr) {
  (void)haydn_sdw_brev_imm(data, ptr, 1);
}
