# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s

# Encoding byte patterns are regenerated whenever the bundle format changes;
# they are a RECORD of the encoder, not an oracle over it — the hand-derived
# oracles are the flex-* files. Currently 12-byte format E parcels.
#
# Historical note, kept because it explains the shape of the file: patterns
# were previously regenerated for the R2-dense Bundle128 cutover
# (AIE two-step _S{0,1,2}_FLEX formats). The asm-printer mnemonic + operand
# round-trip is unchanged; only the encoded byte payload drifted from the
# legacy 8-byte Mode-0 slot-OR parcels to the 16-byte Bundle128 word (s0/s1/s2
# 48-bit windows packed into a single 128-bit LE word). LD/ST, conditional
# branches (BEQ/BNE/BLT/BEQZ/BNEZ), and JAL all migrated to the 16-byte
# Bundle128 format (LD32_S0_FLEX / ST32_S0_FLEX / BEQ_S0_FLEX / JAL_S0_FLEX).
# See / commits 286fc62761a7..b4db878a96de.

#===----------------------------------------------------------------------===
# Test 32-bit R-type encoding (ALU32)
#===----------------------------------------------------------------------===

# CHECK: { add32 r0, r1, r2 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0xe0,0x12,0x20,0x04]
ADD32 R0, R1, R2

# CHECK: { sub32 r3, r4, r5 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0xe0,0x1a,0x86,0x0a]
SUB32 R3, R4, R5

# CHECK: { and32 r6, r7, r8 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0xe0,0x22,0xec,0x10]
AND32 R6, R7, R8

# CHECK: { or32 r9, r10, r11 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0xe0,0x26,0x52,0x17]
OR32 R9, R10, R11

# CHECK: { xor32 r12, r0, r1 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0xe0,0x2a,0x18,0x02]
XOR32 R12, R0, R1

#===----------------------------------------------------------------------===
# Test 32-bit I-type encoding (immediate)
#===----------------------------------------------------------------------===

# CHECK: { addi32 r0, r1, 42 } // encoding: [0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x21,0x54,0x00,0x00,0x00]
ADDI32 R0, R1, 42

# CHECK: { addi32 r2, r3, -100 } // encoding: [0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x65,0x38,0xff,0x1f,0x00]
ADDI32 R2, R3, -100

# CHECK: { andi32 r4, r5, 255 } // encoding: [0x07,0x00,0x00,0x00,0x00,0x00,0x40,0xa8,0xfe,0x01,0x00,0x00]
ANDI32 R4, R5, 255

# CHECK: { ori32 r6, r7, 15 } // encoding: [0x07,0x00,0x00,0x00,0x00,0x00,0x80,0xec,0x1e,0x00,0x00,0x00]
ORI32 R6, R7, 15

# CHECK: { xori32 r8, r9, 7 } // encoding: [0x07,0x00,0x00,0x00,0x00,0x00,0xc0,0x30,0x0f,0x00,0x00,0x00]
XORI32 R8, R9, 7

#===----------------------------------------------------------------------===
# Test shift immediate encoding
#===----------------------------------------------------------------------===

# CHECK: { srli32 r10, r11, 4 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0xa0,0x21,0x74,0x09]
SRLI32 R10, R11, 4

# CHECK: { srai32 r12, r0, 8 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0xa0,0x41,0x18,0x10]
SRAI32 R12, R0, 8

# CHECK: { slli32 r1, r2, 16 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0xa0,0x81,0x42,0x20]
SLLI32 R1, R2, 16

#===----------------------------------------------------------------------===
# Test load/store encoding
#===----------------------------------------------------------------------===

# fixup-restored: scalar LD/ST emit the 16-byte Bundle128 word
# (LD32_S0_FLEX / ST32_S0_FLEX). Mnemonic + operand round-trip is preserved;
# the byte payload is the 16-byte Bundle128 format (s0 LS window packed into
# the 128-bit LE word). The legacy 6-byte WIDE-LS parcel and 4-byte Mode-0 LS
# parcel are retired.
# CHECK: { s_lw_{{[a-z_]*}} r0, r1, 0 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x70,0x1a,0x02,0x00]
s_lw_with_imm R0, R1, 0

# The immediate is an ELEMENT index: S_LW/S_SW are rs + (imm6 << 2),
# so 4 is byte 16 and -1 is byte -4. See ld16-ld8-bundle128-roundtrip.s.
# CHECK: { s_lw_{{[a-z_]*}} r2, r3, 4 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x70,0x5a,0x86,0x00]
s_lw_with_imm R2, R3, 4

# CHECK: { s_sw_{{[a-z_]*}} r4, r5, 0 } // encoding: [0xcf,0x6b,0xa9,0x00,0x40,0x00,0x00,0x00,0x20,0x00,0x00,0x00]
s_sw_with_imm R4, R5, 0

# CHECK: { s_sw_{{[a-z_]*}} r6, r7, -1 } // encoding: [0xcf,0x6b,0xed,0x7e,0x40,0x00,0x00,0x00,0x20,0x00,0x00,0x00]
s_sw_with_imm R6, R7, -1

#===----------------------------------------------------------------------===
# Test branch encoding
#===----------------------------------------------------------------------===

# RI12/I12 branches use WIDE_BranchSImm12[_RI] fixup kinds, not legacy
# BranchSImm16.
#
# The fixup `offset:` and the `+N` in `value:` are the SAME number and it is
# the entry's byte base within the bundle, not a property of the branch —
# 4 for an op in the second entry of a 3-entry bundle, 8 for the third. The
# addend carries it because the fixup is applied relative to the bundle
# start (§ 5.8). They move together or something is wrong; a mismatch
# between them is the shape of the bug that section is about.
# CHECK: { beq r8, r9, target1 } // encoding: [0x8f,0x00,0x00,0x00,0xc0,0x16,0x26,0x00,0x20,0x00,0x00,0x00]
# CHECK-NEXT: fixup A - offset: 4, value: target1+4, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
BEQ R8, R9, target1
target1:

# CHECK: { bne r10, r11, target2 } // encoding: [0x8f,0x00,0x00,0x00,0xc0,0x9e,0x2e,0x00,0x20,0x00,0x00,0x00]
# CHECK-NEXT: fixup A - offset: 4, value: target2+4, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
BNE R10, R11, target2
target2:

# CHECK: { blt r12, r0, target3 } // encoding: [0x8f,0x00,0x00,0x00,0xc0,0x2e,0x03,0x00,0x20,0x00,0x00,0x00]
# CHECK-NEXT: fixup A - offset: 4, value: target3+4, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
BLT R12, R0, target3
target3:

#===----------------------------------------------------------------------===
# Test unconditional branch encoding
#===----------------------------------------------------------------------===

# CHECK: { beqz r1, target4 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0xa0,0x32,0x00,0x00]
# CHECK-NEXT: fixup A - offset: 8, value: target4+8, kind: FIXUP_HAYDN_WIDE_BranchSImm12
BEQZ R1, target4
target4:

# CHECK: { bnez r2, target5 } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0xa0,0x56,0x00,0x00]
# CHECK-NEXT: fixup A - offset: 8, value: target5+8, kind: FIXUP_HAYDN_WIDE_BranchSImm12
BNEZ R2, target5
target5:

#===----------------------------------------------------------------------===
# Test jump and link encoding
#===----------------------------------------------------------------------===

# CHECK: { jal r0, extern_func } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x0f,0x00,0x00,0x20,0x00,0x00,0x00]
# CHECK-NEXT: fixup A - offset: 4, value: extern_func+4, kind: FIXUP_HAYDN_WIDE_CallSImm20
JAL R0, extern_func
