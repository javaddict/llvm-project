# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=BUNDLE=1 %s \
# RUN:   -o %t.b.o
# RUN: llvm-readobj -r %t.b.o | FileCheck %s

# Reloc CSRW_W cutovers to the catalog CSRW I8 member. Encode consumes the
# typed CSR uimm8 fixup (R_HAYDN_CSR_UImm8) — never FIXUP_HAYDN_32 header
# clobber. Bundled and standalone share the same reloc kind.

.text
.ifndef BUNDLE
  csrw_w ext_csr_sym, r1
.else
  { csrw_w ext_csr_sym, r1 }
.endif

# CHECK: R_HAYDN_CSR_UImm8 ext_csr_sym
