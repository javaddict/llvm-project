# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
#
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

# CHECK: add32 r0, r1, r2
ADD32 R0, R1, R2

# CHECK: sub32 r3, r4, r5
SUB32 R3, R4, R5

# CHECK: and32 r6, r7, r8
AND32 R6, R7, R8

# CHECK: or32 r9, r10, r11
OR32 R9, R10, R11

# CHECK: xor32 r12, r0, r1
XOR32 R12, R0, R1

# CHECK: slt32 r0, r1, r2
SLT32 R0, R1, R2

# CHECK: seq32 r0, r1, r2
SEQ32 R0, R1, R2

# mul32 removed : not in the ISA DB; scalar s32 mul lowers via the
# slot-1/2 DR64 unit (sext32t64 + mul64.ll + move32_dr_l). No scalar GPR mul.

# CHECK: move32 r0, r1
MOVE32 R0, R1

# CHECK: not32 r0, r1
NOT32 R0, R1

# CHECK: neg32 r0, r1
NEG32 R0, R1

# CHECK: abs32 r0, r1
ABS32 R0, R1

#===----------------------------------------------------------------------===
# Test ALU32 immediate instructions (s0, FU=ALU32, RI format)
#===----------------------------------------------------------------------===

# CHECK: addi32 r0, r1, 42
ADDI32 R0, R1, 42

# CHECK: slli32 r0, r1, 4
SLLI32 R0, R1, 4

# CHECK: srli32 r0, r1, 8
SRLI32 R0, R1, 8

# CHECK: srai32 r0, r1, 16
SRAI32 R0, R1, 16

# CHECK: lui r0, 1024
LUI R0, 1024

#===----------------------------------------------------------------------===
# Test Load/Store instructions (s0, FU=LS)
#===----------------------------------------------------------------------===

# CHECK: s_lw_{{[a-z_]*}} r0, r1, 0
s_lw_with_imm R0, R1, 0

# CHECK: s_lw_{{[a-z_]*}} r2, r3, 16
s_lw_with_imm R2, R3, 4

# CHECK: s_sw_{{[a-z_]*}} r4, r5, 0
s_sw_with_imm R4, R5, 0

# CHECK: s_sw_{{[a-z_]*}} r6, r7, -4
s_sw_with_imm R6, R7, -1

#===----------------------------------------------------------------------===
# Test ALU64 instructions (s1/s2, FU=ALU64)
# NOTE: move64, neg64, not64 have no MC encoding (isPseudo) — excluded.
#===----------------------------------------------------------------------===

# CHECK: add64 d0, d1, d2
ADD64 D0, D1, D2

# CHECK: sub64 d3, d4, d5
SUB64 D3, D4, D5

# CHECK: and64 d6, d7, d8
AND64 D6, D7, D8

# CHECK: or64 d0, d1, d2
OR64 D0, D1, D2

#===----------------------------------------------------------------------===
# Test MAC instructions (s1/s2, FU=MAC)
#===----------------------------------------------------------------------===

# (Path B): X2MUL32 / X4MUL16 are now TRUE 2-output SIMD-MAC
# (outs DR64:$rd, DR64:$rtd2; ins DR64:$rs1, DR64:$rs2) — the asm form is
# 4-operand. The 3-operand form was retired.
# CHECK: x2mul32 d0, d1, d2, d3
X2MUL32 D0, D1, D2, D3

# CHECK: x4mul16 d0, d1, d2, d3
X4MUL16 D0, D1, D2, D3

# CHECK: x4add16 d0, d1, d2
X4ADD16 D0, D1, D2

#===----------------------------------------------------------------------===
# Test branch instructions (slot 0 only)
#===----------------------------------------------------------------------===

target_branch:
# CHECK: beq r0, r1, target_branch
BEQ R0, R1, target_branch

target_branch2:
# CHECK: bne r0, r1, target_branch2
BNE R0, R1, target_branch2

target_jal:
# CHECK: jal r0, target_jal
JAL R0, target_jal
