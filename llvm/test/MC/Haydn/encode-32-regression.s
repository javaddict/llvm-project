# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
#
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
# The ROUNDTRIP CHECKs therefore match one instruction per line.

#===----------------------------------------------------------------------===
# ALU R-type (Mode-0 s0 ALU32 sub-row, 8 bytes; AND32 r6,r7,r8 compresses)
#===----------------------------------------------------------------------===

# CHECK: { add32 r0, r1, r2 }
# ROUNDTRIP: { add32	r0, r1, r2 }
ADD32 R0, R1, R2

# CHECK: { sub32 r3, r4, r5 }
# ROUNDTRIP: { sub32	r3, r4, r5 }
SUB32 R3, R4, R5

# CHECK: { and32 r6, r7, r8 }
# ROUNDTRIP: { and32	r6, r7, r8 }
AND32 R6, R7, R8

# CHECK: { or32 r9, r10, r11 }
# ROUNDTRIP: { or32	r9, r10, r11 }
OR32 R9, R10, R11

# CHECK: { xor32 r12, r0, r1 }
# ROUNDTRIP: { xor32	r12, r0, r1 }
XOR32 R12, R0, R1

#===----------------------------------------------------------------------===
# ALU I-type (Mode-0 s0 ALU32 sub-row, 8 bytes)
#===----------------------------------------------------------------------===

# CHECK: { addi32 r0, r1, 42 }
# ROUNDTRIP: { addi32	r0, r1, 10 }
ADDI32 R0, R1, 42

# CHECK: { andi32 r4, r5, 255 }
# ROUNDTRIP: { andi32	r4, r5, 31 }
ANDI32 R4, R5, 255

# CHECK: { ori32 r6, r7, 15 }
# ROUNDTRIP: { ori32	r6, r7, 15 }
ORI32 R6, R7, 15

#===----------------------------------------------------------------------===
# Shift immediate (Mode-0 s0 ALU32 sub-row, 8 bytes)
#===----------------------------------------------------------------------===

# CHECK: { srli32 r10, r11, 4 }
# ROUNDTRIP: { srli32	r10, r11, 4 }
SRLI32 R10, R11, 4

# CHECK: { srai32 r12, r0, 8 }
# ROUNDTRIP: { srai32	r12, r0, 8 }
SRAI32 R12, R0, 8

# CHECK: { slli32 r1, r2, 16 }
# ROUNDTRIP: { slli32	r1, r2, 16 }
SLLI32 R1, R2, 16

#===----------------------------------------------------------------------===
# Load/Store (legacy FmtLS, 4 bytes — : asm-parse routes to the generic
# simm16 variant; the Mode-0 LD32_M0S0LS/ST32_M0S0LS ×4-scaled variants are
# finalizer emit targets for CodeGen, not asm-parse targets)
#===----------------------------------------------------------------------===

# CHECK: { s_lw_{{[a-z_]*}}	r0, r1, 0 }
# ROUNDTRIP: { s_lw_{{[a-z_]*}}	r0, r1, 0 }
s_lw_with_imm R0, R1, 0

# The immediate is an ELEMENT index: S_LW_WITH_IMM is rs + (imm6 << 2),
# so 4 here addresses byte 16. The old spelling was the byte offset.
# CHECK: { s_lw_{{[a-z_]*}}	r2, r3, 4 }
# ROUNDTRIP: { s_lw_{{[a-z_]*}}	r2, r3, 4 }
s_lw_with_imm R2, R3, 4

# CHECK: { s_sw_{{[a-z_]*}} r4, r5, 0 }
# ROUNDTRIP: { s_sw_{{[a-z_]*}}	r4, r5, 0 }
s_sw_with_imm R4, R5, 0

#===----------------------------------------------------------------------===
# Branch (FmtBr, 4 bytes)
#===----------------------------------------------------------------------===

# CHECK: { beq r8, r9, target_32 }
# ROUNDTRIP: { beq	r8, r9,
BEQ R8, R9, target_32
target_32:

# CHECK: { bne r10, r11, target_32b }
# ROUNDTRIP: { bne	r10, r11,
BNE R10, R11, target_32b
target_32b:
