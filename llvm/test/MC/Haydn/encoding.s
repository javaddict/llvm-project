# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Core ALU/LS/branch encode→obj→disasm (Format E 12-byte parcels).
# Converted from parse-only/show-encoding to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).
# Fail-closed: no positive ar_sel=2/3, all-zero product-NOP, or golden-unspecified branch-scale invent.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 8b 00 21 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}c: 07 cb 30 54 00 00 00 00 00 00 00 00{{.*}}sub32
# CHECK: {{.*}}18: 07 0b 61 87 00 00 00 00 00 00 00 00{{.*}}and32
# CHECK: {{.*}}24: 07 2b 91 ba 00 00 00 00 00 00 00 00{{.*}}or32
# CHECK: {{.*}}30: 07 4b c1 10 00 00 00 00 00 00 00 00{{.*}}xor32
# CHECK: {{.*}}3c: 07 0f 02 01 15 00 00 00 00 00 00 00{{.*}}addi32
# CHECK: {{.*}}48: 07 0f 22 03 ce ff 07 00 00 00 00 00{{.*}}addi32
# CHECK: {{.*}}54: 07 0f 44 85 7f 00 00 00 00 00 00 00{{.*}}andi32
# CHECK: {{.*}}60: 07 0f 68 87 07 00 00 00 00 00 00 00{{.*}}ori32
# CHECK: {{.*}}6c: 07 0f 8c 89 03 00 00 00 00 00 00 00{{.*}}xori32
# CHECK: {{.*}}78: 07 06 a1 0b 04 00 00 00 00 00 00 00{{.*}}srli32
# CHECK: {{.*}}84: 07 06 c2 00 08 00 00 00 00 00 00 00{{.*}}srai32
# CHECK: {{.*}}90: 07 06 14 02 10 00 00 00 00 00 00 00{{.*}}slli32
# CHECK: {{.*}}9c: 87 43 03 01 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}a8: 87 43 23 03 01 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}b4: 87 43 4b 05 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}c0: 87 43 6b c7 03 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}cc: 07 0d 84 09 0c 00 00 00 00 00 00 00{{.*}}beq
# CHECK-LABEL: <target1>:
# CHECK: {{.*}}d8: 07 0d a6 0b 0c 00 00 00 00 00 00 00{{.*}}bne
# CHECK-LABEL: <target2>:
# CHECK: {{.*}}e4: 07 0d ca 00 0c 00 00 00 00 00 00 00{{.*}}blt
# CHECK-LABEL: <target3>:
# CHECK: {{.*}}f0: 07 0a 18 00 0c 00 00 00 00 00 00 00{{.*}}beqz
# CHECK-LABEL: <target4>:
# CHECK: {{.*}}fc: 07 0a 2a 00 0c 00 00 00 00 00 00 00{{.*}}bnez
# CHECK-LABEL: <target5>:
# CHECK: {{.*}}108: 07 0e 08 00 00 00 00 00 00 00 00 00{{.*}}jal

ADD32 R0, R1, R2

SUB32 R3, R4, R5

AND32 R6, R7, R8

OR32 R9, R10, R11

XOR32 R12, R0, R1

#===----------------------------------------------------------------------===
# Test 32-bit I-type encoding (immediate)
#===----------------------------------------------------------------------===

ADDI32 R0, R1, 42

ADDI32 R2, R3, -100

ANDI32 R4, R5, 255

ORI32 R6, R7, 15

XORI32 R8, R9, 7

#===----------------------------------------------------------------------===
# Test shift immediate encoding
#===----------------------------------------------------------------------===

SRLI32 R10, R11, 4

SRAI32 R12, R0, 8

SLLI32 R1, R2, 16

#===----------------------------------------------------------------------===
# Test load/store encoding
#===----------------------------------------------------------------------===

# fixup-restored: scalar LD/ST emit the 12-byte Format E word
# (LD32_S0_FLEX / ST32_S0_FLEX). Mnemonic + operand round-trip is preserved;
# the byte payload is the 12-byte Format E format (s0 LS window packed into
# the 128-bit LE word). The legacy 6-byte WIDE-LS parcel and 4-byte Mode-0 LS
# parcel are retired.
LD32 R0, R1, 0

LD32 R2, R3, 16

ST32 R4, R5, 0

ST32 R6, R7, -4

#===----------------------------------------------------------------------===
# Test branch encoding
#===----------------------------------------------------------------------===

# Format E RI12/I12 branches use WIDE_BranchSImm12[_RI] fixup kinds (imm12
# FieldLsb 8/4), not legacy BranchSImm16.
BEQ R8, R9, target1
target1:

BNE R10, R11, target2
target2:

BLT R12, R0, target3
target3:

#===----------------------------------------------------------------------===
# Test unconditional branch encoding
#===----------------------------------------------------------------------===

BEQZ R1, target4
target4:

BNEZ R2, target5
target5:

#===----------------------------------------------------------------------===
# Test jump and link encoding
#===----------------------------------------------------------------------===

JAL R0, extern_func
