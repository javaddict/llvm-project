//===-- HaydnFixupHwLoops.h - Post-layout HW loop validation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Late pass (after BranchRelaxation): re-check SET_HWLOOP / LoopStart under
// the final size model. Body resolution is CFG-only
// (haydn::hwloop::resolveBodyMBBCore) plus the Fixup-only layout tail
// (resolveBodyMBBFixup). Formation never uses the tail.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNFIXUPHWLOOPS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNFIXUPHWLOOPS_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnInstrInfo;
class MachineInstr;

class HaydnFixupHwLoops : public MachineFunctionPass {
public:
  static char ID;
  HaydnFixupHwLoops();
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "Haydn Hardware Loop Fixup";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  bool fixupOne(MachineInstr &SetMI, const HaydnInstrInfo &TII);
  unsigned countFollowingBundles(MachineInstr &SetMI,
                                 const HaydnInstrInfo &TII) const;
  int64_t estimateMBBDistance(const MachineFunction &MF,
                              const MachineBasicBlock *FromMBB,
                              MachineBasicBlock::const_iterator FromIt,
                              const MachineBasicBlock *ToMBB,
                              const HaydnInstrInfo &TII) const;
  bool computeOffsets(MachineInstr &SetMI, const HaydnInstrInfo &TII,
                      int64_t &StartOff, int64_t &EndOff,
                      MachineBasicBlock *&StartMBB,
                      MachineBasicBlock *&EndMBB) const;
  bool tryShortenStartOffset(MachineInstr &SetMI, const HaydnInstrInfo &TII,
                             int64_t &StartOff, int64_t &EndOff);
};

FunctionPass *createHaydnFixupHwLoopsPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNFIXUPHWLOOPS_H
