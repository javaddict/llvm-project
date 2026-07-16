# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readelf -r -s %t.o | FileCheck %s --check-prefix=RELOC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -defsym=DEFINE_TARGET=1 -o %t.target.o
# RUN: ld.lld -m elf32haydn -e 0 %t.o %t.target.o -o %t.elf
# RUN: llvm-objdump -d --no-show-raw-insn %t.elf | FileCheck %s --check-prefix=LINKED
#
# Unresolved external RI12 BNE_W must emit R_HAYDN_WIDE_BranchSImm12_RI
# (not crash with "Invalid Haydn relocation kind" / llvm_unreachable).
# After link, both registers survive and the PC-relative field is patched.
#
# RELOC: R_HAYDN_WIDE_BranchSImm12_RI{{.*}} external_target
# RELOC: GLOBAL DEFAULT UND external_target
#
# LINKED: bne_w{{.*}}r1{{.*}}r2

.ifndef DEFINE_TARGET
.text
.globl _start
.balign 16
_start:
  { bne_w r1, r2, external_target; nop; nop }
  { xor32 r0, r0, r0; nop; nop }
.else
.text
.globl external_target
.balign 16
external_target:
  { xor32 r0, r0, r0; nop; nop }
.endif
