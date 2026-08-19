# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s --check-prefix=ASM
# RUN: llvm-mc -triple=haydn-unknown-elf %s | \
# RUN:   llvm-mc -triple=haydn-unknown-elf -filetype=obj -o %t2.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t2.o | FileCheck %s --check-prefix=RT
# REQUIRES: haydn-registered-target

# Role: object — Late-MC one-to-one positive path for final reals that replace
# residual multi-cycle / loop-control pseudos before pack. Avoid all-nop
# parcels (GE96-01 idle/completion golden still open; no golden invent).

.text

# Final ZOL setup real (not residual SET_HWLOOP / LoopStart).
# ASM: set_hwloop_w
# CHECK: set_hwloop
# RT: set_hwloop
# CHECK-NOT: <unknown>
# RT-NOT: <unknown>
{ set_hwloop_w 0, 16, 32, 4; nop }

# Exact-commit address materialization (not residual LOADI32 / LOAD_ADDR).
# ASM: lui
# CHECK: lui
# RT: lui
{ lui r1, 1; nop; nop }

# ASM: addi32
# CHECK: addi32
# RT: addi32
{ addi32_w r1, r1, 2; nop }
