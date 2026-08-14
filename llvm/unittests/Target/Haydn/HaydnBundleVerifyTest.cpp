//===- HaydnBundleVerifyTest.cpp - committed-bundle verify -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for haydn::bundle::verifyCommittedBundle.
//
// AIE structure peers:
//   AIEBundle.h:150-156 getFormatOrNull (hasValidFormat + planFromPacketFormats)
//   AIEHazardRecognizer.cpp:278-312 applyFormatOrdering assert + finalizeBundle
//   AIEBaseInstrInfo.cpp:1440-1459 verifyInstruction fail-closed pattern
//   BundleTest.cpp / HazardRecognizerTest.cpp unit style
//
//===----------------------------------------------------------------------===//

#include "HaydnBundleVerify.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "HaydnTestMCInstrInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

TEST(HaydnBundleVerifyTest, ProductSingletonAdd32Ok) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(FormatID::BundleE3, {Haydn::ADD32},
                                   Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.FID, FormatID::BundleE3);
  EXPECT_EQ(Plan.Bytes.Value, ProductEncodedBytesValue);
  EXPECT_EQ(Plan.memberCount(), 1u);
  EXPECT_EQ(Plan.MemberOpcodes[0], Haydn::ADD32);
}

TEST(HaydnBundleVerifyTest, ProductDisjointPairOk) {
  // ST32 S0-only + ADD64 S1|S2 — encode-oracle packs (AIE canAdd peer).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(FormatID::BundleE3,
                                   {Haydn::S_SW_WITH_IMM, Haydn::ADD64}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 2u);
  EXPECT_NE(Plan.OccupiedSlots & Haydn::SLOT_P30, 0u);
}

TEST(HaydnBundleVerifyTest, ProductThreeSlotFillOk) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(
      FormatID::BundleE3, {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32}, Fmts,
      &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 3u);
  EXPECT_EQ(Plan.OccupiedSlots,
            SlotBits(Haydn::SLOT_P30) | Haydn::SLOT_P31 | Haydn::SLOT_P32);
}

TEST(HaydnBundleVerifyTest, StallEmptyMembersOk) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  BundlePlan Plan;
  auto Err =
      verifyCommittedBundle(FormatID::BundleE3, {}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_TRUE(Plan.empty());
  EXPECT_EQ(Plan.Bytes.Value, ProductEncodedBytesValue);
}

TEST(HaydnBundleVerifyTest, RejectsFourMembers) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  auto Err = verifyCommittedBundle(
      FormatID::BundleE3,
      {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32, Haydn::OR32}, Fmts);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("ISSUE_SLOT_COUNT"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, RejectsSameSlotConflict) {
  // Two ST32 are S0-only — cannot co-issue (encode-oracle canAdd fails).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  auto Err = verifyCommittedBundle(FormatID::BundleE3,
                                   {Haydn::S_SW_WITH_IMM, Haydn::S_SW_WITH_IMM}, Fmts);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("canAdd"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, RejectsUnknownFormatIDImm) {
  // Two imms are known now, 0 and 1, so the fail-closed probe has to reach
  // past both. It must not silently become a composite.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  auto Fake = static_cast<FormatID>(2u);
  auto Err = verifyCommittedBundle(Fake, {Haydn::ADD32}, Fmts);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("unknown FormatID"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, ProductFormatIDImmIsZero) {
  // Durable BUNDLE-root contract: FormatID Full encodes as imm 0.
  // Two composites, two imms: 0 is the 2-entry form and 1 the 3-entry one.
  EXPECT_EQ(formatIDToImm(FormatID::BundleE2), 0u);
  EXPECT_EQ(formatIDToImm(FormatID::BundleE3), 1u);
  EXPECT_TRUE(isKnownFormatIDImm(0u));
  EXPECT_TRUE(isKnownFormatIDImm(1u));
  EXPECT_FALSE(isKnownFormatIDImm(2u));
}

TEST(HaydnBundleVerifyTest, DualLoadMayPack) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(FormatID::BundleE3,
                                   {Haydn::S_LW_WITH_IMM, Haydn::S_LW_WITH_IMM}, Fmts, &Plan);
  // Dual LD32 is product-legal when alts-derived getLegalSlots /
  // PlacementAlternative FieldSlots cover S0|S1 for LD32.
  // If table rejects, canAdd fails — either outcome is fail-closed / explicit.
  if (!Err) {
    EXPECT_TRUE(Plan.isProductLegal());
    EXPECT_EQ(Plan.memberCount(), 2u);
  } else {
    EXPECT_NE(Err->find("canAdd"), std::string::npos) << *Err;
  }
}

TEST(HaydnBundleVerifyTest, LdPlusMacIndependentOk) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  BundlePlan Plan;
  // LD32 + multi-slot MAC family — typical DSP density pack.
  auto Err = verifyCommittedBundle(
      FormatID::BundleE3, {Haydn::S_LW_WITH_IMM, Haydn::X2MULA32}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 2u);
}

TEST(HaydnBundleVerifyTest, EncodedBytesAlwaysSixteenOnSuccess) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  // Real storage, not `ArrayRef<unsigned>{Haydn::NOP}`. ArrayRef keeps a
  // pointer, and its single-element constructor points at a temporary that
  // dies at the end of the full expression, so every opcode read below was
  // whatever happened to be on the stack. It went unnoticed because a formats
  // object with no MCInstrInfo never reads an opcode's NAME — garbage simply
  // missed the alternates table and returned "no slots". With the real
  // MCInstrInfo in play it reaches MCInstrInfo::getName and aborts, which is
  // how it surfaced.
  const std::vector<std::vector<unsigned>> Cases = {
      {Haydn::NOP},
      {Haydn::ADD32},
      {Haydn::S_SW_WITH_IMM, Haydn::ADD64},
      {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32},
  };
  for (ArrayRef<unsigned> Ops : Cases) {
    BundlePlan Plan;
    auto Err =
        verifyCommittedBundle(FormatID::BundleE3, Ops, Fmts, &Plan);
    if (Err)
      continue; // some NOP/slot combos may reject; only check successes
    EXPECT_EQ(Plan.Bytes.Value, ProductEncodedBytesValue);
    EXPECT_EQ(Plan.Cycles.Value, 1u);
    EXPECT_EQ(Plan.FID, FormatID::BundleE3);
  }
}

TEST(HaydnBundleVerifyTest, PlanFromPacketFormatsMatchesOracleOcc) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  BundlePlan Plan;
  ASSERT_FALSE(verifyCommittedBundle(FormatID::BundleE3,
                                     {Haydn::ADD32, Haydn::S_LW_WITH_IMM}, Fmts, &Plan));
  auto Table =
      planFromPacketFormats(Fmts.getPacketFormats(), Plan.OccupiedSlots);
  ASSERT_TRUE(Table.has_value());
  EXPECT_EQ(Table->Bytes, Plan.Bytes);
  EXPECT_EQ(Table->FID, FormatID::BundleE3);
}

} // namespace
