# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o - | \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf - | FileCheck %s

# Role: object — CSRW csr_addr + RI12 branch scale under Format E.

# Short CSRW must encode the CSR address (not 0). Pre-fix: FlexMap
# CSRW→CSRW_S0 kept a leading dead Imm:0 so OpFields wrote csr=0.
#
# Two-register cond branch: Format E packs imm12 after rt/rs (dense), FieldLsb
# for the RI12 fixup is 28 (parcel origin). Asm immediates are **byte** PC
# deltas; wire stores halfwords (ValueShift=1). `bne_w r1, r2, 64` → field 32
# → dump recovers 64 bytes.

# CHECK: {{.*}}0: {{.*}} { {{.*}}csrw{{.*}}17, r1; {{.*}}nop{{.*}} }
# CHECK: {{.*}}c: {{.*}} { {{.*}}csrw{{.*}}18, r1; {{.*}}nop{{.*}} }
# CHECK: {{.*}}18: {{.*}} { {{.*}}bne{{.*}}r1, r2, 128; {{.*}}nop{{.*}} }
# CHECK: {{.*}}24: {{.*}} { {{.*}}beq{{.*}}r3, r4, 128; {{.*}}nop{{.*}} }

.text
  { csrw 17, r1; nop; nop }
  { csrw 18, r1; nop; nop }
  { bne_w r1, r2, 64; nop; nop }
  { beq_w r3, r4, 64; nop; nop }
