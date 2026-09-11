# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: not ld.lld %t.o -o %t -T %S/haydn-far-branch.ld 2>&1 | FileCheck %s
#
# D1.57 fail-closed restamp: out-of-range BEQ must NOT produce the retired
# R0 soft-zero borrow veneer. The link fails with the explicit
# veneer-ABI-gap diagnostic (D1.57 / ISA-70); no __haydn_thunk symbol is
# created.

# RELOCS: R_HAYDN_WIDE_BranchSImm12_RI far_target

.section .text
.globl _start
_start:
    BEQ R0, R1, far_target
    ADD32 R2, R2, R2
    .size _start, .-_start

.section .text.far, "ax"
.globl far_target
far_target:
    ADD32 R3, R3, R3
    .size far_target, .-far_target

# CHECK: error: {{.*}}.o:({{.*}}relocation R_HAYDN_WIDE_BranchSImm12_RI to '{{.*}}' needs a linker range-extension veneer{{.*}}Haydn veneer ABI is not approved (D1.57 / ISA-70)
# CHECK-NOT: __haydn_thunk
