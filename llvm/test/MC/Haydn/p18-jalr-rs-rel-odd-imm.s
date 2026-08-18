# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | \
# RUN:   FileCheck %s

# JALR is rs+imm12 (byte, not PC-relative). Odd immediates are legal;
# even-byte Align is the branch/JAL MinBundleAddressAlignBytes law only.
# calltarget_wide_ri12 IsPCRel=0 so getSImmOpValueXStepWide does not
# borrow that window. AIE AIEBaseMCCodeEmitter.h:127-150 applies step
# from the operand class, not a PCRel flag.

# CHECK: jalr{{.*}}encoding:
  jalr r0, r1, 1
# CHECK: jalr{{.*}}encoding:
  jalr r0, r1, -1
