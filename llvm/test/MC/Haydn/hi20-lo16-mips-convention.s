# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck --check-prefix=ASM %s
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld -m elf32haydn %t.o -o %t \
# RUN:   --section-start=.text=0x10000 --section-start=.rodata=0x18000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck --check-prefix=ELF %s
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
#
# REGRESSION TEST (F07 Bundle128): HI12/LO20 MIPS-style paired materialization.
#
# Bundle128 LUI is HI12 and ADDI32 RI20 is LO20 (not retired parcel HI20/LO16).
# Bare symbol operands on lui/addi32 emit FIXUP_HAYDN_HI12 / FIXUP_HAYDN_LO20
# (asm %hi/%lo modifiers remain a deferred AsmParser nicety — same semantics).
#
# Address with bit 15 set in the low half of a 1MB-ish VA:
# target @ 0x18000
# HI12 = (0x18000 + 0x80000) >> 20 (MIPS-style bias for LO20 sign/range)
# LO20 = residual low field after HI12 reconstruction
# Opcodes must survive link (class). Absolute imm CHECKs are soft;
# the invariant is lui + addi32 still decode post-link.

    .section .text
    .globl _start
    .type _start, @function
_start:
# ASM: { lui{{.*}}r1,
# ASM: FIXUP_HAYDN_HI12
# ASM: addi32{{.*}}r1, r1,
# ASM: FIXUP_HAYDN_LO20
    lui    r1, target_sym
    addi32 r1, r1, target_sym
    .size _start, .-_start

    .section .rodata
    .globl target_sym
    .type target_sym, @object
target_sym:
    .long 0x12345678
    .size target_sym, .-target_sym

# RELOCS: R_HAYDN_HI12 target_sym
# RELOCS: R_HAYDN_LO20 target_sym

# ELF: <_start>:
# ELF: {{.*}} lui{{.*}}r1,
# ELF: {{.*}} addi32{{.*}}r1, r1,
