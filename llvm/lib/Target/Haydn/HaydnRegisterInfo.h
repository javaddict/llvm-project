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

  /// R15 (LR) may be listed as an inline-asm clobber — PEI force-saves it.
  /// R0 / R13 / R14-when-FP are architectural roles and are not clobberable.
  bool isAsmClobberable(const MachineFunction &MF,
                        MCRegister PhysReg) const override;

  /// Soft-zero, SP, and the live frame pointer cannot be written in asm.
  bool isInlineAsmReadOnlyReg(const MachineFunction &MF,
                              MCRegister PhysReg) const override;

  /// SP (R13) when !hasFP; architectural FP R14 ("fp") when hasFP.
  /// Dyn-alloca fixed-frame base is the same FP (BP folded into FP).
  Register getFrameRegister(const MachineFunction &MF) const override;

  /// No separate base pointer — always false. BP role is on FP (R14).
  bool hasBasePointer(const MachineFunction &MF) const;

  /// Same as architectural FP (R14). API compatibility only.
  Register getBaseRegister() const;

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;

  const uint32_t *getNoPreservedMask() const override;

  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS) const override;

  bool requiresRegisterScavenging(const MachineFunction &MF) const override {
    return true;
  }

  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override {
    return true;
  }

  /// Soft physreg order via TRI hints only:
  /// base copy/coalesce > optional compact-subset (-haydn-ra-compact-hints,
  /// default OFF; dual-run spill/reload/Hit/post-RA multi-MI stats gate enable)
  /// > caller-saved preference. Never demotes RC; wide Order remains complete.
  bool getRegAllocationHints(Register VirtReg, ArrayRef<MCPhysReg> Order,
                             SmallVectorImpl<MCPhysReg> &Hints,
                             const MachineFunction &MF,
                             const VirtRegMap *VRM,
                             const LiveRegMatrix *Matrix) const override;
};

/// Compact-subset membership for metrics-gated soft physreg ordering.
/// True for GPR32Lo (R0–R7) and low-DR encodings D0–D7. Soft preference only —
/// not a hard operand-class constraint / RC demotion. Enable remains default
/// OFF under Full-only until dual-run corpus metrics accept a compact row.
bool isHaydnCompactSubsetPhysReg(const TargetRegisterInfo &TRI,
                                 MCPhysReg PhysReg);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNREGISTERINFO_H
