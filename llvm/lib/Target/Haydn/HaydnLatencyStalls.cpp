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
#include "HaydnInstrInfo.h"
#include "HaydnResourceRestrictionClasses.h"
#include "HaydnPortModel.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
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
/// or a single standalone MI (pre-finalize residual, or mid-PreEmit bare inserts
/// before the late Finalize re-commit).
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

/// Largest operand cycle in \p SchedClass — i.e. the instruction's documented
/// Data_Latency.
static unsigned classDataLatency(const InstrItineraryData *Itin,
                                 unsigned SchedClass) {
  unsigned Max = 1;
  int FirstOp = Itin->Itineraries[SchedClass].FirstOperandCycle;
  int LastOp = Itin->Itineraries[SchedClass].LastOperandCycle;
  for (int OpIdx = FirstOp; OpIdx < LastOp; ++OpIdx)
    Max = std::max(Max, Itin->OperandCycles[OpIdx]);
  return haydn::restriction::clampPublishedDataLatency(Max);
}

/// Architectural Data_Latency of \p MI's def at \p DefOpIdx, straight from the
/// itinerary. Schedulers also consume the same architectural OperandCycles;
/// this helper still reads the raw itinerary so the O0 net / O1+ auditor stays
/// independent of any future dep-graph rewrite.
static unsigned defLatency(const InstrItineraryData *Itin,
                           const MachineInstr &MI, unsigned DefOpIdx) {
  if (!Itin || Itin->isEmpty())
    return 1;
  unsigned SchedClass = MI.getDesc().getSchedClass();
  if (std::optional<unsigned> Cycle = Itin->getOperandCycle(SchedClass, DefOpIdx))
    if (*Cycle != 0)
      return *Cycle;
  // No per-operand entry for this def. The ISA documents ONE Data_Latency per
  // instruction and lists EVERY written register as a destination, so a
  // post/pre-increment load's base writeback ("rs = rs + imm") sits in the same
  // window as the loaded value. Assuming 1 here is what let
  //   { s_lbu_post_imm r5, r1, 1 }   ; writes r5 AND r1
  //   { ldu8 r1, r1, 0 }             ; reads the r1 writeback
  // through. Take the class maximum instead. If hardware really does forward
  // the address writeback a cycle earlier, that belongs in the itinerary as an
  // explicit per-operand cycle, not as an assumption here.
  return classDataLatency(Itin, SchedClass);
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
  // are re-committed by late Finalize (no MC uncommitted escape).
  //
  // Consume the same availability-aware record as pre-RA / ordinary post-RA /
  // SMS / hazard recognizer. Itinerary Data_Latency stays the stall authority;
  // the record only binds the admitted aggregate scaffold (CompleteModel
  // closed, no competitive per-op invent).
  static bool ResourceAdmissionPinned = false;
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
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();
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

    // Physical register -> number of further cycles before it may be read.
    DenseMap<MCRegister, unsigned> Pending;

    auto readsPending = [&](const Cycle &C) -> unsigned {
      unsigned Worst = 0;
      for (const MachineInstr *MI : C.Members) {
        const unsigned SC = MI->getDesc().getSchedClass();
        for (unsigned OpIdx = 0, E = MI->getNumOperands(); OpIdx != E;
             ++OpIdx) {
          const MachineOperand &MO = MI->getOperand(OpIdx);
          if (!MO.isReg() || !MO.isUse() || !MO.getReg())
            continue;
          // Published read stage of this use; 1 (issue) unless this is a
          // TIED use with an itinerary annotation. AccFirst consumers read
          // the accumulator at the accumulate stage (OperandCycles entry 2),
          // which is what makes golden acc->acc RecMII=1 chains legal
          // back-to-back; the committed accumulator members carry that tied
          // acc use explicitly (CB-152c), and charging it at issue would
          // insert a stall the hardware does not need. ONLY tied uses take
          // the grace: itinerary OperandCycles are positional, so e.g. a
          // store's value use sits on the load class's dest-latency entry —
          // granting untied uses the annotated cycle silently relaxed
          // load->store chains. Same-cycle reads keep going through the
          // RAW/no-forwarding checks elsewhere; this only relaxes the
          // CROSS-cycle wait for the documented late-read accumulator.
          unsigned ReadAt = 1;
          if (MI->getDesc().getOperandConstraint(OpIdx, MCOI::TIED_TO) >= 0)
            if (std::optional<unsigned> Cyc =
                    Itin->getOperandCycle(SC, OpIdx))
              if (*Cyc != 0)
                ReadAt = *Cyc;
          MCRegister Reg = MO.getReg().asMCReg();
          for (const auto &KV : Pending) {
            if (KV.second == 0)
              continue;
            if (Reg == KV.first || TRI.regsOverlap(Reg, KV.first)) {
              const unsigned Grace = ReadAt - 1;
              if (KV.second > Grace)
                Worst = std::max(Worst, KV.second - Grace);
            }
          }
        }
      }
      return Worst;
    };

    auto tick = [&](unsigned N) {
      for (auto &KV : Pending)
        KV.second = KV.second > N ? KV.second - N : 0;
    };

    auto insertStalls = [&](MachineBasicBlock::iterator InsertPt, DebugLoc DL,
                            unsigned Stalls, StringRef Why) {
      LLVM_DEBUG(dbgs() << "HaydnLatencyStalls: " << Stalls
                        << " stall bundle(s) " << Why << "\n");
      for (unsigned I = 0; I < Stalls; ++I)
        BuildMI(MBB, InsertPt, DL, TII.get(Haydn::NOP));
      NumStallBundles += Stalls;
      if (AuditUnexpected) {
        NumUnexpectedOptStallBundles += Stalls;
        LLVM_DEBUG(dbgs() << "  (unexpected at -O1+; scheduler/late gap)\n");
      }
      Changed = true;
    };

    for (Cycle &C : Cycles) {
      if (unsigned Stalls = readsPending(C)) {
        insertStalls(C.Boundary, C.Members.front()->getDebugLoc(), Stalls,
                     "before next consumer");
        tick(Stalls);
      }

      // This cycle retires one pipeline step for everything already pending,
      // then publishes its own defs.
      tick(1);
      for (const MachineInstr *MI : C.Members) {
        for (unsigned OpIdx = 0, E = MI->getNumOperands(); OpIdx != E; ++OpIdx) {
          const MachineOperand &MO = MI->getOperand(OpIdx);
          if (!MO.isReg() || !MO.isDef() || !MO.getReg())
            continue;
          unsigned Lat = defLatency(Itin, *MI, OpIdx);
          if (Lat > 1)
            Pending[MO.getReg().asMCReg()] = Lat - 1;
        }
      }
    }

    // Conservative block exit: a latency window must not leak into a successor
    // (we cannot know which one runs, and a successor may also be reached from
    // elsewhere). Pad at the end of this block so every outgoing path is
    // covered. Haydn terminators have no Data_Latency > 1 def (JAL/JALR are
    // Branch_Penalty only), so padding in front of them is sufficient.
    unsigned Leak = 0;
    for (const auto &KV : Pending)
      Leak = std::max(Leak, KV.second);
    if (Leak) {
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
                   ("at exit of bb." + Twine(MBB.getNumber())).str());
    }
  }

  return Changed;
}

INITIALIZE_PASS(HaydnLatencyStalls, DEBUG_TYPE,
                "Haydn Exposed-Pipeline Latency Stalls", false, false)

FunctionPass *llvm::createHaydnLatencyStallsPass() {
  return new HaydnLatencyStalls();
}
