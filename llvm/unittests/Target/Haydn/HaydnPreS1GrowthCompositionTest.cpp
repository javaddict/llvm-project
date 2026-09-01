//===- HaydnPreS1GrowthCompositionTest.cpp - D1.35 estimate vocabulary --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// D1.35 (path B): the pre-S1 addPreSched2 far-site estimate decides
// "far" from the CURRENT layout walk plus ONE BranchRelaxSafetyBufferBytes
// inflation per direction inside TII.isBranchOffsetInRange. The post-stamp
// growth the estimate cannot see is a typed, admitted vocabulary:
//
//   * one HWLoop demote insertion      MaxHwLoopDemoteGrowthBytes
//   * closure-loop stall regeneration  InterveningCycles parcels per site
//   * idle-parcel alignment pads       ALREADY charged by the walk
//     (padLayoutBytesForMBBAlign — no residual term exists)
//
// PreS1PostStampGrowthBytes composes the two walk-invisible terms. It is
// deliberately NOT charged into the estimate: D1.33's single-inflation
// law makes isBranchOffsetInRange the only seat that adds an allowance,
// and no finite composition can cover eventless S2 redistribution. The
// rejection class this leaves is closed and named at the post-stamp
// LongBranchNormalize seats (in-block promotion, else fail-closed fatal).
//
// These tests pin the composition law only — pure constants, no
// MachineFunction, no dependency on the closure ledger API (that
// vocabulary is owned by HaydnLateConvergenceBudgetTest / D1.41).
#include "HaydnHWLoopContracts.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

using haydn::hwloop::BranchRelaxSafetyBufferBytes;
using haydn::hwloop::InterveningCycles;
using haydn::hwloop::MaxHwLoopDemoteGrowthBytes;
using haydn::hwloop::MaxHwLoopDemoteGrowthParcels;
using haydn::hwloop::MaxSingleBranchGrowthBytes;
using haydn::hwloop::MaxSingleBranchGrowthParcels;
using haydn::hwloop::PreS1PostStampGrowthBytes;
using haydn::hwloop::ProductParcelBytes;

// The composition is exactly the sum of its two typed terms. A future
// demote-vocabulary edit (the MaxHwLoopDemoteGrowthParcels re-enumeration
// class named on the D1.35 row) or a stall-floor edit that does not
// update PreS1PostStampGrowthBytes fails here.
TEST(HaydnPreS1GrowthCompositionTest, CompositionIsExactSumOfTerms) {
  EXPECT_EQ(PreS1PostStampGrowthBytes,
            MaxHwLoopDemoteGrowthBytes +
                haydn::bundle::productBundlesToBytes(InterveningCycles));
  EXPECT_EQ(PreS1PostStampGrowthBytes,
            MaxHwLoopDemoteGrowthBytes +
                static_cast<int64_t>(InterveningCycles) * ProductParcelBytes);
}

// Different-site law: one demote can push a still-relaxable site OTHER
// than the demote's own span out of range, so the composition must
// strictly dominate the estimate's own inflation (one safety buffer)
// plus the same stall floor. If the composition were only
// buffer + stalls, it would model one site growing its OWN span and miss
// the cross-site push the gr27-d135 MIR pin exercises.
TEST(HaydnPreS1GrowthCompositionTest, CompositionDominatesBufferPlusStalls) {
  EXPECT_GT(PreS1PostStampGrowthBytes,
            BranchRelaxSafetyBufferBytes +
                haydn::bundle::productBundlesToBytes(InterveningCycles));
}

// The composition never becomes a second safety-buffer spelling: the BR
// buffer stays exactly one long-form sequence (LUI+ADDI+JALR+pad), and
// the composition stays strictly larger (demote vocabulary + stall floor).
TEST(HaydnPreS1GrowthCompositionTest, BufferIdentityUnchangedByD135) {
  EXPECT_EQ(BranchRelaxSafetyBufferBytes, MaxSingleBranchGrowthBytes);
  EXPECT_EQ(MaxSingleBranchGrowthParcels, 4u);
  EXPECT_EQ(MaxHwLoopDemoteGrowthParcels, 13u);
  EXPECT_GT(PreS1PostStampGrowthBytes, BranchRelaxSafetyBufferBytes);
  EXPECT_GT(PreS1PostStampGrowthBytes, MaxHwLoopDemoteGrowthBytes);
}

// Alignment is NOT a residual composition term: the pre-S1 walk already
// charges the parcel-rounded worst-case entering-MBB pad
// (padLayoutBytesForMBBAlign), and committed MachineAlignment pads are
// size-bearing parcels every byte-distance consumer charges for free.
// Pinned as the law that adding any alignment term here would be a
// double charge (the D1.33 class).
TEST(HaydnPreS1GrowthCompositionTest, AlignmentHasNoResidualTerm) {
  // The composition is exactly demote + stalls: any third term (e.g. a
  // pad) would have to appear in this difference.
  EXPECT_EQ(PreS1PostStampGrowthBytes - MaxHwLoopDemoteGrowthBytes,
            haydn::bundle::productBundlesToBytes(InterveningCycles));
  EXPECT_EQ(InterveningCycles, 2u);
}

} // namespace
