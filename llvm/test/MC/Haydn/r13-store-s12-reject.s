# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=S12=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=S12
# REQUIRES: haydn-registered-target

# Store S1/S2 FieldSlots have no Format E e1/e2 member. Occupancy keeps the
# residual mask and fills e0 from the generated member; S1/S2 is
# reject-not-migrate. Public asm uses the catalog logical (or st32_post
# alias). Suffix peers are not matcher results.

.ifndef S12
# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: {{.*}}d_sdw_post_imm
# CHECK: {{.*}}c: {{.*}}s_sw_post_imm
# CHECK: {{.*}}18: {{.*}}d_ldw_post_reg
# CHECK: {{.*}}24: {{.*}}d_ldw_cb_imm
# CHECK-NOT: <unknown>
.text
  { d_sdw_post_imm d0, r1, 1 }
  { st32_post r3, r1, 1 }
  { d_ldw_post_reg d0, r1, r2 }
  { d_ldw_cb_imm 0, d0, r1, 1 }
.endif

.ifdef S12
# S12: error: invalid instruction mnemonic
d_sdw_post_imm_s1 d0, r1, 1
d_sdw_post_imm_s2 d0, r1, 1
d_sw_l_with_imm_s2 d0, r1, 0
d_ldw_post_reg_s2 d0, r1, r2
st32_post_s1 r3, r1, 1
st64_post_s1 d0, r1, 1
wbarwua_s1 0, r1, 0
wbarwua_s2 0, r1, 0
d_ldw_cb_imm_s0 0, d0, r1, 0
d_ldw_cb_imm_s1 0, d0, r1, 0
d_ldw_cb_imm_s2 0, d0, r1, 0
d_sdw_cb_imm_s0 0, d0, r1, 0
d_sqhwua_post_s1 d0, 0, r1, r2, 0
d_sqhwua_post_s2 d0, 0, r1, r2, 0
d_stwua_post_s1 d0, 0, r1, r2, 0
d_stwua_post_s2 d0, 0, r1, r2, 0
x2slt32_s1 d0, d1
x2sle32_s1 d0, d1
x2seq32_s1 d0, d1
x4slt16_s1 d0, d1
x4sle16_s1 d0, d1
x4seq16_s1 d0, d1
wfi_s0
.endif
