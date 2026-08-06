# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

# Role: object — CSRR / CSRW parse and print across CSR address range.

#===----------------------------------------------------------------------===
# CSR read
#===----------------------------------------------------------------------===

CSRR R0, 0
# CHECK: csrr r0, 0

CSRR R1, 1
# CHECK: csrr r1, 1

CSRR R2, 255
# CHECK: csrr r2, 255

#===----------------------------------------------------------------------===
# CSR write
#===----------------------------------------------------------------------===

CSRW 0, R3
# CHECK: csrw 0, r3

CSRW 10, R4
# CHECK: csrw 10, r4

CSRW 128, R5
# CHECK: csrw 128, r5
