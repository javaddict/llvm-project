//===-- HaydnRegisterInfo.h - Haydn Register Information Impl --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Haydn implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNREGISTERINFO_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "HaydnGenRegisterInfo.inc"

namespace llvm {

struct HaydnRegisterInfo : public HaydnGenRegisterInfo {
  HaydnRegisterInfo(unsigned HwMode = 0);

  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID id) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  Register getFrameRegister(const MachineFunction &MF) const override;

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;

  const uint32_t *getNoPreservedMask() const override;

  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS) const override;

  bool requiresRegisterScavenging(const MachineFunction &MF) const override {
    return true;
  }

  // Materialize out-of-range FI bases with virtual registers during PEI;
  // scavengeFrameVirtualRegs assigns phys regs afterward. Avoids nested
  // phys-reg scavenge+spill inside eliminateFrameIndex (RISC-V / Hexagon).
  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override {
    return true;
  }

  bool getRegAllocationHints(Register VirtReg, ArrayRef<MCPhysReg> Order,
                             SmallVectorImpl<MCPhysReg> &Hints,
                             const MachineFunction &MF,
                             const VirtRegMap *VRM,
                             const LiveRegMatrix *Matrix) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNREGISTERINFO_H
