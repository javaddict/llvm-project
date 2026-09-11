# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: not ld.lld %t.o -o %t -T %S/haydn-far-branch.ld 2>&1 | FileCheck %s
#
# D1.57 fail-closed restamp: an out-of-range far branch must NOT produce a
# HaydnLongThunk (the retired R0-borrowing veneer left the JALR link in
# soft-zero R0 — linker wrong-code). The link now fails with the explicit
# veneer-ABI-gap diagnostic naming D1.57/ISA-70 and refuses to emit
# R0-writing thunk bytes.

.section .text
.globl _start
_start:
    BEQ R0, R1, far_target
    .size _start, .-_start

.section .text.far, "ax"
.globl far_target
far_target:
    ADD32 R2, R2, R2
    .size far_target, .-far_target

# CHECK: error: {{.*}}.o:({{.*}}relocation R_HAYDN_WIDE_BranchSImm12_RI to '{{.*}}' needs a linker range-extension veneer{{.*}}Haydn veneer ABI is not approved (D1.57 / ISA-70){{.*}}{{[Rr]}}efusing to emit R0-writing thunk bytes
