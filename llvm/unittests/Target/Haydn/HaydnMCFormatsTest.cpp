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
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/STLExtras.h"

#include "gtest/gtest.h"

// Haydn::ADD32 / Haydn::NOT32 / ... opcode constants from tablegen.
#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;

namespace {

TEST(HaydnMCFormatsTest, GetLegalSlotsSpotChecks) {
  // getLegalSlots returns a bitmask (bit k = slot k in Haydn::SLOT convention:
  // SLOT_P30=1<<0, SLOT_P31=1<<1, SLOT_P32=1<<2). Derived from sparse alts.
  HaydnMCFormats Fmts;

  // ALU32 unary family (NOT32/NEG32) packs into all three slots.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::NOT32),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::NEG32),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));

  // ALU32 RR (ADD32) — sparse alts S0|S1|S2.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD32),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));

  // ALU64 (ADD64) — s1/s2 only (no S0).
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD64),
            SlotBits(Haydn::SLOT_P31 | Haydn::SLOT_P32));

  // CB-111: LD32 is logical S0|S1; stores S0.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_LW_WITH_IMM),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_SW_WITH_IMM), SlotBits(Haydn::SLOT_P30));

  // Materialize members live in sparse alts (alts-derived getLegalSlots).
  const std::vector<unsigned> *LD32Alts =
      Fmts.getAlternateInstsOpcode(Haydn::S_LW_WITH_IMM);
  const std::vector<unsigned> *LD64Alts =
      Fmts.getAlternateInstsOpcode(Haydn::D_LDW_WITH_IMM);
  ASSERT_NE(LD32Alts, nullptr);
  ASSERT_NE(LD64Alts, nullptr);
  ASSERT_EQ(LD32Alts->size(), 3u);
  ASSERT_EQ(LD64Alts->size(), 3u);
  EXPECT_NE((*LD32Alts)[1], 0u);
  EXPECT_NE((*LD64Alts)[1], 0u);
  EXPECT_EQ(Fmts.getSlotKind((*LD32Alts)[1]),
            MCSlotKind(MCSlotKind::Haydn_SLOT_P31));
  EXPECT_EQ(Fmts.getSlotKind((*LD64Alts)[1]),
            MCSlotKind(MCSlotKind::Haydn_SLOT_P31));

  // MAC (X2MULA32) — legal in s1|s2.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::X2MULA32),
            SlotBits(Haydn::SLOT_P31 | Haydn::SLOT_P32));
}

TEST(HaydnMCFormatsTest, GetLegalSlotsIsDerivedFromSparseAlts) {
  // getLegalSlots bit k iff getAlternateInstsOpcode[k] != 0.
  // Sparse size-3; members carry fixed getSlotKind (AIE post-setDesc).
  HaydnMCFormats Fmts;
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
    ASSERT_EQ(Alts->size(), 3u) << "opcode " << Opcode << " sparse size-3";
    for (unsigned Slot = 0; Slot < 3; ++Slot) {
      SlotBits Bit = SlotBits(1) << Slot;
      bool LegalHere = (Legal & Bit) != 0;
      bool HasAlt = ((*Alts)[Slot] != 0);
      EXPECT_EQ(LegalHere, HasAlt)
          << "opcode " << Opcode << " slot " << Slot
          << ": getLegalSlots says " << LegalHere << " but sparse alt says "
          << HasAlt;
      // Member Desc has fixed getSlotKind == field (AIEBaseMCFormats.cpp:66-75).
      if (HasAlt) {
        EXPECT_NE((*Alts)[Slot], Opcode) << "opcode " << Opcode;
        EXPECT_EQ(Fmts.getSlotKind((*Alts)[Slot]),
                  MCSlotKind(static_cast<int>(Slot)))
            << "opcode " << Opcode << " slot " << Slot;
      }
    }
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
  HaydnMCFormats Fmts;

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
    EXPECT_NE(SI->getConflictSet(), SlotBits(Haydn::SLOT_SET_E3))
        << "slot " << I << " still has SLOT_SET_E3 ConflictBits sentinel";
    // Self-only: ConflictBits == this slot's own bit.
    EXPECT_EQ(SI->getConflictSet(), SelfBits[I])
        << "slot " << I << " ConflictBits should be self-only under Bundle128";
  }

  // (3) isFormatAvailable for all 8 slot-combos. With Bundle128 covering all 3
  //     slots, every subset is packetable (NOPs fill unused slots).
  for (SlotBits Combo = 0; Combo <= Haydn::SLOT_SET_E3; ++Combo) {
    EXPECT_TRUE(Fmts.isFormatAvailable(Combo))
        << "combo " << Combo << " should be available under Bundle128";
  }
}

//===----------------------------------------------------------------------===//
// Extended alts-derived legality coverage.
// Pins legal-slot families; materialize via AlternateInsts + setDesc.
//===----------------------------------------------------------------------===//

TEST(HaydnMCFormatsTest, LegalSlotFamiliesByFU) {
  HaydnMCFormats Fmts;

  // ALU32 binary / imm family: full 3-slot issue.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD32),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SUB32),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::XOR32),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADDI32),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));

  // ALU64 / shift64: no S0.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD64),
            SlotBits(Haydn::SLOT_P31 | Haydn::SLOT_P32));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SLL64),
            SlotBits(Haydn::SLOT_P31 | Haydn::SLOT_P32));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::MAX64),
            SlotBits(Haydn::SLOT_P31 | Haydn::SLOT_P32));

  // Loads: dual-issue S0|S1 (CB-111); stores stay S0-primary.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_LW_WITH_IMM),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::D_LDW_WITH_IMM),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_SW_WITH_IMM), SlotBits(Haydn::SLOT_P30));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::D_SDW_WITH_IMM), SlotBits(Haydn::SLOT_P30));

  // MAC dual-issue S1|S2.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::X2MULA32),
            SlotBits(Haydn::SLOT_P31 | Haydn::SLOT_P32));
}

TEST(HaydnMCFormatsTest, SparseAltsMatchLegalBitsExhaustive) {
  // Broader opcode sample: every legal bit has a non-zero sparse alt;
  // illegal bits are 0. Members have fixed getSlotKind (alts-derived).
  HaydnMCFormats Fmts;
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
    ASSERT_EQ(Alts->size(), 3u) << "opcode " << Opcode;
    for (unsigned Slot = 0; Slot < 3; ++Slot) {
      bool LegalHere = (Legal & (SlotBits(1) << Slot)) != 0;
      bool HasAlt = ((*Alts)[Slot] != 0);
      EXPECT_EQ(LegalHere, HasAlt)
          << "opcode " << Opcode << " slot " << Slot
          << " legal=" << LegalHere << " alt=" << HasAlt;
      if (HasAlt) {
        EXPECT_NE((*Alts)[Slot], Opcode)
            << "member must be a distinct private encode opcode";
        EXPECT_EQ(Fmts.getSlotKind((*Alts)[Slot]),
                  MCSlotKind(static_cast<int>(Slot)))
            << "opcode " << Opcode << " slot " << Slot;
      }
    }
  }
}

TEST(HaydnMCFormatsTest, SingleSlotFamiliesNeverClaimAllThree) {
  // Guards against accidental sparse-alt rows that would let ST* steal S1/S2
  // and starve ALU/MAC co-issue (pack/IPC regression class).
  HaydnMCFormats Fmts;
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::S_SW_WITH_IMM) &
                SlotBits(Haydn::SLOT_P31 | Haydn::SLOT_P32),
            0u);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::D_SDW_WITH_IMM) &
                SlotBits(Haydn::SLOT_P31 | Haydn::SLOT_P32),
            0u);
  // ALU64 must never claim S0.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD64) & Haydn::SLOT_P30, 0u);
}

// BREV logicals have sparse size-3 alts (LS *_S* members) so unconditional
// setDesc has a legal member per field. LD *_LD_S* are not
// PlacementAlternatives of the logical (encode peers only).
TEST(HaydnMCFormatsTest, BrevLogicalsHaveSparseAltsForSetDesc) {
  HaydnMCFormats Fmts;
  const unsigned Logicals[] = {
      Haydn::S_SW_BREV_IMM, Haydn::S_SW_BREV_REG, Haydn::D_SDW_BREV_IMM,
      Haydn::D_SDW_BREV_REG, Haydn::D_LDW_BREV_IMM, Haydn::D_LDW_BREV_REG,
      Haydn::S_LW_BREV_IMM, Haydn::S_LW_BREV_REG};
  for (unsigned Opcode : Logicals) {
    const std::vector<unsigned> *Alts =
        Fmts.getAlternateInstsOpcode(Opcode);
    ASSERT_NE(Alts, nullptr) << "opcode " << Opcode;
    ASSERT_EQ(Alts->size(), 3u) << "opcode " << Opcode;
    unsigned NonZero = 0;
    for (unsigned Slot = 0; Slot < 3; ++Slot) {
      if ((*Alts)[Slot] == 0)
        continue;
      ++NonZero;
      EXPECT_NE((*Alts)[Slot], Opcode);
      // Member has fixed getSlotKind == field index (AIE getSlotKind peer).
      EXPECT_EQ(Fmts.getSlotKind((*Alts)[Slot]), MCSlotKind(static_cast<int>(Slot)))
          << "opcode " << Opcode << " slot " << Slot;
      // Legal bit tracks sparse hole.
      EXPECT_NE(Fmts.getLegalSlots(Opcode) & (SlotBits(1) << Slot), 0u)
          << "opcode " << Opcode << " slot " << Slot;
    }
    EXPECT_GE(NonZero, 1u) << "opcode " << Opcode;
  }
}

TEST(HaydnMCFormatsTest, PacketFormatCoversEveryOccupiedSubset) {
  // Bundle128 product: every non-empty subset of {S0,S1,S2} has a covering
  // packet format (NOPs fill holes). Empty occupancy is also available for
  // stall accounting at higher layers.
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  for (SlotBits Combo = 0; Combo <= Haydn::SLOT_SET_E3; ++Combo) {
    EXPECT_TRUE(Fmts.isFormatAvailable(Combo)) << "combo=" << Combo;
    if (Combo == 0)
      continue;
    const VLIWFormat *F = Packets.getFormat(Combo);
    // getFormat may return the full covering format even for subsets.
    if (F)
      EXPECT_TRUE(F->covers(Combo)) << "combo=" << Combo;
  }
}

TEST(HaydnMCFormatsTest, SlotInfoSelfOnlyConflictUnderBundle128) {
  // Conflict closure is self-only → any two distinct slots co-issue.
  // When multi-format lands, this pin must tighten (CompatibleFormatMask).
  HaydnMCFormats Fmts;
  const MCSlotKind Kinds[] = {MCSlotKind::Haydn_SLOT_P30,
                              MCSlotKind::Haydn_SLOT_P31,
                              MCSlotKind::Haydn_SLOT_P32};
  for (unsigned I = 0; I < 3; ++I) {
    const MCSlotInfo *SI = Fmts.getSlotInfo(Kinds[I]);
    ASSERT_NE(SI, nullptr);
    SlotBits Self = SlotBits(1) << I;
    EXPECT_EQ(SI->getSlotSet(), Self);
    EXPECT_EQ(SI->getConflictSet(), Self);
  }
}

//===----------------------------------------------------------------------===//
// Extended alts / encode-parity matrix
//===----------------------------------------------------------------------===//

TEST(HaydnMCFormatsTest, LockedDspOpsHaveAltSlots) {
  // ARCTAN/SIN_COS are issue-alone at schedule time; alts still need ≥1
  // member so standalone Bundle128 emit works.
  HaydnMCFormats Fmts;
  EXPECT_NE(Fmts.getLegalSlots(Haydn::ARCTAN), 0u);
  EXPECT_NE(Fmts.getLegalSlots(Haydn::SIN_COS), 0u);
  const std::vector<unsigned> *AAlts =
      Fmts.getAlternateInstsOpcode(Haydn::ARCTAN);
  const std::vector<unsigned> *SAlts =
      Fmts.getAlternateInstsOpcode(Haydn::SIN_COS);
  ASSERT_NE(AAlts, nullptr);
  ASSERT_NE(SAlts, nullptr);
  ASSERT_EQ(AAlts->size(), 3u);
  ASSERT_EQ(SAlts->size(), 3u);
  for (unsigned Slot = 0; Slot < 3; ++Slot) {
    bool LegalA =
        (Fmts.getLegalSlots(Haydn::ARCTAN) & (SlotBits(1) << Slot)) != 0;
    EXPECT_EQ(LegalA, (*AAlts)[Slot] != 0u);
    bool LegalS =
        (Fmts.getLegalSlots(Haydn::SIN_COS) & (SlotBits(1) << Slot)) != 0;
    EXPECT_EQ(LegalS, (*SAlts)[Slot] != 0u);
  }
}

TEST(HaydnMCFormatsTest, SetHwloopLegalOnS0) {
  HaydnMCFormats Fmts;
  // SET_HWLOOP is S0 setup (spec); sparse alt supplies S0 member for setDesc.
  EXPECT_NE(Fmts.getLegalSlots(Haydn::SET_HWLOOP) & Haydn::SLOT_P30, 0u);
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::SET_HWLOOP);
  ASSERT_NE(Alts, nullptr);
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_NE((*Alts)[0], 0u);
  EXPECT_EQ(Fmts.getSlotKind((*Alts)[0]),
            MCSlotKind(MCSlotKind::Haydn_SLOT_P30));
}

TEST(HaydnMCFormatsTest, LegalSlotsSubsetOfSlotAll) {
  HaydnMCFormats Fmts;
  const unsigned Opcodes[] = {
      Haydn::ADD32, Haydn::ADD64, Haydn::S_LW_WITH_IMM, Haydn::S_SW_WITH_IMM, Haydn::D_LDW_WITH_IMM,
      Haydn::D_SDW_WITH_IMM,  Haydn::X2MULA32, Haydn::ARCTAN, Haydn::SIN_COS,
      Haydn::ADDI32, Haydn::NOT32, Haydn::POPCOUNT32};
  for (unsigned Opc : Opcodes) {
    SlotBits L = Fmts.getLegalSlots(Opc);
    EXPECT_EQ(L & ~SlotBits(Haydn::SLOT_SET_E3), 0u) << "opc=" << Opc;
  }
}

TEST(HaydnMCFormatsTest, GetPacketFormatBySizeSixteenBytes) {
  // Product table Size is EncodedBytes=16 for Bundle128.
  HaydnMCFormats Fmts;
  const PacketFormats &P = Fmts.getPacketFormats();
  const VLIWFormat *BySize =
      P.getFormatBySize(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32, 16);
  ASSERT_NE(BySize, nullptr);
  EXPECT_EQ(BySize->getSize(), 16u);
  EXPECT_STREQ(BySize->Name, "BUNDLE_E3");
}

TEST(HaydnMCFormatsTest, SparseAltsDistinctPerSlotWhenLegal) {
  // AlternateInsts members are distinct per legal slot (alts-derived
  // member identity per field).
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD32);
  ASSERT_NE(Alts, nullptr);
  ASSERT_EQ(Alts->size(), 3u);
  unsigned V0 = (*Alts)[0];
  unsigned V1 = (*Alts)[1];
  unsigned V2 = (*Alts)[2];
  ASSERT_NE(V0, 0u);
  ASSERT_NE(V1, 0u);
  ASSERT_NE(V2, 0u);
  EXPECT_NE(V0, V1);
  EXPECT_NE(V1, V2);
  EXPECT_NE(V0, V2);
  EXPECT_EQ(Fmts.getSlotKind(V0), MCSlotKind(MCSlotKind::Haydn_SLOT_P30));
  EXPECT_EQ(Fmts.getSlotKind(V1), MCSlotKind(MCSlotKind::Haydn_SLOT_P31));
  EXPECT_EQ(Fmts.getSlotKind(V2), MCSlotKind(MCSlotKind::Haydn_SLOT_P32));
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
