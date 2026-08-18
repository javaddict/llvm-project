# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=SAME=1 %s -o %t.o
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s --check-prefix=SAME
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=CALLER=1 %s -o %t.caller.o
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=CALLEE=1 %s -o %t.callee.o
# RUN: llvm-readelf -r %t.caller.o | FileCheck %s --check-prefix=RELOC
# RUN: ld.lld %t.caller.o %t.callee.o -o %t.elf --section-start=.text=0x10000
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.elf | \
# RUN:   FileCheck %s --check-prefix=LINK

# E3 JAL (three-member bundle) keeps r_offset at the parcel origin so P is
# the hardware PC. A mid-parcel reloc (E3 I20 starts at bit 17 / byte 2)
# made linked targets land 2 bytes early — not an exact Format E record.
# Same-file and cross-object displacements are whole-parcel byte offsets.

.ifdef SAME
.text
.globl _start
_start:
  { jal lr, callee; nop; nop }
  { nop; nop; xor32 r0, r0, r0 }
callee:
  { nop; nop; jalr r0, lr, 0 }

# SAME-LABEL: <_start>:
# SAME:        0: {{.*}}jal{{.*}}lr, 24
# SAME:        c:
# SAME-LABEL: <callee>:
# SAME:       18:
.endif

.ifdef CALLER
.text
.globl _start
_start:
  { jal lr, callee; nop; nop }

# RELOC: 00000000 {{.*}} R_HAYDN_WIDE_CallSImm20 {{.*}} callee
.endif

.ifdef CALLEE
.text
.globl callee
callee:
  { nop; nop; jalr r0, lr, 0 }
.endif

# LINK-LABEL: <_start>:
# LINK:    10000: {{.*}}jal{{.*}}lr, 12
# LINK-LABEL: <callee>:
# LINK:    1000c:
