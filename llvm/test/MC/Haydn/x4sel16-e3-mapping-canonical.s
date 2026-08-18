# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | llvm-objdump -d --triple=haydn-unknown-elf - | FileCheck --check-prefix=ROUNDTRIP %s
# REQUIRES: haydn-registered-target
# Role: object — X4SEL16 E3_E1_ALU1 operand-to-field mapping.
#
# v2_1 restamp 2026-08-18: the golden mapping row for X4SEL16_E3_E1_ALU1_RRR
# is now CANONICAL (dest=rtd, src1=rsd1, src2=rsd2, src3=rs — one token per
# position, no permutation), so the v2-era same-class canonicalization branch
# no longer exists. The member binds asm operands straight to fields:
# src2->bit[65:62], src1->bit[61:58], src3->bit[57:54], dest->bit[53:50].
# Bytes below verified field-by-field against that row (d6@src2, d5@src1,
# r7@src3, d4@dest).
#
# Historical discriminator (v2 repaired records): bytes ...52 d9... came from
# the permuted delivery mapping + generator canonicalization; both are gone.
#
# The bundle shape steers x4sel16 onto E3 entry1: s_sw_with_imm's only E3
# placement is E0 (LOADSTORE0), s_lw_with_imm's preferred placement is E2
# (LOAD1), so x4sel16 must take E1, where ALU1 wins the member tie-break.

{ s_sw_with_imm r1, r2, 0; s_lw_with_imm r3, r4, 0; x4sel16 d4, d5, d6, r7 }
# CHECK: { s_sw_with_imm r1, r2, 0; s_lw_with_imm r3, r4, 0; x4sel16 d4, d5, d6, r7 } // encoding: [0xcf,0x6b,0x43,0x00,0x20,0x04,0xd2,0x95,0x71,0x7a,0x08,0x00]
# ROUNDTRIP: cf 6b 43 00 20 04 d2 95 71 7a 08 00 {{.*}}x4sel16{{[^;}]*}}d4,{{[^;}]*}}d5,{{[^;}]*}}d6,{{[^;}]*}}r7
# (This base's disassembler prints logical mnemonics — ld32/st32 for the
# partners — but the x4sel16 operand order is the logical order either way.)
