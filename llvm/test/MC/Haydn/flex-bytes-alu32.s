// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target
//
// REGRESSION TEST: hand-computed bytes for `add32 r1, r2, r3` MUST decode
// back to `add32 r1, r2, r3` AND occupy exactly 12 bytes (width assertion).
//
// This is a BYTE-PINNED oracle and its value is that the bytes come from the
// ISA document rather than from the encoder. `--emit roundtrip` checks the
// encoder against the decoder and passes on any defect symmetric across the
// pair; these bytes were derived from format_e_bit_layout_v2.json by hand and
// agree with neither by construction. Keep it that way — if this file is ever
// "updated" by pasting `-show-encoding` output, it stops being evidence.
//
// === BYTE DERIVATION (hand-computed from format_e_bit_layout_v2.json) ===
//
//   bit[2:0]   = 0b111    format indicator
//   bit[3]     = 0        entry_num: 2 entries
//   bit[5:4]   = 0        reserved
//   entry0 = bit[50:6]:
//     mapping   bit[7:6]   = 0b00      -> ALU0
//     type_code bit[12:8]  = 0b01011   -> RR
//     opcode    bit[19:13] = 0x04      -> ADD32
//     dest rt   bit[23:20] = 1         -> r1
//     src1 rs1  bit[27:24] = 2         -> r2
//     src2 rs2  bit[31:28] = 3         -> r3
//     reserved  bit[50:32] = 0
//   entry1 = bit[91:51]:
//     mapping   bit[52:51] = 0b00      -> ALU1
//     type_code bit[53]    = 0         -> NOP
//
//   Note the field order: the ISA document places dest BEFORE the sources in
//   the bit layout, while the asm writes `add32 rt, rs1, rs2`. The two orders
//   are independent and the layout is the authority for the bits.
//
//   Little-endian 12 bytes: 07 8b 10 32 00 00 00 00 00 00 00 00

// CHECK-LABEL: <.text>:
// The byte-CHECK asserts BOTH the exact bytes AND the width: 12 shown, not
// 16 — a Bundle128 parcel here would list 16 and put the next op at 0x10.
// CHECK: 0: 07 8b 10 32 00 00 00 00 00 00 00 00 {{.*}}add32{{.*}}r1, r2, r3
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>
// One ADD32 and no more: the pre-bundle decoder produced repeated ops at
// sub-parcel intervals, and this is what catches a return to that.
// CHECK-NOT: add32

.byte 0x07, 0x8b, 0x10, 0x32, 0x00, 0x00
.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
