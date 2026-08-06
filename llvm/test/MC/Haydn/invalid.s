# RUN: not llvm-mc -triple=haydn-unknown-elf %s 2>&1 | FileCheck %s

# Role: verifier — invalid register names.

#===----------------------------------------------------------------------===
# Test invalid register names
#===----------------------------------------------------------------------===

# CHECK: error: invalid operand for instruction

ADD32 R16, R0, R1

# CHECK: error: invalid operand for instruction
SUB32 R-1, R0, R1

# CHECK: error: invalid operand for instruction
ADD64 D16, D0, D1

# CHECK: error: invalid operand for instruction
AND32 RR0, R1, R2

#===----------------------------------------------------------------------===
# Test out-of-range immediates that still fail matching
# (ADDI32/ANDI32 large imm now parse; SRLI32/SLLI32 still reject.)
#===----------------------------------------------------------------------===

# CHECK: error: unknown error matching instruction
SRLI32 R0, R1, 32

# CHECK: error: unknown error matching instruction
SLLI32 R0, R1, 63

#===----------------------------------------------------------------------===
# Test wrong operand types
#===----------------------------------------------------------------------===

# CHECK: error: invalid operand for instruction
ADD32 R0, D0, R1

# CHECK: error: invalid operand for instruction
ADD64 D0, R1, D2

# CHECK: error: invalid operand for instruction
SUB32 R0, R1

# CHECK: error: invalid operand for instruction
OR32 R0, R1, R2, R3

#===----------------------------------------------------------------------===
# Test invalid instructions
#===----------------------------------------------------------------------===

# CHECK: error: invalid instruction mnemonic
invalid_op R0, R1, R2

#===----------------------------------------------------------------------===
# Test CSR instruction errors
#===----------------------------------------------------------------------===

# CHECK: error: unknown error matching instruction
CSRR R0, 256
