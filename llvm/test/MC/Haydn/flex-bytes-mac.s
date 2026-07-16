// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target
//
// REGRESSION TEST (/ Stage-2 Bundle128 MAC FU, updated for /):
// the 16 LE bytes emitted by the encoder for `x2mula32 d1, d2, d3, d1`
// (Path-B 4-operand form) MUST decode to the same mnemonic + operands
// AND occupy exactly 16 bytes.
//
// === BYTE DERIVATION (128-bit-only Flex encoder, live-verified) ===
//
// Under the encoder is 128-bit-only Flex. The live `llvm-mc -show-encoding`
// for `{ x2mula32 d1, d2, d3, d1 }` produces:
// 00 00 00 00 00 00 13 12 00 60 85 00 00 00 00 00
//
// X2MULA32 is a destructive-accumulator MAC (Path B: outs rtd1, rtd2;
// ins rsd1, rsd2). The 4th operand `rtd2=d1` (tied accumulator) is now
// printed in the 4-operand asm form.
//
// NO XFAIL — Stage-2 MAC (X2MULA32) is migrated to Bundle128.

// CHECK-LABEL: <.text>:
// The byte-CHECK asserts BOTH the exact 16 bytes AND the width (16 bytes
// shown, not 8 — a regression to legacy 8-byte slot-OR would show 8 bytes
// at this offset and a different op at 0x08).
// CHECK: 0: 00 00 00 00 00 00 13 12 00 60 85 00 00 00 00 00
// CHECK: x2mula32 d1, d2, d3, d1
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>

.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x12
.byte 0x00, 0x60, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00
