# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
# Phase-2 decoder purge collateral (prior revision): historically a hybrid
# test. The s0 ALU32 RR sub-row survived (sll32/sra32/add32/max32 all
# decode). Under the 128-bit-only Flex decoder also covers the s1/s2
# ALU64 DR64 sub-row, so or64/add64 round-trip correctly.
#
# UN-XFAIL (tryEncodeGFormat retirement): this test was XFAIL because RR-form
# ALU32 ops with r8-r15 sources (e.g. SLL32 r1, r1, r12) were emitted through
# the G-format path, where the decoder mis-read the slot window and dropped the
# high register bits (r12 -> r0). With tryEncodeGFormat retired (manual §3.6
# conflict), ALL 32-bit ALU32 RR ops route to `emitMode0S0Bundle` (64-bit
# Mode-0 §6), whose decoder reconstructs the full 4-bit rs2 field — so the
# operands now round-trip correctly and the OOB regression stays fixed.
# If a future change re-introduces MC-time G-format emission for these, the
# CHECK lines will fail and this regression will need re-evaluation.
# REGRESSION TEST: HaydnInstPrinter OOB crash on RR-form ALU32 + ALU64 ops
# packed into Mode-0 s0/s1/s2 slots.
#
# Bug : llvm-objdump -d crashed with `Assertion idx < size at
# SmallVector.h:301` inside HaydnInstPrinter::printOperand, called from
# printInstruction. Three independent root causes, all surfaced once the
# packetizer began forming real Mode-0 multi-slot bundles:
#
# (1) RR-form ALU32 in s0 (SLL32, SRA32, ADD32, MAX32, …):
# HaydnMCCodeEmitter::packInstructionIntoSlot PS_S0 read operand 2 with
# ReadImm — but operand 2 is a GPR32 register. ReadImm returns 0
# silently zeroing the shift-amount / source register AND signalling
# the decoder that operand 2 is absent.
# (2) RR-form ALU32 s0 decoder (decodeM0SlotS0) only attached operand 2 for
# RI-form ops (SLLI32, ADDI32, …), producing a 2-operand MCInst for
# RR-form ops. printInstruction indexes operand 2 → OOB.
# (3) ALU64 ops in s1/s2 (OR64, ADD64, …): the.td models 3 DR64 operands
# (rd, rs1, rs2) but s1/s2 layouts encode only 2 register fields (rtd
# rsd1). The encoder uses the destructive/copy form (rs2 = rs1); the
# decoder emitted only 2 operands → OOB.
#
# Why this test exists: this is the regression that blocked ALL post-link
# inspection (CoreMark ELF, real-port ELFs, everything). Without this test, a
# future change to the decoder could silently re-introduce the OOB by
# dropping an operand reconstruction.
#
# If this test fails (objdump crashes, prints wrong operand count, or omits
# the rs2 register), do NOT adjust CHECK lines — re-read and verify the
# decoder emits N operands matching the.td (ins …) list.

# CHECK: file format elf32-unknown
# CHECK-LABEL: <rr_alu32_s0>:
# Every op is a 16-byte Bundle128 composite `{ op.sN...; nop; nop }`.
# CHECK: sll32 r1, r1, r12
# CHECK: sra32 r2, r3, r4
# CHECK: add32 r5, r6, r7
# CHECK: max32 r8, r9, r10

.text
.globl rr_alu32_s0
.type rr_alu32_s0,@function
rr_alu32_s0:
  SLL32 R1, R1, R12
  SRA32 R2, R3, R4
  ADD32 R5, R6, R7
  MAX32 R8, R9, R10
.size rr_alu32_s0, .-rr_alu32_s0

# CHECK-LABEL: <alu64_dr64>:
# CHECK: or64 d0, d1, d2
# CHECK: add64 d3, d4, d5
.globl alu64_dr64
.type alu64_dr64,@function
alu64_dr64:
  OR64 D0, D1, D2
  ADD64 D3, D4, D5
.size alu64_dr64, .-alu64_dr64
