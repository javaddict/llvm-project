# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# REQUIRES: haydn-registered-target

# Standalone one-parcel place for public logicals whose member shape is
# not positional (AR-UA POST extra rs2/dir_sel, CB writeback, 0-op HINT).
# NOP pad is CompletionState, not a second parcel. Refuse sequential E2
# singleton split. Suffix _S* is occupancy recovery, not a matcher.

.text
  { d_lqhwua_post d0, 0, r1, r2, 0 }
  { d_lqhwua_post d0, 0, r1, r2, 0; nop; nop }
  { d_ldw_cb_imm 0, d0, r1, 1 }
  { d_ldw_cb_imm 0, d0, r1, 1; nop; nop }
  { wfi }
  { wfi; nop; nop }

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: {{.*}}d_lqhwua_post
# CHECK: {{.*}}c: {{.*}}d_lqhwua_post
# CHECK: {{.*}}18: {{.*}}d_ldw_cb_imm
# CHECK: {{.*}}24: {{.*}}d_ldw_cb_imm
# CHECK: {{.*}}30: {{.*}}wfi
# CHECK: {{.*}}3c: {{.*}}wfi
# CHECK-NOT: <unknown>
# CHECK-NOT: one-parcel placement failed
# CHECK-NOT: sequential E2 singleton split

# Six parcels × 12 bytes.
# SEC: Name: .text
# SEC: Size: 72
