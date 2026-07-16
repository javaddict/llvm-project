# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

#===----------------------------------------------------------------------===
# Test CSR read instructions
#===----------------------------------------------------------------------===

# CHECK-LABEL: csrr r0, 0
CSRR R0, 0

# CHECK-LABEL: csrr r1, 1
CSRR R1, 1

# CHECK-LABEL: csrr r2, 255
CSRR R2, 255

#===----------------------------------------------------------------------===
# Test CSR write instructions
#===----------------------------------------------------------------------===

# CHECK-LABEL: csrw 0, r3
CSRW 0, R3

# CHECK-LABEL: csrw 10, r4
CSRW 10, R4

# CHECK-LABEL: csrw 128, r5
CSRW 128, R5
