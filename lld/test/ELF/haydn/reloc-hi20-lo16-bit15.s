# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000 \
# RUN:                         --section-start=.rodata=0x18000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# REGRESSION TEST: HI12/LO20 materialization must reconstruct addresses with
# bit 15 set (CB-76 class / D489 Bundle128).
#
# Bundle128 LUI is HI12 (imm12 → bits[31:20], +0x80000 rounding) paired with
# ADDI32 LO20 (signed 20-bit low). For target_bit15 @ 0x18000:
#   HI12 = (0x18000 + 0x80000) >> 20 = 0
#   LO20 = 0x18000 - 0 = 0x18000 = 98304
#   runtime: (0 << 20) + 98304 = 0x18000
#
# Pre-Bundle128 HI20/LO16 MIPS 16/16 checks (lui 2 / addi -32768) are retired.

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: lui {{.*}}r1, 0
    lui R1, target_bit15

    # CHECK: addi32 {{.*}}r1, {{.*}}r1, 98304
    addi32 R1, R1, target_bit15

    .size _start, .-_start

.section .rodata
.globl target_bit15
target_bit15:
    .long 0xCAFEF00D
    .size target_bit15, .-target_bit15
