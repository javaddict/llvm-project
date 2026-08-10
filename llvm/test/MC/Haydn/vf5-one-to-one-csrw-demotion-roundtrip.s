# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s --check-prefix=ASM
# RUN: llvm-mc -triple=haydn-unknown-elf %s | \
# RUN:   llvm-mc -triple=haydn-unknown-elf -filetype=obj -o %t2.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t2.o | FileCheck %s --check-prefix=RT
# REQUIRES: haydn-registered-target

# Role: object — Late-MC one-to-one positive path (serialize only, no multi-cycle
# repair): final CSRW_W / SUBI32 / BNEZ_W demotion forms encode as Format E
# 12-byte parcels and round-trip asm→obj→disasm by mnemonic identity.
# No golden invent: do not pin provisional branch PC-rel immediate scale.

.text

# Final CSRW_W (post-ExpandPseudos SETCBR form).
# ASM: csrw_w
# CHECK: csrw{{.*}}44, r1
# RT: csrw{{.*}}44, r1
# CHECK-NOT: <unknown>
# RT-NOT: <unknown>
{ csrw_w 44, r1; nop; nop }

# ASM: csrw_w
# CHECK: csrw{{.*}}45, r2
# RT: csrw{{.*}}45, r2
{ csrw_w 45, r2; nop; nop }

# Software demotion shape: counter-- then branch if nonzero.
# ASM: subi32
# CHECK: subi32{{.*}}r3, r3, 1
# RT: subi32{{.*}}r3, r3, 1
{ subi32 r3, r3, 1; nop; nop }

# Branch final real: pin mnemonic only (branch scale golden still open).
# ASM: bnez_w
# CHECK: bnez{{.*}}r3
# RT: bnez{{.*}}r3
{ bnez_w r3, 32; nop; nop }

# Mixed co-issue CSRW + ALU (one verified cycle).
# ASM: add32
# ASM: csrw_w
# CHECK-DAG: add32{{.*}}r0, r1, r2
# CHECK-DAG: csrw{{.*}}46, r4
# RT-DAG: add32{{.*}}r0, r1, r2
# RT-DAG: csrw{{.*}}46, r4
{ add32 r0, r1, r2; csrw_w 46, r4; nop }

# SETCBR_END set 1 final form (CSR 0x2F=47).
# ASM: csrw_w
# CHECK: csrw{{.*}}47, r3
# RT: csrw{{.*}}47, r3
{ csrw_w 47, r3; nop; nop }

# Branch exact-commit forms: mnemonic identity only.
# ASM: beqz_w
# CHECK: beqz{{.*}}r1
# RT: beqz{{.*}}r1
{ beqz_w r1, 16; nop; nop }

# ASM: bnez_w
# CHECK: bnez{{.*}}r2
# RT: bnez{{.*}}r2
{ bnez_w r2, 32; nop; nop }
