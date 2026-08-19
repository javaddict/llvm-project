# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
#
# M23: external symbolic jalr mints R_HAYDN_JALRSImm12 (ELF 22), never
# aliases R_HAYDN_WIDE_BranchSImm12_RI.
#
# RELOCS: R_HAYDN_JALRSImm12 ext_sym
# RELOCS-NOT: R_HAYDN_WIDE_BranchSImm12

.globl _start
_start:
    jalr r1, r2, ext_sym
    .size _start, .-_start
