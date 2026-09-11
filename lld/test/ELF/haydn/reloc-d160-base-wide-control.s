# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t \
# RUN:   | FileCheck --check-prefix=LINK %s

# D1.60: explicit base WIDE control kinds (not the RI regex). I12 one-reg
# is R_HAYDN_WIDE_BranchSImm12; RI12 two-reg is _RI; JAL is CallSImm20.
# ValueShift=0 byte PC+imm. Same-section target two parcels ahead (+24).

# RELOCS:      Relocations [
# RELOCS-DAG:    0x0 R_HAYDN_WIDE_CallSImm20 callee 0x0
# RELOCS-DAG:    0xC R_HAYDN_WIDE_BranchSImm12 ext_i12 0x0
# RELOCS-DAG:    0x18 R_HAYDN_WIDE_BranchSImm12_RI ext_ri 0x0
# RELOCS-NOT:    R_HAYDN_BranchSImm16
# RELOCS:      ]

	.section .text
	.globl _start
	.type _start, @function
_start:
	jal_w lr, callee
	beqz r1, ext_i12
	beq_w r2, r3, ext_ri
	.size _start, .-_start

	.globl callee
	.type callee, @function
callee:
	{ add32 r1, r2, r3 }
	.size callee, .-callee

	.globl ext_i12
	.type ext_i12, @function
ext_i12:
	{ add32 r4, r5, r6 }
	.size ext_i12, .-ext_i12

	.globl ext_ri
	.type ext_ri, @function
ext_ri:
	{ add32 r7, r8, r9 }
	.size ext_ri, .-ext_ri

# LINK-LABEL: <_start>:
# LINK: jal{{(_w)?}}{{.*}}lr, 36
# LINK: beqz{{.*}}36
# LINK: beq{{(_w)?}}{{.*}}36
# LINK-LABEL: <callee>:
# LINK-LABEL: <ext_i12>:
# LINK-LABEL: <ext_ri>:
