# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t -T %S/haydn-far-branch.ld
# RUN: llvm-nm %t | FileCheck --check-prefix=NM %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# Mid-function far branch through the linker veneer:
#   LUI+ADDI32+JALR r0 materializes the target and transfers control.
#   JALR writes PC_next into R0, so the landing label sees R0 = link, not 0.
#   The veneer does not emit a post-JALR re-zero (it never falls through).
#
# ABI residual (accepted): soft-zero is restored by function prologue on call
# entry, by ExpandPseudos at compiler JT / JALR-rd=R0 successors, and by the
# epilogue before CSR restore. Mid-fn linker far-branch destinations rely on
# those sites — the veneer itself leaves R0 dirty on purpose (no free AT).

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
    # Landing site: soft-zero not re-established by the veneer.
    add32 r5, r5, r5
    .size mid_far, .-mid_far

# NM: __haydn_thunk_mid_far

# CHECK-LABEL: <_start>:
# CHECK: addi32{{.*}}r3
# CHECK: beq
# CHECK: addi32{{.*}}r4
# CHECK-LABEL: <__haydn_thunk_mid_far>:
# CHECK-NOT: xor32
# CHECK-NOT: r12
# CHECK-LABEL: <mid_far>:
# CHECK: add32{{.*}}r5
