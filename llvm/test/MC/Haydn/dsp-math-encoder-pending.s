# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — EXP2/LOG2/RECIP/SQRT encode→obj→disasm (encoders landed).
# Converted from parse-only/show-encoding to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).
# Fail-closed: no positive ar_sel=2/3, all-zero product-NOP, or golden-unspecified branch-scale invent.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 4f 52 81 00 00 00 00 00 00 00 00 00{{.*}}exp2
# CHECK: {{.*}}c: 4f 42 91 01 00 00 00 00 00 00 00 00{{.*}}log2
# CHECK: {{.*}}18: 4f 62 a1 02 00 00 00 00 00 00 00 00{{.*}}recip
# CHECK: {{.*}}24: 4f 72 b1 03 00 00 00 00 00 00 00 00{{.*}}sqrt

exp2	r0, r1
log2	r2, r3
recip	r4, r5
sqrt	r6, r7
