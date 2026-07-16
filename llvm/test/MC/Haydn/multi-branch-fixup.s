# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readobj -S %t.o | FileCheck %s
#
# REGRESSION TEST: Fix double-offset bug in applyFixup that crashes with 3+
# branch fixups and a second section.
#
# Bug: HaydnAsmBackend::applyFixup used Data[Fixup.getOffset + Idx] but Data
# already points to Contents.data + Fixup.getOffset (the caller adjusts it).
# This double-offset caused writes past the fragment content for the third fixup
# (offset 8 + 8 = 16, but fragment is only 16 bytes), corrupting the next
# section's fragment list Head pointer. The corrupted Head (value 1) caused a
# segfault in MCAssembler::layout when it tried to dereference it as a
# MCFragment pointer.
#
# The crash required: (1) 3+ branch instructions creating PC-relative fixups
# and (2) a second section like.note.GNU-stack (which Clang emits at -O0).
# With 2 or fewer fixups, the double-offset writes stayed within bounds by
# coincidence (max offset 4+4=8 < 16).
#
# This test ensures that assembling 3 forward branches + a second section
# produces a valid ELF object without crashing.
#
# CHECK: .text
# CHECK: .note.GNU-stack

  bnez r0, .L1
  bnez r1, .L2
  bnez r2, .L3
.L1:
.L2:
.L3:
  jalr r0, lr, 0
  .section ".note.GNU-stack","",@progbits
