//===- HaydnVerifyBundles.cpp - Bundle invariant MF pass ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Analysis-only pass: walk top-level BUNDLE roots and fail-close via
// haydn::bundle::verifyCommittedBundle (late firewall). The last instance
// in a complete pipeline is the addPreEmitPass2 freeze gate: one concrete
// generated-member BUNDLE, no representation-expand carve-out, no leftover
// alternate-map / DDG transients, and a dest-window seam walk
// (verifyMBBDestWindowSeams; not verifyCommittedBundle) plus an
// independent EncodedBytes layout recompute (verifyFrozenLayout). Since D1.55 the
// freeze seat also rejects TOP-LEVEL RET/BR_JT/PseudoCALLIndirect as
// representation escapes (every Finalize seat already expanded them)
// and TOP-LEVEL B as a wrap escape (wrap-only Finalize keeps B; encoder
// peels B → BEQZ rs=R0). Standalone returning call is JALR_CALL.
// The two intermediate seats (addPreSched2/addPreEmitPass,
// IsFreezeSeat=false) keep the explicit
// pre-expansion-residual carve-out. Every Verify
// seat first runs the D1.54 member-shape wall inside the MI overload:
// unconditional generated descriptor (arity/kind), tie, implicit, and
// side-effect validation per member — before the inverse/resource
// predicates and never via the optional MachineVerifier.
//
// AIE peers:
//   AIEBaseInstrInfo.cpp:1616-1635 verifyInstruction fail-closed pattern
//   AIEHazardRecognizer.cpp:278-312 commit surface (finalizeBundle)
//   AIEFinalizeBundle.cpp:40-59 — verify runs immediately after finalize
//   AIEMachineScheduler.cpp:1081-1082 / AIEAlternateDescriptors.h:74
//     leaveRegion clears AltDescs (Haydn freeze requires empty transients)
// Haydn also re-runs after PreEmit BR/Fixup growth (AIE PreEmit empty).
//
//===----------------------------------------------------------------------===//

#include "HaydnVerifyBundles.h"
#include "Haydn.h"
#include "HaydnBundleVerify.h"
#include "HaydnHWLoopDemote.h"
#include "HaydnMachineFunctionInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#if defined(LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLE_H)
#error "HaydnVerifyBundles.cpp must not include HaydnBundle.h (no Bundle.canAdd)"
#endif
#if defined(LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEFORMATSOLVER_H)
#error \
    "HaydnVerifyBundles.cpp must not include HaydnBundleFormatSolver.h (no forward solver)"
#endif

using namespace llvm;

#define DEBUG_TYPE "haydn-verify-bundles"

static cl::opt<bool> HaydnFreezeVerify(
    "haydn-freeze-verify", cl::Hidden,
    cl::desc("Force HaydnVerifyBundles freeze-gate checks (residual "
             "representation/logical/mixed children, surviving "
             "alternate-map/DDG transients, dest-window seams, and "
             "independent EncodedBytes layout/displacement)"),
    cl::init(false));

namespace llvm {
namespace haydn {
namespace bundle {

bool isResidualCycleFormingPseudo(unsigned Opc) {
  switch (Opc) {
  case Haydn::LOADI32:
  case Haydn::LOADI64:
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
    // PseudoLoopEnd is isMeta=1 (latch MBB for analyzeBranch). AsmPrinter
    // drops it with no bytes; it is not a residual cycle-forming encode.
    return false;
  }
}

bool isRepresentationExpandPseudo(unsigned Opc) {
  // D1.130: B is product gMIR uncond through encode (MC peels to BEQZ rs=R0).
  // JALR_CALL / JAL_TCO are product gMIR calls, not printer-expand shells.
  switch (Opc) {
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
  case Haydn::LOADI32:
  case Haydn::LOADI64:
  case Haydn::LOAD_ADDR:
  case Haydn::PseudoCALL:
  case Haydn::LIBCALL_SDIV:
  case Haydn::LIBCALL_UDIV:
  case Haydn::LIBCALL_SREM:
  case Haydn::LIBCALL_UREM:
  case Haydn::LIBCALL_MUL64:
  case Haydn::MOV_GPR_TO_DR64:
  case Haydn::MOV_DR64_TO_GPR:
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
/// survived product Finalize. Meta/debug/CFI/KILL/inline-asm and late-noop
/// pseudos are not encode cycles. Representation-expand shells (B/RET/
/// BR_JT/PseudoCALLIndirect) are admitted as pre-expansion residual at the
/// two intermediate Verify seats ONLY (D1.55 / GR1.7): Finalize runs
/// expandLeftoverRetJtCall in the S1 owner (and unstamped Finalize) and
/// wrap-only keeps B (the last wrap-only Finalize is addPreEmit after
/// the LBN closer; addPostBBSections is empty and freeze is
/// addPreEmitPass2), so a top-level RET/BR_JT/PseudoCALLIndirect or
/// unwrapped B at freeze is an uncommitted escape. Standalone JALR_CALL
/// is legal (encoder peels to JALR).
bool isUncommittedBareEncodeEscape(const MachineInstr &MI, bool Freeze) {
  if (MI.isInsideBundle() || MI.isBundle())
    return false;
  if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isPosition() ||
      MI.isInlineAsm() || MI.isKill() || MI.isImplicitDef() ||
      MI.isCFIInstruction())
    return false;
  // Returning fnptr / musttail overlays: legal standalone. Golden JALR
  // members are isTerminator=1, so a mid-block call never takes a member
  // Desc; encoder peels JALR_CALL / JAL*_TCO onto the same JALR/JAL
  // member as jump. JAL_W direct calls bundle normally (isTerminator=0).
  const unsigned Opc = MI.getOpcode();
  if (Opc == Haydn::JALR_CALL || Opc == Haydn::JAL_TCO ||
      Opc == Haydn::JALR_TCO)
    return false;
  if (haydn::bundle::isRepresentationExpandPseudo(MI.getOpcode())) {
    // D1.55 freeze-seat representation-escape law: ExpandPseudos ran
    // before S1, S1 leftover-RET-expands before stamp, and stamped
    // Finalize is wrap-only (AIEFinalizeBundle.cpp:40-59) plus inverse
    // completion via complete packet templates. A surviving top-level
    // RET/BR_JT/PseudoCALLIndirect at freeze is a representation escape and a
    // surviving top-level B is a wrap escape — printer expansion is not
    // a freeze carve-out. The two intermediate seats still admit the
    // shell as pre-expansion residual (documented carve-out;
    // BranchRelaxation/FixupHwLoops legally sit between them and the
    // next Finalize).
    return Freeze;
  }
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

  // HexagonVLIWPacketizer.cpp:90/205 addRequired AA; AMDGPU
  // SIInsertWaitcnts.cpp:963/3232 used-if-available. Limited -run-pass
  // has no AA (fail-closed via mayAlias). Full pipeline AA lets verify
  // accept the same proven-disjoint store/load packs HR/materialize did.
  AAResults *AA = nullptr;
  if (auto *AAR = getAnalysisIfAvailable<AAResultsWrapperPass>())
    AA = &AAR->getAAResults();

  // Freeze seat law (D1.13): freeze identity is bound by REGISTRATION,
  // never by instance order or count. The only instance constructed with
  // IsFreezeSeat=true is the one added at the addPreEmitPass2 seat
  // (HaydnTargetMachine.cpp addPreEmitPass2); the two earlier Verify
  // seats (addPreSched2, addPreEmitPass) pass explicit
  // false and are invariant-only by construction. The legacy
  // default-constructed instance (-run-pass=haydn-verify-bundles) is
  // likewise invariant-only. -haydn-freeze-verify forces freeze on ANY
  // instance. No process-global state participates: neither pipeline
  // census drift nor per-thread pass cloning under parallel codegen can
  // silently move or disable a freeze wall.
  // Peer: AIE leaveRegion clears AltDescs (AIEMachineScheduler.cpp:1081-1082;
  // AIEAlternateDescriptors.h:74) before the next region; Haydn freeze
  // requires the same empty transients after closure.
  LLVM_DEBUG(dbgs() << "HaydnVerifyBundles seat="
                    << (FreezeSeat ? "freeze" : "invariant") << " on "
                    << MF.getName() << '\n');
  const bool Freeze = HaydnFreezeVerify || FreezeSeat;

  if (Freeze) {
    const HaydnMachineFunctionInfo *MFI =
        MF.getInfo<HaydnMachineFunctionInfo>();
    if (!MFI->getAltDescs().empty()) {
      report_fatal_error(
          Twine("HaydnVerifyBundles: ") + MF.getName() +
              ": freeze leftover alternate-map transient after closure",
          /*GenCrashDiag=*/false);
    }
    if (MFI->getInterBlockRegistry()) {
      report_fatal_error(
          Twine("HaydnVerifyBundles: ") + MF.getName() +
              ": freeze leftover inter-block DDG transient after closure",
          /*GenCrashDiag=*/false);
    }
  }

  // Frame-deadline law: PEI froze object offsets and stack size; the first
  // post-PEI Haydn pass snapshotted both (HaydnExpandPseudos). Any later
  // frame-object creation or stack-size change is a pipeline-contract
  // violation. No snapshot (MIR fixtures bypassing the post-PEI lane)
  // stays legal.
  if (std::string Violation =
          MF.getInfo<HaydnMachineFunctionInfo>()->frameFreezeViolation(
              MF.getFrameInfo());
      !Violation.empty()) {
    report_fatal_error(
        Twine("HaydnVerifyBundles: ") + MF.getName() + ": " + Violation,
        /*GenCrashDiag=*/false);
  }

  // GR2.7/D1.40 postcommit CFG identity wall (independent repeat of the
  // seat-level insertIndirectBranch refusal): after the first Finalize run
  // stamped the per-function CFG identity snapshot, the postcommit CFG is
  // identity-frozen — L1/L2 live count, L4 per-MBB identity tokens
  // (CreationID folded in; null-BB/no-BBID replacement is L4, not a
  // colliding NoBBIDSentinel), epoch-guarded L3 numbering slack, L5
  // successor digest (no-exception edge wall), and L6 creation
  // high-water. Generic BranchRelaxation is the only postcommit block
  // creator (trampoline/RestoreBB/split arms). L3 stays epoch-guarded
  // so BR entry RenumberBlocks remains legal; L6 is the un-launderable
  // twin, so post-renumber create-then-delete cannot hide behind
  // compacted numbering slots. Same postCommitCfgCreationViolation
  // owner — no second predicate. No stamp (limited-pipeline probes /
  // MIR fixtures that never run Finalize) observes no wall. Fires at
  // every Verify seat incl. freeze.
  if (std::string CfgViolation =
          MF.getInfo<HaydnMachineFunctionInfo>()
              ->postCommitCfgCreationViolation(MF);
      !CfgViolation.empty()) {
    report_fatal_error(
        Twine("HaydnVerifyBundles: ") + MF.getName() + ": " + CfgViolation,
        /*GenCrashDiag=*/false);
  }

  // D1.36/GR2.8 counted software-latch order wall. Dual-seat with the
  // demote producer (same shape as postCommitCfgCreationViolation above):
  // every Verify instance including freeze, not freeze-only. Walk every
  // MBB; nonempty haydn::hwloop::countedSoftwareLatchViolation is
  // corruption-class — unconditional report_fatal_error. Discriminator
  // (countdown vocabulary) lives in the shared predicate so ordinary
  // branches do not false-fire. AIE peer: AIEBaseInstrInfo.cpp:1587-1635
  // verifyControlFlowConstraints + verifyInstruction (TII verify is the
  // independent fail-closed seat). Do not include HaydnBundle.h /
  // HaydnBundleFormatSolver.h; do not grow a second latch-order machine.
  for (const MachineBasicBlock &LatchMBB : MF) {
    if (std::string LatchViolation =
            haydn::hwloop::countedSoftwareLatchViolation(LatchMBB);
        !LatchViolation.empty()) {
      report_fatal_error(
          Twine("HaydnVerifyBundles: ") + MF.getName() + ": " + LatchViolation,
          /*GenCrashDiag=*/false);
    }
  }

  // D1.150 dual-seat sibling of countedSoftwareLatchViolation: exact
  // ST32/LD32 FixedStack pair on this latch's bound demote-save FI.
  for (const MachineBasicBlock &LatchMBB : MF) {
    if (std::string SaveViolation =
            haydn::hwloop::demoteSaveHomePairViolation(LatchMBB);
        !SaveViolation.empty()) {
      report_fatal_error(
          Twine("HaydnVerifyBundles: ") + MF.getName() + ": " + SaveViolation,
          /*GenCrashDiag=*/false);
    }
  }

  // GR1.8 / GR2.8 freeze-only independent layout wall. Intermediate
  // Verify seats stay silent on range/align/complete-tail/vreg-FI so
  // pre-pad probes remain legal. Runs before inverse so duplicate-control
  // FileChecks this string rather than unit injectivity. Does not call
  // TII.isBranchOffsetInRange.
  if (Freeze) {
    if (auto LayoutErr = haydn::bundle::verifyFrozenLayout(MF)) {
      report_fatal_error(Twine("HaydnVerifyBundles: ") + MF.getName() + ": " +
                             *LayoutErr,
                         /*GenCrashDiag=*/false);
    }
  }

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
      //   * optnone: since GR2.4 the mandatory scheduler
      //     (forcePostRAScheduling) commits optnone via singleton/co-issue
      //     packets itself, so every bare real encode after Finalize is still
      //     an MC standalone escape (all-bare or mixed). Keyed on
      //     hasOptNone(), not skipFunction().
      //   * any function with at least one committed BUNDLE root: mixed bare
      //     encode is a partial-commit hole for plain O0 and optnone alike
      //     (covers independent multi-MI packs that must not leave residual
      //     bare encode beside committed co-issue roots).
      // Plain non-optnone verify-only MIR may still present all-bare MIs
      // before a separate Finalize run. Never change generic skipFunction.
      if ((Freeze || OptNone || HasCommittedCycle) &&
          isUncommittedBareEncodeEscape(MI, Freeze)) {
        const bool ReprEscape =
            Freeze && haydn::bundle::isRepresentationExpandPseudo(MI.getOpcode());
        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << "HaydnVerifyBundles: uncommitted bare encode MI in "
           << MF.getName() << " BB#" << MBB.getNumber();
        if (ReprEscape)
          OS << " (freeze representation-expand escape; every Finalize "
                "seat already expanded B/RET/BR_JT/PseudoCALLIndirect; "
                "printer expansion is not a verifier carve-out)";
        else if (OptNone)
          OS << " (optnone is no-reorder Format E commit via FinalizeBundle; "
                "refuse MC standalone escape)";
        else if (HasCommittedCycle)
          OS << " (mixed committed BUNDLE + bare encode; target-local "
                "no-reorder Finalize must leave only committed cycles)";
        else
          OS << " (freeze residual bare encode; printer expansion is not a "
                "verifier carve-out)";
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
        if (Freeze && haydn::bundle::isRepresentationExpandPseudo(KidOpc)) {
          std::string Msg;
          raw_string_ostream OS(Msg);
          OS << "HaydnVerifyBundles: residual representation-expand pseudo "
                "child in "
             << MF.getName() << " BB#" << MBB.getNumber()
             << " (printer expansion is not a verifier carve-out):\n"
             << "  child: " << *I << "\n  root: " << MI;
          report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
        }
        if (haydn::bundle::isExpandOwnedSemanticPseudo(KidOpc)) {
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
        if (haydn::bundle::isLeftoverGenericResidualPseudo(KidOpc)) {
          std::string Msg;
          raw_string_ostream OS(Msg);
          OS << "HaydnVerifyBundles: residual/logical inverse record not "
                "completed at membership entry in "
             << MF.getName() << " BB#" << MBB.getNumber()
             << " (leftover generic residual is not an inverse key):\n"
             << "  child: " << *I << "\n  root: " << MI;
          report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
        }
      }

      // Product emission ownership: empty roots fail here; row capacity
      // stays in verifyCommittedBundle so OVER-ISSUE FileCheck still
      // matches. Completion is mandatory golden-row fill via
      // expectedGoldenRowCompletion (unused windows are architectural
      // NOP → AllEntriesReal). Never selectCompletionForMembersAndPads.
      // Census is still collectBundleMemberOpcodes / bundleHasPadNop so a
      // hand `BUNDLE { NOP }` is product idle on both sides. Inverse
      // records use encode-dag order including pad holes (never compact
      // a later residual onto an earlier entry).
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

      if (auto Err = haydn::bundle::verifyCommittedBundle(MI, Fmts, nullptr,
                                                          Freeze, AA)) {
        // Unexpanded representation pseudos fail as ordinary diagnostics
        // (exit 1): MC-encode refuse class, same policy as the printer.
        // Every other structural corruption aborts (--crash fixtures).
        bool ReprOnly = StringRef(*Err).contains(
            "residual representation-expand pseudo");
        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << "Haydn bundle invariant violated in " << MF.getName() << " BB#"
           << MBB.getNumber() << ": " << *Err << "\n  MI: " << MI;
        report_fatal_error(Twine(OS.str()),
                           /*GenCrashDiag=*/!ReprOnly);
      }
    }
  }

  // Freeze-only dest-window seam wall. Intermediate Verify seats stay
  // silent (pre-stall false positives). Do not call from
  // verifyCommittedBundle (intra-cycle by D1.4 law).
  if (Freeze) {
    for (const MachineBasicBlock &SeamMBB : MF) {
      if (auto Err = haydn::bundle::verifyMBBDestWindowSeams(SeamMBB)) {
        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << "HaydnVerifyBundles: dest-window seam in " << MF.getName()
           << " BB#" << SeamMBB.getNumber() << ": " << *Err;
        report_fatal_error(Twine(OS.str()));
      }
    }
  }

  // Analysis only — never mutates.
  return false;
}

char HaydnVerifyBundles::ID = 0;

// isAnalysis=false: this is a fail-closed invariant CHECKER, not a
// computed analysis. With isAnalysis=true the legacy PM's schedulePass
// dedup (PMTopLevelManager::findAnalysisPass over live AvailableAnalysis)
// silently drops the second-lane instances — the W68.2R freeze seat at
// addPreEmitPass2 (and any later lane) must always run. Preservation is
// declared via AU.setPreservesAll() below, not via the analysis flag.
INITIALIZE_PASS(HaydnVerifyBundles, DEBUG_TYPE, "Haydn Bundle Invariant Verifier",
                false, false)

// Seat identity is bound at construction (D1.13): the addPreEmitPass2
// adder passes IsFreezeSeat=true; the addPreSched2 and addPreEmitPass
// seats and the legacy -run-pass default construction pass false. No
// instance-count heuristic and no cross-instance globals remain.
HaydnVerifyBundles::HaydnVerifyBundles(bool IsFreezeSeat)
    : MachineFunctionPass(ID), FreezeSeat(IsFreezeSeat) {
  initializeHaydnVerifyBundlesPass(*PassRegistry::getPassRegistry());
}

// Legacy default: REQUIRED for RegisterPass / INITIALIZE_PASS -run-pass
// construction; that instance is invariant-only by explicit construction.
HaydnVerifyBundles::HaydnVerifyBundles() : HaydnVerifyBundles(false) {}

void HaydnVerifyBundles::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  // Used-if-available, not required: limited -run-pass has no AA.
  // Peer: AMDGPU SIInsertWaitcnts.cpp:963. Hexagon packetizer is addRequired.
  AU.addUsedIfAvailable<AAResultsWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

// No default argument: every seat must state its freeze identity at the
// call site, so any future seat addition is a compile-time decision, never
// a silent census drift.
FunctionPass *llvm::createHaydnVerifyBundlesPass(bool IsFreezeSeat) {
  return new HaydnVerifyBundles(IsFreezeSeat);
}
