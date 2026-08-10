# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o - | \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf - | FileCheck %s

# Role: object — CSRW csr_addr product pin under Format E; branch assemble smoke
# without inventing wire scale.

# Short CSRW must encode the CSR address (not 0). Pre-fix: FlexMap
# CSRW→CSRW_S0 kept a leading dead Imm:0 so OpFields wrote csr=0.
#
# Branch PC-rel wire scale is not defined by current golden hardware tables.
# Fail-closed contract: assemble bne/beq to object and require the mnemonic
# to disassemble, but do NOT pin recovered immediates / halfword field values
# as product law. Positive branch-scale oracles stay residual until golden
# publishes scale.

# High-entry-first: real op right-aligned on e0 with high nop pad.
# CHECK: {{.*}}0: {{.*}} { {{.*}}csrw{{.*}}17, r1{{.*}} }
# CHECK: {{.*}}c: {{.*}} { {{.*}}csrw{{.*}}18, r1{{.*}} }
# CHECK: {{.*}}18: {{.*}} { {{.*}}bne{{.*}}r1, r2{{.*}} }
# CHECK: {{.*}}24: {{.*}} { {{.*}}beq{{.*}}r3, r4{{.*}} }

.text
  { csrw 17, r1; nop; nop }
  { csrw 18, r1; nop; nop }
  { bne_w r1, r2, 64; nop; nop }
  { beq_w r3, r4, 64; nop; nop }
