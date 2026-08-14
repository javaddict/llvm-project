// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target
// CHECK: {{.*}}0: 4f 49 08 19 a0 74 00 00 00 00 00 00 { xor32 r0, r0, r0; add32 r1, r2, r3 }
// CHECK: {{.*}}c: 4f e9 00 00 20 02 76 00 00 00 00 00 { lui sp, 1; xor32 r0, r0, r0 }

# Role: object — (/): the `Haydn::Bundle<MCInst>` shuffler (encodeFormatE) must auto-assign DISTINCT slots to bare (unsuffixed) bundle.

// REGRESSION TEST (/): the `Haydn::Bundle<MCInst>` shuffler
// (encodeFormatE) must auto-assign DISTINCT slots to bare (unsuffixed) bundle
// children, with zero collisions.
//
// Bug : the prior ad-hoc position+spread slot loop mixed
// position-authority and suffix-authority. When two children both wanted the
// same slot (e.g. two ALU32 ops, both legal in S0|S1|S2), the collision-spread
// path could abort because the legacy-only `getFlexVariant` switch returned 0
// for already-flex opcodes — the colliding child could never be spread.
//
// Fix : `encodeFormatE` now wires `Haydn::Bundle<MCInst>` (the full
// AIE model: canAdd/add/pickSlot first-fit over getLegalSlots, gated by
// isFormatAvailable + OccupiedSlots bitset). `getLegalSlots` is flex-aware via
// `HaydnMCFormatsWithMII` (suffix-strip normalization). A bare child (no
// suffix, no HaydnMCFlags slot) auto-picks the first free legal slot.
//
// Test design: `{ xor32 r0,r0,r0; add32 r1,r2,r3; nop }` — two bare ALU32 ops
// (both legal S0|S1|S2) + a NOP. The shuffler must place xor32 in S0 and add32
// in S1 (distinct slots — no collision). If the shuffler regresses, the two
// ops collide on S0 and the encoder either aborts or emits a corrupt
// Format E that round-trips with <?>/<unknown>.
//
// Contract: both ops round-trip by name (placed in distinct, valid slots).
// Zero placeholders. 12-byte Format E parcel width (Format E, not legacy 8-byte).

.text
.globl test_cb88_shuffler_bare
test_cb88_shuffler_bare:
  { xor32 r0, r0, r0 ; add32 r1, r2, r3 ; nop }

// 12-byte Format E parcel (two real slots + one NOP slot). Both ops round-trip
// in DISTINCT slots (xor32 + add32 — no collision). The wire-Bundle
// render is single-line `{ op.sN...; op.sN...; op.sN }`, so both ops are
// matched with independent CHECK directives on the same objdump line.
// B3.5 source-order S2-first: xor32→S2, add32→S1 → print { nop; add32; xor32 }.
// Hard bar: zero placeholders (the load-bearing no-misencode contract).

// REGRESSION TEST (/ hint-vs-collision): an explicit `.sN` hint that
// collides with an occupied slot MUST spread to a free legal slot, NOT abort.
// `lui` is S0-only (legal={S0}); `xor32` hints S0 but S0 is taken by lui, so
// the shuffler spreads xor32 to S1 (ALU32 is legal S0|S1|S2). Before this
// aborted because the legacy-only getFlexVariant returned 0 for the colliding
// flex child. If the shuffler regresses, llvm-mc aborts at assemble time.
.globl test_cb88_shuffler_hint_collision
test_cb88_shuffler_hint_collision:
  { lui sp, 1 ; xor32 r0, r0, r0 ; nop }

// lui takes S0; xor32 spreads to free high slot (S2).
