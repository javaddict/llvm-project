//===- HaydnHWLoopContractsTest.cpp - setup/body geometry freeze -*- C++ -*-===
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Dedicated unit seal for HaydnHWLoopContracts.h:
//   * SetupIssueDistance / InterveningCycles named pair (3 / 2)
//   * MinSetupBundles aliases InterveningCycles (not distance)
//   * MinSetupBytes = InterveningCycles × productParcelBytes (no absolute
//     parcel-size freeze; value follows generated EncodedBytes)
//   * Body ≥ MinBodyBundles, strict END > BEGIN, COUNT ≥ MinCount
//   * MaxSingleBranchGrowthBytes = parcels × productParcelBytes
//
// Arithmetic must not drift silently: formation, Fixup, mutations, and lit
// all consume these constants. ExitSU forward edge latency is
// SetupIssueDistance so leaveRegion handleRegionConflicts ExitReady /
// inter-zone pads yield InterveningCycles following cycles after SET under
// top-down and dual-zone merge; bot-up reverse edge is distance-1.
//
//===----------------------------------------------------------------------===//

#include "HaydnBundlePlan.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"
#include "gtest/gtest.h"

// Haydn::SET_HWLOOP opcode and Haydn::SFR register enums from tablegen.
#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"
#define GET_REGINFO_ENUM
#include "HaydnGenRegisterInfo.inc"

using namespace llvm;
using namespace llvm::haydn::bundle;
using namespace llvm::haydn::hwloop;

namespace {

TEST(HaydnHWLoopContractsTest, SetupArithmeticNamedPairFrozen) {
  // freeze: do not re-hide behind ambiguous "t-3".
  EXPECT_EQ(SetupIssueDistance, 3u);
  EXPECT_EQ(InterveningCycles, 2u);
  EXPECT_EQ(InterveningCycles + 1u, SetupIssueDistance);
  EXPECT_EQ(MinSetupBundles, InterveningCycles);
  EXPECT_NE(MinSetupBundles, SetupIssueDistance)
      << "MinSetupBundles must alias InterveningCycles, not distance";
}

// ExitSU artificial latency must be SetupIssueDistance: SET at cycle 0 and
// leaveRegion flush to ExitSU.TopReadyCycle produces bundles [0..D) with
// D-1 following empties = InterveningCycles. Using InterveningCycles as the
// edge would under-pad by one.
TEST(HaydnHWLoopContractsTest, ExitSULatencyIsSetupIssueDistance) {
  EXPECT_EQ(SetupIssueDistance, InterveningCycles + 1u);
  // Alone-SET region: total cycles after flush = SetupIssueDistance,
  // following empty cycles = InterveningCycles.
  EXPECT_EQ(SetupIssueDistance - 1u, InterveningCycles);
}

// Dual-zone leaveRegion handleRegionConflicts ExitReady arithmetic.
// TopFinal = TopCurr + BotCurr; when ExitReady > TopFinal the Top zone bumps
// to ExitReady - BotCurr so the merged region still ends at ExitReady.
// Inter-zone scoreboard pads only raise Top further (Following never shrinks).
// Bot-up reverse edge is SetupIssueDistance-1 (AIE same-cycle-as-Exit).
TEST(HaydnHWLoopContractsTest, DualZoneExitReadyPadCoversIntervening) {
  const unsigned D = SetupIssueDistance;
  const unsigned FollowingFloor = InterveningCycles;
  EXPECT_EQ(D, FollowingFloor + 1u);

  // Top-only: SET emitted at cycle 0, TopCurr advances to 1, BotCurr = 0,
  // ExitReady = 0 + D. Bump Top to D → total D cycles, following D-1.
  {
    const unsigned TopCurr = 1;
    const unsigned BotCurr = 0;
    const unsigned ExitReady = D;
    const unsigned TopFinal = TopCurr + BotCurr;
    unsigned NewTop = TopCurr;
    if (ExitReady > TopFinal)
      NewTop = ExitReady - BotCurr;
    EXPECT_EQ(NewTop, D);
    EXPECT_EQ(NewTop + BotCurr, D);
    EXPECT_EQ(NewTop + BotCurr - 1u, FollowingFloor);
  }

  // Dual-zone: SET on Top (TopCurr=1), BotCurr = FollowingFloor after the seam,
  // ExitReady = D. TopFinal = 1 + FollowingFloor == D → no bump; Following
  // after SET = (NewTop - 1) + BotCurr = FollowingFloor.
  {
    const unsigned TopCurr = 1;
    const unsigned BotCurr = FollowingFloor;
    const unsigned ExitReady = D;
    const unsigned TopFinal = TopCurr + BotCurr;
    EXPECT_EQ(TopFinal, ExitReady);
    unsigned NewTop = TopCurr;
    if (ExitReady > TopFinal)
      NewTop = ExitReady - BotCurr;
    EXPECT_EQ(NewTop, 1u);
    EXPECT_EQ(NewTop + BotCurr, D);
    const unsigned FollowingAfterSet = (NewTop - 1u) + BotCurr;
    EXPECT_EQ(FollowingAfterSet, FollowingFloor);
  }

  // Bot-only critical path: ExitReady stays 0 (nothing top-scheduled) while
  // reverse edge MinGap-1 forces BotCurr >= D when SET is critical →
  // following after earliest SET issue is still FollowingFloor.
  {
    const unsigned BotCurr = D;
    const unsigned ReverseEdge = D - 1u;
    EXPECT_EQ(ReverseEdge, FollowingFloor);
    EXPECT_EQ(BotCurr - 1u, FollowingFloor);
  }
}

// W61 tail credit (AIE RegionEndEdges LoopSetupDistance - ZOLBundlesCount
// analog): the SET-MBB tail — size-bearing parcels from region end through
// the first terminator — lies between the SET cycle and HWLR_BEGIN on every
// activation path, so the in-region ExitSU edge only owes the remainder.
TEST(HaydnHWLoopContractsTest, SetupGapAfterTailCreditArithmetic) {
  EXPECT_EQ(setupGapAfterTailCredit(0), SetupIssueDistance);
  // One tail parcel (explicit B to header) credits one cycle.
  EXPECT_EQ(setupGapAfterTailCredit(1), SetupIssueDistance - 1u);
  EXPECT_EQ(setupGapAfterTailCredit(InterveningCycles), 1u);
  // Tail covers the whole distance: region owes nothing.
  EXPECT_EQ(setupGapAfterTailCredit(SetupIssueDistance), 0u);
  // Clamp: never under-reserves, never wraps.
  EXPECT_EQ(setupGapAfterTailCredit(10u), 0u);
  // Monotone non-increasing in the credit.
  for (unsigned T = 0; T < 8u; ++T)
    EXPECT_LE(setupGapAfterTailCredit(T + 1u), setupGapAfterTailCredit(T));
}

TEST(HaydnHWLoopContractsTest, MinSetupBytesFromProductParcel) {
  // Timing law is the cycle pair; bytes follow product EncodedBytes only.
  EXPECT_EQ(static_cast<unsigned>(ProductParcelBytes),
            productParcelBytes().Value);
  EXPECT_EQ(MinSetupBytes, productBundlesToBytes(InterveningCycles));
  EXPECT_EQ(MinSetupBytes,
            static_cast<int64_t>(InterveningCycles) * ProductParcelBytes);
  EXPECT_EQ(MinSetupIssueBytes, productBundlesToBytes(SetupIssueDistance));
  EXPECT_EQ(MinSetupIssueBytes, MinSetupBytes + ProductParcelBytes);
  // Intervening span is not the full SET→BEGIN PC delta.
  EXPECT_NE(MinSetupBytes, productBundlesToBytes(SetupIssueDistance));
}

TEST(HaydnHWLoopContractsTest, OffsetFieldLimitsAndSafetyMargin) {
  EXPECT_EQ(Offset1Bits, 6u);
  EXPECT_EQ(Offset2Bits, 12u);
  EXPECT_EQ(MaxStartOffsetBytes, 252);
  EXPECT_EQ(MaxEndOffsetBytes, 16380);
  EXPECT_EQ(Off1SafetyMarginBundles, 3);
  EXPECT_EQ(Off1SafetyMarginBytes, productBundlesToBytes(3));
  EXPECT_EQ(Off1SafetyMarginBytes, 3 * ProductParcelBytes);
  EXPECT_EQ(MaxStartOffsetBytesSafe, MaxStartOffsetBytes - Off1SafetyMarginBytes);
}

// Late-layout stable row: Fixup charges MaxSingleBranchGrowthBytes per
// still-relaxable short branch against residual Off margins so the second
// BranchRelaxation cannot invalidate a hardware-form acceptance.
TEST(HaydnHWLoopContractsTest, BranchRelaxSafetyBufferIsGrowthBudget) {
  EXPECT_EQ(BranchRelaxSafetyBufferBytes, MaxSingleBranchGrowthBytes);
  EXPECT_EQ(BranchRelaxSafetyBufferBytes,
            productBundlesToBytes(MaxSingleBranchGrowthParcels));
}

TEST(HaydnHWLoopContractsTest, SecondBRGrowthBudgetFromProductParcel) {
  EXPECT_EQ(MaxSingleBranchGrowthParcels, 4u);
  EXPECT_EQ(MaxSingleBranchGrowthBytes,
            productBundlesToBytes(MaxSingleBranchGrowthParcels));
  EXPECT_EQ(MaxSingleBranchGrowthBytes,
            static_cast<int64_t>(MaxSingleBranchGrowthParcels) *
                ProductParcelBytes);
  // With product EncodedBytes=12, three spacer parcels leave a large enough
  // residual Off1 margin that 3×growth no longer demotes. Keep the dual pin
  // (demote vs keep) by using enough intervening product parcels that
  // 3×MaxSingleBranchGrowthBytes exceeds residual margin while one site fits
  // (same law as hwloop-fixup-second-br-growth.mir).
  // 3×growth > MaxStart − (MinSetup + K×parcel) → K ≥ 8 under EncodedBytes=12.
  const int64_t SpacerParcels = 8;
  const int64_t StartOff = MinSetupBytes + SpacerParcels * ProductParcelBytes;
  const int64_t BeginMargin = MaxStartOffsetBytes - StartOff;
  EXPECT_GT(3 * MaxSingleBranchGrowthBytes, BeginMargin);
  // One site fits residual margin → keep hardware form.
  EXPECT_LE(MaxSingleBranchGrowthBytes, BeginMargin);
}

TEST(HaydnHWLoopContractsTest, BodyEndCountProductLaw) {
  EXPECT_EQ(MinBodyBundles, 3u);
  EXPECT_EQ(MinCount, 1u);
  EXPECT_EQ(MinBodySpanBytes,
            productBundlesToBytes(MinBodyBundles - 1));
  EXPECT_EQ(MinBodySpanBytes, 2 * ProductParcelBytes);

  // body=3: END at BEGIN + 2 parcels → parcels inclusive = 3.
  const int64_t Begin = MinSetupBytes;
  const int64_t EndMin = Begin + MinBodySpanBytes;
  EXPECT_TRUE(bodyMeetsMinLaw(Begin, EndMin));
  EXPECT_EQ(bodyParcelsFromOffsets(Begin, EndMin), MinBodyBundles);

  // Strict END > BEGIN: equal offsets illegal.
  EXPECT_FALSE(bodyMeetsMinLaw(Begin, Begin));
  // body=2: END at BEGIN + 1 parcel → fails min body.
  EXPECT_FALSE(bodyMeetsMinLaw(Begin, Begin + ProductParcelBytes));
  EXPECT_EQ(bodyParcelsFromOffsets(Begin, Begin + ProductParcelBytes), 2u);
}

TEST(HaydnHWLoopContractsTest, ProductParcelMatchesGeneratedVLIW) {
  HaydnMCFormats Fmts;
  auto FromPackets = productEncodedBytesFromPackets(Fmts.getPacketFormats());
  ASSERT_TRUE(FromPackets.has_value());
  EXPECT_EQ(static_cast<unsigned>(ProductParcelBytes), FromPackets->Value);
  EXPECT_EQ(productParcelBytes(), *FromPackets);
}

// REGRESSION (GOALS W46 / audit G-1): the sel constants were once INVERTED
// vs the golden Reference Manual ("for nested loops, HWLR_*[0] is used for
// the inner loop, and HWLR_*[1] for the outer loop") — Innermost=1/Outer=0.
// Harmless only while nesting is unimplemented (single-BB Role-A expand was
// the sole consumer and any in-domain sel programmed the same loop). The day
// nesting lands, the inversion becomes wrong-loop-writes-wrong-CSR. This pin
// freezes the golden convention by VALUE, not just domain membership.
TEST(HaydnHWLoopContractsTest, SelectorConventionMatchesGoldenManual) {
  EXPECT_EQ(InnermostProductSelector, 0);
  EXPECT_EQ(OuterProductSelector, 1);
  EXPECT_NE(InnermostProductSelector, OuterProductSelector);
  EXPECT_TRUE(isProductSelector(InnermostProductSelector));
  EXPECT_TRUE(isProductSelector(OuterProductSelector));
}

} // namespace

namespace {
using namespace llvm;
// REGRESSION (GOALS W47 / audit PA2-G2): the golden Constraints same-sel
// two-SETs-per-bundle ban must ride ONE mechanism — the SFR-space WAW the
// HR already enforces ("one SFR writer per cycle") — for BOTH the real _W
// forms and the residual SET_HWLOOP{,_REG} pseudos. The residual pseudos
// once carried NO Defs and relied only on hasSideEffects=1's DAG Ord
// barrier (topology, not a predicate): flipping hasSideEffects to 0
// without Defs would have silently made two same-sel residual SETs
// coissuable (both write HWLR_*[sel]; the second clobbers the first).
// This descriptor-level pin makes the td law itself, independent of any
// scheduler's barrier behavior.
class HaydnHWLoopSetDefsTest : public testing::Test {
protected:
  std::unique_ptr<HaydnTargetMachine> TM;
  std::unique_ptr<HaydnSubtarget> ST;

  static void SetUpTestSuite() {
    LLVMInitializeHaydnTargetInfo();
    LLVMInitializeHaydnTarget();
    LLVMInitializeHaydnTargetMC();
  }

  void SetUp() override {
    std::string Error;
    Triple TT("haydn-unknown-elf");
    const Target *TheTarget = TargetRegistry::lookupTarget(TT, Error);
    ASSERT_NE(TheTarget, nullptr) << Error;
    TargetOptions Options;
    TM.reset(static_cast<HaydnTargetMachine *>(TheTarget->createTargetMachine(
        TT, "generic", "", Options, std::nullopt, std::nullopt,
        CodeGenOptLevel::Default)));
    ASSERT_NE(TM, nullptr);
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    ASSERT_NE(ST, nullptr);
  }

  const HaydnInstrInfo &TII() const { return *ST->getInstrInfo(); }
};

TEST_F(HaydnHWLoopSetDefsTest, SetVariantsAllDefineSfrSymmetrically) {
  for (unsigned Opc :
       {Haydn::SET_HWLOOP, Haydn::SET_HWLOOP_REG, Haydn::SET_HWLOOP_W,
        Haydn::SET_HWLOOP_F2_W, Haydn::SET_HWLOOP_REG_W}) {
    const MCInstrDesc &D = TII().get(Opc);
    EXPECT_TRUE(is_contained(D.implicit_defs(), Haydn::SFR))
        << TII().getName(Opc) << " must carry td Defs=[SFR] (W47)";
  }
}
} // namespace
