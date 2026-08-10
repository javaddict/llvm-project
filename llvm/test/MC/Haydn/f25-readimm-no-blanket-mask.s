# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — F25 ReadImm no-blanket-mask: imm fields survive encode→obj→disasm.
# Converted from parse-only/show-encoding to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).
# Fail-closed: no positive ar_sel=2/3, all-zero product-NOP, or golden-unspecified branch-scale invent.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 06 14 02 05 00 00 00 00 00 00 00{{.*}}slli32
# CHECK: {{.*}}c: 07 0f 32 84 08 00 00 00 00 00 00 00{{.*}}addi32

SLLI32 R1, R2, 5

ADDI32 R3, R4, 17
