# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — asm→obj→objdump round-trip for add64 in a Format E parcel.

# REGRESSION: `{ add64 d0, d1, d2 }` must assemble to a single 12-byte Format E
# parcel and disassemble back to add64 with the same operands.
#
# Companion flex-bytes-alu64.s pins the same product oracle. Width is
# EncodedBytes=12 (not retired 16-byte / 8-byte paths) and the mnemonic
# survives disassembly without <unknown>/<?>. Exact product bytes are part of
# the assertion so a silent re-encode cannot stay green.
#
# Filename retains historical d385-bundle128-* stem; product is Format E only.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 0b 04 21 00 00 00 00 00 00 00 00 { add64{{.*}}d0, d1, d2
# CHECK-NOT: <?>
# CHECK-NOT: <unknown>

{ add64 d0, d1, d2 }
