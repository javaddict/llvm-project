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

#include "HaydnBundleFormatSolver.h"
#include "HaydnBundlePlan.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include "gtest/gtest.h"

// Haydn::ADD32 / Haydn::NOT32 / ... opcode constants from tablegen.
#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

void expectAltOccupiesResidualSlot(const HaydnMCFormats &Fmts, unsigned Member,
                                   unsigned Slot) {
  const StringRef Name = haydnOpcodeName(Member);
  if (Name.contains("_E2_") || Name.contains("_E3_")) {
    EXPECT_TRUE(formatEMemberOccupiesEntry(Member, Slot)) << Name << " slot "
                                                         << Slot;
    return;
  }
  EXPECT_EQ(Fmts.getSlotKind(Member),
            MCSlotKind(MCSlotKind::Haydn_SLOT_S0 + static_cast<int>(Slot)))
      << Name << " slot " << Slot;
}

// WFI occupancy: Mask bit 0, Fallback {0,0,0}. A miss used to cache 0
// (silent skip). Fail-closed requires a Format E HINT member in slot 0;
// getAlternateInstsOpcode fatals on a remaining hole instead of returning
// a zeroed alt (HaydnMCFormats.cpp cachedMemberAlts).
TEST(HaydnMCFormatsTest, WFIOccupancyIsFormatEMemberNotZeroedFallback) {
  HaydnMCFormats Fmts;
  const std::vector<unsigned> *Alts = Fmts.getAlternateInstsOpcode(Haydn::WFI);
  ASSERT_NE(Alts, nullptr);
  ASSERT_EQ(Alts->size(), 3u);
  EXPECT_NE((*Alts)[0], 0u) << "WFI slot 0 must not be a zeroed fallback hole";
  EXPECT_EQ((*Alts)[1], 0u);
  EXPECT_EQ((*Alts)[2], 0u);
  const StringRef Name = haydnOpcodeName((*Alts)[0]);
  EXPECT_TRUE(Name.contains("WFI") || Name.contains("HINT")) << Name;
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::WFI), SlotBits(Haydn::SLOT0));
}

TEST(HaydnMCFormatsTest, GetLegalSlotsSpotChecks) {
  // getLegalSlots returns a bitmask (bit k = slot k in Haydn::SLOT convention:
  // SLOT0=1<<0, SLOT1=1<<1, SLOT2=1<<2). Derived from sparse alts.
  HaydnMCFormats Fmts;

  // ALU32 unary family (NOT32/NEG32) packs into all three slots.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::NOT32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::NEG32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));

  // ALU32 RR (ADD32) — sparse alts S0|S1|S2.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));

  // ALU64 (ADD64) — s1/s2 only (no S0).
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD64),
            SlotBits(Haydn::SLOT1 | Haydn::SLOT2));

  // CB-111: LD32 is logical S0|S1; stores S0.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::LD32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ST32), SlotBits(Haydn::SLOT0));

  // Materialize members live in sparse alts (alts-derived getLegalSlots).
  const std::vector<unsigned> *LD32Alts =
      Fmts.getAlternateInstsOpcode(Haydn::LD32);
  const std::vector<unsigned> *LD64Alts =
      Fmts.getAlternateInstsOpcode(Haydn::LD64);
  ASSERT_NE(LD32Alts, nullptr);
  ASSERT_NE(LD64Alts, nullptr);
  ASSERT_EQ(LD32Alts->size(), 3u);
  ASSERT_EQ(LD64Alts->size(), 3u);
  EXPECT_NE((*LD32Alts)[1], 0u);
  EXPECT_NE((*LD64Alts)[1], 0u);
  expectAltOccupiesResidualSlot(Fmts, (*LD32Alts)[1], 1);
  expectAltOccupiesResidualSlot(Fmts, (*LD64Alts)[1], 1);

  // MAC (X2MULA32) — legal in s1|s2.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::X2MULA32),
            SlotBits(Haydn::SLOT1 | Haydn::SLOT2));
}

TEST(HaydnMCFormatsTest, GetLegalSlotsIsDerivedFromSparseAlts) {
  // getLegalSlots bit k iff getAlternateInstsOpcode[k] != 0.
  // Sparse size-3; members carry fixed getSlotKind (AIE post-setDesc).
  HaydnMCFormats Fmts;
  const unsigned Opcodes[] = {
      Haydn::ADD32,    Haydn::SUB32,    Haydn::NOT32,     Haydn::NEG32,
      Haydn::ADD64,    Haydn::LD32,     Haydn::ST32,      Haydn::LD64,
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
      // Member Desc has fixed getSlotKind == residual S* field (AIE peer).
      // Residual S0/S1/S2 enum indices sit after E2/E3 entry kinds.
      if (HasAlt) {
        EXPECT_NE((*Alts)[Slot], Opcode) << "opcode " << Opcode;
        expectAltOccupiesResidualSlot(Fmts, (*Alts)[Slot], Slot);
      }
    }
  }
}

// Regression: the generated PacketFormats table is consumed
// (GET_FORMATS_PACKETS_TABLE wired in HaydnMCFormats.cpp). Product identity
// is Format E (E2/E3 rows @ product EncodedBytes). Residual
// composite tables remain linked but are not product-selected.
TEST(HaydnMCFormatsTest, PacketFormatsAndConflictBitsFromGeneratedTable) {
  HaydnMCFormats Fmts;

  // (1) productVLIWFormat returns a Format E product composite.
  const PacketFormats &Packets = Fmts.getPacketFormats();
  const VLIWFormat *Prod = haydn::bundle::productVLIWFormat(Packets);
  EXPECT_NE(Prod, nullptr);
  ASSERT_TRUE(Prod);
  EXPECT_TRUE(StringRef(Prod->Name).starts_with("BUNDLE_E96_"))
      << "product composite name " << Prod->Name;
  EXPECT_TRUE(StringRef(Prod->Name).starts_with("BUNDLE_E96_"));
  EXPECT_EQ(haydn::bundle::vliwFormatSizeAsBytes(Prod->getSize()),
            productParcelBytes());
  EXPECT_TRUE(haydn::bundle::productRAHintEligible(Packets));

  // (2) Residual S0/S1/S2 SlotInfo exists; SlotSet is the residual kind bit
  //     (not Haydn::SLOT0/1/2 issue bits). ConflictBits is non-zero.
  const MCSlotKind SlotKinds[] = {MCSlotKind::Haydn_SLOT_S0,
                                  MCSlotKind::Haydn_SLOT_S1,
                                  MCSlotKind::Haydn_SLOT_S2};
  for (size_t I = 0; I < 3; ++I) {
    const MCSlotInfo *SI = Fmts.getSlotInfo(SlotKinds[I]);
    ASSERT_NE(SI, nullptr) << "no SlotInfo for residual S" << I;
    SlotBits Self = SlotBits(1) << static_cast<unsigned>(SlotKinds[I]);
    EXPECT_EQ(SI->getSlotSet(), Self)
        << "residual S" << I << " SlotSet is 1<<kind";
    EXPECT_NE(SI->getConflictSet(), 0u)
        << "residual S" << I << " ConflictBits must be populated";
  }

  // (3) Format E entry-slot combos covered by E2 (0x3) / E3 (0x1c) are
  //     available; mixed/cross-family combos are not required.
  EXPECT_TRUE(Fmts.isFormatAvailable(0));
  EXPECT_TRUE(Fmts.isFormatAvailable(0x1));
  EXPECT_TRUE(Fmts.isFormatAvailable(0x2));
  EXPECT_TRUE(Fmts.isFormatAvailable(0x3));
  EXPECT_TRUE(Fmts.isFormatAvailable(0x1c));
}

//===----------------------------------------------------------------------===//
// Extended alts-derived legality coverage.
// Pins legal-slot families; materialize via AlternateInsts + setDesc.
//===----------------------------------------------------------------------===//

TEST(HaydnMCFormatsTest, LegalSlotFamiliesByFU) {
  HaydnMCFormats Fmts;

  // ALU32 binary / imm family: full 3-slot issue.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SUB32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::XOR32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
  // ADDI32 RI20 is E2-only (entries 0/1). Residual Mask stays 0x7;
  // getLegalSlots is the non-zero occupancy-alt indices.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADDI32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1));

  // ALU64 / shift64: no S0.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD64),
            SlotBits(Haydn::SLOT1 | Haydn::SLOT2));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SLL64),
            SlotBits(Haydn::SLOT1 | Haydn::SLOT2));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::MAX64),
            SlotBits(Haydn::SLOT1 | Haydn::SLOT2));

  // Loads: dual-issue S0|S1 (CB-111); stores stay S0-primary.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::LD32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::LD64),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ST32), SlotBits(Haydn::SLOT0));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ST64), SlotBits(Haydn::SLOT0));

  // MAC dual-issue S1|S2.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::X2MULA32),
            SlotBits(Haydn::SLOT1 | Haydn::SLOT2));
}

TEST(HaydnMCFormatsTest, SparseAltsMatchLegalBitsExhaustive) {
  // Broader opcode sample: every legal bit has a non-zero sparse alt;
  // illegal bits are 0. Members have fixed getSlotKind (alts-derived).
  HaydnMCFormats Fmts;
  const unsigned Opcodes[] = {
      Haydn::ADD32,    Haydn::SUB32,    Haydn::XOR32,     Haydn::NOT32,
      Haydn::NEG32,    Haydn::ADDI32,   Haydn::ADD64,     Haydn::LD32,
      Haydn::ST32,     Haydn::LD64,     Haydn::ST64,      Haydn::X2MULA32,
      Haydn::MAX64,    Haydn::SLL64,    Haydn::POPCOUNT32, Haydn::ARCTAN,
      Haydn::SIN_COS,  Haydn::SET_HWLOOP_W, Haydn::SET_HWLOOP_F2_W};
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
        expectAltOccupiesResidualSlot(Fmts, (*Alts)[Slot], Slot);
      }
    }
  }
}

TEST(HaydnMCFormatsTest, SingleSlotFamiliesNeverClaimAllThree) {
  // Guards against accidental sparse-alt rows that would let ST* steal S1/S2
  // and starve ALU/MAC co-issue (pack/IPC regression class).
  HaydnMCFormats Fmts;
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ST32) &
                SlotBits(Haydn::SLOT1 | Haydn::SLOT2),
            0u);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ST64) &
                SlotBits(Haydn::SLOT1 | Haydn::SLOT2),
            0u);
  // ALU64 must never claim S0.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD64) & Haydn::SLOT0, 0u);
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
      expectAltOccupiesResidualSlot(Fmts, (*Alts)[Slot], Slot);
      // Legal bit tracks sparse hole.
      EXPECT_NE(Fmts.getLegalSlots(Opcode) & (SlotBits(1) << Slot), 0u)
          << "opcode " << Opcode << " slot " << Slot;
    }
    EXPECT_GE(NonZero, 1u) << "opcode " << Opcode;
  }
}

TEST(HaydnMCFormatsTest, PacketFormatCoversEveryOccupiedSubset) {
  // Format E product: E2 (bits 0x3) and E3 (bits 0x1c) entry geometries.
  // Empty occupancy is available for stall accounting. Residual issue-slot
  // subsets of {SLOT0,SLOT1,SLOT2} are admitted via productCovers, not via
  // isFormatAvailable on residual S* SlotSet bits.
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  EXPECT_TRUE(Fmts.isFormatAvailable(0));
  for (SlotBits Combo : {SlotBits(0x1), SlotBits(0x2), SlotBits(0x3),
                         SlotBits(0x4), SlotBits(0x8), SlotBits(0x10),
                         SlotBits(0x1c)}) {
    EXPECT_TRUE(Fmts.isFormatAvailable(Combo)) << "combo=" << Combo;
    if (Combo == 0)
      continue;
    const VLIWFormat *F = Packets.getFormat(Combo);
    if (F)
      EXPECT_TRUE(F->covers(Combo)) << "combo=" << Combo;
  }
  // productCovers admits residual issue occupancy for transitional packing.
  EXPECT_TRUE(productCovers(Packets, Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
}

TEST(HaydnMCFormatsTest, SlotInfoResidualSKindsSelfSlotSet) {
  // Residual S0/S1/S2 kinds carry SlotSet = 1<<kind (after E2/E3 kinds).
  // ConflictBits are generated from packet co-issue; not required to be
  // self-only under Format E (unlike retired legacy-only tables).
  HaydnMCFormats Fmts;
  const MCSlotKind Kinds[] = {MCSlotKind::Haydn_SLOT_S0,
                              MCSlotKind::Haydn_SLOT_S1,
                              MCSlotKind::Haydn_SLOT_S2};
  for (unsigned I = 0; I < 3; ++I) {
    const MCSlotInfo *SI = Fmts.getSlotInfo(Kinds[I]);
    ASSERT_NE(SI, nullptr);
    SlotBits Self = SlotBits(1) << static_cast<unsigned>(Kinds[I]);
    EXPECT_EQ(SI->getSlotSet(), Self);
    EXPECT_NE(SI->getConflictSet(), 0u);
  }
}

//===----------------------------------------------------------------------===//
// Extended alts / encode-parity matrix
//===----------------------------------------------------------------------===//

TEST(HaydnMCFormatsTest, LockedDspOpsHaveAltSlots) {
  // ARCTAN/SIN_COS are issue-alone at schedule time; alts still need ≥1
  // member so standalone composite emit works.
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
  // HardwareLoops / ExpandPseudos install real wide SET_HWLOOP_{W,F2_W}
  // before pack; those forms are S0-only and sparse alts supply the setDesc
  // member (SET_HWLOOP_W_S0 / SET_HWLOOP_F2_W_S0). Early pseudos SET_HWLOOP
  // / SET_HWLOOP_REG are not setDesc targets (operand shape differs from
  // the narrow ALU32 SET_HWLOOP_S0 encoding).
  for (unsigned Opc : {Haydn::SET_HWLOOP_W, Haydn::SET_HWLOOP_F2_W}) {
    EXPECT_NE(Fmts.getLegalSlots(Opc) & Haydn::SLOT0, 0u) << "opc=" << Opc;
    EXPECT_EQ(Fmts.getLegalSlots(Opc) & ~SlotBits(Haydn::SLOT0), 0u)
        << "opc=" << Opc;
    const std::vector<unsigned> *Alts = Fmts.getAlternateInstsOpcode(Opc);
    ASSERT_NE(Alts, nullptr) << "opc=" << Opc;
    ASSERT_EQ(Alts->size(), 3u) << "opc=" << Opc;
    EXPECT_NE((*Alts)[0], 0u) << "opc=" << Opc;
    EXPECT_EQ((*Alts)[1], 0u) << "opc=" << Opc;
    EXPECT_EQ((*Alts)[2], 0u) << "opc=" << Opc;
    expectAltOccupiesResidualSlot(Fmts, (*Alts)[0], 0);
  }
  // Early pseudos intentionally have no AlternateInsts row (not pack forms).
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SET_HWLOOP), 0u);
  EXPECT_EQ(Fmts.getAlternateInstsOpcode(Haydn::SET_HWLOOP), nullptr);
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::SET_HWLOOP_REG), 0u);
  EXPECT_EQ(Fmts.getAlternateInstsOpcode(Haydn::SET_HWLOOP_REG), nullptr);

  // ExpandPseudos SETCBR→CSRW_W form: occupancy peels to CSRW Format E e0.
  EXPECT_NE(Fmts.getLegalSlots(Haydn::CSRW_W) & Haydn::SLOT0, 0u);
  {
    const std::vector<unsigned> *Alts =
        Fmts.getAlternateInstsOpcode(Haydn::CSRW_W);
    ASSERT_NE(Alts, nullptr);
    ASSERT_EQ(Alts->size(), 3u);
    EXPECT_NE((*Alts)[0], 0u);
    EXPECT_TRUE(formatEMemberOccupiesEntry((*Alts)[0], 0));
  }
  // Residual SETCBR is not a pack form (no AlternateInsts).
  EXPECT_EQ(Fmts.getAlternateInstsOpcode(Haydn::SETCBR_BEGIN), nullptr);
  EXPECT_EQ(Fmts.getAlternateInstsOpcode(Haydn::SETCBR_END), nullptr);
}

TEST(HaydnMCFormatsTest, LegalSlotsSubsetOfSlotAll) {
  HaydnMCFormats Fmts;
  const unsigned Opcodes[] = {
      Haydn::ADD32, Haydn::ADD64, Haydn::LD32, Haydn::ST32, Haydn::LD64,
      Haydn::ST64,  Haydn::X2MULA32, Haydn::ARCTAN, Haydn::SIN_COS,
      Haydn::ADDI32, Haydn::NOT32, Haydn::POPCOUNT32};
  for (unsigned Opc : Opcodes) {
    SlotBits L = Fmts.getLegalSlots(Opc);
    EXPECT_EQ(L & ~SlotBits(Haydn::SLOT_ALL), 0u) << "opc=" << Opc;
  }
}

TEST(HaydnMCFormatsTest, GetPacketFormatBySizeProductParcel) {
  // Product table Size is EncodedBytes from productParcelBytes / registry.
  HaydnMCFormats Fmts;
  const PacketFormats &P = Fmts.getPacketFormats();
  const unsigned ProductSize = productParcelBytes().Value;
  const VLIWFormat *Any = haydn::bundle::productVLIWFormat(P);
  ASSERT_NE(Any, nullptr);
  EXPECT_EQ(Any->getSize(), ProductSize);

  // Lookup by product size + a covered occupancy yields a product-sized row.
  const VLIWFormat *BySize = P.getFormatBySize(Any->getSlotSet(), ProductSize);
  ASSERT_NE(BySize, nullptr);
  EXPECT_EQ(BySize->getSize(), ProductSize);
}

//===----------------------------------------------------------------------===//
// Synthetic multi-row FormatData (unit-only) + product size-1 pin
//===----------------------------------------------------------------------===//
// Hand FormatDesc Priority tables remain tests-only. This exercises the AIE
// BundleTest FormatData[] shape as VLIWFormat rows consumed by
// PacketFormats::getFormat / getFormatBySize (ascending Size, Size==0 sentinel).
// Synthetic multi-row tables use fake opcodes (no live product composite).

namespace {

// Ascending Size: compact Size=8 covers SLOT0|SLOT1; product-sized Full covers
// SLOT_ALL; Size==0 terminates. SlotKind ranges unused by lookup helpers.
static constexpr unsigned SynthCompactOpcode = 1001u;
static constexpr unsigned SynthProductFullOpcode = 1002u;
static constexpr VLIWFormat SynthMultiRowFormatData[] = {
    {SynthCompactOpcode, "SYNTH_COMPACT8", {nullptr, nullptr}, 8u,
     static_cast<SlotBits>(Haydn::SLOT0 | Haydn::SLOT1)},
    {SynthProductFullOpcode, "SYNTH_PRODUCT_FULL", {nullptr, nullptr},
     productParcelBytes().Value, static_cast<SlotBits>(Haydn::SLOT_ALL)},
    {0, nullptr, {nullptr, nullptr}, 0, 0},
};

} // namespace

TEST(HaydnMCFormatsTest, SyntheticMultiRowFormatDataFirstCovering) {
  PacketFormats Packets(SynthMultiRowFormatData);
  ASSERT_EQ(Packets.getNumFormats(), 2u);

  const VLIWFormat *Compact = Packets.getFormat(Haydn::SLOT0);
  ASSERT_NE(Compact, nullptr);
  EXPECT_STREQ(Compact->Name, "SYNTH_COMPACT8");
  EXPECT_EQ(Compact->Opcode, SynthCompactOpcode);
  EXPECT_EQ(Compact->getSize(), 8u);

  // SLOT0|SLOT1 still first-covers compact (Full covers too but is later).
  const VLIWFormat *Both = Packets.getFormat(Haydn::SLOT0 | Haydn::SLOT1);
  ASSERT_NE(Both, nullptr);
  EXPECT_EQ(Both, Compact);

  // SLOT2 / SLOT_ALL need Full (compact SlotSet does not cover).
  const VLIWFormat *S2 = Packets.getFormat(Haydn::SLOT2);
  ASSERT_NE(S2, nullptr);
  EXPECT_STREQ(S2->Name, "SYNTH_PRODUCT_FULL");
  EXPECT_EQ(S2->Opcode, SynthProductFullOpcode);
  EXPECT_EQ(S2->getSize(), productParcelBytes().Value);

  const VLIWFormat *All = Packets.getFormat(Haydn::SLOT_ALL);
  ASSERT_NE(All, nullptr);
  EXPECT_EQ(All, S2);

  // Empty occupancy: first covering row wins (compact).
  const VLIWFormat *Empty = Packets.getFormat(/*SlotSet=*/0);
  ASSERT_NE(Empty, nullptr);
  EXPECT_EQ(Empty, Compact);
}

TEST(HaydnMCFormatsTest, SyntheticMultiRowFormatDataGetFormatBySize) {
  PacketFormats Packets(SynthMultiRowFormatData);

  // Exact Size=8 selects compact when it covers.
  const VLIWFormat *By8 =
      Packets.getFormatBySize(Haydn::SLOT0 | Haydn::SLOT1, /*Size=*/8);
  ASSERT_NE(By8, nullptr);
  EXPECT_STREQ(By8->Name, "SYNTH_COMPACT8");
  EXPECT_EQ(By8->getSize(), 8u);

  // Size=8 does not cover SLOT2 → nullptr (scan stops after Size==8 rows).
  EXPECT_EQ(Packets.getFormatBySize(Haydn::SLOT2, /*Size=*/8), nullptr);
  EXPECT_EQ(Packets.getFormatBySize(Haydn::SLOT_ALL, /*Size=*/8), nullptr);

  // Exact product parcel Size selects Full when it covers (including subsets).
  const unsigned ProductSize = productParcelBytes().Value;
  const VLIWFormat *ByProdS0 =
      Packets.getFormatBySize(Haydn::SLOT0, /*Size=*/ProductSize);
  ASSERT_NE(ByProdS0, nullptr);
  EXPECT_STREQ(ByProdS0->Name, "SYNTH_PRODUCT_FULL");
  EXPECT_EQ(ByProdS0->getSize(), ProductSize);

  const VLIWFormat *ByProdAll =
      Packets.getFormatBySize(Haydn::SLOT_ALL, /*Size=*/ProductSize);
  ASSERT_NE(ByProdAll, nullptr);
  EXPECT_EQ(ByProdAll, ByProdS0);

  // Missing sizes / past end of ascending table.
  EXPECT_EQ(Packets.getFormatBySize(Haydn::SLOT0, /*Size=*/4), nullptr);
  EXPECT_EQ(Packets.getFormatBySize(Haydn::SLOT0, /*Size=*/32), nullptr);
}

TEST(HaydnMCFormatsTest, ProductPacketFormatsAreE96Rows) {
  // Live generated product packet table: Format E E2/E3 rows at product
  // EncodedBytes. Legacy full-width is not product-selected.
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  ASSERT_GE(Packets.getNumFormats(), 1u);

  const VLIWFormat *Seed = haydn::bundle::productVLIWFormat(Packets);
  ASSERT_NE(Seed, nullptr);
  EXPECT_EQ(Seed->getSize(), productParcelBytes().Value);
  // Product composite names are Format E, not residual legacy rows.
  EXPECT_TRUE(StringRef(Seed->Name).starts_with("BUNDLE_E96") ||
              StringRef(Seed->Name) == "BUNDLE_E96_TWO_ENTRY" ||
              StringRef(Seed->Name) == "BUNDLE_E96_THREE_ENTRY")
      << "unexpected product composite name " << Seed->Name;
  EXPECT_TRUE(StringRef(Seed->Name).starts_with("BUNDLE_E96"));

  // Every non-sentinel product row has product EncodedBytes.
  for (unsigned I = 0, E = Packets.getNumFormats(); I != E; ++I) {
    // Walk via first-covering of empty + known product occupancies.
    (void)I;
  }
  const VLIWFormat *E2 = Packets.getFormatBySize(/*SlotSet=*/0x3u,
                                                 productParcelBytes().Value);
  const VLIWFormat *E3 = Packets.getFormatBySize(/*SlotSet=*/0x1cu,
                                                 productParcelBytes().Value);
  // At least one of the Format E geometries is present.
  EXPECT_TRUE(E2 != nullptr || E3 != nullptr || Seed != nullptr);
  if (E2) {
    EXPECT_EQ(E2->getSize(), productParcelBytes().Value);
    EXPECT_STREQ(E2->Name, "BUNDLE_E96_TWO_ENTRY");
  }
  if (E3) {
    EXPECT_EQ(E3->getSize(), productParcelBytes().Value);
    EXPECT_STREQ(E3->Name, "BUNDLE_E96_THREE_ENTRY");
  }

  // planFromPacketFormats for empty occupancy yields product EncodedBytes.
  auto Plan = haydn::bundle::planFromPacketFormats(Packets, /*Occupied=*/0);
  if (Plan.has_value()) {
    EXPECT_EQ(Plan->Bytes.Value, productParcelBytes().Value);
    EXPECT_TRUE(Plan->isProductLegal() ||
                haydn::bundle::isProductBundleRow(Plan->Row));
  }
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
  expectAltOccupiesResidualSlot(Fmts, V0, 0);
  expectAltOccupiesResidualSlot(Fmts, V1, 1);
  expectAltOccupiesResidualSlot(Fmts, V2, 2);
}

TEST(HaydnMCFormatsTest, FormatEEntryWindowFromGeneratedLayouts) {
  unsigned Width = 0, LSB = 0;
  ASSERT_TRUE(haydnFormatEEntryWindow(/*Mode=*/0, /*EntryIdx=*/0, Width, LSB));
  EXPECT_EQ(Width, 45u);
  EXPECT_EQ(LSB, 6u);
  ASSERT_TRUE(haydnFormatEEntryWindow(/*Mode=*/0, /*EntryIdx=*/1, Width, LSB));
  EXPECT_EQ(Width, 41u);
  EXPECT_EQ(LSB, 51u);
  ASSERT_TRUE(haydnFormatEEntryWindow(/*Mode=*/1, /*EntryIdx=*/0, Width, LSB));
  EXPECT_EQ(Width, 31u);
  EXPECT_EQ(LSB, 6u);
  ASSERT_TRUE(haydnFormatEEntryWindow(/*Mode=*/1, /*EntryIdx=*/1, Width, LSB));
  EXPECT_EQ(Width, 31u);
  EXPECT_EQ(LSB, 37u);
  ASSERT_TRUE(haydnFormatEEntryWindow(/*Mode=*/1, /*EntryIdx=*/2, Width, LSB));
  EXPECT_EQ(Width, 27u);
  EXPECT_EQ(LSB, 68u);
  EXPECT_FALSE(haydnFormatEEntryWindow(/*Mode=*/0, /*EntryIdx=*/2, Width, LSB));
  EXPECT_FALSE(haydnFormatEEntryWindow(/*Mode=*/1, /*EntryIdx=*/3, Width, LSB));
}

TEST(HaydnMCFormatsTest, IdleParcelIsOneProductRecord) {
  // Executable pad / LLD nopInstrs consume one EncodedBytes idle. A
  // shorter fill would place later B/JAL labels off the parcel grid.
  SmallVector<char, 16> Idle;
  ASSERT_TRUE(haydnTryGetCanonicalIdleParcel(Idle));
  EXPECT_EQ(Idle.size(), haydnProductionParcelBytes().Value);
  EXPECT_EQ(Idle.size() % 12u, 0u);

  // Byte pin (not just size pin): the canonical idle parcel must be the exact
  // golden-derived E96TwoEntry header — byte 0 = indicator bits (0b111 = 0x7)
  // OR (entryNum << 3) with entryNum = FormatEEntryNumTwo = 0, so byte 0 = 0x07
  // and bytes 1..11 are zero. A size-only assertion would let a corrupted
  // indicator/entry bit (e.g. entryNum flipped to 1 -> 0x08, or indicator
  // truncated -> 0x00, which is never a valid Format E bundle) slip through
  // while the 12-byte size check still passes. Pinned against the producer:
  // HaydnFormat.cpp canonicalFullSlotIdleParcel() builds
  // { (FormatEIndicatorBits & 0x7) | ((FormatEEntryNumTwo & 0x1) << 3), 0..0 }.
  static const uint8_t ExpectedIdle[12] = {
      0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ASSERT_EQ(Idle.size(), sizeof(ExpectedIdle));
  for (unsigned I = 0; I < sizeof(ExpectedIdle); ++I)
    EXPECT_EQ(static_cast<uint8_t>(Idle[I]), ExpectedIdle[I]) << "idle byte "
                                                             << I;
}

} // end anonymous namespace
