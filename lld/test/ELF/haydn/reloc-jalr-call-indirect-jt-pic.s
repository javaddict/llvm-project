# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r -h %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: llvm-readelf -r %t.o | FileCheck --check-prefix=ELFNUM %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000 --section-start=.rodata=0x20000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck --check-prefix=TEXT %s
# RUN: llvm-objdump -s -j .rodata %t | FileCheck --check-prefix=RODATA %s
#
# Same-artifact pin: one object holds symbolic JALR, call-indirect, and
# PIC/JT label-diff. Symbolic jalr is R_HAYDN_JALRSImm12 (ELF 22,
# rs+imm12, ValueShift=0). Call-indirect jalr rd, rs, 0 bakes a zero imm
# and must not mint a second JALR ELF kind. PIC/JT `.long LBB - JT` is
# R_HAYDN_32_PCREL (FK_Data_4 + IsPCRel). Objects keep EM_HAYDN=259 /
# EF_HAYDN_E96=0x1.
#
# Layout (EncodedBytes=12):
#   0x10000: jalr r1, r2, ext_sym          -> patched imm12 = 24
#   0x1000c: jalr_w lr, r3, 0              -> no reloc
#   0x10018: ext_sym / .Ltarget            -> add32
#   0x20000: .long .Ltarget - jt (x2)      -> 0xffff0018
#
# RELOCS: Machine: 0x103
# RELOCS: Flags [ (0x1)
# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-NEXT:     0x0 R_HAYDN_JALRSImm12 ext_sym
# RELOCS-NEXT:   }
# RELOCS-NEXT:   Section ({{.*}}) .rela.rodata {
# RELOCS-NEXT:     0x0 R_HAYDN_32_PCREL .Ltarget
# RELOCS-NEXT:     0x4 R_HAYDN_32_PCREL .Ltarget
# RELOCS-NEXT:   }
# RELOCS-NEXT: ]
# RELOCS-NOT: R_HAYDN_WIDE_BranchSImm12
# RELOCS-NOT: R_HAYDN_WIDE_BranchSImm12_RI
# RELOCS-NOT: R_HAYDN_GOT
#
# ELF32 r_info low byte is the type: 0x16 = ELF 22.
# ELFNUM: {{[0-9a-fA-F]+}}16 R_HAYDN_JALRSImm12
# ELFNUM: R_HAYDN_32_PCREL
# ELFNUM-NOT: R_HAYDN_WIDE_BranchSImm12
#
# TEXT-LABEL: <_start>:
# TEXT: 10000: {{.*}} jalr{{.*}}r2, 24
# TEXT: 1000c: {{.*}} jalr{{.*}}r3, 0
# TEXT-LABEL: <ext_sym>:
# TEXT: {{.*}} add32
#
# RODATA: Contents of section .rodata:
# RODATA: 20000 1800ffff 1800ffff

.section .text
.globl _start
_start:
    jalr r1, r2, ext_sym
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
