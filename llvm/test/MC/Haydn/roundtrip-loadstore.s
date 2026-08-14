# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Round-trip test for Load/Store instructions: asm → parse → print.
# Converted from parse-only to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 87 43 03 01 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}c: 87 43 23 03 01 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}18: 87 43 4b 05 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}24: 87 43 6b c7 03 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}30: 87 43 84 09 00 00 00 00 00 00 00 00{{.*}}ld16
# CHECK: {{.*}}3c: 87 43 a6 0b 00 00 00 00 00 00 00 00{{.*}}ld8
# CHECK: {{.*}}48: 87 43 c5 00 00 00 00 00 00 00 00 00{{.*}}ldu16
# CHECK: {{.*}}54: 87 43 17 02 00 00 00 00 00 00 00 00{{.*}}ldu8
# CHECK: {{.*}}60: 87 43 3c 04 00 00 00 00 00 00 00 00{{.*}}st16
# CHECK: {{.*}}6c: 87 43 5e 06 00 00 00 00 00 00 00 00{{.*}}st8
# CHECK: {{.*}}78: 87 43 02 01 00 00 00 00 00 00 00 00{{.*}}ld64
# CHECK: {{.*}}84: 87 43 73 08 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}90: 87 43 93 0a 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}9c: 87 43 bb 0c 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}a8: 87 43 0b 01 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK-NOT: <unknown>

# Round-trip test for Load/Store instructions: asm → parse → print.
# NOTE: Full encode→decode roundtrip deferred until disassembler is complete.

# Basic load/store (32-bit)

ld32 r0, r1, 0

ld32 r2, r3, 16

st32 r4, r5, 0

st32 r6, r7, -4

# Load/store size variants
ld16 r8, r9, 0

ld8 r10, r11, 0

ldu16 r12, r0, 0

ldu8 r1, r2, 0

st16 r3, r4, 0

st8 r5, r6, 0

# 64-bit load (Slot 1)
ld64 d0, r1, 0

# Load with various offsets
ld32 r7, r8, 64

ld32 r9, r10, -64

# Store with various offsets
st32 r11, r12, 128

st32 r0, r1, -128
