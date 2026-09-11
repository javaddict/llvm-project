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
// W68.1: the generic MachinePipeliner is the product multi-stage owner for
// BOTH soft and ZOL loops (PPS-3 bound; the W59 defer seam is retired —
// one engine, no routing). No format/member witness crosses RA (D493).
//
//===----------------------------------------------------------------------===//

#include "HaydnPipelinerLoopInfo.h"
#include "Haydn.h"
#include "HaydnFormatERecords.h"
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

// F41: pre-RA StageCount containment bound as a test/bisect knob, shared by
// BOTH loop forms (W68.1: form-uniform). The PRODUCT value is the PPS-3
// max-stage bound (generic MachinePipeliner owns soft and ZOL multi-stage
// alike); smaller values restore the historic StageCount==1 Option A
// containment for bisect.
static cl::opt<unsigned> HaydnSMSContainmentMax(
    "haydn-sms-containment-max", cl::Hidden,
    cl::init(HaydnPreRASchedStrategy::productSMSSoftContainmentMaxStageCount),
    cl::desc("F41 test/bisect: max StageCount the pre-RA containment accepts "
             "for BOTH soft and ZOL loops (product = the PPS-3 max-stage "
             "bound; 1 restores the historic Option A containment)."));


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
  // This is a pre-RA geometry cost, not a parcels-per-iter proof. The
  // post-RA host independently recounts planned parcels against searched II.
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

  // Stage-0 PostPipeliner is deleted. AIE ZeroOverheadLoop::preferPostPipeliner
  // (AIEBasePipelinerLoopInfo.cpp:770-834) routes some ZOL to PostPipeliner;
  // W68.1: the generic MachinePipeliner (this path) is the only multi-stage
  // owner; the bespoke post-RA host is deleted.

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
  //
  // CB-166 (2026-08-27): when ZOLTripReg carries the RUNTIME count
  // register feeding LoopStart, the expander guard contract CAN emit a
  // dynamic per-prologue condition on that register
  // (createTripCountGreaterCondition below; Hexagon J2_loop0r law,
  // HexagonInstrInfo.cpp:755-771), so small/unknown static trips no
  // longer refuse the schedule — the guard skips prologue/kernel for
  // trips that cannot cover the peel, preserving the iteration-count
  // invariant without a static bound. The static MinTripCount law above
  // still governs constant-trip loops (guard-free acceptance).
  if (IsZOL && !ZOLTripReg.isValid() &&
      (MinTripCount == 0 ||
       static_cast<int64_t>(PrologueCount) >= MinTripCount)) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "ZOL: reject SMS (MaxStageCount=" << PrologueCount
             << " MinTripCount=" << MinTripCount
             << " — no runtime trip reg for a dynamic guard)\n";
      logZOLGeometryFloors();
    });
    return false;
  }
  if (IsZOL && ZOLTripReg.isValid()) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "ZOL: runtime trip reg live — dynamic prologue guards "
                "cover the peel (MaxStageCount="
             << PrologueCount << " MinTripCount=" << MinTripCount << ")\n";
    });
  }

  // W59 routing seam RETIRED (W68.1): its reason to exist was "pre-RA never
  // accepts multi-stage, so ZOL candidates must reach the post-RA host
  // unpolluted." With ZOL multi-stage qualified on the generic path below,
  // pre-RA IS the multi-stage owner for both forms and a decline-and-defer
  // would split one loop between two engines. The bespoke post-RA host is
  // DELETED with this change (W68.1 final step).

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

  // W68.1 containment: the generic MachinePipeliner owns multi-stage for BOTH
  // soft and ZOL loops (classic ModuloScheduleExpander; ZOL adds the
  // LoopStart $adj edit + static guard via MinTripCount > PrologueCount),
  // bounded by the PPS-3 max-stage gate. The F41 knob bisects the bound DOWN
  // for both forms (1 restores the historic Option A single-stage
  // containment). ZOL's own AIE-peer gates above (single-stage reject,
  // MinTripCount guard) remain the ZOL law; no freeze crosses RA (D493).
  const unsigned ContainmentMax = HaydnSMSContainmentMax;
  if (StageCount > ContainmentMax) {
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

  // Release-visible polarity pin: pure product containment helper agrees
  // this schedule is the only remaining accept path. Flag specials
  // (force-pressure / containment-max override) already returned above, so
  // at product defaults this accept is exactly the helper's complement.
  // W68.1: the bound is form-uniform (soft == ZOL == PPS-3), so the pin
  // passes the knob's ContainmentMax for both forms.
  if (HaydnSMSContainmentMax ==
          HaydnPreRASchedStrategy::productSMSSoftContainmentMaxStageCount &&
      HaydnPreRASchedStrategy::smsProductShouldUseScheduleFailsClosed(
          IsZOL, PrologueCount, MinTripCount, /*PressureExcess=*/false,
          HaydnSMSMaxStageCount, HaydnSMSTrackRegPressure,
          HaydnSMSContainmentMax, /*HasRuntimeTripReg=*/ZOLTripReg.isValid()))
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

  // Accept remaining schedules as bare logical MIs only — soft counted
  // residual (proven trip count; AIE DownCountLoop peer) and qualified ZOL
  // multi-stage (static guard; LoopStart $adj edit) alike. Not approximate
  // (limit-init)/step invent. Metrics-only; no pre-RA cycle groups.
  // Geometry cost is recorded for final parcels (kernel II + body pad); setup
  // floor is never an II proxy. The accept-line tail names the LIVE
  // containment bound (product PPS-3 vs F41 bisect override); log truth
  // only, the bound itself is the ContainmentMax selection above.
  // The contract's only pre-RA format API. It prices the loop body
  // (the SMS DAG's real SUnits) against the golden-admitted format table:
  // coverage in at least one available row, then the E3-widest format-union
  // cycle floor. Diagnostics-only and computed solely inside this
  // DEBUG_WITH_TYPE block — release builds never run the pricing walk, so
  // it is NOT a live proposal-ranking input in product builds; the
  // accept/reject inputs are the ResMII/RecMII floors and the containment
  // bound named above. A body with an uncovered logical would have failed
  // the build-time schema check; nullopt here is fail-closed log truth,
  // never an accept/reject input.
  DEBUG_WITH_TYPE("pipeliner", {
    SmallVector<MachineInstr *, 16> Body;
    for (const SUnit &SU : SSD.SUnits) {
      const MachineInstr *BMI = SU.getInstr();
      // The staging ignore set (loop-control chain + ZOL setup) is not part
      // of the per-iteration body the advisory prices.
      if (!BMI || shouldIgnoreForPipelining(BMI))
        continue;
      Body.push_back(const_cast<MachineInstr *>(BMI));
    }
    std::optional<CycleEstimate> Advisory =
        estimateCyclesAcrossAvailableFormats(Body);
    dbgs() << "SMS-SHOULDUSE: advisory cycles="
           << (Advisory ? std::to_string(Advisory->Cycles) : "uncovered")
           << " (estimateCyclesAcrossAvailableFormats; format-union E3 floor; "
              "advisory, not emitted-II truth)\n";
    dbgs() << "SMS-SHOULDUSE: accept stages=" << StageCount << " II=" << II
           << " (metrics-only; bare logical MIs; proven counted residual; "
              "no pre-RA cycle groups; "
           << (ContainmentMax ==
                       HaydnPreRASchedStrategy::
                           productSMSSoftContainmentMaxStageCount
                   ? "product containment (PPS-3 bound)"
                   : "containment-max override (bisect down)")
           << ")\n";
    dbgs() << "SMS-SHOULDUSE: final-parcel cost kernel=" << KernelParcels
           << " body_pad=" << BodyPadParcels
           << " final_body=" << FinalBodyParcels
           << " (MinBodyBundles=" << haydn::hwloop::MinBodyBundles
           << " SetupIssueDistance=" << haydn::hwloop::SetupIssueDistance
           << " not used as II floor)\n";
  });
  return true;
}

std::optional<CycleEstimate>
HaydnPipelinerLoopInfo::estimateCyclesAcrossAvailableFormats(
    ArrayRef<MachineInstr *> Body) const {
  using namespace haydn::format_e;

  // Packable Format E entries only: loop control (the caller passes body
  // instructions; a terminator in the set still must not be counted as a
  // packable entry), PHIs, and metadata (debug/CFI/kill/position/implicit-def
  // markers) occupy no Format E entry and contribute no cycles.
  auto isPackableBodyMI = [](const MachineInstr &MI) {
    return !MI.isDebugInstr() && !MI.isPosition() && !MI.isKill() &&
           !MI.isImplicitDef() && !MI.isCFIInstruction() && !MI.isPHI() &&
           !MI.isTerminator();
  };

  // Coverage: the golden-admitted table must admit the logical name in at
  // least one non-NOP row. findAltSpan is the one generated coverage oracle
  // (non-NOP logicals only); a miss means an RA-legal tuple with no alternate
  // — a build-time schema gap, surfaced as nullopt (fail closed), never a
  // silently invented cycle number.
  const TargetInstrInfo &TII = *HII;
  unsigned Packable = 0;
  for (MachineInstr *MI : Body) {
    if (!MI || !isPackableBodyMI(*MI))
      continue;
    // Coverage keys on the golden catalog token, not the raw TableGen def
    // name: post-inc / load-store families use catalog occupancy names
    // (LD32 -> S_LW_WITH_IMM etc.), the same normalization
    // peelLogicalOpcodeName applies everywhere else.
    const std::string CatalogName =
        peelLogicalOpcodeName(TII.getName(MI->getOpcode()));
    if (!findAltSpan(CatalogName.c_str()))
      return std::nullopt;
    ++Packable;
  }

  // Format-union issue-cycle lower bound: pack the packable entries at the
  // admitted widest (E3) row entry capacity — one issue cycle per pack. The
  // widest row minimizes the bound, so this is the advisory floor over all
  // available formats; it reveals no particular format/row/alternate and is
  // not emitted-II truth. Zero packable instructions cost one architectural
  // cycle (the minimum nonempty packet).
  const FamilyRecords Fam = getDefaultFamilyRecords();
  const unsigned EntryCount = Fam.E3EntryCapacity ? Fam.E3EntryCapacity : 1u;
  const unsigned Cycles =
      std::max(1u, (Packable + EntryCount - 1u) / EntryCount);
  return CycleEstimate{Cycles};
}

std::optional<bool> HaydnPipelinerLoopInfo::createTripCountGreaterCondition(
    int TC, MachineBasicBlock &MBB,
    SmallVectorImpl<MachineOperand> &Cond) {
  // Dynamic-guard emission shared by the soft counted path and the ZOL
  // runtime-trip path (CB-166). Emits a runtime "branch if CountReg <= TC"
  // (skip-prologue) condition into Cond and returns nullopt — NEVER a
  // compile-time static bool. Returning a static bool here was the Blocker-1
  // silent-wrong-code root cause: a hand-rolled `(limit-init)/step` value
  // drove `PeelingModuloScheduleExpander::fixupBranches`
  // (ModuloSchedule.cpp:1980-1999) into the static-false (`KernelDisposed`)
  // branch, collapsing countable loops (e.g. dot_product_16, trip 16) to ~1
  // iteration with `-verify-machineinstrs` still green.
  auto EmitDynamicGuard = [&](Register CountReg) {
    // CountReg > TC <=> NOT (CountReg < TC + 1)
    // <=> BNEZ (SLT32 CountReg, TC+1)
    MachineRegisterInfo &MRI = MF->getRegInfo();
    const TargetRegisterClass *RC = &Haydn::GPR32RegClass;
    DebugLoc BranchDL = MBB.findBranchDebugLoc();

    // Materialize (TC + 1) into a register.
    Register CmpReg = MRI.createVirtualRegister(RC);
    if (isInt<16>(TC + 1)) {
      BuildMI(&MBB, BranchDL, HII->get(Haydn::LOADI32), CmpReg).addImm(TC + 1);
    } else {
      BuildMI(&MBB, BranchDL, HII->get(Haydn::LUI), CmpReg)
          .addImm(((static_cast<uint32_t>(TC + 1) + 0x8000) >> 16) & 0xFFFF);
      BuildMI(&MBB, BranchDL, HII->get(Haydn::ADDI32_W), CmpReg)
          .addReg(CmpReg)
          .addImm((TC + 1) & 0xFFFF);
    }

    // CmpResult = (CountReg < TC + 1)
    Register CmpResult = MRI.createVirtualRegister(RC);
    BuildMI(&MBB, BranchDL, HII->get(Haydn::SLT32), CmpResult)
        .addReg(CountReg)
        .addReg(CmpReg);

    // fix: upstream contract (see Hexagon's J2_jumpf reference and
    // PeelingModuloScheduleExpander::fixupBranches / placeRematerializersCall
    // call sites in ModuloSchedule.cpp:886,1975) requires the Cond to be TRUE
    // (branch-taken) when the trip count is NOT greater than TC, i.e. when the
    // prologue should be SKIPPED. CmpResult = (CountReg < TC+1) is true when
    // trip <= TC. To branch on that "skip" condition we must fire when
    // CmpResult != 0, hence BNEZ_W. The previous BEQZ fired when trip > TC
    // (CmpResult == 0), reversing the guard and dead-stripping every pipelined
    // loop with trip > stage count (counting-sort, vec-max). Phase 1b: emit
    // the WIDE 48-bit form so insertBranch / AsmPrinter produce a WIDE parcel.
    Cond.push_back(MachineOperand::CreateImm(Haydn::BNEZ_W));
    Cond.push_back(MachineOperand::CreateReg(CmpResult, false));
    return std::optional<bool>{};
  };

  // ZOL mode — the hardware loop counter handles the iteration count.
  // CB-166 (2026-08-27): with a RUNTIME count register feeding LoopStart,
  // emit the same dynamic per-prologue guard as the soft path on that
  // register (Hexagon J2_loop0r law, HexagonInstrInfo.cpp:755-771). The
  // register holds exactly the value SET_HWLOOP_F2_W consumes (Role A
  // reads LoopStart operand 0), so the guard tests the count the hardware
  // decrements — the iteration-count invariant. The expander inserts the
  // conditional branch in the PROLOGUE blocks (plain BBs; the ZOL
  // terminator itself is only ever cloned into the kernel), so nothing
  // reverses the ZOL exit.
  // Constant-trip ZOL keeps the AIE static law (peer
  // AIEBasePipelinerLoopInfo.cpp:750-761): only claim "no guard needed"
  // when MinTripCount statically exceeds the requested TC.
  if (IsZOL) {
    if (ZOLTripReg.isValid()) {
      LLVM_DEBUG(dbgs() << "ZOL: dynamic prologue guard on runtime trip reg "
                           "(TC="
                        << TC << ")\n");
      return EmitDynamicGuard(ZOLTripReg);
    }
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
  // static-trip-count path whatsoever.
  //
  // `analyzeLoopForPipelining` rejects any loop without a usable runtime
  // trip-count register, so TripCountReg must be valid here.
  assert(TripCountReg.isValid() && "pipelined loop must have a runtime TC reg");
  return EmitDynamicGuard(TripCountReg);
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

  // F41 (CR-H3): soft counted loops are adjusted STRUCTURALLY by the
  // expander, so this hook must NOT mutate MIR. Do not insert an adjusted
  // trip-count def anywhere, and never replaceRegWith(TripCountReg, NewTC):
  //
  //   * Block lifetime: the old code inserted the def at the head of the
  //     ORIGINAL loop MBB. The classic ModuloScheduleExpander erases that
  //     block (cleanup(): BB->clear(); BB->eraseFromParent(),
  //     ModuloSchedule.cpp) while every replaceRegWith-rewritten use survives
  //     in the kernel/prologs/epilogs → dangling vreg (verifier: "Reading
  //     virtual register without a def"). Latent until now only because
  //     StageCount==1 schedules never reach the expander (MachinePipeliner
  //     "No overlapped iterations" skip) and StageCount>1 was containment
  //     rejected.
  //   * Guard semantics: the expanders call createTripCountGreaterCondition
  //     for every prologue BEFORE adjustTripCount (classic addBranches /
  //     peeling fixupBranches). Those guards read TripCountReg and must test
  //     the ORIGINAL trip ("original trip > j+1" decides skip-prologue). A
  //     replaceRegWith here rewrites the already-inserted guards to read the
  //     adjusted count, flipping skip decisions for every trip in
  //     (adjusted, original] — silent wrong code on any future accept.
  //
  // Why no def is needed at all: shouldIgnoreForPipelining keeps the
  // loop-control chain (IV bump → CmpMI/InvertMI → EndLoop) out of the
  // pipelined stages, and computeUnpipelineableNodes forces that closure
  // into stage 0. The classic expander therefore clones the whole chain
  // into EVERY prolog (generateProlog) and the kernel control PHI init
  // chains from the last prolog's clone — the peel delta is realized by
  // the cloned chain itself (prolog j executes stages 0..j of iteration j
  // and its own copy of the countdown consumes the (MaxIter+1-j) peel).
  // Peer law, same shape: ARM ARMPipelinerLoopInfo::adjustTripCount is an
  // empty no-op (ARMBaseInstrInfo.cpp, soft t2Bcc/t2LoopEnd loops) and AIE's
  // soft DownCountLoop base adjustTripCount is log-only
  // (AIEBasePipelinerLoopInfo.cpp:113-117). Only hardware-loop SETUP forms
  // edit state here (Hexagon A2_addi on the LOOP0r operand; AIE/Haydn ZOL
  // LoopStart $adj above) because their hw counter is not cloned by the
  // expander.
  // The delta sign comment is retained for the post-RA owner: the expander
  // passes an already-signed delta (classic -(MaxIter+1) / peeling
  // -(NumStages-1)); the retired code's `Adj = -TripCountAdjust`
  // double-negation was the CoreMark matrix_sum OOB root cause.
  assert(TripCountReg.isValid() && "pipelined loop must have a runtime TC reg");
  // -debug-only=pipeliner (same convention as the SMS-SHOULDUSE pins).
  DEBUG_WITH_TYPE("pipeliner", {
    dbgs() << "SMS-TC: soft adjustTripCount delta=" << TripCountAdjust
           << " is a structural no-op (expander clones the stage-0 "
              "control chain; guards keep the original count)\n";
  });
}

void HaydnPipelinerLoopInfo::setPreheader(MachineBasicBlock *NewPreheader) {
  // No-op for both forms:
  //  * ZOL: the IR-level HardwareLoops pass already emitted LoopStart
  //    (preheader) + PseudoLoopEnd (latch) before the pipeliner runs; the
  //    expander clones the ZOL terminator into its new
  //    preheader/prologue/epilogue blocks directly, and adjustTripCount edits
  //    the LoopStart $adj operand wherever it lives (the operand, not the
  //    block, is the state).
  //  * soft counted (F41): there is no loop-setup instruction to splice —
  //    the trip count is an ordinary SSA value (TripCountReg) consumed by
  //    the expander-cloned stage-0 control chain; the classic expander
  //    never erases the original preheader, so the def stays where it is.
  //    (Peer contrast: Hexagon setPreheader splices its LOOP0 setup into
  //    the surviving preheader because that setup WOULD otherwise die with
  //    the original latch MBB. Haydn soft loops have no such instruction.)
}

