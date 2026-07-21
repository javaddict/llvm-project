# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

# Test comparison instructions

# Signed less than
# CHECK-LABEL: slt32 r0, r1, r2
SLT32 R0, R1, R2

# Unsigned less than
# CHECK-LABEL: sltu32 r3, r4, r5
SLTU32 R3, R4, R5

# Signed less than or equal
# CHECK-LABEL: sle32 r6, r7, r8
SLE32 R6, R7, R8

# Set equal
# CHECK-LABEL: seq32 r9, r10, r11
SEQ32 R9, R10, R11

# Test conditional move instructions

# Move if true (condition flag set)
# CHECK-LABEL: movt32 r0, r1, r2
MOVT32 R0, R1, R2

# Move if false (condition flag clear)
# CHECK-LABEL: movf32 r3, r4, r5
MOVF32 R3, R4, R5

# Test max/min operations

# Signed 32-bit max
# CHECK-LABEL: max32 r6, r7, r8
MAX32 R6, R7, R8

# Unsigned 32-bit max
# CHECK-LABEL: maxu32 r9, r10, r11
MAXU32 R9, R10, R11

# Signed 32-bit min
# CHECK-LABEL: min32 r0, r1, r2
MIN32 R0, R1, R2

# Unsigned 32-bit min
# CHECK-LABEL: minu32 r3, r4, r5
MINU32 R3, R4, R5
