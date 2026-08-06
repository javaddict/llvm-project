# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

# Role: object — encodable DSP math instruction round-trip (asm -> parse -> print).

# REGRESSION TEST: encodable DSP math instruction round-trip (asm -> parse -> print).
#
# This test covers the subset of the D-class DSP-math instructions that currently
# HAVE an encoder and therefore can round-trip through the assembler.
#
# Instructions tested here (encodable):
# SFR moves: MOVEGPR2SFR, MOVESFR2GPR (GPR-only operands)
#
# LUT fixed-point math EXP2/LOG2/RECIP/SQRT now encode; asm→print coverage is
# in companion dsp-math-encoder-pending.s (can fold here once objdump checks
# are unified).
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
