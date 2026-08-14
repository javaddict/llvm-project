# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --image-base=0 --section-start=.text=0x0
# RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t | FileCheck %s
#
# T-MC2: R_HAYDN_LS_IMM patches signed imm6 at parcel bits[33:28], not
# SImm16 (bit 0) and not LO20 (bits[31:50]). nearby @ VA 12 fits [-32,31].
#
# RELOCS: R_HAYDN_LS_IMM nearby
#
# Constant `ld32 r0, r1, 12` encodes 87 43 03 c1 00 00 00 00 00 00 00 00.
# (This base's disassembler prints the logical mnemonic.)
# CHECK: {{.*}}0: 87 43 03 c1 00 00 00 00 00 00 00 00{{.*}}{{ld32|s_lw_with_imm}}{{.*}}r0, r1, 12

.globl _start
_start:
    ld32 r0, r1, nearby
nearby:
    nop
    .size _start, .-_start
