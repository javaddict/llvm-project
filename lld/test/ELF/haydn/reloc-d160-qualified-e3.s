# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld -m elf32haydn %t.o -o %t --defsym=tgt=0x10060 --defsym=ext_csr=0x40 \
# RUN:   --section-start=.text=0x10000 --section-start=.rodata=0xa2004
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t \
# RUN:   | FileCheck --check-prefix=LINK %s

# D1.60: first positive object+link pins for E3-qualified WIDE control and
# the HI12/CSR twins that hand assembly can reach. ValueShift=0 byte
# PC+imm. FieldLsb rides the TYPE (never a sniff). Proven parcel shapes
# (textual order is not entry order):
#   { zero_gpr; zero_dr; beqz }     → BranchSImm12_E3E0  @23
#   { beqz; zero_gpr; nop }         → BranchSImm12_E3E1  @54
#   { zero_gpr; beqz; zero_dr }     → BranchSImm12_E3E2  @81
#   { zero_gpr; zero_dr; bne_w }    → BranchSImm12_RI_E3E0 @23
#   { nop; bne_w; lui %hi12 }       → BranchSImm12_RI_E3E1 @54 + HI12_E3E0_ALU2
#   { nop; jal; lui %hi12 }         → CallSImm20_E3E1 @48 + HI12_E3E0_ALU2
#   { lui %hi12; zero_gpr; zero_dr }→ HI12_E3E2_ALU0 @81
#   { csrr; zero_gpr; zero_dr }     → CSR_UImm8_E3E2 @85
# tgt @ 0x10060; high_sym @ 0xa2004 → HI12 = 1; ext_csr = 64.

# RELOCS:      Relocations [
# RELOCS-DAG:    0x0 R_HAYDN_WIDE_BranchSImm12_E3E0 tgt 0x0
# RELOCS-DAG:    0xC R_HAYDN_WIDE_BranchSImm12_E3E1 tgt 0x0
# RELOCS-DAG:    0x18 R_HAYDN_WIDE_BranchSImm12_E3E2 tgt 0x0
# RELOCS-DAG:    0x24 R_HAYDN_WIDE_BranchSImm12_RI_E3E0 tgt 0x0
# RELOCS-DAG:    0x30 R_HAYDN_WIDE_BranchSImm12_RI_E3E1 tgt 0x0
# RELOCS-DAG:    0x30 R_HAYDN_HI12_E3E0_ALU2 high_sym 0x0
# RELOCS-DAG:    0x3C R_HAYDN_WIDE_CallSImm20_E3E1 tgt 0x0
# RELOCS-DAG:    0x3C R_HAYDN_HI12_E3E0_ALU2 high_sym 0x0
# RELOCS-DAG:    0x48 R_HAYDN_HI12_E3E2_ALU0 high_sym 0x0
# RELOCS-DAG:    0x54 R_HAYDN_CSR_UImm8_E3E2 ext_csr 0x0
# RELOCS-NOT:    R_HAYDN_WIDE_BranchSImm12{{ }}
# RELOCS-NOT:    R_HAYDN_WIDE_CallSImm20{{ }}
# RELOCS:      ]

	.section .text
	.globl _start
	.type _start, @function
_start:
	{ zero_gpr r8; zero_dr d0; beqz r1, tgt }
	{ beqz r1, tgt; zero_gpr r8; nop }
	{ zero_gpr r8; beqz r1, tgt; zero_dr d0 }
	{ zero_gpr r8; zero_dr d0; bne_w r4, r5, tgt }
	{ nop; bne_w r4, r5, tgt; lui r9, %hi12(high_sym) }
	{ nop; jal lr, tgt; lui r9, %hi12(high_sym) }
	{ lui r9, %hi12(high_sym); zero_gpr r8; zero_dr d0 }
	{ csrr r1, ext_csr; zero_gpr r8; zero_dr d0 }
	.size _start, .-_start

	.section .rodata
	.globl high_sym
	.type high_sym, @object
high_sym:
	.long 0
	.size high_sym, 4

# LINK-LABEL: <_start>:
# LINK: beqz{{.*}}96
# LINK: beqz{{.*}}84
# LINK: beqz{{.*}}72
# LINK: bne{{(_w)?}}{{.*}}60
# LINK: bne{{(_w)?}}{{.*}}48
# LINK: jal{{(_w)?}}{{.*}}lr, 36
# LINK: lui{{.*}} r9, 1
# LINK: csrr{{.*}} r1, 64
