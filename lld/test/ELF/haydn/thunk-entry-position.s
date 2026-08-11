# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf --defsym POS=0 %s -o %t0.o
# RUN: ld.lld %t0.o -o %t0 --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t0 | FileCheck %s
#
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf --defsym POS=1 %s -o %t1.o
# RUN: ld.lld %t1.o -o %t1 --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t1 | FileCheck %s
#
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf --defsym POS=2 %s -o %t2.o
# RUN: ld.lld %t2.o -o %t2 --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t2 | FileCheck %s

# A far call must land EXACTLY on its veneer, from whichever entry of the
# bundle the `jal` happens to occupy.
#
# A branch resolves from the start of the bundle; the relocation points at the
# entry inside it. The emitter reconciles the two by adding the entry's byte
# base to the addend, so S + A - P cancels it. Redirecting the relocation to a
# thunk overwrites the addend, and the base has to be put back there — that is
# what `getPCBias()` does for Haydn, the same seam Hexagon uses for its
# variable-length packets.
#
# Without it the call lands `base` bytes short of the veneer. That is 0 bytes
# for entry 2 and 4 bytes for entries 0 and 1, so two thirds of far calls in a
# real link point into the middle of the preceding bundle. BundleSim rejects
# the image outright ("direct control target is not an exact code record")
# rather than executing it, which is the only reason this was ever visible:
# nothing in lld's own suite was checking the number.
#
# `thunk-addend.s` is the test that should have caught it and did not. It
# matches the veneer's shape — `lui{{.*}}r0,` accepts any immediate — and never
# compares an address to anything. Hence the arithmetic below, which is the
# whole point of this file: capture the call site and its displacement, add
# them, and require the sum to BE the veneer's address.

.section .text
.globl _start
_start:
.if POS == 0
    { jal lr, callee; nop; nop }
.elseif POS == 1
    { nop; jal lr, callee; nop }
.else
    { nop; nop; jal lr, callee }
.endif
    .space 0x140000

.globl callee
callee:
    add32 r3, r3, r3
    .size callee, .-callee
    .size _start, .-_start

# The veneer is emitted ahead of _start, so capture its address first and
# require the call's displacement to be exactly the distance to it.
# The call site's address is taken from the <_start> label rather than from
# the instruction line: FileCheck refuses to use a numeric variable defined
# earlier in the SAME directive, and the two have the same value because the
# call is the first bundle of the function.
# CHECK: [[#%x,THUNK:]] <__haydn_thunk_callee>:
# CHECK: [[#%x,CALL:]] <_start>:
# CHECK-NEXT: {{.*}}jal{{.*}}lr, [[#%d,THUNK-CALL]]{{[;} ]}}
