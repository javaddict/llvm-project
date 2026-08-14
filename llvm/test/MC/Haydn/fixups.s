# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Branch/call fixups encode→obj→disasm (mnemonic identity; no golden branch-scale invent).
# Converted from parse-only/show-encoding to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).
# Fail-closed: no positive ar_sel=2/3, all-zero product-NOP, or golden-unspecified branch-scale invent.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 0d 04 01 0c 00 00 00 00 00 00 00{{.*}}beq
# CHECK-LABEL: <forward_label>:
# CHECK: {{.*}}c: 07 8b 20 43 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK-LABEL: <backward_label>:
# CHECK: {{.*}}18: 07 0d 56 06 00 00 00 00 00 00 00 00{{.*}}bne
# CHECK: {{.*}}24: 07 0a 78 00 18 00 00 00 00 00 00 00{{.*}}beqz
# CHECK-LABEL: <backward_target>:
# CHECK: {{.*}}30: 07 0a 8a 00 00 00 00 00 00 00 00 00{{.*}}bnez
# CHECK-LABEL: <forward_target>:
# CHECK: {{.*}}3c: 07 8b 90 ba 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}48: 07 0e 08 00 00 00 00 00 00 00 00 00{{.*}}jal
# CHECK: {{.*}}54: 87 43 33 04 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}60: 87 43 5b 06 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK-LABEL: <local_loop>:
# CHECK: {{.*}}6c: 07 0d aa 0b 00 00 00 00 00 00 00 00{{.*}}blt
# CHECK: {{.*}}78: 07 0d c8 00 84 00 00 00 00 00 00 00{{.*}}bge
# CHECK: {{.*}}84: 07 8b 10 11 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}90: 07 8b 20 22 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}9c: 07 8b 30 33 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}a8: 07 8b 40 44 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}b4: 07 8b 50 55 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}c0: 07 8b 60 66 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}cc: 07 8b 70 77 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}d8: 07 8b 80 88 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}e4: 07 8b 90 99 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}f0: 07 8b a0 aa 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK-LABEL: <far_target>:
# CHECK: {{.*}}fc: 07 8b b0 bb 00 00 00 00 00 00 00 00{{.*}}add32

BEQ R0, R1, forward_label

forward_label:
ADD32 R2, R3, R4

# Backward branch reference
backward_label:
BNE R5, R6, backward_label

#===----------------------------------------------------------------------===
# Test unconditional branch relocations
#===----------------------------------------------------------------------===

# Forward unconditional branch
BEQZ R7, forward_target

# Backward unconditional branch
backward_target:
BNEZ R8, backward_target

forward_target:
ADD32 R9, R10, R11

#===----------------------------------------------------------------------===
# Test jump and link relocations
#===----------------------------------------------------------------------===

# JAL to external symbol
JAL R0, external_func

#===----------------------------------------------------------------------===
# Test load/store with symbolic offsets
#===----------------------------------------------------------------------===

# Load with symbol reference (requires relocation)
LD32 R3, R4, symbol_offset

# Store with symbol reference
ST32 R5, R6, data_location

#===----------------------------------------------------------------------===
# Test PC-relative references
#===----------------------------------------------------------------------===

# PC-relative branch
local_func:
local_loop:
BLT R10, R11, local_loop

# Test far branch
BGE R12, R0, far_target

# Skip some instructions to create distance
ADD32 R1, R1, R1
ADD32 R2, R2, R2
ADD32 R3, R3, R3
ADD32 R4, R4, R4
ADD32 R5, R5, R5
ADD32 R6, R6, R6
ADD32 R7, R7, R7
ADD32 R8, R8, R8
ADD32 R9, R9, R9
ADD32 R10, R10, R10

far_target:
ADD32 R11, R11, R11
