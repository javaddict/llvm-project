//===- HaydnPlacementAlternativeTest.cpp - placement alt table -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for getAlternateInstsOpcode / PlacementAlternative over
// BUNDLE128_FULL members + CompatibleFormatMask + sparse size-3
// FieldSlots-by-index (alts-derived; index == field).
// Mirrors AIE BundleTest / HazardRecognizerTest alternate-opcode coverage.
//
// AIE peer: getAlternateInstsOpcode (AIEMCFormats.h:376-379) fed by
// MultiSlot_Pseudo materializableInto (AIE2InstrFormats.td:21-31) via
// CodeGenFormat addAlternateInstInMultiSlotPseudo.
// Sparse alts rows are size-3 (index == field; 0 = hole).
//
//===----------------------------------------------------------------------===//

#include "HaydnBundlePlan.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"

#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_ADD32_AllThreeSlots) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD32);
  ASSERT_NE(Alts, nullptr);
  // Sparse size-3, index == field.
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_EQ((*Alts)[0], Haydn::ADD32_S0);
  EXPECT_EQ((*Alts)[1], Haydn::ADD32_S1);
  EXPECT_EQ((*Alts)[2], Haydn::ADD32_S2);

  SmallVector<PlacementAlternative, 4> Enumerated;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD32, Enumerated));
  ASSERT_EQ(Enumerated.size(), 3u);
  EXPECT_EQ(Enumerated[0].MemberOpcode, Haydn::ADD32_S0);
  EXPECT_EQ(Enumerated[0].FieldSlots, SlotBits(Haydn::SLOT0));
  EXPECT_EQ(Enumerated[1].MemberOpcode, Haydn::ADD32_S1);
  EXPECT_EQ(Enumerated[1].FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_EQ(Enumerated[2].MemberOpcode, Haydn::ADD32_S2);
  EXPECT_EQ(Enumerated[2].FieldSlots, SlotBits(Haydn::SLOT2));
  // Product alts stamp Full CompatibleFormatMask.
  for (const PlacementAlternative &A : Enumerated) {
    EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
    EXPECT_TRUE(A.isCompatibleWith(FormatID::Bundle128Full));
  }
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_LD32_SparseS0S1) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::LD32);
  ASSERT_NE(Alts, nullptr);
  // Sparse: {LD32_S0, LD32_S1, 0}
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_EQ((*Alts)[0], Haydn::LD32_S0);
  EXPECT_EQ((*Alts)[1], Haydn::LD32_S1);
  EXPECT_EQ((*Alts)[2], 0u);

  SmallVector<PlacementAlternative, 4> Enumerated;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::LD32, Enumerated));
  ASSERT_EQ(Enumerated.size(), 2u);
  EXPECT_EQ(Enumerated[0].FieldSlots, SlotBits(Haydn::SLOT0));
  EXPECT_EQ(Enumerated[1].FieldSlots, SlotBits(Haydn::SLOT1));
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_ADD64_SparseS1S2) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD64);
  ASSERT_NE(Alts, nullptr);
  // Sparse: {0, ADD64_S1, ADD64_S2} — no S0 (ALU64 is s1|s2 only).
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_EQ((*Alts)[0], 0u);
  EXPECT_EQ((*Alts)[1], Haydn::ADD64_S1);
  EXPECT_EQ((*Alts)[2], Haydn::ADD64_S2);

  SmallVector<PlacementAlternative, 4> Enumerated;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD64, Enumerated));
  ASSERT_EQ(Enumerated.size(), 2u);
  EXPECT_EQ(Enumerated[0].MemberOpcode, Haydn::ADD64_S1);
  EXPECT_EQ(Enumerated[0].FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_EQ(Enumerated[1].MemberOpcode, Haydn::ADD64_S2);
  EXPECT_EQ(Enumerated[1].FieldSlots, SlotBits(Haydn::SLOT2));
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_ST32_SparseS0Only) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ST32);
  ASSERT_NE(Alts, nullptr);
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_EQ((*Alts)[0], Haydn::ST32_S0);
  EXPECT_EQ((*Alts)[1], 0u);
  EXPECT_EQ((*Alts)[2], 0u);
}

TEST(HaydnPlacementAlternativeTest, FieldSlotsFromSparseIndexNotFlexReverse) {
  // FieldSlots = 1<<alt-index (alts-derived; index == field).
  // Member identity is sparse AlternateInsts.
  HaydnMCFormats Fmts;
  SmallVector<PlacementAlternative, 4> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD64, Alts));
  // Sparse holes mean index 1 → SLOT1; index is the sole FieldSlots authority.
  EXPECT_EQ(Alts[0].FieldSlots, fieldSlotsForAltIndex(1));
  EXPECT_EQ(Alts[1].FieldSlots, fieldSlotsForAltIndex(2));
  const std::vector<unsigned> *Sparse =
      Fmts.getAlternateInstsOpcode(Haydn::ADD64);
  ASSERT_NE(Sparse, nullptr);
  ASSERT_EQ(Sparse->size(), 3u);
  EXPECT_EQ((*Sparse)[0], 0u);
  EXPECT_EQ((*Sparse)[1], Haydn::ADD64_S1);
  EXPECT_EQ((*Sparse)[2], Haydn::ADD64_S2);
  EXPECT_EQ(Fmts.getSlotKind(Haydn::ADD64_S1),
            MCSlotKind(MCSlotKind::Haydn_SLOT_S1));
}

TEST(HaydnPlacementAlternativeTest, LegalSlotsEqualsOROfFieldSlots) {
  // Consistency: getLegalSlots(Opc) == OR FieldSlots from enumerate.
  HaydnMCFormats Fmts;
  const unsigned Opcodes[] = {
      Haydn::ADD32, Haydn::SUB32,  Haydn::NOT32, Haydn::NEG32,
      Haydn::ADD64, Haydn::LD32,   Haydn::ST32,  Haydn::LD64,
      Haydn::X2MULA32, Haydn::MAX64, Haydn::SLL64, Haydn::POPCOUNT32};

  for (unsigned Opcode : Opcodes) {
    SlotBits Legal = Fmts.getLegalSlots(Opcode);
    SmallVector<PlacementAlternative, 4> Enumerated;
    const bool HasAlts =
        enumeratePlacementAlternatives(Fmts, Opcode, Enumerated);
    SlotBits FromFields = 0;
    for (const PlacementAlternative &A : Enumerated)
      FromFields |= A.FieldSlots;
    EXPECT_EQ(Legal, FromFields) << "opcode " << Opcode;
    EXPECT_EQ(HasAlts, Legal != 0) << "opcode " << Opcode;

    // Sparse raw vector: bit k set iff Alts[k] != 0.
    const std::vector<unsigned> *Raw =
        Fmts.getAlternateInstsOpcode(Opcode);
    if (!Raw) {
      EXPECT_EQ(Legal, 0u) << "opcode " << Opcode;
      continue;
    }
    ASSERT_EQ(Raw->size(), 3u) << "opcode " << Opcode << " sparse size-3";
    SlotBits FromSparse = 0;
    for (unsigned I = 0; I < 3; ++I)
      if ((*Raw)[I] != 0)
        FromSparse |= (SlotBits(1) << I);
    EXPECT_EQ(Legal, FromSparse) << "opcode " << Opcode;
    EXPECT_TRUE(hasPlacementAlternatives(Fmts, Opcode) || Legal == 0);
    EXPECT_EQ(getPlacementMemberOpcodes(Fmts, Opcode), Raw);
  }
}

TEST(HaydnPlacementAlternativeTest, UnknownOpcodeReturnsNull) {
  HaydnMCFormats Fmts;
  // PHI is not a Haydn multi-slot logical.
  EXPECT_EQ(Fmts.getAlternateInstsOpcode(/*Opcode=*/0), nullptr);
  EXPECT_FALSE(hasPlacementAlternatives(Fmts, 0));
}

//===----------------------------------------------------------------------===//
// CompatibleFormatMask (plan §6.1)
//===----------------------------------------------------------------------===//

TEST(HaydnPlacementAlternativeTest, CompatibleFormatMask_ProductFullOnly) {
  EXPECT_EQ(ProductFormatMask, 1ull << 0);
  EXPECT_EQ(formatIDBit(FormatID::Bundle128Full), ProductFormatMask);

  HaydnMCFormats Fmts;
  SmallVector<PlacementAlternative, 4> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD32, Alts));
  ASSERT_FALSE(Alts.empty());
  for (const PlacementAlternative &A : Alts) {
    EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
    EXPECT_TRUE(A.isCompatibleWith(ProductFormatID));
    EXPECT_TRUE(A.isCompatibleWith(FormatID::Bundle128Full));
    constexpr FormatID Synth = static_cast<FormatID>(1);
    EXPECT_FALSE(A.isCompatibleWith(Synth));
  }
}

TEST(HaydnPlacementAlternativeTest, FilterAlternativesForFormat_Synthetic) {
  constexpr FormatID SynthNarrow = static_cast<FormatID>(1);
  const uint64_t SynthMask = formatIDBit(SynthNarrow);
  const uint64_t BothMask = ProductFormatMask | SynthMask;

  SmallVector<PlacementAlternative, 4> Alts;
  Alts.emplace_back(/*MemberOpc=*/Haydn::ADD32_S0, ProductFormatMask);
  Alts.emplace_back(/*MemberOpc=*/Haydn::ADD32_S1, BothMask);
  Alts.emplace_back(/*MemberOpc=*/Haydn::ADD32_S2, SynthMask);

  SmallVector<PlacementAlternative, 4> ForFull = Alts;
  filterAlternativesForFormat(ForFull, FormatID::Bundle128Full);
  ASSERT_EQ(ForFull.size(), 2u);
  EXPECT_EQ(ForFull[0].MemberOpcode, Haydn::ADD32_S0);
  EXPECT_EQ(ForFull[1].MemberOpcode, Haydn::ADD32_S1);

  SmallVector<PlacementAlternative, 4> ForSynth = Alts;
  filterAlternativesForFormat(ForSynth, SynthNarrow);
  ASSERT_EQ(ForSynth.size(), 2u);
  EXPECT_EQ(ForSynth[0].MemberOpcode, Haydn::ADD32_S1);
  EXPECT_EQ(ForSynth[1].MemberOpcode, Haydn::ADD32_S2);
}

TEST(HaydnPlacementAlternativeTest, DefaultCtorStampsProductMask) {
  PlacementAlternative A(Haydn::ADD32_S0);
  EXPECT_EQ(A.MemberOpcode, Haydn::ADD32_S0);
  EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
  EXPECT_TRUE(A.isCompatibleWith(FormatID::Bundle128Full));
}

} // namespace
