# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

# Role: object — Comprehensive load/store instruction test.

# Comprehensive load/store instruction test.
# Only includes instructions actually defined in HaydnInstrInfo.td.
# NOTE: LDM, STM, LD32.P, LD32.M, ST32.P, ST32.M, LD64, LL32, SC32,
# PREFETCH are NOT defined — removed.

#===----------------------------------------------------------------------===
# Load Operations (Slot 0)
#===----------------------------------------------------------------------===

# CHECK: ld32 r0, r1, 0

LD32 R0, R1, 0

# CHECK: ld32 r2, r3, 4
LD32 R2, R3, 4

# CHECK: ld32 r4, r5, -8
LD32 R4, R5, -8

# CHECK: ld32 r6, r7, 1024
LD32 R6, R7, 1024

# CHECK: ld16 r8, r9, 0
LD16 R8, R9, 0

# CHECK: ld16 r10, r11, 2
LD16 R10, R11, 2

# CHECK: ld16 r12, r0, -4
LD16 R12, R0, -4

# CHECK: ld8 r1, r2, 0
LD8 R1, R2, 0

# CHECK: ld8 r3, r4, 1
LD8 R3, R4, 1

# CHECK: ld8 r5, r6, -2
LD8 R5, R6, -2

# CHECK: ldu16 r7, r8, 0
LDU16 R7, R8, 0

# CHECK: ldu16 r9, r10, 2
LDU16 R9, R10, 2

# CHECK: ldu16 r11, r12, 256
LDU16 R11, R12, 256

# CHECK: ldu8 r0, r1, 0
LDU8 R0, R1, 0

# CHECK: ldu8 r2, r3, 1
LDU8 R2, R3, 1

# CHECK: ldu8 r4, r5, 16
LDU8 R4, R5, 16

#===----------------------------------------------------------------------===
# Store Operations (Slot 0)
#===----------------------------------------------------------------------===

# CHECK: st32 r6, r7, 0
ST32 R6, R7, 0

# CHECK: st32 r8, r9, 4
ST32 R8, R9, 4

# CHECK: st32 r10, r11, -8
ST32 R10, R11, -8

# CHECK: st32 r12, r0, 2048
ST32 R12, R0, 2048

# CHECK: st16 r1, r2, 0
ST16 R1, R2, 0

# CHECK: st16 r3, r4, 2
ST16 R3, R4, 2

# CHECK: st16 r5, r6, -4
ST16 R5, R6, -4

# CHECK: st16 r7, r8, 512
ST16 R7, R8, 512

# CHECK: st8 r9, r10, 0
ST8 R9, R10, 0

# CHECK: st8 r11, r12, 1
ST8 R11, R12, 1

# CHECK: st8 r0, r1, -2
ST8 R0, R1, -2

# CHECK: st8 r2, r3, 64
ST8 R2, R3, 64

#===----------------------------------------------------------------------===
# DR64 Load/Store
#===----------------------------------------------------------------------===

# LD64 (internal name LD64_S1, Slot 1) and ST64 (Slot 0) for 64-bit data registers.
# The asm mnemonic is "ld64" (not "ld64").

# CHECK: ld64 d0, r7, 0
LD64 D0, R7, 0

# CHECK: ld64 d1, r8, 8
LD64 D1, R8, 8

# CHECK: st64 d2, r9, 0
ST64 D2, R9, 0

# CHECK: st64 d3, r10, -16
ST64 D3, R10, -16
