# REQUIRES: haydn
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf --defsym=SYMJALR=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=SYMJALR %s
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r -h %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: llvm-readelf -r %t.o | FileCheck --check-prefix=ELFNUM %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000 --section-start=.rodata=0x20000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck --check-prefix=TEXT %s
# RUN: llvm-objdump -s -j .rodata %t | FileCheck --check-prefix=RODATA %s
#
# ISA-69: symbolic jalr is refuse-at-assemble. This file keeps
# call-indirect jalr_w lr, r3, 0 (no JALR reloc) and PIC/JT
# `.long LBB - JT` as R_HAYDN_32_PCREL. Objects keep EM_HAYDN=259 /
# EF_HAYDN_E96=0x1.
#
# Layout (EncodedBytes=12):
#   0x10000: jalr_w lr, r3, 0              -> no reloc
#   0x1000c: ext_sym / .Ltarget            -> add32
#   0x20000: .long .Ltarget - jt (x2)      -> 0xffff000c
#
# SYMJALR: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# SYMJALR: refusing silent PC-relative R_HAYDN_JALRSImm12
#
# RELOCS: Machine: 0x103
# RELOCS: Flags [ (0x1)
# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.rodata {
# RELOCS-NEXT:     0x0 R_HAYDN_32_PCREL .Ltarget
# RELOCS-NEXT:     0x4 R_HAYDN_32_PCREL .Ltarget
# RELOCS-NEXT:   }
# RELOCS-NEXT: ]
# RELOCS-NOT: R_HAYDN_JALRSImm12
# RELOCS-NOT: R_HAYDN_WIDE_BranchSImm12
# RELOCS-NOT: R_HAYDN_GOT
#
# ELFNUM: R_HAYDN_32_PCREL
# ELFNUM-NOT: R_HAYDN_JALRSImm12
# ELFNUM-NOT: R_HAYDN_WIDE_BranchSImm12
#
# TEXT-LABEL: <_start>:
# TEXT: 10000: {{.*}} jalr{{.*}}r3, 0
# TEXT-LABEL: <ext_sym>:
# TEXT: {{.*}} add32
#
# RODATA: Contents of section .rodata:
# RODATA: 20000 0c00ffff 0c00ffff

.ifdef SYMJALR
    jalr r1, r2, ext_sym
.endif

.section .text
.globl _start
_start:
    jalr_w lr, r3, 0
    .size _start, .-_start

.globl ext_sym
ext_sym:
.Ltarget:
    { add32 r0, r0, r0 }
    .size ext_sym, .-ext_sym

.section .rodata
.globl jt
jt:
    .long .Ltarget - jt
    .long .Ltarget - jt
    .size jt, .-jt
