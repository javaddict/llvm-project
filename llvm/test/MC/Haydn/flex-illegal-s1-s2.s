// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
// REQUIRES: haydn-registered-target

# Role: object — defense-in-depth: a retired mis-slotted JAL byte pattern must
# NOT silently decode as a product Format E JAL mnemonic.

// REGRESSION TEST: product Format E must not treat an opaque non-product
// byte stream as `jal`. This keeps the s0-only / legal-entry routing contract
// from silently accepting garbage as control-flow.
//
// Input is a fixed opaque .byte sequence (historical mis-slot probe). The
// load-bearing assertion is CHECK-NOT: jal — anything else
// (`<unknown>` / `.word` / idle fragments) is acceptable degradation.

// CHECK-LABEL: <.text>:
// CHECK-NOT: jal r1, 74565
// CHECK-NOT: jal r1, 0x12345
// CHECK-NOT: jal
// CHECK: {{.*}}0:

.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xa2
.byte 0x91, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00
