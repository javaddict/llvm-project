# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readobj -h %t.o | FileCheck %s --check-prefix=HDR
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC

# Torture CODE_IMAGE_REJECT is three independent object facts, not one
# opcode bug. Diagnose them here; do not invent EM/e_flags/pad answers.
#
#   1) e_machine 0x103 (259) is the experimental Haydn producer number.
#      Official ELF 259 is Kalray KVX — distinguished only by Flags 0x1.
#   2) e_flags must be EF_HAYDN_E96 (0x1). Zero flags are refused at LLD.
#   3) .text size stays on the 12-byte parcel grid. A 4/8-byte pad or a
#      MaxBytesToEmit-capped align walk leaves later B/JAL records
#      off-grid (BundleSim "direct control target is not an exact code
#      record"). See e96-text-align-256-maxparcels.s and
#      e96-control-target-not-record.s for the align/bytes arms.

.text
.p2align 4
aligned:
  { add32 r1, r2, r3 }
  { jal lr, aligned }

# HDR: Machine: 0x103
# HDR: Flags [ (0x1)

# SEC: Name: .text
# SEC: Size: 24
# SEC: AddressAlignment: 16
