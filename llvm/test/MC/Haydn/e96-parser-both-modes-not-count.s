# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=THREE_PAD=1 %s -o %t3.o
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t2.o
# RUN: llvm-objcopy -O binary -j .text %t3.o %t3.bin
# RUN: llvm-objcopy -O binary -j .text %t2.o %t2.bin
# RUN: cmp %t3.bin %t2.bin
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=THREE_REAL=1 %s -o %t3r.o
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t3r.o | \
# RUN:   FileCheck %s --check-prefix=THREE-REAL
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=E3ONLY_PAD=1 %s -o %te3.o
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %te3.o | \
# RUN:   FileCheck %s --check-prefix=E3ONLY

# When both E2 and E3 are generated-legal, extra textual NOP pads must not
# pick BUNDLE_E96_THREE_ENTRY from child count. `{ add32; nop; nop }` encodes
# as the same two-entry parcel as `{ add32; nop }`. Three *real* members still
# occupy E3 (E2 has two entries). E3-only logicals still force E3 even with
# two text entries.

.ifdef THREE_REAL
.text
  { add64 d0, d1, d2; add64 d3, d4, d5; add32 r0, r1, r2 }
# THREE-REAL-LABEL: <.text>:
# THREE-REAL: add64
# THREE-REAL: add64
# THREE-REAL: add32
# THREE-REAL-NOT: <unknown>
.else
.ifdef E3ONLY_PAD
.text
  { sin_cos d0, r1, 1; nop }
# E3ONLY-LABEL: <.text>:
# E3ONLY: sin_cos
# E3ONLY-NOT: <unknown>
.else
.ifdef THREE_PAD
.text
  { add32 r1, r0, r2; nop; nop }
  { xor32 r3, r0, r4; nop; nop }
.else
.text
  { add32 r1, r0, r2; nop }
  { xor32 r3, r0, r4; nop }
.endif
.endif
.endif
