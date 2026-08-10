# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Format E product oracle: `{ add64 d0, d1, d2 }` encodes as a
# 12-byte parcel and disassembles back to add64.

// REGRESSION TEST (Format E ALU64): product assembler→encoder path for
// `add64 d0, d1, d2` MUST:
// (a) round-trip through objdump as add64 with the same operands
// (b) occupy EXACTLY 12 bytes in .text (Format E EncodedBytes)
//
// Golden bytes from live `llvm-mc -show-encoding` under production E96 profile.
// Do not invent encodings.

// CHECK-LABEL: <.text>:
// CHECK: {{.*}}0: 07 0b 04 21 00 00 00 00 00 00 00 00  { nop; add64{{.*}}d0, d1, d2
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>

{ add64 d0, d1, d2 }
