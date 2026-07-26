// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -O0 -c -o %t.o0.o %s
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -O2 -c -o %t.o2.o %s
// REQUIRES: haydn-registered-target

// C0.2 exit gate: 28 public POST/PRE Golden LS store builtins must lower
// C → IR → object at -O0 and -O2 with no cannot-select. Representative
// set covers DR64 and GPR data, post/pre, imm/reg.

#include <haydn.h>

// IR-LABEL: @probe_d_sdw_post_imm
// IR: call ptr @llvm.haydn.d.sdw.post.imm
void *probe_d_sdw_post_imm(long long data, void *base) {
  return __builtin_haydn_d_sdw_post_imm(data, base, 0);
}

// IR-LABEL: @probe_d_sdw_post_reg
// IR: call ptr @llvm.haydn.d.sdw.post.reg
void *probe_d_sdw_post_reg(long long data, void *base, int off) {
  return __builtin_haydn_d_sdw_post_reg(data, base, off);
}

// IR-LABEL: @probe_d_sdw_pre_imm
// IR: call ptr @llvm.haydn.d.sdw.pre.imm
void *probe_d_sdw_pre_imm(long long data, void *base) {
  return __builtin_haydn_d_sdw_pre_imm(data, base, 1);
}

// IR-LABEL: @probe_d_shw_pre_reg
// IR: call ptr @llvm.haydn.d.shw.pre.reg
void *probe_d_shw_pre_reg(long long data, void *base, int off) {
  return __builtin_haydn_d_shw_pre_reg(data, base, off);
}

// IR-LABEL: @probe_d_sw_h_post_imm
// IR: call ptr @llvm.haydn.d.sw.h.post.imm
void *probe_d_sw_h_post_imm(long long data, void *base) {
  return __builtin_haydn_d_sw_h_post_imm(data, base, 0);
}

// IR-LABEL: @probe_d_sw_l_pre_imm
// IR: call ptr @llvm.haydn.d.sw.l.pre.imm
void *probe_d_sw_l_pre_imm(long long data, void *base) {
  return __builtin_haydn_d_sw_l_pre_imm(data, base, 2);
}

// IR-LABEL: @probe_s_sb_post_imm
// IR: call ptr @llvm.haydn.s.sb.post.imm
void *probe_s_sb_post_imm(int data, void *base) {
  return __builtin_haydn_s_sb_post_imm(data, base, 0);
}

// IR-LABEL: @probe_s_shw_post_reg
// IR: call ptr @llvm.haydn.s.shw.post.reg
void *probe_s_shw_post_reg(int data, void *base, int off) {
  return __builtin_haydn_s_shw_post_reg(data, base, off);
}

// IR-LABEL: @probe_s_sw_pre_imm
// IR: call ptr @llvm.haydn.s.sw.pre.imm
void *probe_s_sw_pre_imm(int data, void *base) {
  return __builtin_haydn_s_sw_pre_imm(data, base, 4);
}

// IR-LABEL: @probe_s_sw_pre_reg
// IR: call ptr @llvm.haydn.s.sw.pre.reg
void *probe_s_sw_pre_reg(int data, void *base, int off) {
  return __builtin_haydn_s_sw_pre_reg(data, base, off);
}
