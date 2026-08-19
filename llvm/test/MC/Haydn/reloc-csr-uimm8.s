# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=KIND=1 %s \
# RUN:   | FileCheck --check-prefix=KIND %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=EXT=1 %s \
# RUN:   -o %t.ext.o
# RUN: llvm-readobj -r -h %t.ext.o | FileCheck --check-prefix=EXT %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=IN_RANGE=1 %s \
# RUN:   -o %t.in.o
# RUN: llvm-readobj -r %t.in.o | FileCheck --check-prefix=INREL %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=CSRW_W=1 %s \
# RUN:   -o %t.w.o
# RUN: llvm-readobj -r %t.w.o | FileCheck --check-prefix=CSRW-W %s

# Typed CSR I8 fixup for reloc CSRW / CSRW_W. Golden E2 e0 uimm8 sits at
# parcel bits[39:32] (FieldLsb=32); unsigned 8-bit, ValueShift=0, Align=1.
# Unresolved externals emit R_HAYDN_CSR_UImm8 (ELF 23), never R_HAYDN_8
# (data-section .byte) and never an untyped NONE fixup. Objects keep
# EM_HAYDN=259 / EF_HAYDN_E96=0x1 (no replacement e_machine). CSR numbers
# 0x20-0x25 are ordinary uimm8 values here — same-bundle SET_HWLOOP law
# is not a reloc constraint.

.ifdef KIND
	csrw csr_local, r3
csr_local:
	{ add32 r0, r0, r0 }
# KIND: fixup A - offset: 0, value: csr_local, kind: FIXUP_HAYDN_CSR_UImm8
# KIND-NOT: kind: FIXUP_HAYDN_NONE
# KIND-NOT: kind: FIXUP_HAYDN_32
.endif

.ifdef EXT
	csrw ext_csr, r3
	csrr r1, ext_csr
# EXT: Machine: 0x103
# EXT: Flags [ (0x1)
# EXT: R_HAYDN_CSR_UImm8 ext_csr
# EXT-NOT: R_HAYDN_8
# EXT-NOT: R_HAYDN_32
.endif

.ifdef IN_RANGE
# Absolute I8 is R_ABS: same-section symbols stay as relocs until link
# (unlike PC-rel JALR, which the AsmBackend applies). Field stays 0 in
# the .o; LLD reloc-csr-uimm8.s pins the patched encoding.
.text
	csrw pos_ok, r3
pos_ok:
	{ add32 r0, r0, r0 }
# INREL: R_HAYDN_CSR_UImm8 pos_ok
.endif

.ifdef CSRW_W
	csrw_w ext_csr, r3
# CSRW-W: R_HAYDN_CSR_UImm8 ext_csr
# CSRW-W-NOT: R_HAYDN_8
# CSRW-W-NOT: R_HAYDN_NONE
.endif
