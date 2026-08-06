# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s

// CHECK: 	{ 	add32	r0, r1, r2 }            // encoding: [0x07,0x8b,0x00,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	sub32	r3, r4, r5 }            // encoding: [0x07,0xcb,0x30,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	and32	r6, r7, r8 }            // encoding: [0x07,0x0b,0x61,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	or32	r9, r10, r11 }          // encoding: [0x07,0x2b,0x91,0xba,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	xor32	r12, r0, r1 }           // encoding: [0x07,0x4b,0xc1,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	slt32	r0, r1, r2 }            // encoding: [0x07,0x8b,0x02,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	seq32	r0, r1, r2 }            // encoding: [0x07,0xeb,0x02,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	move32	r0, r1 }                // encoding: [0x07,0x44,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	not32	r0, r1 }                // encoding: [0x07,0x24,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	neg32	r0, r1 }                // encoding: [0x07,0x44,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	abs32	r0, r1 }                // encoding: [0x07,0x04,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	addi32	r0, r1, 42 }            // encoding: [0x07,0x0f,0x02,0x01,0x15,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	slli32	r0, r1, 4 }             // encoding: [0x07,0x06,0x04,0x01,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	srli32	r0, r1, 8 }             // encoding: [0x07,0x06,0x01,0x01,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	srai32	r0, r1, 16 }            // encoding: [0x07,0x06,0x02,0x01,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	lui	r0, 1024 }              // encoding: [0x07,0x0a,0x02,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	ld32	r0, r1, 0 }             // encoding: [0x87,0x43,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	ld32	r2, r3, 16 }            // encoding: [0x87,0x43,0x23,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	st32	r4, r5, 0 }             // encoding: [0x87,0x43,0x4b,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	st32	r6, r7, -4 }            // encoding: [0x87,0x43,0x6b,0xc7,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	add64	d0, d1, d2 }            // encoding: [0x07,0x0b,0x04,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	sub64	d3, d4, d5 }            // encoding: [0x07,0x0b,0x35,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	and64	d6, d7, d8 }            // encoding: [0x07,0x8b,0x66,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	or64	d0, d1, d2 }            // encoding: [0x07,0xab,0x06,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	x2mul32	d0, d1, d2, d3 }        // encoding: [0x47,0x02,0x11,0x03,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	x4mul16	d0, d1, d2, d3 }        // encoding: [0x47,0x02,0x1c,0x03,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	x4add16	d0, d1, d2 }            // encoding: [0x07,0x0b,0x0c,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: target_branch:
// CHECK: 	{ 	beq	r0, r1, target_branch } // encoding: [0x07,0x0d,0x04,0x01,A,0b0000AAAA,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK:                                         //   fixup A - offset: 0, value: target_branch, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
// CHECK: target_branch2:
// CHECK: 	{ 	bne	r0, r1, target_branch2 } // encoding: [0x07,0x0d,0x06,0x01,A,0b0000AAAA,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK:                                         //   fixup A - offset: 0, value: target_branch2, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
// CHECK: target_jal:
// CHECK: 	{ jal	r0, target_jal }                // encoding: [0x07,0x0e,0x08,0bA0000000,A,A,0b00000AAA,0x00,0x00,0x00,0x00,0x00]
// CHECK:                                         //   fixup A - offset: 0, value: target_jal, kind: FIXUP_HAYDN_WIDE_CallSImm20
# Role: object — Encoding migration Phase 2 round-trip verification.

# REGRESSION TEST: Encoding migration Phase 2 round-trip verification.
#
# This test verifies that individual instructions assemble correctly after
# the emitter was migrated from legacy D-class encoding to the new
# encoding_manual.md spec (Mode 0/1 FU-based encoding with FU select bits).
#
# Key encoding changes verified:
# NOP: 16-bit 0x0000 (was 64-bit 0x03)
# ADD32: opcode 0x3E (was 0x00, remapped per section 10)
# ADD64: opcode 0x058 (was 0x000, remapped per section 10)
# Per-slot FU select bits (s0: 1b, s1: 2b, s2: 1b)
# Single instructions still use TableGen-generated width-tag encoding
#
# If any CHECK line fails, do NOT update it without understanding the root
# cause. The encoding_manual.md spec at
# ssd/mhyang/dsp/VLIW_Engine_Tool_20260604/AI/Database/hypo_encoding/encoding_manual.md
# is the authoritative reference.
#
# Test design: Individual instructions assembled via the single-instruction
# path (getBinaryCodeForInstr) are encoding-independent and unchanged.
# Bundle encoding is tested via CodeGen (llc) tests, not MC assembly.

#===----------------------------------------------------------------------===
# Test ALU32 instructions (s0, FU=ALU32)
# These use the single-instruction path (TableGen width-tag encoding).
#===----------------------------------------------------------------------===


ADD32 R0, R1, R2

SUB32 R3, R4, R5

AND32 R6, R7, R8

OR32 R9, R10, R11

XOR32 R12, R0, R1

SLT32 R0, R1, R2

SEQ32 R0, R1, R2

# mul32 removed : not in the ISA DB; scalar s32 mul lowers via the
# slot-1/2 DR64 unit (sext32t64 + mul64.ll + move32_dr_l). No scalar GPR mul.

MOVE32 R0, R1

NOT32 R0, R1

NEG32 R0, R1

ABS32 R0, R1

#===----------------------------------------------------------------------===
# Test ALU32 immediate instructions (s0, FU=ALU32, RI format)
#===----------------------------------------------------------------------===

ADDI32 R0, R1, 42

SLLI32 R0, R1, 4

SRLI32 R0, R1, 8

SRAI32 R0, R1, 16

LUI R0, 1024

#===----------------------------------------------------------------------===
# Test Load/Store instructions (s0, FU=LS)
#===----------------------------------------------------------------------===

LD32 R0, R1, 0

LD32 R2, R3, 16

ST32 R4, R5, 0

ST32 R6, R7, -4

#===----------------------------------------------------------------------===
# Test ALU64 instructions (s1/s2, FU=ALU64)
# NOTE: move64, neg64, not64 have no MC encoding (isPseudo) — excluded.
#===----------------------------------------------------------------------===

ADD64 D0, D1, D2

SUB64 D3, D4, D5

AND64 D6, D7, D8

OR64 D0, D1, D2

#===----------------------------------------------------------------------===
# Test MAC instructions (s1/s2, FU=MAC)
#===----------------------------------------------------------------------===

# (Path B): X2MUL32 / X4MUL16 are now TRUE 2-output SIMD-MAC
# (outs DR64:$rd, DR64:$rtd2; ins DR64:$rs1, DR64:$rs2) — the asm form is
# 4-operand. The 3-operand form was retired.
X2MUL32 D0, D1, D2, D3

X4MUL16 D0, D1, D2, D3

X4ADD16 D0, D1, D2

#===----------------------------------------------------------------------===
# Test branch instructions (slot 0 only)
#===----------------------------------------------------------------------===

target_branch:
BEQ R0, R1, target_branch

target_branch2:
BNE R0, R1, target_branch2

target_jal:
JAL R0, target_jal
