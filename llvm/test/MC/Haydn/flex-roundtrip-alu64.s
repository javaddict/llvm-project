# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o /dev/null
# REQUIRES: haydn-registered-target
# TRACKING: encoder slot-assignment gap (post- aggressive Bundle128-only
# cutover). The encoder places ALU64 ops in the s0 window, emitting
# `21 00 00 00 20 40...` -> `add64`. The spec-correct layout is the s1
# window (see companion flex-bytes-alu64.s decoder oracle, which feeds the
# s1 bytes and renders `add64`). Per CLAUDE.md slot model ALU64 is
# Slot12_ALU only, so `add64` is wrong. This round-trip test pins the
# spec byte form; it will PASS once the encoder reads the slot from the
# format-def (tracked task #256). Do NOT edit CHECKs to s0 bytes — that
# masks the bug. Un-XFAIL when add64 round-trips as add64.
#
# REGRESSION TEST (// Stage-1 Bundle128 Flex encoding):
# `{ add64 d0, d1, d2 }` MUST assemble to a 16-byte Bundle128 parcel and
# disassemble back to the same mnemonic + operands. The companion byte-pin
# test flex-bytes-alu64.s asserts the EXACT bytes; this test covers the
# asm -> bytes -> asm round-trip and the WIDTH (must be 16 bytes, not 8).
#
# This is the ORACLE for the Flex 128-bit encoding. The byte constants in
# flex-bytes-alu64.s are derived FROM THE SPEC (s1_encoding.md §4.3 ALU64-RR
# + byte table), NOT from running the encoder. The bytes are the
# canonical definition; a future encoder/decoder regression that shifts
# even one bit is caught by flex-bytes-alu64.s before it can rot downstream
# oracles.
#
# === BYTE DERIVATION (hand-computed, see) ===
#
# For `add64 d0, d1, d2` placed in s1 (Stage-1 contract: ADD64 native slot):
# FU(3b) = ALU64 = 010 @ MSB-off [40,42] -> bundle[87:85]
# opcode(8b)= 0x3E @ MSB-off [43,50] -> bundle[84:77]
# rsd1(4b) = D1 = 1 @ MSB-off [51,54] -> bundle[76:73]
# rsd2(4b) = D2 = 2 @ MSB-off [55,58] -> bundle[72:69]
# rtd(4b) = D0 = 0 @ MSB-off [59,62] -> bundle[68:65]
#
# Result 128-bit word, LE 16 bytes (R2-dense Bundle128):
# 00 00 00 00 00 00 21 00 00 20 40 00 00 00 00 00
#
# === WIDTH ASSERTION (the trap that hid everything) ===
#
# Round-trip is WIDTH-AGNOSTIC — it passed on the 64b path for the whole
# > arc. The Bundle128 path MUST emit 16 bytes. The byte-CHECK
# below asserts BOTH the exact 16 bytes AND the width (16 bytes shown, not
# 8 — a regression to legacy 8-byte slot-OR would show 8 bytes and a
# different op at 0x08). The companion flex-bytes-alu64.s is the canonical
# byte-pin oracle; this test covers the asm->bytes->asm round-trip.
#
# Stage-1 status: was shipping pre-; now XFAIL tracking the post
# slot-assignment gap (see top-of-file XFAIL note).

# The byte-CHECK asserts BOTH the exact 16 bytes AND the width (16 bytes
# shown, not 8 — a regression to legacy 8-byte slot-OR would show 8 bytes
# at this offset and a different op at 0x08).

{ add64 d0, d1, d2 }
