# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=KIND=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=KIND %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=EXT=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=EXT %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=JALRW=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=JALRW %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=IN_RANGE=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=IN %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_POS=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-POS %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=ODD=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=ODD %s
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=LITERAL=1 %s \
# RUN:   | FileCheck --check-prefix=LITERAL %s

# ISA-69: symbolic JALR has no golden relocation base. Assembly must
# refuse identifier / specifier / non-absolute immediates rather than
# emit FIXUP_HAYDN_JALRSImm12 or R_HAYDN_JALRSImm12 (ELF 22). Do not
# treat the leftover PC-relative table row as ABI. Literal jalr
# immediates, including odd, remain legal (ISA-68 /
# p18-jalr-rs-rel-odd-imm.s).

.ifdef KIND
jalr_local:
	jalr r1, r2, jalr_local
# KIND: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# KIND: refusing silent PC-relative R_HAYDN_JALRSImm12
# KIND-NOT: kind: FIXUP_HAYDN_JALRSImm12
# KIND-NOT: kind: FIXUP_HAYDN_WIDE_BranchSImm12
.endif

.ifdef EXT
	jalr r1, r2, ext_sym
# EXT: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# EXT: refusing silent PC-relative R_HAYDN_JALRSImm12
# EXT-NOT: R_HAYDN_JALRSImm12
.endif

.ifdef JALRW
	jalr_w r1, r2, ext_sym
# JALRW: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# JALRW: refusing silent PC-relative R_HAYDN_JALRSImm12
# JALRW-NOT: R_HAYDN_JALRSImm12
.endif

.ifdef IN_RANGE
.text
	jalr r1, r2, pos_ok
	.space 2028
pos_ok:
	{ add32 r0, r0, r0 }
# IN: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
.endif

.ifdef OOR_POS
	jalr r1, r2, pos_bad
	.space 2036
pos_bad:
	{ add32 r0, r0, r0 }
# OOR-POS: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# OOR-POS-NOT: relocation offset out of range
.endif

.ifdef ODD
	jalr r1, r2, oddtgt
	.space 1
oddtgt:
	{ nop; nop; xor32 r0, r0, r0 }
# ODD: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# ODD-NOT: mis-aligned relocation target
.endif

.ifdef LITERAL
	jalr r1, r2, 0
	jalr r1, r2, 1
	jalr r1, r2, -3
# LITERAL: jalr{{.*}}encoding:
# LITERAL: jalr{{.*}}encoding:
# LITERAL: jalr{{.*}}encoding:
.endif
