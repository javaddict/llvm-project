// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target
//
// REGRESSION TEST (/ Bundle128 §4 NOP encoding): an all-zero 128-bit
// bundle MUST decode as a NOP — no FU sub-decoder clocks, no spurious
// operand fetch, no `<?>` placeholder. This is the canonical §4 NOP rule.
//
// Bug surface: any decoder regression that reads opcode 0 in an FU as a real
// instruction (instead of NOP) breaks the §4 contract. The R1 -> R2 spec
// fix (encoding_manual_flex.md §11 codex #9) replaced the conflicting
// "opcode 0 is universal NOP" rule with the unambiguous "all-zero slot =
// NOP" rule. This test pins that rule.
//
// === BYTE DERIVATION (hand-computed) ===
// All 128 bits zero per encoding_manual_flex.md §4:
// "Bundle NOP = all 128 bits zero."
// s0 = 0x0000_0000_0000 (48'b0)
// s1 = 0x00_0000_0000 (40'b0)
// s2 = 0x00_0000_0000 (40'b0)
// Concatenated: 0x0000_0000_0000_0000_0000_0000_0000_0000 (128'b0)
// Little-endian 16 bytes: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
//
// Harness note: earlier --disassemble --hex form was broken (read // RUN
// lines as hex). This test uses.byte directives + objdump -d.
//
// NO XFAIL — NOP is an architectural invariant (§4); must pass from day
// one of the Bundle128 wiring.

// CHECK-LABEL: <.text>:
// the all-zero 16 bytes decode as ONE 128-bit Bundle128 composite with
// 3 NOP slots (s0/s1/s2 all-zero = NOP per §4). The pre- variable-width
// decoder produced 7× { nop } at 2-byte intervals; unified to Bundle128.
// CHECK: 0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 { nop; nop; nop }
// CHECK-NOT: add
// CHECK-NOT: ld
// CHECK-NOT: st
// CHECK-NOT: mul
// CHECK-NOT: jal
// CHECK-NOT: <?>

.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
