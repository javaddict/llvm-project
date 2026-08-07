# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000 --section-start=.data=0x20000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
# RUN: llvm-readobj -x .data %t | FileCheck --check-prefix=DATA %s
#
# REGRESSION: R_HAYDN_32 absolute data + HI12/LO20 address materialization for
# baremetal static link (no dynamic relocs). Bare `lui`/`addi32` with a symbol
# (no %hi/%lo syntax — unimplemented).

# RELOCS:      Relocations [
# RELOCS-DAG:    R_HAYDN_HI12 my_data
# RELOCS-DAG:    R_HAYDN_LO20 my_data
# RELOCS:      ]

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: {{.*}} lui
    LUI R1, my_data
    # CHECK: {{.*}} addi32
    ADDI32 R2, R1, my_data
    # CHECK: {{.*}} s_lw_with_imm
    S_LW_WITH_IMM R3, R2, 0
    # Absolute 32-bit data reloc in .text (same class as reloc-32.s).
    .long my_data

    .size _start, .-_start

.section .data
.globl my_data
my_data:
    .long 0x12345678
    .size my_data, .-my_data

# DATA: Hex dump of section '.data':
# DATA: 78563412
