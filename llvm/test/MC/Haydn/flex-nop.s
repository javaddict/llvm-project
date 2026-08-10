// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target

# Role: object — fail-closed idle/NOP contract: an all-zero 12-byte parcel is
# NOT a product FU instruction. Do not invent a product idle header or claim
# all-zero bytes are the approved product NOP (golden idle still unspecified).

// Fail-closed residual: all-zero EncodedBytes=12 must not decode as a real
// FU op (add/ld/st/mul/jal). Idle completion pattern is residual — no positive
// `{ nop }` / idle-header invent as product law.

// CHECK-LABEL: <.text>:
// CHECK: <unknown>
// CHECK-NOT: add32
// CHECK-NOT: ld32
// CHECK-NOT: st32
// CHECK-NOT: mul
// CHECK-NOT: jal

.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
