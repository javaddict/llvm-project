//===- HaydnLatencyStalls.cpp - Exposed-pipeline RAW stall insertion ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See HaydnLatencyStalls.h for the ISA rule and why nothing else enforces it.
//
//===----------------------------------------------------------------------===//

#include "HaydnLatencyStalls.h"
#include "Haydn.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnInstrInfo.h"
#include "HaydnPortModel.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "haydn-latency-stalls"

STATISTIC(NumStallBundles, "Number of NOP stall bundles inserted");
STATISTIC(NumUnexpectedOptStallBundles,
          "NOP stall bundles inserted at -O1+ (scheduler/late-mutation gap)");
STATISTIC(NumRegeneratedStallParcels,
          "Previous dest-window stall parcels stripped before re-insert");
STATISTIC(NumStallBytesCharged,
          "Layout bytes charged for inserted stalls via getInstSizeInBytes");
STATISTIC(NumStallAlignPadBytes,
          "Min bundle-address alignment remainder at stall insert prefixes");
STATISTIC(NumLatencyStallResourceAdmissionPinsHeld,
          "Number of latency-stall functions that held fail-closed per-op "
          "resource admission (product closed until golden import)");

namespace {

/// One "cycle" of the exposed pipeline: either a BUNDLE root plus its children
/// (already packed by post-RA leaveRegion) or a single standalone MI that
/// the following Finalize will wrap.
struct Cycle {
  MachineBasicBlock::iterator Boundary; // insertion point for a stall
  SmallVector<MachineInstr *, 4> Members;
};

/// Collect the block as a list of cycles in program order.
static void collectCycles(MachineBasicBlock &MBB,
                          SmallVectorImpl<Cycle> &Cycles) {
  for (MachineBasicBlock::instr_iterator I = MBB.instr_begin(),
                                         E = MBB.instr_end();
       I != E;) {
    MachineInstr &MI = *I;
    if (MI.isBundledWithPred()) {
      // Defensive: a child without a visited root (should not happen).
      ++I;
      continue;
    }
    Cycle C;
    C.Boundary = MachineBasicBlock::iterator(I);
    if (MI.isBundle()) {
      // BUNDLE root carries no encoding itself; the children are the ops.
      ++I;
      while (I != E && I->isBundledWithPred()) {
        if (!I->isMetaInstruction() && !I->isDebugInstr() && !I->isPosition())
          C.Members.push_back(&*I);
        ++I;
      }
    } else {
      if (!MI.isMetaInstruction() && !MI.isDebugInstr() && !MI.isPosition())
        C.Members.push_back(&MI);
      ++I;
    }
    if (!C.Members.empty())
      Cycles.push_back(std::move(C));
  }
}

/// Logical NOP, including generated Format E members (AIE slot NOP overlay).
static bool isLogicalNop(const MachineInstr &MI) {
  return haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode()) == Haydn::NOP;
}

/// Dest-window stall identity. Scheduler/HWLoop insertNoop does not set
/// NoMerge; only this pass marks parcels it owns so a later invocation can
/// strip them (regenerate) without eating resource-idle NOPs.
static bool isRegenerableStallParcel(const MachineInstr &MI) {
  return isLogicalNop(MI) && MI.getFlag(MachineInstr::NoMerge);
}

static unsigned alignmentPadBytes(uint64_t Size) {
  constexpr unsigned A = haydn::format::MinBundleAddressAlignBytes;
  return unsigned((A - (Size % A)) % A);
}

static bool isRealCycleMember(const MachineInstr &MI) {
  return !MI.isMetaInstruction() && !MI.isDebugInstr() && !MI.isPosition();
}

/// Strip stall parcels this pass previously inserted. All-NOP BUNDLEs whose
/// real children all carry the stall pin are idle cycles we own (S2 may have
/// wrapped a bare stall). Mixed BUNDLEs keep stall-flagged NOP children as
/// legal format completion — they do not form extra issue cycles.
static bool eraseRegeneratedStalls(MachineBasicBlock &MBB) {
  bool Changed = false;
  for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;) {
    MachineInstr &MI = *I;
    if (MI.isBundledWithPred()) {
      ++I;
      continue;
    }
    if (MI.isBundle()) {
      bool AllStall = true;
      bool AnyReal = false;
      MachineBasicBlock::instr_iterator C = std::next(MI.getIterator());
      while (C != MBB.instr_end() && C->isBundledWithPred()) {
        if (isRealCycleMember(*C)) {
          AnyReal = true;
          if (!isRegenerableStallParcel(*C))
            AllStall = false;
        }
        ++C;
      }
      MachineBasicBlock::iterator Next(C);
      if (!AnyReal || !AllStall) {
        I = Next;
        continue;
      }
      SmallVector<MachineInstr *, 8> BundleMIs;
      for (MachineBasicBlock::instr_iterator B = std::next(MI.getIterator());
           B != MBB.instr_end() && B->isBundledWithPred();)
        BundleMIs.push_back(&*B++);
      for (MachineInstr *K : BundleMIs) {
        if (isRegenerableStallParcel(*K))
          ++NumRegeneratedStallParcels;
        if (K->isBundledWithPred())
          K->unbundleFromPred();
        if (K->isBundledWithSucc())
          K->unbundleFromSucc();
        K->eraseFromParent();
      }
      MI.eraseFromParent();
      Changed = true;
      I = Next;
      continue;
    }
    if (isRegenerableStallParcel(MI)) {
      I = MBB.erase(I);
      ++NumRegeneratedStallParcels;
      Changed = true;
      continue;
    }
    ++I;
  }
  return Changed;
}

/// Pin a freshly inserted stall: empty MMOs, inherited DebugLoc, no extra
/// liveness, layout charge via the same getInstSizeInBytes range checks use.
static unsigned pinStallParcel(MachineInstr &Nop, MachineFunction &MF,
                               const TargetInstrInfo &TII, const DebugLoc &DL) {
  Nop.setDebugLoc(DL);
  Nop.setFlag(MachineInstr::NoMerge);
  Nop.setMemRefs(MF, {});
  if (Nop.getNumOperands() != 0)
    report_fatal_error(
        "Haydn latency stall NOP grew operands; liveness would be unspecified",
        /*GenCrashDiag=*/false);
  if (!Nop.memoperands_empty())
    report_fatal_error(
        "Haydn latency stall NOP carries MMOs; NOP is not a memory op",
        /*GenCrashDiag=*/false);
  const unsigned Bytes = TII.getInstSizeInBytes(Nop);
  if (Bytes == 0)
    report_fatal_error(
        "Haydn latency stall charges 0 bytes via getInstSizeInBytes; "
        "branch/HWLoop range checks would miss the prefix growth",
        /*GenCrashDiag=*/false);
  NumStallBytesCharged += Bytes;
  return Bytes;
}

static uint64_t layoutBytesTo(MachineBasicBlock &MBB, const TargetInstrInfo &TII,
                             MachineBasicBlock::iterator End) {
  uint64_t Bytes = 0;
  for (MachineBasicBlock::iterator I = MBB.begin(); I != End; ++I)
    Bytes += TII.getInstSizeInBytes(*I);
  return Bytes;
}

} // namespace

char HaydnLatencyStalls::ID = 0;

HaydnLatencyStalls::HaydnLatencyStalls() : MachineFunctionPass(ID) {}

void HaydnLatencyStalls::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool HaydnLatencyStalls::runOnMachineFunction(MachineFunction &MF) {
  // No skipFunction: latency is correctness, not quality. Plain O0 without
  // optnone still runs postmisched (and may hold multi-MI packs); optnone
  // quality-skips postmisched only. Both need this net so Data_Latency
  // windows never leak into the next issue cycle. Inserted bare stall NOPs
  // are committed by the following Finalize (no MC uncommitted escape).
  //
  // Consume the same availability-aware record as pre-RA / ordinary post-RA /
  // SMS / hazard recognizer. Itinerary Data_Latency stays the stall authority;
  // the record only binds the admitted aggregate scaffold (CompleteModel
  // closed, no competitive per-op invent).
  if (!ResourceAdmissionPinned) {
    ResourceAdmissionPinned = true;
    if (!haydnAvailabilityAwareConsumePinsHold())
      report_fatal_error(
          "Haydn latency-stall resource admission pins failed",
          /*GenCrashDiag=*/false);
  }
  ++NumLatencyStallResourceAdmissionPinsHeld;

  const HaydnSubtarget &STI = MF.getSubtarget<HaydnSubtarget>();
  const HaydnInstrInfo &TII = *STI.getInstrInfo();
  const InstrItineraryData *Itin = STI.getInstrItineraryData();
  if (!Itin || Itin->isEmpty())
    return false;

  // -O1+: insertions are unexpected if schedulers already saw architectural
  // latency; still insert for correctness and count for the auditor.
  const bool AuditUnexpected =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None;

  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    // Accumulate-vs-regenerate pin: drop this pass's previous stall parcels
    // before recomputing dest-window need on the current inventory.
    Changed |= eraseRegeneratedStalls(MBB);

    SmallVector<Cycle, 32> Cycles;
    collectCycles(MBB, Cycles);
    if (Cycles.empty())
      continue;

    // Dest-window authority: reuse the HR dest-read / SIN_COS windows
    // (DestReadPending / DestWritePending), not a second pass-local map.
    // Intra-block O1+ insertions stay unexpected-auditor if a later
    // mutation reopens a window the scheduler already serialized.
    HaydnHazardRecognizer DestHR(&TII, Itin, /*IsPreRA=*/false);
    DestHR.Reset();

    auto insertStalls = [&](MachineBasicBlock::iterator InsertPt, DebugLoc DL,
                            unsigned Stalls, StringRef Why, bool Unexpected) {
      LLVM_DEBUG(dbgs() << "HaydnLatencyStalls: " << Stalls
                        << " stall bundle(s) " << Why << "\n");
      for (unsigned I = 0; I < Stalls; ++I) {
        // One NOP-insertion mechanism — TII.insertNoop (Hexagon
        // HexagonInstrInfo.cpp:1667-1671 peer), not a second local BuildMI.
        TII.insertNoop(MBB, InsertPt);
        MachineInstr &Nop = *std::prev(InsertPt);
        pinStallParcel(Nop, MF, TII, DL);
      }
      // Prefix + min bundle-address remainder — same size interface range
      // checks consume (getInstSizeInBytes), not a second byte model.
      const uint64_t Prefix = layoutBytesTo(MBB, TII, InsertPt);
      const unsigned AlignPad = alignmentPadBytes(Prefix);
      NumStallAlignPadBytes += AlignPad;
      LLVM_DEBUG(dbgs() << "  prefix=" << Prefix << " align-pad=" << AlignPad
                        << "\n");
      NumStallBundles += Stalls;
      if (AuditUnexpected && Unexpected) {
        NumUnexpectedOptStallBundles += Stalls;
        LLVM_DEBUG(dbgs() << "  (unexpected at -O1+; scheduler/late gap)\n");
      }
      Changed = true;
    };

    for (Cycle &C : Cycles) {
      unsigned Stalls = 0;
      for (const MachineInstr *MI : C.Members)
        Stalls = std::max(Stalls, DestHR.destWindowStallNeed(*MI));
      if (Stalls) {
        insertStalls(C.Boundary, C.Members.front()->getDebugLoc(), Stalls,
                     "before next consumer", /*Unexpected=*/true);
        for (unsigned I = 0; I < Stalls; ++I)
          DestHR.advanceDestWindows();
      }

      // This cycle retires one pipeline step, then publishes its own defs.
      DestHR.advanceDestWindows();
      for (const MachineInstr *MI : C.Members)
        DestHR.emitForDestWindow(*MI);
    }

    // Conservative block exit: a latency window must not leak into a successor
    // (we cannot know which one runs, and a successor may also be reached from
    // elsewhere). Pad at the end of this block so every outgoing path is
    // covered. Haydn terminators have no Data_Latency > 1 def (JAL/JALR are
    // Branch_Penalty only), so padding in front of them is sufficient.
    // Exit leak is successor-unknown, not an intra-block scheduler gap.
    if (unsigned Leak = DestHR.destWindowExitLeak()) {
      // Pad before the BUNDLE ROOT holding the first terminator, never before
      // a bundle child — that would split the packet.
      MachineBasicBlock::iterator InsertPt = MBB.getFirstTerminator();
      if (InsertPt != MBB.end()) {
        if (MachineInstr *Root = haydn::bundle::bundleRootOf(*InsertPt))
          InsertPt = MachineBasicBlock::iterator(Root->getIterator());
      }
      insertStalls(InsertPt, DebugLoc(), Leak,
                   ("at exit of bb." + Twine(MBB.getNumber())).str(),
                   /*Unexpected=*/false);
    }
  }

  return Changed;
}

INITIALIZE_PASS(HaydnLatencyStalls, DEBUG_TYPE,
                "Haydn Exposed-Pipeline Latency Stalls", false, false)

FunctionPass *llvm::createHaydnLatencyStallsPass() {
  return new HaydnLatencyStalls();
}
