// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target
//
// REGRESSION TEST (/ Stage-2 Bundle128 ALU32 FU): the hand-computed
// 16 LE bytes for `add32 r1, r2, r3` (per s0_encoding.md §4.1 ALU32-RR +
// §5 ADD32 opcode remap 0x00 -> 0x3E + byte table) MUST decode to
// `add32 r1, r2, r3` AND occupy exactly 16 bytes (width assertion).
//
// === BYTE DERIVATION (hand-computed per) ===
//
// s0 layout (48b window, MSB-offsets [80,127]):
// FU(3b) = ALU32 = 000 @ MSB-off [80,82] -> bundle[47:45]
// opcode(6b)= 0x3E @ MSB-off [83,88] -> bundle[44:39]
// rsd1(4b) = r2 = 2 @ MSB-off [89,92] -> bundle[38:35] (src first)
// rsd2(4b) = r3 = 3 @ MSB-off [93,96] -> bundle[34:31]
// rtd(4b) = r1 = 1 @ MSB-off [97,100] -> bundle[30:27] (dest last)
// spare(27b)= 0
// s1, s2 = NOP (all-zero).
//
// Encoder places sources BEFORE dest (s1_encoding.md RR convention applied
// uniformly across s0/s1/s2 by placeFlexSlot — NOT the s0_encoding.md doc
// order of "rt rs1 rs2").
//
// Result 128-bit word, LE 16 bytes (R2-dense Bundle128):
// 32 01 00 00 40 01 00 00 00 00 00 00 00 00 00 00
//
// NO XFAIL — Stage-2 ALU32 (ADD32) is migrated to Bundle128 (encoder routes
// Haydn::ADD32 through encodeBundle128 per isBundle128TargetOpcode).

// CHECK-LABEL: <.text>:
// The byte-CHECK asserts BOTH the exact 16 bytes AND the width (16 bytes
// shown, not 8 — a regression to legacy 8-byte slot-OR would show 8 bytes
// at this offset and a different op at 0x08).
// CHECK: 0: 32 01 00 00 40 01 00 00 00 00 00 00 00 00 00 00
// CHECK: add32 r1, r2, r3
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>
// CHECK-NOT: add32

.byte 0x32, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00
.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
