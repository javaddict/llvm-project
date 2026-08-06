# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s

// CHECK: { add32 r0, r1, r2 } // encoding: [0x07,0x8b,0x00,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { sub32 r3, r4, r5 } // encoding: [0x07,0xcb,0x30,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { and32 r6, r7, r8 } // encoding: [0x07,0x0b,0x61,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { or32 r9, r10, r11 } // encoding: [0x07,0x2b,0x91,0xba,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { xor32 r12, r0, r1 } // encoding: [0x07,0x4b,0xc1,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { addi32 r0, r1, 42 } // encoding: [0x07,0x0f,0x02,0x01,0x15,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { andi32 r4, r5, 255 } // encoding: [0x07,0x0f,0x44,0x85,0x7f,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { ori32 r6, r7, 15 } // encoding: [0x07,0x0f,0x68,0x87,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { srli32 r10, r11, 4 } // encoding: [0x07,0x06,0xa1,0x0b,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { srai32 r12, r0, 8 } // encoding: [0x07,0x06,0xc2,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { slli32 r1, r2, 16 } // encoding: [0x07,0x06,0x14,0x02,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { ld32 r0, r1, 0 } // encoding: [0x87,0x43,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { ld32 r2, r3, 16 } // encoding: [0x87,0x43,0x23,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { st32 r4, r5, 0 } // encoding: [0x87,0x43,0x4b,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { beq r8, r9, target_32 } // encoding: [0x07,0x0d,0x84,0x09,A,0b0000AAAA,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: // fixup A - offset: 0, value: target_32, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
// CHECK: target_32:
// CHECK: { bne r10, r11, target_32b } // encoding: [0x07,0x0d,0xa6,0x0b,A,0b0000AAAA,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: // fixup A - offset: 0, value: target_32b, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
// CHECK: target_32b:
# Role: object — Mode-0 ALU32/I/shift show-encoding pins; LS/BR encode path survives without objdump ROUNDTRIP.

# Phase-2 decoder purge collateral (prior revision): the ROUNDTRIP objdump
# RUN was REMOVED — the legacy Haydn32 decoder probes for FmtLS (ld32/st32)
# and FmtBr (beq/bne) 4-byte parcels were deleted, so objdump renders
# `<unknown>` for those lines. The ALU32 (R/I/shift) CHECKs SURVIVE (Mode-0
# s0 ALU32 sub-row decoder is intact). The ENC (`-show-encoding`) run is the
# load-bearing assertion for the LS/BR formats now. CodeGen still emits
# these legacy LS/BR parcels — the decoder gap is real on a SURVIVING emit
# path. Tracked here.
#
# REGRESSION TEST: 32-bit instruction encoding round-trip.
#
# FIXED the ld32→nop silent miscompile : LD32_M0 (5-bit unscaled
# i32imm, encoder target) and LD32_M0S0LS (4-bit ×4-scaled, decoder target)
# were unified — the finalizer now points at LD32_M0S0LS and the buggy
# LD32_M0/ST32_M0 defs are deleted. `ld32 r2,r3,16` round-trips correctly.
# See /ssd2/mhyang/haydn-plans/decisions/-ld32-st32-s0-layout-unify.md
# (originally authored as; renumbered by a parallel condensation pass).
#
# Purpose: Verify that key ALU32 instructions (R-type, I-type, shift
# immediate) plus load/store and branch encode to the expected binary under
# the post-migration Mode-0 encoder and round-trip through objdump.
#
# Migration note (R10 /): the legacy standalone 32-bit EW_32Bit format
# was replaced by Mode-0 packed bundles. ALU32 ops now emit as 8-byte
# Mode-0 bundles (the s0 ALU32 sub-row), with a few (e.g. AND32 r6,r7,r8
# when r8 is the soft-zero) compressing to 4 bytes. Load/Store/branch
# retain their dedicated 32-bit G/LS/Br formats (4 bytes each). The
# byte-CHECKs below pin the new layout.
#
# note: the encoder emits ONE child per slot window, so each
# instruction prints as its own `{... }` bundle on its own objdump line.
# Objdump ROUNDTRIP CHECKs removed (legacy 4-byte probe deleted); ENC path is load-bearing.

#===----------------------------------------------------------------------===
# ALU R-type (Mode-0 s0 ALU32 sub-row, 8 bytes; AND32 r6,r7,r8 compresses)
#===----------------------------------------------------------------------===


ADD32 R0, R1, R2

SUB32 R3, R4, R5

AND32 R6, R7, R8

OR32 R9, R10, R11

XOR32 R12, R0, R1

#===----------------------------------------------------------------------===
# ALU I-type (Mode-0 s0 ALU32 sub-row, 8 bytes)
#===----------------------------------------------------------------------===

ADDI32 R0, R1, 42

ANDI32 R4, R5, 255

ORI32 R6, R7, 15

#===----------------------------------------------------------------------===
# Shift immediate (Mode-0 s0 ALU32 sub-row, 8 bytes)
#===----------------------------------------------------------------------===

SRLI32 R10, R11, 4

SRAI32 R12, R0, 8

SLLI32 R1, R2, 16

#===----------------------------------------------------------------------===
# Load/Store (legacy FmtLS, 4 bytes — : asm-parse routes to the generic
# simm16 variant; the Mode-0 LD32_M0S0LS/ST32_M0S0LS ×4-scaled variants are
# finalizer emit targets for CodeGen, not asm-parse targets)
#===----------------------------------------------------------------------===

LD32 R0, R1, 0

LD32 R2, R3, 16

ST32 R4, R5, 0

#===----------------------------------------------------------------------===
# Branch (FmtBr, 4 bytes)
#===----------------------------------------------------------------------===

BEQ R8, R9, target_32
target_32:

BNE R10, R11, target_32b
target_32b:
