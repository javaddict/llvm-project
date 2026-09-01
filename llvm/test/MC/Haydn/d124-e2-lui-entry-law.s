# REQUIRES: haydn-registered-target
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld -m elf32haydn %t.o -o %t \
# RUN:   --section-start=.text=0x10000 --section-start=.rodata=0xa2004
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t \
# RUN:   | FileCheck --check-prefix=ELF %s

# REGRESSION TEST: D1.24 (subsumed by D1.17) — the E2 HI12 entry-
# discrimination hole is closed by structure, not by sniff guessing.
#
# Golden law (format_e_bit_layout_v2_2.json entry_num_0.entry1): E2 e1
# hosts only RI20/I32 at ALU1, R/RR/RRR at MAC1, and AR/RI6/RR at LOAD1.
# There is NO I12 or I8 member at E2 e1 — a symbolic LUI cannot be placed
# there at all (no member, no fixup), so no E2-e1 HI12/CSR window can ever
# be patched. The pre-D1.17 worry — a symbolic LUI at E2 e1 patching the
# e0 tail at bits[43:32] — is unreachable by absence, and the producer
# fails closed (qualifyFixupKindForEntry fatal) if a future golden admits
# such a site without minting a qualified twin.
#
# The reachable neighbor is what this test pins: `{ xor32; lui }` commits
# xor32@e1 ALU1 R + lui@e0 — E2 e1 has no I12, so LUI lands on the
# generated E2 e0 ALU0 I12 member... but E2 e1 ALU1 only carries RI20/I32,
# and unit cover picks the E3 row instead: the committed parcel is E3 with
# lui@e0 ALU2 → R_HAYDN_HI12_E3E0_ALU2 (window 21). HI12 of 0xa2004 =
# (0xa2004 + 0x80000) >> 20 = 1.

# RELOCS:      Relocations [
# RELOCS:        0x0 R_HAYDN_HI12_E3E0_ALU2 high_sym 0x0
# RELOCS:      ]

# ELF: <_start>:
# ELF: lui{{.*}} r3, 1

    .section .text
    .globl _start
    .type _start, @function
_start:
    { xor32 r1, r1, r1; lui r3, %hi12(high_sym) }
    .size _start, .-_start

    .section .rodata
    .globl high_sym
    .type high_sym, @object
high_sym:
    .long 0
    .size high_sym, 4
