# REQUIRES: haydn-registered-target
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld -m elf32haydn %t.o -o %t \
# RUN:   --section-start=.text=0x10000 --section-start=.rodata=0x20000
# RUN: llvm-objdump -s -j .rodata %t | FileCheck --check-prefix=LINKED %s

# Role: object — PIC/JT EK_LabelDifference32 is R_HAYDN_32_PCREL, not R_HAYDN_32.
#
# REGRESSION TEST: `.long LBB - JT` (same form AsmPrinter emits for PIC jump
# tables) must mint R_HAYDN_32_PCREL. ELFObjectWriter folds the subtract into
# IsPCRel + addend. Absolute R_HAYDN_32 would write LBB, not LBB-JT, and
# G_BRJT's +JTBase reconstruction would jalr to a wild PC.
#
# Layout: .text at 0x10000 (two 12-byte parcels), .rodata JT at 0x20000.
# target = 0x1000c, jt = 0x20000 → field = 0x1000c - 0x20000 = 0xffff000c.
# Both entries share that difference (second addend is +4 so S+A-P matches).

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.rodata {
# RELOCS-NEXT:     0x0 R_HAYDN_32_PCREL .Ltarget
# RELOCS-NEXT:     0x4 R_HAYDN_32_PCREL .Ltarget
# RELOCS-NEXT:   }
# RELOCS-NEXT: ]
# RELOCS-NOT: R_HAYDN_32{{ }}

# LINKED: Contents of section .rodata:
# LINKED: 20000 0c00ffff 0c00ffff

    .section .text
    .globl _start
    .type _start, @function
_start:
    { xor32 r0, r0, r0 }
.Ltarget:
    { xor32 r0, r0, r0 }
    .size _start, .-_start

    .section .rodata
    .globl jt
    .type jt, @object
jt:
    .long .Ltarget - jt
    .long .Ltarget - jt
    .size jt, .-jt
