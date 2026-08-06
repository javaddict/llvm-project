# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s

// CHECK: { add32 r0, r1, r2 } // encoding: [0x07,0x8b,0x00,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { sub32 r3, r4, r5 } // encoding: [0x07,0xcb,0x30,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { and32 r6, r7, r8 } // encoding: [0x07,0x0b,0x61,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { or32 r9, r10, r11 } // encoding: [0x07,0x2b,0x91,0xba,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { xor32 r12, r0, r1 } // encoding: [0x07,0x4b,0xc1,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { addi32 r0, r1, 42 } // encoding: [0x07,0x0f,0x02,0x01,0x15,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { addi32 r2, r3, -100 } // encoding: [0x07,0x0f,0x22,0x03,0xce,0xff,0x07,0x00,0x00,0x00,0x00,0x00]
// CHECK: { andi32 r4, r5, 255 } // encoding: [0x07,0x0f,0x44,0x85,0x7f,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { ori32 r6, r7, 15 } // encoding: [0x07,0x0f,0x68,0x87,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { xori32 r8, r9, 7 } // encoding: [0x07,0x0f,0x8c,0x89,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { srli32 r10, r11, 4 } // encoding: [0x07,0x06,0xa1,0x0b,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { srai32 r12, r0, 8 } // encoding: [0x07,0x06,0xc2,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { slli32 r1, r2, 16 } // encoding: [0x07,0x06,0x14,0x02,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { ld32 r0, r1, 0 } // encoding: [0x87,0x43,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { ld32 r2, r3, 16 } // encoding: [0x87,0x43,0x23,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { st32 r4, r5, 0 } // encoding: [0x87,0x43,0x4b,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { st32 r6, r7, -4 } // encoding: [0x87,0x43,0x6b,0xc7,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { beq r8, r9, target1 } // encoding: [0x07,0x0d,0x84,0x09,A,0b0000AAAA,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: // fixup A - offset: 0, value: target1, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
// CHECK: target1:
// CHECK: { bne r10, r11, target2 } // encoding: [0x07,0x0d,0xa6,0x0b,A,0b0000AAAA,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: // fixup A - offset: 0, value: target2, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
// CHECK: target2:
// CHECK: { blt r12, r0, target3 } // encoding: [0x07,0x0d,0xca,0x00,A,0b0000AAAA,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: // fixup A - offset: 0, value: target3, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
// CHECK: target3:
// CHECK: { beqz r1, target4 } // encoding: [0x07,0x0a,0x18,0x00,A,0b0000AAAA,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: // fixup A - offset: 0, value: target4, kind: FIXUP_HAYDN_WIDE_BranchSImm12
// CHECK: target4:
// CHECK: { bnez r2, target5 } // encoding: [0x07,0x0a,0x2a,0x00,A,0b0000AAAA,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: // fixup A - offset: 0, value: target5, kind: FIXUP_HAYDN_WIDE_BranchSImm12
// CHECK: target5:
// CHECK: { jal r0, extern_func } // encoding: [0x07,0x0e,0x08,0bA0000000,A,A,0b00000AAA,0x00,0x00,0x00,0x00,0x00]
// CHECK: // fixup A - offset: 0, value: extern_func, kind: FIXUP_HAYDN_WIDE_CallSImm20
# Role: object — Encoding byte patterns for production Format E
# (96-bit / 12-byte EncodedBytes, G-FORMAT-E-96-CUTOVER).

# Asm-printer mnemonic + operand round-trip is unchanged; each parcel is a
# 12-byte Format E word. LD/ST, conditional branches, and JAL use the product
# Format E path. Golden encodings below are live product bytes.


#===----------------------------------------------------------------------===
# Test 32-bit R-type encoding (ALU32)
#===----------------------------------------------------------------------===


ADD32 R0, R1, R2

SUB32 R3, R4, R5

AND32 R6, R7, R8

OR32 R9, R10, R11

XOR32 R12, R0, R1

#===----------------------------------------------------------------------===
# Test 32-bit I-type encoding (immediate)
#===----------------------------------------------------------------------===

ADDI32 R0, R1, 42

ADDI32 R2, R3, -100

ANDI32 R4, R5, 255

ORI32 R6, R7, 15

XORI32 R8, R9, 7

#===----------------------------------------------------------------------===
# Test shift immediate encoding
#===----------------------------------------------------------------------===

SRLI32 R10, R11, 4

SRAI32 R12, R0, 8

SLLI32 R1, R2, 16

#===----------------------------------------------------------------------===
# Test load/store encoding
#===----------------------------------------------------------------------===

# fixup-restored: scalar LD/ST emit the 12-byte Format E word
# (LD32_S0_FLEX / ST32_S0_FLEX). Mnemonic + operand round-trip is preserved;
# the byte payload is the 12-byte Format E format (s0 LS window packed into
# the 128-bit LE word). The legacy 6-byte WIDE-LS parcel and 4-byte Mode-0 LS
# parcel are retired.
LD32 R0, R1, 0

LD32 R2, R3, 16

ST32 R4, R5, 0

ST32 R6, R7, -4

#===----------------------------------------------------------------------===
# Test branch encoding
#===----------------------------------------------------------------------===

# Format E RI12/I12 branches use WIDE_BranchSImm12[_RI] fixup kinds (imm12
# FieldLsb 8/4), not legacy BranchSImm16.
BEQ R8, R9, target1
target1:

BNE R10, R11, target2
target2:

BLT R12, R0, target3
target3:

#===----------------------------------------------------------------------===
# Test unconditional branch encoding
#===----------------------------------------------------------------------===

BEQZ R1, target4
target4:

BNEZ R2, target5
target5:

#===----------------------------------------------------------------------===
# Test jump and link encoding
#===----------------------------------------------------------------------===

JAL R0, extern_func
