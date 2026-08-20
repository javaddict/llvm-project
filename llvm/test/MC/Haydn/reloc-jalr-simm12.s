# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=KIND=1 %s \
# RUN:   | FileCheck --check-prefix=KIND %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=EXT=1 %s \
# RUN:   -o %t.ext.o
# RUN: llvm-readobj -r -h %t.ext.o | FileCheck --check-prefix=EXT %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=JALRW=1 %s \
# RUN:   -o %t.jalrw.o
# RUN: llvm-readobj -r %t.jalrw.o | FileCheck --check-prefix=JALRW %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=IN_RANGE=1 %s \
# RUN:   -o %t.in.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.in.o | \
# RUN:   FileCheck --check-prefix=IN %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_POS=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-POS %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=ODD=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=ODD %s

# Symbolic JALR uses the dedicated JALRSImm12 row (ELF 22). The baked
# calltarget_wide_ri12 EncoderMethod kind is WIDE_BranchSImm12; layout
# lookup must override it so unresolved externals never borrow a branch
# reloc. Local same-section symbols share the branch even-byte Align=2
# window (signed 12-bit [-2048, +2046]); odd *literals* stay legal.
# Objects keep EM_HAYDN=259 / EF_HAYDN_E96=0x1 (no replacement e_machine).

.ifdef KIND
jalr_local:
	jalr r1, r2, jalr_local
# KIND: fixup A - offset: 0, value: jalr_local, kind: FIXUP_HAYDN_JALRSImm12
# KIND-NOT: kind: FIXUP_HAYDN_WIDE_BranchSImm12
# KIND-NOT: kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
# KIND-NOT: kind: FIXUP_HAYDN_BranchSImm16
.endif

.ifdef EXT
	jalr r1, r2, ext_sym
# EXT: Machine: 0x103
# EXT: Flags [ (0x1)
# EXT: R_HAYDN_JALRSImm12 ext_sym
# EXT-NOT: R_HAYDN_WIDE_BranchSImm12
# EXT-NOT: R_HAYDN_WIDE_BranchSImm12_RI
.endif

.ifdef JALRW
	jalr_w r1, r2, ext_sym
# JALRW: R_HAYDN_JALRSImm12 ext_sym
# JALRW-NOT: R_HAYDN_WIDE_BranchSImm12
# JALRW-NOT: R_HAYDN_WIDE_BranchSImm12_RI
.endif

.ifdef IN_RANGE
.text
	jalr r1, r2, pos_ok
	.space 2028
pos_ok:
	{ add32 r0, r0, r0 }
# IN: jalr{{.*}}2040
.endif

.ifdef OOR_POS
	jalr r1, r2, pos_bad
	.space 2036
pos_bad:
	{ add32 r0, r0, r0 }
# OOR-POS: relocation offset out of range
.endif

.ifdef ODD
	jalr r1, r2, oddtgt
	.space 1
oddtgt:
	{ nop; nop; xor32 r0, r0, r0 }
# ODD: mis-aligned relocation target
.endif
