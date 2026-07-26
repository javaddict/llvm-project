//===- HaydnVerifyBundles.cpp - Bundle invariant MF pass ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Analysis-only pass: walk top-level BUNDLE roots and fail-close via
// haydn::bundle::verifyCommittedBundle (B1.4 / B4.3 late firewall).
//
// AIE peers:
//   AIEBaseInstrInfo.cpp:1440-1459 verifyInstruction fail-closed pattern
//   AIEHazardRecognizer.cpp:278-312 commit surface (finalizeBundle)
//   AIEFinalizeBundle.cpp:40-59 — verify runs immediately after finalize
// Haydn also re-runs after PreEmit BR/Fixup growth (AIE PreEmit empty).
//
//===----------------------------------------------------------------------===//

#include "HaydnVerifyBundles.h"
#include "Haydn.h"
#include "HaydnBundleVerify.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-verify-bundles"

bool HaydnVerifyBundles::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  HaydnMCFormats Fmts;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB.instrs()) {
      // Only top-level BUNDLE roots (committed encode cycles).
      if (MI.isInsideBundle())
        continue;
      if (!MI.isBundle())
        continue;

      if (auto Err = haydn::bundle::verifyCommittedBundle(MI, Fmts)) {
        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << "Haydn bundle invariant violated in " << MF.getName() << " BB#"
           << MBB.getNumber() << ": " << *Err << "\n  MI: " << MI;
        report_fatal_error(Twine(OS.str()));
      }
    }
  }

  // Analysis only — never mutates.
  return false;
}

char HaydnVerifyBundles::ID = 0;

INITIALIZE_PASS(HaydnVerifyBundles, DEBUG_TYPE, "Haydn Bundle Invariant Verifier",
                false, true)

HaydnVerifyBundles::HaydnVerifyBundles() : MachineFunctionPass(ID) {
  initializeHaydnVerifyBundlesPass(*PassRegistry::getPassRegistry());
}

void HaydnVerifyBundles::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

FunctionPass *llvm::createHaydnVerifyBundlesPass() {
  return new HaydnVerifyBundles();
}
