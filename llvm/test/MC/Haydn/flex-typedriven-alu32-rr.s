// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target
//
// REGRESSION TEST (type-driven Flex scale): NEW ALU32 RR ops (SUB32
// AND32, OR32, XOR32 — opcodes 0x01-0x04, NOT in the 7-op baseline) MUST
// round-trip through the Flex Bundle128 path AND occupy exactly 16 bytes
// (width assertion). This proves the type-driven `placeFlexSlot`
// `decodeFlexSlot` (HaydnFlexLayout table) generalize beyond the hand-coded
// 7-op switch — the same ST_RRR layout covers every ALU32 RR op.
//
// The byte-pinned oracle for ADD32 (flex-bytes-alu32.s) is the canonical
// bit-level proof; this test extends coverage to the rest of the ALU32 RR
// family via round-trip + width. A future byte-pin oracle can be added per
// op if a regression bites.
//
// Test design: each op assembles standalone (wrapped in a single-child
// bundle), disassembles to itself, and occupies exactly 16 bytes (the
// Bundle128 contract — a regression to the 8-byte legacy slot-OR path would
// emit 8 bytes and break the width check).
//
// NO XFAIL — type-driven scale covers all ALU32 RR ops.

// CHECK-LABEL: <.text>:
// Each op renders as `<offset>: <16 bytes> { mnemonic }` (bytes + mnemonic
// on one objdump line). Loose CHECK (not CHECK-NEXT) since the offset line
// and the mnemonic line are the SAME line in objdump output.
// The byte offset (0x10, 0x20, 0x30 — not 0x08, 0x18, 0x28) is the width
// assertion: 16 bytes per parcel, not the legacy 8.
// CHECK: 0:
// CHECK: sub32 r1, r2, r3
// CHECK: 10:
// CHECK: and32 r4, r5, r6
// CHECK: 20:
// CHECK: or32 r7, r8, r9
// CHECK: 30:
// CHECK: xor32 r10, r11, r0
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>

{ sub32 r1, r2, r3 }
{ and32 r4, r5, r6 }
{ or32 r7, r8, r9 }
{ xor32 r10, r11, r0 }
