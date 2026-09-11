# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: not ld.lld %t.o -o %t -T %S/haydn-far-branch.ld 2>&1 | FileCheck %s
#
# D1.57 fail-closed restamp: this used to pin that Thunk::alignment stayed
# a power of two (Hexagon Align-4; EncodedBytes=12 is not 2^n) so
# ThunkSection::assignOffsets could not abort. No Haydn thunk or thunk
# island exists now (no spacing override, no pre-created ThunkSections),
# so the out-of-range branch is an explicit link error naming the veneer
# ABI gap. The power-of-two-alignment law returns with the ISA-70 template
# if one is approved; this test then restamps its geometry pin.

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

# CHECK: error: {{.*}}.o:({{.*}}relocation R_HAYDN_WIDE_BranchSImm12_RI to '{{.*}}' needs a linker range-extension veneer{{.*}}Haydn veneer ABI is not approved (D1.57 / ISA-70)
# CHECK-NOT: __haydn_thunk
