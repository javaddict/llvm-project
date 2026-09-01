# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# REQUIRES: haydn-registered-target

# Public d_*wua_post is a one-parcel Format E encode. FieldSlot _S*
# peers are CodeGen-only; the matcher uses the catalog logical. Since the
# CB-151 reshape the logical carries the member wire shape (ar_sel, rtd,
# rs) — stride/dir_sel are not encoded (golden rs = rs+8; direction in
# rs[2:1]) and no keep-map arm runs.

.text
  { d_lqhwua_post 0, d0, r1 }
  { d_ltwua_post 0, d0, r1 }
  { d_sqhwua_post 0, d0, r1 }
  { d_stwua_post 0, d0, r1 }
  { d_lqhwua_post 0, d0, r1; nop; nop }

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: {{.*}}d_lqhwua_post
# CHECK: {{.*}}c: {{.*}}d_ltwua_post
# CHECK: {{.*}}18: {{.*}}d_sqhwua_post
# CHECK: {{.*}}24: {{.*}}d_stwua_post
# CHECK: {{.*}}30: {{.*}}d_lqhwua_post
# CHECK-NOT: <unknown>
# CHECK-NOT: failed to fill
# CHECK-NOT: one-parcel placement failed

# SEC: Name: .text
# SEC: Size: 60
