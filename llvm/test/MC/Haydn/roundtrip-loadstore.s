# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

# Role: object — Round-trip test for Load/Store instructions: asm → parse → print.

# Round-trip test for Load/Store instructions: asm → parse → print.
# NOTE: Full encode→decode roundtrip deferred until disassembler is complete.

# Basic load/store (32-bit)
# CHECK: ld32 r0, r1, 0

ld32 r0, r1, 0

# CHECK: ld32 r2, r3, 16
ld32 r2, r3, 16

# CHECK: st32 r4, r5, 0
st32 r4, r5, 0

# CHECK: st32 r6, r7, -4
st32 r6, r7, -4

# Load/store size variants
# CHECK: ld16 r8, r9, 0
ld16 r8, r9, 0

# CHECK: ld8 r10, r11, 0
ld8 r10, r11, 0

# CHECK: ldu16 r12, r0, 0
ldu16 r12, r0, 0

# CHECK: ldu8 r1, r2, 0
ldu8 r1, r2, 0

# CHECK: st16 r3, r4, 0
st16 r3, r4, 0

# CHECK: st8 r5, r6, 0
st8 r5, r6, 0

# 64-bit load (Slot 1)
# CHECK: ld64 d0, r1, 0
ld64 d0, r1, 0

# Load with various offsets
# CHECK: ld32 r7, r8, 64
ld32 r7, r8, 64

# CHECK: ld32 r9, r10, -64
ld32 r9, r10, -64

# Store with various offsets
# CHECK: st32 r11, r12, 128
st32 r11, r12, 128

# CHECK: st32 r0, r1, -128
st32 r0, r1, -128
