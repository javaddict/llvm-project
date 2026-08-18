//===-- HaydnPreRASchedStrategy.h - AIE-style pre-RA MI sched ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Peer: AIEPreRASchedStrategy (AIEMachineScheduler.h/cpp).
// Mechanism: MachineSchedStrategy::isAvailableNode (ported from AIE into stock
// MachineScheduler so Haydn can delay pressure-worsening SUnits exactly as AIE
// does — not inventing a dual path).
//
// Pre-RA reasons about a feasible FormatID *matching frontier* without
// freezing FormatID or setDesc (plan §5.1 / §7.1). Product table size 1 =
// Format E PacketFormats → productFeasibleFormatMask is ProductFormatMask for
// coverable occupancy. tryCandidate ranks ready SUs by live HR
// MatchingFrontierScore (successor cardinality / free-slot scarcity) after
// pressure and critical-path, before NodeOrder — pure probe, no setDesc.
// CreateTargetMIHazardRecognizer installs HaydnHazardRecognizer with
// IsPreRA=true (MRI-correct vreg GPR/DR/AR ports; no AltDesc/member stamp).
// Logical opcodes only through RA.
//
// SMS-RESMII (pre-RA slice): expose the pure exhaustive ≤3-issue product
// format ResMII oracle (haydn::bundle::computeExhaustiveProductResMII),
// greedy/preferred overestimate helpers, and the fail-close predicate
// (productResMIIFailsQualification). Also re-export the pure port lower-bound
// ResMII floor (HaydnPortModel) so list-sched / HR ownership pins the same
// 3×1W → ≥2 cycle fact as SMS MI packing without touching ResourceCycle.
//
// Soft-exit QoR (pre-RA): productSoftExitIIFloor combines format exhaustive
// ResMII with the port lower bound so qualification corpora can pin II floors
// (ports may bind when format-only ResMII is still 1). RecMII remains a
// DDG/SMS sibling surface (macc-acc-feedback); pre-RA never invents recurrence
// numbers. productQualKernelExactlyPackable is the post-RA exact-pack metrics
// pin (no HANDOFF invent / no setDesc).
//
// SMS shouldUseSchedule fail-close (pre-RA slice): pure stage-count and
// spill-pressure predicates that pin the same accept/reject law the SMS track
// applies in PipelinerLoopInfo::shouldUseSchedule (AIE canAcceptII peer) —
// without owning SMSchedule, RegPressureTracker, or ResourceCycle. Product
// defaults match -haydn-sms-max-stagecount=3 and
// -haydn-pipeliner-track-regpressure=true. StageCount = PrologueCount + 1.
// Pre-RA never freezes FormatID/setDesc from these metrics.
//
// Pre-RA packability surface (metrics only). Pre-RA never freezes FormatID,
// never stamps setDesc/member opcodes, and never materializes durable BUNDLE
// roots from SMS cycle membership or matching-frontier scores. Pure exact-cycle
// / ResMII oracles prove qualification co-issue sets remain packable under the
// shared product model so post-RA exact no-split commit can pack them without
// scheduled splits once registers are physical. Pre-RA multi-member BUNDLE
// freeze stays permanently off (StageCount>1 containment; product multi-stage
// is post-RA only).
//
// MOVE32-class ports (pre-RA surface): every explicit operand field
// reserves one port. `MOVE32 rd, rs, rs` is 2R1W on the MI path and the
// descriptor path. Pre-RA HR always takes the MI path. Sibling SMS owns
// ResourceCycle packing under the same demand.
//
// Generic-pass dual-run baseline (plan §8.3 / §8.4 #11): Full-only product
// ranking (matching-frontier ON, finer RP ON, isavail-delay OFF) must match
// a frozen generic-pass residual arm on cycle/pressure KPI when
// matching-frontier is disabled (pressure/critical stay primary; stock
// NodeOrder after pressure). Target HR is always installed via
// CreateTargetMIHazardRecognizer — never a null factory. Dual-run lit
// prera-format-generic-baseline.ll freezes spill/reload parity, post-RA
// multi-MI exact finalize, silent split/hard-root counters, and
// pre-greedy/pre-postmisched logical-only identity (no BUNDLE / no _S*).
// isavail-delay stays product OFF (seed1 residual); pre-RA packability
// metrics never invent BUNDLE roots.
//
// ILP / critical ranking residual attribution: product matching-frontier
// ResourceDemand fires only after pressure (RegExcess/RegCritical/RegMax)
// and critical weak edges. Residual arm (-matching-frontier=false) keeps
// those primary reasons and drops ResourceDemand (NodeOrder after
// pressure). Lit scheduler-ilp.ll / scheduler-critical-path.ll dual-run
// -stats pin product ResourceDemand present, residual ResourceDemand
// silent, RegMax still primary on critical kernels, multi-MI finalize
// parity, and logical-only through greedy. Soft-exit / exact-pack floors
// are independent of the ranking residual.
//
// SMS-HOOK II-wrap false-accept (pre-RA surface, plan §2.5 / §8.4 #8):
// Product class-3 inventory is empty (InstrStage cycles==1;
// ProductCrossCycleCapacityEnabled=false). CreateTargetMIHazardRecognizer
// installs HaydnHazardRecognizer IsPreRA with *linear* stage-relative
// scoreboard booking (DeltaCycles+StageCycle), not SMS modulo-II
// ResourceCycle phases. The classic SMS issue-time-only false-accept gap
// (independent ResourceCycle per phase accepts concurrent use that multi-
// cycle occupancy wrapping under II forbids) is sibling ownership. Pre-RA
// still pins the same catalog polarity via pure helpers below so list-sched
// / HR ownership never claims multi-cycle product support or invents setDesc
// from II-wrap metrics. Positive multi-cycle product needs an approved shared
// hook — not a catalog-only flip.
//
// FE5B / WP4 whole-kernel periodic certificate (pre-RA surface):
// ResourceCycle owns the pack-oracle certificate (proveWholeKernelPeriodicPhases,
// SMSPeriodicCertificate lifecycle, same-bank simultaneous-def fail-close).
// Pre-RA re-exports polarity so list-sched never claims multi-stage product
// enable or discards the original loop before final accept. WP5 multi-stage
// product policy is not flipped here.
//
// SMS/post-RA format-acceptance differential (pre-RA surface, plan §8.4 #7):
// Descriptor-derived format legality is pure exactTryAddProduct depth — the
// same API ResourceCycle canReserve/reserve and post-RA HR
// CurrentCycleCandidates / commitPlacementForEmit use. Pre-RA HR
// (CreateTargetMIHazardRecognizer IsPreRA) expands the same candidate set
// via exact matching; scoreMatchingFrontier.Feasible is that probe.
// Format accept/reject is opcode-keyed, so MI and descriptor forms of the
// same logical multiset agree. MOVE32-class port demand is 2R1W on both
// the MI and descriptor paths (per-field). Sibling SMS owns live
// ResourceCycle packing under that one law
// tests; pre-RA owns the HR / pure-exact polarity surface without touching
// ResourceCycle. Metrics-only; never setDesc / member opcodes.
//
// Plan: /ssd2/mhyang/haydn-plans/topics/scheduling/TOPIC.md
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPRERASCHEDSTRATEGY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPRERASCHEDSTRATEGY_H

#include "HaydnBundleFormatSolver.h"
#include "HaydnPortModel.h"
#include "HaydnResourceCycle.h" // FE5B WP4 periodic certificate surface
#include "HaydnResourceRestrictionClasses.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include <cstdint>
#include <vector>

namespace llvm {

// Pre-RA: GenericScheduler + AIE isAvailableNode pressure delayer +
// pressure-aware tryCandidate + matching-frontier ranking (logical only).
class HaydnPreRASchedStrategy : public GenericScheduler {
public:
  HaydnPreRASchedStrategy(const MachineSchedContext *C)
      : GenericScheduler(C) {}

  //===--------------------------------------------------------------------===//
  // Generic-pass dual-run product defaults
  //===--------------------------------------------------------------------===//
  // Pure product policy pins for dual-run KPI (matching-frontier ON/OFF and
  // optional finer-RP OFF residual). cl::opt init values must match these;
  // lit prera-format-generic-baseline.ll freezes cycle/pressure parity.
  // Target HR is always on — these flags only change tryCandidate ranking.
  //
  // Ranking residual attribution layers (ILP / critical pre-RA kernels):
  //   1. Pressure (RegExcess / RegCritical / RegMax) — primary both arms
  //   2. Matching-frontier ResourceDemand — product residual after (1)
  //   3. NodeOrder — tertiary after pressure (and after frontier when ON)
  // Residual arm disables only layer (2); it must not uninstall target HR,
  // invent setDesc/BUNDLE, or weaken soft-exit / port floors.

  /// Product: matching-frontier ranking after pressure/critical (plan §5.1).
  static constexpr bool productMatchingFrontierDefault = true;
  /// Generic residual arm for dual-run: stock NodeOrder after pressure.
  static constexpr bool genericPassMatchingFrontierBaseline = false;

  /// Product: AIE-style pressure-first tryCandidate (finer RP tracking).
  static constexpr bool productFinerRPTrackingDefault = true;
  /// Optional dual-run residual: pure GenericScheduler::tryCandidate pressure.
  static constexpr bool genericPassFinerRPTrackingBaseline = false;

  /// Product: isAvailableNode pressure delayer OFF (seed1 HOSTCALL residual).
  static constexpr bool productIsAvailPressureDelayDefault = false;

  /// Product ranking residual is matching-frontier only — pressure/critical
  /// stay primary under both dual-run arms (ILP / critical attribution).
  static constexpr bool productRankingPressurePrimary = true;
  /// Product ResourceDemand (matching-frontier) is the residual ranking arm
  /// after pressure/critical; residual dual-run disables only this layer.
  static constexpr bool productMatchingFrontierIsRankingResidual = true;

  void initPolicy(MachineBasicBlock::iterator Begin,
                  MachineBasicBlock::iterator End,
                  unsigned NumRegionInstrs) override;

  void initialize(ScheduleDAGMI *DAG) override;

  void enterRegion(MachineBasicBlock *BB, MachineBasicBlock::iterator Begin,
                   MachineBasicBlock::iterator End, unsigned NumRegionInstrs);
  void leaveRegion(const SUnit &ExitSU);

  // AIE peer: delay nodes that would exceed pressure if a pending reducer
  // exists (AIEMachineScheduler.cpp isAvailableNode).
  bool isAvailableNode(SUnit &SU, SchedBoundary &Zone,
                       bool VerifyReadyCycle) override;

 // : product FormatID frontier for Pre-RA (logical only; no freeze).
  // AIE has no explicit pre-RA FormatID mask; Haydn names the same occupancy →
  // covering-mask path SMS Bundle uses (AIEBundle.h:150-156 getFormatOrNull /
  // AIEFormat.cpp:18-27 first-covering strengthened to a bitset).
  // Product size-1 Full → ProductFormatMask for empty and Full-covering occ.
  static uint64_t productFeasibleFormatMask(SlotBits Occupied = 0) {
    return haydn::bundle::productFeasibleFormatMask(Occupied);
  }

  //===--------------------------------------------------------------------===//
  // SMS-RESMII — pre-RA surface for exhaustive ≤3 format ResMII oracle
  //===--------------------------------------------------------------------===//
  // Pure helpers (no MIR mutation). Greedy = left-to-right exactTryAddProduct
  // (SMS ResourceCycle / DFA peer depth). Exhaustive = set-partition DP over
  // ≤ISSUE_SLOT_COUNT-member packable cycles (N ≤ MaxExhaustiveProductResMIIOps).
  // Positive overestimate → productResMIIFailsQualification (fail-close signal
  // for format-dependent SMS product). Preferred-collapse overestimate is a
  // weaker diagnostic baseline (first-fit dead-end inflation).
  // Port lower-bound ResMII is independent of format packing (3×1W GPR → ≥2).

  /// Left-to-right greedy product ResMII (exactTryAddProduct depth).
  static unsigned productGreedyResMII(ArrayRef<unsigned> Opcodes) {
    return haydn::bundle::computeProductResMII(Opcodes);
  }

  /// Exhaustive ≤3-issue format ResMII oracle (set partition; N≤12 exact).
  static unsigned productExhaustiveResMII(ArrayRef<unsigned> Opcodes) {
    return haydn::bundle::computeExhaustiveProductResMII(Opcodes);
  }

  /// Preferred-collapse sequential ResMII (weaker first-fit diagnostic).
  static unsigned productPreferredResMII(ArrayRef<unsigned> Opcodes) {
    return haydn::bundle::computePreferredProductResMII(Opcodes);
  }

  /// Greedy − exhaustive. Positive ⇒ greedy overestimates the format bound.
  static int productResMIIOverestimate(ArrayRef<unsigned> Opcodes) {
    return haydn::bundle::productResMIIOverestimate(Opcodes);
  }

  /// Preferred − exhaustive (diagnostic; first-fit dead-end detection).
  static int productPreferredResMIIOverestimate(ArrayRef<unsigned> Opcodes) {
    return haydn::bundle::preferredProductResMIIOverestimate(Opcodes);
  }

  /// Fail-close: greedy overestimates exhaustive oracle on an exact-bound body.
  /// True ⇒ format-dependent SMS product qualification must reject (no soft log).
  static bool productResMIIFailsQualification(ArrayRef<unsigned> Opcodes) {
    return haydn::bundle::productResMIIFailsQualification(Opcodes);
  }

  /// Pure port-pressure lower bound on issue cycles (ceil demand / budget).
  /// Three independent GPR writes → ≥2 under HAYDN_GPR_WRITE_PORTS=2.
  static unsigned portLowerBoundResMII(unsigned GPRReads, unsigned GPRWrites,
                                      unsigned DRReads = 0,
                                      unsigned DRWrites = 0,
                                      unsigned ARReads = 0,
                                      unsigned ARWrites = 0) {
    return haydnPortLowerBoundResMII(GPRReads, GPRWrites, DRReads, DRWrites,
                                    ARReads, ARWrites);
  }

  /// Soft-exit II lower bound for a qualification multiset: max of exhaustive
  /// product format ResMII and pure port-pressure ResMII.
  ///
  /// Ports bind when format-only packing still reports 1 (classic 3×1W GPR
  /// write body under HAYDN_GPR_WRITE_PORTS=2). Metrics-only — never freezes
  /// FormatID, never stamps setDesc, never invents BUNDLE membership or RecMII.
  /// Sibling SMS track owns analyzeLoop Res/Rec/II reporting and ResourceCycle.
  static unsigned productSoftExitIIFloor(ArrayRef<unsigned> Opcodes,
                                         unsigned GPRReads, unsigned GPRWrites,
                                         unsigned DRReads = 0,
                                         unsigned DRWrites = 0,
                                         unsigned ARReads = 0,
                                         unsigned ARWrites = 0) {
    const unsigned FormatII = productExhaustiveResMII(Opcodes);
    const unsigned PortII = portLowerBoundResMII(
        GPRReads, GPRWrites, DRReads, DRWrites, ARReads, ARWrites);
    return FormatII > PortII ? FormatII : PortII;
  }

  //===--------------------------------------------------------------------===//
  // SMS shouldUseSchedule — stage / spill-pressure fail-close (pre-RA surface)
  //===--------------------------------------------------------------------===//
  // Pure accept/reject law for schedules SMS already found. Mirrors AIE
  // canAcceptII / shouldUseSchedule stage-count + TrackRegPressure/canAllocate
  // gates folded into HaydnPipelinerLoopInfo::shouldUseSchedule. This surface
  // does not run SMSchedule or RegPressureTracker — it pins the decision
  // predicates so list-sched ownership and qualification corpora share one
  // fail-close contract with the SMS sibling (ResourceCycle / pipeliner hooks).
  // Metrics-only: never freezes FormatID, never stamps setDesc/member opcodes.
  //
  // StageCount = PrologueCount + 1 (SMSchedule::getMaxStageCount() + 1).
  // Product defaults match cl::opt -haydn-sms-max-stagecount (3) and
  // -haydn-pipeliner-track-regpressure (true). Product Option A containment
  // further rejects every StageCount > 1 (soft and ZOL); only proven soft
  // StageCount == 1 schedules may mutate MIR. PreferPostPipeliner and
  // force-pressure-reject remain flag-only and are outside this pure surface.

  /// Product max total stages (prologue stages + 1). AIE LoopMaxStageCount peer.
  static constexpr unsigned productSMSMaxStageCount = 3;

  /// Product pre-RA SMS containment: soft StageCount == 1 only. Multi-stage is
  /// post-RA greenfield work; pre-RA rejects StageCount > 1 before mutation.
  static constexpr unsigned productSMSContainmentMaxStageCount = 1;

  /// Product default for the spill-pressure gate (AIE track-regpressure peer).
  static constexpr bool productSMSTrackRegPressureDefault = true;

  /// True when StageCount exceeds the product/max stage gate → reject SMS.
  static bool smsStageCountExceedsMax(
      unsigned StageCount,
      unsigned MaxStageCount = productSMSMaxStageCount) {
    return StageCount > MaxStageCount;
  }

  /// True when StageCount exceeds product Option A StageCount1 containment.
  static bool smsProductStageCountExceedsContainment(
      unsigned StageCount,
      unsigned ContainmentMax = productSMSContainmentMaxStageCount) {
    return StageCount > ContainmentMax;
  }

  /// ZOL: single-stage schedules have no pipeline overlap → reject (AIE peer).
  static bool smsZOLRejectsSingleStage(bool IsZOL, unsigned StageCount) {
    return IsZOL && StageCount <= 1u;
  }

  /// ZOL: prologue peels need MinTripCount > PrologueCount; MinTripCount==0
  /// (unknown/unbounded) refuses every multi-stage schedule (AIE canAcceptII).
  static bool smsZOLRejectsMinTrip(bool IsZOL, unsigned PrologueCount,
                                   int64_t MinTripCount) {
    return IsZOL && (MinTripCount == 0 ||
                     static_cast<int64_t>(PrologueCount) >= MinTripCount);
  }

  /// Pure RA pressure-set excess: any MaxSetPressure[i] > Limits[i].
  /// Peer of canAllocateSMS CheckPressureExcess (incoming live-in pressure).
  /// Empty inputs → no excess (vacuously allocatable). Length mismatch uses the
  /// shorter span so partial vectors remain fail-closed only on known sets.
  static bool smsSpillPressureExceedsLimits(ArrayRef<unsigned> MaxSetPressure,
                                            ArrayRef<unsigned> Limits) {
    const size_t N = MaxSetPressure.size() < Limits.size()
                         ? MaxSetPressure.size()
                         : Limits.size();
    for (size_t I = 0; I < N; ++I)
      if (MaxSetPressure[I] > Limits[I])
        return true;
    return false;
  }

  /// Combined shouldUseSchedule fail-close (true = reject schedule).
  /// PreferPostPipeliner is out of product path (always false) and omitted.
  /// \p PressureExcess is the pure result of smsSpillPressureExceedsLimits (or
  /// canAllocateSMS inverted). When TrackRegPressure is false, pressure is
  /// ignored — matching -haydn-pipeliner-track-regpressure=false.
  /// Historical AIE-shaped surface: max-stage + ZOL + pressure only. Product
  /// Option A StageCount1 containment is layered by
  /// smsProductShouldUseScheduleFailsClosed (used by live shouldUseSchedule).
  static bool smsShouldUseScheduleFailsClosed(
      bool IsZOL, unsigned PrologueCount, int64_t MinTripCount,
      bool PressureExcess,
      unsigned MaxStageCount = productSMSMaxStageCount,
      bool TrackRegPressure = productSMSTrackRegPressureDefault) {
    const unsigned StageCount = PrologueCount + 1u;
    if (smsZOLRejectsSingleStage(IsZOL, StageCount))
      return true;
    if (smsZOLRejectsMinTrip(IsZOL, PrologueCount, MinTripCount))
      return true;
    if (smsStageCountExceedsMax(StageCount, MaxStageCount))
      return true;
    if (TrackRegPressure && PressureExcess)
      return true;
    return false;
  }

  /// Inverse of smsShouldUseScheduleFailsClosed — accept remaining schedules.
  static bool smsShouldUseScheduleAccepts(
      bool IsZOL, unsigned PrologueCount, int64_t MinTripCount,
      bool PressureExcess,
      unsigned MaxStageCount = productSMSMaxStageCount,
      bool TrackRegPressure = productSMSTrackRegPressureDefault) {
    return !smsShouldUseScheduleFailsClosed(IsZOL, PrologueCount, MinTripCount,
                                            PressureExcess, MaxStageCount,
                                            TrackRegPressure);
  }

  /// Product shouldUseSchedule fail-close including StageCount1 containment.
  /// Live PipelinerLoopInfo::shouldUseSchedule must agree with this polarity
  /// for every non-flag special case (force-pressure / prefer-post-pipeliner).
  static bool smsProductShouldUseScheduleFailsClosed(
      bool IsZOL, unsigned PrologueCount, int64_t MinTripCount,
      bool PressureExcess,
      unsigned MaxStageCount = productSMSMaxStageCount,
      bool TrackRegPressure = productSMSTrackRegPressureDefault,
      unsigned ContainmentMax = productSMSContainmentMaxStageCount) {
    const unsigned StageCount = PrologueCount + 1u;
    if (smsProductStageCountExceedsContainment(StageCount, ContainmentMax))
      return true;
    return smsShouldUseScheduleFailsClosed(IsZOL, PrologueCount, MinTripCount,
                                           PressureExcess, MaxStageCount,
                                           TrackRegPressure);
  }

  /// Inverse of smsProductShouldUseScheduleFailsClosed.
  static bool smsProductShouldUseScheduleAccepts(
      bool IsZOL, unsigned PrologueCount, int64_t MinTripCount,
      bool PressureExcess,
      unsigned MaxStageCount = productSMSMaxStageCount,
      bool TrackRegPressure = productSMSTrackRegPressureDefault,
      unsigned ContainmentMax = productSMSContainmentMaxStageCount) {
    return !smsProductShouldUseScheduleFailsClosed(
        IsZOL, PrologueCount, MinTripCount, PressureExcess, MaxStageCount,
        TrackRegPressure, ContainmentMax);
  }

  //===--------------------------------------------------------------------===//
  // SMS-HOOK II-wrap false-accept — pre-RA fail-closed pin
  //===--------------------------------------------------------------------===//
  // Catalog authority: HaydnResourceRestrictionClasses. Product InstrStage
  // cycles==1 and ProductCrossCycleCapacityEnabled=false, so multi-cycle /
  // II-wrap occupancy never product-enables through list-sched or SMS.
  // CreateTargetMIHazardRecognizer(IsPreRA) installs a linear stage-relative
  // scoreboard (not modulo-II ResourceCycle); the classic issue-time-only
  // false-accept differential lives on the sibling SMS track. These pure
  // helpers pin the same reject polarity for list-sched / HR ownership so
  // pre-RA never claims multi-cycle product support or freezes setDesc from
  // II-wrap metrics. Metrics-only; no MIR mutation.

  /// Product pin: anonymous cross-cycle capacity is not product-enabled.
  static constexpr bool productCrossCycleCapacityEnabled =
      haydn::restriction::ProductCrossCycleCapacityEnabled;

  /// Product InstrStage cycle-width upper bound (all product stages single).
  static constexpr unsigned productMaxInstrStageCycles =
      haydn::restriction::ProductMaxInstrStageCycles;

  /// Product inventory: class-3 cross-cycle entries (must stay 0).
  static constexpr unsigned productClass3RestrictionCount =
      haydn::restriction::ProductClass3RestrictionCount;

  /// True when multi-cycle occupancy issued at \p IssuePhase for
  /// \p StageCycles under II covers modulo phase \p QueryPhase.
  static constexpr bool smsIIWrapOccupiesPhase(unsigned IssuePhase,
                                              unsigned StageCycles, unsigned II,
                                              unsigned QueryPhase) {
    return haydn::restriction::smsIIWrapOccupiesPhase(IssuePhase, StageCycles,
                                                      II, QueryPhase);
  }

  /// True when multi-cycle occupancy under \p II covers a phase other than
  /// the issue phase (or self-wraps when StageCycles > II).
  static constexpr bool smsIIWrapSpansBeyondIssuePhase(unsigned StageCycles,
                                                      unsigned II) {
    return haydn::restriction::smsIIWrapSpansBeyondIssuePhase(StageCycles, II);
  }

  /// True when StageCycles > II: one instruction claims the same phase twice.
  static constexpr bool smsIIWrapSelfConflicts(unsigned StageCycles,
                                               unsigned II) {
    return haydn::restriction::smsIIWrapSelfConflicts(StageCycles, II);
  }

  /// SMS-HOOK polarity: multi-cycle stage must reject under product law
  /// whenever II-wrap would span beyond issue (class-3 disabled).
  static constexpr bool smsHookRejectsIIWrapFalseAccept(unsigned StageCycles,
                                                       unsigned II) {
    return haydn::restriction::smsHookRejectsIIWrapFalseAccept(StageCycles, II);
  }

  /// Multi-cycle stage alone (catalog pin) rejects under product law.
  static constexpr bool smsHookRejectsMultiCycleStage(unsigned Cycles) {
    return haydn::restriction::smsHookRejectsMultiCycleStage(Cycles);
  }

  /// Classic false-accept shape pin: StageCycles=2 under II=2 issued at phase
  /// 0 occupies phase 1, and SMS-HOOK rejects that multi-cycle stage. Pre-RA
  /// re-exports so unit/list-sched ownership matches the catalog without
  /// touching ResourceCycle. Always true under current product law.
  static constexpr bool productIIWrapFalseAcceptFailsClosed() {
    return !productCrossCycleCapacityEnabled &&
           productMaxInstrStageCycles == 1u &&
           productClass3RestrictionCount == 0u &&
           smsIIWrapOccupiesPhase(/*Issue=*/0, /*Stage=*/2, /*II=*/2,
                                  /*Query=*/1) &&
           smsHookRejectsIIWrapFalseAccept(/*StageCycles=*/2, /*II=*/2) &&
           !smsHookRejectsIIWrapFalseAccept(/*StageCycles=*/1, /*II=*/2);
  }

  //===--------------------------------------------------------------------===//
  // FE5B whole-kernel periodic certificate — pre-RA re-export (WP4)
  //===--------------------------------------------------------------------===//
  // Authority: HaydnResourceCycle (pack oracle). Lifecycle retains the
  // original loop until Accepted; Rejected requires recoverable rollback.
  // Metrics/polarity only here — no MIR mutation, no WP5 multi-stage enable.

  /// Product pin: multi-cycle / II-wrap long occupancy fails closed.
  static constexpr bool
  productIIWrapLongOccupancyFailsClosed(unsigned StageCycles, unsigned II) {
    return HaydnResourceCycle::productIIWrapLongOccupancyFailsClosed(
        StageCycles, II);
  }

  /// Original loop must remain until final accept (not on Accepted/Rejected).
  static constexpr bool
  productSMSCertOriginalLoopMustRemain(SMSCertLifecycle S) {
    return SMSPeriodicCertificate::originalLoopMustRemain(S);
  }

  /// Final accept is the only state that may discard the original loop.
  static constexpr bool
  productSMSCertMayDiscardOriginalLoop(SMSCertLifecycle S) {
    return SMSPeriodicCertificate::mayDiscardOriginalLoop(S);
  }

  /// Rejected certificates require recoverable rollback if rewrite ran.
  static constexpr bool productSMSCertMustRollback(SMSCertLifecycle S) {
    return SMSPeriodicCertificate::mustRollback(S);
  }

  /// Whole-kernel II/phase proof (ResourceCycle pack oracle).
  static bool productProveWholeKernelPeriodicPhases(
      unsigned II, ArrayRef<SMSCertPhaseOp> Ops) {
    return HaydnResourceCycle::proveWholeKernelPeriodicPhases(II, Ops);
  }

  /// Transactional cert: retain → pre-proof → post-valid → accept/reject.
  static SMSCertLifecycle productRunPeriodicCertificate(
      unsigned II, ArrayRef<SMSCertPhaseOp> Ops, bool PostRewriteStillValid) {
    return HaydnResourceCycle::runPeriodicCertificate(II, Ops,
                                                      PostRewriteStillValid);
  }

  /// Full WP4 product pins (lifecycle + II-wrap + same-bank + legal kernel).
  static bool productPeriodicCertificatePins() {
    return HaydnResourceCycle::productPeriodicCertificatePins();
  }

  /// Half-enabled multi-stage remains forbidden until WP1–WP4 product ON.
  /// Certificate surface never claims multi-stage product enable alone.
  static constexpr bool productHalfEnabledMultiStageForbidden() {
    return productSMSCertOriginalLoopMustRemain(
               SMSCertLifecycle::OriginalRetained) &&
           productSMSCertOriginalLoopMustRemain(
               SMSCertLifecycle::PreRewriteProved) &&
           productSMSCertOriginalLoopMustRemain(
               SMSCertLifecycle::PostRewriteValid) &&
           !productSMSCertMayDiscardOriginalLoop(
               SMSCertLifecycle::OriginalRetained) &&
           productSMSCertMustRollback(SMSCertLifecycle::Rejected) &&
           !productCrossCycleCapacityEnabled &&
           productClass3RestrictionCount == 0u;
  }


  //===--------------------------------------------------------------------===//
  // SMS/post-RA format-acceptance differential — pre-RA HR surface
  //===--------------------------------------------------------------------===//
  // Plan §8.4 #7. Descriptor-derived format legality is pure
  // exactTryAddProduct (ResourceCycle canReserve peer; post-RA HR
  // CurrentCycleCandidates / commitPlacement peer). CreateTargetMIHazardRecognizer
  // IsPreRA expands the same candidate set; scoreMatchingFrontier probes it
  // without freeze. Format is opcode-keyed → MI and descriptor forms of the
  // same logical multiset have identical format accept/reject polarity.
  // Operand-dependent port demand (MOVE32-class below) is orthogonal.
  // Sibling SMS owns live ResourceCycle differential tests. Metrics-only;
  // no MIR mutation / setDesc / member opcodes.

  /// Sequential pure exact packing: true iff every opcode in order joins one
  /// product cycle under exactTryAddProduct (ResourceCycle / post-RA HR peer).
  static bool productExactCanPackSequence(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    HaydnMCFormats Fmts;
    return haydn::bundle::exactCanPackProductSequence(Fmts, Opcodes);
  }

  /// Greedy open-new-cycle count via pure exact depth (≡ productGreedyResMII
  /// / sequential ResourceCycle canReserve walk for format-only opcodes).
  static unsigned productExactSequentialCycleCount(ArrayRef<unsigned> Opcodes) {
    return productGreedyResMII(Opcodes);
  }

  /// Format accept/reject polarity for one multiset under exact matching
  /// (order-insensitive set oracle for N ≤ issue width). Same pure path
  /// productFormsOneExactCycle uses for packability metrics.
  static bool productExactCanPackSet(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    if (Opcodes.size() > Haydn::ISSUE_SLOT_COUNT)
      return false;
    HaydnMCFormats Fmts;
    return haydn::bundle::exactCanPackProductSet(Fmts, Opcodes);
  }

  /// True when two opcode sequences have identical sequential format
  /// acceptance (MI multiset vs descriptor multiset). Format is opcode-keyed,
  /// so equal opcodes always agree; unequal multisets may differ.
  static bool productFormatAcceptanceAgrees(ArrayRef<unsigned> A,
                                            ArrayRef<unsigned> B) {
    return productExactCanPackSequence(A) == productExactCanPackSequence(B);
  }

  //===--------------------------------------------------------------------===//
  // ILP / critical dual-run residual attribution — pure pins
  //===--------------------------------------------------------------------===//
  // Plan §8.3 pre-RA exit + §8.4 #11 generic-pass freeze on ILP and
  // critical-path kernels. Pure probes only: ranking residual must not
  // change soft-exit floors, exact-pack polarity, or invent HANDOFF/setDesc.
  // Live -stats attribution lives in scheduler-ilp.ll /
  // scheduler-critical-path.ll (product ResourceDemand vs residual silent;
  // RegMax primary on critical; multi-MI finalize parity).

  /// Ranking residual attribution layer pin: pressure primary; matching-
  /// frontier is the residual layer; residual arm disables only that layer.
  static constexpr bool productIlpCriticalRankingResidualLayers() {
    return productRankingPressurePrimary &&
           productMatchingFrontierIsRankingResidual &&
           productMatchingFrontierDefault &&
           !genericPassMatchingFrontierBaseline &&
           productFinerRPTrackingDefault &&
           !genericPassFinerRPTrackingBaseline &&
           !productIsAvailPressureDelayDefault;
  }

  /// ILP coissue + critical soft-exit floors independent of ranking residual.
  /// Three independent ALU ops pack under Full (format ResMII 1) while 3×1W
  /// port floor is 2; coissue ADD+XOR+OR packs; soft-exit still binds ports.
  /// Metrics-only — never freezes FormatID / setDesc / BUNDLE membership.
  static bool productIlpCriticalDualRunResidualPins() {
    if (!productIlpCriticalRankingResidualLayers())
      return false;
    unsigned IlpOps[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    unsigned Coissue[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
    // ILP independent ALU: format packable, port floor binds soft-exit.
    if (!productFormsOneExactCycle(IlpOps) ||
        !productQualKernelExactlyPackable(IlpOps) ||
        productExhaustiveResMII(IlpOps) != 1u ||
        portLowerBoundResMII(/*GPRR=*/0, /*GPRW=*/3) != 2u ||
        productSoftExitIIFloor(IlpOps, 0, 3) != 2u)
      return false;
    // Critical-friendly coissue (independent side chains) packs one cycle;
    // full 3×(2R1W) soft-exit still binds at 2 — ranking residual must not
    // rewrite these floors.
    if (!productFormsOneExactCycle(Coissue) ||
        !productQualKernelCoissuePackable(Coissue) ||
        productExhaustiveResMII(Coissue) != 1u ||
        productSoftExitIIFloor(Coissue, /*GPRR=*/6, /*GPRW=*/3) != 2u)
      return false;
    // Format frontier stays size-1 Full (no compact residual invent).
    return productFeasibleFormatMask(/*Occupied=*/0) ==
           haydn::bundle::ProductFormatMask;
  }

  /// Product pin for §8.4 #7 format-acceptance shapes used by list-sched / HR:
  /// three ADD32 pack; two ST32 do not (need 2 cycles); rematch triple packs;
  /// co-issue ADD32+XOR32+OR32 packs; sequential cycle counts match greedy.
  static bool productFormatAcceptanceDifferentialPins() {
    unsigned ThreeADD[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    unsigned TwoST[] = {Haydn::ST32, Haydn::ST32};
    unsigned Rematch[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    unsigned Coissue[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
    return productExactCanPackSequence(ThreeADD) &&
           productExactCanPackSet(ThreeADD) &&
           !productExactCanPackSequence(TwoST) &&
           !productExactCanPackSet(TwoST) &&
           productExactCanPackSequence(Rematch) &&
           productExactCanPackSequence(Coissue) &&
           productExactSequentialCycleCount(ThreeADD) == 1u &&
           productExactSequentialCycleCount(TwoST) == 2u &&
           productExactSequentialCycleCount(Rematch) == 1u &&
           productFormsOneExactCycle(Rematch) &&
           productFormsOneExactCycle(Coissue) &&
           // MI ≡ desc for same opcodes (format opcode-keyed).
           productFormatAcceptanceAgrees(Rematch, Rematch) &&
           productFormatAcceptanceAgrees(TwoST, TwoST);
  }

  //===--------------------------------------------------------------------===//
  // MOVE32-class ports — pre-RA ownership pin (per-field)
  //===--------------------------------------------------------------------===//
  // MCInstrDesc shape for MOVE32 is (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2):
  // NumDefs=1, two register uses. Every explicit field reserves one port, so
  // MOVE32 rd, rs, rs is 2R1W on the MI path and the descriptor path.
  // CreateTargetMIHazardRecognizer(IsPreRA) uses only the MI path.

  /// MI PortModel demand for MOVE32 rd, rs, rs (per-field, no identity dedup).
  static constexpr unsigned move32ClassMiRepeatedSrcGprReads = 2;
  static constexpr unsigned move32ClassMiRepeatedSrcGprWrites = 1;
  /// Descriptor-only shape (1 def + 2 use slots) with no same-reg identity.
  static constexpr unsigned move32ClassDescShapeGprReads = 2;
  static constexpr unsigned move32ClassDescShapeGprWrites = 1;

  /// Retired: both paths are 2R1W (per-field).
  static constexpr bool move32ClassDescOvercountsMiPorts() {
    return move32ClassDescShapeGprReads > move32ClassMiRepeatedSrcGprReads &&
           move32ClassDescShapeGprWrites == move32ClassMiRepeatedSrcGprWrites;
  }

  /// Port lower-bound ResMII for N identical MOVE32-class ops under the MI
  /// repeated-source model (2R1W each). N=3 → ≥2 (write pool and 6R).
  static unsigned move32ClassMiRepeatedSrcPortLowerBoundResMII(unsigned N) {
    return portLowerBoundResMII(N * move32ClassMiRepeatedSrcGprReads,
                               N * move32ClassMiRepeatedSrcGprWrites);
  }

  /// Port lower-bound ResMII for N identical MOVE32-class ops under the
  /// descriptor-shape model (2R1W each). Same as the MI path.
  static unsigned move32ClassDescShapePortLowerBoundResMII(unsigned N) {
    return portLowerBoundResMII(N * move32ClassDescShapeGprReads,
                               N * move32ClassDescShapeGprWrites);
  }

  /// Retired: both shapes are 2R1W, so this is never true.
  static bool move32ClassDescSaturatesReadPoolEarlier(unsigned N) {
    const unsigned MiR = N * move32ClassMiRepeatedSrcGprReads;
    const unsigned DescR = N * move32ClassDescShapeGprReads;
    return MiR <= HAYDN_GPR_READ_PORTS && DescR > HAYDN_GPR_READ_PORTS;
  }

  //===--------------------------------------------------------------------===//
  // Pre-RA metrics-only packability (no hard cycle groups)
  //===--------------------------------------------------------------------===//
  // Matching-frontier tryCandidate and these helpers are pure probes. They
  // never invent standard BUNDLE roots, never stamp AltDesc/setDesc, and do
  // not read SMS scalar metrics as membership. Sibling SMS track owns
  // ResourceCycle / analyzeLoop packability metrics.

  /// True iff \p Opcodes form one legal product cycle under exact matching
  /// (alts + rematch). Empty is vacuously true. Pure; no MIR mutation.
  static bool productFormsOneExactCycle(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    HaydnMCFormats Fmts;
    return haydn::bundle::exactCanFormOneProductCycle(Fmts, Opcodes);
  }

  /// Qualification co-issue packability: size fits one issue cycle and exact
  /// matching packs the full multiset. Used to pin post-RA no-split co-issue
  /// sets (e.g. ADD32+XOR32+OR32, ADD32+2×ADD64 rematch) from the pre-RA
  /// surface without claiming a durable handoff group.
  static bool productQualKernelCoissuePackable(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    if (Opcodes.size() > Haydn::ISSUE_SLOT_COUNT)
      return false;
    return productFormsOneExactCycle(Opcodes);
  }

  /// Body-level qualification packability under the exhaustive ≤3 format
  /// oracle: no greedy overestimate (when N is inside the exact DP bound),
  /// and a finite product cover exists. For N >
  /// MaxExhaustiveProductResMIIOps the exhaustive oracle falls back to
  /// greedy (same contract as SMS-RESMII): do **not** fail-close on the
  /// inexact oracle — a finite greedy cover remains product-legal. Does
  /// **not** create hard BUNDLE membership — metrics only.
  static bool productQualKernelExactlyPackable(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    // Finite exhaustive cover is product-legal. Greedy overestimate is a
    // conservative II floor, not un-packable under Option A containment.
    return productExhaustiveResMII(Opcodes) >= 1u;
  }

protected:
  bool tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand,
                    SchedBoundary *Zone) const override;

  // Whether delaying DelayedSU for Delayer would form a delay cycle.
  bool canBeDelayed(const SUnit &DelayedSU, const SUnit &Delayer) const;

private:
  static constexpr unsigned UnknownSUNum = ~0u;

  MachineBasicBlock *CurMBB = nullptr;
  /// Top-level BUNDLE roots present at enterRegion (phase-firewall).
  unsigned PreRAEnterBundleRoots = 0;
  /// Bundled private members (isBundledWithPred) present at enterRegion.
  unsigned PreRAEnterBundledMembers = 0;
  /// Placement-form opcodes (private members / residual S-slot peers) at enter.
  unsigned PreRAEnterPrivatePlacementOps = 0;
  std::vector<unsigned> PSetThresholds;
  // SUDelayerMap[SU] = NodeNum of SU we are waiting for (AIE).
  std::vector<unsigned> SUDelayerMap;
};

class HaydnScheduleDAGMILive final : public ScheduleDAGMILive {
public:
  using ScheduleDAGMILive::ScheduleDAGMILive;

  void enterRegion(MachineBasicBlock *BB, MachineBasicBlock::iterator Begin,
                   MachineBasicBlock::iterator End,
                   unsigned RegionInstrs) override;
  void exitRegion() override;

  HaydnPreRASchedStrategy *getSchedImpl() const {
    return static_cast<HaydnPreRASchedStrategy *>(SchedImpl.get());
  }
};

ScheduleDAGInstrs *createHaydnPreRAScheduler(MachineSchedContext *C);

} // namespace llvm

#endif
