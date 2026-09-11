# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: not ld.lld %t.o -o %t -T %S/haydn-far-branch.ld 2>&1 | FileCheck %s
#
# D1.57 fail-closed restamp: this WAS the wrong-code repro shape — a
# mid-function far branch through the R0-borrowing veneer, where JALR left
# PC_next in soft-zero R0 and the landing label saw R0 == link, not 0. The
# residual is no longer "accepted ABI": the veneer is retired and the
# mid-function far branch is an explicit link error naming the veneer ABI
# gap (D1.57 / ISA-70). No R0-writing thunk bytes are emitted.

.section .text
.globl _start
_start:
    # Mid-fn shape: fall into a far BEQ, then more code in the same section
    # after the branch site (not a call).
    addi32 r3, r0, 1
    beq r0, r1, mid_far
    addi32 r4, r0, 2
    .size _start, .-_start

.section .text.far, "ax"
.globl mid_far
mid_far:
    add32 r5, r5, r5
    .size mid_far, .-mid_far

# CHECK: error: {{.*}}.o:({{.*}}relocation R_HAYDN_WIDE_BranchSImm12_RI to '{{.*}}' needs a linker range-extension veneer{{.*}}Haydn veneer ABI is not approved (D1.57 / ISA-70){{.*}}{{[Rr]}}efusing to emit R0-writing thunk bytes
