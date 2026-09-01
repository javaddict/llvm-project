# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000 --section-start=.rodata=0x11008
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s

# R_HAYDN_PC_LO20 (specifier %pc_lo20) is RI20 PC-relative. Linker applies
# R_PC: (S+A-P) & 0xFFFFF at the LO20 window. Never R_HAYDN_LO20 (absolute)
# and never R_HAYDN_32_PCREL (data-word PIC/JT label-diff).
#
# _start @ 0x10000, target_data @ 0x11008:
#   e0 ALU0 @31: (0x11008 - 0x10000) & 0xFFFFF = 0x1008 = 4104
#   e1 ALU1 @65: (0x11008 - 0x1000c) & 0xFFFFF = 0xffc  = 4092
# `{ xor32; addi32 }` places addi32 at E2 e1. Wrong FieldLsb writes the
# e0 window and leaves the executed imm 0.

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS:          R_HAYDN_PC_LO20 target_data
# RELOCS:          R_HAYDN_PC_LO20_E1 target_data
# RELOCS-NOT:      R_HAYDN_LO20
# RELOCS-NOT:      R_HAYDN_32_PCREL
# RELOCS:        }
# RELOCS-NEXT: ]

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: 10000: {{.*}} addi32{{.*}}r1,{{.*}}r1, 4104
    # CHECK: 1000c: {{.*}} addi32{{.*}}r2,{{.*}}r2, 4092
    addi32 r1, r1, %pc_lo20(target_data)
    { xor32 r0, r0, r0; addi32 r2, r2, %pc_lo20(target_data) }

    .size _start, .-_start

.section .rodata
.globl target_data
target_data:
    .long 0xDEADBEEF
    .size target_data, .-target_data
