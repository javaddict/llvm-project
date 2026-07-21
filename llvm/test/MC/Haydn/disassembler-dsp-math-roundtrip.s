# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s
#
# REGRESSION TEST: encodable DSP math instruction round-trip (asm -> parse -> print).
#
# This test covers the subset of the D-class DSP-math instructions that currently
# HAVE an encoder and therefore can round-trip through the assembler. The
# LUT-based fixed-point math instructions EXP2, LOG2, RECIP, SQRT are real DB
# instructions (syntax "EXP2 rt, rs", GPR operands, ~/haydn-plans/Database
# haydn_instruction_db.json) but have NO encoder yet (M5 encoding work) and are
# shielded from the assembler via isCodeGenOnly=1 so the MC emitter does not
# crash ("LLVM ERROR: Unsupported instruction"). They are tracked in the
# companion XFAIL test dsp-math-encoder-pending.s and will be moved back here
# once the M5 encoders land.
#
# Instructions tested here (encodable):
# SFR moves: MOVEGPR2SFR, MOVESFR2GPR (GPR-only operands)
#
# Instructions NOT tested here (encoder pending M5, see companion test):
# LUT-based fixed-point math: EXP2, LOG2, RECIP, SQRT
#
# Instructions NOT tested here (parser gap — mixed GPR/DR operands):
# CORDIC-based math: ARCTAN, SIN_COS
# Normalization helpers: NSA16_L, NSA32_L, NSAZ16_L, NSAZ32_L
#
# These instructions use the D-class 64-bit bundle encoding (TSFlags=14
# Slot1+2 ALU) and were added to the HaydnDClassOpcodes mapping table.
#
# If any CHECK line fails, do NOT update it without understanding the root
# cause. The disassembler register class determination must correctly handle
# mixed GPR/DR operands (SRC_MIXED_GPR_DST) for ARCTAN, SIN_COS, and NSA*_L.

#===----------------------------------------------------------------------===
# SFR move instructions (GPR-only operands, encodable)
#===----------------------------------------------------------------------===

# CHECK: movegpr2sfr	r0
movegpr2sfr	r0

# CHECK: movesfr2gpr	r1
movesfr2gpr	r1
