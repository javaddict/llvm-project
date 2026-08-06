//===- HaydnPlacementAlternativeTest.cpp - placement alt table -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for getAlternateInstsOpcode / PlacementAlternative over
// Format E placement members + CompatibleFormatMask + sparse size-3
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
  // Product alts stamp E2|E3 CompatibleFormatMask.
  for (const PlacementAlternative &A : Enumerated) {
    EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
    EXPECT_TRUE(A.isCompatibleWith(BundleFormatRowID::E96TwoEntry));
    EXPECT_TRUE(A.isCompatibleWith(BundleFormatRowID::E96ThreeEntry));
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

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_X4CMUL16_SparseS1S2) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::X4CMUL16);
  ASSERT_NE(Alts, nullptr);
  // Sparse: {0, X4CMUL16_S1, X4CMUL16_S2} — MAC D_R is s1|s2 only.
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_EQ((*Alts)[0], 0u);
  EXPECT_EQ((*Alts)[1], Haydn::X4CMUL16_S1);
  EXPECT_EQ((*Alts)[2], Haydn::X4CMUL16_S2);

  SmallVector<PlacementAlternative, 4> Enumerated;
  ASSERT_TRUE(
      enumeratePlacementAlternatives(Fmts, Haydn::X4CMUL16, Enumerated));
  ASSERT_EQ(Enumerated.size(), 2u);
  EXPECT_EQ(Enumerated[0].MemberOpcode, Haydn::X4CMUL16_S1);
  EXPECT_EQ(Enumerated[0].FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_EQ(Enumerated[1].MemberOpcode, Haydn::X4CMUL16_S2);
  EXPECT_EQ(Enumerated[1].FieldSlots, SlotBits(Haydn::SLOT2));
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_MOVEI_H_SparseS0Only) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::MOVEI_H);
  ASSERT_NE(Alts, nullptr);
  // Sparse: {MOVEI_H_S0, 0, 0} — ALU64 I32 is s0 only.
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_EQ((*Alts)[0], Haydn::MOVEI_H_S0);
  EXPECT_EQ((*Alts)[1], 0u);
  EXPECT_EQ((*Alts)[2], 0u);

  SmallVector<PlacementAlternative, 4> Enumerated;
  ASSERT_TRUE(
      enumeratePlacementAlternatives(Fmts, Haydn::MOVEI_H, Enumerated));
  ASSERT_EQ(Enumerated.size(), 1u);
  EXPECT_EQ(Enumerated[0].MemberOpcode, Haydn::MOVEI_H_S0);
  EXPECT_EQ(Enumerated[0].FieldSlots, SlotBits(Haydn::SLOT0));
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

TEST(HaydnPlacementAlternativeTest, CompatibleFormatMask_ProductE96Rows) {
  // Product mask is E96TwoEntry | E96ThreeEntry.
  EXPECT_EQ(ProductFormatMask,
            formatRowBit(BundleFormatRowID::E96TwoEntry) |
                formatRowBit(BundleFormatRowID::E96ThreeEntry));

  HaydnMCFormats Fmts;
  SmallVector<PlacementAlternative, 4> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD32, Alts));
  ASSERT_FALSE(Alts.empty());
  for (const PlacementAlternative &A : Alts) {
    EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
    EXPECT_TRUE(A.isCompatibleWith(BundleFormatRowID::E96TwoEntry));
    EXPECT_TRUE(A.isCompatibleWith(BundleFormatRowID::E96ThreeEntry));
  }
}

TEST(HaydnPlacementAlternativeTest, FilterAlternativesForFormat_ProductRows) {
  // FE8: synthetic FormatID filter overloads are deleted; only the
  // BundleFormatRowID filter remains. Exercise both product rows.
  SmallVector<PlacementAlternative, 4> Alts;
  Alts.emplace_back(/*MemberOpc=*/Haydn::ADD32_S0, ProductFormatMask);
  Alts.emplace_back(/*MemberOpc=*/Haydn::ADD32_S1, ProductFormatMask);

  SmallVector<PlacementAlternative, 4> ForE2 = Alts;
  filterAlternativesForFormat(ForE2, BundleFormatRowID::E96TwoEntry);
  ASSERT_EQ(ForE2.size(), 2u);
  EXPECT_EQ(ForE2[0].MemberOpcode, Haydn::ADD32_S0);
  EXPECT_EQ(ForE2[1].MemberOpcode, Haydn::ADD32_S1);

  SmallVector<PlacementAlternative, 4> ForE3 = Alts;
  filterAlternativesForFormat(ForE3, BundleFormatRowID::E96ThreeEntry);
  ASSERT_EQ(ForE3.size(), 2u);
}

TEST(HaydnPlacementAlternativeTest, DefaultCtorStampsProductMask) {
  PlacementAlternative A(Haydn::ADD32_S0);
  EXPECT_EQ(A.MemberOpcode, Haydn::ADD32_S0);
  EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
  EXPECT_TRUE(A.isCompatibleWith(BundleFormatRowID::E96TwoEntry));
  EXPECT_TRUE(A.isCompatibleWith(BundleFormatRowID::E96ThreeEntry));
}

} // namespace
