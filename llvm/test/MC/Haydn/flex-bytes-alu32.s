// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target

# Role: object — Format E product oracle: `{ add32 r1, r2, r3 }` encodes as a
# 12-byte (EncodedBytes) parcel and disassembles back to add32.

// REGRESSION TEST (Format E ALU32): product assembler→encoder path for
// `add32 r1, r2, r3` MUST:
// (a) round-trip through objdump as add32 with the same operands
// (b) occupy EXACTLY 12 bytes in .text (Format E EncodedBytes; not retired
//     16-byte Bundle128 / 8-byte Mode-0)
//
// Golden bytes from live `llvm-mc -show-encoding` under production E96 profile
// (Format E 96 cutover). Do not invent encodings.

// CHECK-LABEL: <.text>:
// CHECK: {{.*}}0: 07 8b 10 32 00 00 00 00 00 00 00 00  { nop; add32{{.*}}r1, r2, r3
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>

{ add32 r1, r2, r3 }
