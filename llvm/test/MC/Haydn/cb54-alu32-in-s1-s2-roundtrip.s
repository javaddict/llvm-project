# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — ALU32 is now sub-encodable in S1 and S2 of a Format E parcel (previously S0-only).

# REGRESSION TEST : ALU32 is now sub-encodable in S1 and S2 of a
# Format E word (previously S0-only).
#
# Bug: ALU32 was encodable only in S0. Bundles mixing ALU32 with ALU64 in
# s1/s2 had no legal Mode-0 row (s1/s2 columns were ALU64-only), so the
# encoder fell through to the legacy-flat path and emitted a malformed
# >8-byte blob, breaking 8-byte fetch alignment and hanging CoreMark on
# the -c/ELF path.
#
# Fix (encoding_manual.md §6): the s1/s2 ALU slots now accept ALU32 via
# sub-mode markers (s1: FU=00 + opc_imm[11:10]=11; s2: FU=0 + opcode[8]=1).
# This test pins the round-trip: ALU32 unary ops pack alongside ALU64 in a
# single Format E word and objdump decodes them back.
#
# Post cutover: 3-issue multi-op packing IS implemented. Each bundle is
# 12 bytes (Format E); the decoder renders slots in s0/s1/s2 order with
# `nop` for idle slots. The earlier "SUPERSeded by one-child-per-window"
# XFAIL note is obsolete.
#
# Scope: only the 2-operand unary ALU32 ops (NOT32/NEG32/NEG32S/NSA32/NSAU32
# POPCOUNT32) are S1/S2-legal in this revision — the s1/s2 ALU32 sub-row has
# no rs2/immediate field, so the 3-operand RR family (ADD32/...) and RI forms
# (ADDI32/...) stay S0-only. (3x ALU32 in one bundle is also port-limited out
# of real code: 3x 2R1W = 6R3W > the 4R2W GPR file.)
#
# Spec reference: encoding_manual.md §6 s1/s2 Field Breakdown (ALU32 sub-row).

.text
# ALU32 (S0) + ALU64 (S1): neg32 packs in s0 alongside add64 in s1.
{ neg32 r1, r2; add64 d0, d1, d2 }

# ALU64 (S1) + ALU32 (S2): not32 packs in s2 alongside add64 in s1.
# The encoder routes by FlexMap slot authority, so disasm slot order
# may differ from textual order — both ops still land in one Format E parcel.
{ add64 d3, d4, d5; not32 r5, r6 }

# Two ALU32 ops: not32 + popcount32.
{ not32 r7, r8; popcount32 r9, r10 }

# Backward compat: ALU64 in s1 and s2 still decodes as ALU64 (the ALU32
# sub-mode markers are clear: add64 opcode 0x058 has bit[8]=0, so s2 does
# not read it as ALU32; s1 opc_imm top bits != 11).
{ add64 d0, d1, d2; add64 d3, d4, d5 }

# CHECK-LABEL: .text

# Each bundle is 16 bytes (cursor advances 0xc per bundle), proving
# Format E packing with both slots populated on one objdump line.
# B3.5 source-order S2-first: print is S0-S1-S2 (high-prefer members last).
# CHECK: {{.*}}0: 4f 09 82 10 20 61 84 00 00 00 00 00 { neg32 r1, r2; add64 d0, d1, d2 }
# CHECK: c: 4f 42 28 03 a0 04 0d 15 00 00 00 00 { add64 d3, d4, d5; not32 r5, r6 }
# CHECK: {{.*}}18: 4f 52 48 05 20 21 1c 02 00 00 00 00 { not32 r7, r8; popcount32 r9, r10 }
# CHECK: {{.*}}24: 4f 09 1a 2a a0 04 41 08 00 00 00 00 { add64 d0, d1, d2; add64 d3, d4, d5 }
# CHECK-NOT:  <unknown>
