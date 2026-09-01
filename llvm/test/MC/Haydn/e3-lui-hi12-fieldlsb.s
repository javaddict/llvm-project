# REQUIRES: haydn-registered-target
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld -m elf32haydn %t.o -o %t \
# RUN:   --section-start=.text=0x10000 --section-start=.rodata=0xa2004
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t \
# RUN:   | FileCheck --check-prefix=ELF %s

# Role: object — HI12 FieldLsb follows the committed LUI I12 window (W25 / F15).
#
# REGRESSION TEST: E3 e0 LUI %hi12 must patch golden I12 bits, not E2 e0 LSB=32.
#
# Bug: post-RA may commit singleton / E3 LUI as LUI_E3_E0_ALU2_I12 (imm
# abs[32:21], LSB=21) or LUI_E3_E0_ALU0_I12 (LSB=23). resolveFieldLsb used
# to fall through to the E2 table FieldLsb=32. lld wrote hi12=1 at bit 32,
# which is only the top bit of the ALU2 field, so the ISS executed
# lui rt, 2048. Materialized addresses became 0x7FFxxxxx. BundleSim
# plat_extras freopen(path, "w", stdout) then failed mode_to_flags (exit 21).
#
# D1.17: NOP-only siblings keep these parcels on the E2 e0 ALU0 member
# (header 0x07; base kind R_HAYDN_HI12, default window 32 — no producer
# sniff involved). The E3 typed twins need a real non-NOP neighbor and are
# pinned in d117-hi12-mixed-parcel-typed-window.s; the opc-pinned sniff
# arms are pinned in HaydnRelocLayoutTest.
#
# Address math (same class as plat_extras .rodata / stdout ≥ 0x80000):
#   high_sym @ 0xa2004
#   HI12 = (0xa2004 + 0x80000) >> 20 = 1
# Wrong LSB → executed imm 0x800 = 2048. If this regresses, ELF shows
# `lui r2, 2048` (or another value ≠ 1) instead of `lui r2, 1`.
#
# Test design: a 3-entry bundle with LUI in textual e0 (then NOP pads)
# forces Format E3 so LUI cannot stay on the E2 e0 ALU0 member. NOP pad
# keeps GPR 2W. Constant %hi12(0xa2004) pins MC applyFixup; the linked
# symbol pins lld relocate. Both consumers share resolveFieldLsb.

# RELOCS:      Relocations [
# RELOCS:        R_HAYDN_HI12 high_sym
# RELOCS:      ]

# ELF: <_start>:
# ELF-NOT: lui{{.*}} 2048
# ELF: lui{{.*}} r2, 1
# ELF: lui{{.*}} r3, 1

    .section .text
    .globl _start
    .type _start, @function
_start:
    { lui r2, %hi12(high_sym); nop; nop }
    { lui r3, %hi12(0xa2004); nop; nop }
    .size _start, .-_start

    .section .rodata
    .globl high_sym
    .type high_sym, @object
high_sym:
    .long 0
    .size high_sym, 4
