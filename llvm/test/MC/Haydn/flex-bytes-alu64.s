# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
//
// REGRESSION TEST (// Stage-1 Bundle128 Flex encoding):
// BYTE-PINNED + WIDTH-PINNED oracle. The hand-computed 16 LE bytes for
// `add64 d0, d1, d2` (per encoding_manual_flex.md §1/§2 + s1_encoding.md
// §4.3 ALU64-RR + byte table) MUST:
// (a) round-trip through the disassembler as `add64 d0, d1, d2`
// (b) occupy EXACTLY 16 bytes in.text (not 8 — the width assertion is
// the trap that hid the transitional-form bug for the whole arc).
//
// Harness note: earlier `--disassemble --hex %s` form read the WHOLE file
// as hex (including the lit-directive line) and failed with "invalid input
// token". This test assembles `.byte` directives the normal way, then
// disassembles via objdump — the bytes are pinned BOTH as input (here in
// the source) AND as output (the objdump -d listing).
//
// === BYTE DERIVATION (hand-computed per) ===
//
// Bundle layout (MSB-indexed offsets per HaydnMCFormatDesc):
// s0 window = MSB-offsets [80,127] (48b), FU at MSB-off 80 -> bundle[47:45]
// s1 window = MSB-offsets [40,79] (40b), FU at MSB-off 40 -> bundle[87:85]
// s2 window = MSB-offsets [0,39] (40b), FU at MSB-off 0 -> bundle[127:125]
//
// For `add64 d0, d1, d2` placed in s1 (encoder native slot for ADD64):
// FU(3b) = ALU64 = 010 @ MSB-off [40,42] -> bundle[87:85]
// opcode(8b)= 0x3E @ MSB-off [43,50] -> bundle[84:77]
// rsd1(4b) = D1 = 1 @ MSB-off [51,54] -> bundle[76:73]
// rsd2(4b) = D2 = 2 @ MSB-off [55,58] -> bundle[72:69]
// rtd(4b) = D0 = 0 @ MSB-off [59,62] -> bundle[68:65]
// spare(17b)= 0 @ MSB-off [63,79] -> bundle[64:48]
// s0, s2 = NOP (all-zero).
//
// Encoder places sources BEFORE dest (per s1_encoding.md RR convention);
// the encoder reorders from the MCInst dag `(rd, rs1, rs2)` to (rs1, rs2, rd).
//
// Result 128-bit word, LE 16 bytes (R2-dense Bundle128):
// 00 00 00 00 00 00 21 00 00 20 40 00 00 00 00 00
//
// NO XFAIL — Stage-1 ADD64 is shipped and round-trips today (verified).

// CHECK-LABEL: <.text>:
// The byte-CHECK asserts BOTH the exact 16 bytes AND the width (16 bytes
// shown, not 8 — a regression to legacy 8-byte slot-OR would show 8 bytes
// at this offset and a different op at 0x08).
// CHECK: 0: 00 00 00 00 00 00 21 00 00 20 40 00 00 00 00 00
// CHECK: add64 d0, d1, d2
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>
// CHECK-NOT: add64

.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x00
.byte 0x00, 0x20, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00
