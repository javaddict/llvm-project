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
//   * hwloop::ProductParcelBytes / ProductFormatDesc.Bytes / productParcelBytes
//     are one EncodedBytes oracle (AIE Format->getSize peer)
//   * ceilProductParcels / productBundlesToBytes for Fixup + HardwareLoops
//   * FormatID imm encode/decode (durable BUNDLE-root contract)
//
//===----------------------------------------------------------------------===//

#include "HaydnBundlePlan.h"
#include "HaydnHWLoopContracts.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "HaydnTestMCInstrInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

TEST(HaydnBundlePlanTest, ProductConstants) {
  // The one place a literal belongs: this pins the architectural fact rather
  // than restating a constant. 12 bytes / 96 bits, and note 12 is not a power
  // of two, which is why alignment is requested as 4 (5.9).
  EXPECT_EQ(ProductEncodedBytesValue, 12u);
  EXPECT_EQ(ProductEncodedBitsValue, 96u);
  EXPECT_EQ(ProductEncodedBytes.Value, ProductEncodedBytesValue);
  EXPECT_EQ(ProductEncodedBits.Value, ProductEncodedBitsValue);
  // Bundle128 had ONE composite, so "the product format" named a single row.
  // Format E has two and they are the same size, which is what lets a single
  // default survive at all; both are product formats.
  EXPECT_EQ(ProductFormatID, FormatID::BundleE2);
  EXPECT_TRUE(isProductFormat(FormatID::BundleE2));
  EXPECT_TRUE(isProductFormat(FormatID::BundleE3));
  EXPECT_EQ(encodedBytesFor(FormatID::BundleE2),
            encodedBytesFor(FormatID::BundleE3));
}

TEST(HaydnBundlePlanTest, EncodedBytesBitsNotAliased) {
  // Plan §3.5: never treat bytes as bits or vice versa.
  EncodedBytes B = ProductEncodedBytes;
  EncodedBits Bits = ProductEncodedBits;
  EXPECT_NE(B.Value, Bits.Value);
  EXPECT_EQ(vliwFormatSizeAsBits(B.Value).Value, Bits.Value);
  EXPECT_EQ(vliwFormatSizeAsBytes(16u).Value, 16u);
  EXPECT_EQ(slotInfoSizeAsBits(48u).Value, 48u);
}

TEST(HaydnBundlePlanTest, SlotWindowsAccountForEveryBundleBit) {
  // Bundle128's 48/40/40 summed to 128 exactly, so the invariant could be
  // "the windows ARE the word". Format E's do not tile it: 45+41 leaves four
  // bits unused and 31+31+27 leaves one, over a payload that starts at bit 6.
  // The invariant that survives is per composite and includes both the header
  // and the slack -- rescaling the old sum would have been wrong rather than
  // merely stale (FORMAT-E-SWITCH-PLAN.md 5.2).
  EXPECT_EQ(BundleEHeaderBits + P20EncodedBits.Value + P21EncodedBits.Value +
                BundleE2UnusedBits,
            ProductEncodedBitsValue);
  EXPECT_EQ(BundleEHeaderBits + P30EncodedBits.Value + P31EncodedBits.Value +
                P32EncodedBits.Value + BundleE3UnusedBits,
            ProductEncodedBitsValue);

  EXPECT_EQ(P20EncodedBits.Value, 45u);
  EXPECT_EQ(P21EncodedBits.Value, 41u);
  EXPECT_EQ(P30EncodedBits.Value, 31u);
  EXPECT_EQ(P31EncodedBits.Value, 31u);
  EXPECT_EQ(P32EncodedBits.Value, 27u);

  // The 3-entry form is not the 2-entry form subdivided: it buys a third
  // entry by making all of them narrower, which is why an instruction can
  // have a placement in one and not the other.
  EXPECT_LT(P30EncodedBits.Value, P20EncodedBits.Value);
}

TEST(HaydnBundlePlanTest, HwloopBytesMatchesPlanAuthority) {
  // Single EncodedBytes oracle — no dual hard-coded 16.
  EXPECT_EQ(static_cast<unsigned>(haydn::hwloop::ProductParcelBytes),
            ProductEncodedBytesValue);
  EXPECT_EQ(static_cast<unsigned>(haydn::hwloop::ProductParcelBytes),
            productParcelBytes().Value);
  EXPECT_EQ(static_cast<unsigned>(haydn::hwloop::ProductParcelBytes),
            ProductFormatDesc.Bytes.Value);
  auto FromID = encodedBytesFor(FormatID::BundleE3);
  ASSERT_TRUE(FromID.has_value());
  EXPECT_EQ(static_cast<unsigned>(haydn::hwloop::ProductParcelBytes),
            FromID->Value);
  EXPECT_EQ(haydn::hwloop::MinSetupBytes,
            productBundlesToBytes(haydn::hwloop::MinSetupBundles));
  EXPECT_EQ(haydn::hwloop::MinSetupBytes, 48);
}

TEST(HaydnBundlePlanTest, EncodedBytesForProductFormat) {
  auto B = encodedBytesFor(FormatID::BundleE3);
  ASSERT_TRUE(B.has_value());
  EXPECT_EQ(B->Value, ProductEncodedBytesValue);
  EXPECT_EQ(encodedBytesOrProduct(FormatID::BundleE3),
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
  EXPECT_EQ(productParcelBytes(), ProductEncodedBytes);
  EXPECT_EQ(ProductFormatDesc.FID, FormatID::BundleE3);
}

TEST(HaydnBundlePlanTest, MakeBundle128PlanIsProductLegal) {
  unsigned Members[] = {Haydn::ADD32, Haydn::S_LW_WITH_IMM};
  BundlePlan P = makeProductPlan(Haydn::SLOT_P30 | Haydn::SLOT_P32, Members);
  EXPECT_TRUE(P.isProductLegal());
  EXPECT_EQ(P.FID, FormatID::BundleE3);
  EXPECT_EQ(P.Bytes.Value, ProductEncodedBytesValue);
  EXPECT_EQ(P.Cycles.Value, 1u);
  EXPECT_EQ(P.memberCount(), 2u);
  EXPECT_EQ(P.OccupiedSlots, SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P32));
  EXPECT_FALSE(P.empty());
}

TEST(HaydnBundlePlanTest, StallPlanIsProductLegalEmptyMembers) {
  BundlePlan Stall = makeStallPlan();
  EXPECT_TRUE(Stall.isProductLegal());
  EXPECT_TRUE(Stall.empty());
  EXPECT_EQ(Stall.OccupiedSlots, 0u);
  EXPECT_EQ(Stall.Bytes.Value, ProductEncodedBytesValue) << "idle cycle still emits Bundle128 NOP parcel";
}

TEST(HaydnBundlePlanTest, RejectsTooManyMembers) {
  BundlePlan P = makeProductPlan(Haydn::SLOT_SET_E3);
  P.MemberOpcodes = {1, 2, 3, 4}; // > ISSUE_SLOT_COUNT
  EXPECT_FALSE(P.isProductLegal());
}

TEST(HaydnBundlePlanTest, GeneratedPacketFormatSizeIsEncodedBytes) {
  // Generated VLIWFormat row for BUNDLE_E3: Size==16 means EncodedBytes.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const PacketFormats &Packets = Fmts.getPacketFormats();
  const VLIWFormat *Full =
      Packets.getFormat(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32);
  ASSERT_NE(Full, nullptr);
  EncodedBytes B = vliwFormatSizeAsBytes(Full->getSize());
  EXPECT_EQ(B, ProductEncodedBytes);
  EncodedBits Bits = vliwFormatSizeAsBits(Full->getSize());
  EXPECT_EQ(Bits, ProductEncodedBits);
  // Name pin.
  EXPECT_STREQ(Full->Name, "BUNDLE_E3");
}

TEST(HaydnBundlePlanTest, GeneratedSlotInfoSizeIsEncodedBits) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const MCSlotInfo *S0 = Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_P30);
  const MCSlotInfo *S1 = Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_P31);
  const MCSlotInfo *S2 = Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_P32);
  ASSERT_NE(S0, nullptr);
  ASSERT_NE(S1, nullptr);
  ASSERT_NE(S2, nullptr);
  // MCSlotInfo::getSize() — bits (public getter).
  EXPECT_EQ(slotInfoSizeAsBits(S0->getSize()), P30EncodedBits);
  EXPECT_EQ(slotInfoSizeAsBits(S1->getSize()), P31EncodedBits);
  EXPECT_EQ(slotInfoSizeAsBits(S2->getSize()), P32EncodedBits);
  EXPECT_EQ(S0->getSize() + S1->getSize() + S2->getSize(),
            ProductEncodedBitsValue);
}

TEST(HaydnBundlePlanTest, PlanFromPacketFormatsAllSubsets) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const PacketFormats &Packets = Fmts.getPacketFormats();
  for (SlotBits Combo = 0; Combo <= Haydn::SLOT_SET_E3; ++Combo) {
    auto Plan = planFromPacketFormats(Packets, Combo);
    ASSERT_TRUE(Plan.has_value()) << "combo=" << Combo;
    EXPECT_TRUE(Plan->isProductLegal()) << "combo=" << Combo;
    EXPECT_EQ(Plan->OccupiedSlots, Combo);
    EXPECT_EQ(Plan->Bytes.Value, ProductEncodedBytesValue);
  }
}

TEST(HaydnBundlePlanTest, CycleCountIsNotBytes) {
  // Architectural setup distance is in cycles; byte math multiplies separately.
  CycleCount Setup = CycleCount{haydn::hwloop::MinSetupBundles};
  EncodedBytes SetupBytes{
      Setup.Value * ProductEncodedBytesValue};
  EXPECT_EQ(Setup.Value, 3u);
  EXPECT_EQ(SetupBytes.Value, 48u);
  EXPECT_NE(Setup.Value, SetupBytes.Value);
}

//===----------------------------------------------------------------------===//
// Extended plan / size authority
//===----------------------------------------------------------------------===//

TEST(HaydnBundlePlanTest, MakePlanCopiesMembersInOrder) {
  unsigned Members[] = {Haydn::S_LW_WITH_IMM, Haydn::X2MULA32, Haydn::ADD32};
  BundlePlan P = makeProductPlan(Haydn::SLOT_SET_E3, Members);
  ASSERT_EQ(P.MemberOpcodes.size(), 3u);
  EXPECT_EQ(P.MemberOpcodes[0], Haydn::S_LW_WITH_IMM);
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
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const PacketFormats &Packets = Fmts.getPacketFormats();
  for (SlotBits Combo : {SlotBits(0), SlotBits(Haydn::SLOT_P30),
                         SlotBits(Haydn::SLOT_P31 | Haydn::SLOT_P32),
                         SlotBits(Haydn::SLOT_SET_E3)}) {
    auto FromTable = planFromPacketFormats(Packets, Combo);
    ASSERT_TRUE(FromTable.has_value());
    BundlePlan Hand = makeProductPlan(Combo);
    EXPECT_EQ(FromTable->FID, Hand.FID);
    EXPECT_EQ(FromTable->Bytes, Hand.Bytes);
    EXPECT_EQ(FromTable->Cycles, Hand.Cycles);
    EXPECT_EQ(FromTable->OccupiedSlots, Hand.OccupiedSlots);
  }
}

TEST(HaydnBundlePlanTest, SlotBitWidthsMatchGeneratedSlotInfo) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  EXPECT_EQ(Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_P30)->getSize(),
            P30EncodedBits.Value);
  EXPECT_EQ(Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_P31)->getSize(),
            P31EncodedBits.Value);
  EXPECT_EQ(Fmts.getSlotInfo(MCSlotKind::Haydn_SLOT_P32)->getSize(),
            P32EncodedBits.Value);
}

TEST(HaydnBundlePlanTest, Off1SafetyMarginIsThreeParcels) {
  // hwloop contracts: 3 * 16 = 48 — must track ProductEncodedBytes.
  EXPECT_EQ(haydn::hwloop::Off1SafetyMarginBytes,
            static_cast<int64_t>(3 * ProductEncodedBytesValue));
  EXPECT_EQ(haydn::hwloop::Off1SafetyMarginBundles, 3);
}

//===----------------------------------------------------------------------===//
// Durable FormatID imm on BUNDLE roots (plan §6.3)
//===----------------------------------------------------------------------===//
// Mirrors AIE format identity after finalizeBundle (AIEHazardRecognizer.cpp:
// 278-312; AIEBundle.h:150-156 getFormatOrNull). Haydn stores FormatID as an
// imm so multi-format tables need no MF side map. Product Full = imm 0.

TEST(HaydnBundlePlanTest, FormatIDImmProductIsZero) {
  EXPECT_EQ(formatIDToImm(FormatID::BundleE3), 0u);
  EXPECT_EQ(formatIDToImm(ProductFormatID), 0u);
  auto Decoded = formatIDFromImm(0u);
  ASSERT_TRUE(Decoded.has_value());
  EXPECT_EQ(*Decoded, FormatID::BundleE3);
  EXPECT_TRUE(isKnownFormatIDImm(0u));
}

TEST(HaydnBundlePlanTest, FormatIDImmUnknownRejected) {
  // N-format-ready: unknown encodings must not silently become Full.
  EXPECT_FALSE(formatIDFromImm(1u).has_value());
  EXPECT_FALSE(formatIDFromImm(0xffffffffu).has_value());
  EXPECT_FALSE(isKnownFormatIDImm(1u));
}

TEST(HaydnBundlePlanTest, FormatIDImmRoundTrip) {
  for (FormatID ID : {FormatID::BundleE3}) {
    unsigned Imm = formatIDToImm(ID);
    auto Back = formatIDFromImm(Imm);
    ASSERT_TRUE(Back.has_value());
    EXPECT_EQ(*Back, ID);
    auto Bytes = encodedBytesFor(ID);
    ASSERT_TRUE(Bytes.has_value());
    EXPECT_EQ(Bytes->Value, ProductEncodedBytesValue);
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
  EXPECT_EQ(*ID, FormatID::BundleE3);
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
  EXPECT_EQ(Bytes->Value, ProductEncodedBytesValue);
  // Singleton cycle is still one architectural cycle / one 16 B parcel.
  BundlePlan Single = makeProductPlan(Haydn::SLOT_P30, {Haydn::ADD32});
  EXPECT_TRUE(Single.isProductLegal());
  EXPECT_EQ(Single.memberCount(), 1u);
  EXPECT_EQ(Single.Bytes.Value, ProductEncodedBytesValue);
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
  EXPECT_EQ(ProductFormatDesc.FID, FormatID::BundleE3);
  EXPECT_EQ(ProductFormatDesc.Priority, 0u);
  EXPECT_EQ(ProductFormatDesc.Bytes, ProductEncodedBytes);
  EXPECT_EQ(ProductFormatDesc.SlotSet,
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));
  EXPECT_TRUE(ProductFormatDesc.isProduct());
  EXPECT_TRUE(ProductFormatDesc.covers(0));
  EXPECT_TRUE(ProductFormatDesc.covers(Haydn::SLOT_P30));
  EXPECT_TRUE(ProductFormatDesc.covers(Haydn::SLOT_SET_E3));

  ArrayRef<FormatDesc> Product = productFormatTable();
  ASSERT_EQ(Product.size(), 1u);
  const FormatDesc *Sel =
      selectFormatByPriority(Product, Haydn::SLOT_P30 | Haydn::SLOT_P32);
  ASSERT_NE(Sel, nullptr);
  EXPECT_EQ(Sel->FID, FormatID::BundleE3);
  EXPECT_EQ(Sel->Bytes.Value, ProductEncodedBytesValue);
}

TEST(HaydnBundlePlanTest, SelectFormatByPriority_SyntheticTwoRow) {
  // Unit-only second format (not product encode). Narrow prefers lower Priority
  // when it covers; Full is wide fallback with higher Priority number.
  constexpr FormatID SynthNarrow = static_cast<FormatID>(1);
  const FormatDesc Table[] = {
      // Priority 0: 8 B, SLOT_P30|SLOT_P31 only (synthetic compact).
      {SynthNarrow, /*Priority=*/0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT_P30 | Haydn::SLOT_P31)},
      // Priority 1: product Full wide fallback.
      {FormatID::BundleE3, /*Priority=*/1, ProductEncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT_SET_E3)},
  };

  // SLOT_P30 only → Narrow covers and wins on Priority.
  const FormatDesc *A = selectFormatByPriority(Table, Haydn::SLOT_P30);
  ASSERT_NE(A, nullptr);
  EXPECT_EQ(A->FID, SynthNarrow);
  EXPECT_EQ(A->Bytes.Value, 8u);

  // SLOT_P30|SLOT_P31 → still Narrow.
  const FormatDesc *B =
      selectFormatByPriority(Table, Haydn::SLOT_P30 | Haydn::SLOT_P31);
  ASSERT_NE(B, nullptr);
  EXPECT_EQ(B->FID, SynthNarrow);

  // SLOT_P32 only → Narrow does not cover; Full wins.
  const FormatDesc *C = selectFormatByPriority(Table, Haydn::SLOT_P32);
  ASSERT_NE(C, nullptr);
  EXPECT_EQ(C->FID, FormatID::BundleE3);
  EXPECT_EQ(C->Bytes.Value, ProductEncodedBytesValue);

  // All three slots → Full only.
  const FormatDesc *D = selectFormatByPriority(Table, Haydn::SLOT_SET_E3);
  ASSERT_NE(D, nullptr);
  EXPECT_EQ(D->FID, FormatID::BundleE3);

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
       static_cast<SlotBits>(Haydn::SLOT_P30)},
      {SynthB, /*Priority=*/5, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT_P30)},
  };
  const FormatDesc *Sel = selectFormatByPriority(Table, Haydn::SLOT_P30);
  ASSERT_NE(Sel, nullptr);
  EXPECT_EQ(Sel->FID, SynthA);
}

TEST(HaydnBundlePlanTest, SelectFormatByPriority_NoCoverReturnsNull) {
  const FormatDesc Table[] = {
      {FormatID::BundleE3, 0, ProductEncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT_P30)}, // only S0
  };
  EXPECT_EQ(selectFormatByPriority(Table, Haydn::SLOT_P31), nullptr);
  EXPECT_EQ(selectFormatByPriority(Table, Haydn::SLOT_P30 | Haydn::SLOT_P31),
            nullptr);
}

TEST(HaydnBundlePlanTest, PlanFromFormatTable_ProductAndSynthetic) {
  // Product table always yields Full / 16 B.
  auto Prod = planFromFormatTable(productFormatTable(), Haydn::SLOT_SET_E3);
  ASSERT_TRUE(Prod.has_value());
  EXPECT_TRUE(Prod->isProductLegal());
  EXPECT_EQ(Prod->FID, FormatID::BundleE3);
  EXPECT_EQ(Prod->Bytes.Value, ProductEncodedBytesValue);

  constexpr FormatID SynthNarrow = static_cast<FormatID>(1);
  const FormatDesc Multi[] = {
      {SynthNarrow, 0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT_P30 | Haydn::SLOT_P31)},
      ProductFormatDesc,
  };
  auto NarrowPlan = planFromFormatTable(Multi, Haydn::SLOT_P30, {Haydn::ADD32});
  ASSERT_TRUE(NarrowPlan.has_value());
  EXPECT_EQ(NarrowPlan->FID, SynthNarrow);
  EXPECT_EQ(NarrowPlan->Bytes.Value, 8u);
  EXPECT_FALSE(NarrowPlan->isProductLegal())
      << "synthetic format must not pass product legality";
  EXPECT_EQ(NarrowPlan->memberCount(), 1u);

  auto FullPlan = planFromFormatTable(Multi, Haydn::SLOT_P32);
  ASSERT_TRUE(FullPlan.has_value());
  EXPECT_EQ(FullPlan->FID, FormatID::BundleE3);
  EXPECT_TRUE(FullPlan->isProductLegal());
}

TEST(HaydnBundlePlanTest, FormatIDBitAndProductMask) {
  EXPECT_EQ(formatIDBit(FormatID::BundleE3), 1ull);
  EXPECT_EQ(ProductFormatMask, 1ull);
  EXPECT_EQ(formatIDBit(static_cast<FormatID>(1)), 2ull);
  EXPECT_EQ(formatIDBit(static_cast<FormatID>(2)), 4ull);
}

} // namespace
