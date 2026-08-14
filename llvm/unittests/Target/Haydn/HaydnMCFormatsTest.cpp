//===- HaydnMCFormatsTest.cpp - alts-derived slot legality tests -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for HaydnMCFormats::getLegalSlots: slot k is legal iff
// sparse AlternateInsts[k] != 0 (index == field). Alts + setDesc are
// materialize authority; MC encodes member Desc as-is (AIE).
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HaydnBaseInfo.h"
#include "HaydnTestMCInstrInfo.h"
#include "HaydnBundlePlan.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/STLExtras.h"

#include "gtest/gtest.h"

// Haydn::ADD32 / Haydn::NOT32 / ... opcode constants from tablegen.
#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;

namespace {

// Slot masks worth naming once. A legal-slot mask spans BOTH composites and is
// therefore not an occupancy: it says "every entry position this logical has a
// member for", and no bundle ever holds a P2x and a P3x at the same time.
static constexpr SlotBits AluAny = Haydn::SLOT_P20 | Haydn::SLOT_P30 |
                                   Haydn::SLOT_P31 | Haydn::SLOT_P32;
static constexpr SlotBits StoreAny = Haydn::SLOT_P20 | Haydn::SLOT_P30;

// The alternates vector is indexed by PLACEMENT, 0..17, and is sparse; under
// Bundle128 the index was the slot, which is why these tests used to walk it
// as size-3 with `MCSlotKind(Slot)`. Ask each member its own slot instead.
static std::vector<unsigned> liveMembers(const std::vector<unsigned> *Alts) {
  std::vector<unsigned> Out;
  if (Alts)
    for (unsigned M : *Alts)
      if (M != 0)
        Out.push_back(M);
  return Out;
}

static SlotBits slotsOfMembers(const HaydnBaseMCFormats &Fmts,
                               const std::vector<unsigned> *Alts) {
  SlotBits Bits = 0;
  for (unsigned M : liveMembers(Alts)) {
    MCSlotKind K = Fmts.getSlotKind(M);
    if (K != MCSlotKind())
      Bits |= SlotBits(1) << static_cast<unsigned>(K);
  }
  return Bits;
}


TEST(HaydnMCFormatsTest, GetLegalSlotsSpotChecks) {
  // getLegalSlots returns a bitmask (bit k = slot k in Haydn::SLOT convention:
  // SLOT_P30=1<<0, SLOT_P31=1<<1, SLOT_P32=1<<2). Derived from sparse alts.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());

  // These sets span BOTH composites: a legal-slot mask is not an occupancy,
  // it is "every entry position this logical has a member for". P20/P21 belong
  // to the 2-entry form and P30/P31/P32 to the 3-entry one, and the two are
  // mutually exclusive, so no bundle ever occupies a mask like these.
  const SlotBits AluAny = Haydn::SLOT_P20 | Haydn::SLOT_P30 |
                          Haydn::SLOT_P31 | Haydn::SLOT_P32;

  // The ALU32 family reaches entry 0 of the 2-entry form and all three of the
  // 3-entry form. It does NOT reach P21, whose units are ALU1/LOAD1/MAC1 --
  // ALU1 serves it, but these logicals have no P21 member.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::NOT32), AluAny);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::NEG32), AluAny);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD32), AluAny);

  // ADD64 is no longer narrower than ADD32. Bundle128 gave it s1|s2 only;
  // format E gives the 64-bit ALU ops the same placements as the 32-bit ones,
  // which is what makes three ALU64 ops in one bundle legal.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD64), AluAny);

  // The MAC family reaches ONE MORE position than the ALU family: MAC0 serves
  // P20/P30/P31 and MAC1 serves P21/P32, so between them every entry is
  // covered. That is the shape 7.1 calls the balance point, and it is data.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::X2MULA32),
            SlotBits(Haydn::SLOT_MASK_ANY));

  // Loads reach every entry position, because LOADSTORE0 serves P20/P30 and
  // LOAD1 serves P21/P31/P32. Stores reach only the LOADSTORE0 positions:
  // there is one store unit, not two.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_LW_WITH_IMM),
            SlotBits(Haydn::SLOT_MASK_ANY));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_SW_WITH_IMM),
            SlotBits(Haydn::SLOT_P20 | Haydn::SLOT_P30));

  // Members live in the alternates vector, which is indexed by PLACEMENT and
  // not by slot, so it is neither size 3 nor addressable by slot number. Find
  // a member by asking each one its own slot.
  auto memberForSlot = [&](unsigned Opcode, MCSlotKind Want) -> unsigned {
    const std::vector<unsigned> *Alts = Fmts.getAlternateInstsOpcode(Opcode);
    if (!Alts)
      return 0;
    for (unsigned M : *Alts)
      if (M != 0 && Fmts.getSlotKind(M) == Want)
        return M;
    return 0;
  };
  const MCSlotKind P31{MCSlotKind::Haydn_SLOT_P31};
  EXPECT_NE(memberForSlot(Haydn::S_LW_WITH_IMM, P31), 0u);
  EXPECT_NE(memberForSlot(Haydn::D_LDW_WITH_IMM, P31), 0u);
}

TEST(HaydnMCFormatsTest, GetLegalSlotsIsDerivedFromSparseAlts) {
  // getLegalSlots bit k iff getAlternateInstsOpcode[k] != 0.
  // Sparse size-3; members carry fixed getSlotKind (AIE post-setDesc).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const unsigned Opcodes[] = {
      Haydn::ADD32,    Haydn::SUB32,    Haydn::NOT32,     Haydn::NEG32,
      Haydn::ADD64,    Haydn::S_LW_WITH_IMM,     Haydn::S_SW_WITH_IMM,      Haydn::D_LDW_WITH_IMM,
      Haydn::X2MULA32, Haydn::MAX64,    Haydn::SLL64,     Haydn::POPCOUNT32};

  for (unsigned Opcode : Opcodes) {
    SlotBits Legal = Fmts.getLegalSlots(Opcode);
    const std::vector<unsigned> *Alts =
        Fmts.getAlternateInstsOpcode(Opcode);
    if (Legal == 0) {
      EXPECT_TRUE(Alts == nullptr ||
                  llvm::all_of(*Alts, [](unsigned M) { return M == 0; }))
          << "opcode " << Opcode;
      continue;
    }
    ASSERT_NE(Alts, nullptr) << "opcode " << Opcode;
    // The vector is indexed by PLACEMENT -- the (entry position, unit) pair,
    // 0..17 -- not by slot. Under Bundle128 the two coincided, which is why
    // this used to be a size-3 walk with index == slot. Placement 6 and
    // placement 10 are both ALU0 in different entries, so neither the index
    // nor its low bits name a slot (FORMAT-E-SWITCH-PLAN.md 5.2).
    //
    // The invariant that survives the reindexing: getLegalSlots is exactly the
    // union of the members' own slot kinds. Ask each member, never the index.
    SlotBits FromMembers = 0;
    for (unsigned Member : *Alts) {
      if (Member == 0)
        continue; // sparse hole
      EXPECT_NE(Member, Opcode) << "opcode " << Opcode << " alt is itself";
      MCSlotKind Kind = Fmts.getSlotKind(Member);
      EXPECT_NE(Kind, MCSlotKind())
          << "opcode " << Opcode << " member " << Member << " has no slot";
      FromMembers |= SlotBits(1) << static_cast<unsigned>(Kind);
    }
    EXPECT_EQ(Legal, FromMembers)
        << "opcode " << Opcode
        << ": getLegalSlots disagrees with the union of its members' slots";
  }
}

// Regression: the generated PacketFormats table + real ConflictBits are
// consumed (GET_FORMATS_PACKETS_TABLE wired in HaydnMCFormats.cpp).
// Previously ConflictBits was documented as SLOT_SET_E3 (sentinel for "no
// exclusivity info"); with BUNDLE_E3 declared as a composite packet format
// covering {S0,S1,S2}, the backend's computeSlotSets derives ConflictBits =
// self-only for every slot (1/2/4). This test anchors:
//   1. getPacketFormats() returns the generated table (non-empty, has a format
//      covering the full slot set).
//   2. ConflictBits != SLOT_SET_E3 for every slot (the sentinel is gone).
//   3. isFormatAvailable is true for all subsets of {S0,S1,S2} (the Bundle128
//      packet format covers them; behavior preserved vs the hand-authored LUT).
TEST(HaydnMCFormatsTest, PacketFormatsAndConflictBitsFromGeneratedTable) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());

  // (1) getPacketFormats returns a table that covers the full slot set.
  const PacketFormats &Packets = Fmts.getPacketFormats();
  const VLIWFormat *Full =
      Packets.getFormat(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32);
  EXPECT_NE(Full, nullptr);
  ASSERT_TRUE(Full);
  EXPECT_TRUE(Full->covers(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));

  // (2) ConflictBits != SLOT_SET_E3 for every slot. Bundle128 co-emits all three,
  //     so each slot's conflict-closure is self-only (1/2/4).
  const MCSlotKind SlotKinds[] = {MCSlotKind::Haydn_SLOT_P30,
                                  MCSlotKind::Haydn_SLOT_P31,
                                  MCSlotKind::Haydn_SLOT_P32};
  const SlotBits SelfBits[] = {Haydn::SLOT_P30, Haydn::SLOT_P31, Haydn::SLOT_P32};
  for (size_t I = 0; I < 3; ++I) {
    const MCSlotInfo *SI = Fmts.getSlotInfo(SlotKinds[I]);
    ASSERT_NE(SI, nullptr) << "no SlotInfo for slot " << I;
    // Self plus the whole other composite: an E3 slot cannot co-issue with an
    // E2 one. Self-only was Bundle128's degenerate case, not the model.
    EXPECT_EQ(SI->getConflictSet(),
              SlotBits(SelfBits[I] | Haydn::SLOT_SET_E2))
        << "slot " << I;
  }

  // (3) A combo is packetable iff it fits inside one composite; the two are
  //     mutually exclusive, so mixed masks are not.
  for (SlotBits Combo = 0; Combo <= Haydn::SLOT_MASK_ANY; ++Combo) {
    const bool Fits = (Combo & ~SlotBits(Haydn::SLOT_SET_E2)) == 0 ||
                      (Combo & ~SlotBits(Haydn::SLOT_SET_E3)) == 0;
    EXPECT_EQ(Fmts.isFormatAvailable(Combo), Fits) << "combo " << Combo;
  }
}

//===----------------------------------------------------------------------===//
// Extended alts-derived legality coverage.
// Pins legal-slot families; materialize via AlternateInsts + setDesc.
//===----------------------------------------------------------------------===//

TEST(HaydnMCFormatsTest, LegalSlotFamiliesByFU) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());

  // The ALU32 family: entry 0 of the 2-entry form, every entry of the 3-entry
  // one. Not P21, whose ALU is ALU1 -- these logicals have no P21 member.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD32), AluAny);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SUB32), AluAny);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::XOR32), AluAny);

  // ADDI32 is the narrow one, and for a size reason rather than a unit one:
  // its imm20 only fits the wide 2-entry windows, so it has P20 and P21 and
  // no 3-entry placement at all.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADDI32), SlotBits(Haydn::SLOT_SET_E2));

  // The 64-bit ALU ops are no longer narrower than the 32-bit ones.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD64), AluAny);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SLL64), AluAny);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::MAX64), AluAny);

  // Loads reach every entry (LOADSTORE0 at P20/P30, LOAD1 at P21/P31/P32);
  // stores reach only the LOADSTORE0 ones, because there is one store unit.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_LW_WITH_IMM),
            SlotBits(Haydn::SLOT_MASK_ANY));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::D_LDW_WITH_IMM),
            SlotBits(Haydn::SLOT_MASK_ANY));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_SW_WITH_IMM), StoreAny);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::D_SDW_WITH_IMM), StoreAny);

  // MAC0 at P20/P30/P31 and MAC1 at P21/P32 -- between them, everywhere.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::X2MULA32),
            SlotBits(Haydn::SLOT_MASK_ANY));
}

TEST(HaydnMCFormatsTest, SparseAltsMatchLegalBitsExhaustive) {
  // Broader opcode sample: every legal bit has a non-zero sparse alt;
  // illegal bits are 0. Members have fixed getSlotKind (alts-derived).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const unsigned Opcodes[] = {
      Haydn::ADD32,    Haydn::SUB32,    Haydn::XOR32,     Haydn::NOT32,
      Haydn::NEG32,    Haydn::ADDI32,   Haydn::ADD64,     Haydn::S_LW_WITH_IMM,
      Haydn::S_SW_WITH_IMM,     Haydn::D_LDW_WITH_IMM,     Haydn::D_SDW_WITH_IMM,      Haydn::X2MULA32,
      Haydn::MAX64,    Haydn::SLL64,    Haydn::POPCOUNT32, Haydn::ARCTAN,
      Haydn::SIN_COS,  Haydn::SET_HWLOOP};
  for (unsigned Opcode : Opcodes) {
    SlotBits Legal = Fmts.getLegalSlots(Opcode);
    const std::vector<unsigned> *Alts =
        Fmts.getAlternateInstsOpcode(Opcode);
    if (Legal == 0) {
      EXPECT_TRUE(Alts == nullptr ||
                  llvm::all_of(*Alts, [](unsigned M) { return M == 0; }))
          << "opcode " << Opcode;
      continue;
    }
    ASSERT_NE(Alts, nullptr) << "opcode " << Opcode;
    // Every legal slot is claimed by at least one member, and every member
    // claims a legal slot. Stated as a set equality because the vector's
    // positions carry no meaning.
    EXPECT_EQ(Legal, slotsOfMembers(Fmts, Alts)) << "opcode " << Opcode;
    for (unsigned M : liveMembers(Alts)) {
      EXPECT_NE(M, Opcode)
          << "member must be a distinct private encode opcode";
      MCSlotKind K = Fmts.getSlotKind(M);
      EXPECT_NE(K, MCSlotKind()) << "opcode " << Opcode << " member " << M;
      EXPECT_NE(Legal & (SlotBits(1) << static_cast<unsigned>(K)), 0u)
          << "opcode " << Opcode << " member " << M;
    }
  }
}

TEST(HaydnMCFormatsTest, SingleSlotFamiliesNeverClaimAllThree) {
  // Guards against accidental sparse-alt rows that would let ST* steal S1/S2
  // and starve ALU/MAC co-issue (pack/IPC regression class).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  // Stores reach only the LOADSTORE0 positions. The constraint is the UNIT --
  // there is one store unit and LOAD1 cannot store -- which is why P21/P31/P32
  // stay closed to them however the entry layout is re-delivered.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_SW_WITH_IMM), StoreAny);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::D_SDW_WITH_IMM), StoreAny);

  // ADD64 claiming P30 is no longer a regression, it is the point: format E
  // gives the 64-bit ALU ops the same placements as the 32-bit ones.
  EXPECT_NE(Fmts.getLegalSlots(Haydn::ADD64) & Haydn::SLOT_P30, 0u);
}

// BREV logicals have sparse size-3 alts (LS *_S* members) so unconditional
// setDesc has a legal member per field. LD *_LD_S* are not
// PlacementAlternatives of the logical (encode peers only).
TEST(HaydnMCFormatsTest, BrevLogicalsHaveSparseAltsForSetDesc) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const unsigned Logicals[] = {
      Haydn::S_SW_BREV_IMM, Haydn::S_SW_BREV_REG, Haydn::D_SDW_BREV_IMM,
      Haydn::D_SDW_BREV_REG, Haydn::D_LDW_BREV_IMM, Haydn::D_LDW_BREV_REG,
      Haydn::S_LW_BREV_IMM, Haydn::S_LW_BREV_REG};
  for (unsigned Opcode : Logicals) {
    const std::vector<unsigned> *Alts =
        Fmts.getAlternateInstsOpcode(Opcode);
    ASSERT_NE(Alts, nullptr) << "opcode " << Opcode;
    std::vector<unsigned> Members = liveMembers(Alts);
    EXPECT_FALSE(Members.empty()) << "opcode " << Opcode;
    for (unsigned M : Members) {
      EXPECT_NE(M, Opcode) << "opcode " << Opcode;
      // The member carries its own slot; the position it sits at does not.
      EXPECT_NE(Fmts.getSlotKind(M), MCSlotKind())
          << "opcode " << Opcode << " member " << M;
    }
    EXPECT_EQ(Fmts.getLegalSlots(Opcode), slotsOfMembers(Fmts, Alts))
        << "opcode " << Opcode;
  }
}

TEST(HaydnMCFormatsTest, PacketFormatCoversEveryOccupiedSubset) {
  // Bundle128 product: every non-empty subset of {S0,S1,S2} has a covering
  // packet format (NOPs fill holes). Empty occupancy is also available for
  // stall accounting at higher layers.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const PacketFormats &Packets = Fmts.getPacketFormats();
  // An occupancy is available iff it fits inside ONE composite. Bundle128 had
  // a single row covering everything, so every subset was available and the
  // loop could not fail; format E's rows are mutually exclusive, so a mask
  // mixing a 2-entry and a 3-entry slot -- P20|P30, say -- is not a bundle
  // anyone can build and must come back unavailable.
  for (SlotBits Combo = 0; Combo <= Haydn::SLOT_MASK_ANY; ++Combo) {
    const bool FitsE2 = (Combo & ~SlotBits(Haydn::SLOT_SET_E2)) == 0;
    const bool FitsE3 = (Combo & ~SlotBits(Haydn::SLOT_SET_E3)) == 0;
    EXPECT_EQ(Fmts.isFormatAvailable(Combo), FitsE2 || FitsE3)
        << "combo=" << Combo;
    if (Combo == 0 || !(FitsE2 || FitsE3))
      continue;
    if (const VLIWFormat *F = Packets.getFormat(Combo))
      EXPECT_TRUE(F->covers(Combo)) << "combo=" << Combo;
  }
}

TEST(HaydnMCFormatsTest, ConflictClosureMakesTheCompositesExclusive) {
  // Bundle128 covered all three slots with ONE packet format, so every slot
  // co-emitted with every other and the conflict closure degenerated to
  // self-only. Format E has two composites and they are mutually exclusive, so
  // each slot now conflicts with itself AND with every slot of the other
  // composite. This is what makes "which entry count" a lookup rather than a
  // decision (5.2).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  struct Row { MCSlotKind Kind; SlotBits Self; SlotBits Other; };
  const Row Rows[] = {
      {MCSlotKind::Haydn_SLOT_P20, Haydn::SLOT_P20, Haydn::SLOT_SET_E3},
      {MCSlotKind::Haydn_SLOT_P21, Haydn::SLOT_P21, Haydn::SLOT_SET_E3},
      {MCSlotKind::Haydn_SLOT_P30, Haydn::SLOT_P30, Haydn::SLOT_SET_E2},
      {MCSlotKind::Haydn_SLOT_P31, Haydn::SLOT_P31, Haydn::SLOT_SET_E2},
      {MCSlotKind::Haydn_SLOT_P32, Haydn::SLOT_P32, Haydn::SLOT_SET_E2},
  };
  for (const Row &R : Rows) {
    const MCSlotInfo *SI = Fmts.getSlotInfo(R.Kind);
    ASSERT_NE(SI, nullptr);
    EXPECT_EQ(SI->getSlotSet(), R.Self);
    EXPECT_EQ(SI->getConflictSet(), SlotBits(R.Self | R.Other));
  }
}

//===----------------------------------------------------------------------===//
// Extended alts / encode-parity matrix
//===----------------------------------------------------------------------===//

TEST(HaydnMCFormatsTest, LockedDspOpsHaveAltSlots) {
  // ARCTAN/SIN_COS are issue-alone at schedule time; alts still need ≥1
  // member so standalone Bundle128 emit works.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  EXPECT_NE(Fmts.getLegalSlots(Haydn::ARCTAN), 0u);
  EXPECT_NE(Fmts.getLegalSlots(Haydn::SIN_COS), 0u);
  const std::vector<unsigned> *AAlts =
      Fmts.getAlternateInstsOpcode(Haydn::ARCTAN);
  const std::vector<unsigned> *SAlts =
      Fmts.getAlternateInstsOpcode(Haydn::SIN_COS);
  ASSERT_NE(AAlts, nullptr);
  ASSERT_NE(SAlts, nullptr);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ARCTAN), slotsOfMembers(Fmts, AAlts));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SIN_COS), slotsOfMembers(Fmts, SAlts));

  // Both are ALU2/ALU1-only, and ALU2 never appears in the 2-entry form, so
  // they have NO 2-entry placement. This is the concrete case behind 5.2's
  // "give a solitary instruction no slot hint": hinting entry 0 of a 2-entry
  // bundle would have had nowhere to put them.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ARCTAN), SlotBits(Haydn::SLOT_SET_E3));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SIN_COS), SlotBits(Haydn::SLOT_SET_E3));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ARCTAN) & Haydn::SLOT_SET_E2, 0u);
}

TEST(HaydnMCFormatsTest, SetHwloopFitsOnlyTheWideEntry) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  // SET_HWLOOP has exactly ONE placement, P20, and the reason is width rather
  // than units: it needs 35 bits of operands, which only the 45-bit entry 0 of
  // the 2-entry form has room for (5.1). So a hardware-loop setup forces its
  // bundle to be BUNDLE_E2.
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::SET_HWLOOP);
  ASSERT_NE(Alts, nullptr);
  EXPECT_EQ(liveMembers(Alts).size(), 1u);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SET_HWLOOP), SlotBits(Haydn::SLOT_P20));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SET_HWLOOP), slotsOfMembers(Fmts, Alts));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SET_HWLOOP) & Haydn::SLOT_SET_E3, 0u);
}

TEST(HaydnMCFormatsTest, LegalSlotsSubsetOfSlotAll) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const unsigned Opcodes[] = {
      Haydn::ADD32, Haydn::ADD64, Haydn::S_LW_WITH_IMM, Haydn::S_SW_WITH_IMM, Haydn::D_LDW_WITH_IMM,
      Haydn::D_SDW_WITH_IMM,  Haydn::X2MULA32, Haydn::ARCTAN, Haydn::SIN_COS,
      Haydn::ADDI32, Haydn::NOT32, Haydn::POPCOUNT32};
  for (unsigned Opc : Opcodes) {
    SlotBits L = Fmts.getLegalSlots(Opc);
    // There is deliberately no SLOT_ALL: a full bundle is E2's mask or E3's
    // depending on the composite. SLOT_MASK_ANY is a range check, not an
    // occupancy, and that is all this test can mean now.
    EXPECT_EQ(L & ~SlotBits(Haydn::SLOT_MASK_ANY), 0u) << "opc=" << Opc;
    EXPECT_NE(L, 0u) << "opc=" << Opc;
  }
}

TEST(HaydnMCFormatsTest, GetPacketFormatBySizeIsTheParcel) {
  // Both composites are the same size, so a size lookup cannot distinguish
  // them -- the SlotSet does. That equality is what lets one default row
  // survive at all (5.2).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const PacketFormats &P = Fmts.getPacketFormats();
  const VLIWFormat *BySize = P.getFormatBySize(
      Haydn::SLOT_SET_E3, llvm::haydn::bundle::ProductEncodedBytesValue);
  ASSERT_NE(BySize, nullptr);
  EXPECT_EQ(BySize->getSize(), llvm::haydn::bundle::ProductEncodedBytesValue);
  EXPECT_STREQ(BySize->Name, "BUNDLE_E3");
}

TEST(HaydnMCFormatsTest, SparseAltsDistinctPerSlotWhenLegal) {
  // AlternateInsts members are distinct per legal slot (alts-derived
  // member identity per field).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD32);
  ASSERT_NE(Alts, nullptr);
  // Members are distinct from each other and from the logical, but they are
  // NOT one per slot: ADD32 has two members at each 3-entry position, one per
  // ALU. Distinctness is the property; a bijection with slots is not.
  std::vector<unsigned> Members = liveMembers(Alts);
  EXPECT_EQ(Members.size(), 7u);
  std::vector<unsigned> Unique = Members;
  llvm::sort(Unique);
  Unique.erase(std::unique(Unique.begin(), Unique.end()), Unique.end());
  EXPECT_EQ(Unique.size(), Members.size()) << "members must be distinct";
  for (unsigned M : Members) {
    EXPECT_NE(M, Haydn::ADD32);
    EXPECT_NE(Fmts.getSlotKind(M), MCSlotKind());
  }
  // Two members sharing one slot is the normal case, and is exactly why the
  // vector cannot be indexed by slot.
  EXPECT_EQ(Fmts.getSlotKind(Haydn::ADD32_P31_ALU0),
            Fmts.getSlotKind(Haydn::ADD32_P31_ALU1));
}

// stripHaydnMemberSuffix is the one place that knows how a placed member is
// spelled. Bundle128 is live; format E is generated but not yet included, so
// its spelling can only be covered here until the encoding switches.

TEST(HaydnMemberSuffix, LogicalIsNotAMember) {
  EXPECT_FALSE(stripHaydnMemberSuffix("ADD32").has_value());
  EXPECT_FALSE(stripHaydnMemberSuffix("JAL").has_value());
  // Logicals whose own name ends in a word must not look placed.
  EXPECT_FALSE(stripHaydnMemberSuffix("ADD32_W").has_value());
  EXPECT_FALSE(stripHaydnMemberSuffix("D_LDW_POST_IMM").has_value());
  EXPECT_FALSE(stripHaydnMemberSuffix("SET_HWLOOP_F2_W").has_value());
}

TEST(HaydnMemberSuffix, Bundle128Slots) {
  EXPECT_EQ(stripHaydnMemberSuffix("ADD32_S0"), StringRef("ADD32"));
  EXPECT_EQ(stripHaydnMemberSuffix("ADD32_S1"), StringRef("ADD32"));
  EXPECT_EQ(stripHaydnMemberSuffix("ADD32_S2"), StringRef("ADD32"));
  EXPECT_EQ(stripHaydnMemberSuffix("JAL_S0"), StringRef("JAL"));
  EXPECT_EQ(stripHaydnMemberSuffix("ADDI32_W_S0"), StringRef("ADDI32_W"));
  // `_S3` is not a slot.
  EXPECT_FALSE(stripHaydnMemberSuffix("ADD32_S3").has_value());
}

TEST(HaydnMemberSuffix, FormatEPlacements) {
  EXPECT_EQ(stripHaydnMemberSuffix("JAL_P20_ALU0"), StringRef("JAL"));
  EXPECT_EQ(stripHaydnMemberSuffix("BEQ_P31_ALU0"), StringRef("BEQ"));
  EXPECT_EQ(stripHaydnMemberSuffix("ADD32_P32_ALU2"), StringRef("ADD32"));
  EXPECT_EQ(stripHaydnMemberSuffix("X2MULA32_P30_MAC0"), StringRef("X2MULA32"));
  EXPECT_EQ(stripHaydnMemberSuffix("LD32_P21_LOAD1"), StringRef("LD32"));
  // The unit infix that defeats a plain `_S<k>` strip is not special here:
  // the logical is exactly the part before `_P<form><pos>`.
  EXPECT_EQ(stripHaydnMemberSuffix("D_LDW_BREV_IMM_P20_LOADSTORE0"),
            StringRef("D_LDW_BREV_IMM"));
  EXPECT_EQ(stripHaydnMemberSuffix("D_LDW_POST_IMM_P31_LOAD1"),
            StringRef("D_LDW_POST_IMM"));
}

TEST(HaydnMemberSuffix, FormatENearMissesAreNotMembers) {
  // A real unit name, but no placement token in front of it.
  EXPECT_FALSE(stripHaydnMemberSuffix("SOMETHING_ALU0").has_value());
  // Placement token present, unit name not one of the seven.
  EXPECT_FALSE(stripHaydnMemberSuffix("ADD32_P20_ALU9").has_value());
  // Form/position must be exactly two digits.
  EXPECT_FALSE(stripHaydnMemberSuffix("ADD32_P2_ALU0").has_value());
  EXPECT_FALSE(stripHaydnMemberSuffix("ADD32_P200_ALU0").has_value());
  EXPECT_FALSE(stripHaydnMemberSuffix("ADD32_PXY_ALU0").has_value());
}

//===----------------------------------------------------------------------===//
// The unit axis
//===----------------------------------------------------------------------===//
//
// A bundle entry uses exactly one hardware unit and no two entries may share
// one. That is a SECOND axis: slot occupancy says where in the bundle, unit
// occupancy says which hardware serves it, and neither implies the other.
//
// The axis is inert while Bundle128 is live — its members carry no unit,
// because its slot model pinned one unit per slot and the slot already said
// everything — so these tests are the only place the mechanism is exercised
// until the encoding switches. Same arrangement as the format E member
// spellings above, and the same reason: a pure function can be tested before
// the encoding that produces its inputs is the live one.

TEST(HaydnMemberUnit, Bundle128MembersHaveNoUnit) {
  // Not a gap. Under Bundle128 the slot IS the unit, so there is nothing for
  // the spelling to carry, and "no unit" must read as "no unit constraint"
  // rather than as a parse failure.
  EXPECT_FALSE(haydnMemberUnitFromName("ADD32_S0").has_value());
  EXPECT_FALSE(haydnMemberUnitFromName("ADD32_S2").has_value());
  EXPECT_FALSE(haydnMemberUnitFromName("JAL_S0").has_value());
  EXPECT_EQ(haydnMemberUnitBits("ADD32_S1"), Haydn::UnitBits(0));
  // A logical is not a member and has no unit either.
  EXPECT_FALSE(haydnMemberUnitFromName("ADD32").has_value());
  EXPECT_EQ(haydnMemberUnitBits("ADD32"), Haydn::UnitBits(0));
}

TEST(HaydnMemberUnit, FormatEMembersNameTheirUnit) {
  EXPECT_EQ(haydnMemberUnitFromName("JAL_P20_ALU0"), Haydn::Unit::ALU0);
  EXPECT_EQ(haydnMemberUnitFromName("ADD32_P32_ALU2"), Haydn::Unit::ALU2);
  EXPECT_EQ(haydnMemberUnitFromName("X2MULA32_P30_MAC0"), Haydn::Unit::MAC0);
  EXPECT_EQ(haydnMemberUnitFromName("LD32_P21_LOAD1"), Haydn::Unit::LOAD1);
  EXPECT_EQ(haydnMemberUnitFromName("D_LDW_BREV_IMM_P20_LOADSTORE0"),
            Haydn::Unit::LOADSTORE0);
  // LOADSTORE0 vs LOAD1 share a prefix; the longer name must win.
  EXPECT_EQ(haydnMemberUnitFromName("ST32_P30_LOADSTORE0"),
            Haydn::Unit::LOADSTORE0);
}

TEST(HaydnMemberUnit, NamesRoundTrip) {
  for (unsigned I = 0; I != Haydn::UNIT_COUNT; ++I) {
    auto U = static_cast<Haydn::Unit>(I);
    std::string Member = std::string("ADD32_P30_") + haydnUnitName(U).str();
    EXPECT_EQ(haydnMemberUnitFromName(Member), U) << "member " << Member;
  }
}

TEST(HaydnMemberUnit, SlotAndUnitAreIndependentAxes) {
  // Same unit, different entries — a bundle may hold only ONE of these, and
  // the slot check alone would happily take both.
  EXPECT_EQ(haydnMemberUnitFromName("ADD32_P30_ALU0"), Haydn::Unit::ALU0);
  EXPECT_EQ(haydnMemberUnitFromName("ADD32_P31_ALU0"), Haydn::Unit::ALU0);
  EXPECT_EQ(haydnMemberUnitBits("ADD32_P30_ALU0"),
            haydnMemberUnitBits("ADD32_P31_ALU0"));

  // Same entry, different units — a bundle may hold only one of these either,
  // but for the OTHER reason, and the unit check alone would allow both.
  EXPECT_NE(haydnMemberUnitBits("ADD32_P30_ALU0"),
            haydnMemberUnitBits("LD32_P30_LOADSTORE0"));
}

TEST(HaydnMemberUnit, BitsAreDistinctAndSingle) {
  Haydn::UnitBits Seen = 0;
  for (unsigned I = 0; I != Haydn::UNIT_COUNT; ++I) {
    Haydn::UnitBits B = Haydn::unitBit(static_cast<Haydn::Unit>(I));
    EXPECT_TRUE(llvm::isPowerOf2_32(B)) << "unit " << I << " is not one bit";
    EXPECT_EQ(Seen & B, Haydn::UnitBits(0)) << "unit " << I << " bit reused";
    Seen |= B;
  }
}

} // end anonymous namespace
