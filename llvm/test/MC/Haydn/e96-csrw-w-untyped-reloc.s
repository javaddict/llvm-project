# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o /dev/null 2>&1 | \
# RUN:   FileCheck %s

# Reloc CSRW_W has no published CSR fixup row. Refuse FIXUP_HAYDN_32
# (header clobber) rather than invent a CSR reloc kind.

.text
  csrw_w ext_csr_sym, r1

# CHECK: error: no typed fixup kind
