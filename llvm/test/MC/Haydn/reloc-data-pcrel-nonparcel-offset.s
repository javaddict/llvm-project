# REQUIRES: haydn-registered-target
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld -m elf32haydn %t.o -o %t \
# RUN:   --section-start=.text=0x10000 --section-start=.rodata=0x20000 \
# RUN:   --section-start=.data=0x30000
# RUN: llvm-objdump -s -j .rodata %t | FileCheck --check-prefix=LINKED-RO %s
# RUN: llvm-objdump -s -j .data %t | FileCheck --check-prefix=LINKED-DA %s

# Role: object/link — R_HAYDN_32_PCREL data words at fragment offsets NOT
# congruent to 0 mod 12 evaluate and link as S + A - P with P the word's own
# address (r_offset). D1.28 pin.
#
# REGRESSION PIN (passes before and after the D1.28 deletion of
# HaydnAsmBackend::evaluateFixup): FK_Data_4 data fixups are created PCRel
# false and nothing sets the flag, so the old Abs % Parcel seeding never fired
# on them. The pin freezes the law so the class cannot re-emerge: any future
# PCRel-flagged data kind (Data32PCRel / FIXUP_HAYDN_32_PCREL row is
# IsPCRel=true) routed through a backend hook that re-bases Value would
# corrupt these words by up to Parcel-1 bytes. Default MC evaluation is
# S + C - Abs with no Haydn perturbation.
#
# Layout: .text at 0x10000 (four 12-byte parcels; targets 0x1000c / 0x10018 /
# 0x10024 are DISTINCT so a wrong-place bug cannot alias to equal bytes),
# .rodata at 0x20000, .data at 0x30000.
#   r_offset 0x04 (04 mod 12 = 4): S+A-P = 0x1000c + 4 - 0x20004 = 0xffff000c
#   r_offset 0x10 (16 mod 12 = 4): S+A-P = 0x10018 + 0x10 - 0x20010 = 0xffff0018
#   r_offset 0x1c (28 mod 12 = 4): S+A-P = 0x10024 + 0x1c - 0x2001c = 0xffff0024
# .data arm is fully resolved at assembly (same-section label diff): the
# addend +k and P +k cancel, so `.long lab - .` pins exact S - Abs = -4 / -16.

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.rodata {
# RELOCS-NEXT:     0x4 R_HAYDN_32_PCREL .Ltarget1 0x4
# RELOCS-NEXT:     0x10 R_HAYDN_32_PCREL .Ltarget2 0x10
# RELOCS-NEXT:     0x1C R_HAYDN_32_PCREL .Ltarget3 0x1C
# RELOCS-NEXT:   }
# RELOCS-NEXT: ]
# No instruction-parcel content carries a relocation: only .rela.rodata
# exists (no .rela.text, no .rela.data — the .data words are resolved).
# RELOCS-NOT: R_HAYDN_32{{ }}

# LINKED-RO: Contents of section .rodata:
# LINKED-RO: 20000 11111111 0c00ffff 22222222 33333333
# LINKED-RO: 20010 1800ffff 44444444 55555555 2400ffff

# LINKED-DA: Contents of section .data:
# LINKED-DA: 30000 77777777 fcffffff 88888888 99999999
# LINKED-DA: 30010 f0ffffff

    .section .text
    .globl _start
    .type _start, @function
_start:
    { xor32 r0, r0, r0 }
.Ltarget1:
    { xor32 r0, r0, r0 }
.Ltarget2:
    { xor32 r0, r0, r0 }
.Ltarget3:
    { xor32 r0, r0, r0 }
    .size _start, .-_start

    .section .rodata
    .globl jt
    .type jt, @object
jt:
    .long 0x11111111
    .long .Ltarget1 - jt
    .long 0x22222222
    .long 0x33333333
    .long .Ltarget2 - jt
    .long 0x44444444
    .long 0x55555555
    .long .Ltarget3 - jt
    .size jt, .-jt

    .section .data
lab:
    .long 0x77777777
    .long lab - .
    .long 0x88888888
    .long 0x99999999
    .long lab - .
