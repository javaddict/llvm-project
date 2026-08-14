// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target
//
// REGRESSION TEST: the minimal bundle MUST decode as NOPs — no FU
// sub-decoder clocks, no spurious operand fetch, no `<?>` placeholder.
//
// Bundle128's rule was "all 128 bits zero = NOP", and this test pinned it.
// Format E cannot state it that way: `bit[2:0] = 0b111` is the format
// indicator, so an all-zero parcel is not a bundle at all — it disassembles
// as `<unknown>`. The rule that survives is one step weaker and is what the
// bytes below assert: **every payload bit zero, with only the format
// indicator set, decodes as NOPs**.
//
// === BYTE DERIVATION (hand-computed from format_e_bit_layout_v2.json) ===
//
//   bit[2:0]   = 0b111   format indicator
//   bit[3]     = 0       entry_num: 2 entries
//   bit[5:4]   = 0       reserved
//   entry0 = bit[50:6],  mapping bit[7:6] = 0b00 -> ALU0
//                        type_code bit[12:8] = 0b00000 -> SFR
//                        opcode bit[19:18] = 0b00 -> NOP
//   entry1 = bit[91:51], mapping bit[52:51] = 0b00 -> ALU1
//                        type_code bit[53] = 0 -> NOP
//
//   Every field a NOP needs is zero, so the whole 96-bit word is zero except
//   the three format-indicator bits: byte0 = 0b0000_0111 = 0x07.
//
//   Little-endian 12 bytes: 07 00 00 00 00 00 00 00 00 00 00 00
//
// The width is pinned too: 12 bytes, not 16. A Bundle128 parcel at this
// offset would show 16 and the next instruction would start at 0x10.
//
// NO XFAIL — a bundle of NOPs is an architectural invariant.

// CHECK-LABEL: <.text>:
// CHECK: 0: 07 00 00 00 00 00 00 00 00 00 00 00 {{.*}}{ {{.*}}nop; {{.*}}nop }
// CHECK-NOT: add
// CHECK-NOT: ld
// CHECK-NOT: st
// CHECK-NOT: mul
// CHECK-NOT: jal
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>

.byte 0x07, 0x00, 0x00, 0x00, 0x00, 0x00
.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
