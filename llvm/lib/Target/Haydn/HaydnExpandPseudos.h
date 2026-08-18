//===-- HaydnExpandPseudos.h - Expand pseudo instructions -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Post-RA expansion of Haydn pseudos that still need physical registers or
// a late operand rewrite: LOAD_ADDR, SETCBR, leftover *_POST_INC, leftover
// generic SET_HWLOOP{,_REG} rewrite, and VAEND no-op. Soft-zero R0 restore
// lives in HaydnPostRAScratch; this pass only calls it after leftover
// expand so real JAL_W is visible. Product SET is SET_HWLOOP_F2_W at
// HardwareLoops (HaydnHardwareLoops.cpp:701). Leftover expand-owned
// semantic pseudos and leftover cycle-forming BUNDLE children are fatal.
//
// Relocated owners (not this pass):
//   VASTART / VACOPY / G_VAARG  — HaydnLegalizerInfo
//   integer div/rem libcalls    — legalizer libcallFor
//   direct calls                — CallLowering emits JAL_W + regmask
//   ADJCALLSTACKDOWN/UP         — FrameLowering::eliminateCallFramePseudoInstr
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNEXPANDPSEUDOS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNEXPANDPSEUDOS_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnInstrInfo;

/// Expands Haydn post-RA pseudo instructions into real machine instructions.
/// Cross-bank register moves (MOV_GPR_TO_DR64, MOV_DR64_TO_GPR) are handled
/// in HaydnInstrInfo::expandPostRAPseudo instead, because they create frame
/// indices that must be eliminated by PEI.
class HaydnExpandPseudos : public MachineFunctionPass {
public:
  static char ID;

  HaydnExpandPseudos();

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "Haydn pseudo instruction expansion pass";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  const HaydnInstrInfo *TII = nullptr;

  bool expandMBB(MachineBasicBlock &MBB);
  bool expandMI(MachineBasicBlock &MBB, MachineInstr &MI);

  bool expandLOAD_ADDR(MachineBasicBlock &MBB, MachineInstr &MI);
};

FunctionPass *createHaydnExpandPseudosPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNEXPANDPSEUDOS_H
