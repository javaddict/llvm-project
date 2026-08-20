# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: llvm-readelf -r %t.o | FileCheck --check-prefix=ELFNUM %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# Symbolic jalr mints R_HAYDN_JALRSImm12 (ELF 22), never aliases
# R_HAYDN_WIDE_BranchSImm12_RI. Linker applies the parcel-relative R_PC
# convention (ValueShift=0); execution stays rs+imm12. Same-file global
# target at the next parcel so the patched imm12 is 12.
#
# RELOCS: R_HAYDN_JALRSImm12 ext_sym
# RELOCS-NOT: R_HAYDN_WIDE_BranchSImm12
# RELOCS-NOT: R_HAYDN_WIDE_BranchSImm12_RI
#
# ELF32 r_info low byte is the type: 0x16 = ELF 22.
# ELFNUM: {{[0-9a-fA-F]+}}16 R_HAYDN_JALRSImm12
#
# CHECK-LABEL: <_start>:
# CHECK: 10000: {{.*}} jalr{{.*}}r2, 12
# CHECK-LABEL: <ext_sym>:
# CHECK: {{.*}} add32

.section .text
.globl _start
_start:
    jalr r1, r2, ext_sym
    .size _start, .-_start

.globl ext_sym
ext_sym:
    { add32 r0, r0, r0 }
    .size ext_sym, .-ext_sym
