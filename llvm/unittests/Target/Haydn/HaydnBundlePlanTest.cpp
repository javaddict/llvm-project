//===- HaydnBundlePlanTest.cpp - Product EncodedBytes / plan ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for HaydnBundlePlan product size authority (Format E cutover):
//   * productParcelBytes() == registry EncodedBytes for E96 rows (pin 12)
//   * EncodedBytes vs EncodedBits never aliased
//   * BundlePlan product legality uses product EncodedBytes + product rows
//   * hwloop::ProductParcelBytes tracks productParcelBytes only
//   * planFromPacketFormats uses registry EncodedBytes (composite Size ignored)
//   * ceilProductParcels / productBundlesToBytes follow product parcel
//
//===----------------------------------------------------------------------===//

#include "HaydnBundlePlan.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnRegisterInfo.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::haydn::bundle;
// Do not `using namespace llvm::haydn::format` — EncodedBytes/EncodedBits collide
// with bundle:: types. Qualify format:: APIs explicitly.

namespace {

//===----------------------------------------------------------------------===//
// Product EncodedBytes = 12 via registry / generated row
//===----------------------------------------------------------------------===//

TEST(HaydnBundlePlanTest, ProductParcelBytesIsTwelveViaRegistry) {
  // Pin product parcel EncodedBytes to the production registry rows.
  EncodedBytes Two =
      bundleEncodedBytesOrDie(BundleFormatRowID::E96TwoEntry);
  EncodedBytes Three =
      bundleEncodedBytesOrDie(BundleFormatRowID::E96ThreeEntry);
  EncodedBytes ProfileMax{
      haydn::format::maxEncodedBytesInProfile(
          haydn::format::ObjectEncodingProfileID::E96)
          .Value};

  EXPECT_EQ(Two.Value, Three.Value);
  EXPECT_EQ(Two.Value, ProfileMax.Value);
  EXPECT_EQ(productParcelBytes().Value, Two.Value);
  EXPECT_EQ(productParcelBytes().Value, Three.Value);
  EXPECT_EQ(productParcelBytes().Value, ProfileMax.Value);
  EXPECT_EQ(productParcelBytes().Value, 12u);
  EXPECT_EQ(ProductEncodedBytesValue, 12u);
  EXPECT_EQ(ProductEncodedBitsValue, 96u);
  EXPECT_EQ(ProductEncodedBytes, productParcelBytes());
}

TEST(HaydnBundlePlanTest, ProductRowsAreE96Only) {
  EXPECT_TRUE(isProductBundleRow(BundleFormatRowID::E96TwoEntry));
  EXPECT_TRUE(isProductBundleRow(BundleFormatRowID::E96ThreeEntry));
  EXPECT_TRUE(haydn::format::isProductRow(BundleFormatRowID::E96TwoEntry));
  EXPECT_TRUE(haydn::format::isProductRow(BundleFormatRowID::E96ThreeEntry));
  EXPECT_EQ(ProductDefaultRowID, BundleFormatRowID::E96TwoEntry);
  EXPECT_EQ(ProductFormatMask,
            formatRowBit(BundleFormatRowID::E96TwoEntry) |
                formatRowBit(BundleFormatRowID::E96ThreeEntry));
}

TEST(HaydnBundlePlanTest, EncodedBytesBitsNotAliased) {
  EncodedBytes B = productParcelBytes();
  EncodedBits Bits = ProductEncodedBits;
  EXPECT_NE(B.Value, Bits.Value);
  EXPECT_EQ(vliwFormatSizeAsBits(B.Value).Value, Bits.Value);
  EXPECT_EQ(vliwFormatSizeAsBytes(productParcelBytes().Value),
            productParcelBytes());
}

//===----------------------------------------------------------------------===//
// hwloop contracts follow productParcelBytes only
//===----------------------------------------------------------------------===//

TEST(HaydnBundlePlanTest, HwloopBytesMatchesProductParcel) {
  HaydnMCFormats Fmts;
  auto FromPackets = productEncodedBytesFromPackets(Fmts.getPacketFormats());
  ASSERT_TRUE(FromPackets.has_value());
  EXPECT_EQ(static_cast<unsigned>(haydn::hwloop::ProductParcelBytes),
            productParcelBytes().Value);
  EXPECT_EQ(*FromPackets, productParcelBytes());
  EXPECT_EQ(haydn::hwloop::MinSetupBytes,
            productBundlesToBytes(haydn::hwloop::InterveningCycles));
  EXPECT_EQ(haydn::hwloop::MinSetupBytes,
            productBundlesToBytes(haydn::hwloop::MinSetupBundles));
  EXPECT_NE(haydn::hwloop::MinSetupBytes,
            productBundlesToBytes(haydn::hwloop::SetupIssueDistance));
  // InterveningCycles=2 → MinSetupBytes = 2 × 12 = 24 (not 2 × 16).
  EXPECT_EQ(haydn::hwloop::MinSetupBytes, 24);
  EXPECT_EQ(haydn::hwloop::MinSetupIssueBytes, 36);
  EXPECT_EQ(haydn::hwloop::Off1SafetyMarginBytes, 36);
  EXPECT_EQ(haydn::hwloop::MaxSingleBranchGrowthBytes, 48);
}

TEST(HaydnBundlePlanTest, CeilProductParcelsFromEncodedBytes) {
  EXPECT_EQ(ceilProductParcels(0), 0u);
  EXPECT_EQ(ceilProductParcels(1), 1u);
  EXPECT_EQ(ceilProductParcels(productParcelBytes().Value), 1u);
  EXPECT_EQ(ceilProductParcels(productParcelBytes().Value + 1), 2u);
  EXPECT_EQ(ceilProductParcels(2u * productParcelBytes().Value), 2u);
  EXPECT_EQ(ceilProductParcels(2u * productParcelBytes().Value + 1), 3u);
  // Synthetic non-product unit.
  EXPECT_EQ(ceilParcelsForBytes(16, EncodedBytes{8}), 2u);
  EXPECT_EQ(productBundlesToBytes(0), 0);
  EXPECT_EQ(productBundlesToBytes(1),
            static_cast<int64_t>(productParcelBytes().Value));
  EXPECT_EQ(productBundlesToBytes(3),
            3 * static_cast<int64_t>(productParcelBytes().Value));
}

// W70.1: the size oracle is per-row EncodedBytes, never a member-count
// multiple of the parcel. Both product rows carry the same registry width
// today; the law this pins is that committedEncodedBytes resolves THROUGH
// encodedBytesForRow (registry lookup), so a future unequal-width family
// changes the answer by construction, not by falling through to
// productParcelBytes() arithmetic.
TEST(HaydnBundlePlanTest, RowBytesAreRegistryLookupNotParcelArithmetic) {
  for (BundleFormatRowID Row :
       {BundleFormatRowID::E96TwoEntry, BundleFormatRowID::E96ThreeEntry}) {
    auto B = encodedBytesForRow(Row);
    ASSERT_TRUE(B.has_value());
    // Registry width equals (not derived from) the product parcel today.
    EXPECT_EQ(B->Value, productParcelBytes().Value);
    // The lookup is the only sanctioned source: it must not return a
    // member-count multiple (E3 == 3 entries == 1 parcel, NOT 3).
    EXPECT_LT(B->Value,
              3u * productParcelBytes().Value);
  }
  // Unknown rows fail closed rather than inventing a parcel width.
  EXPECT_FALSE(encodedBytesForRow(static_cast<BundleFormatRowID>(0xDEAD))
                   .has_value());
}

//===----------------------------------------------------------------------===//
// BundlePlan product legality + plan paths
//===----------------------------------------------------------------------===//

TEST(HaydnBundlePlanTest, MakeProductPlanIsProductLegal) {
  unsigned Members[] = {Haydn::ADD32, Haydn::LD32};
  BundlePlan P = makeProductPlan(Haydn::SLOT0 | Haydn::SLOT2, Members);
  EXPECT_TRUE(P.isProductLegal());
  EXPECT_EQ(P.Row, BundleFormatRowID::E96TwoEntry);
  EXPECT_EQ(P.Bytes, productParcelBytes());
  EXPECT_EQ(P.Bytes.Value, 12u);
  EXPECT_EQ(P.Cycles.Value, 1u);
  EXPECT_EQ(P.memberCount(), 2u);
  EXPECT_EQ(P.OccupiedSlots, SlotBits(Haydn::SLOT0 | Haydn::SLOT2));
}

TEST(HaydnBundlePlanTest, ThreeMemberPlanSelectsE96ThreeEntry) {
  unsigned Members[] = {Haydn::ADD32, Haydn::LD32, Haydn::X2MULA32};
  BundlePlan P = makeProductPlan(Haydn::SLOT_ALL, Members);
  EXPECT_TRUE(P.isProductLegal());
  EXPECT_EQ(P.Row, BundleFormatRowID::E96ThreeEntry);
  EXPECT_EQ(P.Bytes, productParcelBytes());
  EXPECT_EQ(P.Completion, CompletionStateID::AllEntriesReal);
  EXPECT_TRUE(P.isProductEncodable());
}

TEST(HaydnBundlePlanTest, StallPlanIsProductLegalIdleStub) {
  BundlePlan Stall = makeStallPlan();
  EXPECT_TRUE(Stall.isProductLegal());
  EXPECT_TRUE(Stall.empty());
  EXPECT_EQ(Stall.OccupiedSlots, 0u);
  EXPECT_EQ(Stall.Bytes, productParcelBytes());
  EXPECT_EQ(Stall.Completion, CompletionStateID::StubIdle);
  EXPECT_FALSE(Stall.isProductEncodable())
      << "idle stub is plan-legal but not product-encodable until idle law";
}

TEST(HaydnBundlePlanTest, RejectsTooManyMembers) {
  BundlePlan P = makeProductPlan(Haydn::SLOT_ALL);
  P.MemberOpcodes = {1, 2, 3, 4};
  EXPECT_FALSE(P.isProductLegal());
}

TEST(HaydnBundlePlanTest, WrongBytesRejectsProductLegal) {
  BundlePlan P = makeStallPlan();
  P.Bytes = EncodedBytes{8};
  EXPECT_FALSE(P.isProductLegal());
}

TEST(HaydnBundlePlanTest, WrongCyclesRejectsProductLegal) {
  BundlePlan P = makeStallPlan();
  P.Cycles = CycleCount{2};
  EXPECT_FALSE(P.isProductLegal());
}

TEST(HaydnBundlePlanTest, ProductEncodedBytesFromPacketsMatchesRegistry) {
  // Live PacketFormats product rows are Format E (Size == productParcelBytes).
  // EncodedBytes authority remains the registry.
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  const VLIWFormat *Prod = productVLIWFormat(Packets);
  ASSERT_NE(Prod, nullptr);
  // Product composite name is Format E.
  EXPECT_TRUE(StringRef(Prod->Name).starts_with("BUNDLE_E96_"))
      << "product row name=" << Prod->Name;
  EXPECT_EQ(vliwFormatSizeAsBytes(Prod->getSize()), productParcelBytes());
  EXPECT_EQ(Prod->getSize(), productParcelBytes().Value);

  auto FromPackets = productEncodedBytesFromPackets(Packets);
  ASSERT_TRUE(FromPackets.has_value());
  EXPECT_EQ(*FromPackets, productParcelBytes());
  EXPECT_EQ(FromPackets->Value, 12u);
}

TEST(HaydnBundlePlanTest, PlanFromPacketFormatsUsesProductParcelBytes) {
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  // Empty occupancy is always product-legal (idle / NOP-fill parcel).
  auto Idle = planFromPacketFormats(Packets, /*Occupied=*/0);
  ASSERT_TRUE(Idle.has_value());
  EXPECT_TRUE(Idle->isProductLegal());
  EXPECT_EQ(Idle->Bytes, productParcelBytes());
  EXPECT_EQ(Idle->Bytes.Value, 12u);

  // Any occupancy the live PacketFormats table covers must charge product
  // EncodedBytes.
  for (SlotBits Combo = 0; Combo < 32; ++Combo) {
    if (!Packets.getFormat(Combo))
      continue;
    auto Plan = planFromPacketFormats(Packets, Combo);
    ASSERT_TRUE(Plan.has_value()) << "combo=" << Combo;
    EXPECT_TRUE(Plan->isProductLegal()) << "combo=" << Combo;
    EXPECT_EQ(Plan->Bytes, productParcelBytes()) << "combo=" << Combo;
    EXPECT_EQ(Plan->Bytes.Value, 12u) << "combo=" << Combo;
  }
}

TEST(HaydnBundlePlanTest, CycleCountIsNotBytes) {
  CycleCount Intervening = CycleCount{haydn::hwloop::InterveningCycles};
  CycleCount IssueDistance = CycleCount{haydn::hwloop::SetupIssueDistance};
  EncodedBytes InterveningBytes{
      Intervening.Value * productParcelBytes().Value};
  EXPECT_EQ(Intervening.Value, 2u);
  EXPECT_EQ(IssueDistance.Value, 3u);
  EXPECT_EQ(InterveningBytes.Value, 24u);
  EXPECT_NE(Intervening.Value, InterveningBytes.Value);
}

TEST(HaydnBundlePlanTest, Off1SafetyMarginIsThreeProductParcels) {
  EXPECT_EQ(haydn::hwloop::Off1SafetyMarginBundles, 3);
  EXPECT_EQ(haydn::hwloop::Off1SafetyMarginBytes,
            productBundlesToBytes(3));
  EXPECT_EQ(haydn::hwloop::Off1SafetyMarginBytes, 36);
}

//===----------------------------------------------------------------------===//
// Row / completion durable identity
//===----------------------------------------------------------------------===//

TEST(HaydnBundlePlanTest, FormatRowImmRoundTrip) {
  for (BundleFormatRowID Row : {BundleFormatRowID::E96TwoEntry,
                                BundleFormatRowID::E96ThreeEntry}) {
    unsigned Imm = formatRowToImm(Row);
    auto Back = formatRowFromImm(Imm);
    ASSERT_TRUE(Back.has_value());
    EXPECT_EQ(*Back, Row);
    auto Bytes = encodedBytesForRow(Row);
    ASSERT_TRUE(Bytes.has_value());
    EXPECT_EQ(*Bytes, productParcelBytes());
    EXPECT_EQ(Bytes->Value, 12u);
  }
  EXPECT_FALSE(formatRowFromImm(0x7fffu).has_value())
      << "unknown imm must not decode as a product row";
}

TEST(HaydnBundlePlanTest, SelectProductRowByMemberCount) {
  EXPECT_EQ(selectProductRowForMemberCount(0), BundleFormatRowID::E96TwoEntry);
  EXPECT_EQ(selectProductRowForMemberCount(1), BundleFormatRowID::E96TwoEntry);
  EXPECT_EQ(selectProductRowForMemberCount(2), BundleFormatRowID::E96TwoEntry);
  EXPECT_EQ(selectProductRowForMemberCount(3),
            BundleFormatRowID::E96ThreeEntry);
}

TEST(HaydnBundlePlanTest, SelectCompletionForRow) {
  EXPECT_EQ(selectCompletionFor(BundleFormatRowID::E96TwoEntry, 0),
            CompletionStateID::StubIdle);
  EXPECT_EQ(selectCompletionFor(BundleFormatRowID::E96TwoEntry, 1),
            CompletionStateID::AllEntriesReal);
  EXPECT_EQ(selectCompletionFor(BundleFormatRowID::E96TwoEntry, 2),
            CompletionStateID::AllEntriesReal);
  EXPECT_EQ(selectCompletionFor(BundleFormatRowID::E96ThreeEntry, 3),
            CompletionStateID::AllEntriesReal);
  EXPECT_EQ(selectCompletionForMembersAndPads(BundleFormatRowID::E96TwoEntry,
                                              /*RealMembers=*/0,
                                              /*HasPadNop=*/true),
            CompletionStateID::AllEntriesReal);
  EXPECT_EQ(selectCompletionForMembersAndPads(BundleFormatRowID::E96TwoEntry,
                                              /*RealMembers=*/0,
                                              /*HasPadNop=*/false),
            CompletionStateID::StubIdle);
  // Inverse expected fill: same answer, independently derived (pad-only
  // idle and any real membership are AllEntriesReal; empty is StubIdle).
  EXPECT_EQ(expectedGoldenRowCompletion(/*RealMembers=*/1, /*HasPadNop=*/false),
            CompletionStateID::AllEntriesReal);
  EXPECT_EQ(expectedGoldenRowCompletion(/*RealMembers=*/0, /*HasPadNop=*/true),
            CompletionStateID::AllEntriesReal);
  EXPECT_EQ(expectedGoldenRowCompletion(/*RealMembers=*/2, /*HasPadNop=*/true),
            CompletionStateID::AllEntriesReal);
  EXPECT_EQ(expectedGoldenRowCompletion(/*RealMembers=*/0, /*HasPadNop=*/false),
            CompletionStateID::StubIdle);
  EXPECT_TRUE(isProductLegalCompletion(CompletionStateID::AllEntriesReal));
  EXPECT_TRUE(isStubCompletion(CompletionStateID::StubIdle));
}

//===----------------------------------------------------------------------===//
// Compact-subset soft RA physreg membership (unchanged contract)
//===----------------------------------------------------------------------===//

TEST(HaydnBundlePlanTest, CompactSubsetPhysRegMembershipAndFullHintGate) {
  HaydnMCFormats Fmts;
  EXPECT_TRUE(productRAHintEligible(Fmts.getPacketFormats()));

  HaydnRegisterInfo TRI;
  EXPECT_TRUE(isHaydnCompactSubsetPhysReg(TRI, Haydn::R0));
  EXPECT_TRUE(isHaydnCompactSubsetPhysReg(TRI, Haydn::R7));
  EXPECT_FALSE(isHaydnCompactSubsetPhysReg(TRI, Haydn::R8));
  EXPECT_FALSE(isHaydnCompactSubsetPhysReg(TRI, Haydn::R12));
  EXPECT_TRUE(isHaydnCompactSubsetPhysReg(TRI, Haydn::D0));
  EXPECT_FALSE(isHaydnCompactSubsetPhysReg(TRI, Haydn::D8));
}

TEST(HaydnBundlePlanTest, ProductRAHintEligibleFromGeneratedComposite) {
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  const VLIWFormat *Prod = productVLIWFormat(Packets);
  ASSERT_NE(Prod, nullptr);
  EXPECT_EQ(vliwFormatSizeAsBytes(Prod->getSize()), productParcelBytes());
  EXPECT_TRUE(productRAHintEligible(Packets));
  EXPECT_TRUE(productCovers(Packets, /*Occupied=*/0));
  // Live product rows cover their own SlotSet (E2 0x3 / E3 0x1c); residual
  // SLOT_ALL (0x7) is not a product coverage identity after Format E cutover.
  EXPECT_TRUE(productCovers(Packets, Prod->getSlotSet()));
}

} // namespace
