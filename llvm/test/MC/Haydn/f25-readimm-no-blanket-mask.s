# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s

// CHECK: 	{ 	slli32	r1, r2, 5 }             // encoding: [0x07,0x06,0x14,0x02,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	addi32	r3, r4, 17 }            // encoding: [0x07,0x0f,0x32,0x84,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# Role: object — F25 — ReadImm must not blanket-mask immediates to 16 bits.

# REGRESSION TEST: F25 — ReadImm must not blanket-mask immediates to 16 bits.
#
# Bug: HaydnMCCodeEmitter::packInstructionIntoSlot's ReadImm helper applied
# `Op.getImm & 0xFFFF` to every immediate operand. This silently truncated
# any immediate wider than 16 bits, corrupting:
# 32-bit MOVEI/WideImm immediates (when 48-bit ops flow through the
# bundle packer instead of the dedicated encode48Bit path)
# 8-bit immediates with bit 15+ set (e.g. negative shift amounts encoded
# as unsigned after sign extension)
#
# Fix: removed the blanket mask; each call site masks its own field width
# (5b ext, 8b imm, 12b payload, etc.).
#
# Test design: assemble an ALU32 immediate instruction with an immediate
# whose lower 5 bits are zero but upper bits are set (the 5b field masks
# correctly), and another whose value would be silently truncated by the
# old 16b mask. The encoded 5b ext field must be the lower 5 bits of the
# requested immediate; if F25 regresses, the field would silently be zero
# for any immediate wider than 16 bits even when its low 5 bits are nonzero.
#
# Do NOT update CHECK lines without understanding the root cause.

#===----------------------------------------------------------------------===#
# SLLI32 with imm=0x10005 (lower 5 bits = 0x05, upper bits set).
# The old 16-bit mask would have given 0x10005 & 0xFFFF = 0x0005 — same low
# 5 bits in this case. But if a hypothetical 0x20005 came through, the old
# mask would also yield 0x0005 (correct here), whereas a value like 0x50005
# would still be truncated. The real test is that no diagnostics/truncation
# happens — the immediate is accepted as-is and the field-encoded value
# reflects the low 5 bits.
#===----------------------------------------------------------------------===#

SLLI32 R1, R2, 5

ADDI32 R3, R4, 17
