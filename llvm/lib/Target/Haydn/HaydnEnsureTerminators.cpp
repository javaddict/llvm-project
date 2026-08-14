//===-- HaydnEnsureTerminators.cpp - Dead-end MBB terminators ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// systematic fix: ensure every successor-empty machine basic block has
// a terminator. Primary coverage for:
//
// * whole-function empty `unreachable` bodies
// * mid-function `unreachable` MBBs (PEI never visits non-return blocks)
// * any other dead-end left without a terminator after CFG/RA/PEI
//
// Soft policy: emit Haydn::RET (uses LR). A true trap is preferable for UB
// fidelity but Haydn has no dedicated trap opcode on the live path; RET
// prevents fall-through into the next function (BundleSim PC-not-found / wrong
// code). Real returns already have RET from CallLowering.
//
// Placement: addPostRegAlloc (pre-PEI) so invented RET can receive
// prologue/epilogue. FrameLowering may also plant RET on empty entry as a
// belt; this pass is the invariant enforcer. Runs for optnone and every other
// function (no skipFunction) so mid-function unreachable arms cannot fall
// through into the next symbol after PEI.
//
//===----------------------------------------------------------------------===//

#include "HaydnEnsureTerminators.h"
#include "Haydn.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "haydn-ensure-terminators"

using namespace llvm;

STATISTIC(NumDeadEndRets,
          "Number of RET terminators inserted on succ-empty MBBs");

char HaydnEnsureTerminators::ID = 0;

INITIALIZE_PASS(HaydnEnsureTerminators, DEBUG_TYPE,
                "Haydn Ensure Dead-End Terminators", false, false)

FunctionPass *llvm::createHaydnEnsureTerminatorsPass() {
  return new HaydnEnsureTerminators();
}

HaydnEnsureTerminators::HaydnEnsureTerminators() : MachineFunctionPass(ID) {
  initializeHaydnEnsureTerminatorsPass(*PassRegistry::getPassRegistry());
}

void HaydnEnsureTerminators::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool HaydnEnsureTerminators::runOnMachineFunction(MachineFunction &MF) {
  // Correctness path: never call skipFunction. Generic PostMachineScheduler
  // may still quality-skip optnone (no reorder); dead-end terminator insertion
  // is target-local ownership that must run for every function so PEI can
  // attach epilogues and product emission never falls through empty MBBs into
  // the next symbol. Never change generic skipFunction semantics for quality
  // passes.

  const HaydnInstrInfo *TII =
      MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    // EH pads have their own control-flow contract; do not invent returns.
    if (MBB.isEHPad())
      continue;

    // Fallthrough / branch targets always have successors listed in MIR, even
    // when the terminator is a fallthrough with no MI. Only true dead-ends
    // (no CFG successors) need an artificial terminator.
    if (!MBB.succ_empty())
      continue;

    if (MBB.getFirstTerminator() != MBB.end())
      continue;

    // Empty or non-empty dead-end without a terminator → soft RET.
    DebugLoc DL;
    if (!MBB.empty())
      DL = MBB.back().getDebugLoc();

    BuildMI(MBB, MBB.end(), DL, TII->get(Haydn::RET));
    ++NumDeadEndRets;
    Changed = true;

    LLVM_DEBUG(dbgs() << "inserted RET on dead-end " << MBB.getName()
                      << " in " << MF.getName() << '\n');
  }

  return Changed;
}
