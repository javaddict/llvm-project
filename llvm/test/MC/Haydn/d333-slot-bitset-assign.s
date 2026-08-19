# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --triple haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Prior XFAIL (Phase-2 decoder purge collateral) is resolved: the s0 ALU32 sub-row decodes via the Mode-0 s0 trie; the s1/s2 ALU64 ops (add64) decode.

# Prior XFAIL (Phase-2 decoder purge collateral) is resolved: the s0 ALU32
# sub-row decodes via the Mode-0 s0 trie; the s1/s2 ALU64 ops (add64) decode
# via DecoderTableHaydnM0S1ALU64RR3 / DecoderTableHaydnM0S2ALU64; the s1 MAC
# family (x2mula32) decodes via DecoderTableHaydnM0S1MAC64.
#
# E4d: the -haydn-slot-bitset-assign flag was RETIRED with the row
# auction deletion — encodeBundle is unconditionally pure format-first
# (slot-OR + 1-child standalone + fatal), no fallback. The flag was removed
# from the RUN line; the test now exercises the default emission path.

# REGRESSION TEST (, M7 step 2): the bitset-driven slot assigner
# (assignSlotsBitset) must produce VALID Mode-0 bundles.
#
# Under -haydn-slot-bitset-assign, encodeBundle's fallback path uses
# assignSlotsBitset (getAltSlotSet candidates + distinct-slot backtracking +
# FU-combo validation against M0Rows) instead of findCompatibleRow. The generic
# opcodes here are NOT EW_64Bit slot variants, so encodeBundleSlotOR declines
# and the fallback path runs assignSlotsBitset.
#
# NOTE: the Hayden MC parser emits each `{ ... }` child as its own single-op
# bundle (multi-op packing is a CodeGen/post-RA-scheduler concern, not an
# asm-syntax one). So this test exercises assignSlotsBitset's SINGLE-CHILD path
# across the FU classes (ALU32/ALU64/MAC/ALU32-unary) — proving it picks a
# valid slot + emits a clean, decodable word for each. The multi-child path is
# covered by the unittest's self-consistency check + future post-RA MIR tests.
#
# Contract: every op round-trips — the disassembly shows the original op names
# with ZERO `<?>` / `<unknown>` placeholders. If assignSlotsBitset picked a
# wrong slot, packInstructionIntoSlot would emit garbage and objdump would show
# <?>. Bytes are not compared (step-2 byte-equality relaxed per directive).

.text
.globl test_d333_assign_slots_bitset
test_d333_assign_slots_bitset:
  # 2-op bundle: ALU32 (FU_ALU32, s0) + ALU64 (FU_ALU64, s1).
  { add32 r8, r9, r10 ; add64 d0, d1, d2 }
  # Slot-order independence (codex step-2 note): same two ops REVERSED. The
  # assigner must place them in distinct valid slots regardless of child order;
  # bytes may differ but the decode is the same op set.
  { add64 d0, d1, d2 ; add32 r8, r9, r10 }
  # 2-op bundle: two ALU64 (FU_ALU64, s1 + s2).
  { add64 d3, d4, d5 ; add64 d6, d7, d8 }
  # 3-op bundle: ALU32-unary (s0/s1/s2 legal) + ALU64 (s1) + ALU32-unary.
  { neg32 r1, r2 ; add64 d3, d4, d5 ; not32 r3, r4 }
  # MAC (FU_MAC, s1/s2) + ALU64 (s1/s2) — two s1/s2 ops, distinct slots.
  { x2mula32 d1, d2, d3, d1 ; add64 d6, d7, d8 }

# CHECK-LABEL: <test_d333_assign_slots_bitset>:
# CHECK: {{.*}}0: 4f 09 82 10 a0 24 60 2a 00 00 00 00 { nop; add32 r8, r9, r10; add64 d0, d1, d2 }
# CHECK: c: 4f 49 c0 54 a0 04 41 08 00 00 00 00 { nop; add64 d0, d1, d2; add32 r8, r9, r10 }
# CHECK: {{.*}}18: 4f 09 b2 43 a0 04 0d 15 00 00 00 00 { nop; add64 d3, d4, d5; add64 d6, d7, d8 }
# CHECK: {{.*}}24: 4f 42 18 02 a0 04 0d 15 20 29 42 00 { neg32 r1, r2; add64 d3, d4, d5; not32 r3, r4 }
# CHECK: {{.*}}30: 07 0b 64 87 00 00 48 20 21 13 00 00 { x2mula32 d1, d2, d3, d1; add64 d6, d7, d8 }

# CHECK-NOT: <?>
# CHECK-NOT: <unknown>
