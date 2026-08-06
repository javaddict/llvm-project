# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s

# REGRESSION TEST (residual): the Bundle128 `JALR_S0_FLEX` imm12
# rs-relative offset field MUST decode as a SIGNED 12-bit byte offset.
#
# Bug: the Bundle128 cutover moved `jalr lr, r2, <off>` into a 16-byte
# Bundle128 whose s0 slot decodes via JALR_S0_FLEX (HaydnFormatsALU32.td).
# That slot's imm12 operand was the generic `simm12` (no DecoderMethod), so
# the tablegen decoder stored the raw 12-bit field UNSIGNED. A negative offset
# like -100 (field 0xF9C) rendered as the huge positive 3996. fixed the
# symmetric JAL imm20 bug via a dedicated `calltarget_s0` operand; this is its
# JALR imm12 twin.
#
# Root cause: the JALR_S0_FLEX slot field stores the signed 12-bit rs-relative
# byte offset DIRECTLY (no /2 word-offset scaling on this format — matching
# the WIDE JALR `calltarget_wide_ri12` Shift=0 semantics; the encoder routes
# via getMachineOpValue which stores the raw value). The field only needs
# sign-extension from 12 bits to render correctly.
#
# Byte-round-trip evidence (pre-fix binary):
# jalr lr, r2, 100 -> bytes...64... -> renders 100 (field 0x064)
# jalr lr, r2, -100 -> bytes...9c... -> renders 3996 (field 0xF9C)
# `0x064` vs low byte `0x9c` of `0xF9C` confirms the field carries the signed
# byte offset (Shift=0), so the only missing transform is sign-extension.
#
# Fix: a dedicated `calltarget_s0_ri12` operand whose DecoderMethod is
# `decodeSImmOperandXStepWide<12,0,/*IsSigned*/1>` (sign-extend from 12 bits
# no shift). The encoder side is unchanged (default getMachineOpValue raw
# encode = byte-identical), so encoder output is preserved. See decision
# cb82-jal-imm20-signext-decode.md (which flagged this JALR residual as
# out of scope) / -cb82-jalr-imm12-signed-decode.md.
#
# Test design: assemble `jalr lr, r2, <offset>` for the -flagged case
# (-100), a small negative (-4), zero (0), and a small positive (+100).
# Pre-fix: objdump printed 3996 / 4092 / 0 / 100 (raw unsigned 12-bit field).
# Post-fix: objdump prints -100 / -4 / 0 / 100 (sign-extended). The test pins
# the signed rendering. If the JALR_S0_FLEX operand ever reverts to the
# raw-unsigned `simm12`, the negative-offset CHECK lines fail (3996 / 4092
# reappear) and the JALR residual is back.
#
# Note: bare `jalr lr, r2, -100` (no ``) routes to a legacy 16-bit parcel
# NOT the Bundle128 `JALR_S0_FLEX` slot — a separate legacy-parcel concern
# outside. The explicit `` form here is the precise repro for the
# documented Bundle128 decode bug.

# -flagged case: -100 (field 0xF9C). Pre-fix: 3996.
jalr lr, r2, -100
# CHECK-LABEL: Disassembly of section .text:
# CHECK: jalr	lr, r2, -100

# Small negative offset: -4 (field 0xFFC). Pre-fix: 4092.
jalr lr, r2, -4
# CHECK: jalr	lr, r2, -4

# Zero offset (field 0x000). Unaffected by sign-extension.
jalr lr, r2, 0
# CHECK: jalr	lr, r2, 0

# Small positive offset: +100 (field 0x064). Unaffected.
jalr lr, r2, 100
# CHECK: jalr	lr, r2, 100
