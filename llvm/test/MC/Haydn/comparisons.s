# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Compare / select / min-max encode→obj→disasm (Format E 12-byte).
# Converted from parse-only/show-encoding to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).
# Fail-closed: no positive ar_sel=2/3, all-zero product-NOP, or golden-unspecified branch-scale invent.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 8b 02 21 00 00 00 00 00 00 00 00{{.*}}slt32
# CHECK: {{.*}}c: 07 ab 32 54 00 00 00 00 00 00 00 00{{.*}}sltu32
# CHECK: {{.*}}18: 07 cb 62 87 00 00 00 00 00 00 00 00{{.*}}sle32
# CHECK: {{.*}}24: 07 eb 92 ba 00 00 00 00 00 00 00 00{{.*}}seq32
# CHECK: {{.*}}30: 07 2b 03 21 00 00 00 00 00 00 00 00{{.*}}movt32
# CHECK: {{.*}}3c: 07 0b 33 54 00 00 00 00 00 00 00 00{{.*}}movf32
# CHECK: {{.*}}48: 07 0b 62 87 00 00 00 00 00 00 00 00{{.*}}max32
# CHECK: {{.*}}54: 07 2b 92 ba 00 00 00 00 00 00 00 00{{.*}}maxu32
# CHECK: {{.*}}60: 07 4b 02 21 00 00 00 00 00 00 00 00{{.*}}min32
# CHECK: {{.*}}6c: 07 6b 32 54 00 00 00 00 00 00 00 00{{.*}}minu32

SLT32 R0, R1, R2

# Unsigned less than
SLTU32 R3, R4, R5

# Signed less than or equal
SLE32 R6, R7, R8

# Set equal
SEQ32 R9, R10, R11

# Conditional moves
MOVT32 R0, R1, R2

MOVF32 R3, R4, R5

# Min / max
MAX32 R6, R7, R8

MAXU32 R9, R10, R11

MIN32 R0, R1, R2

MINU32 R3, R4, R5
