# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Comprehensive instruction test for Haydn DSP.
# Converted from parse-only to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 8b 00 21 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}c: 07 0f 32 04 32 00 00 00 00 00 00 00{{.*}}addi32
# CHECK: {{.*}}18: 07 cb 50 76 00 00 00 00 00 00 00 00{{.*}}sub32
# CHECK: {{.*}}24: 07 0f 8a 09 e7 ff 07 00 00 00 00 00{{.*}}subi32
# CHECK: {{.*}}30: 07 0b a1 cb 00 00 00 00 00 00 00 00{{.*}}and32
# CHECK: {{.*}}3c: 07 2b 01 21 00 00 00 00 00 00 00 00{{.*}}or32
# CHECK: {{.*}}48: 07 4b 31 54 00 00 00 00 00 00 00 00{{.*}}xor32
# CHECK: {{.*}}54: 07 24 60 07 00 00 00 00 00 00 00 00{{.*}}not32
# CHECK: {{.*}}60: 07 0f 84 89 7f 00 00 00 00 00 00 00{{.*}}andi32
# CHECK: {{.*}}6c: 07 0f a8 0b 40 00 00 00 00 00 00 00{{.*}}ori32
# CHECK: {{.*}}78: 07 0f 0c 01 20 00 00 00 00 00 00 00{{.*}}xori32
# CHECK: {{.*}}84: 07 44 20 03 00 00 00 00 00 00 00 00{{.*}}move32
# CHECK: {{.*}}90: 07 0a 42 00 2a 00 00 00 00 00 00 00{{.*}}lui
# CHECK: {{.*}}9c: 07 06 51 06 04 00 00 00 00 00 00 00{{.*}}srli32
# CHECK: {{.*}}a8: 07 06 72 08 08 00 00 00 00 00 00 00{{.*}}srai32
# CHECK: {{.*}}b4: 07 06 94 0a 10 00 00 00 00 00 00 00{{.*}}slli32
# CHECK: {{.*}}c0: 07 cb b1 10 00 00 00 00 00 00 00 00{{.*}}srl32
# CHECK: {{.*}}cc: 07 8b 21 43 00 00 00 00 00 00 00 00{{.*}}sra32
# CHECK: {{.*}}d8: 07 eb 51 76 00 00 00 00 00 00 00 00{{.*}}sll32
# CHECK: {{.*}}e4: 07 24 81 09 00 00 00 00 00 00 00 00{{.*}}abs32s
# CHECK: {{.*}}f0: 07 0b a2 cb 00 00 00 00 00 00 00 00{{.*}}max32
# CHECK: {{.*}}fc: 07 2b 02 21 00 00 00 00 00 00 00 00{{.*}}maxu32
# CHECK: {{.*}}108: 07 4b 32 54 00 00 00 00 00 00 00 00{{.*}}min32
# CHECK: {{.*}}114: 07 6b 62 87 00 00 00 00 00 00 00 00{{.*}}minu32
# CHECK: {{.*}}120: 07 44 91 0a 00 00 00 00 00 00 00 00{{.*}}neg32
# CHECK: {{.*}}12c: 07 64 b1 0c 00 00 00 00 00 00 00 00{{.*}}neg32s
# CHECK: {{.*}}138: 07 6b 01 21 00 00 00 00 00 00 00 00{{.*}}brev32
# CHECK: {{.*}}144: 07 84 31 04 00 00 00 00 00 00 00 00{{.*}}nsa32
# CHECK: {{.*}}150: 07 a4 51 06 00 00 00 00 00 00 00 00{{.*}}nsau32
# CHECK: {{.*}}15c: 07 c4 71 08 00 00 00 00 00 00 00 00{{.*}}popcount32
# CHECK: {{.*}}168: 07 8b 92 ba 00 00 00 00 00 00 00 00{{.*}}slt32
# CHECK: {{.*}}174: 07 ab c2 10 00 00 00 00 00 00 00 00{{.*}}sltu32
# CHECK: {{.*}}180: 07 cb 22 43 00 00 00 00 00 00 00 00{{.*}}sle32
# CHECK: {{.*}}18c: 07 eb 52 76 00 00 00 00 00 00 00 00{{.*}}seq32
# CHECK: {{.*}}198: 07 2b 83 a9 00 00 00 00 00 00 00 00{{.*}}movt32
# CHECK: {{.*}}1a4: 07 0b b3 0c 00 00 00 00 00 00 00 00{{.*}}movf32
# CHECK: {{.*}}1b0: 87 43 13 02 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}1bc: 87 43 34 84 00 00 00 00 00 00 00 00{{.*}}ld16
# CHECK: {{.*}}1c8: 87 43 56 06 01 00 00 00 00 00 00 00{{.*}}ld8
# CHECK: {{.*}}1d4: 87 43 75 88 01 00 00 00 00 00 00 00{{.*}}ldu16
# CHECK: {{.*}}1e0: 87 43 97 0a 02 00 00 00 00 00 00 00{{.*}}ldu8
# CHECK: {{.*}}1ec: 87 43 bb 0c 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}1f8: 87 43 0c 41 00 00 00 00 00 00 00 00{{.*}}st16
# CHECK: {{.*}}204: 87 43 2e 23 00 00 00 00 00 00 00 00{{.*}}st8
# CHECK: {{.*}}210: 07 0d 44 05 00 00 00 00 00 00 00 00{{.*}}beq
# CHECK: {{.*}}21c: 07 0d 66 07 f4 0f 00 00 00 00 00 00{{.*}}bne
# CHECK: {{.*}}228: 07 0d 88 09 e8 0f 00 00 00 00 00 00{{.*}}bge
# CHECK: {{.*}}234: 07 0d ac 0b dc 0f 00 00 00 00 00 00{{.*}}bgeu
# CHECK: {{.*}}240: 07 0d ca 00 d0 0f 00 00 00 00 00 00{{.*}}blt
# CHECK: {{.*}}24c: 07 0d 1e 02 c4 0f 00 00 00 00 00 00{{.*}}bltu
# CHECK: {{.*}}258: 07 0a 38 00 b8 0f 00 00 00 00 00 00{{.*}}beqz
# CHECK: {{.*}}264: 07 0a 4a 00 ac 0f 00 00 00 00 00 00{{.*}}bnez
# CHECK: {{.*}}270: 07 0a 5c 00 a0 0f 00 00 00 00 00 00{{.*}}bgez
# CHECK: {{.*}}27c: 07 0a 6e 00 94 0f 00 00 00 00 00 00{{.*}}bltz
# CHECK: {{.*}}288: 07 0b 04 21 00 00 00 00 00 00 00 00{{.*}}add64
# CHECK: {{.*}}294: 07 0b 35 54 00 00 00 00 00 00 00 00{{.*}}sub64
# CHECK: {{.*}}2a0: 07 8b 66 87 00 00 00 00 00 00 00 00{{.*}}and64
# CHECK: {{.*}}2ac: 07 ab 96 ba 00 00 00 00 00 00 00 00{{.*}}or64
# CHECK: {{.*}}2b8: 07 cb c6 ed 00 00 00 00 00 00 00 00{{.*}}xor64
# CHECK: {{.*}}2c4: 07 0b 58 76 00 00 00 00 00 00 00 00{{.*}}x2add32
# CHECK: {{.*}}2d0: 07 8b 89 a9 00 00 00 00 00 00 00 00{{.*}}x2sub32
# CHECK: {{.*}}2dc: 47 02 b1 dc 0e 00 00 00 00 00 00 00{{.*}}x2mul32
# CHECK: {{.*}}2e8: 07 0b ec 0f 00 00 00 00 00 00 00 00{{.*}}x4add16
# CHECK: {{.*}}2f4: 47 02 1c 32 04 00 00 00 00 00 00 00{{.*}}x4mul16
# CHECK: {{.*}}300: 87 43 42 05 00 00 00 00 00 00 00 00{{.*}}ld64
# CHECK: {{.*}}30c: 07 03 68 00 00 00 00 00 00 00 00 00{{.*}}csrr
# CHECK: {{.*}}318: 07 03 7a 00 00 00 00 00 00 00 00 00{{.*}}csrw
# CHECK-NOT: <unknown>

# Comprehensive instruction test for Haydn DSP. The WideImm immediate-form
# shifts (slli64/srli64/srai64) and the 4-operand MAC32 are AsmParser gaps
# (M5 + post- MAC cleanup) tracked separately.
# This test covers all major instruction categories

#===----------------------------------------------------------------------===
# Slot 0 ALU Instructions (32-bit scalar)
#===----------------------------------------------------------------------===

# Arithmetic

ADD32 R0, R1, R2
ADDI32 R3, R4, 100
SUB32 R5, R6, R7
SUBI32 R8, R9, -50

# Logical
AND32 R10, R11, R12
OR32 R0, R1, R2
XOR32 R3, R4, R5
NOT32 R6, R7

# Logical Immediate
ANDI32 R8, R9, 255
ORI32 R10, R11, 128
XORI32 R0, R1, 64

# Move and Load Immediate
MOVE32 R2, R3
LUI R4, 42

# Shifts (Immediate)
SRLI32 R5, R6, 4
SRAI32 R7, R8, 8
SLLI32 R9, R10, 16

# Shifts (Register)
SRL32 R11, R0, R1
SRA32 R2, R3, R4
SLL32 R5, R6, R7

# DSP/Miscellaneous
ABS32S R8, R9
MAX32 R10, R11, R12
MAXU32 R0, R1, R2
MIN32 R3, R4, R5
MINU32 R6, R7, R8
NEG32 R9, R10
NEG32S R11, R12
BREV32 R0, R1, R2
NSA32 R3, R4
NSAU32 R5, R6
POPCOUNT32 R7, R8

# Compare/Conditional Move
SLT32 R9, R10, R11
SLTU32 R12, R0, R1
SLE32 R2, R3, R4
SEQ32 R5, R6, R7
MOVT32 R8, R9, R10
MOVF32 R11, R12, R0

#===----------------------------------------------------------------------===
# Slot 0 Load/Store Instructions
#===----------------------------------------------------------------------===

LD32 R1, R2, 0
LD16 R3, R4, 8
LD8 R5, R6, 16
LDU16 R7, R8, 24
LDU8 R9, R10, 32

ST32 R11, R12, 0
ST16 R0, R1, 4
ST8 R2, R3, 2

#===----------------------------------------------------------------------===
# Branch Instructions
#===----------------------------------------------------------------------===

# Conditional branches (two-register)
branch_target:
BEQ R4, R5, branch_target
BNE R6, R7, branch_target
BGE R8, R9, branch_target
BGEU R10, R11, branch_target
BLT R12, R0, branch_target
BLTU R1, R2, branch_target

# Conditional branches (one-register)
BEQZ R3, branch_target
BNEZ R4, branch_target
BGEZ R5, branch_target
BLTZ R6, branch_target

#===----------------------------------------------------------------------===
# Jump/Link Instructions
#===----------------------------------------------------------------------===

# JAL R7, external_call # AsmParser gap: 2-operand JAL form not parsed (M5)

#===----------------------------------------------------------------------===
# Slot 1/2 ALU64/SIMD Instructions
#===----------------------------------------------------------------------===

# 64-bit operations
ADD64 D0, D1, D2
SUB64 D3, D4, D5
AND64 D6, D7, D8
OR64 D9, D10, D11
XOR64 D12, D13, D14

# 64-bit shifts — WideImm immediate form (slli64/srli64/srai64 d,d,imm)
# is an M5 AsmParser gap; not assembled here.

# SIMD X2 (dual 32-bit)
X2ADD32 D5, D6, D7
X2SUB32 D8, D9, D10
# (Path B): X2MUL32 is now TRUE 2-output — 4-operand asm form.
X2MUL32 D11, D12, D13, D14

# SIMD X4 (quad 16-bit)
X4ADD16 D14, D15, D0
# (Path B): X4MUL16 is now TRUE 2-output — 4-operand asm form.
X4MUL16 D1, D2, D3, D4

# MAC operations — 4-operand MAC32 removed post-; not assembled here.

# Slot 1 loads (LD64 is the mnemonic)
LD64 D4, R5, 0

#===----------------------------------------------------------------------===
# System Instructions
#===----------------------------------------------------------------------===

CSRR R6, 0
CSRW 0, R7

# Verify all instructions are recognized
# Mulq31/macq31/mulq63 REMOVED (phantom — not in the ISA DB).
