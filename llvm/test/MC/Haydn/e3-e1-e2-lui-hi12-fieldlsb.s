# REQUIRES: haydn-registered-target
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld -m elf32haydn %t.o -o %t \
# RUN:   --section-start=.text=0x10000 --section-start=.rodata=0xa2004
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t \
# RUN:   | FileCheck --check-prefix=ELF %s

# Role: object — HI12 FieldLsb follows the committed LUI I12 window at every
# generated E3 entry (e0/e1/e2), not only E3 e0 (e3-lui-hi12-fieldlsb.s).
#
# REGRESSION TEST: E3 e1/e2 LUI %hi12 must patch golden I12 bits.
#
# Table FieldLsb=32 / NBytes=8 is E2 e0 authority. E3 e1 ALU1/ALU0 imm sits
# at abs 54 (needs bits past 63); e2 ALU2 @83 / ALU0 @81. A 3-entry bundle
# is high-first (`{ a; b; c }` = e2,e1,e0 — cb142-e96-entry-position-roundtrip.s),
# so the three textual orders cover the three generated LUI members.
# Constant %hi12(0xa2004) pins MC applyFixup; the linked symbol pins lld
# relocate. Both share resolveFieldLsb. HI12 = (0xa2004 + 0x80000) >> 20 = 1.
# Wrong LSB → executed imm 0x800 = 2048.
#
# D1.17: NOP-only siblings keep these parcels on the E2 e0 ALU0 member
# (header 0x07; base kind R_HAYDN_HI12, default window 32). The E3
# qualified twins (E3E0_ALU2/E3E1/E3E2_*) need a real non-NOP neighbor —
# pinned in d117-hi12-mixed-parcel-typed-window.s; the opc-pinned sniff
# arms are pinned in HaydnRelocLayoutTest.

# RELOCS:      Relocations [
# RELOCS-COUNT-3: R_HAYDN_HI12 high_sym
# RELOCS:      ]

# ELF: <_start>:
# ELF-NOT: lui{{.*}} 2048
# ELF: lui{{.*}} r2, 1
# ELF: lui{{.*}} r3, 1
# ELF: lui{{.*}} r4, 1
# ELF: lui{{.*}} r5, 1
# ELF: lui{{.*}} r6, 1
# ELF: lui{{.*}} r7, 1

    .section .text
    .globl _start
    .type _start, @function
_start:
    { lui r2, %hi12(high_sym); nop; nop }
    { nop; lui r3, %hi12(high_sym); nop }
    { nop; nop; lui r4, %hi12(high_sym) }
    { lui r5, %hi12(0xa2004); nop; nop }
    { nop; lui r6, %hi12(0xa2004); nop }
    { nop; nop; lui r7, %hi12(0xa2004) }
    .size _start, .-_start

    .section .rodata
    .globl high_sym
    .type high_sym, @object
high_sym:
    .long 0
    .size high_sym, 4
