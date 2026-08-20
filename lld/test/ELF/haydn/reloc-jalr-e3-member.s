# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r -h %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: llvm-readelf -r %t.o | FileCheck --check-prefix=ELFNUM %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t | \
# RUN:   FileCheck --check-prefix=LINK %s
#
# Symbolic JALR on generated E3 members keeps R_HAYDN_JALRSImm12 (ELF 22)
# and patches the typed member window, not the E2 e0 table FieldLsb=32.
# A 3-entry bundle is high-first (`{ a; b; c }` = e2,e1,e0).
#   e0_site: { xor32; nop; jalr } — JALR_E3_E0_ALU0_RI12, FieldLsb=23
#   e1_site: { xor32; jalr; st32 } — JALR_E3_E1_ALU0_RI12, FieldLsb=54
# Same-file target is two parcels ahead so the patched imm12 is 24.
# Kind must not alias R_HAYDN_WIDE_BranchSImm12_RI. Objects keep
# EM_HAYDN=259 / production e_flags=0x1 (provisional; no e_machine replacement).
#
# RELOCS: Machine: 0x103
# RELOCS: Flags [ (0x1)
# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-NEXT:     0x0 R_HAYDN_JALRSImm12 tgt_e0
# RELOCS-NEXT:     0x24 R_HAYDN_JALRSImm12 tgt_e1
# RELOCS-NEXT:   }
# RELOCS-NEXT: ]
# RELOCS-NOT: R_HAYDN_WIDE_BranchSImm12
# RELOCS-NOT: R_HAYDN_WIDE_BranchSImm12_RI
#
# ELF32 r_info low byte is the type: 0x16 = ELF 22.
# ELFNUM: {{[0-9a-fA-F]+}}16 R_HAYDN_JALRSImm12 {{.*}} tgt_e0
# ELFNUM: {{[0-9a-fA-F]+}}16 R_HAYDN_JALRSImm12 {{.*}} tgt_e1
#
# LINK-LABEL: <e0_site>:
# LINK: 10000: {{.*}}jalr{{.*}}r2, 24
# LINK-LABEL: <tgt_e0>:
# LINK: 10018:
# LINK-LABEL: <e1_site>:
# LINK: 10024: {{.*}}jalr{{.*}}r2, 24
# LINK-LABEL: <tgt_e1>:
# LINK: 1003c:

.section .text
.globl _start
_start:

.globl e0_site
e0_site:
    { xor32 r3, r3, r3; nop; jalr r1, r2, tgt_e0 }
    { xor32 r0, r0, r0 }

.globl tgt_e0
tgt_e0:
    { add32 r0, r0, r0 }

.globl e1_site
e1_site:
    { xor32 r3, r3, r3; jalr r1, r2, tgt_e1; st32 r4, r5, 0 }
    { xor32 r0, r0, r0 }

.globl tgt_e1
tgt_e1:
    { add32 r0, r0, r0 }

.size _start, .-_start
