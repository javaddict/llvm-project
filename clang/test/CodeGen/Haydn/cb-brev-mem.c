// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
// REQUIRES: haydn-registered-target

// REGRESSION TEST: CB / BREV memory intrinsics must not be DCE'd or marked
// IntrNoMem (F20).
//
// Bug (F20 / consolidated-fix-list §2): the circular-buffer and bit-reversed
// load/store intrinsics (ldw_cb_imm, ldw_cb_reg, sdw_cb_imm, sdw_cb_reg,
// ldw_brev_imm, ldw_brev_reg, lw_brev_imm, lw_brev_reg, sdw_brev_imm,
// sdw_brev_reg, sw_brev_imm, sw_brev_reg) were declared IntrNoMem in
// IntrinsicsHaydn.td. IntrNoMem tells the optimizer the call does not touch
// memory, so a load whose result is unused is DCE'd, and stores can be
// reordered with surrounding memory ops — silently breaking FFT/circular-
// buffer kernels.
//
// The fix changes loads to IntrReadMem and stores to IntrWriteMem so
// MemorySSA / MemoryDependence see the access.
//
// Test design: call a CB load and discard the result; call a CB store with
// no subsequent read. If the intrinsics regress to IntrNoMem, both calls
// vanish from the IR.

#include <haydn_intrin.h>

// CHECK-LABEL: @cb_load_not_dced
// CHECK: call i64 @llvm.haydn.ldw.cb.imm
void cb_load_not_dced(int ptr, int cbr_sel) {
  // Result discarded. IntrReadMem keeps the call live.
  (void)__haydn_ldw_cb_imm(ptr, cbr_sel, 1);
}

// CHECK-LABEL: @cb_store_not_dced
// CHECK: call void @llvm.haydn.sdw.cb.imm
void cb_store_not_dced(long long data, int ptr, int cbr_sel) {
  // No subsequent read. IntrWriteMem keeps the call live.
  __haydn_sdw_cb_imm(data, ptr, cbr_sel, 1);
}

// CHECK-LABEL: @brev_load_not_dced
// CHECK: call i32 @llvm.haydn.lw.brev.imm
void brev_load_not_dced(int ptr) {
  (void)__haydn_lw_brev_imm(ptr, 1);
}

// CHECK-LABEL: @brev_store_not_dced
// CHECK: call i32 @llvm.haydn.sw.brev.imm
void brev_store_not_dced(int ptr) {
  (void)__haydn_sw_brev_imm(ptr, 1);
}
