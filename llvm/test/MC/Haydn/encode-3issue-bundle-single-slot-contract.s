# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | \
# RUN:     llvm-objdump -d --triple=haydn-unknown-elf - | FileCheck %s
# REQUIRES: haydn-registered-target
# Phase-2 decoder purge (prior revision) + WIDE LS migration: the s0
# 0x3F-escape sub-row SURVIVES (`{ move32_dr_l }` decodes), and the s1 ALU64
# sub-row (`{ add64 }`) decodes via Mode-0 s1. The s0 LS sub-row (`{ ld32 }`)
# was a real decoder gap (legacy FmtLS probe deleted) until routed
# asm-parse LD/ST through the 48-bit WIDE LS parcel (emitWideLSParcel)
# which the Haydn48 trie decodes cleanly as `ld32_wl`. The CHECK contract
# (each instruction round-trips to its real mnemonic, NOT a c.* mis-decode)
# is unchanged. Do NOT weaken the CHECKs.
#
# REGRESSION TEST (, revised Cycle-3): single-instruction bundles MUST
# round-trip cleanly through objdump.
#
# This test pins the round-trip contract: every single-real-child bundle
# decodes to its full-width mnemonic, NOT a c.<compressed> mis-decode. The
# original emitted single-child bundles as standalone legacy-flat
# (bits[31:30]=01 32-bit + zero padding) because the OLD decoder's bundle
# probe rejected single-slot Mode-0 words. Cycle-3 revised this: the CURRENT
# decoder accepts single-slot Mode-0 bundles (decodeDClassBundle returns
# Success with the bare instruction when exactly one slot decodes), so
# encodeBundle now packs single-child bundles via Mode-0 when the child has
# a DClass FU/opcode mapping (TryMode0 = true). This avoids the bits[1:0]=11
# collision between legacy bits[31:30]=01 32-bit encodings and the Path-B
# 64-bit Mode-0/1 patterns that broke __udivsi3/__divsi3 objdump.
#
# Ops WITHOUT a DClass mapping (e.g. sext32t64, mul64.ll) still
# use the legacy-flat fallback; those are tracked for a future mappings
# addition.
#
# The contract: each instruction decodes to its own mnemonic and not to
# garbage.
#
# It used to be spelled "NOT a c.<compressed> alias". That guard could not
# fire: 16-bit compressed forms were retired before the switch
# (HaydnInstrInfoC.td is intentionally empty). Under format E a mis-decode
# does not produce a wrong mnemonic at all — the generated sub-tries check the
# reserved bits, so it produces `<unknown>` (FORMAT-E-SWITCH-PLAN.md § 5.2).
# That is what this now forbids.

# CHECK-LABEL: <.text>:

# CHECK: s_lw_with_imm
# CHECK-NOT: <unknown>
# CHECK: add64
# CHECK-NOT: <unknown>
# CHECK: add32
# CHECK-NOT: <unknown>
{ s_lw_with_imm r1, r1, 1 }

{ add64 d0, d1, d2 }

add32 r1, r2, r3
