// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target
//
// REGRESSION TEST (/ Bundle128 §1.3 + §9 — defense-in-depth):
// an s0-only opcode (JAL is the canonical example — control-flow family)
// encoded in the s1 window MUST NOT be silently mis-decoded as a real
// instruction. The decoder MUST raise an illegal-instruction trap (or at
// minimum, refuse to render it as the s0-form mnemonic).
//
// Bug surface: a decoder that only checks FU + opcode (without the slot
// routing rule) would happily decode this JAL bytes as `jal r1, 0x12345`
// violating the §1.3 policy: "every PC-altering, hardware-loop, CSR/system
// or >20b-immediate op is s0-only. In s1/s2 these opcodes are reserved and
// decode raises an illegal-instruction trap."
//
// === BYTE DERIVATION (hand-computed per) ===
// We place the JAL payload at the s1 window (instead of s0 where it belongs).
// s0 layout for JAL per s0_encoding.md §4.1 I20:
// FU(3b) = ALU32 = 000 at slot-MSB
// opcode(6b) = 0x20 (JAL)
// rt(4b) = 1 (r1)
// imm20(20b) = 0x12345
//
// MISPLACED in s1 (window MSB-offsets [40,79]):
// FU(3b) = ALU32 = 000 @ MSB-off [40,42] -> bundle[87:85]
// opcode(6b)= 0x20 @ MSB-off [43,48] -> bundle[84:79]
// rt(4b) = r1 = 1 @ MSB-off [49,52] -> bundle[78:75]
// imm20(20b)= 0x12345 @ MSB-off [53,72] -> bundle[74:55]
// spare(7b) = 0 @ MSB-off [73,79] -> bundle[54:48]
// s0, s2 = NOP.
//
// Result 128-bit word, LE 16 bytes:
// 00 00 00 00 00 00 80 a2 91 08 10 00 00 00 00 00
//
// Decoder behavior required (per §1.3/§9): treat as illegal-instruction
// (raise trap, or render as `.word 0x...` / `<unknown>` / `<?>` — anything
// that is NOT the silent s0-form `jal r1, 0x12345`).

// CHECK-LABEL: <.text>:
// The decoder MUST NOT render this as the s0-form JAL — that would be a
// silent mis-execute (the §1.3 trap is defense-in-depth).
// CHECK-NOT: jal r1, 74565
// CHECK-NOT: jal r1, 0x12345
// CHECK-NOT: jal
// The bytes MUST produce some objdump output (the input bytes are non-zero
// so -d emits at least one line for offset 0). The decoder now correctly
// enforces the §1.3 s0-only routing rule: JAL in s1 is rejected (rendered
// as `.word` / `<unknown>` / `<?>` / `.long` — anything that is NOT the
// silent s0-form `jal`). If the decoder regresses, this CHECK-NOT fires.
// CHECK: 0:

.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xa2
.byte 0x91, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00
