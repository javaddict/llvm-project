# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o /dev/null 2>&1 | \
# RUN:   FileCheck %s

# Even-byte Align is the ISA window (MinBundleAddressAlignBytes). A
# 2-byte gap is even but not a Format E record (EncodedBytes=12). Linked
# B/JAL targets that miss the parcel grid become BundleSim
# CODE_IMAGE_REJECT. Fail closed at applyFixup. AIE 16-byte bundles make
# any 2^n pad a record; Haydn EncodedBytes is 12.

.text
  jal lr, offgrid
  .space 2
offgrid:
  { nop; nop; xor32 r0, r0, r0 }

# CHECK: error: control relocation target is not an exact Format E record
