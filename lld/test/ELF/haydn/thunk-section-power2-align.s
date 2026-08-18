# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t -T %S/haydn-far-branch.ld
# RUN: llvm-nm %t | FileCheck --check-prefix=NM %s
# RUN: llvm-readobj --symbols %t | FileCheck --check-prefix=SYM %s
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t | \
# RUN:   FileCheck %s
#
# ThunkSection::assignOffsets calls alignToPowerOf2(offset, Thunk::alignment).
# EncodedBytes=12 is not 2^n (and a missing registry read is 0); either value
# aborts ld.lld. Veneer policy is Hexagon Align-4 plus 3 x EncodedBytes.
# Island spacing is 0 mod EncodedBytes so the entry stays on the .text phase.

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

# NM: __haydn_thunk_far_target

# SYM: Name: __haydn_thunk_far_target
# SYM: Size: 36

# CHECK-LABEL: <__haydn_thunk_far_target>:
# CHECK-NEXT:  {{.*}} lui{{.*}}r0
# CHECK-NEXT:  {{.*}} addi32{{.*}}r0
# CHECK-NEXT:  {{.*}} jalr{{.*}}r0
# CHECK-NOT: r12
