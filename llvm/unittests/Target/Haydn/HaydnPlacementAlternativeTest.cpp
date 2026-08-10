//===- HaydnPlacementAlternativeTest.cpp - placement alt table -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for getAlternateInstsOpcode / PlacementAlternative over
// BUNDLE_E3 members + CompatibleFormatMask + sparse size-3
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
#include "HaydnTestMCInstrInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"

#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

// The alternates vector is indexed by PLACEMENT -- the (entry position, unit)
// pair, 0..17 -- and is sparse. Under Bundle128 the index WAS the slot, which
// is why these tests used to read `(*Alts)[k]`. They now talk about the SET of
// live members and ask each one its own slot, which is both the correct model
// and durable: 7.1 says the (unit, position) relation is expected to be
// re-delivered, and a test written against positions would break every time.
static std::vector<unsigned> liveMembers(const std::vector<unsigned> *Alts) {
  std::vector<unsigned> Out;
  if (Alts)
    for (unsigned M : *Alts)
      if (M != 0)
        Out.push_back(M);
  llvm::sort(Out);
  return Out;
}

static std::vector<unsigned> sorted(std::vector<unsigned> V) {
  llvm::sort(V);
  return V;
}


TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_ADD32_AllThreeSlots) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD32);
  ASSERT_NE(Alts, nullptr);
  // Seven members, not three: ADD32 reaches entry 0 of the 2-entry form and
  // every entry of the 3-entry form, and at P30/P31/P32 it has a member on two
  // different ALUs apiece. Slot count and member count are no longer the same
  // number.
  EXPECT_EQ(liveMembers(Alts),
            sorted({Haydn::ADD32_P20_ALU0, Haydn::ADD32_P30_ALU0,
                    Haydn::ADD32_P30_ALU2, Haydn::ADD32_P31_ALU0,
                    Haydn::ADD32_P31_ALU1, Haydn::ADD32_P32_ALU0,
                    Haydn::ADD32_P32_ALU2}));

  SmallVector<PlacementAlternative, 4> Enumerated;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD32, Enumerated));
  EXPECT_EQ(Enumerated.size(), liveMembers(Alts).size());
  SlotBits Union = 0;
  for (const PlacementAlternative &A : Enumerated) {
    // Each row's slot comes from its own member, never from its position.
    EXPECT_EQ(A.FieldSlots, fieldSlotsForMember(Fmts, A.MemberOpcode));
    Union |= A.FieldSlots;
    // Both composites are product formats now, so a member is compatible with
    // the one its entry belongs to and the mask covers both rows.
    EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
  }
  EXPECT_EQ(Union, Fmts.getLegalSlots(Haydn::ADD32));
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_LD32_SparseS0S1) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::S_LW_WITH_IMM);
  ASSERT_NE(Alts, nullptr);
  // A load reaches every entry: LOADSTORE0 serves P20/P30 and LOAD1 serves
  // P21/P31/P32. Five members over five slots, one unit each.
  EXPECT_EQ(liveMembers(Alts),
            sorted({Haydn::S_LW_WITH_IMM_P20_LOADSTORE0,
                    Haydn::S_LW_WITH_IMM_P21_LOAD1,
                    Haydn::S_LW_WITH_IMM_P30_LOADSTORE0,
                    Haydn::S_LW_WITH_IMM_P31_LOAD1,
                    Haydn::S_LW_WITH_IMM_P32_LOAD1}));

  SmallVector<PlacementAlternative, 4> Enumerated;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::S_LW_WITH_IMM, Enumerated));
  EXPECT_EQ(Enumerated.size(), 5u);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_LW_WITH_IMM),
            SlotBits(Haydn::SLOT_MASK_ANY));
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_ADD64_SparseS1S2) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD64);
  ASSERT_NE(Alts, nullptr);
  // ADD64 is NOT sparse any more. Bundle128 gave the 64-bit ALU ops s1|s2
  // only; format E gives them exactly the placements ADD32 has, which is what
  // makes three ALU64 ops in one bundle legal.
  EXPECT_EQ(liveMembers(Alts),
            sorted({Haydn::ADD64_P20_ALU0, Haydn::ADD64_P30_ALU0,
                    Haydn::ADD64_P30_ALU2, Haydn::ADD64_P31_ALU0,
                    Haydn::ADD64_P31_ALU1, Haydn::ADD64_P32_ALU0,
                    Haydn::ADD64_P32_ALU2}));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD64),
            Fmts.getLegalSlots(Haydn::ADD32));

  SmallVector<PlacementAlternative, 4> Enumerated;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD64, Enumerated));
  EXPECT_EQ(Enumerated.size(), 7u);
  for (const PlacementAlternative &A : Enumerated)
    EXPECT_EQ(A.FieldSlots, fieldSlotsForMember(Fmts, A.MemberOpcode));
}

TEST(HaydnPlacementAlternativeTest, GetAlternateInstsOpcode_ST32_SparseS0Only) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::S_SW_WITH_IMM);
  ASSERT_NE(Alts, nullptr);
  // Genuinely sparse, and for a reason the slot model cannot state: there is
  // one store unit. LOADSTORE0 appears at P20 and P30 and nowhere else, so a
  // store has two placements while a load has five.
  EXPECT_EQ(liveMembers(Alts),
            sorted({Haydn::S_SW_WITH_IMM_P20_LOADSTORE0,
                    Haydn::S_SW_WITH_IMM_P30_LOADSTORE0}));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_SW_WITH_IMM),
            SlotBits(Haydn::SLOT_P20 | Haydn::SLOT_P30));
}

TEST(HaydnPlacementAlternativeTest, FieldSlotsComeFromTheMemberNotTheIndex) {
  // The alternates index is NOT the slot under format E — it is the placement,
  // the (entry position, unit) pair. This test used to assert
  // `FieldSlots == 1 << alt-index`, which held only while the vector was
  // indexed by slot; it is the invariant the switch breaks (plan § 5.2), so it
  // now asserts the replacement: each row's FieldSlots is the bit of the slot
  // its OWN member names.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  SmallVector<PlacementAlternative, 4> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD64, Alts));
  for (const PlacementAlternative &A : Alts)
    EXPECT_EQ(A.FieldSlots, fieldSlotsForMember(Fmts, A.MemberOpcode));
  // The point restated positively: two members can share a slot while sitting
  // at different placements, so the index cannot be the slot.
  const std::vector<unsigned> *Sparse =
      Fmts.getAlternateInstsOpcode(Haydn::ADD64);
  ASSERT_NE(Sparse, nullptr);
  EXPECT_EQ(Fmts.getSlotKind(Haydn::ADD64_P31_ALU0),
            MCSlotKind(MCSlotKind::Haydn_SLOT_P31));
  EXPECT_EQ(Fmts.getSlotKind(Haydn::ADD64_P31_ALU1),
            MCSlotKind(MCSlotKind::Haydn_SLOT_P31));
  EXPECT_NE(Haydn::ADD64_P31_ALU0, Haydn::ADD64_P31_ALU1);
}

TEST(HaydnPlacementAlternativeTest, LegalSlotsEqualsOROfFieldSlots) {
  // Consistency: getLegalSlots(Opc) == OR FieldSlots from enumerate.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const unsigned Opcodes[] = {
      Haydn::ADD32, Haydn::SUB32,  Haydn::NOT32, Haydn::NEG32,
      Haydn::ADD64, Haydn::S_LW_WITH_IMM,   Haydn::S_SW_WITH_IMM,  Haydn::D_LDW_WITH_IMM,
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
    // Union over the members' OWN slots, not over vector positions.
    SlotBits FromSparse = 0;
    for (unsigned M : liveMembers(Raw))
      FromSparse |= fieldSlotsForMember(Fmts, M);
    EXPECT_EQ(Legal, FromSparse) << "opcode " << Opcode;
    EXPECT_TRUE(hasPlacementAlternatives(Fmts, Opcode) || Legal == 0);
    EXPECT_EQ(getPlacementMemberOpcodes(Fmts, Opcode), Raw);
  }
}

TEST(HaydnPlacementAlternativeTest, UnknownOpcodeReturnsNull) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  // PHI is not a Haydn multi-slot logical.
  EXPECT_EQ(Fmts.getAlternateInstsOpcode(/*Opcode=*/0), nullptr);
  EXPECT_FALSE(hasPlacementAlternatives(Fmts, 0));
}

//===----------------------------------------------------------------------===//
// CompatibleFormatMask (plan §6.1)
//===----------------------------------------------------------------------===//

TEST(HaydnPlacementAlternativeTest, CompatibleFormatMaskCoversBothComposites) {
  // Bundle128 had one row, so the product mask was one bit and "compatible
  // with the product format" was a single question. Format E has two rows and
  // both are product formats, so the mask is both bits.
  EXPECT_EQ(ProductFormatMask, (1ull << 0) | (1ull << 1));
  EXPECT_EQ(formatIDBit(FormatID::BundleE2) | formatIDBit(FormatID::BundleE3),
            ProductFormatMask);

  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  SmallVector<PlacementAlternative, 4> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD32, Alts));
  ASSERT_FALSE(Alts.empty());
  for (const PlacementAlternative &A : Alts) {
    EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
    EXPECT_TRUE(A.isCompatibleWith(ProductFormatID));
    EXPECT_TRUE(A.isCompatibleWith(FormatID::BundleE2));
    EXPECT_TRUE(A.isCompatibleWith(FormatID::BundleE3));
    // FormatID 1 is BundleE3 now, not a spare value, so the fail-closed probe
    // has to reach past BOTH composites to mean anything.
    constexpr FormatID Synth = static_cast<FormatID>(2);
    EXPECT_FALSE(A.isCompatibleWith(Synth));
  }
}

TEST(HaydnPlacementAlternativeTest, FilterAlternativesForFormat_Synthetic) {
  // Past both live composites -- FormatID 1 is BundleE3.
  constexpr FormatID SynthNarrow = static_cast<FormatID>(2);
  const uint64_t SynthMask = formatIDBit(SynthNarrow);
  const uint64_t BothMask = ProductFormatMask | SynthMask;

  SmallVector<PlacementAlternative, 4> Alts;
  Alts.emplace_back(/*MemberOpc=*/Haydn::ADD32_P30_ALU0, ProductFormatMask);
  Alts.emplace_back(/*MemberOpc=*/Haydn::ADD32_P31_ALU0, BothMask);
  Alts.emplace_back(/*MemberOpc=*/Haydn::ADD32_P32_ALU0, SynthMask);

  SmallVector<PlacementAlternative, 4> ForFull = Alts;
  filterAlternativesForFormat(ForFull, FormatID::BundleE2);
  ASSERT_EQ(ForFull.size(), 2u);
  EXPECT_EQ(ForFull[0].MemberOpcode, Haydn::ADD32_P30_ALU0);
  EXPECT_EQ(ForFull[1].MemberOpcode, Haydn::ADD32_P31_ALU0);

  SmallVector<PlacementAlternative, 4> ForSynth = Alts;
  filterAlternativesForFormat(ForSynth, SynthNarrow);
  ASSERT_EQ(ForSynth.size(), 2u);
  EXPECT_EQ(ForSynth[0].MemberOpcode, Haydn::ADD32_P31_ALU0);
  EXPECT_EQ(ForSynth[1].MemberOpcode, Haydn::ADD32_P32_ALU0);
}

TEST(HaydnPlacementAlternativeTest, DefaultCtorStampsProductMask) {
  PlacementAlternative A(Haydn::ADD32_P30_ALU0);
  EXPECT_EQ(A.MemberOpcode, Haydn::ADD32_P30_ALU0);
  EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
  EXPECT_TRUE(A.isCompatibleWith(FormatID::BundleE3));
}

} // namespace
