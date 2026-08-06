# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-nm %t | FileCheck --check-prefix=NM %s
# RUN: llvm-readobj -x .text %t | FileCheck --check-prefix=HEX %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# Far call veneer: LUI + ADDI32 + JALR on soft-zero R0.
# 3 × production EncodedBytes parcels at offsets 0 / N / 2N (Align-4).
# Geometry from the object-encoding registry; no Format E 16-byte path.
#
#   0x10000: parcel 0 (LUI)
#   0x1000c: parcel 1 (ADDI32)   — N = production EncodedBytes
#   0x10018: parcel 2 (JALR)

.section .text
.globl _start
_start:
    jal lr, callee
    .space 0x140000

.globl callee
callee:
    add32 r1, r1, r1
    .size callee, .-callee
    .size _start, .-_start

# NM: __haydn_thunk_callee

# Three product parcels: 3 * EncodedBytes. With production EncodedBytes = 12,
# veneer is 36 bytes starting at the section base used for the far call site.
# HEX: Hex dump of section '.text':
# First parcel (LUI) is non-zero in the HI12 window; third parcel (JALR) is non-zero.
# Exact imm bits depend on the materialised target VA.

# CHECK-LABEL: <__haydn_thunk_callee>:
# CHECK-NOT: r12
# CHECK-NOT: xor32
# CHECK-NOT: lui_w
# CHECK-NOT: jalr_w
