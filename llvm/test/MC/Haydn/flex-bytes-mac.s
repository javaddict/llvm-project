// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target
//
// REGRESSION TEST: hand-computed bytes for `x2mula32 d1, d2, d3, d1` MUST
// decode to the same mnemonic + operands AND occupy exactly 12 bytes.
//
// The previous revision of this file said its "byte derivation" was the live
// `llvm-mc -show-encoding` output. That made it a record of what the encoder
// did rather than a check on it, which is exactly what the round-trip gate
// already does and what § 5.4 warns is not evidence. The bytes below are
// derived from format_e_bit_layout_v2.json by hand instead, so the encoder
// gets no say in what this test expects.
//
// === BYTE DERIVATION (hand-computed from format_e_bit_layout_v2.json) ===
//
//   bit[2:0]   = 0b111    format indicator
//   bit[3]     = 0        entry_num: 2 entries
//   bit[5:4]   = 0        reserved
//   entry0 = bit[50:6]:
//     mapping    bit[7:6]   = 0b01    -> MAC0
//     type_code  bit[9:8]   = 0b10    -> RRR
//     opcode     bit[19:16] = 0x02    -> X2MULA32
//     dest1 rtd1 bit[23:20] = 1       -> d1
//     dest2 rtd2 bit[27:24] = 2       -> d2
//     src1  rsd1 bit[31:28] = 3       -> d3
//     src2  rsd2 bit[35:32] = 1       -> d1
//   entry1 = bit[91:51]:
//     mapping    bit[52:51] = 0b00    -> ALU1
//     type_code  bit[53]    = 0       -> NOP
//
//   The operand order is the database's: `X2MULA32 rtd1, rtd2, rsd1, rsd2`,
//   two destinations first. The old comment here called the FOURTH operand
//   the tied accumulator; it is the SECOND. X2MULA32 accumulates into both
//   destinations (`rtd1 += rsd1[63:32]*rsd2[63:32]`, `rtd2 += the low
//   halves`), so both are read as well as written, and the bit layout
//   interleaves them with the sources rather than following the asm order.
//
//   Little-endian 12 bytes: 47 02 12 32 01 00 00 00 00 00 00 00

// CHECK-LABEL: <.text>:
// CHECK: 0: 47 02 12 32 01 00 00 00 00 00 00 00 {{.*}}x2mula32{{.*}}d1, d2, d3, d1
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>

.byte 0x47, 0x02, 0x12, 0x32, 0x01, 0x00
.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
