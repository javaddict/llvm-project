# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o 2>&1 | \
# RUN:   FileCheck %s

# REGRESSION TEST : decodeSImmOperandXStepWide must NEVER abort.
#
# Bug: HaydnDisassembler.cpp:333 `decodeSImmOperandXStepWide` asserted
# `isUInt<N>(Imm)`. The generated Bundle128 LD slot sub-trie (cases 147/148
# 149/150 in HaydnGenDisassemblerTables.inc) reads an 8-bit slice
# (`fieldFromInstruction(insn, 28, 8)`) for a `simm6` operand, because tblgen
# merges the adjacent `reserved` field into the same decoded region. When a
# legacy parcel stream (or a straddle read) sets bits 6/7 of that 8-bit slice
# the assert fired -> abort (exit 134). compiler-rt adddf3.o triggered this on
# every objdump run (the dominant direct-.elf blocker). The disassembler MUST
# decode-or-degrade, never assert (CLAUDE.md "bounds-safe printOperand" bar).
#
# Root cause: the tablegen field aggregation over-reads N bits; the template
# trusted the width rather than masking. Fix: mask `Imm &=
# maskTrailingOnes<uint32_t>(N)` BEFORE the sign/zero-extend so the assert can
# never fire. The encoder only ever wrote N bits; masking recovers exactly
# what was encoded. See decision -decoder-assert-graceful-and-jal-diagnosis.md.
#
# Test design: emit the EXACT 16 bytes from adddf3.o offset 0x5f0 that crashed
# objdump pre-fix (a legacy parcel stream that passed the Bundle128 content
# gate because all 3 slot windows had valid FU=2 ALU64 + in-range opcode, then
# routed a simm6 operand whose 8-bit tblgen-aggregated slice had bits 6/7
# set). Pre-fix: objdump aborted with exit 134 on this window. Post-fix:
# objdump exits 0 and renders *something* (a decode or <?>/<unknown>) — the
# hard bar is "no crash", and the CHECK only pins completion + the absence of
# the assertion marker. We do NOT pin the exact textual decode because the
# over-read bits are a tblgen artifact, not an ISA-defined value.

# The exact 16-byte window from adddf3.o:0x5f0 that aborted pre-fix.
.byte 0x40, 0x40, 0x33, 0x50, 0x37, 0x55, 0xee, 0x59
.byte 0x10, 0x00, 0x50, 0x60, 0x00, 0x41, 0xf3, 0x51

# CHECK-LABEL: Disassembly of section .text:
# The hard bar: objdump completes (exit 0) and does NOT abort.
# CHECK-NOT: {{Assertion|abort|Stack dump|PLEASE submit a bug report}}
