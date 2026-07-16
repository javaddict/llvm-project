# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o - | \
# RUN:   llvm-objdump -d - | FileCheck %s

# Short CSRW must encode the CSR address (not 0). Pre-fix: FlexMap
# CSRW→CSRW_S0 kept a leading dead Imm:0 so OpFields wrote csr=0.
# CHECK: csrw{{.*}}17{{.*}}r1
# CHECK: csrw{{.*}}18{{.*}}r1

# Two-register WIDE cond branch: fixup FieldLsb must be 8 (imm12 @ bits[19:8])
# not 4 (I12 form). Pre-fix clobbered rt/rs → e.g. bne_w r1, r8, 2.
# CHECK: bne_w{{.*}}r1{{.*}}r2
# CHECK: beq_w{{.*}}r3{{.*}}r4

.text
  { csrw 17, r1; nop; nop }
  { csrw 18, r1; nop; nop }
  { bne_w r1, r2, 64; nop; nop }
  { beq_w r3, r4, 64; nop; nop }
