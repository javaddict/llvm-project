//===- HaydnAIEParityBundleTest.cpp - AIE-shaped bundle/format pins -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Extensive AIE-parity unit tests for Haydn product packet formats.
// Inspired by llvm-aie unittests/Target/AIE/BundleTest.cpp patterns:
//   empty accept, multi-slot fill, format coverage, clear, meta no-slot,
//   reserve vs add occupancy agreement.
//
// Product: one live format row. Infra must stay N-format-ready
// (FormatID / Plan / PacketFormats APIs — not hard-coded "always pack").
//
//===----------------------------------------------------------------------===//

#include "HaydnBundle.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCInst.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <iterator>

// Opcode enums come via HaydnPortModel → HaydnMCTargetDesc (GET_INSTRINFO_ENUM).
// Do not re-include the enum; a second include conflicts.

using namespace llvm;
using namespace llvm::Haydn;
using namespace llvm::haydn::bundle;

namespace {

//===----------------------------------------------------------------------===//
// AIE BundleTest-style contracts on real Haydn opcodes
//===----------------------------------------------------------------------===//

TEST(HaydnAIEParityBundleTest, EmptyAcceptsAnySupported) {
  // AIE: Empty bundles can accept any instruction (BundleTest Construct).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  EXPECT_TRUE(B.empty());
  EXPECT_TRUE(B.canAdd(Haydn::ADD32));
  EXPECT_TRUE(B.canAdd(Haydn::LD32));
  EXPECT_TRUE(B.canAdd(Haydn::ST32));
  EXPECT_TRUE(B.canAdd(Haydn::X2MULA32));
  EXPECT_TRUE(B.canAdd(Haydn::ADD64));
  EXPECT_TRUE(B.canAdd(Haydn::ARCTAN));
}

TEST(HaydnAIEParityBundleTest, MetaDoesNotConsumeSlots) {
  // AIE: meta / BUNDLE root do not occupy slots.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst Meta, Alu;
  Meta.setOpcode(TargetOpcode::IMPLICIT_DEF);
  Alu.setOpcode(Haydn::ADD32);
  B.add(&Meta);
  EXPECT_TRUE(B.empty()) << "meta must not count as Instrs for empty()";
  EXPECT_EQ(B.getOccupiedSlots(), 0u);
  B.add(&Alu);
  EXPECT_EQ(B.size(), 1u);
  EXPECT_NE(B.getOccupiedSlots(), 0u);
}

TEST(HaydnAIEParityBundleTest, FormatAvailableAllSubsetsLikeAIEPacket) {
  // Format E PacketFormats: E2 (0x3) and E3 (0x1c) entry geometries.
  // Residual issue-slot subsets are admitted via productCovers, not LUT.
  HaydnMCFormats Fmts;
  EXPECT_TRUE(Fmts.isFormatAvailable(0));
  EXPECT_TRUE(Fmts.isFormatAvailable(0x3));
  EXPECT_TRUE(Fmts.isFormatAvailable(0x1c));
  EXPECT_TRUE(productCovers(Fmts.getPacketFormats(), SLOT_ALL));
}

TEST(HaydnAIEParityBundleTest, PacketFormatNameAndSizeAIEShape) {
  HaydnMCFormats Fmts;
  const VLIWFormat *F = productVLIWFormat(Fmts.getPacketFormats());
  ASSERT_NE(F, nullptr);
  EXPECT_TRUE(StringRef(F->Name).starts_with("BUNDLE_E96_")) << F->Name;
  // Product table Size = EncodedBytes (AIE VLIWFormat::Size is packet size unit).
  EXPECT_EQ(vliwFormatSizeAsBytes(F->getSize()), productParcelBytes());
  // Residual issue occupancy is product-covered (transitional packing).
  EXPECT_TRUE(productCovers(Fmts.getPacketFormats(), SLOT0));
  EXPECT_TRUE(productCovers(Fmts.getPacketFormats(), SLOT1 | SLOT2));
  EXPECT_TRUE(productCovers(Fmts.getPacketFormats(), SLOT_ALL));
}

TEST(HaydnAIEParityBundleTest, ThreeIssueFillThenRejectLikeAIE) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst Ops[4];
  for (int I = 0; I < 3; ++I) {
    Ops[I].setOpcode(Haydn::ADD32);
    ASSERT_TRUE(B.canAdd(Ops[I].getOpcode()));
    B.add(&Ops[I]);
  }
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(SLOT_ALL));
  EXPECT_TRUE(B.hasValidFormat());
  Ops[3].setOpcode(Haydn::ADD32);
  EXPECT_FALSE(B.canAdd(Ops[3].getOpcode()));
}

TEST(HaydnAIEParityBundleTest, ClearReturnsToEmptyAccept) {
  // AIE Bundle clear resets occupancy.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, X;
  A.setOpcode(Haydn::ADD32);
  X.setOpcode(Haydn::XOR32);
  B.add(&A);
  B.add(&X);
  B.clear();
  EXPECT_TRUE(B.empty());
  EXPECT_EQ(B.getOccupiedSlots(), 0u);
  EXPECT_TRUE(B.canAdd(Haydn::ST32));
}

TEST(HaydnAIEParityBundleTest, SlotMapReflectsPlacement) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St, Alu;
  St.setOpcode(Haydn::ST32);
  Alu.setOpcode(Haydn::ADD32);
  B.add(&St);
  B.add(&Alu);
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_S0), &St);
  // ST is S0-only; multi-slot ALU prefers high slots (S2 then S1).
  EXPECT_TRUE(B.at(MCSlotKind::Haydn_SLOT_S1) == &Alu ||
              B.at(MCSlotKind::Haydn_SLOT_S2) == &Alu)
      << "ALU must land S1 or S2; occ=" << B.getOccupiedSlots();
  EXPECT_NE(B.getOccupiedSlots() & (Haydn::SLOT1 | Haydn::SLOT2), 0u);
}

//===----------------------------------------------------------------------===//
// FormatID / BundlePlan N-format-ready APIs (single live row)
//===----------------------------------------------------------------------===//

TEST(HaydnAIEParityBundleTest, EveryPackedCycleIsProductPlan) {
  HaydnMCFormats Fmts;
  const unsigned Sequences[][4] = {
      {Haydn::ADD32, 0, 0, 0},
      {Haydn::ADD32, Haydn::XOR32, 0, 0},
      {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32, 0},
      {Haydn::LD32, Haydn::X2MULA32, 0, 0},
      {Haydn::ST32, Haydn::ADD32, 0, 0},
      {Haydn::LD32, Haydn::LD32, Haydn::ADD32, 0},
      {Haydn::ST32, Haydn::ST32, 0, 0}, // split
  };
  for (const auto &Seq : Sequences) {
    SmallVector<unsigned, 4> Ops;
    for (int I = 0; I < 4 && Seq[I] != 0; ++I)
      Ops.push_back(Seq[I]);
    auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
    for (const OpcodeCycle &C : Cycles) {
      EXPECT_EQ(C.Plan.Bytes.Value, productParcelBytes().Value);
      EXPECT_TRUE(C.Plan.isProductLegal());
      EXPECT_TRUE(isProductBundleRow(C.Plan.Row));
      auto B = encodedBytesForRow(C.Plan.Row);
      ASSERT_TRUE(B.has_value());
      EXPECT_EQ(*B, productParcelBytes());
    }
  }
}

TEST(HaydnAIEParityBundleTest, PlanFromPacketFormatsMatchesLiveRow) {
  HaydnMCFormats Fmts;
  for (SlotBits Occ :
       {SlotBits(0), SlotBits(SLOT0), SlotBits(SLOT1 | SLOT2),
        SlotBits(SLOT_ALL)}) {
    auto P = planFromPacketFormats(Fmts.getPacketFormats(), Occ);
    ASSERT_TRUE(P.has_value()) << "occ=" << Occ;
    // FE8: ProductFormatID alias deleted; product identity is the stamped
    // BundleFormatRowID (E96TwoEntry / E96ThreeEntry) via stampBundleCommit.
    EXPECT_TRUE(isProductBundleRow(P->Row));
    EXPECT_EQ(P->Bytes.Value, productParcelBytes().Value);
  }
}

//===----------------------------------------------------------------------===//
// DSP classic packs (AIE multi-slot density class)
//===----------------------------------------------------------------------===//

TEST(HaydnAIEParityBundleTest, DspKernelLdMacAluDensity) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::LD32, Haydn::X2MULA32, Haydn::ADD32};
  EXPECT_TRUE(opcodesFormOneLegalCycle(Ops, Fmts));
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  ASSERT_EQ(Cycles.size(), 1u);
  EXPECT_EQ(Cycles[0].Plan.OccupiedSlots, SlotBits(SLOT_ALL));
}

TEST(HaydnAIEParityBundleTest, DualLoadMacFill) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst L0, L1, Mac;
  L0.setOpcode(Haydn::LD32);
  L1.setOpcode(Haydn::LD32);
  Mac.setOpcode(Haydn::X2MULA32);
  B.add(&L0);
  ASSERT_TRUE(B.canAdd(L1.getOpcode()));
  B.add(&L1);
  ASSERT_TRUE(B.canAdd(Mac.getOpcode()));
  B.add(&Mac);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(SLOT_ALL));
  EXPECT_TRUE(B.hasValidFormat());
}

TEST(HaydnAIEParityBundleTest, StoreStoreMustSplitLikeResourceConflict) {
  // AIE-style: same exclusive resource → not one bundle.
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::ST32, Haydn::ST32};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  EXPECT_EQ(Cycles.size(), 3u);
  for (const auto &C : Cycles)
    EXPECT_EQ(C.Opcodes.size(), 1u);
}

//===----------------------------------------------------------------------===//
// Stress: long mixed stream partition (AIE schedule density)
//===----------------------------------------------------------------------===//

TEST(HaydnAIEParityBundleTest, LongMixedStreamPartitionInvariant) {
  HaydnMCFormats Fmts;
  // Realistic FIR-ish stream: LD LD MAC ALU ST LD MAC ALU ST ...
  const unsigned Pattern[] = {
      Haydn::LD32, Haydn::LD32, Haydn::X2MULA32, Haydn::ADD32, Haydn::ST32,
      Haydn::LD32, Haydn::X2MULA32, Haydn::XOR32, Haydn::ST32,
      Haydn::ADD64, Haydn::SLL64, Haydn::ST32,
      Haydn::NOT32, Haydn::NEG32, Haydn::ADDI32, Haydn::ADD32};
  SmallVector<unsigned, 32> Ops(std::begin(Pattern), std::end(Pattern));
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  SmallVector<unsigned, 32> Flat;
  size_t MaxMembers = 0;
  for (const OpcodeCycle &C : Cycles) {
    EXPECT_TRUE(C.Plan.isProductLegal());
    EXPECT_LE(C.Opcodes.size(), 3u);
    MaxMembers = std::max(MaxMembers, C.Opcodes.size());
    Flat.append(C.Opcodes.begin(), C.Opcodes.end());
    // Self-legal subcycle (AIE: committed bundle is valid format).
    EXPECT_TRUE(opcodesFormOneLegalCycle(C.Opcodes, Fmts));
  }
  ASSERT_EQ(Flat.size(), Ops.size());
  for (size_t I = 0; I < Ops.size(); ++I)
    EXPECT_EQ(Flat[I], Ops[I]);
  EXPECT_GE(MaxMembers, 2u) << "stream should pack at least some multi-issue";
  EXPECT_GE(Cycles.size(), 4u);
}

TEST(HaydnAIEParityBundleTest, ResplitFixedPointOnAllSubcycles) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::LD32, Haydn::X2MULA32, Haydn::ST32,
                    Haydn::ADD32, Haydn::ADD64, Haydn::ST32, Haydn::XOR32};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  for (const OpcodeCycle &C : Cycles) {
    auto Again = greedySplitLegalOpcodeCycles(C.Opcodes, Fmts);
    ASSERT_EQ(Again.size(), 1u);
    ASSERT_EQ(Again[0].Opcodes.size(), C.Opcodes.size());
    for (size_t I = 0; I < C.Opcodes.size(); ++I)
      EXPECT_EQ(Again[0].Opcodes[I], C.Opcodes[I]);
  }
}

//===----------------------------------------------------------------------===//
// N-format-ready: only one product FormatID today
//===----------------------------------------------------------------------===//

TEST(HaydnAIEParityBundleTest, ProductFormatIdIsSingletonEnum) {
  // Product identity is BundleFormatRowID E96TwoEntry / E96ThreeEntry.
  // FE8: the legacy full-width FormatID enum + isProductFormat(FormatID)
  // helper were deleted; Format E rows are the sole product authority.
  using haydn::format::BundleFormatRowID;
  EXPECT_TRUE(isProductBundleRow(BundleFormatRowID::E96TwoEntry));
  EXPECT_TRUE(isProductBundleRow(BundleFormatRowID::E96ThreeEntry));
  EXPECT_EQ(haydn::format::encodedBytesOrDie(BundleFormatRowID::E96TwoEntry)
                .Value,
            productParcelBytes().Value);
}

TEST(HaydnAIEParityBundleTest, SlotWindowBitsSum128AIEComposite) {
  // AIE composite size from residual slot field geometry (48+40+40 = 128).
  // FE8: removed the named Slot0/1/2EncodedBits / full-width EncodedBits
  // constants; the slot widths are queried from the registry slot info table.
  HaydnMCFormats Fmts;
  unsigned Sum = 0;
  for (auto K : {MCSlotKind::Haydn_SLOT_S0, MCSlotKind::Haydn_SLOT_S1,
                 MCSlotKind::Haydn_SLOT_S2})
    Sum += Fmts.getSlotInfo(K)->getSize();
  EXPECT_EQ(Sum, 128u);
}

//===----------------------------------------------------------------------===//
// MC serialize-only (AIEBaseAsmPrinter.cpp:161-164
// AIEBaseMCCodeEmitter.cpp:45-68 peers)
//===----------------------------------------------------------------------===//

TEST(HaydnAIEParityBundleTest, FormatOpcodeIsProductCompositeSerializeOnly) {
  // AIE: MCBundle.setOpcode(Format->Opcode); emitter getBinaryCode + emit.
  // Haydn product: Format->Opcode is BUNDLE_E96_* (not legacy full-width).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, X, N;
  A.setOpcode(Haydn::ADD32_E3_E0_ALU0_RR);
  X.setOpcode(Haydn::ADD32_E3_E1_ALU1_RR);
  N.setOpcode(Haydn::ADD32_E3_E2_ALU2_RR);
  ASSERT_NE(Fmts.getSlotKind(A.getOpcode()), MCSlotKind());
  ASSERT_NE(Fmts.getSlotKind(X.getOpcode()), MCSlotKind());
  ASSERT_NE(Fmts.getSlotKind(N.getOpcode()), MCSlotKind());
  ASSERT_TRUE(B.canAdd(A.getOpcode()));
  B.add(&A);
  ASSERT_TRUE(B.canAdd(X.getOpcode()));
  B.add(&X);
  ASSERT_TRUE(B.canAdd(N.getOpcode()));
  B.add(&N);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96_")) << Fmt->Name;
  EXPECT_TRUE(Fmt->Opcode == Haydn::BUNDLE_E96_TWO_ENTRY ||
              Fmt->Opcode == Haydn::BUNDLE_E96_THREE_ENTRY)
      << "AsmPrinter must emit Format E composite, not legacy full-width";
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96"));

  // Committed members keep fixed getSlotKind — encode must not re-auction.
  // REBASED 2026-08-21: SlotMap keys are residual S* FieldSlots kinds
  // (E3_0→S0, E3_1→S1, E3_2→S2 via issueFieldSlotsForCommittedKind); the
  // entry kinds stay on the Format.getSlots()/getSlotKind axis.
  const MCInst *AtS0 = B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_S0));
  const MCInst *AtS1 = B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_S1));
  const MCInst *AtS2 = B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_S2));
  ASSERT_NE(AtS0, nullptr);
  ASSERT_NE(AtS1, nullptr);
  ASSERT_NE(AtS2, nullptr);
  EXPECT_NE(Fmts.getSlotKind(AtS0->getOpcode()), MCSlotKind());
  EXPECT_NE(Fmts.getSlotKind(AtS1->getOpcode()), MCSlotKind());
  EXPECT_NE(Fmts.getSlotKind(AtS2->getOpcode()), MCSlotKind());
}

TEST(HaydnAIEParityBundleTest, SerializeSlotMapNoReAuctionOnMembers) {
  // Source order that would starve under wrong re-auction still packs by
  // Desc getSlotKind (members already placed). Second E0-only store fails
  // canAdd — no constrained-first re-auction escape.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St0, St1;
  St0.setOpcode(Haydn::S_SW_WITH_IMM_E3_E0_LOADSTORE0_RI6);
  St1.setOpcode(Haydn::S_SW_WITH_IMM_E3_E0_LOADSTORE0_RI6);
  ASSERT_TRUE(B.canAdd(St0.getOpcode()));
  B.add(&St0);
  EXPECT_FALSE(B.canAdd(St1.getOpcode()))
      << "second fixed E0 store member must fail (no encode re-auction)";
  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96_")) << Fmt->Name;
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96"));
}

} // namespace
