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

#include "HaydnBundleFormatSolver.h"
#include "HaydnBundlePlan.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"

#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

namespace llvm {
const MCInstrInfo &getHaydnSharedMCInstrInfo();
}

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_ADD32_AllThreeSlots) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD32);
  ASSERT_NE(Alts, nullptr);
  // Sparse size-3, index == residual occupancy. Members are Format E.
  ASSERT_EQ(Alts->size(), 3u);
  for (unsigned Slot = 0; Slot < 3; ++Slot) {
    EXPECT_NE((*Alts)[Slot], 0u);
    EXPECT_TRUE(formatEMemberOccupiesEntry((*Alts)[Slot], Slot));
  }

  SmallVector<PlacementAlternative, 8> Enumerated;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD32, Enumerated));
  ASSERT_FALSE(Enumerated.empty());
  uint64_t UnionMask = 0;
  for (const PlacementAlternative &A : Enumerated) {
    const StringRef Name = haydn::bundle::haydnOpcodeName(A.MemberOpcode);
    EXPECT_TRUE(Name.contains("_E2_") || Name.contains("_E3_"));
    EXPECT_FALSE(Name.ends_with("_S0") || Name.ends_with("_S1") ||
                 Name.ends_with("_S2"));
    UnionMask |= A.CompatibleFormatMask;
  }
  EXPECT_EQ(UnionMask, ProductFormatMask);
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_LD32_SparseS0S1) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::LD32);
  ASSERT_NE(Alts, nullptr);
  // Residual occupancy S0|S1 — not raw EntryIdx (LD32 has e2 LOAD1).
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_NE((*Alts)[0], 0u);
  EXPECT_NE((*Alts)[1], 0u);
  EXPECT_EQ((*Alts)[2], 0u);
  EXPECT_TRUE(formatEMemberOccupiesEntry((*Alts)[0], 0));
  EXPECT_TRUE(formatEMemberOccupiesEntry((*Alts)[1], 1));

  SmallVector<PlacementAlternative, 8> Enumerated;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::LD32, Enumerated));
  ASSERT_FALSE(Enumerated.empty());
  for (const PlacementAlternative &A : Enumerated) {
    const StringRef Name = haydn::bundle::haydnOpcodeName(A.MemberOpcode);
    EXPECT_TRUE(Name.contains("_E2_") || Name.contains("_E3_"));
  }
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_ADD64_SparseS1S2) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD64);
  ASSERT_NE(Alts, nullptr);
  // Residual occupancy S1|S2 — not raw EntryIdx (ADD64 has e0 ALU0/ALU2).
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_EQ((*Alts)[0], 0u);
  EXPECT_NE((*Alts)[1], 0u);
  EXPECT_NE((*Alts)[2], 0u);
  EXPECT_TRUE(formatEMemberOccupiesEntry((*Alts)[1], 1));
  EXPECT_TRUE(formatEMemberOccupiesEntry((*Alts)[2], 2));

  SmallVector<PlacementAlternative, 8> Enumerated;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD64, Enumerated));
  ASSERT_FALSE(Enumerated.empty());
  for (const PlacementAlternative &A : Enumerated) {
    const StringRef Name = haydn::bundle::haydnOpcodeName(A.MemberOpcode);
    EXPECT_TRUE(Name.contains("_E2_") || Name.contains("_E3_"));
    // Occupancy stays residual S1|S2; e0 members are not extra SLOT0 alts.
    EXPECT_NE(A.FieldSlots, SlotBits(Haydn::SLOT0));
  }
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_ST32_SparseS0Only) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ST32);
  ASSERT_NE(Alts, nullptr);
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_NE((*Alts)[0], 0u);
  EXPECT_EQ((*Alts)[1], 0u);
  EXPECT_EQ((*Alts)[2], 0u);
  EXPECT_TRUE(formatEMemberOccupiesEntry((*Alts)[0], 0));
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_X4CMUL16_SparseS1S2) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::X4CMUL16);
  ASSERT_NE(Alts, nullptr);
  // Residual occupancy S1|S2 — not raw EntryIdx (X4CMUL16 has e0 MAC0).
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_EQ((*Alts)[0], 0u);
  EXPECT_NE((*Alts)[1], 0u);
  EXPECT_NE((*Alts)[2], 0u);
  EXPECT_TRUE(formatEMemberOccupiesEntry((*Alts)[1], 1));
  EXPECT_TRUE(formatEMemberOccupiesEntry((*Alts)[2], 2));

  SmallVector<PlacementAlternative, 8> Enumerated;
  ASSERT_TRUE(
      enumeratePlacementAlternatives(Fmts, Haydn::X4CMUL16, Enumerated));
  ASSERT_FALSE(Enumerated.empty());
  for (const PlacementAlternative &A : Enumerated) {
    const StringRef Name = haydn::bundle::haydnOpcodeName(A.MemberOpcode);
    EXPECT_TRUE(Name.contains("_E2_") || Name.contains("_E3_"));
  }
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_MOVEI_H_SparseS0Only) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::MOVEI_H);
  ASSERT_NE(Alts, nullptr);
  // Residual occupancy S0 only. Member is Format E when an e0 row exists.
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_NE((*Alts)[0], 0u);
  EXPECT_EQ((*Alts)[1], 0u);
  EXPECT_EQ((*Alts)[2], 0u);
  EXPECT_TRUE(formatEMemberOccupiesEntry((*Alts)[0], 0));

  SmallVector<PlacementAlternative, 8> Enumerated;
  ASSERT_TRUE(
      enumeratePlacementAlternatives(Fmts, Haydn::MOVEI_H, Enumerated));
  ASSERT_FALSE(Enumerated.empty());
  for (const PlacementAlternative &A : Enumerated) {
    const StringRef Name = haydn::bundle::haydnOpcodeName(A.MemberOpcode);
    EXPECT_TRUE(Name.contains("_E2_") || Name.contains("_E3_"));
  }
}

TEST(HaydnPlacementAlternativeTest, FieldSlotsFromSparseIndexNotFlexReverse) {
  // Residual AlternateInsts stay sparse size-3. Enumerate keeps that
  // occupancy (ADD64 has no residual S0) and stamps Format E members.
  HaydnMCFormats Fmts;
  SmallVector<PlacementAlternative, 8> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD64, Alts));
  for (const PlacementAlternative &A : Alts)
    EXPECT_NE(A.FieldSlots, fieldSlotsForAltIndex(0));
  const std::vector<unsigned> *Sparse =
      Fmts.getAlternateInstsOpcode(Haydn::ADD64);
  ASSERT_NE(Sparse, nullptr);
  ASSERT_EQ(Sparse->size(), 3u);
  EXPECT_EQ((*Sparse)[0], 0u);
  EXPECT_TRUE(formatEMemberOccupiesEntry((*Sparse)[1], 1));
  EXPECT_TRUE(formatEMemberOccupiesEntry((*Sparse)[2], 2));
  EXPECT_EQ(Fmts.getSlotKind(Haydn::MOVEI_H_E2_E0_ALU0_I32),
            MCSlotKind(MCSlotKind::Haydn_SLOT_E2_0));
}

TEST(HaydnPlacementAlternativeTest, LegalSlotsEqualsOROfFieldSlots) {
  // getLegalSlots still follows residual AlternateInsts. Enumerate may
  // use Format E members; it must stay non-empty when Legal != 0.
  HaydnMCFormats Fmts;
  const unsigned Opcodes[] = {
      Haydn::ADD32, Haydn::SUB32,  Haydn::NOT32, Haydn::NEG32,
      Haydn::ADD64, Haydn::LD32,   Haydn::ST32,  Haydn::LD64,
      Haydn::X2MULA32, Haydn::MAX64, Haydn::SLL64, Haydn::POPCOUNT32};

  for (unsigned Opcode : Opcodes) {
    SlotBits Legal = Fmts.getLegalSlots(Opcode);
    SmallVector<PlacementAlternative, 8> Enumerated;
    const bool HasAlts =
        enumeratePlacementAlternatives(Fmts, Opcode, Enumerated);
    EXPECT_EQ(HasAlts, Legal != 0) << "opcode " << Opcode;

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

TEST(HaydnPlacementAlternativeTest, OccupancyMatchesLogicalDesc_SLT64) {
  // SLT64 ISel is unary dest+src. Golden also has SFR-only 2-src at the
  // same EntryIdx. Occupancy must not first-match the 0-def form.
  HaydnMCFormats Fmts;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  const MCInstrDesc &Log = MII.get(Haydn::SLT64);
  ASSERT_EQ(Log.getNumDefs(), 1u);
  ASSERT_EQ(Log.getNumOperands(), 2u);
  const std::vector<unsigned> *Alts = Fmts.getAlternateInstsOpcode(Haydn::SLT64);
  ASSERT_NE(Alts, nullptr);
  unsigned Members = 0;
  for (unsigned Member : *Alts) {
    if (Member == 0)
      continue;
    const MCInstrDesc &Mem = MII.get(Member);
    EXPECT_EQ(Mem.getNumDefs(), Log.getNumDefs()) << Member;
    EXPECT_EQ(Mem.getNumOperands(), Log.getNumOperands()) << Member;
    ++Members;
  }
  EXPECT_GE(Members, 2u);
}

TEST(HaydnPlacementAlternativeTest, OccupancyMatchesLogicalDesc_X2SLT32) {
  // X2SLT32 ISel is SFR-only (outs empty, 2 src). Do not pick unary ALU0.
  HaydnMCFormats Fmts;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  const MCInstrDesc &Log = MII.get(Haydn::X2SLT32);
  ASSERT_EQ(Log.getNumDefs(), 0u);
  ASSERT_EQ(Log.getNumOperands(), 2u);
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::X2SLT32);
  ASSERT_NE(Alts, nullptr);
  unsigned Members = 0;
  for (unsigned Member : *Alts) {
    if (Member == 0)
      continue;
    const MCInstrDesc &Mem = MII.get(Member);
    EXPECT_EQ(Mem.getNumDefs(), Log.getNumDefs()) << Member;
    EXPECT_EQ(Mem.getNumOperands(), Log.getNumOperands()) << Member;
    ++Members;
  }
  EXPECT_GE(Members, 2u);
}

TEST(HaydnPlacementAlternativeTest, OccupancyPrefersE2WhenBothModesExist) {
  // XOR32 / ADD32 have E2 at e0 and E3 at e0/e1/e2. Occupancy must not
  // first-match E3 at a residual index that also has an E2 member —
  // E2/E3 is a bundle-level fact (child count), not occupancy first-match.
  HaydnMCFormats Fmts;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  const std::vector<unsigned> *Alts = Fmts.getAlternateInstsOpcode(Haydn::XOR32);
  ASSERT_NE(Alts, nullptr);
  ASSERT_EQ(Alts->size(), 3u);
  ASSERT_NE((*Alts)[0], 0u);
  EXPECT_TRUE(StringRef(MII.getName((*Alts)[0])).contains("_E2_"))
      << MII.getName((*Alts)[0]);

  const std::vector<unsigned> *AddAlts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD32);
  ASSERT_NE(AddAlts, nullptr);
  ASSERT_EQ(AddAlts->size(), 3u);
  ASSERT_NE((*AddAlts)[0], 0u);
  EXPECT_TRUE(StringRef(MII.getName((*AddAlts)[0])).contains("_E2_"))
      << MII.getName((*AddAlts)[0]);

  const std::vector<unsigned> *MspAlts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD32_MSP);
  ASSERT_NE(MspAlts, nullptr);
  ASSERT_EQ(MspAlts->size(), 3u);
  ASSERT_NE((*MspAlts)[0], 0u);
  EXPECT_TRUE(StringRef(MII.getName((*MspAlts)[0])).contains("_E2_"))
      << MII.getName((*MspAlts)[0]);
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
  SmallVector<PlacementAlternative, 8> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD32, Alts));
  ASSERT_FALSE(Alts.empty());
  uint64_t UnionMask = 0;
  for (const PlacementAlternative &A : Alts) {
    UnionMask |= A.CompatibleFormatMask;
    EXPECT_TRUE(A.isCompatibleWith(BundleFormatRowID::E96TwoEntry) ||
                A.isCompatibleWith(BundleFormatRowID::E96ThreeEntry));
  }
  EXPECT_EQ(UnionMask, ProductFormatMask);
}

TEST(HaydnPlacementAlternativeTest, FilterAlternativesForFormat_ProductRows) {
  // FE8: synthetic FormatID filter overloads are deleted; only the
  // BundleFormatRowID filter remains. Exercise both product rows.
  SmallVector<PlacementAlternative, 4> Alts;
  Alts.emplace_back(/*MemberOpc=*/Haydn::ADD32_E2_E0_ALU0_RR, ProductFormatMask);
  Alts.emplace_back(/*MemberOpc=*/Haydn::ADD32_E3_E1_ALU1_RR, ProductFormatMask);

  SmallVector<PlacementAlternative, 4> ForE2 = Alts;
  filterAlternativesForFormat(ForE2, BundleFormatRowID::E96TwoEntry);
  ASSERT_EQ(ForE2.size(), 2u);
  EXPECT_EQ(ForE2[0].MemberOpcode, Haydn::ADD32_E2_E0_ALU0_RR);
  EXPECT_EQ(ForE2[1].MemberOpcode, Haydn::ADD32_E3_E1_ALU1_RR);

  SmallVector<PlacementAlternative, 4> ForE3 = Alts;
  filterAlternativesForFormat(ForE3, BundleFormatRowID::E96ThreeEntry);
  ASSERT_EQ(ForE3.size(), 2u);
}

TEST(HaydnPlacementAlternativeTest, DefaultCtorStampsProductMask) {
  PlacementAlternative A(Haydn::ADD32_E2_E0_ALU0_RR);
  EXPECT_EQ(A.MemberOpcode, Haydn::ADD32_E2_E0_ALU0_RR);
  EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
  EXPECT_TRUE(A.isCompatibleWith(BundleFormatRowID::E96TwoEntry));
  EXPECT_TRUE(A.isCompatibleWith(BundleFormatRowID::E96ThreeEntry));
}

TEST(HaydnPlacementAlternativeTest, CSRW_WStaysResidualFieldSlot) {
  // No Format E CSRW_W members; reloc keeps CSRW_W_S0.
  HaydnMCFormats Fmts;
  SmallVector<PlacementAlternative, 4> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::CSRW_W, Alts));
  ASSERT_EQ(Alts.size(), 1u);
  EXPECT_EQ(Alts[0].MemberOpcode, Haydn::CSRW_W_S0);
}

} // namespace
