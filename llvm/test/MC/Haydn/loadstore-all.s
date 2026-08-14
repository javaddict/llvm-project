# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Comprehensive load/store instruction test.
# Converted from parse-only to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 87 43 03 01 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}c: 87 43 23 43 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}18: 87 43 43 85 03 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}24: 87 43 63 07 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}30: 87 43 84 09 00 00 00 00 00 00 00 00{{.*}}ld16
# CHECK: {{.*}}3c: 87 43 a4 2b 00 00 00 00 00 00 00 00{{.*}}ld16
# CHECK: {{.*}}48: 87 43 c4 c0 03 00 00 00 00 00 00 00{{.*}}ld16
# CHECK: {{.*}}54: 87 43 16 02 00 00 00 00 00 00 00 00{{.*}}ld8
# CHECK: {{.*}}60: 87 43 36 14 00 00 00 00 00 00 00 00{{.*}}ld8
# CHECK: {{.*}}6c: 87 43 56 e6 03 00 00 00 00 00 00 00{{.*}}ld8
# CHECK: {{.*}}78: 87 43 75 08 00 00 00 00 00 00 00 00{{.*}}ldu16
# CHECK: {{.*}}84: 87 43 95 2a 00 00 00 00 00 00 00 00{{.*}}ldu16
# CHECK: {{.*}}90: 87 43 b5 0c 00 00 00 00 00 00 00 00{{.*}}ldu16
# CHECK: {{.*}}9c: 87 43 07 01 00 00 00 00 00 00 00 00{{.*}}ldu8
# CHECK: {{.*}}a8: 87 43 27 13 00 00 00 00 00 00 00 00{{.*}}ldu8
# CHECK: {{.*}}b4: 87 43 47 05 01 00 00 00 00 00 00 00{{.*}}ldu8
# CHECK: {{.*}}c0: 87 43 6b 07 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}cc: 87 43 8b 49 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}d8: 87 43 ab 8b 03 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}e4: 87 43 cb 00 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}f0: 87 43 1c 02 00 00 00 00 00 00 00 00{{.*}}st16
# CHECK: {{.*}}fc: 87 43 3c 24 00 00 00 00 00 00 00 00{{.*}}st16
# CHECK: {{.*}}108: 87 43 5c c6 03 00 00 00 00 00 00 00{{.*}}st16
# CHECK: {{.*}}114: 87 43 7c 08 00 00 00 00 00 00 00 00{{.*}}st16
# CHECK: {{.*}}120: 87 43 9e 0a 00 00 00 00 00 00 00 00{{.*}}st8
# CHECK: {{.*}}12c: 87 43 be 1c 00 00 00 00 00 00 00 00{{.*}}st8
# CHECK: {{.*}}138: 87 43 0e e1 03 00 00 00 00 00 00 00{{.*}}st8
# CHECK: {{.*}}144: 87 43 2e 03 00 00 00 00 00 00 00 00{{.*}}st8
# CHECK: {{.*}}150: 87 43 02 07 00 00 00 00 00 00 00 00{{.*}}ld64
# CHECK: {{.*}}15c: 87 43 12 88 00 00 00 00 00 00 00 00{{.*}}ld64
# CHECK: {{.*}}168: 87 43 2a 09 00 00 00 00 00 00 00 00{{.*}}st64
# CHECK: {{.*}}174: 87 43 3a 0a 03 00 00 00 00 00 00 00{{.*}}st64
# CHECK-NOT: <unknown>

# Comprehensive load/store instruction test.
# Only includes instructions actually defined in HaydnInstrInfo.td.
# NOTE: LDM, STM, LD32.P, LD32.M, ST32.P, ST32.M, LD64, LL32, SC32,
# PREFETCH are NOT defined — removed.

#===----------------------------------------------------------------------===
# Load Operations (Slot 0)
#===----------------------------------------------------------------------===


LD32 R0, R1, 0

LD32 R2, R3, 4

LD32 R4, R5, -8

LD32 R6, R7, 1024

LD16 R8, R9, 0

LD16 R10, R11, 2

LD16 R12, R0, -4

LD8 R1, R2, 0

LD8 R3, R4, 1

LD8 R5, R6, -2

LDU16 R7, R8, 0

LDU16 R9, R10, 2

LDU16 R11, R12, 256

LDU8 R0, R1, 0

LDU8 R2, R3, 1

LDU8 R4, R5, 16

#===----------------------------------------------------------------------===
# Store Operations (Slot 0)
#===----------------------------------------------------------------------===

ST32 R6, R7, 0

ST32 R8, R9, 4

ST32 R10, R11, -8

ST32 R12, R0, 2048

ST16 R1, R2, 0

ST16 R3, R4, 2

ST16 R5, R6, -4

ST16 R7, R8, 512

ST8 R9, R10, 0

ST8 R11, R12, 1

ST8 R0, R1, -2

ST8 R2, R3, 64

#===----------------------------------------------------------------------===
# DR64 Load/Store
#===----------------------------------------------------------------------===

# LD64 (internal name LD64_S1, Slot 1) and ST64 (Slot 0) for 64-bit data registers.
# The asm mnemonic is "ld64" (not "ld64").

LD64 D0, R7, 0

LD64 D1, R8, 8

ST64 D2, R9, 0

ST64 D3, R10, -16
