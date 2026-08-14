//===- HaydnResourceRestrictionClasses.h - SMS class catalog -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Single inventory for Full-only SMS resource restrictions. Every hardware
// restriction consumed by SMS must be classified as exactly one of:
//
//   1. Issue-cycle format / capacity — same-modulo-cycle demand handled by
//      generated PacketFormats + HaydnResourceCycle / PackLegality / HR.
//   2. Real data / order / recurrence — generic DDG edges, itinerary operand
//      latency, and existing mutation hooks (getSMSMutations). Not a modulo
//      reservation table and not anonymous multi-cycle capacity.
//   3. Anonymous cross-cycle capacity — multi-cycle FU occupancy without a
//      corresponding dependence edge. Product set is empty: every product
//      InstrStage uses cycles==1. Draft ARCTAN/SIN_COS (uimm4+2) multi-cycle
//      slot lock remains deferred and is not product-enabled; those opcodes
//      are class-1 issue-alone only (PackLegality rule 4 / isLockedSlotDspOp).
//
// Peer of HaydnHWLoopContracts.h: constants and static_asserts so a silent
// reclassification trips CI. SMS-HOOK multi-cycle scan in HaydnInstrInfo
// consumes ProductCrossCycleCapacityEnabled — when false, any InstrStage with
// getCycles()>1 fail-closes analyzeLoopForPipelining (AIE precedent).
//
// Authority plan surfaces: PackLegality product rules 1–7; HaydnResourceCycle
// same-issue-cycle adapter; HaydnHazardRecognizer ports/slots/WAW/locked DSP
// and CSRW↔SET; MachinePipeliner DDG for class 2. Do not invent a second
// product format row, multi-width object, pad-drop policy, or new
// pass/side-map from this catalog.
//
// Dedicated unit: unittests/Target/Haydn/HaydnResourceRestrictionClassesTest.cpp
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCERESTRICTIONCLASSES_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCERESTRICTIONCLASSES_H

#include "HaydnPackLegality.h"
#include <cstdint>

namespace llvm {
namespace haydn {
namespace restriction {

/// SMS restriction taxonomy (plan §2.5 three-way split). Values are the
/// durable class numbers — do not renumber without unit + lit updates.
enum class SMSRestrictionClass : unsigned {
  IssueCycleFormatCapacity = 1,
  DataOrderRecurrence = 2,
  AnonymousCrossCycleCapacity = 3,
};

//===----------------------------------------------------------------------===//
// Class 1 — issue-cycle format / capacity
//===----------------------------------------------------------------------===//
//
// Same-modulo-cycle demand. Handled by PacketFormats + HaydnResourceCycle
// (descriptor-derived placement), PackLegality / HazardRecognizer (ports,
// slots, WAW, locked DSP, CSRW↔SET). ARCTAN/SIN_COS issue-alone is class 1,
// not class 3: it is a same-cycle capacity gate, not a multi-cycle lock.
//===----------------------------------------------------------------------===//

/// PackLegality product rule count (header inventory rules 1–7).
inline constexpr unsigned PackLegalityRuleCount = 7;

/// PackLegality rule identifiers (match HaydnPackLegality.h product list).
inline constexpr unsigned PackRuleDualR0DefsIllegal = 1;
inline constexpr unsigned PackRuleDualLiveSameRegWAWIllegal = 2;
inline constexpr unsigned PackRuleDualDeadImplicitSfrIllegal = 3;
inline constexpr unsigned PackRuleArctanSinCosIssueAlone = 4;
inline constexpr unsigned PackRuleIssueAndPortBudgets = 5;
inline constexpr unsigned PackRulePlacementFieldSlots = 6;
inline constexpr unsigned PackRuleCsrwSetHwloopExclusion = 7;

/// Product issue cap (PackLegality rule 5 / Haydn::ISSUE_SLOT_COUNT peer).
inline constexpr unsigned Class1MaxIssuePerCycle = pack::MaxIssuePerCycle;

/// True when a PackLegality rule number is in the sealed class-1 inventory.
inline constexpr bool isClass1PackLegalityRule(unsigned Rule) {
  return Rule >= 1 && Rule <= PackLegalityRuleCount;
}

/// ARCTAN / SIN_COS issue-alone is class 1. Draft multi-cycle (uimm4+2) lock
/// is not product-enabled (see ProductDraftArctanMultiCycleLockEnabled).
inline constexpr bool Class1ArctanSinCosIssueAloneOnly = true;

/// Class-1 ownership surfaces (documentation pins for unit tests).
inline constexpr bool Class1OwnedByResourceCycle = true;
inline constexpr bool Class1OwnedByPackLegality = true;
inline constexpr bool Class1OwnedByHazardRecognizer = true;

//===----------------------------------------------------------------------===//
// Class 2 — real data / order / recurrence
//===----------------------------------------------------------------------===//
//
// Generic DDG + itinerary operand latency + getSMSMutations. RecMII is DDG
// ownership (e.g. acc→acc feedback). Mutations may add real dependences but
// must not encode anonymous multi-cycle capacity as fake edges.
//===----------------------------------------------------------------------===//

inline constexpr bool Class2OwnedByGenericDDG = true;
inline constexpr bool Class2OwnedByItineraryOperandLatency = true;
inline constexpr bool Class2OwnedBySMSMutations = true;
/// Mutations are not a modulo reservation table for class-3 capacity.
inline constexpr bool Class2MutationsAreNotCrossCycleCapacity = true;

//===----------------------------------------------------------------------===//
// Class 3 — anonymous cross-cycle capacity (product empty)
//===----------------------------------------------------------------------===//
//
// Multi-cycle FU occupancy without a corresponding dependence. Product
// itineraries use InstrStage cycles==1 only, so the live product set is empty.
// ProductCrossCycleCapacityEnabled pins SMS-HOOK: when false, any stage with
// getCycles()>1 rejects the loop in analyzeLoopForPipelining. Enabling this
// requires an approved shared hook (not this catalog flip alone).
//===----------------------------------------------------------------------===//

/// Product pin: class-3 cross-cycle capacity is not product-enabled.
inline constexpr bool ProductCrossCycleCapacityEnabled = false;

/// Product inventory: no anonymous cross-cycle capacity entries.
inline constexpr unsigned ProductClass3RestrictionCount = 0;

/// Product InstrStage cycle width upper bound (all stages are single-cycle).
inline constexpr unsigned ProductMaxInstrStageCycles = 1;

/// Draft ARCTAN/SIN_COS multi-cycle (uimm4+2) slot lock is deferred, not
/// product-enabled. Issue-alone (class 1) remains the only live rule.
inline constexpr bool ProductDraftArctanMultiCycleLockEnabled = false;

/// Published SIN_COS/ARCTAN dest OperandCycles (uimm4_max+2). Not scoreboard
/// depth while the class-3 lock is off — clamp to issue-cycle ALU latency.
inline constexpr unsigned SinCosScaffoldDataLatency = 17;

/// Data latency that may size the HR scoreboard or stall auditor.
inline constexpr unsigned clampPublishedDataLatency(unsigned Lat) {
  if (!ProductDraftArctanMultiCycleLockEnabled &&
      Lat >= SinCosScaffoldDataLatency)
    return 1;
  return Lat;
}

/// Operand-dependent format predicates beyond MCInstrDesc are not product-
/// enabled either; SMS-HOOK keeps a fail-closed hook for when they land.
inline constexpr bool ProductOperandDependentFormatPredicateEnabled = false;

//===----------------------------------------------------------------------===//
// Freeze assertions — silent reclassification must fail the build
//===----------------------------------------------------------------------===//

static_assert(static_cast<unsigned>(SMSRestrictionClass::IssueCycleFormatCapacity) ==
                  1,
              "class 1 is issue-cycle format/capacity");
static_assert(static_cast<unsigned>(SMSRestrictionClass::DataOrderRecurrence) ==
                  2,
              "class 2 is data/order/recurrence");
static_assert(static_cast<unsigned>(
                  SMSRestrictionClass::AnonymousCrossCycleCapacity) == 3,
              "class 3 is anonymous cross-cycle capacity");

static_assert(PackLegalityRuleCount == 7,
              "PackLegality product rules 1–7 are class-1 inventory");
static_assert(isClass1PackLegalityRule(1) && isClass1PackLegalityRule(7),
              "rules 1 and 7 are class 1");
static_assert(!isClass1PackLegalityRule(0) && !isClass1PackLegalityRule(8),
              "rule numbers outside 1–7 are not class-1 PackLegality");
static_assert(Class1MaxIssuePerCycle == 3,
              "class-1 issue cap is three ops per cycle");
static_assert(Class1ArctanSinCosIssueAloneOnly,
              "ARCTAN/SIN_COS are class-1 issue-alone only");
static_assert(Class1OwnedByResourceCycle && Class1OwnedByPackLegality &&
                  Class1OwnedByHazardRecognizer,
              "class 1 is owned by ResourceCycle / PackLegality / HR");

static_assert(Class2OwnedByGenericDDG && Class2OwnedByItineraryOperandLatency &&
                  Class2OwnedBySMSMutations,
              "class 2 is owned by DDG / latency / SMS mutations");
static_assert(Class2MutationsAreNotCrossCycleCapacity,
              "SMS mutations must not fake class-3 capacity");

static_assert(!ProductCrossCycleCapacityEnabled,
              "product class-3 cross-cycle capacity is disabled");
static_assert(ProductClass3RestrictionCount == 0,
              "product class-3 inventory is empty");
static_assert(ProductMaxInstrStageCycles == 1,
              "product InstrStage cycles are single-cycle only");
static_assert(!ProductDraftArctanMultiCycleLockEnabled,
              "draft ARCTAN multi-cycle lock is not product-enabled");
static_assert(SinCosScaffoldDataLatency == 17,
              "SinCos scaffold dest latency is uimm4_max+2");
static_assert(clampPublishedDataLatency(17) == 1,
              "SinCos scaffold does not size the product scoreboard");
static_assert(clampPublishedDataLatency(2) == 2,
              "load Data_Latency 2 is unchanged");
static_assert(!ProductOperandDependentFormatPredicateEnabled,
              "operand-dependent format predicates are not product-enabled");

/// Classify a multi-cycle itinerary stage under the product pin.
/// Returns class 3 when cycles>1 (unsupported without ProductCrossCycle
/// capacity enable). Single-cycle stages are class-1 issue capacity.
inline constexpr SMSRestrictionClass
classifyInstrStageCycles(unsigned Cycles) {
  return Cycles > ProductMaxInstrStageCycles
             ? SMSRestrictionClass::AnonymousCrossCycleCapacity
             : SMSRestrictionClass::IssueCycleFormatCapacity;
}

/// True when SMS-HOOK must reject a multi-cycle stage under product law.
inline constexpr bool
smsHookRejectsMultiCycleStage(unsigned Cycles) {
  return Cycles > ProductMaxInstrStageCycles &&
         !ProductCrossCycleCapacityEnabled;
}

//===----------------------------------------------------------------------===//
// II-wrap false-accept model (SMS issue-time-only ResourceCycle boundary)
//===----------------------------------------------------------------------===//
//
// MachinePipeliner ResourceManager keeps one independent ResourceCycle per
// modulo phase and only calls canReserve/reserve on the issue phase. Multi-
// cycle FU occupancy that spans (Issue + k) % II for k in [0, StageCycles) is
// invisible to that adapter: two ops can both "succeed" on different phases
// even when their multi-cycle spans collide after II wrap. That is issue-time-
// only *false acceptance*.
//
// Product law keeps ProductCrossCycleCapacityEnabled=false and product
// InstrStage cycles==1, so live product never exercises multi-cycle wrap.
// SMS-HOOK still fail-closes any multi-cycle stage (and a dedicated bisect
// force path) so product SMS cannot accept a body that would rely on the
// missing II-wrap checker. Positive multi-cycle product support needs an
// approved shared hook — not a catalog-only flip.
//===----------------------------------------------------------------------===//

/// True when multi-cycle occupancy issued at \p IssuePhase for \p StageCycles
/// under initiation interval \p II covers modulo phase \p QueryPhase.
/// Models (IssuePhase + k) % II for k in [0, StageCycles). II==0 is empty.
inline constexpr bool smsIIWrapOccupiesPhase(unsigned IssuePhase,
                                            unsigned StageCycles, unsigned II,
                                            unsigned QueryPhase) {
  if (II == 0 || StageCycles == 0)
    return false;
  const unsigned Issue = IssuePhase % II;
  const unsigned Query = QueryPhase % II;
  for (unsigned K = 0; K < StageCycles; ++K) {
    if ((Issue + K) % II == Query)
      return true;
  }
  return false;
}

/// True when multi-cycle occupancy under \p II covers at least one phase
/// other than the issue phase (or self-wraps when StageCycles > II). Single-
/// cycle stages never leave the issue phase, so issue-time-only booking is
/// exact for them.
inline constexpr bool smsIIWrapSpansBeyondIssuePhase(unsigned StageCycles,
                                                    unsigned II) {
  if (II == 0 || StageCycles <= ProductMaxInstrStageCycles)
    return false;
  // StageCycles > 1 always claims (Issue+1)%II as well as Issue — either a
  // distinct phase (II > 1) or a self-wrap collision (II == 1, StageCycles>1).
  return StageCycles > 1;
}

/// True when a multi-cycle stage self-conflicts under II (StageCycles > II):
/// the same phase is claimed twice by one instruction's wrap.
inline constexpr bool smsIIWrapSelfConflicts(unsigned StageCycles,
                                             unsigned II) {
  return II > 0 && StageCycles > II;
}

/// SMS-HOOK polarity for the II-wrap false-accept hazard: multi-cycle stage
/// must reject under product law whenever II-wrap would span beyond issue.
/// II is a schedule parameter; the catalog rejects on StageCycles alone when
/// product class-3 is disabled (any II that could schedule the op is unsafe
/// without an approved cross-cycle representation).
inline constexpr bool smsHookRejectsIIWrapFalseAccept(unsigned StageCycles,
                                                     unsigned II) {
  return smsHookRejectsMultiCycleStage(StageCycles) &&
         smsIIWrapSpansBeyondIssuePhase(StageCycles, II);
}

static_assert(classifyInstrStageCycles(1) ==
                  SMSRestrictionClass::IssueCycleFormatCapacity,
              "single-cycle stage is class 1");
static_assert(classifyInstrStageCycles(2) ==
                  SMSRestrictionClass::AnonymousCrossCycleCapacity,
              "multi-cycle stage is class 3");
static_assert(!smsHookRejectsMultiCycleStage(1),
              "single-cycle stages are not SMS-HOOK multi-cycle rejects");
static_assert(smsHookRejectsMultiCycleStage(2),
              "multi-cycle stages reject while product class-3 is disabled");

// II-wrap pure pins (compile-time). Classic false-accept: StageCycles=2, II=2
// issued at phase 0 occupies {0,1}; a second capacity-1 op at phase 1 collides
// under wrap but issue-time-only ResourceCycles accept both independently.
static_assert(smsIIWrapOccupiesPhase(/*Issue=*/0, /*Stage=*/2, /*II=*/2,
                                     /*Query=*/0),
              "II-wrap stage covers issue phase");
static_assert(smsIIWrapOccupiesPhase(0, 2, 2, 1),
              "II-wrap stage covers next phase under II=2");
static_assert(!smsIIWrapOccupiesPhase(0, 1, 2, 1),
              "single-cycle stage does not cover neighbor phase");
static_assert(smsIIWrapSpansBeyondIssuePhase(2, 2),
              "multi-cycle stage spans beyond issue under II=2");
static_assert(!smsIIWrapSpansBeyondIssuePhase(1, 2),
              "single-cycle stage stays on issue phase");
static_assert(smsIIWrapSelfConflicts(3, 2),
              "StageCycles > II self-conflicts on wrap");
static_assert(!smsIIWrapSelfConflicts(2, 2),
              "StageCycles == II is exact cover, not self-conflict");
static_assert(smsHookRejectsIIWrapFalseAccept(2, 2),
              "SMS-HOOK rejects II-wrap false-accept while class-3 is disabled");
static_assert(!smsHookRejectsIIWrapFalseAccept(1, 2),
              "single-cycle stages are not II-wrap false-accept rejects");

} // namespace restriction
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCERESTRICTIONCLASSES_H
