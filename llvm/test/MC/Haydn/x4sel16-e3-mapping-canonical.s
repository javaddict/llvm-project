# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | llvm-objdump -d --triple=haydn-unknown-elf - | FileCheck --check-prefix=ROUNDTRIP %s
# REQUIRES: haydn-registered-target
# Role: object — X4SEL16 E3_E1_ALU1 operand-to-field mapping.
#
# The golden mapping for X4SEL16_E3_E1_ALU1_RRR is permuted against the
# logical syntax (rtd, rsd1, rsd2, rs): field src1 holds rsd2, src2 holds rs,
# src3 holds rsd1. It is the only member whose SAME-CLASS operand order
# permutes against its logical's majority signature, so the bag-by-class
# binding in fillFormatEMemberInst would swap rsd1/rsd2 in the bit image
# unless the member's (ins) order is canonicalized by the generator.
#
# Discriminator, measured on both states of the generated records:
#   * delivery-mapping records (src1 hardwired 0 / class-order binding):
#     bytes ...92 d5... and the round-trip prints "x4sel16 d4, d5, r7, d6".
#   * repaired + canonicalized records: bytes ...52 d9... and the round-trip
#     prints the logical operand order below.
#
# The bundle shape steers x4sel16 onto E3 entry1: s_sw_with_imm's only E3
# placement is E0 (LOADSTORE0), s_lw_with_imm's preferred placement is E2
# (LOAD1), so x4sel16 must take E1, where ALU1 wins the member tie-break.

{ s_sw_with_imm r1, r2, 0; s_lw_with_imm r3, r4, 0; x4sel16 d4, d5, d6, r7 }
# CHECK: { s_sw_with_imm r1, r2, 0; s_lw_with_imm r3, r4, 0; x4sel16 d4, d5, d6, r7 } // encoding: [0xcf,0x6b,0x43,0x00,0x20,0x04,0x52,0xd9,0x71,0x7a,0x08,0x00]
# ROUNDTRIP: cf 6b 43 00 20 04 52 d9 71 7a 08 00 {{.*}}x4sel16{{[^;}]*}}d4,{{[^;}]*}}d5,{{[^;}]*}}d6,{{[^;}]*}}r7
# (This base's disassembler prints logical mnemonics — ld32/st32 for the
# partners — but the x4sel16 operand order is the logical order either way.)
