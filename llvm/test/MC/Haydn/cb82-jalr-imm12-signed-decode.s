# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s

# Role: object — Format E JALR imm12 rs-relative offset MUST decode as signed.

jalr lr, r2, -100
# CHECK-LABEL: Disassembly of section .text:
# CHECK: {{.*}}0: { nop; jalr lr, r2, -100 }

jalr lr, r2, -4
# CHECK: c: { nop; jalr lr, r2, -4 }

jalr lr, r2, 0
# CHECK: {{.*}}18: { nop; jalr lr, r2, 0 }

jalr lr, r2, 100
# CHECK: {{.*}}24: { nop; jalr lr, r2, 100 }

# CHECK-NOT: 3996
# CHECK-NOT: 4092
