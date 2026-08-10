//===- HaydnVerifyBundles.cpp - Bundle invariant MF pass ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Analysis-only pass: walk top-level BUNDLE roots and fail-close via
// haydn::bundle::verifyCommittedBundle (late firewall).
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
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-verify-bundles"

namespace llvm {
namespace haydn {
namespace bundle {

bool isResidualCycleFormingPseudo(unsigned Opc) {
  switch (Opc) {
  case Haydn::LOADI32:
  case Haydn::LOAD_ADDR:
  case Haydn::SETCBR_BEGIN:
  case Haydn::SETCBR_END:
  case Haydn::LoopStart:
  case Haydn::LoopDec:
  case Haydn::LoopJNZ:
  case Haydn::SET_HWLOOP:
  case Haydn::SET_HWLOOP_REG:
    return true;
  default:
    return false;
  }
}

bool isRepresentationExpandPseudo(unsigned Opc) {
  switch (Opc) {
  case Haydn::B:
  case Haydn::RET:
  case Haydn::BR_JT:
  case Haydn::PseudoCALLIndirect:
    return true;
  default:
    return false;
  }
}

static bool isAllowedLateNoopPseudo(unsigned Opc) {
  switch (Opc) {
  case Haydn::ADJCALLSTACKDOWN:
  case Haydn::ADJCALLSTACKUP:
  case Haydn::VAEND:
    return true;
  default:
    return false;
  }
}

bool isExpandOwnedSemanticPseudo(unsigned Opc) {
  switch (Opc) {
  case Haydn::LOAD_ADDR:
  case Haydn::PseudoCALL:
  case Haydn::LIBCALL_SDIV:
  case Haydn::LIBCALL_UDIV:
  case Haydn::LIBCALL_SREM:
  case Haydn::LIBCALL_UREM:
  case Haydn::LIBCALL_MUL64:
  case Haydn::LD32_POST_INC:
  case Haydn::ST32_POST_INC:
  case Haydn::LD64_POST_INC:
  case Haydn::ST64_POST_INC:
  case Haydn::VASTART:
  case Haydn::VACOPY:
  case Haydn::VAARG_I32:
  case Haydn::VAARG_I64:
  case Haydn::VAEND:
  case Haydn::SETCBR_BEGIN:
  case Haydn::SETCBR_END:
  case Haydn::SET_HWLOOP:
  case Haydn::SET_HWLOOP_REG:
    return true;
  default:
    return false;
  }
}

bool isResidualExecutablePseudo(const MachineInstr &MI) {
  if (MI.isBundle() || !MI.isPseudo())
    return false;
  if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isKill() ||
      MI.isImplicitDef() || MI.isCFIInstruction() || MI.isInlineAsm() ||
      MI.isPosition())
    return false;
  if (isRepresentationExpandPseudo(MI.getOpcode()))
    return false;
  if (isAllowedLateNoopPseudo(MI.getOpcode()))
    return false;
  unsigned Opc = MI.getOpcode();
  if (Opc == TargetOpcode::COPY || Opc == TargetOpcode::SUBREG_TO_REG ||
      Opc == TargetOpcode::INSERT_SUBREG ||
      Opc == TargetOpcode::EXTRACT_SUBREG || Opc == TargetOpcode::REG_SEQUENCE)
    return false;
  return true;
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

bool HaydnVerifyBundles::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  HaydnMCFormats Fmts;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB.instrs()) {
      // Bare residual cycle-forming pseudo (not yet wrapped) — fail closed.
      // Shared law: haydn::bundle::isResidualCycleFormingPseudo (AsmPrinter peer).
      if (!MI.isInsideBundle() && !MI.isBundle() &&
          haydn::bundle::isResidualCycleFormingPseudo(MI.getOpcode())) {
        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << "HaydnVerifyBundles: residual cycle-forming pseudo in "
           << MF.getName() << " BB#" << MBB.getNumber()
           << " (exact-commit required before late firewall):\n  MI: "
           << MI;
        report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
      }

      // Only top-level BUNDLE roots (committed encode cycles).
      if (MI.isInsideBundle())
        continue;
      if (!MI.isBundle())
        continue;

      // Residual cycle-forming children inside a committed root.
      for (MachineBasicBlock::instr_iterator I = std::next(MI.getIterator()),
                                             E = MBB.instr_end();
           I != E && I->isInsideBundle(); ++I) {
        if (haydn::bundle::isResidualCycleFormingPseudo(I->getOpcode())) {
          std::string Msg;
          raw_string_ostream OS(Msg);
          OS << "HaydnVerifyBundles: residual cycle-forming pseudo child in "
             << MF.getName() << " BB#" << MBB.getNumber()
             << " (one-to-one ban):\n  child: " << *I
             << "\n  root: " << MI;
          report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
        }
      }

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
