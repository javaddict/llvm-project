// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -O0 -c -o %t.o0.o %s
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -O2 -c -o %t.o2.o %s
// REQUIRES: haydn-registered-target

// C0.1 exit gate: public WITH_* LS builtins must lower C → IR → object at
// -O0 and -O2 with no cannot-select. Focus on d_lw_with_imm (the reported
// miss) plus a representative store and GPR load.

#include <haydn.h>

// IR-LABEL: @probe_d_lw_with_imm
// IR: call i64 @llvm.haydn.d.lw.with.imm
long long probe_d_lw_with_imm(const void *base) {
  return __builtin_haydn_d_lw_with_imm(base, 0);
}

// IR-LABEL: @probe_s_lw_with_imm
// IR: call i32 @llvm.haydn.s.lw.with.imm
int probe_s_lw_with_imm(const void *base) {
  return __builtin_haydn_s_lw_with_imm(base, 4);
}

// IR-LABEL: @probe_d_sdw_with_imm
// IR: call void @llvm.haydn.d.sdw.with.imm
void probe_d_sdw_with_imm(long long data, void *base) {
  __builtin_haydn_d_sdw_with_imm(data, base, 0);
}

// IR-LABEL: @probe_s_sw_with_reg
// IR: call void @llvm.haydn.s.sw.with.reg
void probe_s_sw_with_reg(int data, void *base, int off) {
  __builtin_haydn_s_sw_with_reg(data, base, off);
}

// IR-LABEL: @probe_d_ldw_with_reg
// IR: call i64 @llvm.haydn.d.ldw.with.reg
long long probe_d_ldw_with_reg(const void *base, int off) {
  return __builtin_haydn_d_ldw_with_reg(base, off);
}
