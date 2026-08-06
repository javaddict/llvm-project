//===- HaydnResourceRestrictionClassesTest.cpp - class catalog -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Dedicated unit seal for HaydnResourceRestrictionClasses.h:
//   * Three-way SMS restriction taxonomy (class 1 / 2 / 3) frozen
//   * PackLegality rules 1–7 + ports/slots + CSRW↔SET are class 1
//   * DDG / itinerary latency / SMS mutations own class 2
//   * Product class-3 inventory empty; ProductCrossCycleCapacityEnabled=false
//   * Product InstrStage cycles==1; draft ARCTAN multi-cycle lock deferred
//   * SMS-HOOK multi-cycle reject pin matches the catalog
//   * II-wrap pure phase math + issue-time-only false-accept polarity
//
// Silent reclassification (enabling class-3 capacity, renumbering classes,
// collapsing issue-alone into multi-cycle, inventing class-3 entries) must
// fail this unit. Lit peers: postmisched-arctan-locked-slot.mir,
// sms-format-hook-reject.mir, sms-format-hook-iiwrap-reject.mir,
// sms-format-resmii.mir, postmisched-r0-waw-hazard.mir,
// packetizer-sfr-hazard-regression.ll.
//
//===----------------------------------------------------------------------===//

#include "HaydnPackLegality.h"
#include "HaydnResourceRestrictionClasses.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::haydn::pack;
using namespace llvm::haydn::restriction;

namespace {

TEST(HaydnResourceRestrictionClassesTest, TaxonomyNumbersFrozen) {
  EXPECT_EQ(static_cast<unsigned>(SMSRestrictionClass::IssueCycleFormatCapacity),
            1u);
  EXPECT_EQ(static_cast<unsigned>(SMSRestrictionClass::DataOrderRecurrence), 2u);
  EXPECT_EQ(
      static_cast<unsigned>(SMSRestrictionClass::AnonymousCrossCycleCapacity),
      3u);
}

TEST(HaydnResourceRestrictionClassesTest, Class1PackLegalityRulesOneThroughSeven) {
  EXPECT_EQ(PackLegalityRuleCount, 7u);
  for (unsigned R = 1; R <= PackLegalityRuleCount; ++R)
    EXPECT_TRUE(isClass1PackLegalityRule(R)) << "rule " << R;
  EXPECT_FALSE(isClass1PackLegalityRule(0));
  EXPECT_FALSE(isClass1PackLegalityRule(8));

  EXPECT_EQ(PackRuleDualR0DefsIllegal, 1u);
  EXPECT_EQ(PackRuleDualLiveSameRegWAWIllegal, 2u);
  EXPECT_EQ(PackRuleDualDeadImplicitSfrLegal, 3u);
  EXPECT_EQ(PackRuleArctanSinCosIssueAlone, 4u);
  EXPECT_EQ(PackRuleIssueAndPortBudgets, 5u);
  EXPECT_EQ(PackRulePlacementFieldSlots, 6u);
  EXPECT_EQ(PackRuleCsrwSetHwloopExclusion, 7u);
}

TEST(HaydnResourceRestrictionClassesTest, Class1IssueCapAndOwners) {
  EXPECT_EQ(Class1MaxIssuePerCycle, MaxIssuePerCycle);
  EXPECT_EQ(Class1MaxIssuePerCycle, 3u);
  EXPECT_TRUE(Class1OwnedByResourceCycle);
  EXPECT_TRUE(Class1OwnedByPackLegality);
  EXPECT_TRUE(Class1OwnedByHazardRecognizer);
  EXPECT_TRUE(Class1ArctanSinCosIssueAloneOnly);
}

TEST(HaydnResourceRestrictionClassesTest, Class2DDGLatencyMutationOwners) {
  EXPECT_TRUE(Class2OwnedByGenericDDG);
  EXPECT_TRUE(Class2OwnedByItineraryOperandLatency);
  EXPECT_TRUE(Class2OwnedBySMSMutations);
  EXPECT_TRUE(Class2MutationsAreNotCrossCycleCapacity);
}

TEST(HaydnResourceRestrictionClassesTest, ProductClass3EmptyAndDisabled) {
  EXPECT_FALSE(ProductCrossCycleCapacityEnabled);
  EXPECT_EQ(ProductClass3RestrictionCount, 0u);
  EXPECT_EQ(ProductMaxInstrStageCycles, 1u);
  EXPECT_FALSE(ProductDraftArctanMultiCycleLockEnabled);
  EXPECT_FALSE(ProductOperandDependentFormatPredicateEnabled);
}

TEST(HaydnResourceRestrictionClassesTest, InstrStageClassificationPins) {
  EXPECT_EQ(classifyInstrStageCycles(1),
            SMSRestrictionClass::IssueCycleFormatCapacity);
  EXPECT_EQ(classifyInstrStageCycles(2),
            SMSRestrictionClass::AnonymousCrossCycleCapacity);
  EXPECT_EQ(classifyInstrStageCycles(4),
            SMSRestrictionClass::AnonymousCrossCycleCapacity);

  EXPECT_FALSE(smsHookRejectsMultiCycleStage(1));
  EXPECT_TRUE(smsHookRejectsMultiCycleStage(2));
  EXPECT_TRUE(smsHookRejectsMultiCycleStage(ProductMaxInstrStageCycles + 1));
}

TEST(HaydnResourceRestrictionClassesTest,
     ArctanIssueAloneNotClass3ProductEnable) {
  // Rule 4 is class-1 issue-alone. Enabling draft multi-cycle lock would be a
  // product class-3 change and must not land by flipping only the issue-alone pin.
  EXPECT_TRUE(isClass1PackLegalityRule(PackRuleArctanSinCosIssueAlone));
  EXPECT_TRUE(Class1ArctanSinCosIssueAloneOnly);
  EXPECT_FALSE(ProductDraftArctanMultiCycleLockEnabled);
  EXPECT_FALSE(ProductCrossCycleCapacityEnabled);
}

TEST(HaydnResourceRestrictionClassesTest,
     SilentClass3EnableWouldBreakSmsHookContract) {
  // Product law: multi-cycle stages reject. If ProductCrossCycleCapacityEnabled
  // were true, smsHookRejectsMultiCycleStage would stop rejecting — that flip
  // requires an approved shared hook, not a catalog-only edit. This pin makes
  // the current reject polarity explicit for CI.
  EXPECT_FALSE(ProductCrossCycleCapacityEnabled);
  EXPECT_TRUE(smsHookRejectsMultiCycleStage(2));
  // Empty product class-3 inventory cannot grow without unit update.
  EXPECT_EQ(ProductClass3RestrictionCount, 0u);
}

TEST(HaydnResourceRestrictionClassesTest, IIWrapPhaseMathAndFalseAcceptPolarity) {
  // Classic II-wrap false-accept: StageCycles=2, II=2, issue phase 0 covers
  // phases {0,1}. A capacity-1 op at phase 1 collides under wrap, but
  // issue-time-only ResourceCycle (one independent cycle per phase) cannot see
  // that span — SMS-HOOK must reject multi-cycle stages to fail closed.
  EXPECT_TRUE(smsIIWrapOccupiesPhase(/*Issue=*/0, /*Stage=*/2, /*II=*/2,
                                     /*Query=*/0));
  EXPECT_TRUE(smsIIWrapOccupiesPhase(0, 2, 2, 1));
  EXPECT_FALSE(smsIIWrapOccupiesPhase(0, 1, 2, 1));
  EXPECT_TRUE(smsIIWrapOccupiesPhase(/*Issue=*/1, /*Stage=*/2, /*II=*/2,
                                     /*Query=*/0)); // wrap: 1, then 0

  EXPECT_TRUE(smsIIWrapSpansBeyondIssuePhase(2, 2));
  EXPECT_TRUE(smsIIWrapSpansBeyondIssuePhase(2, 1)); // self-wrap under II=1
  EXPECT_FALSE(smsIIWrapSpansBeyondIssuePhase(1, 2));
  EXPECT_FALSE(smsIIWrapSpansBeyondIssuePhase(2, 0)); // II==0 empty

  EXPECT_TRUE(smsIIWrapSelfConflicts(/*Stage=*/3, /*II=*/2));
  EXPECT_FALSE(smsIIWrapSelfConflicts(2, 2));
  EXPECT_FALSE(smsIIWrapSelfConflicts(1, 2));

  EXPECT_TRUE(smsHookRejectsIIWrapFalseAccept(2, 2));
  EXPECT_TRUE(smsHookRejectsIIWrapFalseAccept(4, 3));
  EXPECT_FALSE(smsHookRejectsIIWrapFalseAccept(1, 2));
  // Polarity is class-3 disabled + multi-cycle; single-cycle never rejects.
  EXPECT_FALSE(ProductCrossCycleCapacityEnabled);
  EXPECT_TRUE(smsHookRejectsMultiCycleStage(2));
}

} // namespace
