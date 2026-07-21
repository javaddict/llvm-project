# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o /dev/null
# REQUIRES: haydn-registered-target
# TRACKING: encoder slot-assignment gap (post- aggressive Bundle128-only
# cutover). The encoder rewrite places ALU64 ops (add64) in the s0
# window, emitting bytes `21 00 00 00 20 40...` -> `add64`. The
# spec-correct layout is the s1 window (per the flex-bytes-alu64.s decoder
# oracle: bytes `00 00 00 00 00 00 21 00 00 20 40 00...` -> `add64`).
# Per CLAUDE.md slot model, ALU64 runs in Slot 1/2 only (Slot012_ALU vs
# Slot12_ALU), so `add64` is wrong. This test pins the SPEC byte layout
# (s1) and will PASS again once the encoder slot-assignment reads the slot
# from the format-def (tracked task #256 / HaydnSlotVariantFinalizer
# retirement). Do NOT "fix" by editing CHECKs to the s0 bytes — that would
# mask the encoder bug. Un-XFAIL when add64 round-trips as add64.
#
# REGRESSION TEST (// Stage-1): Bundle128 single-composite format.
# `add64 d0, d1, d2` MUST round-trip through the symmetric no-rewrite
# Bundle128 path (16-byte parcel) and decode back to the same MCInst.
#
# Bug surface this pins: any future regression in the encoder's placeFlexSlot
# or the decoder's extractSlot (or the HaydnMCFormatDesc slot-window offsets)
# breaks the round-trip — either objdump shows `<unknown>` / `<?>`, or the
# decoded mnemonic/operands diverge. The WIDTH assertion (16 bytes, not 8)
# catches a regression to the legacy slot-OR path that round-trips fine but
# breaks the Bundle128 contract.
#
# Encoder-side contract (HaydnMCCodeEmitter::encodeBundle128 + placeFlexSlot):
# Slot 1 window = MSB-offsets [40,79] (40b), FU at MSB-off 40 -> bundle[87:85]:
# FU(3) = ALU64 (=010) @ MSB-off [40,42] -> bundle[87:85]
# opcode(8)= 0x3E (ADD64_OPCODE) @ MSB-off [43,50] -> bundle[84:77]
# rsd1(4) = rs1 = D1 @ MSB-off [51,54] -> bundle[76:73]
# rsd2(4) = rs2 = D2 @ MSB-off [55,58] -> bundle[72:69]
# rtd(4) = rd = D0 @ MSB-off [59,62] -> bundle[68:65]
# Slot 0 (s0) and Slot 2 (s2) windows: all zero (FU=0 ALU32 NOP).
# Encoder places sources BEFORE dest (s1_encoding.md §4.3 RR convention).
#
# Decoder-side contract (HaydnDisassembler::tryDecodeBundle128):
# 1. Read 16 bytes LE into a 128-bit APInt.
# 2. extractSlot for s1 (MSB-off [40,79]) -> SlotBits (40b).
# 3. FU = top 3 bits of SlotBits; require == ALU64 (=010).
# 4. Read opcode at slot[36:29]; require == 0x3E (ADD64_OPCODE).
# 5. Read rsd1/rsd2/rtd at slot[28:25]/[24:21]/[20:17].
# 6. setOpcode(ADD64); push DR64 operands via decodeDR64.
#
# Expected 16 LE bytes for `add64 d0, d1, d2` (R2-dense Bundle128):
# 00 00 00 00 00 00 21 00 00 20 40 00 00 00 00 00
# Width = 16 bytes (Bundle128 parcel); a regression to the 8-byte legacy
# path emits different bytes at a different offset.


# add64 d0, d1, d2: emits one Bundle128 parcel (16 bytes), decodes cleanly.
# The byte-CHECK asserts BOTH the exact 16 bytes AND the width (16 bytes
# shown, not 8 — a regression to legacy 8-byte slot-OR would show 8 bytes
# at this offset and a different op at 0x08).

{ add64 d0, d1, d2 }
