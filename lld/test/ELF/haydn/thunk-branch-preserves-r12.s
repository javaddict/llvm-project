# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: not ld.lld %t.o -o %t -T %S/haydn-far-branch.ld 2>&1 | FileCheck %s
#
# D1.57 fail-closed restamp: the R0-borrowing far-branch veneer is retired
# (JALR wrote the link into soft-zero R0; mid-function arrivals could see
# R0 dirty — wrong-code). There is no "preserves R12" veneer to test: an
# out-of-range BEQ is a link error naming the veneer ABI gap. The R12
# sentinel stays as the site shape.

.section .text
.globl _start
_start:
    # Live R12 sentinel — site shape for the far branch below.
    addi32 r12, r0, 0x55
    beq r0, r1, far_target
    .size _start, .-_start

.section .text.far, "ax"
.globl far_target
far_target:
    add32 r2, r2, r2
    .size far_target, .-far_target

# CHECK: error: {{.*}}.o:({{.*}}relocation R_HAYDN_WIDE_BranchSImm12_RI to '{{.*}}' needs a linker range-extension veneer{{.*}}Haydn veneer ABI is not approved (D1.57 / ISA-70)
# CHECK-NOT: __haydn_thunk
