# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — CSRR / CSRW parse and print across CSR address range.
# Converted from parse-only to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 03 08 00 00 00 00 00 00 00 00 00{{.*}}csrr
# CHECK: {{.*}}c: 07 03 18 00 01 00 00 00 00 00 00 00{{.*}}csrr
# CHECK: {{.*}}18: 07 03 28 00 ff 00 00 00 00 00 00 00{{.*}}csrr
# CHECK: {{.*}}24: 07 03 3a 00 00 00 00 00 00 00 00 00{{.*}}csrw
# CHECK: {{.*}}30: 07 03 4a 00 0a 00 00 00 00 00 00 00{{.*}}csrw
# CHECK: {{.*}}3c: 07 03 5a 00 80 00 00 00 00 00 00 00{{.*}}csrw
# CHECK-NOT: <unknown>

#===----------------------------------------------------------------------===
# CSR read
#===----------------------------------------------------------------------===

CSRR R0, 0

CSRR R1, 1

CSRR R2, 255

#===----------------------------------------------------------------------===
# CSR write
#===----------------------------------------------------------------------===

CSRW 0, R3

CSRW 10, R4

CSRW 128, R5
