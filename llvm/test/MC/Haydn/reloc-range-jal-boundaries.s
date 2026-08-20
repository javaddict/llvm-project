# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=IN_RANGE=1 %s \
# RUN:   -o %t.in.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.in.o | \
# RUN:   FileCheck --check-prefix=IN %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_POS=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-POS %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_NEG=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-NEG %s

# JAL symbolic targets share the branch even-byte law (Align=2) and the
# same computeRelocValue range authority. Signed 20-bit field → even
# window [-524288, +524286]. Parcel-aligned code can only use multiples
# of 12, so the in-range probe is +524280 and the first 12-multiple past
# the window is +524292. Odd-target reject is p18-reloc-jal-align-parity.s.

.ifdef IN_RANGE
.text
	jal lr, pos_ok
	.space 524268
pos_ok:
	{ add32 r0, r0, r0 }
# IN: jal{{.*}}524280
.endif

.ifdef OOR_POS
	jal lr, pos_bad
	.space 524280
pos_bad:
	{ add32 r0, r0, r0 }
# OOR-POS: relocation offset out of range
.endif

.ifdef OOR_NEG
neg_bad:
	{ add32 r1, r1, r1 }
	.space 524280
	jal lr, neg_bad
# OOR-NEG: relocation offset out of range
.endif
