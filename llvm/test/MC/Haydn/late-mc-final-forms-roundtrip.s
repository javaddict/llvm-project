# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s --check-prefix=ASM
# RUN: llvm-mc -triple=haydn-unknown-elf %s | \
# RUN:   llvm-mc -triple=haydn-unknown-elf -filetype=obj -o %t2.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t2.o | FileCheck %s --check-prefix=RT
# REQUIRES: haydn-registered-target
# Format E96 cutover residual: FileCheck/idle-pad/reloc geometry still open (GE96-01/03).
# XFAIL: *

# Role: object — Late-MC one-to-one positive path for final reals that replace residual multi-cycle / loop-control pseudos before pack:.

# Late-MC one-to-one positive path for final reals that replace residual
# multi-cycle / loop-control pseudos before pack:
#   * SET_HWLOOP_W — final form of residual LoopStart / SET_HWLOOP{,_REG}
#   * LUI + ADDI32_W — final exact-commit form of residual LOADI32/LOAD_ADDR
# Each is one Format E cycle; asm→obj→disasm and print→re-asm preserve
# identity. Serialize only; no multi-cycle repair / FlexMap.

.text

# RT-LABEL: <.text>:

# Final ZOL setup real (not residual SET_HWLOOP / LoopStart).
# ASM: set_hwloop_w
# RT: set_hwloop_w
# RT-NOT: <unknown>
{ set_hwloop_w 0, 16, 32, 4; nop; nop }

# Body / end parcel placeholders (imm offsets above assume 16 B parcels).
# ASM: { nop; nop; nop }
# RT: { nop; nop; nop }
{ nop; nop; nop }
# ASM: { nop; nop; nop }
# RT: { nop; nop; nop }
{ nop; nop; nop }

# Exact-commit address materialization (not residual LOADI32 / LOAD_ADDR).
# ASM: lui{{.*}}r1{{.*}}1
# RT: lui{{.*}}r1{{.*}}1
{ lui r1, 1; nop; nop }

# ASM: addi32_w{{.*}}r1{{.*}}r1{{.*}}42
# RT: addi32_w{{.*}}r1{{.*}}r1{{.*}}42
{ addi32_w r1, r1, 42; nop; nop }

# Co-issue final real + ALU in one verified cycle (parser/codegen parity).
# ASM: set_hwloop_w
# RT: set_hwloop_w
# RT: add32{{.*}}r0{{.*}}r2{{.*}}r3
{ add32 r0, r2, r3; set_hwloop_w 0, 16, 32, 8; nop }

# Final F2 SET form (reg-count exact-commit of residual SET_HWLOOP_REG).
# ASM: set_hwloop_f2_w
# RT: set_hwloop_f2_w
# RT-NOT: <unknown>
{ set_hwloop_f2_w 0, 16, 48, r1; nop; nop }

# Far-branch exact-commit shape (insertIndirectBranch): LUI + ADDI32_W +
# JALR_W — three one-cycle reals, not residual LOADI32(MBB). Serialize only.
# ASM: lui{{.*}}r2
# RT: lui{{.*}}r2
{ lui r2, 0; nop; nop }
# ASM: addi32_w{{.*}}r2{{.*}}r2
# RT: addi32_w{{.*}}r2{{.*}}r2
{ addi32_w r2, r2, 0; nop; nop }
# ASM: jalr_w{{.*}}r2{{.*}}r2
# RT: jalr_w{{.*}}r2{{.*}}r2
{ jalr_w r2, r2, 0; nop; nop }
