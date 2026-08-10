//===- HaydnAIEParityBundleTest.cpp - AIE-shaped bundle/format pins -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Extensive AIE-parity unit tests for Haydn Bundle128 (G-BUNDLE-FORMAT).
// Inspired by llvm-aie unittests/Target/AIE/BundleTest.cpp patterns:
//   empty accept, multi-slot fill, format coverage, clear, meta no-slot,
//   reserve vs add occupancy agreement.
//
// Product: one live format BUNDLE_E3. Infra must stay N-format-ready
// (FormatID / Plan / PacketFormats APIs — not hard-coded "always pack").
//
//===----------------------------------------------------------------------===//

#include "HaydnBundle.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "HaydnTestMCInstrInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCInst.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <iterator>

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::Haydn;
using namespace llvm::haydn::bundle;

namespace {

//===----------------------------------------------------------------------===//
// AIE BundleTest-style contracts on real Haydn opcodes
//===----------------------------------------------------------------------===//

TEST(HaydnAIEParityBundleTest, EmptyAcceptsAnySupported) {
  // AIE: Empty bundles can accept any instruction (BundleTest Construct).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  Bundle<MCInst> B(&Fmts);
  EXPECT_TRUE(B.empty());
  EXPECT_TRUE(B.canAdd(Haydn::ADD32));
  EXPECT_TRUE(B.canAdd(Haydn::S_LW_WITH_IMM));
  EXPECT_TRUE(B.canAdd(Haydn::S_SW_WITH_IMM));
  EXPECT_TRUE(B.canAdd(Haydn::X2MULA32));
  EXPECT_TRUE(B.canAdd(Haydn::ADD64));
  EXPECT_TRUE(B.canAdd(Haydn::ARCTAN));
}

TEST(HaydnAIEParityBundleTest, MetaDoesNotConsumeSlots) {
  // AIE: meta / BUNDLE root do not occupy slots.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
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
  // AIE PacketFormats: subsets covered by composite. Bundle128 = all 8.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  for (SlotBits Combo = 0; Combo <= SLOT_SET_E3; ++Combo)
    EXPECT_TRUE(Fmts.isFormatAvailable(Combo)) << "combo=" << Combo;
}

TEST(HaydnAIEParityBundleTest, PacketFormatNameAndSizeAIEShape) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const VLIWFormat *F =
      Fmts.getPacketFormats().getFormat(SLOT_P30 | SLOT_P31 | SLOT_P32);
  ASSERT_NE(F, nullptr);
  EXPECT_STREQ(F->Name, "BUNDLE_E3");
  // Product table Size = EncodedBytes (AIE VLIWFormat::Size is packet size unit).
  EXPECT_EQ(vliwFormatSizeAsBytes(F->getSize()), ProductEncodedBytes);
  EXPECT_TRUE(F->covers(SLOT_P30));
  EXPECT_TRUE(F->covers(SLOT_P31 | SLOT_P32));
  EXPECT_TRUE(F->covers(SLOT_SET_E3));
}

TEST(HaydnAIEParityBundleTest, ThreeIssueFillThenRejectLikeAIE) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  Bundle<MCInst> B(&Fmts);
  MCInst Ops[4];
  for (int I = 0; I < 3; ++I) {
    Ops[I].setOpcode(Haydn::ADD32);
    ASSERT_TRUE(B.canAdd(Ops[I].getOpcode()));
    B.add(&Ops[I]);
  }
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(SLOT_SET_E3));
  EXPECT_TRUE(B.hasValidFormat());
  Ops[3].setOpcode(Haydn::ADD32);
  EXPECT_FALSE(B.canAdd(Ops[3].getOpcode()));
}

TEST(HaydnAIEParityBundleTest, ClearReturnsToEmptyAccept) {
  // AIE Bundle clear resets occupancy.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  Bundle<MCInst> B(&Fmts);
  MCInst A, X;
  A.setOpcode(Haydn::ADD32);
  X.setOpcode(Haydn::XOR32);
  B.add(&A);
  B.add(&X);
  B.clear();
  EXPECT_TRUE(B.empty());
  EXPECT_EQ(B.getOccupiedSlots(), 0u);
  EXPECT_TRUE(B.canAdd(Haydn::S_SW_WITH_IMM));
}

TEST(HaydnAIEParityBundleTest, SlotMapReflectsPlacement) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  Bundle<MCInst> B(&Fmts);
  MCInst St, Alu;
  St.setOpcode(Haydn::S_SW_WITH_IMM);
  Alu.setOpcode(Haydn::ADD32);
  B.add(&St);
  B.add(&Alu);
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_P30), &St);
  // ST is S0-only; multi-slot ALU prefers high slots (S2 then S1).
  EXPECT_TRUE(B.at(MCSlotKind::Haydn_SLOT_P31) == &Alu ||
              B.at(MCSlotKind::Haydn_SLOT_P32) == &Alu)
      << "ALU must land S1 or S2; occ=" << B.getOccupiedSlots();
  EXPECT_NE(B.getOccupiedSlots() & (Haydn::SLOT_P31 | Haydn::SLOT_P32), 0u);
}

//===----------------------------------------------------------------------===//
// FormatID / BundlePlan N-format-ready APIs (single live row)
//===----------------------------------------------------------------------===//

TEST(HaydnAIEParityBundleTest, EveryPackedCycleIsProductPlan) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const unsigned Sequences[][4] = {
      {Haydn::ADD32, 0, 0, 0},
      {Haydn::ADD32, Haydn::XOR32, 0, 0},
      {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32, 0},
      {Haydn::S_LW_WITH_IMM, Haydn::X2MULA32, 0, 0},
      {Haydn::S_SW_WITH_IMM, Haydn::ADD32, 0, 0},
      {Haydn::S_LW_WITH_IMM, Haydn::S_LW_WITH_IMM, Haydn::ADD32, 0},
      {Haydn::S_SW_WITH_IMM, Haydn::S_SW_WITH_IMM, 0, 0}, // split
  };
  for (const auto &Seq : Sequences) {
    SmallVector<unsigned, 4> Ops;
    for (int I = 0; I < 4 && Seq[I] != 0; ++I)
      Ops.push_back(Seq[I]);
    auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
    for (const OpcodeCycle &C : Cycles) {
      EXPECT_EQ(C.Plan.FID, FormatID::BundleE3);
      EXPECT_EQ(C.Plan.Bytes.Value, 16u);
      EXPECT_TRUE(C.Plan.isProductLegal());
      // N-format-ready: encodedBytesFor(FormatID) works
      auto B = encodedBytesFor(C.Plan.FID);
      ASSERT_TRUE(B.has_value());
      EXPECT_EQ(*B, ProductEncodedBytes);
    }
  }
}

TEST(HaydnAIEParityBundleTest, PlanFromPacketFormatsMatchesLiveRow) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  for (SlotBits Occ :
       {SlotBits(0), SlotBits(SLOT_P30), SlotBits(SLOT_P31 | SLOT_P32),
        SlotBits(SLOT_SET_E3)}) {
    auto P = planFromPacketFormats(Fmts.getPacketFormats(), Occ);
    ASSERT_TRUE(P.has_value()) << "occ=" << Occ;
    EXPECT_EQ(P->FID, ProductFormatID);
    EXPECT_EQ(P->Bytes.Value, 16u);
  }
}

//===----------------------------------------------------------------------===//
// DSP classic packs (AIE multi-slot density class)
//===----------------------------------------------------------------------===//

TEST(HaydnAIEParityBundleTest, DspKernelLdMacAluDensity) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_LW_WITH_IMM, Haydn::X2MULA32, Haydn::ADD32};
  EXPECT_TRUE(opcodesFormOneLegalCycle(Ops, Fmts));
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  ASSERT_EQ(Cycles.size(), 1u);
  EXPECT_EQ(Cycles[0].Plan.OccupiedSlots, SlotBits(SLOT_SET_E3));
}

TEST(HaydnAIEParityBundleTest, DualLoadMacFill) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  Bundle<MCInst> B(&Fmts);
  MCInst L0, L1, Mac;
  L0.setOpcode(Haydn::S_LW_WITH_IMM);
  L1.setOpcode(Haydn::S_LW_WITH_IMM);
  Mac.setOpcode(Haydn::X2MULA32);
  B.add(&L0);
  ASSERT_TRUE(B.canAdd(L1.getOpcode()));
  B.add(&L1);
  ASSERT_TRUE(B.canAdd(Mac.getOpcode()));
  B.add(&Mac);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(SLOT_SET_E3));
  EXPECT_TRUE(B.hasValidFormat());
}

TEST(HaydnAIEParityBundleTest, StoreStoreMustSplitLikeResourceConflict) {
  // AIE-style: same exclusive resource → not one bundle.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::S_SW_WITH_IMM, Haydn::S_SW_WITH_IMM};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  EXPECT_EQ(Cycles.size(), 3u);
  for (const auto &C : Cycles)
    EXPECT_EQ(C.Opcodes.size(), 1u);
}

//===----------------------------------------------------------------------===//
// Stress: long mixed stream partition (AIE schedule density)
//===----------------------------------------------------------------------===//

TEST(HaydnAIEParityBundleTest, LongMixedStreamPartitionInvariant) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  // Realistic FIR-ish stream: LD LD MAC ALU ST LD MAC ALU ST ...
  const unsigned Pattern[] = {
      Haydn::S_LW_WITH_IMM, Haydn::S_LW_WITH_IMM, Haydn::X2MULA32, Haydn::ADD32, Haydn::S_SW_WITH_IMM,
      Haydn::S_LW_WITH_IMM, Haydn::X2MULA32, Haydn::XOR32, Haydn::S_SW_WITH_IMM,
      Haydn::ADD64, Haydn::SLL64, Haydn::S_SW_WITH_IMM,
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
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::S_LW_WITH_IMM, Haydn::X2MULA32, Haydn::S_SW_WITH_IMM,
                    Haydn::ADD32, Haydn::ADD64, Haydn::S_SW_WITH_IMM, Haydn::XOR32};
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
  // When multi-format lands, this test extends — today only Full is legal.
  EXPECT_TRUE(isProductFormat(FormatID::BundleE3));
  auto B = encodedBytesFor(FormatID::BundleE3);
  ASSERT_TRUE(B.has_value());
  EXPECT_EQ(B->Value, 16u);
}

TEST(HaydnAIEParityBundleTest, SlotWindowBitsSum128AIEComposite) {
  // AIE composite size from field geometry; Haydn Full = 48+40+40.
  EXPECT_EQ(P30EncodedBits.Value + P31EncodedBits.Value +
                P32EncodedBits.Value,
            ProductEncodedBitsValue);
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Sum = 0;
  for (auto K : {MCSlotKind::Haydn_SLOT_P30, MCSlotKind::Haydn_SLOT_P31,
                 MCSlotKind::Haydn_SLOT_P32})
    Sum += Fmts.getSlotInfo(K)->getSize();
  EXPECT_EQ(Sum, 128u);
}

//===----------------------------------------------------------------------===//
// B3.5 MC serialize-only (AIEBaseAsmPrinter.cpp:161-164 /
// AIEBaseMCCodeEmitter.cpp:45-68 peers)
//===----------------------------------------------------------------------===//

TEST(HaydnAIEParityBundleTest, FormatOpcodeIsProductCompositeSerializeOnly) {
  // AIE: MCBundle.setOpcode(Format->Opcode); emitter getBinaryCode + emit.
  // Haydn product: Format->Opcode == BUNDLE_E3 for every covered set.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  Bundle<MCInst> B(&Fmts);
  MCInst A, X, N;
  A.setOpcode(Haydn::ADD32_P32_ALU0);
  X.setOpcode(Haydn::XOR32_P31_ALU1);
  // ALU2: ADD32 above already holds ALU0, and one unit serves one entry.
  N.setOpcode(Haydn::NOT32_P30_ALU2);
  if (Fmts.getSlotKind(A.getOpcode()) == MCSlotKind()) {
    A.setOpcode(Haydn::ADD32);
    X.setOpcode(Haydn::XOR32);
    N.setOpcode(Haydn::NOT32);
  }
  ASSERT_TRUE(B.canAdd(A.getOpcode()));
  B.add(&A);
  ASSERT_TRUE(B.canAdd(X.getOpcode()));
  B.add(&X);
  ASSERT_TRUE(B.canAdd(N.getOpcode()));
  B.add(&N);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_STREQ(Fmt->Name, "BUNDLE_E3");
  EXPECT_EQ(Fmt->Opcode, Haydn::BUNDLE_E3)
      << "AsmPrinter must emit Format->Opcode composite, not Haydn::BUNDLE";

  // Committed members keep fixed getSlotKind — encode must not re-auction.
  EXPECT_NE(Fmts.getSlotKind(B.at(MCSlotKind::Haydn_SLOT_P30)->getOpcode()),
            MCSlotKind());
  EXPECT_NE(Fmts.getSlotKind(B.at(MCSlotKind::Haydn_SLOT_P31)->getOpcode()),
            MCSlotKind());
  EXPECT_NE(Fmts.getSlotKind(B.at(MCSlotKind::Haydn_SLOT_P32)->getOpcode()),
            MCSlotKind());
}

TEST(HaydnAIEParityBundleTest, SerializeSlotMapNoReAuctionOnMembers) {
  // Source order that would starve under wrong re-auction still packs by
  // Desc getSlotKind (members already placed). Second S0-only fails canAdd
  // — no constrained-first re-auction escape (B3.5 delete).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  Bundle<MCInst> B(&Fmts);
  MCInst St0, St1;
  St0.setOpcode(Haydn::S_SW_WITH_IMM_P30_LOADSTORE0);
  St1.setOpcode(Haydn::S_SW_WITH_IMM_P30_LOADSTORE0);
  if (Fmts.getSlotKind(St0.getOpcode()) == MCSlotKind()) {
    St0.setOpcode(Haydn::S_SW_WITH_IMM);
    St1.setOpcode(Haydn::S_SW_WITH_IMM);
  }
  ASSERT_TRUE(B.canAdd(St0.getOpcode()));
  B.add(&St0);
  EXPECT_FALSE(B.canAdd(St1.getOpcode()))
      << "second fixed S0 member must fail (no encode re-auction)";
  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_EQ(Fmt->Opcode, Haydn::BUNDLE_E3);
}

} // namespace
