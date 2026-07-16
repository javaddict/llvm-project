//===- HaydnMCFormatsTest.cpp - FlexMap slot-authority tests -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the D464 single FlexMap slot authority
// (`HaydnMCFormats::getLegalSlots`): slot k is legal iff a `_S<k>_FLEX` variant
// exists. This replaced the retired DClass `getLegalSlots`/`getAltSlotSet`/
// M0Rows/FU-acceptance tables (D472). The consistency test anchors that
// `getLegalSlots` is DERIVED from `getFlexVariant` (no separate hand table).
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"

#include "gtest/gtest.h"

// Haydn::ADD32 / Haydn::NOT32 / ... opcode constants from tablegen.
#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;

namespace {

TEST(HaydnMCFormatsTest, GetLegalSlotsSpotChecks) {
  // getLegalSlots returns a bitmask (bit k = slot k in Haydn::SLOT convention:
  // SLOT0=1<<0, SLOT1=1<<1, SLOT2=1<<2). An opcode with no `_S<k>_FLEX`
  // variant in any slot returns 0 (standalone WIDE / pseudo).
  HaydnMCFormats Fmts;

  // ALU32 unary family (NOT32/NEG32) packs into all three slots — the unary
  // form needs only one source operand, which every slot's sub-row provides.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::NOT32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::NEG32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));

  // ALU32 RR (ADD32) — FlexMap has S0|S1|S2 (full ALU32 issue set).
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));

  // ALU64 (ADD64) — s1/s2 only post-D474 (Slot0 has no 64-bit ALU unit; the
  // ISA-illegal `_S0_FLEX` variants were deleted).
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ADD64),
            SlotBits(Haydn::SLOT1 | Haydn::SLOT2));

  // LS (LD32/ST32) — s0 only.
  // CB-111: LD32 is logical S0|S1 (FlexMap {LD32_S0, LD32_S1}); dual-load
  // packing uses placement, not a separate MIR opcode family.
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::LD32),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1));
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::ST32), SlotBits(Haydn::SLOT0));

  // FlexMap: LD32 slot-1 variant is encode-only LD32_S1 (no intermediate
  // logical twin). getFlexVariant(LD32, 1) is the format authority.
  EXPECT_NE(Fmts.getFlexVariant(Haydn::LD32, 1), 0u);
  EXPECT_NE(Fmts.getFlexVariant(Haydn::LD64, 1), 0u);

  // MAC (X2MULA32) — legal in s1|s2 (the MAC unit is dual-issue).
  EXPECT_EQ(Fmts.getLegalSlots(Haydn::X2MULA32),
            SlotBits(Haydn::SLOT1 | Haydn::SLOT2));
}

TEST(HaydnMCFormatsTest, GetLegalSlotsIsConsistentWithFlexVariant) {
  // The FlexMap authority: slot k is legal iff getFlexVariant(Opc, k) != 0.
  // This self-consistency check anchors that getLegalSlots is DERIVED from
  // getFlexVariant (no separate hand table — the DClass authority is gone).
  HaydnMCFormats Fmts;
  const unsigned Opcodes[] = {
      Haydn::ADD32,    Haydn::SUB32,    Haydn::NOT32,     Haydn::NEG32,
      Haydn::ADD64,    Haydn::LD32,     Haydn::ST32,      Haydn::LD64,
      Haydn::X2MULA32, Haydn::MAX64,    Haydn::SLL64,     Haydn::POPCOUNT32};

  for (unsigned Opcode : Opcodes) {
    SlotBits Legal = Fmts.getLegalSlots(Opcode);
    for (unsigned Slot = 0; Slot < 3; ++Slot) {
      unsigned FlexOpc = Fmts.getFlexVariant(Opcode, Slot);
      SlotBits Bit = SlotBits(1) << Slot;
      bool LegalHere = (Legal & Bit) != 0;
      bool HasVariant = (FlexOpc != 0);
      EXPECT_EQ(LegalHere, HasVariant)
          << "opcode " << Opcode << " slot " << Slot
          << ": getLegalSlots says " << LegalHere << " but getFlexVariant says "
          << HasVariant;
    }
  }
}

// REGRESSION TEST (D482): the generated PacketFormats table + real ConflictBits
// are now consumed (GET_FORMATS_PACKETS_TABLE wired in HaydnMCFormats.cpp).
// Previously ConflictBits was documented as SLOT_ALL (sentinel for "no
// exclusivity info"); with BUNDLE128_FULL declared as a composite packet format
// covering {S0,S1,S2}, the backend's computeSlotSets derives ConflictBits =
// self-only for every slot (1/2/4). This test anchors:
//   1. getPacketFormats() returns the generated table (non-empty, has a format
//      covering the full slot set).
//   2. ConflictBits != SLOT_ALL for every slot (the sentinel is gone).
//   3. isFormatAvailable is true for all subsets of {S0,S1,S2} (the Bundle128
//      packet format covers them; behavior preserved vs the hand-authored LUT).
TEST(HaydnMCFormatsTest, PacketFormatsAndConflictBitsFromGeneratedTable) {
  HaydnMCFormats Fmts;

  // (1) getPacketFormats returns a table that covers the full slot set.
  const PacketFormats &Packets = Fmts.getPacketFormats();
  const VLIWFormat *Full =
      Packets.getFormat(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2);
  EXPECT_NE(Full, nullptr);
  ASSERT_TRUE(Full);
  EXPECT_TRUE(Full->covers(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));

  // (2) ConflictBits != SLOT_ALL for every slot. Bundle128 co-emits all three,
  //     so each slot's conflict-closure is self-only (1/2/4).
  const MCSlotKind SlotKinds[] = {MCSlotKind::Haydn_SLOT_S0,
                                  MCSlotKind::Haydn_SLOT_S1,
                                  MCSlotKind::Haydn_SLOT_S2};
  const SlotBits SelfBits[] = {Haydn::SLOT0, Haydn::SLOT1, Haydn::SLOT2};
  for (size_t I = 0; I < 3; ++I) {
    const MCSlotInfo *SI = Fmts.getSlotInfo(SlotKinds[I]);
    ASSERT_NE(SI, nullptr) << "no SlotInfo for slot " << I;
    EXPECT_NE(SI->getConflictSet(), SlotBits(Haydn::SLOT_ALL))
        << "slot " << I << " still has SLOT_ALL ConflictBits sentinel";
    // Self-only: ConflictBits == this slot's own bit.
    EXPECT_EQ(SI->getConflictSet(), SelfBits[I])
        << "slot " << I << " ConflictBits should be self-only under Bundle128";
  }

  // (3) isFormatAvailable for all 8 slot-combos. With Bundle128 covering all 3
  //     slots, every subset is packetable (NOPs fill unused slots).
  for (SlotBits Combo = 0; Combo <= Haydn::SLOT_ALL; ++Combo) {
    EXPECT_TRUE(Fmts.isFormatAvailable(Combo))
        << "combo " << Combo << " should be available under Bundle128";
  }
}

} // end anonymous namespace
