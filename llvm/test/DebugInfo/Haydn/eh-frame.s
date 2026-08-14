// Test the bits of .eh_frame on Haydn that are already implemented correctly.

# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s | llvm-dwarfdump -eh-frame - \
# RUN:    | FileCheck  %s

# .eh_frame CIE return-address column = 15 (R15=LR). Fixed 2026-06-16:
# HaydnMCTargetDesc.cpp MCRegisterInfo init was R13(SP); corrected to R15(LR)
# to match the CodeGen layer (HaydnRegisterInfo.cpp:37).
# Code alignment factor is golden two-byte min bundle-address alignment
# (HaydnFormat.h MinBundleAddressAlignBytes), not EncodedBytes.

func:
  .cfi_startproc
  jalr r0, lr, 0
  .cfi_endproc

# CHECK: 00000000 00000010 00000000 CIE
# CHECK:   Version:               1
# CHECK:   Augmentation:          "zR"
# CHECK:   Code alignment factor: 2
# CHECK:   Data alignment factor: -4
# CHECK:   Return address column: 15
# CHECK:   Augmentation data:     1B
# CHECK:   DW_CFA_def_cfa: R13 +0

# CHECK:   CFA=R13
#
# CHECK: 00000014 00000010 00000018 FDE cie=00000000
# CHECK:   DW_CFA_nop:
# CHECK:   DW_CFA_nop:
# CHECK:   DW_CFA_nop:
# CHECK:   0x0: CFA=R13
