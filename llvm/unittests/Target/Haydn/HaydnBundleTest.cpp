//===- HaydnBundleTest.cpp - VLIW Bundle format-coverage tests --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for `Haydn::Bundle` (D341 Phase 1 dep-infra) — the AIE-port bundle
// resource model that the post-RA hazard recognizer + SMS will reason about.
// Pins the multi-slot first-fit pick (Haydn's extension over AIE's single-slot
// model) + format-coverage check, against real opcodes via HaydnMCFormats.
//
#include "HaydnBundle.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/MC/MCInst.h"
#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::Haydn;

namespace {

TEST(HaydnBundleTest, EmptyAcceptsAny) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  EXPECT_TRUE(B.empty());
  EXPECT_TRUE(B.canAdd(Haydn::ADD32));
  EXPECT_TRUE(B.canAdd(Haydn::ADD64));
}

TEST(HaydnBundleTest, DisjointSlotsFit) {
  // ST32 is S0-only; ADD64 is S1|S2. Disjoint → both fit.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St32, Add64;
  St32.setOpcode(Haydn::ST32);
  Add64.setOpcode(Haydn::ADD64);
  ASSERT_TRUE(B.canAdd(St32.getOpcode()));
  B.add(&St32);
  ASSERT_TRUE(B.canAdd(Add64.getOpcode()));
  B.add(&Add64);
  EXPECT_EQ(B.size(), 2u);
  EXPECT_EQ(B.getOccupiedSlots() & Haydn::SLOT0, SlotBits(Haydn::SLOT0));
  EXPECT_NE(B.getOccupiedSlots() & (Haydn::SLOT1 | Haydn::SLOT2), 0u);
  EXPECT_TRUE(B.hasValidFormat());
  EXPECT_NE(B.at(MCSlotKind::Haydn_SLOT_S0), nullptr);
}

TEST(HaydnBundleTest, SameSlotConflicts) {
  // ADD32 is multi-slot (FlexMap S0|S1|S2). Three copies fill every slot;
  // a fourth must be rejected (format/slot saturation).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A[4];
  for (int I = 0; I < 3; ++I) {
    A[I].setOpcode(Haydn::ADD32);
    ASSERT_TRUE(B.canAdd(A[I].getOpcode())) << "ADD32 #" << I;
    B.add(&A[I]);
  }
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT0) | Haydn::SLOT1 | Haydn::SLOT2);
  A[3].setOpcode(Haydn::ADD32);
  EXPECT_FALSE(B.canAdd(A[3].getOpcode()))
      << "fourth ADD32 must conflict once S0|S1|S2 are full";
}

TEST(HaydnBundleTest, MultiSlotOpPicksFirstFree) {
  // NOT32 is legal in S0|S1|S2. pickSlot prefers higher slots first (S2→S1→S0)
  // so flexible ALU leaves S0 free for loads (HaydnBundle.h).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst Not;
  Not.setOpcode(Haydn::NOT32);
  ASSERT_TRUE(B.canAdd(Not.getOpcode()));
  B.add(&Not);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT2))
      << "multi-slot NOT32 alone should prefer S2";
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_S2)->getOpcode(), Haydn::NOT32);

  // Second multi-slot op avoids occupied S2 → S1.
  Bundle<MCInst> B2(&Fmts);
  MCInst Add32, Not2;
  Add32.setOpcode(Haydn::ADD32);
  Not2.setOpcode(Haydn::NOT32);
  B2.add(&Add32); // takes S2
  ASSERT_TRUE(B2.canAdd(Not2.getOpcode()));
  B2.add(&Not2);
  EXPECT_EQ(B2.getOccupiedSlots(), SlotBits(Haydn::SLOT2) | Haydn::SLOT1)
      << "second multi-slot op should pick S1 (S2 occupied)";
}

TEST(HaydnBundleTest, ClearResets) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A;
  A.setOpcode(Haydn::ADD32);
  B.add(&A);
  EXPECT_FALSE(B.empty());
  B.clear();
  EXPECT_TRUE(B.empty());
  EXPECT_EQ(B.getOccupiedSlots(), 0u);
}

// D428: reserveByOpcode updates OccupiedSlots without pushing Instrs (SMS
// ResourceCycle path). empty() stays true after reserve, but canAdd must
// consult pickSlot (not the bare empty() standalone escape). ADD32 is legal
// on S0|S1|S2 — three reserves fill the cycle; a fourth must return false so
// ResMII can grow above 1. Truly empty (OccupiedSlots==0) still accepts.
TEST(HaydnBundleTest, ReserveByOpcodeRejectsSaturatedSlot) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  EXPECT_TRUE(B.empty());
  EXPECT_EQ(B.getOccupiedSlots(), 0u);
  ASSERT_TRUE(B.canAdd(Haydn::ADD32));
  B.reserveByOpcode(Haydn::ADD32);
  EXPECT_TRUE(B.empty()) << "reserveByOpcode must not push Instrs";
  EXPECT_NE(B.getOccupiedSlots(), 0u)
      << "reserveByOpcode must mark OccupiedSlots";
  // Still empty of Instrs, but OccupiedSlots != 0: canAdd must fall through
  // to pickSlot and still accept while free slots remain.
  ASSERT_TRUE(B.canAdd(Haydn::ADD32))
      << "D428: Instrs-empty with free slots must still accept via pickSlot";
  B.reserveByOpcode(Haydn::ADD32);
  ASSERT_TRUE(B.canAdd(Haydn::ADD32));
  B.reserveByOpcode(Haydn::ADD32);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT0) | Haydn::SLOT1 | Haydn::SLOT2);
  EXPECT_TRUE(B.empty()) << "still no Instrs after three reserves";
  // Bundle fully reserved — fourth ADD32 must NOT escape via empty().
  EXPECT_FALSE(B.canAdd(Haydn::ADD32))
      << "D428: after reserve saturates slots, canAdd must return false";
  // ADD64 is S1|S2 only — also saturated now.
  EXPECT_FALSE(B.canAdd(Haydn::ADD64));
}

} // namespace
