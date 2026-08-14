# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — LD32/s_lw must not print as jal after encode→obj→disasm.
# Converted from parse-only/show-encoding to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).
# Fail-closed: no positive ar_sel=2/3, all-zero product-NOP, or golden-unspecified branch-scale invent.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 87 43 03 01 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}c: 87 43 23 03 01 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}18: 87 43 43 85 03 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}24: 87 43 c3 40 02 00 00 00 00 00 00 00{{.*}}ld32

ld32 r0, r1, 0

ld32 r2, r3, 16

ld32 r4, r5, -8

ld32 r12, r0, 100
