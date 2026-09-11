# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
#
# ISel general call is LUI HI12 + ADDI32 LO20 + JALR. RISC-V relaxCall
# deletes AUIPC and drops a fetch cycle. Haydn overlay keeps the committed
# three-parcel / three-cycle grid: in-range sites become idle + idle + JAL.
# .text.far is placed at 2MiB so the second triple stays on the general form.
#
# RUN: ld.lld %t.o -o %t -T %p/relax-call-cycle-neutral.ld
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck --check-prefix=RELAX %s
# RUN: llvm-readobj -x .text %t | FileCheck --check-prefix=HEX %s
#
# RUN: ld.lld %t.o -o %t.norelax -T %p/relax-call-cycle-neutral.ld --no-relax
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.norelax | FileCheck --check-prefix=NORELAX %s

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-DAG:      0x0 R_HAYDN_HI12 nearby 0x0
# RELOCS-DAG:      0xC R_HAYDN_LO20 nearby 0x0
# RELOCS-DAG:      0x24 R_HAYDN_HI12 far 0x0
# RELOCS-DAG:      0x30 R_HAYDN_LO20 far 0x0
# RELOCS-DAG:      0x48 R_HAYDN_HI12 nearby 0x0
# RELOCS-DAG:      0x54 R_HAYDN_LO20 nearby 0x0
# RELOCS-DAG:      0x6C R_HAYDN_HI12 nearby 0x0
# RELOCS-DAG:      0x78 R_HAYDN_LO20 nearby 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

.section .text
.globl _start
_start:
    # RELAX-LABEL: <_start>:
    # Nearby triple stays three parcels: LUI+ADDI keep the address temp,
    # JALR becomes JAL (cycle-neutral; do not idle the temp).
    # RELAX: 10000: {{.*}} lui{{.*}}r1
    # RELAX: 1000c: {{.*}} addi32{{.*}}r1
    # RELAX: 10018: {{.*}} jal{{.*}}lr
    #
    # Far triple stays LUI+ADDI+JALR (CallSImm20 cannot reach 0x200000).
    # RELAX: 10024: {{.*}} lui{{.*}}r2
    # RELAX: 10030: {{.*}} addi32{{.*}}r2
    # RELAX: 1003c: {{.*}} jalr{{.*}}lr,{{.*}}r2
    #
    # Long jump (rd != LR) stays JALR even though nearby fits CallSImm20.
    # RELAX: jalr{{.*}}r3,{{.*}}r3
    # Returning call whose rs is not the address temp stays JALR.
    # RELAX: jalr{{.*}}lr,{{.*}}r1
    #
    # NORELAX-LABEL: <_start>:
    # NORELAX: 10000: {{.*}} lui{{.*}}r1
    # NORELAX: 1000c: {{.*}} addi32{{.*}}r1
    # NORELAX: 10018: {{.*}} jalr{{.*}}lr,{{.*}}r1
    # NORELAX: 10024: {{.*}} lui{{.*}}r2
    # NORELAX: 10030: {{.*}} addi32{{.*}}r2
    # NORELAX: 1003c: {{.*}} jalr{{.*}}lr,{{.*}}r2
    lui r1, %hi12(nearby)
    addi32 r1, r1, %lo20(nearby)
    jalr lr, r1, 0

    lui r2, %hi12(far)
    addi32 r2, r2, %lo20(far)
    jalr lr, r2, 0

    lui r3, %hi12(nearby)
    addi32 r3, r3, %lo20(nearby)
    jalr r3, r3, 0

    lui r4, %hi12(nearby)
    addi32 r4, r4, %lo20(nearby)
    jalr lr, r1, 0
    .size _start, .-_start

.globl nearby
nearby:
    add32 r0, r0, r0
    .size nearby, .-nearby

.section .text.far,"ax",@progbits
.globl far
far:
    add32 r0, r0, r0
    .size far, .-far

# Nearby three parcels remain 36 bytes (cycle-neutral). LUI at 0x10000
# is not idled; JAL (07 0e f8) occupies parcel 2.
# HEX: Hex dump of section '.text':
# HEX: 0x00010000 070a1200
# HEX: 070ef800
