// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -O0 -c -o %t.o0.o %s
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -O2 -c -o %t.o2.o %s
// REQUIRES: haydn-registered-target

// C2.1 / G-MEM-INTRIN: public/IR bases and AGU writebacks are pointer-correct
// (void*/const void*/llvm_ptr_ty), not signed int. Frexp .new_ptr is void*.
// Exit: header + IR types + C→object O0/O2.

#include <haydn.h>

// IR-LABEL: @probe_ldw_cb_imm_ptr
// IR: call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr {{%.*}}, i32 0, i32 1)
haydn_cb_ld_t probe_ldw_cb_imm_ptr(const void *base) {
  return haydn_ldw_cb_imm(base, /*cbr_sel=*/0, /*stride=*/1);
}

// IR-LABEL: @probe_sdw_cb_imm_ptr
// IR: call ptr @llvm.haydn.sdw.cb.imm(i64 {{%.*}}, ptr {{%.*}}, i32 0, i32 1)
void *probe_sdw_cb_imm_ptr(long long data, void *base) {
  return haydn_sdw_cb_imm(data, base, /*cbr_sel=*/0, /*stride=*/1);
}

// IR-LABEL: @probe_ldw_brev_imm_ptr
// IR: call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr {{%.*}}, i32 1)
haydn_cb_ld_t probe_ldw_brev_imm_ptr(const void *base) {
  return haydn_ldw_brev_imm(base, /*stride=*/1);
}

// IR-LABEL: @probe_d_ldw_post_imm_ptr
// IR: call { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr {{%.*}}, i32 0)
haydn_cb_ld_t probe_d_ldw_post_imm_ptr(const void *base) {
  return haydn_d_ldw_post_imm(base, 0);
}

// IR-LABEL: @probe_s_lw_post_reg_ptr
// IR: call { i32, ptr } @llvm.haydn.s.lw.post.reg(ptr {{%.*}}, i32 {{%.*}})
haydn_sld_t probe_s_lw_post_reg_ptr(const void *base, int off) {
  return haydn_s_lw_post_reg(base, off);
}

// IR-LABEL: @probe_d_lw_with_imm_ptr
// IR: call i64 @llvm.haydn.d.lw.with.imm(ptr {{%.*}}, i32 0)
long long probe_d_lw_with_imm_ptr(const void *base) {
  return haydn_d_lw_with_imm(base, 0);
}

// IR-LABEL: @probe_d_sdw_post_imm_ptr
// IR: call ptr @llvm.haydn.d.sdw.post.imm(i64 {{%.*}}, ptr {{%.*}}, i32 0)
void *probe_d_sdw_post_imm_ptr(long long data, void *base) {
  return haydn_d_sdw_post_imm(data, base, 0);
}

// IR-LABEL: @probe_s_sw_with_reg_ptr
// IR: call void @llvm.haydn.s.sw.with.reg(i32 {{%.*}}, ptr {{%.*}}, i32 {{%.*}})
void probe_s_sw_with_reg_ptr(int data, void *base, int off) {
  haydn_s_sw_with_reg(data, base, off);
}

// IR-LABEL: @probe_pldwwua_ptr
// IR: call void @llvm.haydn.pldwwua(i32 0, ptr {{%.*}})
void probe_pldwwua_ptr(const void *base) {
  haydn_pldwwua(/*ar_sel=*/0, base);
}

// IR-LABEL: @probe_d_ltwua_post_ptr
// IR: call i64 @llvm.haydn.d.ltwua.post(ptr {{%.*}}, i32 0)
haydn_x2int32 probe_d_ltwua_post_ptr(const void *base) {
  return haydn_d_ltwua_post(base, /*ar=*/0);
}

// Frexp out-arg shape at the builtin level: void ** for new_ptr.
// IR-LABEL: @probe_builtin_frexp_voidpp
// IR: call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr {{%.*}}, i32 0, i32 1)
long long probe_builtin_frexp_voidpp(const void *base, void **np_out) {
  // Keep writeback live so frexp store is not DCE'd at -O2.
  return __builtin_haydn_ldw_cb_imm_pair(np_out, base, 0, 1);
}
