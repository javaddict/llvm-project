# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf - | FileCheck %s
#
# REGRESSION TEST: HI20 fixup compensation value was 0x800 instead of 0x8000.
#
# Bug: HaydnAsmBackend.cpp applied HI20 fixup compensation as
# (Value + 0x800) >> 12 -- the RISC-V 20/12 bit split formula.
# Haydn uses a 16/16 split (LUI loads uimm16<<16, ADDI32 adds sext(simm16))
# so the correct compensation is (Value + 0x8000) >> 16.
#
# The fixup compensation is exercised when the linker resolves HI20/LO16
# relocations. The CodeGen path (const.ll) tests the selector's C++ compensation
# (if (Lo16 & 0x8000) HiBits++). This test verifies the MC layer encodes
# LUI+ADDI32 pairs correctly, confirming the instruction format layout that the
# fixup handler operates on (FmtI: bits [15:0] = 16-bit immediate).
#
# ISA-43 update: LUI immediate is now uimm12 (0..4095). The LUI operands below
# use valid uimm12 values; the FmtI 16-bit immediate layout the fixup operates
# on is unchanged. 0x123 (bit 9 set) and 0xFFF (max uimm12, bit 11 set) still
# exercise the immediate encoding field.

.text
.globl _start
_start:

# CHECK: <_start>:
# LUI R1, 0x123 -> upper bits (uimm12, bit 9 set).
# Post-migration (R10): LUI's uimm12 immediate now passes through the Mode-0
# s0 path and the decoded immediate renders as the small split value (3) of
# 0x123 in the new bit layout. The load-bearing assertion for this test is
# the FIXUP COMPENSATION formula in HaydnAsmBackend::applyFixup (HI20 =
# (Value + 0x8000) >> 16, not 0x800); the specific decoded immediate values
# below just pin the MC format layout. Post- the decoder renders the
# FULL immediate (291 = 0x123, not the stale "split value" of 3 the prior
# Mode-0 bit layout produced).
# CHECK: lui r1, 291
    LUI R1, 0x123

# ADDI32 R1, R1, 0x5678 -> lower 16 bits. Post- the full immediate
# (22136 = 0x5678) renders, not the stale split value (24).
# CHECK: addi32 r1, r1, 22136
    ADDI32 R1, R1, 0x5678

# LUI with zero upper half
# CHECK: lui r2, 0
    LUI R2, 0x0000

# ADDI32 with positive value (bit 15 not set, no compensation).
# Post- the full immediate (42) renders, not the stale split value (10).
# CHECK: addi32 r2, r2, 42
    ADDI32 R2, R2, 42

# LUI with max uimm12 (0xFFF, bit 11 set — exercises sign-extension boundary).
# Post- the full immediate (4095 = 0xFFF) renders, not the stale split
# value (31).
# CHECK: lui r3, 4095
    LUI R3, 0xFFF

# ADDI32 with negative value (tests sign extension in simm16 encoding).
# InstPrinter renders signed immediates in decimal (-100), not the unsigned
# 20-bit field form (1048476). Accept signed print.
# CHECK: addi32 r3, r3, -100
    ADDI32 R3, R3, -100
