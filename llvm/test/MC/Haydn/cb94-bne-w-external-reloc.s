# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readelf -r -s %t.o | FileCheck %s --check-prefix=RELOC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -defsym=DEFINE_TARGET=1 -o %t.target.o
# RUN: ld.lld -m elf32haydn -e 0 %t.o %t.target.o -o %t.elf
# RUN: llvm-objdump -d --no-show-raw-insn %t.elf | FileCheck %s --check-prefix=LINKED
#
# REGRESSION TEST: Unresolved external RI12 BNE_W emits
# R_HAYDN_WIDE_BranchSImm12_RI (not "Invalid Haydn relocation kind").
# After link, both registers survive and the PC-rel field is the byte
# displacement (GE96-03). Two 12-byte parcels → +24.

# RELOC: R_HAYDN_WIDE_BranchSImm12_RI{{.*}} external_target
# RELOC: GLOBAL DEFAULT UND external_target

# LINKED: bne{{.*}}r1, r2, 24

.ifndef DEFINE_TARGET

.text
.globl _start
_start:
  { bne_w r1, r2, external_target; nop; nop }
  { xor32 r0, r0, r0; nop; nop }

.else

.text
.globl external_target
external_target:
  { xor32 r0, r0, r0; nop; nop }

.endif
