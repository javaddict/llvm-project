# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s --check-prefix=ASM
# RUN: llvm-mc -triple=haydn-unknown-elf %s | \
# RUN:   llvm-mc -triple=haydn-unknown-elf -filetype=obj -o %t2.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t2.o | FileCheck %s --check-prefix=RT
# REQUIRES: haydn-registered-target

# Role: object — Late-MC one-to-one multi-member round-trip for co-issue shapes
# that hard-root exact-commit leaves as one Format E 12-byte parcel.
# Pin membership mnemonics; do not invent branch-scale or idle-pad goldens.

.text

# Two-member ALU co-issue.
# ASM: add32
# ASM: xor32
# CHECK-DAG: add32{{.*}}r4, r0, r1
# CHECK-DAG: xor32{{.*}}r5, r2, r3
# RT-DAG: add32{{.*}}r4, r0, r1
# RT-DAG: xor32{{.*}}r5, r2, r3
# CHECK-NOT: <unknown>
# RT-NOT: <unknown>
{ add32 r4, r0, r1; xor32 r5, r2, r3; nop }

# Three-member mixed FU (ADD32 + 2×ADD64).
# ASM: add32
# ASM: add64
# CHECK-DAG: add32{{.*}}r4, r0, r1
# CHECK-DAG: add64{{.*}}d4, d0, d1
# CHECK-DAG: add64{{.*}}d5, d2, d3
# RT-DAG: add32{{.*}}r4, r0, r1
# RT-DAG: add64{{.*}}d4, d0, d1
# RT-DAG: add64{{.*}}d5, d2, d3
{ add32 r4, r0, r1; add64 d4, d0, d1; add64 d5, d2, d3 }

# Mixed LD+ALU hard-root shape.
# ASM: add32
# CHECK-DAG: add32{{.*}}r4, r0, r1
# CHECK-DAG: ld32{{.*}}r5, r2
# RT-DAG: add32{{.*}}r4, r0, r1
# RT-DAG: ld32{{.*}}r5, r2
{ add32 r4, r0, r1; ld32 r5, r2, 0; nop }

# Demotion final reals as one-cycle parcels.
# ASM: subi32
# CHECK: subi32{{.*}}r3, r3, 1
# RT: subi32{{.*}}r3, r3, 1
{ subi32 r3, r3, 1; nop; nop }

# Co-issued demotion final + ALU.
# ASM: subi32
# ASM: xor32
# CHECK-DAG: subi32{{.*}}r3, r3, 1
# CHECK-DAG: xor32{{.*}}r6, r4, r5
# RT-DAG: subi32{{.*}}r3, r3, 1
# RT-DAG: xor32{{.*}}r6, r4, r5
{ subi32 r3, r3, 1; xor32 r6, r4, r5; nop }

# Branch / CSR final reals: mnemonic only (no branch imm scale claim).
# ASM: bnez_w
# CHECK: bnez{{.*}}r3
# RT: bnez{{.*}}r3
{ bnez_w r3, 32; nop; nop }

# ASM: csrw_w
# CHECK: csrw{{.*}}44, r1
# RT: csrw{{.*}}44, r1
{ csrw_w 44, r1; nop; nop }
