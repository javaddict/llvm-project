//===- HaydnLaneStoreFoldTest.cpp - lane-store imm law -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// Dedicated unit seal for the CB-160 lane-store immediate law
// (haydn::postselect::laneStoreImmForWordScaledOffset):
//
// The golden S_SW (ST32) and D_SW_L/H store families — the plain WITH_IMM
// form the fold targets and the PRE/POST writeback forms sharing the same
// field — encode ONE word-scaled EA law: EA = rs + (imm6 << 2). A selected
// ST32 offset operand is already that word-scaled imm, so folding
// MOVE32_DR_L/H + ST32 into D_SW_L/H_WITH_IMM must pass it through
// UNCHANGED (identity), not rescale it. The pre-fix fold divided by 4:
// `ST32 %v, %y16, -4` (EA y+0, the first DR-pair store of each
// fft_stage_inner_DFT4_16x16_ie iteration) became `d_sw_l_with_imm -1`
// (EA y+12) — y[0] never stored, y[12] clobbered, at -O1/-O2/-O3 only
// (the pass runs O1+; -O0 was correct).
//
// The pass-level shape (fold firing end-to-end with imm out == imm in) is
// pinned by llvm/test/CodeGen/Haydn/cb160-lane-store-fold-word-scaled-imm.mir.
//
//===----------------------------------------------------------------------===//

#include "HaydnPostSelectOptimize.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

TEST(HaydnLaneStoreFoldTest, PassThroughIdentityInSignedImm6) {
  // The law is an identity: the D_SW immediate equals the already
  // word-scaled ST32 offset, for every in-range value.
  for (int64_t Off = -32; Off <= 31; ++Off) {
    int64_t ScaledImm = 0xdeadbeef;
    ASSERT_TRUE(haydn::postselect::laneStoreImmForWordScaledOffset(
        Off, ScaledImm))
        << "in-range word offset " << Off << " must fold";
    EXPECT_EQ(ScaledImm, Off) << "imm must pass through unscaled";
  }
}

// The exact CB-160 boundary: the first DR-pair store of each loop
// iteration lowered to ST32 %v, %y16, -4 (EA y+0). The pre-fix /4 rescale
// mapped it to d_sw_l_with_imm -1 (EA y+12) — a silent 16-byte EA move.
TEST(HaydnLaneStoreFoldTest, Cb160BoundaryMinusFourStaysMinusFour) {
  int64_t ScaledImm = 0;
  ASSERT_TRUE(
      haydn::postselect::laneStoreImmForWordScaledOffset(-4, ScaledImm));
  EXPECT_EQ(ScaledImm, -4);
}

// Pair-coverage sweep across a rotated pre-incremented base: offsets
// -4..+3 are the eight lanes of one DFT4 iteration off base = y+16.
TEST(HaydnLaneStoreFoldTest, FullPairCoverageAroundRotatedBase) {
  for (int64_t Off = -4; Off <= 3; ++Off) {
    int64_t ScaledImm = 0;
    ASSERT_TRUE(
        haydn::postselect::laneStoreImmForWordScaledOffset(Off, ScaledImm));
    EXPECT_EQ(ScaledImm, Off);
  }
}

// Range gate: outside signed imm6 the fold must fail closed (returns
// false, no output written) so the MOVE32 + ST32 pair survives.
TEST(HaydnLaneStoreFoldTest, OutOfRangeFailsClosed) {
  int64_t ScaledImm = 0xdeadbeef;
  EXPECT_FALSE(
      haydn::postselect::laneStoreImmForWordScaledOffset(-33, ScaledImm));
  EXPECT_FALSE(
      haydn::postselect::laneStoreImmForWordScaledOffset(32, ScaledImm));
  EXPECT_FALSE(haydn::postselect::laneStoreImmForWordScaledOffset(
      1LL << 40, ScaledImm));
  // Fail-closed leaves the out-param untouched.
  EXPECT_EQ(ScaledImm, 0xdeadbeef);
}

// Boundary saturation: the extreme encodable word offsets.
TEST(HaydnLaneStoreFoldTest, SignedImm6Boundaries) {
  int64_t ScaledImm = 0;
  ASSERT_TRUE(
      haydn::postselect::laneStoreImmForWordScaledOffset(-32, ScaledImm));
  EXPECT_EQ(ScaledImm, -32);
  ASSERT_TRUE(
      haydn::postselect::laneStoreImmForWordScaledOffset(31, ScaledImm));
  EXPECT_EQ(ScaledImm, 31);
}

} // namespace
