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
#include "HaydnHazardRecognizer.h"
#include "HaydnInstrInfo.h"
#include "HaydnPortModel.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-latency-stalls"

STATISTIC(NumStallBundles, "Number of NOP stall bundles inserted");
STATISTIC(NumUnexpectedOptStallBundles,
          "NOP stall bundles inserted at -O1+ (scheduler/late-mutation gap)");
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
      for (unsigned I = 0; I < Stalls; ++I)
        BuildMI(MBB, InsertPt, DL, TII.get(Haydn::NOP));
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
        MachineBasicBlock::instr_iterator II = InsertPt.getInstrIterator();
        while (II != MBB.instr_begin() && II->isBundledWithPred())
          --II;
        InsertPt = MachineBasicBlock::iterator(II);
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
