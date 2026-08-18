# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o /dev/null 2>&1 | \
# RUN:   FileCheck %s

# JAL symbolic targets share the branch even-byte law (Align=2,
# MinBundleAddressAlignBytes). An odd same-section label must fail at
# applyFixup, not encode as a mid-record control displacement.

.text
  jal lr, oddtgt
  .space 1
oddtgt:
  { nop; nop; xor32 r0, r0, r0 }

# CHECK: error: mis-aligned relocation target
