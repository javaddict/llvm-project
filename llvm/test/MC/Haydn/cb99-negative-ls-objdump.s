# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d %t.o | FileCheck %s
// CHECK: {{.*}} { {{.*}}s_lw_with_imm{{.*}}r1, r10, -4{{.*}} }
// CHECK: {{.*}} { {{.*}}d_ldw_with_imm{{.*}}d1, r10, -8{{.*}} }
// CHECK: {{.*}} { {{.*}}s_sw_with_imm{{.*}}r1, r10, -4{{.*}} }
// CHECK: {{.*}} { {{.*}}s_lbu_with_imm{{.*}}r1, r10, -1{{.*}} }
// CHECK: {{.*}} { {{.*}}s_sb_with_imm{{.*}}r1, r10, -1{{.*}} }
.text
cb99_negative_ls_objdump:
  { s_lw_with_imm r1, r10, -4; nop }
  { d_ldw_with_imm d1, r10, -8; nop }
  { s_sw_with_imm r1, r10, -4; nop }
  { s_lbu_with_imm r1, r10, -1; nop }
  { s_sb_with_imm r1, r10, -1; nop }
  jalr r0, lr, 0
