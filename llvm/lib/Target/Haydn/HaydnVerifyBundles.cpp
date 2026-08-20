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
#include "llvm/IR/Function.h"
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
  case Haydn::ADJCALLSTACKDOWN:
  case Haydn::ADJCALLSTACKUP:
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

namespace {

/// True when a top-level bare MI would be an uncommitted encode escape if it
/// survived product Finalize. Meta/debug/CFI/KILL/inline-asm and representation
/// expand / late-noop pseudos are not encode cycles.
bool isUncommittedBareEncodeEscape(const MachineInstr &MI) {
  if (MI.isInsideBundle() || MI.isBundle())
    return false;
  if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isPosition() ||
      MI.isInlineAsm() || MI.isKill() || MI.isImplicitDef() ||
      MI.isCFIInstruction())
    return false;
  if (haydn::bundle::isResidualExecutablePseudo(MI))
    return true;
  // Real non-pseudo encode ops (logical or private member) must be BUNDLE
  // children after target-local no-reorder Finalize.
  return !MI.isPseudo();
}

} // namespace

bool HaydnVerifyBundles::runOnMachineFunction(MachineFunction &MF) {
  // Product emission gate: never call skipFunction (FunctionPass::skipFunction
  // is optnone/bisect quality; AIEFinalizeBundle.cpp:40-59 also never skips).
  // Committed-cycle inverse is mandatory for optnone and every other function.
  // A skipFunction-skipped function with a noncanonical cycle must still fail
  // here — never an MC uncommitted/unverified escape hatch.

  // Local registry wrapper (same type as haydnDefaultMCFormats in
  // HaydnBundleFormatSolver.h:96-99) — this pass must not include the
  // forward solver / Bundle.canAdd.
  const HaydnMCFormats Fmts;
  const bool OptNone = MF.getFunction().hasOptNone();

  // Pre-scan: any top-level BUNDLE root means commit ownership has started for
  // this function. Mixed committed roots + bare encode MIs is always illegal
  // (partial Finalize escape). All-bare fixtures remain legal only for
  // non-optnone verify-only MIR unit tests that never ran Finalize.
  bool HasCommittedCycle = false;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB.instrs()) {
      if (!MI.isInsideBundle() && MI.isBundle()) {
        HasCommittedCycle = true;
        break;
      }
    }
    if (HasCommittedCycle)
      break;
  }

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

      // Refuse uncommitted bare encode escape for product commit ownership:
      //   * optnone: postmisched quality-skips reorder, so every bare real
      //     encode after Finalize is an MC standalone escape (all-bare or
      //     mixed). Keyed on hasOptNone(), not skipFunction().
      //   * any function with at least one committed BUNDLE root: mixed bare
      //     encode is a partial-commit hole for plain O0 and optnone alike
      //     (covers independent multi-MI packs that must not leave residual
      //     bare encode beside committed co-issue roots).
      // Plain non-optnone verify-only MIR may still present all-bare MIs
      // before a separate Finalize run. Never change generic skipFunction.
      if ((OptNone || HasCommittedCycle) && isUncommittedBareEncodeEscape(MI)) {
        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << "HaydnVerifyBundles: uncommitted bare encode MI in "
           << MF.getName() << " BB#" << MBB.getNumber();
        if (OptNone)
          OS << " (optnone is no-reorder Format E commit via FinalizeBundle; "
                "refuse MC standalone escape)";
        else
          OS << " (mixed committed BUNDLE + bare encode; target-local "
                "no-reorder Finalize must leave only committed cycles)";
        OS << ":\n  MI: " << MI;
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
        const unsigned KidOpc = I->getOpcode();
        if (haydn::bundle::isResidualCycleFormingPseudo(KidOpc)) {
          std::string Msg;
          raw_string_ostream OS(Msg);
          OS << "HaydnVerifyBundles: residual cycle-forming pseudo child in "
             << MF.getName() << " BB#" << MBB.getNumber()
             << " (one-to-one ban):\n  child: " << *I
             << "\n  root: " << MI;
          report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
        }
        if (haydn::bundle::isExpandOwnedSemanticPseudo(KidOpc) &&
            !haydn::bundle::isRepresentationExpandPseudo(KidOpc)) {
          std::string Msg;
          raw_string_ostream OS(Msg);
          OS << "HaydnVerifyBundles: residual expand-owned pseudo child in "
             << MF.getName() << " BB#" << MBB.getNumber()
             << " (inverse records require completed FormatEInverse on every "
                "residual/logical root; leftover expand-owned is not an "
                "inverse key):\n"
             << "  child: " << *I << "\n  root: " << MI;
          report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
        }
      }

      // Product emission ownership: empty roots fail here; row capacity
      // stays in verifyCommittedBundle so OVER-ISSUE FileCheck still
      // matches. Completion is mandatory golden-row fill (unused windows
      // are architectural NOP → AllEntriesReal), not the stamper helper.
      // Census is still collectBundleMemberOpcodes / bundleHasPadNop so a
      // hand `BUNDLE { NOP }` is product idle on both sides.
      {
        SmallVector<unsigned, 3> Members =
            haydn::bundle::collectBundleMemberOpcodes(MI);
        const bool HasPadNop = haydn::bundle::bundleHasPadNop(MI);
        auto Row = haydn::bundle::getBundleRowID(MI);
        if (Members.empty() && !HasPadNop) {
          std::string Msg;
          raw_string_ostream OS(Msg);
          OS << "HaydnVerifyBundles: empty BUNDLE is not a Format E cycle in "
             << MF.getName() << " BB#" << MBB.getNumber()
             << " (no real members and no architectural NOP fill)";
          if (OptNone)
            OS << " [optnone no-reorder commit]";
          OS << "\n  MI: " << MI;
          report_fatal_error(Twine(OS.str()));
        }
        auto Comp = haydn::bundle::getBundleCompletionID(MI);
        // Completion is mandatory on every residual/logical root that already
        // carries a product row. Missing/unknown row stays a later
        // verifyCommittedBundle diagnostic so NO-IMM / BAD-ID FileCheck still
        // match; inverse-record completion is required once the row exists.
        if (Row && !Comp) {
          std::string Msg;
          raw_string_ostream OS(Msg);
          OS << "HaydnVerifyBundles: missing CompletionStateID on BUNDLE root "
                "in "
             << MF.getName() << " BB#" << MBB.getNumber()
             << " (residual/private members require typed completion)";
          if (OptNone)
            OS << " [optnone no-reorder commit]";
          OS << "\n  MI: " << MI;
          report_fatal_error(Twine(OS.str()));
        } else if (Comp && Row) {
          haydn::bundle::CompletionStateID Expected =
              haydn::bundle::expectedGoldenRowCompletion(
                  static_cast<unsigned>(Members.size()), HasPadNop);
          if (*Comp != Expected) {
            std::string Msg;
            raw_string_ostream OS(Msg);
            OS << "HaydnVerifyBundles: BUNDLE root CompletionStateID does "
                  "not match row and member count in "
               << MF.getName() << " BB#" << MBB.getNumber()
               << " (stamped completion "
               << haydn::bundle::completionToImm(*Comp) << ", expected "
               << haydn::bundle::completionToImm(Expected)
               << " for row member count " << Members.size() << ")";
            if (OptNone)
              OS << " [optnone no-reorder commit]";
            OS << "\n  MI: " << MI;
            report_fatal_error(Twine(OS.str()));
          }
        } else if (Comp && !Members.empty() &&
                   haydn::bundle::isStubCompletion(*Comp)) {
          std::string Msg;
          raw_string_ostream OS(Msg);
          OS << "HaydnVerifyBundles: unqualified stub CompletionStateID on "
                "non-empty BUNDLE in "
             << MF.getName() << " BB#" << MBB.getNumber()
             << " (refuse executable singleton-stub / underfill completion; "
                "product requires full-slot architectural NOP pad)";
          if (OptNone)
            OS << " [optnone no-reorder commit]";
          OS << "\n  MI: " << MI;
          report_fatal_error(Twine(OS.str()));
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
