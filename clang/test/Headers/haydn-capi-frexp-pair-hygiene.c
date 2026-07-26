// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding -Werror -pedantic %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm \
// RUN:   -ffreestanding -o - %s | FileCheck %s --check-prefix=IR
//
// REQUIRES: haydn-registered-target
//
// C4.2 / G-CAPI: frexp-pair IMM macros must not capture a caller identifier
// named `r` (base or offset/stride) and must be __extension__-wrapped so
// pedantic C accepts the GNU statement expression. Temp is __haydn_frexp.
//
// Capture modes:
//   - pointer base named `r` (IMM/REG)
//   - enum constant named `r` as ImmArg offset (would type-error if shadowed
//     by the frexp result struct)
//   - variable named `r` as REG stride (silent wrong AGU if shadowed)

#include <haydn.h>

// IR-LABEL: @cb_imm_r_as_base
// Caller pointer `r` must reach the builtin as base, not the frexp temp.
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 2
int64_t cb_imm_r_as_base(const void *p) {
  const void *r = p;
  haydn_cb_ld_t out = haydn_ldw_cb_imm(r, 0, 2);
  return out.data;
}

// IR-LABEL: @cb_imm_r_as_offset
// Enum constant `r` is an ICE for ImmArg; if the stmt-expr local shadows it,
// Sema would reject (struct used as int). Must lower to stride 1.
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 1
int64_t cb_imm_r_as_offset(const void *base) {
  enum { r = 1 };
  haydn_cb_ld_t out = haydn_ldw_cb_imm(base, 0, r);
  return out.data;
}

// IR-LABEL: @brev_imm_r_as_offset
// IR: call {{.*}}@llvm.haydn.ldw.brev.imm{{.*}}i32 1
int64_t brev_imm_r_as_offset(const void *base) {
  enum { r = 1 };
  haydn_cb_ld_t out = haydn_ldw_brev_imm(base, r);
  return out.data;
}

// IR-LABEL: @lw_brev_imm_r_as_offset
// IR: call {{.*}}@llvm.haydn.lw.brev.imm{{.*}}i32 1
int lw_brev_imm_r_as_offset(const void *base) {
  enum { r = 1 };
  haydn_sld_t out = haydn_lw_brev_imm(base, r);
  return out.data;
}

// IR-LABEL: @post_imm_r_as_offset
// PairLdWb IMM frexp macros share the same hygiene contract.
// IR: call {{.*}}@llvm.haydn.d.ldw.post.imm{{.*}}i32 4
int64_t post_imm_r_as_offset(const void *base) {
  enum { r = 4 };
  haydn_cb_ld_t out = haydn_d_ldw_post_imm(base, r);
  return out.data;
}

// IR-LABEL: @post_imm_r_as_base
// IR: call {{.*}}@llvm.haydn.d.ldw.post.imm{{.*}}i32 8
int64_t post_imm_r_as_base(const void *p) {
  const void *r = p;
  haydn_cb_ld_t out = haydn_d_ldw_post_imm(r, 8);
  return out.data;
}

// IR-LABEL: @cb_reg_r_as_stride
// REG-stride CB form is a stmt-expr macro (cbr_sel ImmArg); variable `r` as
// stride must not be shadowed by the frexp temp (silent wrong AGU).
// IR: call {{.*}}@llvm.haydn.ldw.cb.reg{{.*}}i32 0
int64_t cb_reg_r_as_stride(const void *base, int stride) {
  int r = stride;
  haydn_cb_ld_t out = haydn_ldw_cb_reg(base, 0, r);
  return out.data;
}
