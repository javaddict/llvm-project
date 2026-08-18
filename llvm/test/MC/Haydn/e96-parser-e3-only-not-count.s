# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s

# Two text entries would pick E2 from count alone. SIN_COS is generated
# E3-only; the parser must not choose E2 from the child count.

.text
  { sin_cos d0, r1, 1; nop }

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: {{.*}}sin_cos
# CHECK-NOT: <unknown>
