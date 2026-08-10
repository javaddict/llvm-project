# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000 --section-start=.rodata=0x11008
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# REGRESSION: LUI+ADDI32 materialization uses HI12/LO20 (D489), not RISC-V
# HI20 or legacy HI20/LO16 parcel geometry.
#
# target_data @ 0x11008:
#   HI12 = (0x11008 + 0x80000) >> 20 = 0
#   LO20 = 0x11008
#   lui r1, 0 ; addi32 r1, r1, 69640

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# The offsets are bundle_start + the entry byte base, so they move with the
# packer, not with the value. FORMAT-E-SWITCH-PLAN.md 5.4.
# RELOCS-DAG:      0x8 R_HAYDN_HI12 target_data 0x0
# RELOCS-DAG:      0x12 R_HAYDN_LO20 target_data 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: 10000: {{.*}} lui{{.*}}r1, 0
    lui R1, target_data

    # CHECK: 1000c: {{.*}} addi32{{.*}}r1,{{.*}}r1, 69640
    addi32 R1, R1, target_data

    .size _start, .-_start

.section .rodata
.globl target_data
target_data:
    .long 0xDEADBEEF
    .size target_data, .-target_data
