# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readobj -h %t.o | FileCheck %s --check-prefix=HDR
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# REQUIRES: haydn-registered-target

# Role: object — product ELF profile identity.
# Positive: every object from llvm-mc stamps e_flags = EF_HAYDN_E96 (0x1).
# Companion: CodeGen/Haydn/eflags-e96-product-profile.ll (llc path).
# Consumer reject for wrong/zero e_flags: BundleSim bundlesim_test_elf.
# Seat inventory: Inputs/CORRUPTION-MATRIX.txt (profile/ELF).

.text
.globl eflags_probe
.type eflags_probe,@function
eflags_probe:
  add32 r1, r2, r3
  nop

# Experimental EM_HAYDN=259. Distinguished from a 259-KVX object by Flags 0x1.
# Do not invent a replacement e_machine.
# HDR: Machine: 0x103
# HDR: Flags [ (0x1)
# HDR-NEXT: 0x1

# One product parcel per committed cycle → .text size is a multiple of 12.
# SEC: Name: .text
# SEC: Size: 24
