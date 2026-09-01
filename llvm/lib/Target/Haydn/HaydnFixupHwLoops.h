//===-- HaydnFixupHwLoops.h - Post-layout HW loop validation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Late pass (after BranchRelaxation, and on every HaydnLateConvergence
// iteration after S2): re-check SET_HWLOOP / LoopStart under the CURRENT
// size model. Each invocation rewrites Off1/Off2 from the live inventory;
// nested inner demote/resize re-checks the outer (AIE processLoop
// inner-first overlay). Body resolution is CFG-only
// (haydn::hwloop::resolveBodyMBBCore) plus the Fixup-only layout tail
// (resolveBodyMBBFixup). Formation never uses the tail.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNFIXUPHWLOOPS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNFIXUPHWLOOPS_H

#include "llvm/ADT/SmallVector.h"
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

  /// Live SET/LoopStart members from the current layout (instrs walk so a
  /// coissued SET child is not missed). Rebuilt every cascade wave.
  void collectRetainedSetups(MachineFunction &MF, const HaydnInstrInfo &TII,
                             SmallVectorImpl<MachineInstr *> &Sets) const;

  /// True iff Inner sits in Outer's current SET→END layout window so an
  /// inner demote/resize moves Outer's Off1/Off2 (AIE inner-first overlay
  /// without MLI).
  bool setupWindowContains(const MachineInstr &Outer,
                           const MachineInstr &Inner,
                           const HaydnInstrInfo &TII) const;

  /// Stable inner-first order: a SET contained in another's window is
  /// processed before the containing (outer) setup.
  void sortInnermostFirst(SmallVectorImpl<MachineInstr *> &Sets,
                          const HaydnInstrInfo &TII) const;

  /// Inner-first revalidate from the live inventory; re-check remaining
  /// outers after any demote/resize. Demotion is monotone.
  bool revalidateRetainedSetups(MachineFunction &MF,
                                const HaydnInstrInfo &TII);

  /// After a successful demote: latch still lists Header; live trip
  /// countdown+BNEZ remain; preallocated-FI LD/ST carry exact FixedStack
  /// MMOs; frame object count did not grow.
  void gatePostDemotePreservation(MachineFunction &MF,
                                  const HaydnInstrInfo &TII,
                                  MachineBasicBlock *Preheader,
                                  MachineBasicBlock *Header,
                                  MachineBasicBlock *Latch,
                                  unsigned FrameObjectsBefore);
};

FunctionPass *createHaydnFixupHwLoopsPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNFIXUPHWLOOPS_H
