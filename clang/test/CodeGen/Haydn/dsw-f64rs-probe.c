// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -O0 -c -o %t.o0.o %s
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -O2 -c -o %t.o2.o %s
// REQUIRES: haydn-registered-target

// ISA-65 exit gate: the four public d_sw_f64rs_* fused round-sat-store
// builtins must lower C → IR → object at -O0 and -O2 with no cannot-select.
// Companion to with-offset-ls-probe.c / post-pre-store-ls-probe.c (the
// builtins are ClangBuiltin auto-mapped, so only IR shape + object exit are
// pinned here; selection itself is pinned by
// llvm/test/CodeGen/Haydn/dsw-f64rs-intrinsics.ll).

#include <haydn.h>

// IR-LABEL: @probe_d_sw_f64rs_post_imm
// IR: call ptr @llvm.haydn.d.sw.f64rs.post.imm
void *probe_d_sw_f64rs_post_imm(long long data, void *base) {
  return __builtin_haydn_d_sw_f64rs_post_imm(data, base, 1);
}

// IR-LABEL: @probe_d_sw_f64rs_post_reg
// IR: call ptr @llvm.haydn.d.sw.f64rs.post.reg
void *probe_d_sw_f64rs_post_reg(long long data, void *base, int off) {
  return __builtin_haydn_d_sw_f64rs_post_reg(data, base, off);
}

// IR-LABEL: @probe_d_sw_f64rs_with_imm
// IR: call void @llvm.haydn.d.sw.f64rs.with.imm
void probe_d_sw_f64rs_with_imm(long long data, void *base) {
  __builtin_haydn_d_sw_f64rs_with_imm(data, base, 2);
}

// IR-LABEL: @probe_d_sw_f64rs_with_reg
// IR: call void @llvm.haydn.d.sw.f64rs.with.reg
void probe_d_sw_f64rs_with_reg(long long data, void *base, int off) {
  __builtin_haydn_d_sw_f64rs_with_reg(data, base, off);
}
