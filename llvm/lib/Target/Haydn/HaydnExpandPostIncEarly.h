//===-- HaydnExpandPostIncEarly.h - Early post-inc expansion ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Product post-inc expansion home (single-home contract, default ON:
// haydn-enable-expand-post-inc-early). Sole product path that expands the
// four Haydn post-increment pseudo instructions (LD32_POST_INC
// ST32_POST_INC / LD64_POST_INC / ST64_POST_INC) into real two-instruction
// equivalents (load/store + ADDI32 base update) BEFORE the VLIW packetizer.
//
// Experimental form-via-HaydnLoadStoreOptimizer (-haydn-enable-ldst-opt
// default OFF) is feature-test only and is not a dual product path; any
// *_POST_INC it creates are still expanded here when this pass is on.
//
// Why expand early (): each *_POST_INC pseudo has NoItinerary
// and is a packetize-region boundary (see). As a result each
// post-inc pseudo packets SOLO if left until late ExpandPseudos, which then
// splits it into LD + ADDI in TWO separate bundles. Expanding here lets the
// packetizer see real itineraries: load slot + ALU slot + independent MAC.
// Late HaydnExpandPseudos remains for every other pseudo and keeps
// expandPseudosInBundles as a safety net.
//
// Pipeline: addPreSched2 after optional LoadStoreOpt (form, opt-in), before
// PostMachineScheduler / VLIW bundle formation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNEXPANDPOSTINCEARLY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNEXPANDPOSTINCEARLY_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnInstrInfo;

// Product post-inc expansion home: expands *_POST_INC pseudos into real
// load/store + ADDI32 BEFORE the VLIW packetizer so real itineraries pack
// the load with the base-update and an independent MAC.
class HaydnExpandPostIncEarly : public MachineFunctionPass {
public:
  static char ID;

  HaydnExpandPostIncEarly();

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "Haydn early post-increment pseudo expansion";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  const HaydnInstrInfo *TII = nullptr;

  // Expand one *_POST_INC pseudo. Inserts the expansion BEFORE MI and erases
  // MI. Returns true on expansion, false if MI is not a post-inc pseudo.
  bool expandMI(MachineBasicBlock &MBB, MachineInstr &MI);
};

// Creates and returns a HaydnExpandPostIncEarly pass.
FunctionPass *createHaydnExpandPostIncEarlyPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNEXPANDPOSTINCEARLY_H
