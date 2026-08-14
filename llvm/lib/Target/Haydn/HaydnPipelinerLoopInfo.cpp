//===-- HaydnPipelinerLoopInfo.cpp - SMS PipelinerLoopInfo ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn implementation of TargetInstrInfo::PipelinerLoopInfo.
// Peer: AIEBasePipelinerLoopInfo.cpp (helpers getInstrSequence 408-429,
// collectLiveInRegs 431-456, canAllocate 458-518, ZeroOverheadLoop
// shouldUseSchedule 826-835 / canAcceptII 839-847).
// shouldUseSchedule polarity is unchanged; product multi-stage SMS stays
// post-RA only (pre-RA StageCount>1 containment).
//
//===----------------------------------------------------------------------===//

#include "HaydnPipelinerLoopInfo.h"
#include "Haydn.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnInstrInfo.h"
#include "HaydnPortModel.h"
#include "HaydnPreRASchedStrategy.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachinePipeliner.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-pipeliner"

STATISTIC(NumSMSSharedResourceRecordConsumes,
          "Number of SMS shouldUseSchedule checks that consumed the shared "
          "availability-aware resource record (CompleteModel closed)");

// Stage-0 PostPipeliner deleted — prefer-PP path retired (always false).
static constexpr bool EnableZOLPreferPostPipeliner = false;

// PPS-3: AIE-style stage-count gate for SMS (into shouldUseSchedule).
static cl::opt<unsigned> HaydnSMSMaxStageCount(
    "haydn-sms-max-stagecount", cl::Hidden, cl::init(3),
    cl::desc("PPS-3: reject SMS schedules with more than this many stages "
             "(prologue stages + 1). Default 3 (AIE LoopMaxStageCount)."));

// PPS-3: AIE-style reg-pressure gate for SMS (into shouldUseSchedule — this
// LLVM has no PipelinerLoopInfo::canAcceptII virtual). Mirrors AIE's
// TrackRegPressure + canAllocate (AIEBasePipelinerLoopInfo.cpp:31-34, 460-518
// 865-869). Default ON matches AIE aie-pipeliner-track-regpressure.
static cl::opt<bool> HaydnSMSTrackRegPressure(
    "haydn-pipeliner-track-regpressure", cl::Hidden, cl::init(true),
    cl::desc("PPS-3: refuse SMS schedules likely to force register spills "
             "(AIE canAllocate peer). Default ON matching AIE."));

// PPS-3 spill-pressure positive / bisect: organic canAllocateSMS excess is
// schedule-shape fragile for a stable lit. Force the same shouldUseSchedule
// reject path that TrackRegPressure + canAllocateSMS takes. Default OFF —
// never product policy (product gate stays -haydn-pipeliner-track-regpressure).
static cl::opt<bool> ForceSMSPressureReject(
    "haydn-sms-force-pressure-reject", cl::Hidden, cl::init(false),
    cl::desc("PPS-3 test/bisect: reject every SMS schedule in "
             "shouldUseSchedule as if canAllocateSMS failed (spill-pressure "
             "fail-closed pin). Default OFF — never product policy."));


namespace {

// Get an instruction sequence from an SMS schedule estimated to have similar
// register pressure to premisched output. Reverse stage extraction: stages
// laid down in reverse order without interleaving.
// Peer: AIEBasePipelinerLoopInfo.cpp:getInstrSequence (408-429).
std::vector<MachineInstr *> getSMSInstrSequence(SMSchedule &Sched) {
  std::vector<MachineInstr *> Seq;

  for (int Stage = static_cast<int>(Sched.getMaxStageCount()); Stage >= 0;
       --Stage) {
    int FirstSeqCycle =
        Sched.getFirstCycle() + Stage * Sched.getInitiationInterval();
    int LastSeqCycle = FirstSeqCycle + Sched.getInitiationInterval() - 1;
    for (int SeqCycle = FirstSeqCycle; SeqCycle <= LastSeqCycle; ++SeqCycle) {
      for (SUnit *SU : Sched.getInstructions(SeqCycle))
        Seq.push_back(SU->getInstr());
    }
  }

  return Seq;
}

// Replay instructions in \p Seq and collect live-in registers.
// Peer: AIEBasePipelinerLoopInfo.cpp:collectLiveInRegs (431-456).
std::vector<VRegMaskOrUnit>
collectSMSLiveInRegs(const std::vector<MachineInstr *> &Seq,
                     const MachineFunction &MF) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  LiveRegSet LiveRegs;
  LiveRegs.init(MF.getRegInfo());

  for (const MachineInstr *MI : reverse(Seq)) {
    // Ignore PHI nodes: they make two values appear live-in without actually
    // increasing pressure when correctly placed/allocated.
    if (MI->isPHI())
      continue;

    RegisterOperands RegOpers;
    RegOpers.collect(*MI, *TRI, MF.getRegInfo(), true, true);
    for (const VRegMaskOrUnit &Def : RegOpers.Defs)
      LiveRegs.erase(Def);
    for (const VRegMaskOrUnit &Use : RegOpers.Uses)
      LiveRegs.insert(Use);
  }

  SmallVector<VRegMaskOrUnit> LiveInRegs;
  LiveRegs.appendTo(LiveInRegs);
  return {LiveInRegs.begin(), LiveInRegs.end()};
}

// Estimate whether RA can allocate the schedule without spilling by checking
// incoming register pressure against pressure-set limits.
// Peer: AIEBasePipelinerLoopInfo.cpp:canAllocate (458-518).
bool canAllocateSMS(SMSchedule &Sched) {
  std::vector<MachineInstr *> Seq = getSMSInstrSequence(Sched);
  if (Seq.empty())
    return true;

  for (const MachineInstr *MI : Seq)
    LLVM_DEBUG(dbgs() << "PPS-3 predicted order: " << *MI);

  MachineBasicBlock &MBB = *Seq.front()->getParent();
  MachineFunction &MF = *MBB.getParent();

  RegionPressure RegPressure;
  RegPressureTracker RPTracker(RegPressure);
  RegisterClassInfo RegClassInfo;
  RegClassInfo.runOnMachineFunction(MF);
  RPTracker.init(&MF, &RegClassInfo, nullptr, &MBB,
                 MachineBasicBlock::iterator(Seq.back()), false, false);
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  auto CheckPressureExcess = [&](const RegisterPressure &Pressure) {
    bool PressureExcess = false;
    for (unsigned I = 0, E = Pressure.MaxSetPressure.size(); I < E; ++I) {
      unsigned Limit = RegClassInfo.getRegPressureSetLimit(I);
      if (Pressure.MaxSetPressure[I] > Limit) {
        LLVM_DEBUG(dbgs() << TRI->getRegPressureSetName(I) << " Limit " << Limit
                          << " Actual " << Pressure.MaxSetPressure[I] << "\n");
        PressureExcess = true;
      }
    }
    return PressureExcess;
  };

  for (const VRegMaskOrUnit &LiveInReg : collectSMSLiveInRegs(Seq, MF)) {
    Register PrintReg =
        LiveInReg.VRegOrUnit.isVirtualReg()
            ? LiveInReg.VRegOrUnit.asVirtualReg()
            : Register(static_cast<unsigned>(LiveInReg.VRegOrUnit.asMCRegUnit()));
    LLVM_DEBUG(dbgs() << "PPS-3 add livein pressure: "
                      << printReg(PrintReg, TRI, 0, &MF.getRegInfo()) << ":"
                      << printRegClassOrBank(PrintReg, MF.getRegInfo(), TRI)
                      << "\n");

    // Ignore partially live regs — RPTracker overcounts pressure units.
    if (LiveInReg.VRegOrUnit.isVirtualReg() &&
        LiveInReg.LaneMask !=
            MF.getRegInfo().getMaxLaneMaskForVReg(
                LiveInReg.VRegOrUnit.asVirtualReg())) {
      LLVM_DEBUG(dbgs() << "PPS-3 skipped partially live reg\n");
      continue;
    }
    RPTracker.increaseRegPressure(LiveInReg.VRegOrUnit, LaneBitmask::getNone(),
                                  LiveInReg.LaneMask);
  }

  // true when no pressure set is overbooked (AIE ExcessIncomingPressure).
  bool CanAllocate = !CheckPressureExcess(RPTracker.getPressure());
  return CanAllocate;
}

} // namespace

bool HaydnPipelinerLoopInfo::shouldIgnoreForPipelining(
    const MachineInstr *MI) const {
  // Ignore the loop-control chain — latch branch (EndLoop), compare (CmpMI),
  // and optional XORI invert (InvertMI). These must remain in stage 0 and
  // cannot be pipelined across stages. Staging InvertMI alone while CmpMI /
  // EndLoop stay stage-0 delays the exit predicate by one iteration: the
  // kernel branch reads a previous-iteration SEQ via the staged XORI PHI
  // (20000605-1: countdown overshoots, post-loop SEQ live-out is 0 → abort).
  // MachinePipeliner::computeUnpipelineableNodes also pulls same-iteration
  // predecessors of ignored nodes into stage 0, so the IV bump that feeds
  // SEQ is kept coherent with the exit test.
  // In ZOL mode, also ignore LoopStart (preheader setup) and
  // PseudoLoopEnd (the meta latch terminator).
  if (MI == EndLoop || MI == CmpMI || MI == InvertMI)
    return true;
  if (IsZOL && (MI == LoopStart ||
                MI->getOpcode() == Haydn::PseudoLoopEnd ||
                MI->getOpcode() == Haydn::LoopStart))
    return true;
  return false;
}

bool HaydnPipelinerLoopInfo::shouldUseSchedule(SwingSchedulerDAG &SSD,
                                               SMSchedule &SMS) {
  const unsigned PrologueCount = SMS.getMaxStageCount();
  const unsigned StageCount = PrologueCount + 1;
  const unsigned II = SMS.getInitiationInterval();

  // Final-parcel geometry cost (not II-proxy). SetupIssueDistance is a
  // preheader→BEGIN architectural floor; MinBodyBundles is kernel body
  // parcels after materialize. Never treat II >= SetupIssueDistance as the
  // setup gate — Fixup pads preheader separately. Kernel parcel count for a
  // candidate is the scheduled II (one issue cycle per modulo phase); body
  // pad is max(0, MinBodyBundles - KernelParcels) when ZOL form is live.
  const unsigned KernelParcels = std::max(II, 1u);
  const unsigned BodyPadParcels =
      IsZOL && KernelParcels < haydn::hwloop::MinBodyBundles
          ? haydn::hwloop::MinBodyBundles - KernelParcels
          : 0u;
  const unsigned FinalBodyParcels = KernelParcels + BodyPadParcels;
  auto logZOLGeometryFloors = [&]() {
    dbgs() << "ZOL: geometry floors MinBodyBundles="
           << haydn::hwloop::MinBodyBundles
           << " SetupIssueDistance=" << haydn::hwloop::SetupIssueDistance
           << " InterveningCycles=" << haydn::hwloop::InterveningCycles
           << " (cost on final parcel schedule; not II>=Setup floor)\n";
    dbgs() << "ZOL: final-parcel cost kernel=" << KernelParcels
           << " body_pad=" << BodyPadParcels
           << " final_body=" << FinalBodyParcels
           << " setup_preheader_floor=" << haydn::hwloop::SetupIssueDistance
           << " (setup is preheader→BEGIN; independent of II)\n";
  };

  // DEBUG_WITH_TYPE("pipeliner"): file DEBUG_TYPE is haydn-instr-info, but
  // sms-* lits pin reject lines with -debug-only=pipeliner (match SMS-HOOK /
  // SMS-RESMII). Product gates themselves are flag-driven, not log-driven.

  // PR8 / AIE preferPostPipeliner seed: when the flag is set, defer ZOL to the
  // post-pipeliner path by rejecting every SMS schedule for ZOL form. Default
  // off so classic pre-RA SMS behavior is unchanged.
  if (IsZOL && EnableZOLPreferPostPipeliner) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "ZOL: preferring post-pipeliner over SMS "
                "(-haydn-zol-prefer-post-pipeliner)\n";
      logZOLGeometryFloors();
    });
    return false;
  }

  // For ZOL loops, reject single-stage schedules (StageCount <= 1).
  // A single-stage schedule has no pipeline overlap -- it just adds
  // prologue/epilogue overhead (register copies, trip-count adjustments)
  // without any benefit. This caused +358 bundles on the corpus when ZOL
  // pipelining was first enabled with unconditional acceptance.
  // Mirrors AIE's ZeroOverheadLoop::shouldUseSchedule
  // (AIEBasePipelinerLoopInfo.cpp:826-835) which delegates to the base class
  // that rejects StageCount <= 1.
  // Combined with StageCount>1 containment below, pre-RA ZOL SMS never
  // accepts: product multi-stage is post-RA only (Option A / Option C law).
  if (IsZOL && StageCount <= 1) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "ZOL: rejecting single-stage schedule (no overlap; pre-RA "
                "ZOL multi-stage is post-RA only under StageCount>1 "
                "containment)\n";
      logZOLGeometryFloors();
    });
    return false;
  }

  // AIE ZeroOverheadLoop::canAcceptII: MaxStageCount >= MinTripCount.
  // Prologue stages peel iterations; without a static min trip high enough
  // to cover them, ZOL cannot emit a dynamic guard and would mis-iterate
  // (memcpy-class variable trip, small sizes). MinTripCount==0 means
  // unknown/unbounded — refuse every multi-stage schedule (analysis may
  // still succeed so the ZOL form is recognized; SMS is not applied).
  // Peer AIEBasePipelinerLoopInfo.cpp:807-814.
  if (IsZOL &&
      (MinTripCount == 0 ||
       static_cast<int64_t>(PrologueCount) >= MinTripCount)) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "ZOL: reject SMS (MaxStageCount=" << PrologueCount
             << " MinTripCount=" << MinTripCount << ")\n";
      logZOLGeometryFloors();
    });
    return false;
  }

  // PPS-3: AIE canAcceptII stage-count gate (into shouldUseSchedule — this
  // LLVM has no PipelinerLoopInfo::canAcceptII virtual). Reject schedules
  // with too many stages; high stage count forces many prologue/epilogue
  // copies and often loses to Stage-0 PostPipeliner or no-SWP.
  // Mirrors AIEBasePipelinerLoopInfo::canAcceptII MaxStageCount check
  // (AIEBasePipelinerLoopInfo.cpp:839-847).
  if (StageCount > HaydnSMSMaxStageCount) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "SMS-SHOULDUSE: reject stages=" << StageCount
             << " max=" << HaydnSMSMaxStageCount << " II=" << II << "\n";
      dbgs() << "PPS-3: reject SMS (stages=" << StageCount
             << " > max=" << HaydnSMSMaxStageCount << " II=" << II << ")\n";
    });
    return false;
  }

  // Force pressure reject is test/bisect only — honor it before multi-stage
  // containment so PRESSURE pins stay stable under -haydn-sms-force-pressure-reject.
  if (ForceSMSPressureReject) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "SMS-SHOULDUSE: reject pressure stages=" << StageCount
             << " II=" << II
             << " (forced by -haydn-sms-force-pressure-reject)\n";
      dbgs() << "PPS-3: reject SMS (too much block pressure, stages="
             << StageCount << " II=" << II << ")\n";
    });
    return false;
  }

  // Option A containment: reject every pre-RA StageCount > 1 (ZOL and soft
  // counted alike). Closes the inverted ZOL multi-stage gate: ZOL single-stage
  // is rejected above, multi-stage is rejected here, so pre-RA ZOL SMS never
  // expands bare multi-stage. Product multi-stage is post-RA only; freeze must
  // not cross RA.
  if (StageCount > 1) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "SMS-SHOULDUSE: reject multi-stage stages=" << StageCount
             << " II=" << II
             << " (pre-RA StageCount>1 containment; post-RA multi-stage only)\n";
      if (IsZOL)
        logZOLGeometryFloors();
    });
    return false;
  }

  // PPS-3: AIE canAcceptII TrackRegPressure/canAllocate gate. Reject schedules
  // whose estimated kernel live-ins exceed register pressure-set limits (spill
  // risk). Peer AIEBasePipelinerLoopInfo.cpp:865-869.
  const bool PressureExcess =
      HaydnSMSTrackRegPressure && !canAllocateSMS(SMS);
  if (PressureExcess) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "SMS-SHOULDUSE: reject pressure stages=" << StageCount
             << " II=" << II << "\n";
      dbgs() << "PPS-3: reject SMS (too much block pressure, stages="
             << StageCount << " II=" << II << ")\n";
    });
    return false;
  }

  // Release-visible polarity pin: pure product StageCount1 helper agrees that
  // this soft StageCount==1 schedule is the only remaining accept path. Flag
  // specials (prefer-post-pipeliner / force-pressure) already returned above.
  if (HaydnPreRASchedStrategy::smsProductShouldUseScheduleFailsClosed(
          IsZOL, PrologueCount, MinTripCount, /*PressureExcess=*/false,
          HaydnSMSMaxStageCount, HaydnSMSTrackRegPressure))
    report_fatal_error(
        "Haydn SMS shouldUseSchedule pure product containment polarity desync",
        /*GenCrashDiag=*/false);

  // SMS consume uses the same availability-aware record as pre-RA / ordinary
  // post-RA / hazard recognizer / late latency verify. Competitive II/density
  // stays closed until the complete per-op table is admitted.
  static bool SMSResourcePinned = false;
  if (!SMSResourcePinned) {
    SMSResourcePinned = true;
    if (!haydnAvailabilityAwareConsumePinsHold())
      report_fatal_error(
          "Haydn SMS shouldUseSchedule resource admission pins failed",
          /*GenCrashDiag=*/false);
  }
  ++NumSMSSharedResourceRecordConsumes;

  // Accept remaining StageCount == 1 schedules as bare logical MIs only.
  // Soft counted residual (proven trip count; AIE DownCountLoop peer) — not
  // approximate (limit-init)/step invent. Metrics-only; no pre-RA cycle groups.
  // Geometry cost is recorded for final parcels (kernel II + body pad); setup
  // floor is never an II proxy.
  DEBUG_WITH_TYPE("pipeliner", {
    dbgs() << "SMS-SHOULDUSE: accept stages=1 II=" << II
           << " (metrics-only; bare logical MIs; proven counted residual; "
              "no pre-RA cycle groups; StageCount1 product containment)\n";
    dbgs() << "SMS-SHOULDUSE: final-parcel cost kernel=" << KernelParcels
           << " body_pad=" << BodyPadParcels
           << " final_body=" << FinalBodyParcels
           << " (MinBodyBundles=" << haydn::hwloop::MinBodyBundles
           << " SetupIssueDistance=" << haydn::hwloop::SetupIssueDistance
           << " not used as II floor)\n";
  });
  return true;
}

std::optional<bool> HaydnPipelinerLoopInfo::createTripCountGreaterCondition(
    int TC, MachineBasicBlock &MBB,
    SmallVectorImpl<MachineOperand> &Cond) {
  // ZOL mode — the hardware loop counter handles the iteration count.
  // We cannot emit a dynamic guard (the ZOL terminator cannot be reversed).
  // AIE only returns true when MinTripCount > TC (static no-guard); otherwise
  // llvm_unreachable. We mirror that contract: only claim "no guard needed"
  // when MinTripCount statically exceeds the requested TC. Schedules that
  // would need a dynamic guard must already have been rejected in
  // shouldUseSchedule / analyzeLoopForPipelining.
  // Peer AIEBasePipelinerLoopInfo.cpp:750-761.
  if (IsZOL) {
    if (MinTripCount > TC)
      return true;
    LLVM_DEBUG(dbgs() << "ZOL: createTripCountGreaterCondition TC=" << TC
                      << " MinTripCount=" << MinTripCount
                      << " — cannot reverse ZOL; refuse static true\n");
    // Returning false = static "trip not greater" → expander skips/disposes.
    // Prefer this over asserting: analyze may have accepted via override.
    return false;
  }

  // Always emit a runtime "branch if TripCountReg > TC" and return nullopt
  // NEVER a compile-time static bool. This mirrors the ARM reference
  // implementation (ARMBaseInstrInfo.cpp::ARMPipelinerLoopInfo), which has no
  // static-trip-count path whatsoever. Returning a static bool here was the
  // Blocker-1 silent-wrong-code root cause: a hand-rolled `(limit-init)/step`
  // value drove `PeelingModuloScheduleExpander::fixupBranches`
  // (ModuloSchedule.cpp:1980-1999) into the static-false (`KernelDisposed`)
  // branch, collapsing countable loops (e.g. dot_product_16, trip 16) to ~1
  // iteration with `-verify-machineinstrs` still green. See.
  //
  // `analyzeLoopForPipelining` rejects any loop without a usable runtime
  // trip-count register, so TripCountReg must be valid here.
  assert(TripCountReg.isValid() && "pipelined loop must have a runtime TC reg");

  // TripCountReg > TC <=> NOT (TripCountReg < TC + 1)
  // <=> BEQZ (SLT32 TripCountReg, TC+1)
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetRegisterClass *RC = &Haydn::GPR32RegClass;
  DebugLoc BranchDL = MBB.findBranchDebugLoc();

  // Materialize (TC + 1) into a register.
  Register CmpReg = MRI.createVirtualRegister(RC);
  if (isInt<16>(TC + 1)) {
    BuildMI(&MBB, BranchDL, HII->get(Haydn::LOADI32), CmpReg).addImm(TC + 1);
  } else {
    BuildMI(&MBB, BranchDL, HII->get(Haydn::LUI), CmpReg)
        .addReg(Haydn::R0)
        .addImm(((static_cast<uint32_t>(TC + 1) + 0x8000) >> 16) & 0xFFFF);
    BuildMI(&MBB, BranchDL, HII->get(Haydn::ADDI32_W), CmpReg)
        .addReg(CmpReg)
        .addImm((TC + 1) & 0xFFFF);
  }

  // CmpResult = (TripCountReg < TC + 1)
  Register CmpResult = MRI.createVirtualRegister(RC);
  BuildMI(&MBB, BranchDL, HII->get(Haydn::SLT32), CmpResult)
      .addReg(TripCountReg)
      .addReg(CmpReg);

  // fix: upstream contract (see Hexagon's J2_jumpf reference and
  // PeelingModuloScheduleExpander::fixupBranches / placeRematerializersCall
  // call sites in ModuloSchedule.cpp:886,1975) requires the Cond to be TRUE
  // (branch-taken) when the trip count is NOT greater than TC, i.e. when the
  // prologue should be SKIPPED. CmpResult = (TripCountReg < TC+1) is true when
  // trip <= TC. To branch on that "skip" condition we must fire when CmpResult
  // != 0, hence BNEZ_W. The previous BEQZ fired when trip > TC (CmpResult ==
  // 0), reversing the guard and dead-stripping every pipelined loop with trip
  // > stage count (counting-sort, vec-max). Phase 1b: emit the
  // WIDE 48-bit form so insertBranch / AsmPrinter produce a WIDE parcel.
  Cond.push_back(MachineOperand::CreateImm(Haydn::BNEZ_W));
  Cond.push_back(MachineOperand::CreateReg(CmpResult, false));
  return {};
}

void HaydnPipelinerLoopInfo::adjustTripCount(int TripCountAdjust) {
  // ZOL mode — edit LoopStart's simm6:$adj operand directly.
  // Mirrors AIE's ZeroOverheadLoop::adjustTripCount
  // (AIEBasePipelinerLoopInfo.cpp:763-768).
  if (IsZOL) {
    assert(LoopStart && "ZOL pipelined loop must have a LoopStart");
    // LoopStart has operands: $src (reg), $adj (simm6). The adj field is
    // the pipeliner's trip-count adjustment — add the delta to it.
    int64_t CurAdj = LoopStart->getOperand(1).getImm();
    LoopStart->getOperand(1).setImm(CurAdj + TripCountAdjust);
    return;
  }

  // Runtime trip count: apply the expander's signed delta to TripCountReg.
  // ModuloScheduleExpander calls adjustTripCount(-(MaxIter+1)) /
  // PeelingModuloScheduleExpander calls adjustTripCount(-(NumStages-1)), so
  // TripCountAdjust is already the (negative) amount to add. Peer Hexagon
  // HexagonInstrInfo.cpp adjustTripCount: `A2_addi New, Old, TripCountAdjust`
  // with no extra negation. A prior `Adj = -TripCountAdjust` double-negated
  // the delta (stages=2 → asked for -1, applied +1), so countdown/limit
  // bounds grew by the prolog peel and SMS kernels over-iterated (CoreMark
  // matrix_sum OOB / ORACLE_MISMATCH). There is no static path — see
  // createTripCountGreaterCondition.
  assert(TripCountReg.isValid() && "pipelined loop must have a runtime TC reg");

  MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetRegisterClass *RC = &Haydn::GPR32RegClass;
  Register NewTC = MRI.createVirtualRegister(RC);

  // Use the cached LoopBB (the original loop body).
  MachineBasicBlock *LoopBB = this->LoopBB;
  if (isInt<16>(TripCountAdjust)) {
    BuildMI(*LoopBB, LoopBB->getFirstNonPHI(), DL, HII->get(Haydn::ADDI32_W),
            NewTC)
        .addReg(TripCountReg)
        .addImm(TripCountAdjust);
  } else {
    Register AdjReg = MRI.createVirtualRegister(RC);
    BuildMI(*LoopBB, LoopBB->getFirstNonPHI(), DL, HII->get(Haydn::LOADI32),
            AdjReg)
        .addImm(TripCountAdjust);
    BuildMI(*LoopBB, LoopBB->getFirstNonPHI(), DL, HII->get(Haydn::ADD32),
            NewTC)
        .addReg(TripCountReg)
        .addReg(AdjReg);
  }

  // Rewrite all subsequent uses of the trip-count register.
  MRI.replaceRegWith(TripCountReg, NewTC);
}

void HaydnPipelinerLoopInfo::setPreheader(MachineBasicBlock *NewPreheader) {
  // No-op. SMS runs PRE-RA on ZOL form : the IR-level
  // HardwareLoops pass has already emitted LoopStart (preheader) +
  // PseudoLoopEnd (latch) before the pipeliner runs. The expander clones the
  // ZOL terminator into its new preheader/prologue/epilogue blocks directly;
  // adjustTripCount edits the LoopStart $adj operand already present in the
  // expander's new preheader, so no additional target splice is needed here.
}

