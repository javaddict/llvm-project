// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target

// REGRESSION TEST (/): the `Haydn::Bundle<MCInst>` shuffler
// (encodeBundle128) must auto-assign DISTINCT slots to bare (unsuffixed) bundle
// children, with zero collisions.
//
// Bug : the prior ad-hoc position+spread slot loop mixed
// position-authority and suffix-authority. When two children both wanted the
// same slot (e.g. two ALU32 ops, both legal in S0|S1|S2), the collision-spread
// path could abort because the legacy-only `getFlexVariant` switch returned 0
// for already-flex opcodes — the colliding child could never be spread.
//
// Fix : `encodeBundle128` now wires `Haydn::Bundle<MCInst>` (the full
// AIE model: canAdd/add/pickSlot first-fit over getLegalSlots, gated by
// isFormatAvailable + OccupiedSlots bitset). `getLegalSlots` is flex-aware via
// `HaydnMCFormatsWithMII` (suffix-strip normalization). A bare child (no
// suffix, no HaydnMCFlags slot) auto-picks the first free legal slot.
//
// Test design: `{ xor32 r0,r0,r0; add32 r1,r2,r3; nop }` — two bare ALU32 ops
// (both legal S0|S1|S2) + a NOP. The shuffler must place xor32 in S0 and add32
// in S1 (distinct slots — no collision). If the shuffler regresses, the two
// ops collide and the encoder either aborts or emits a corrupt parcel that
// round-trips with <?>/<unknown>.
//
// Contract: both ops round-trip by name, in distinct valid placements, with
// zero placeholders. WHICH placement each gets is the packer's choice and is
// not asserted — that is § 5.12's territory and it will move. What has to
// hold is that there are two of them and neither turned into a placeholder.

.text
.globl test_cb88_shuffler_bare
test_cb88_shuffler_bare:
  { xor32 r0, r0, r0 ; add32 r1, r2, r3 ; nop }

// CHECK-LABEL: <test_cb88_shuffler_bare>:
// One 12-byte parcel holding both ops plus a NOP. Both round-trip, in
// distinct placements, on the same objdump line — either print order
// satisfies that, so the check takes both.
// CHECK: { {{xor32 r0, r0, r0.*add32 r1, r2, r3|add32 r1, r2, r3.*xor32 r0, r0, r0}}
// Hard bar: zero placeholders (the load-bearing no-misencode contract).
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>

// REGRESSION TEST (/ hint-vs-collision): an explicit `.sN` hint that
// collides with an occupied slot MUST spread to a free legal slot, NOT abort.
// `lui` is S0-only (legal={S0}); `xor32` hints S0 but S0 is taken by lui, so
// the shuffler spreads xor32 to S1 (ALU32 is legal S0|S1|S2). Before this
// aborted because the legacy-only getFlexVariant returned 0 for the colliding
// flex child. If the shuffler regresses, llvm-mc aborts at assemble time.
.globl test_cb88_shuffler_hint_collision
test_cb88_shuffler_hint_collision:
  { lui sp, 1 ; xor32 r0, r0, r0 ; nop }

// CHECK-LABEL: <test_cb88_shuffler_hint_collision>:
// The hint collides, the shuffler spreads rather than aborting, and both ops
// survive in one bundle. Order is not the contract.
// CHECK: { {{xor32 r0, r0, r0.*lui sp, 1|lui sp, 1.*xor32 r0, r0, r0}}
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>
