# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d %t.o | FileCheck %s
// CHECK: {{.*}} { {{.*}}d_sw_l_with_imm{{.*}}d0, r1, -2{{.*}} }
// CHECK: {{.*}} { {{.*}}d_sw_l_with_imm{{.*}}d3, r1, -1{{.*}} }
// CHECK: {{.*}} { {{.*}}d_sw_h_with_imm{{.*}}d0, r2, -2{{.*}} }
// CHECK: {{.*}} { {{.*}}d_sw_l_with_imm{{.*}}d0, r1, 1{{.*}} }
.text
cb22_d_sw_l_simm6:
  { d_sw_l_with_imm d0, r1, -2; nop }
  { d_sw_l_with_imm d3, r1, -1; nop }
  { d_sw_h_with_imm d0, r2, -2; nop }
  { d_sw_l_with_imm d0, r1, 1; nop }
  jalr r0, lr, 0
