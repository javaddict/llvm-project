# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=KINDS=1 %s \
# RUN:   | FileCheck --check-prefix=KIND %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=JALR=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=JALR %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=JALREXT=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=JALREXT %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=UNKNOWN=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=F19 %s

# REGRESSION TEST: P14 / W27 — fixup kind from HaydnRelocLayout fields.
#
# Bug: getExprFixupKind / getBranchFixupKind / getCallFixupKind switched
# on peeled logical names. JALR was two inconsistent PCRel kinds
# (WIDE_BranchSImm12_RI vs BranchSImm16) while golden JALR is rs+imm12.
# A future TypeName/opcode would silently borrow a wrong window.
#
# Fix: AIE findFixupfromFixupFields shape — generated TypeName + type
# opcode + field size look up RelocFieldInfo. JALR (RI12 opc 1) resolves
# to the dedicated JALRSImm12 row (same RI12 field numbers as the branch
# row, distinct identity — W27); execution stays golden rs+imm12. F17/
# F18/F19 and W37/W38 HWLoop Off1/Off2 defaults stay. ISA-69: symbolic
# jalr is fail-closed (no golden relocation base); JALR/JALREXT arms
# refuse rather than emit FIXUP_HAYDN_JALRSImm12 / R_HAYDN_JALRSImm12.
#
# If the name-switch returns, KIND lines drift. If F19 regresses,
# UNKNOWN emits FIXUP_HAYDN_32.

.ifdef KINDS
# KIND: fixup A - offset: 0, value: sym_i12, kind: FIXUP_HAYDN_WIDE_BranchSImm12
	beqz r1, sym_i12
# KIND: fixup A - offset: 0, value: sym_ri12, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
	beq r1, r2, sym_ri12
# KIND: fixup A - offset: 0, value: sym_call, kind: FIXUP_HAYDN_WIDE_CallSImm20
	jal r1, sym_call
# KIND: fixup A - offset: 0, value: foo, kind: FIXUP_HAYDN_HI12
	lui r1, foo
# KIND: fixup A - offset: 0, value: bar, kind: FIXUP_HAYDN_LO20
	addi32 r1, r2, bar
# KIND: fixup A - offset: 0, value: sat, kind: FIXUP_HAYDN_LO20
	addi32s r1, r2, sat
# KIND: fixup A - offset: 0, value: nearby, kind: FIXUP_HAYDN_LS_IMM
	ld32 r1, r2, nearby
# KIND: fixup A - offset: 0, value: xbody, kind: FIXUP_HAYDN_HWLoopOff1
# KIND: fixup B - offset: 0, value: xend, kind: FIXUP_HAYDN_HWLoopOff2
	set_hwloop_w 0, xbody, xend, 5
.endif

.ifdef JALR
jalr_local_target:
	jalr r1, r2, jalr_local_target
# JALR: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# JALR: refusing silent PC-relative R_HAYDN_JALRSImm12
# JALR-NOT: kind: FIXUP_HAYDN_JALRSImm12
# JALR-NOT: kind: FIXUP_HAYDN_32
# JALR-NOT: kind: FIXUP_HAYDN_WIDE_BranchSImm12
.endif

.ifdef JALREXT
	jalr r1, r2, ext_sym
# JALREXT: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# JALREXT: refusing silent PC-relative R_HAYDN_JALRSImm12
# JALREXT-NOT: R_HAYDN_JALRSImm12
# JALREXT-NOT: R_HAYDN_WIDE_BranchSImm12
.endif

.ifdef UNKNOWN
	slli32 r1, r2, unknown_sym
# F19: error: no typed fixup kind for symbolic operand
# F19-NOT: kind: FIXUP_HAYDN_32
.endif
