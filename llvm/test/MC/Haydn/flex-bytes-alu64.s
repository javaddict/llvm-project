# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: BYTE-PINNED + WIDTH-PINNED oracle. Hand-computed bytes for
# `add64 d0, d1, d2` MUST:
#   (a) round-trip through the disassembler as `add64 d0, d1, d2`
#   (b) occupy EXACTLY 12 bytes in .text — the width assertion is the trap
#       that hid the transitional-form bug for the whole Bundle128 arc, and
#       12-vs-16 is the same assertion one format later.
#
# The bytes are derived from format_e_bit_layout_v2.json by hand, not copied
# from `-show-encoding`. That independence is the whole point: the round trip
# gate checks the encoder against the decoder and cannot see a defect the two
# share.
#
# === BYTE DERIVATION (hand-computed from format_e_bit_layout_v2.json) ===
#
#   bit[2:0]   = 0b111    format indicator
#   bit[3]     = 0        entry_num: 2 entries
#   bit[5:4]   = 0        reserved
#   entry0 = bit[50:6]:
#     mapping   bit[7:6]   = 0b00      -> ALU0
#     type_code bit[12:8]  = 0b01011   -> RR
#     opcode    bit[19:13] = 0x20      -> ADD64
#     dest rtd  bit[23:20] = 0         -> d0
#     src1 rsd1 bit[27:24] = 1         -> d1
#     src2 rsd2 bit[31:28] = 2         -> d2
#     reserved  bit[50:32] = 0
#   entry1 = bit[91:51]:
#     mapping   bit[52:51] = 0b00      -> ALU1
#     type_code bit[53]    = 0         -> NOP
#
#   ADD64 shares the ALU0 RR type with ADD32 and differs only in the 7-bit
#   opcode (0x20 against 0x04), which is what makes the pair worth pinning
#   side by side: a decoder that lost the opcode width would confuse them.
#
#   Little-endian 12 bytes: 07 0b 04 21 00 00 00 00 00 00 00 00

# CHECK-LABEL: <.text>:
# CHECK: 0: 07 0b 04 21 00 00 00 00 00 00 00 00 {{.*}}add64{{.*}}d0, d1, d2
# CHECK-NOT: <?>
# CHECK-NOT: <unknown>
# CHECK-NOT: add32

.byte 0x07, 0x0b, 0x04, 0x21, 0x00, 0x00
.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
