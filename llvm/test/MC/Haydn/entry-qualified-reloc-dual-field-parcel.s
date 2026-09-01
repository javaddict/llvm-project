# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t | FileCheck %s --check-prefix=LINK

# Role: object+link — entry-qualified relocations (ELF 24/25) for parcels
# that carry TWO same-kind symbolic RI20 fields (the G004 keep-off defect).
#
# REGRESSION TEST: dual-RI20 parcel. `{ addi32; addi32 }` packs ADDI32 at
# E2 e0 ALU0 and E2 e1 ALU1. Pre-fix, both relocs were base R_HAYDN_LO20
# with r_offset = parcel origin, and the content-sniffed resolveFieldLsb
# patched the e0 window twice / e1 never (t_global g=g*3 linked the store
# base as 0). Post-fix, the e1 member's fixup is retargeted to
# R_HAYDN_LO20_E1 (typed window @65) by encodeSlotSubInst; r_offset stays
# the parcel origin (exact code record).
#
# Layout: _start @0x10000; the dual-ADDI32 parcel is 12 bytes, so
# g0 @0x1000c and g1 @0x10018. LO20 is ABSOLUTE (S), so:
#   g0 field = 0x1000c = 65548, g1 field = 0x10018 = 65560.
# The LINK lines prove BOTH fields carry their own symbol's value — the
# pre-fix shape patched the e0 window twice and linked one symbol's value
# into both adds.

.section .text
.globl _start, g0, g1
_start:
    { addi32 r1, r1, g0; addi32 r2, r2, g1 }
g0:
    { nop; nop }
g1:
    { nop; nop }

# RELOC:      Relocations [
# RELOC-NEXT:   Section ({{.*}}) .rela.text {
# RELOC-NEXT:     0x0 R_HAYDN_LO20 g1 0x0
# RELOC-NEXT:     0x0 R_HAYDN_LO20_E1 g0 0x0
# RELOC-NEXT:   }
# RELOC-NEXT: ]

# Both adds print inside ONE parcel line; match the single line with both
# values so any window swap (pre-fix shape patched one value twice) fails.
# LINK: <_start>:
# LINK-NEXT: 10000: {{.*}}addi32{{.*}}r1, {{.*}}r1, 65548
# LINK-SAME: ; {{.*}}addi32{{.*}}r2, {{.*}}r2, 65560
