//===- HaydnBundlePlanTest.cpp - FormatID / size types ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for HaydnBundlePlan (FormatID + size authority):
//   * EncodedBytes vs EncodedBits never aliased
//   * Product FormatID is only Bundle128Full
//   * Generated PacketFormats Size is EncodedBytes (16)
//   * Slot windows sum to 128 EncodedBits
//   * BundlePlan product legality
//   * hwloop::Bundle128Bytes / ProductFormatDesc.Bytes / productParcelBytes
//     are one EncodedBytes oracle (AIE Format->getSize peer)
//   * ceilProductParcels / productBundlesToBytes for Fixup + HardwareLoops
//   * FormatID imm encode/decode (durable BUNDLE-root contract)
//
//===----------------------------------------------------------------------===//

#include "HaydnBundlePlan.h"
#include "HaydnHWLoopContracts.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

TEST(HaydnBundlePlanTest, ProductConstants) {
  EXPECT_EQ(Bundle128EncodedBytesValue, 16u);
  EXPECT_EQ(Bundle128EncodedBitsValue, 128u);
  EXPECT_EQ(Bundle128EncodedBytes.Value, 16u);
  EXPECT_EQ(Bundle128EncodedBits.Value, 128u);
  EXPECT_EQ(ProductFormatID, FormatID::Bundle128Full);
  EXPECT_TRUE(isProductFormat(FormatID::Bundle128Full));
}

TEST(HaydnBundlePlanTest, EncodedBytesBitsNotAliased) {
  // Plan §3.5: never treat bytes as bits or vice versa.
  EncodedBytes B = Bundle128EncodedBytes;
  EncodedBits Bits = Bundle128EncodedBits;
  EXPECT_NE(B.Value, Bits.Value);
  EXPECT_EQ(vliwFormatSizeAsBits(B.Value).Value, Bits.Value);
  EXPECT_EQ(vliwFormatSizeAsBytes(16u).Value, 16u);
  EXPECT_EQ(slotInfoSizeAsBits(48u).Value, 48u);
}

TEST(HaydnBundlePlanTest, SlotWindowsSumToBundle128Bits) {
  EXPECT_EQ(Slot0EncodedBits.Value + Slot1EncodedBits.Value +
                Slot2EncodedBits.Value,
            Bundle128EncodedBitsValue);
  EXPECT_EQ(Slot0EncodedBits.Value, 48u);
  EXPECT_EQ(Slot1EncodedBits.Value, 40u);
  EXPECT_EQ(Slot2EncodedBits.Value, 40u);
}

TEST(HaydnBundlePlanTest, HwloopBytesMatchesPlanAuthority) {
  // Single EncodedBytes oracle — no dual hard-coded 16.
  EXPECT_EQ(static_cast<unsigned>(haydn::hwloop::Bundle128Bytes),
            Bundle128EncodedBytesValue);
  EXPECT_EQ(static_cast<unsigned>(haydn::hwloop::Bundle128Bytes),
            productParcelBytes().Value);
  EXPECT_EQ(static_cast<unsigned>(haydn::hwloop::Bundle128Bytes),
            ProductFormatDesc.Bytes.Value);
  auto FromID = encodedBytesFor(FormatID::Bundle128Full);
  ASSERT_TRUE(FromID.has_value());
  EXPECT_EQ(static_cast<unsigned>(haydn::hwloop::Bundle128Bytes),
            FromID->Value);
  EXPECT_EQ(haydn::hwloop::MinSetupBytes,
            productBundlesToBytes(haydn::hwloop::MinSetupBundles));
  EXPECT_EQ(haydn::hwloop::MinSetupBytes, 48);
}

TEST(HaydnBundlePlanTest, EncodedBytesForProductFormat) {
  auto B = encodedBytesFor(FormatID::Bundle128Full);
  ASSERT_TRUE(B.has_value());
  EXPECT_EQ(B->Value, 16u);
  EXPECT_EQ(encodedBytesOrProduct(FormatID::Bundle128Full),
            productParcelBytes());
}

// BR / Fixup / HardwareLoops ceil byte→parcel via EncodedBytes oracle
// (AIEMachineAlignment.cpp:287+ sums Format->getSize(); Haydn uses
// ProductFormatDesc.Bytes for product-only live table).
TEST(HaydnBundlePlanTest, CeilProductParcelsFromEncodedBytes) {
  EXPECT_EQ(ceilProductParcels(0), 0u);
  EXPECT_EQ(ceilProductParcels(1), 1u);
  EXPECT_EQ(ceilProductParcels(16), 1u);
  EXPECT_EQ(ceilProductParcels(17), 2u);
  EXPECT_EQ(ceilProductParcels(32), 2u);
  EXPECT_EQ(ceilProductParcels(33), 3u);
  // Multi-parcel pseudo sizing (LOADI32 worst = 2 × productParcelBytes).
  EXPECT_EQ(ceilProductParcels(2u * productParcelBytes().Value), 2u);
  // N-format-ready: synthetic compact unit (not product emit).
  EXPECT_EQ(ceilParcelsForBytes(16, EncodedBytes{8}), 2u);
  EXPECT_EQ(ceilParcelsForBytes(15, EncodedBytes{8}), 2u);
  EXPECT_EQ(ceilParcelsForBytes(8, EncodedBytes{8}), 1u);
  EXPECT_EQ(productBundlesToBytes(0), 0);
  EXPECT_EQ(productBundlesToBytes(1), 16);
  EXPECT_EQ(productBundlesToBytes(3), 48);
  EXPECT_EQ(productBundlesToBytes(haydn::hwloop::MinSetupBundles),
            haydn::hwloop::MinSetupBytes);
}

TEST(HaydnBundlePlanTest, ProductParcelBytesIsFormatDescAuthority) {
  // getInstSizeInBytes bare-real unit must be ProductFormatDesc.Bytes.
  EXPECT_EQ(productParcelBytes(), ProductFormatDesc.Bytes);
  EXPECT_EQ(productParcelBytes(), Bundle128EncodedBytes);
  EXPECT_EQ(ProductFormatDesc.FID, FormatID::Bundle128Full);
}

TEST(HaydnBundlePlanTest, MakeBundle128PlanIsProductLegal) {
  unsigned Members[] = {Haydn::ADD32, Haydn::LD32};
  BundlePlan P = makeBundle128Plan(Haydn::SLOT0 | Haydn::SLOT2, Members);
  EXPECT_TRUE(P.isProductLegal());
  EXPECT_EQ(P.FID, FormatID::Bundle128Full);
  EXPECT_EQ(P.Bytes.Value, 16u);
  EXPECT_EQ(P.Cycles.Value, 1u);
  EXPECT_EQ(P.memberCount(), 2u);
  EXPECT_EQ(P.OccupiedSlots, SlotBits(Haydn::SLOT0 | Haydn::SLOT2));
  EXPECT_FALSE(P.empty());
}

TEST(HaydnBundlePlanTest, StallPlanIsProductLegalEmptyMembers) {
  BundlePlan Stall = makeStallPlan();
  EXPECT_TRUE(Stall.isProductLegal());
  EXPECT_TRUE(Stall.empty());
  EXPECT_EQ(Stall.OccupiedSlots, 0u);
  EXPECT_EQ(Stall.Bytes.Value, 16u) << "idle cycle still emits Bundle128 NOP parcel";
}

TEST(HaydnBundlePlanTest, RejectsTooManyMembers) {
  BundlePlan P = makeBundle128Plan(Haydn::SLOT_ALL);
  P.MemberOpcodes = {1, 2, 3, 4}; // > ISSUE_SLOT_COUNT
  EXPECT_FALSE(P.isProductLegal());
}

TEST(HaydnBundlePlanTest, GeneratedPacketFormatSizeIsEncodedBytes) {
  // Generated VLIWFormat row for BUNDLE128_FULL: Size==16 means EncodedBytes.
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  const VLIWFormat *Full =
      Packets.getFormat(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2);
  ASSERT_NE(Full, nullptr);
  EncodedBytes B = vliwFormatSizeAsBytes(Full->getSize());
  EXPECT_EQ(B, Bundle128EncodedBytes);
  EncodedBits Bits = vliwFormatSizeAsBits(Full->getSize());
  EXPECT_EQ(Bits, Bundle128EncodedBits);
  // Name pin.
  EXPECT_STREQ(Full->Name, "BUNDLE128_FULL");
}

TEST(HaydnBundlePlanTest, GeneratedSlotInfoSizeIsEncodedBits) {
  HaydnMCFormats Fmts;
  const MCSlotInfo *S0 = Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_S0);
  const MCSlotInfo *S1 = Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_S1);
  const MCSlotInfo *S2 = Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_S2);
  ASSERT_NE(S0, nullptr);
  ASSERT_NE(S1, nullptr);
  ASSERT_NE(S2, nullptr);
  // MCSlotInfo::getSize() — bits (public getter).
  EXPECT_EQ(slotInfoSizeAsBits(S0->getSize()), Slot0EncodedBits);
  EXPECT_EQ(slotInfoSizeAsBits(S1->getSize()), Slot1EncodedBits);
  EXPECT_EQ(slotInfoSizeAsBits(S2->getSize()), Slot2EncodedBits);
  EXPECT_EQ(S0->getSize() + S1->getSize() + S2->getSize(),
            Bundle128EncodedBitsValue);
}

TEST(HaydnBundlePlanTest, PlanFromPacketFormatsAllSubsets) {
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  for (SlotBits Combo = 0; Combo <= Haydn::SLOT_ALL; ++Combo) {
    auto Plan = planFromPacketFormats(Packets, Combo);
    ASSERT_TRUE(Plan.has_value()) << "combo=" << Combo;
    EXPECT_TRUE(Plan->isProductLegal()) << "combo=" << Combo;
    EXPECT_EQ(Plan->OccupiedSlots, Combo);
    EXPECT_EQ(Plan->Bytes.Value, 16u);
  }
}

TEST(HaydnBundlePlanTest, CycleCountIsNotBytes) {
  // Architectural setup distance is in cycles; byte math multiplies separately.
  CycleCount Setup = CycleCount{haydn::hwloop::MinSetupBundles};
  EncodedBytes SetupBytes{
      Setup.Value * Bundle128EncodedBytesValue};
  EXPECT_EQ(Setup.Value, 3u);
  EXPECT_EQ(SetupBytes.Value, 48u);
  EXPECT_NE(Setup.Value, SetupBytes.Value);
}

//===----------------------------------------------------------------------===//
// Extended plan / size authority
//===----------------------------------------------------------------------===//

TEST(HaydnBundlePlanTest, MakePlanCopiesMembersInOrder) {
  unsigned Members[] = {Haydn::LD32, Haydn::X2MULA32, Haydn::ADD32};
  BundlePlan P = makeBundle128Plan(Haydn::SLOT_ALL, Members);
  ASSERT_EQ(P.MemberOpcodes.size(), 3u);
  EXPECT_EQ(P.MemberOpcodes[0], Haydn::LD32);
  EXPECT_EQ(P.MemberOpcodes[1], Haydn::X2MULA32);
  EXPECT_EQ(P.MemberOpcodes[2], Haydn::ADD32);
  EXPECT_TRUE(P.isProductLegal());
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

TEST(HaydnBundlePlanTest, EncodedBytesBitsRoundTrip) {
  for (unsigned Bytes : {1u, 2u, 4u, 8u, 16u, 32u}) {
    EncodedBits Bits = vliwFormatSizeAsBits(Bytes);
    EXPECT_EQ(Bits.Value, Bytes * 8u);
    EXPECT_EQ(vliwFormatSizeAsBytes(Bytes).Value, Bytes);
  }
}

TEST(HaydnBundlePlanTest, PlanFromPacketFormatsMatchesMakePlan) {
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  for (SlotBits Combo : {SlotBits(0), SlotBits(Haydn::SLOT0),
                         SlotBits(Haydn::SLOT1 | Haydn::SLOT2),
                         SlotBits(Haydn::SLOT_ALL)}) {
    auto FromTable = planFromPacketFormats(Packets, Combo);
    ASSERT_TRUE(FromTable.has_value());
    BundlePlan Hand = makeBundle128Plan(Combo);
    EXPECT_EQ(FromTable->FID, Hand.FID);
    EXPECT_EQ(FromTable->Bytes, Hand.Bytes);
    EXPECT_EQ(FromTable->Cycles, Hand.Cycles);
    EXPECT_EQ(FromTable->OccupiedSlots, Hand.OccupiedSlots);
  }
}

TEST(HaydnBundlePlanTest, SlotBitWidthsMatchGeneratedSlotInfo) {
  HaydnMCFormats Fmts;
  EXPECT_EQ(Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_S0)->getSize(),
            Slot0EncodedBits.Value);
  EXPECT_EQ(Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_S1)->getSize(),
            Slot1EncodedBits.Value);
  EXPECT_EQ(Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_S2)->getSize(),
            Slot2EncodedBits.Value);
}

TEST(HaydnBundlePlanTest, Off1SafetyMarginIsThreeParcels) {
  // hwloop contracts: 3 * 16 = 48 — must track Bundle128EncodedBytes.
  EXPECT_EQ(haydn::hwloop::Off1SafetyMarginBytes,
            static_cast<int64_t>(3 * Bundle128EncodedBytesValue));
  EXPECT_EQ(haydn::hwloop::Off1SafetyMarginBundles, 3);
}

//===----------------------------------------------------------------------===//
// Durable FormatID imm on BUNDLE roots (plan §6.3)
//===----------------------------------------------------------------------===//
// Mirrors AIE format identity after finalizeBundle (AIEHazardRecognizer.cpp:
// 278-312; AIEBundle.h:150-156 getFormatOrNull). Haydn stores FormatID as an
// imm so multi-format tables need no MF side map. Product Full = imm 0.

TEST(HaydnBundlePlanTest, FormatIDImmProductIsZero) {
  EXPECT_EQ(formatIDToImm(FormatID::Bundle128Full), 0u);
  EXPECT_EQ(formatIDToImm(ProductFormatID), 0u);
  auto Decoded = formatIDFromImm(0u);
  ASSERT_TRUE(Decoded.has_value());
  EXPECT_EQ(*Decoded, FormatID::Bundle128Full);
  EXPECT_TRUE(isKnownFormatIDImm(0u));
}

TEST(HaydnBundlePlanTest, FormatIDImmUnknownRejected) {
  // N-format-ready: unknown encodings must not silently become Full.
  EXPECT_FALSE(formatIDFromImm(1u).has_value());
  EXPECT_FALSE(formatIDFromImm(0xffffffffu).has_value());
  EXPECT_FALSE(isKnownFormatIDImm(1u));
}

TEST(HaydnBundlePlanTest, FormatIDImmRoundTrip) {
  for (FormatID ID : {FormatID::Bundle128Full}) {
    unsigned Imm = formatIDToImm(ID);
    auto Back = formatIDFromImm(Imm);
    ASSERT_TRUE(Back.has_value());
    EXPECT_EQ(*Back, ID);
    auto Bytes = encodedBytesFor(ID);
    ASSERT_TRUE(Bytes.has_value());
    EXPECT_EQ(Bytes->Value, Bundle128EncodedBytesValue);
  }
}

TEST(HaydnBundlePlanTest, FormatIDImmOperandShape) {
  // Contract used by stampBundleFormatID: CreateImm(formatIDToImm(...)).
  MachineOperand MO =
      MachineOperand::CreateImm(static_cast<int64_t>(formatIDToImm(
          ProductFormatID)));
  ASSERT_TRUE(MO.isImm());
  EXPECT_FALSE(MO.isReg());
  auto ID = formatIDFromImm(static_cast<unsigned>(MO.getImm()));
  ASSERT_TRUE(ID.has_value());
  EXPECT_EQ(*ID, FormatID::Bundle128Full);
}

//===----------------------------------------------------------------------===//
// Singleton BUNDLE roots share the same FormatID imm contract
//===----------------------------------------------------------------------===//
// HaydnFinalizeBundle (AIEFinalizeBundle.cpp:22-54 peer) wraps standalone
// real MIs via finalizeBundle + stampBundleFormatID(ProductFormatID). Product
// encode remains Bundle128Full / imm 0 only (N-format-ready API, one live row).

TEST(HaydnBundlePlanTest, SingletonBundleFormatIDIsProductFull) {
  // Same imm encoding multi-MI and singleton BUNDLE roots use.
  EXPECT_EQ(formatIDToImm(ProductFormatID), 0u);
  auto Bytes = encodedBytesFor(ProductFormatID);
  ASSERT_TRUE(Bytes.has_value());
  EXPECT_EQ(Bytes->Value, Bundle128EncodedBytesValue);
  // Singleton cycle is still one architectural cycle / one 16 B parcel.
  BundlePlan Single = makeBundle128Plan(Haydn::SLOT0, {Haydn::ADD32});
  EXPECT_TRUE(Single.isProductLegal());
  EXPECT_EQ(Single.memberCount(), 1u);
  EXPECT_EQ(Single.Bytes.Value, 16u);
  EXPECT_EQ(Single.Cycles.Value, 1u);
}

//===----------------------------------------------------------------------===//
// FormatDesc Priority ranking + synthetic 2nd format (solver only)
//===----------------------------------------------------------------------===//
// AIE PacketFormats::getFormat first-covering (AIEFormat.cpp:18-27;
// AIEFormat.h:44-70 VLIWFormat). Haydn strengthens with explicit Priority
// (plan §4.3). Multi-row synthetic table mirrors AIE BundleTest.cpp:33-41
// FormatData[] — unit/solver infra only, not product emit.

TEST(HaydnBundlePlanTest, ProductFormatDescRow) {
  EXPECT_EQ(ProductFormatDesc.FID, FormatID::Bundle128Full);
  EXPECT_EQ(ProductFormatDesc.Priority, 0u);
  EXPECT_EQ(ProductFormatDesc.Bytes, Bundle128EncodedBytes);
  EXPECT_EQ(ProductFormatDesc.SlotSet,
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
  EXPECT_TRUE(ProductFormatDesc.isProduct());
  EXPECT_TRUE(ProductFormatDesc.covers(0));
  EXPECT_TRUE(ProductFormatDesc.covers(Haydn::SLOT0));
  EXPECT_TRUE(ProductFormatDesc.covers(Haydn::SLOT_ALL));

  ArrayRef<FormatDesc> Product = productFormatTable();
  ASSERT_EQ(Product.size(), 1u);
  const FormatDesc *Sel =
      selectFormatByPriority(Product, Haydn::SLOT0 | Haydn::SLOT2);
  ASSERT_NE(Sel, nullptr);
  EXPECT_EQ(Sel->FID, FormatID::Bundle128Full);
  EXPECT_EQ(Sel->Bytes.Value, 16u);
}

TEST(HaydnBundlePlanTest, SelectFormatByPriority_SyntheticTwoRow) {
  // Unit-only second format (not product encode). Narrow prefers lower Priority
  // when it covers; Full is wide fallback with higher Priority number.
  constexpr FormatID SynthNarrow = static_cast<FormatID>(1);
  const FormatDesc Table[] = {
      // Priority 0: 8 B, SLOT0|SLOT1 only (synthetic compact).
      {SynthNarrow, /*Priority=*/0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT0 | Haydn::SLOT1)},
      // Priority 1: product Full wide fallback.
      {FormatID::Bundle128Full, /*Priority=*/1, Bundle128EncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT_ALL)},
  };

  // SLOT0 only → Narrow covers and wins on Priority.
  const FormatDesc *A = selectFormatByPriority(Table, Haydn::SLOT0);
  ASSERT_NE(A, nullptr);
  EXPECT_EQ(A->FID, SynthNarrow);
  EXPECT_EQ(A->Bytes.Value, 8u);

  // SLOT0|SLOT1 → still Narrow.
  const FormatDesc *B =
      selectFormatByPriority(Table, Haydn::SLOT0 | Haydn::SLOT1);
  ASSERT_NE(B, nullptr);
  EXPECT_EQ(B->FID, SynthNarrow);

  // SLOT2 only → Narrow does not cover; Full wins.
  const FormatDesc *C = selectFormatByPriority(Table, Haydn::SLOT2);
  ASSERT_NE(C, nullptr);
  EXPECT_EQ(C->FID, FormatID::Bundle128Full);
  EXPECT_EQ(C->Bytes.Value, 16u);

  // All three slots → Full only.
  const FormatDesc *D = selectFormatByPriority(Table, Haydn::SLOT_ALL);
  ASSERT_NE(D, nullptr);
  EXPECT_EQ(D->FID, FormatID::Bundle128Full);

  // Empty occupancy: both cover; lowest Priority (Narrow) wins — AIE-like
  // first-preferred among covering rows.
  const FormatDesc *E = selectFormatByPriority(Table, /*Occupied=*/0);
  ASSERT_NE(E, nullptr);
  EXPECT_EQ(E->FID, SynthNarrow);
}

TEST(HaydnBundlePlanTest, SelectFormatByPriority_EqualPriorityStableOrder) {
  // Equal Priority: earlier table index wins (stable AIE table-order tie-break).
  constexpr FormatID SynthA = static_cast<FormatID>(1);
  constexpr FormatID SynthB = static_cast<FormatID>(2);
  const FormatDesc Table[] = {
      {SynthA, /*Priority=*/5, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT0)},
      {SynthB, /*Priority=*/5, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT0)},
  };
  const FormatDesc *Sel = selectFormatByPriority(Table, Haydn::SLOT0);
  ASSERT_NE(Sel, nullptr);
  EXPECT_EQ(Sel->FID, SynthA);
}

TEST(HaydnBundlePlanTest, SelectFormatByPriority_NoCoverReturnsNull) {
  const FormatDesc Table[] = {
      {FormatID::Bundle128Full, 0, Bundle128EncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT0)}, // only S0
  };
  EXPECT_EQ(selectFormatByPriority(Table, Haydn::SLOT1), nullptr);
  EXPECT_EQ(selectFormatByPriority(Table, Haydn::SLOT0 | Haydn::SLOT1),
            nullptr);
}

TEST(HaydnBundlePlanTest, PlanFromFormatTable_ProductAndSynthetic) {
  // Product table always yields Full / 16 B.
  auto Prod = planFromFormatTable(productFormatTable(), Haydn::SLOT_ALL);
  ASSERT_TRUE(Prod.has_value());
  EXPECT_TRUE(Prod->isProductLegal());
  EXPECT_EQ(Prod->FID, FormatID::Bundle128Full);
  EXPECT_EQ(Prod->Bytes.Value, 16u);

  constexpr FormatID SynthNarrow = static_cast<FormatID>(1);
  const FormatDesc Multi[] = {
      {SynthNarrow, 0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT0 | Haydn::SLOT1)},
      ProductFormatDesc,
  };
  auto NarrowPlan = planFromFormatTable(Multi, Haydn::SLOT0, {Haydn::ADD32});
  ASSERT_TRUE(NarrowPlan.has_value());
  EXPECT_EQ(NarrowPlan->FID, SynthNarrow);
  EXPECT_EQ(NarrowPlan->Bytes.Value, 8u);
  EXPECT_FALSE(NarrowPlan->isProductLegal())
      << "synthetic format must not pass product legality";
  EXPECT_EQ(NarrowPlan->memberCount(), 1u);

  auto FullPlan = planFromFormatTable(Multi, Haydn::SLOT2);
  ASSERT_TRUE(FullPlan.has_value());
  EXPECT_EQ(FullPlan->FID, FormatID::Bundle128Full);
  EXPECT_TRUE(FullPlan->isProductLegal());
}

TEST(HaydnBundlePlanTest, FormatIDBitAndProductMask) {
  EXPECT_EQ(formatIDBit(FormatID::Bundle128Full), 1ull);
  EXPECT_EQ(ProductFormatMask, 1ull);
  EXPECT_EQ(formatIDBit(static_cast<FormatID>(1)), 2ull);
  EXPECT_EQ(formatIDBit(static_cast<FormatID>(2)), 4ull);
}

} // namespace
