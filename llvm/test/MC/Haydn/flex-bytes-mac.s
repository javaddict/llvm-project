// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target

# Role: object — Format E product oracle: `{ nop; x2mula32 d1, d2, d3, d1 }` encodes
# as a 12-byte parcel and disassembles as the product MAC form.

// REGRESSION TEST (Format E MAC): product assembler→encoder path for
// `x2mula32 d1, d2, d3, d1` MUST:
// (a) round-trip through objdump without <unknown>/<?>
// (b) occupy EXACTLY 12 bytes in .text (Format E EncodedBytes)
//
// Golden bytes from live `llvm-mc -show-encoding` under production E96 profile.
// Printer may collapse the tied accumulator form; pin bytes + family mnemonic.

// CHECK-LABEL: <.text>:
// CHECK: {{.*}}0: 47 02 12 32 01 00 00 00 00 00 00 00 {{.*}}x2mula32
// CHECK-NOT: <?>
// CHECK-NOT: <unknown>

{ nop; x2mula32 d1, d2, d3, d1 }
