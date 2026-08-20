# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# REQUIRES: haydn-registered-target

# Public CB / LS-POST logicals encode as one Format E parcel. The
# keep-map drops the extra writeback onto the generated member. Unused
# entries are architectural NOP pads (full-slot idle). Do not split
# into sequential two-entry parcels.

.text
  { d_ldw_cb_imm 0, d0, r1, 0 }
  { d_sdw_cb_imm 0, d0, r1, 1 }
  { d_ldw_post_imm d0, r1, 0 }
  { d_sdw_post_imm d0, r1, 4 }
  { d_ldw_cb_imm 0, d0, r1, 0; nop; nop }

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: {{.*}}d_ldw_cb_imm
# CHECK: {{.*}}c: {{.*}}d_sdw_cb_imm
# CHECK: {{.*}}18: {{.*}}d_ldw_post_imm
# CHECK: {{.*}}24: {{.*}}d_sdw_post_imm
# CHECK: {{.*}}30: {{.*}}d_ldw_cb_imm
# CHECK-NOT: <unknown>
# CHECK-NOT: failed to fill
# CHECK-NOT: one-parcel placement failed

# SEC: Name: .text
# SEC: Size: 60
