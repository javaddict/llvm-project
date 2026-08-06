// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target

# Role: object — Format E idle/NOP parcel: product idle header 0x07 + zero
# entries MUST decode as a single NOP cycle (no spurious FU mnemonics).

// REGRESSION TEST (Format E idle parcel): the GE96-01 provisional idle
// pattern (header 0x07 + zero entries, EncodedBytes=12) MUST decode as
// `{ nop }` — no FU sub-decoder clocks, no `<?>`, no real mnemonic.
//
// Product path does not accept bare `{ nop }` as an all-zero word; the
// approved idle completion is the Format E idle header form. This test pins
// the object bytes via .byte and the disassembler contract.

// CHECK-LABEL: <.text>:
// CHECK: {{.*}}0: 07 00 00 00 00 00 00 00 00 00 00 00  { nop }
// CHECK-NOT: add
// CHECK-NOT: ld
// CHECK-NOT: st
// CHECK-NOT: mul
// CHECK-NOT: jal
// CHECK-NOT: <?>

.byte 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
