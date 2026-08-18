# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=LEAD96=1 %s -o %t.o
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=LEAD-SEC
# RUN: llvm-readobj -x .text %t.o | FileCheck %s --check-prefix=LEAD-HEX
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s --check-prefix=LEAD-DIS
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=AT_ZERO=1 %s -o %t0.o
# RUN: llvm-readobj -S %t0.o | FileCheck %s --check-prefix=ZERO-SEC

# emitCodeAlignment must pad with whole Format E idle parcels (12 B) up to
# Align(256). From a parcel-aligned offset of 96 the next reachable 256-grid
# address is 768 (56 parcels). A 16-parcel guard, or AsmPrinter's default
# MaxBytesToEmit=Alignment (256 → 21 parcels), stopped at 288, left the
# label off the 256 grid, and later direct B/JAL records failed
# exact-code-record checks. Bound is Align/gcd(12,Align)=64, not the
# MaxBytesToEmit/12 cap. AIE emitCodeAlignment(Align(16)) is the
# power-of-two peer (AIETargetELFStreamer.cpp:73-81); Haydn overlays
# 12-byte idle parcels (header 0x07 + architectural NOP entries).

.ifdef LEAD96
.text
lead:
  .rept 8
  { add32 r1, r2, r3 }
  .endr
  .p2align 8
aligned:
  { add32 r1, r2, r3 }

# LEAD-SEC: Name: .text
# LEAD-SEC: Size: 780
# LEAD-SEC: AddressAlignment: 256

# First idle parcel at 0x60 (offset 96) and last idle at 0x2f4 (756)
# are the generated full-slot idle, not zeros or a short pad.
# llvm-readobj -x prints 16-byte rows: 0x60 is a row start; 0x2f4 is
# the second word of the 0x2f0 row.
# LEAD-HEX: 0x00000060 07000000 00000000 00000000
# LEAD-HEX: 0x000002f0 00000000 07000000 00000000 00000000

# LEAD-DIS-LABEL: <lead>:
# LEAD-DIS:        0:
# LEAD-DIS:       60:
# LEAD-DIS-LABEL: <aligned>:
# LEAD-DIS:      300:
.endif

.ifdef AT_ZERO
.text
  .p2align 8
already:
  { add32 r1, r2, r3 }

# ZERO-SEC: Name: .text
# ZERO-SEC: Size: 12
# ZERO-SEC: AddressAlignment: 256
.endif
