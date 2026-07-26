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
#include "MCTargetDesc/HaydnMCFormats.h"
#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

TEST(HaydnBundleVerifyTest, ProductSingletonAdd32Ok) {
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(FormatID::Bundle128Full, {Haydn::ADD32},
                                   Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.FID, FormatID::Bundle128Full);
  EXPECT_EQ(Plan.Bytes.Value, 16u);
  EXPECT_EQ(Plan.memberCount(), 1u);
  EXPECT_EQ(Plan.MemberOpcodes[0], Haydn::ADD32);
}

TEST(HaydnBundleVerifyTest, ProductDisjointPairOk) {
  // ST32 S0-only + ADD64 S1|S2 — encode-oracle packs (AIE canAdd peer).
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(FormatID::Bundle128Full,
                                   {Haydn::ST32, Haydn::ADD64}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 2u);
  EXPECT_NE(Plan.OccupiedSlots & Haydn::SLOT0, 0u);
}

TEST(HaydnBundleVerifyTest, ProductThreeSlotFillOk) {
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(
      FormatID::Bundle128Full, {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32}, Fmts,
      &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 3u);
  EXPECT_EQ(Plan.OccupiedSlots,
            SlotBits(Haydn::SLOT0) | Haydn::SLOT1 | Haydn::SLOT2);
}

TEST(HaydnBundleVerifyTest, StallEmptyMembersOk) {
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err =
      verifyCommittedBundle(FormatID::Bundle128Full, {}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_TRUE(Plan.empty());
  EXPECT_EQ(Plan.Bytes.Value, 16u);
}

TEST(HaydnBundleVerifyTest, RejectsFourMembers) {
  HaydnMCFormats Fmts;
  auto Err = verifyCommittedBundle(
      FormatID::Bundle128Full,
      {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32, Haydn::OR32}, Fmts);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("ISSUE_SLOT_COUNT"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, RejectsSameSlotConflict) {
  // Two ST32 are S0-only — cannot co-issue (encode-oracle canAdd fails).
  HaydnMCFormats Fmts;
  auto Err = verifyCommittedBundle(FormatID::Bundle128Full,
                                   {Haydn::ST32, Haydn::ST32}, Fmts);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("canAdd"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, RejectsUnknownFormatIDImm) {
  // N-format-ready: only imm 0 (Bundle128Full) is known. Cast an unknown
  // value past the enum to exercise the fail-closed gate (no silent Full).
  HaydnMCFormats Fmts;
  auto Fake = static_cast<FormatID>(1u);
  auto Err = verifyCommittedBundle(Fake, {Haydn::ADD32}, Fmts);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("unknown FormatID"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, ProductFormatIDImmIsZero) {
  // Durable BUNDLE-root contract: FormatID Full encodes as imm 0.
  EXPECT_EQ(formatIDToImm(FormatID::Bundle128Full), 0u);
  EXPECT_TRUE(isKnownFormatIDImm(0u));
  EXPECT_FALSE(isKnownFormatIDImm(1u));
}

TEST(HaydnBundleVerifyTest, DualLoadMayPack) {
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(FormatID::Bundle128Full,
                                   {Haydn::LD32, Haydn::LD32}, Fmts, &Plan);
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
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  // LD32 + multi-slot MAC family — typical DSP density pack.
  auto Err = verifyCommittedBundle(
      FormatID::Bundle128Full, {Haydn::LD32, Haydn::X2MULA32}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 2u);
}

TEST(HaydnBundleVerifyTest, EncodedBytesAlwaysSixteenOnSuccess) {
  HaydnMCFormats Fmts;
  for (ArrayRef<unsigned> Ops :
       {ArrayRef<unsigned>{Haydn::NOP}, ArrayRef<unsigned>{Haydn::ADD32},
        ArrayRef<unsigned>{Haydn::ST32, Haydn::ADD64},
        ArrayRef<unsigned>{Haydn::ADD32, Haydn::XOR32, Haydn::NOT32}}) {
    BundlePlan Plan;
    auto Err =
        verifyCommittedBundle(FormatID::Bundle128Full, Ops, Fmts, &Plan);
    if (Err)
      continue; // some NOP/slot combos may reject; only check successes
    EXPECT_EQ(Plan.Bytes.Value, 16u);
    EXPECT_EQ(Plan.Cycles.Value, 1u);
    EXPECT_EQ(Plan.FID, FormatID::Bundle128Full);
  }
}

TEST(HaydnBundleVerifyTest, PlanFromPacketFormatsMatchesOracleOcc) {
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  ASSERT_FALSE(verifyCommittedBundle(FormatID::Bundle128Full,
                                     {Haydn::ADD32, Haydn::LD32}, Fmts, &Plan));
  auto Table =
      planFromPacketFormats(Fmts.getPacketFormats(), Plan.OccupiedSlots);
  ASSERT_TRUE(Table.has_value());
  EXPECT_EQ(Table->Bytes, Plan.Bytes);
  EXPECT_EQ(Table->FID, FormatID::Bundle128Full);
}

} // namespace
