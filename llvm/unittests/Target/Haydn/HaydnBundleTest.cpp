//===- HaydnBundleTest.cpp - VLIW Bundle format-coverage tests --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for `Haydn::Bundle` — AIE-port bundle resource model (SMS + HR).
// Bundle is a CycleState tryAdd adapter for alts-bearing logicals
// (AIEBundle.h:62-105 canAdd / AIEHazardRecognizer.cpp:174-214 alt try).
// Pins multi-slot S2→S1→S0 pick + format coverage against real opcodes.
//
#include "HaydnBundle.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnPlacementAlternative.h"
#include "HaydnPreRASchedStrategy.h"
#include "HaydnResourceCycle.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::Haydn;
using namespace llvm::haydn::bundle;

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
  St32.setOpcode(Haydn::S_SW_WITH_IMM);
  Add64.setOpcode(Haydn::ADD64);
  ASSERT_TRUE(B.canAdd(St32.getOpcode()));
  B.add(&St32);
  ASSERT_TRUE(B.canAdd(Add64.getOpcode()));
  B.add(&Add64);
  EXPECT_EQ(B.size(), 2u);
  EXPECT_EQ(B.getOccupiedSlots() & Haydn::SLOT_P30, SlotBits(Haydn::SLOT_P30));
  EXPECT_NE(B.getOccupiedSlots() & (Haydn::SLOT_P31 | Haydn::SLOT_P32), 0u);
  EXPECT_TRUE(B.hasValidFormat());
  EXPECT_NE(B.at(MCSlotKind::Haydn_SLOT_P30), nullptr);
}

TEST(HaydnBundleTest, SameSlotConflicts) {
  // ADD32 is multi-slot (alts-derived getLegalSlots / PlacementAlternative
  // FieldSlots S0|S1|S2). Three copies fill every slot; a fourth must be
  // rejected (format/slot saturation).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A[4];
  for (int I = 0; I < 3; ++I) {
    A[I].setOpcode(Haydn::ADD32);
    ASSERT_TRUE(B.canAdd(A[I].getOpcode())) << "ADD32 #" << I;
    B.add(&A[I]);
  }
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT_P30) | Haydn::SLOT_P31 | Haydn::SLOT_P32);
  A[3].setOpcode(Haydn::ADD32);
  EXPECT_FALSE(B.canAdd(A[3].getOpcode()))
      << "fourth ADD32 must conflict once S0|S1|S2 are full";
}

// Encode-time pack: four multi-slot ALU ops cannot all fit (issue/slot
// saturation). Three can fill Bundle128.
TEST(HaydnBundleTest, ExhaustiveThreeSlotFillRejectsFourth) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst Ops[4];
  for (int I = 0; I < 3; ++I) {
    Ops[I].setOpcode(Haydn::XOR32);
    ASSERT_TRUE(B.canAdd(Ops[I].getOpcode())) << "XOR32 #" << I;
    B.add(&Ops[I]);
  }
  EXPECT_TRUE(B.hasValidFormat());
  Ops[3].setOpcode(Haydn::XOR32);
  EXPECT_FALSE(B.canAdd(Ops[3].getOpcode()));
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
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_P32))
      << "multi-slot NOT32 alone should prefer S2";
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_P32)->getOpcode(), Haydn::NOT32);

  // Second multi-slot op avoids occupied S2 → S1.
  Bundle<MCInst> B2(&Fmts);
  MCInst Add32, Not2;
  Add32.setOpcode(Haydn::ADD32);
  Not2.setOpcode(Haydn::NOT32);
  B2.add(&Add32); // takes S2
  ASSERT_TRUE(B2.canAdd(Not2.getOpcode()));
  B2.add(&Not2);
  EXPECT_EQ(B2.getOccupiedSlots(), SlotBits(Haydn::SLOT_P32) | Haydn::SLOT_P31)
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

// reserveByOpcode updates OccupiedSlots without pushing Instrs (SMS
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
      << "Instrs-empty with free slots must still accept via pickSlot";
  B.reserveByOpcode(Haydn::ADD32);
  ASSERT_TRUE(B.canAdd(Haydn::ADD32));
  B.reserveByOpcode(Haydn::ADD32);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT_P30) | Haydn::SLOT_P31 | Haydn::SLOT_P32);
  EXPECT_TRUE(B.empty()) << "still no Instrs after three reserves";
  // Bundle fully reserved — fourth ADD32 must NOT escape via empty().
  EXPECT_FALSE(B.canAdd(Haydn::ADD32))
      << "after reserve saturates slots, canAdd must return false";
  // ADD64 is S1|S2 only — also saturated now.
  EXPECT_FALSE(B.canAdd(Haydn::ADD64));
}

//===----------------------------------------------------------------------===//
// Extended Bundle packing contracts — slot-aware + format-aware encode half.
// These pin encode-legality contracts (Haydn::Bundle + HaydnMCFormats).
//===----------------------------------------------------------------------===//

TEST(HaydnBundleTest, LoadAndMacDisjointPack) {
  // Classic DSP cycle: LD32 (S0|S1) + X2MULA32 (S1|S2) must co-issue.
  // Prefer-S2 MAC + prefer-S2-first ALU leave a free low slot for the load.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst Ld, Mac;
  Ld.setOpcode(Haydn::S_LW_WITH_IMM);
  Mac.setOpcode(Haydn::X2MULA32);
  ASSERT_TRUE(B.canAdd(Ld.getOpcode()));
  B.add(&Ld);
  ASSERT_TRUE(B.canAdd(Mac.getOpcode()))
      << "MAC must fit after LD32; occupied=" << B.getOccupiedSlots();
  B.add(&Mac);
  EXPECT_EQ(B.size(), 2u);
  EXPECT_TRUE(B.hasValidFormat());
  // Occupied must include a load slot (0 or 1) and a MAC slot (1 or 2).
  SlotBits Occ = B.getOccupiedSlots();
  EXPECT_NE(Occ & (Haydn::SLOT_P30 | Haydn::SLOT_P31), 0u);
  EXPECT_NE(Occ & (Haydn::SLOT_P31 | Haydn::SLOT_P32), 0u);
  EXPECT_EQ(Occ & Haydn::SLOT_P30 ? 1 : 0, (Occ & Haydn::SLOT_P30) ? 1 : 0);
}

TEST(HaydnBundleTest, StoreThenTwoAluFillsBundle) {
  // ST32 is S0-only; two multi-slot ALUs take S2 then S1 → full cycle.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St, A, X;
  St.setOpcode(Haydn::S_SW_WITH_IMM);
  A.setOpcode(Haydn::ADD32);
  X.setOpcode(Haydn::XOR32);
  B.add(&St);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_P30));
  ASSERT_TRUE(B.canAdd(A.getOpcode()));
  B.add(&A);
  ASSERT_TRUE(B.canAdd(X.getOpcode()));
  B.add(&X);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));
  EXPECT_TRUE(B.hasValidFormat());
  EXPECT_FALSE(B.canAdd(Haydn::ADD32));
}

TEST(HaydnBundleTest, TwoS0OnlyOpsConflict) {
  // Two ST32 cannot share a cycle (both S0-only).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St0, St1;
  St0.setOpcode(Haydn::S_SW_WITH_IMM);
  St1.setOpcode(Haydn::S_SW_WITH_IMM);
  B.add(&St0);
  EXPECT_FALSE(B.canAdd(St1.getOpcode()));
}

TEST(HaydnBundleTest, DualLoadCanShareCycle) {
  // LD32 is S0|S1 — two loads must pack (dual-load product).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst L0, L1;
  L0.setOpcode(Haydn::S_LW_WITH_IMM);
  L1.setOpcode(Haydn::S_LW_WITH_IMM);
  B.add(&L0);
  ASSERT_TRUE(B.canAdd(L1.getOpcode()))
      << "second LD32 must fit on the free load slot; occ="
      << B.getOccupiedSlots();
  B.add(&L1);
  EXPECT_EQ(B.size(), 2u);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31));
  EXPECT_TRUE(B.hasValidFormat());
}

TEST(HaydnBundleTest, HintSlotHonoredWhenFreeAndLegal) {
  // add(Instr, HintSlot): when S0 is free and legal for ADD32, place there
  // even though first-fit prefers S2. MC encoder / .sN path uses this.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A;
  A.setOpcode(Haydn::ADD32);
  ASSERT_TRUE(B.canAdd(A.getOpcode()));
  B.add(&A, MCSlotKind::Haydn_SLOT_P30);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_P30));
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_P30), &A);
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_P32), nullptr);
}

TEST(HaydnBundleTest, HintSlotFallsBackWhenOccupied) {
  // Hint S2 after S2 is taken → fall back to first free legal (S1).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, X;
  A.setOpcode(Haydn::ADD32);
  X.setOpcode(Haydn::XOR32);
  B.add(&A); // prefers S2
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_P32));
  ASSERT_TRUE(B.canAdd(X.getOpcode()));
  B.add(&X, MCSlotKind::Haydn_SLOT_P32); // conflict → fallback
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT_P32 | Haydn::SLOT_P31));
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_P31), &X);
}

TEST(HaydnBundleTest, HintIllegalSlotFallsBack) {
  // ST32 is not legal on S2; hint S2 must fall back to S0.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St;
  St.setOpcode(Haydn::S_SW_WITH_IMM);
  ASSERT_TRUE(B.canAdd(St.getOpcode()));
  B.add(&St, MCSlotKind::Haydn_SLOT_P32);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_P30));
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_P30), &St);
}

TEST(HaydnBundleTest, CanAddAgreesWithAddOnSaturation) {
  // Contract: canAdd false ⇒ must not add. After full fill, all common
  // multi-slot ops reject.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst Ops[3];
  Ops[0].setOpcode(Haydn::ADD32);
  Ops[1].setOpcode(Haydn::XOR32);
  Ops[2].setOpcode(Haydn::NOT32);
  for (int I = 0; I < 3; ++I) {
    ASSERT_TRUE(B.canAdd(Ops[I].getOpcode()));
    B.add(&Ops[I]);
  }
  EXPECT_FALSE(B.canAdd(Haydn::ADD32));
  EXPECT_FALSE(B.canAdd(Haydn::ADD64));
  EXPECT_FALSE(B.canAdd(Haydn::S_LW_WITH_IMM));
  EXPECT_FALSE(B.canAdd(Haydn::X2MULA32));
  EXPECT_FALSE(B.canAdd(Haydn::S_SW_WITH_IMM));
}

TEST(HaydnBundleTest, ReserveThenAddSharesOccupancy) {
  // SMS reserve + later real add must see the same OccupiedSlots budget.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  B.reserveByOpcode(Haydn::S_SW_WITH_IMM); // claims S0 without Instrs
  EXPECT_TRUE(B.empty());
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_P30));
  EXPECT_FALSE(B.canAdd(Haydn::S_SW_WITH_IMM)); // second store blocked
  MCInst A;
  A.setOpcode(Haydn::ADD32);
  ASSERT_TRUE(B.canAdd(A.getOpcode()));
  B.add(&A);
  EXPECT_EQ(B.size(), 1u);
  EXPECT_NE(B.getOccupiedSlots() & Haydn::SLOT_P30, 0u);
  EXPECT_NE(B.getOccupiedSlots() & (Haydn::SLOT_P31 | Haydn::SLOT_P32), 0u);
}

TEST(HaydnBundleTest, Alu64CannotPairWithTwoS0Only) {
  // ADD64 is S1|S2 only; one ST32 (S0) + ADD64 is legal; second ST32 is not.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St, Add64;
  St.setOpcode(Haydn::S_SW_WITH_IMM);
  Add64.setOpcode(Haydn::ADD64);
  B.add(&St);
  ASSERT_TRUE(B.canAdd(Add64.getOpcode()));
  B.add(&Add64);
  EXPECT_TRUE(B.hasValidFormat());
  EXPECT_FALSE(B.canAdd(Haydn::S_SW_WITH_IMM));
}

TEST(HaydnBundleTest, ClearAllowsRepack) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, B0;
  A.setOpcode(Haydn::ADD32);
  B0.setOpcode(Haydn::ADD32);
  B.add(&A);
  B.add(&B0);
  B.clear();
  EXPECT_TRUE(B.empty());
  EXPECT_EQ(B.getOccupiedSlots(), 0u);
  // After clear, three ADD32 fill again.
  MCInst Ops[3];
  for (int I = 0; I < 3; ++I) {
    Ops[I].setOpcode(Haydn::ADD32);
    ASSERT_TRUE(B.canAdd(Ops[I].getOpcode()));
    B.add(&Ops[I]);
  }
  EXPECT_FALSE(B.canAdd(Haydn::ADD32));
}

TEST(HaydnBundleTest, PreferHighSlotsLeavesS0ForLoad) {
  // Product rationale (HaydnBundle.h): multi-slot ALU prefers S2 so LD can
  // take S0. Pin that ADD32 alone lands on S2, then LD32 still fits.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst Alu, Ld;
  Alu.setOpcode(Haydn::ADD32);
  Ld.setOpcode(Haydn::S_LW_WITH_IMM);
  B.add(&Alu);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_P32));
  ASSERT_TRUE(B.canAdd(Ld.getOpcode()));
  B.add(&Ld);
  EXPECT_NE(B.getOccupiedSlots() & (Haydn::SLOT_P30 | Haydn::SLOT_P31), 0u);
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_P32), &Alu);
}

//===----------------------------------------------------------------------===//
// Extended pack matrix (encode half)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleTest, LoadMacAluClassicDspFill) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst Ld, Mac, Alu;
  Ld.setOpcode(Haydn::S_LW_WITH_IMM);
  Mac.setOpcode(Haydn::X2MULA32);
  Alu.setOpcode(Haydn::ADD32);
  ASSERT_TRUE(B.canAdd(Ld.getOpcode()));
  B.add(&Ld);
  ASSERT_TRUE(B.canAdd(Mac.getOpcode()));
  B.add(&Mac);
  ASSERT_TRUE(B.canAdd(Alu.getOpcode()));
  B.add(&Alu);
  EXPECT_EQ(B.size(), 3u);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));
  EXPECT_TRUE(B.hasValidFormat());
  EXPECT_FALSE(B.canAdd(Haydn::XOR32));
}

TEST(HaydnBundleTest, DualMacFillsS1S2) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst M0, M1;
  M0.setOpcode(Haydn::X2MULA32);
  M1.setOpcode(Haydn::X2MULA32);
  B.add(&M0);
  ASSERT_TRUE(B.canAdd(M1.getOpcode()));
  B.add(&M1);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT_P31 | Haydn::SLOT_P32));
  EXPECT_FALSE(B.canAdd(Haydn::X2MULA32));
  // S0 free for a store.
  MCInst St;
  St.setOpcode(Haydn::S_SW_WITH_IMM);
  ASSERT_TRUE(B.canAdd(St.getOpcode()));
  B.add(&St);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));
}

TEST(HaydnBundleTest, DualAlu64ThenRejectThird) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, S;
  A.setOpcode(Haydn::ADD64);
  S.setOpcode(Haydn::SLL64);
  B.add(&A);
  ASSERT_TRUE(B.canAdd(S.getOpcode()));
  B.add(&S);
  EXPECT_FALSE(B.canAdd(Haydn::MAX64));
  // S0 still free for ST.
  EXPECT_TRUE(B.canAdd(Haydn::S_SW_WITH_IMM));
}

TEST(HaydnBundleTest, DualLoadThenRejectThirdLoad) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst L0, L1, L2;
  L0.setOpcode(Haydn::S_LW_WITH_IMM);
  L1.setOpcode(Haydn::S_LW_WITH_IMM);
  L2.setOpcode(Haydn::S_LW_WITH_IMM);
  B.add(&L0);
  ASSERT_TRUE(B.canAdd(L1.getOpcode()));
  B.add(&L1);
  EXPECT_FALSE(B.canAdd(L2.getOpcode()));
  // S2 free for ALU/MAC.
  EXPECT_TRUE(B.canAdd(Haydn::ADD32));
  EXPECT_TRUE(B.canAdd(Haydn::X2MULA32));
}

TEST(HaydnBundleTest, HintS1OnSecondLoad) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst L0, L1;
  L0.setOpcode(Haydn::S_LW_WITH_IMM);
  L1.setOpcode(Haydn::S_LW_WITH_IMM);
  B.add(&L0, MCSlotKind::Haydn_SLOT_P30);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_P30));
  ASSERT_TRUE(B.canAdd(L1.getOpcode()));
  B.add(&L1, MCSlotKind::Haydn_SLOT_P31);
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_P31), &L1);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31));
}

TEST(HaydnBundleTest, ReserveByOpcodeSaturatesLikeAdd) {
  // SMS path: three ADD32 reserves == three adds for occupancy.
  HaydnMCFormats Fmts;
  Bundle<MCInst> ByAdd(&Fmts), ByRes(&Fmts);
  MCInst Ops[3];
  for (int I = 0; I < 3; ++I) {
    Ops[I].setOpcode(Haydn::ADD32);
    ByAdd.add(&Ops[I]);
    ByRes.reserveByOpcode(Haydn::ADD32);
  }
  EXPECT_EQ(ByAdd.getOccupiedSlots(), ByRes.getOccupiedSlots());
  EXPECT_EQ(ByRes.getOccupiedSlots(),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));
  EXPECT_FALSE(ByRes.canAdd(Haydn::ADD32));
  EXPECT_FALSE(ByAdd.canAdd(Haydn::ADD32));
}

TEST(HaydnBundleTest, AllSingleSlotCombosHaveValidFormat) {
  // Any subset occupancy after packing real ops must remain format-valid.
  HaydnMCFormats Fmts;
  const unsigned Seeds[] = {Haydn::S_SW_WITH_IMM, Haydn::ADD32, Haydn::ADD64,
                            Haydn::S_LW_WITH_IMM,  Haydn::X2MULA32};
  for (unsigned Opc : Seeds) {
    Bundle<MCInst> B(&Fmts);
    MCInst M;
    M.setOpcode(Opc);
    ASSERT_TRUE(B.canAdd(Opc));
    B.add(&M);
    if (B.getOccupiedSlots() != 0)
      EXPECT_TRUE(B.hasValidFormat()) << "opc=" << Opc;
  }
}

TEST(HaydnBundleTest, CanAddFalseNeverAcceptsOnFullBundle) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst Ops[3];
  for (int I = 0; I < 3; ++I) {
    Ops[I].setOpcode(Haydn::XOR32);
    B.add(&Ops[I]);
  }
  const unsigned Rejects[] = {Haydn::ADD32, Haydn::S_LW_WITH_IMM, Haydn::S_SW_WITH_IMM,
                              Haydn::ADD64, Haydn::X2MULA32, Haydn::ADDI32};
  for (unsigned Opc : Rejects)
    EXPECT_FALSE(B.canAdd(Opc)) << "opc=" << Opc;
}

//===----------------------------------------------------------------------===//
// Bundle is CycleState tryAdd adapter (AIE alt-try wire)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleTest, B24_AltsBearingUsesTryAddOrder) {
  // Alts-bearing ADD32 must prefer S2 via tryAdd (not getLegalSlots S0-first).
  HaydnMCFormats Fmts;
  ASSERT_TRUE(hasPlacementAlternatives(Fmts, Haydn::ADD32));
  Bundle<MCInst> B(&Fmts);
  MCInst A;
  A.setOpcode(Haydn::ADD32);
  B.add(&A);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_P32));
  EXPECT_EQ(B.lastPickedSlot(), MCSlotKind(MCSlotKind::Haydn_SLOT_P32));
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_P32), &A);
}

TEST(HaydnBundleTest, B24_BundleOccupancyMatchesSolver) {
  // Adapter contract: sequential Bundle.add occupancy == pure tryAddProduct.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  haydn::bundle::CycleState S = haydn::bundle::makeProductCycleState();
  MCInst Ops[3];
  const unsigned Seq[] = {Haydn::S_SW_WITH_IMM, Haydn::ADD64, Haydn::ADD32};
  for (unsigned I = 0; I < 3; ++I) {
    Ops[I].setOpcode(Seq[I]);
    ASSERT_TRUE(B.canAdd(Seq[I]));
    ASSERT_TRUE(haydn::bundle::canTryAddProduct(S, Fmts, Seq[I]));
    B.add(&Ops[I]);
    ASSERT_TRUE(haydn::bundle::tryAddProduct(S, Fmts, Seq[I]));
    EXPECT_EQ(B.getOccupiedSlots(), S.OccupiedSlots) << "idx " << I;
  }
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));
}

TEST(HaydnBundleTest, B24_EmptyStandaloneEscapeRetained) {
  // AIE AIEBundle.h:71-73: truly empty still accepts (SMS ResMII escape).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  EXPECT_TRUE(B.empty());
  EXPECT_EQ(B.getOccupiedSlots(), 0u);
  EXPECT_TRUE(B.canAdd(Haydn::ADD32));
  // Even after meta-only: OccupiedSlots still 0 → accept.
  B.reserveByOpcode(TargetOpcode::KILL); // no-op
  EXPECT_TRUE(B.canAdd(Haydn::S_SW_WITH_IMM));
}

//===----------------------------------------------------------------------===//
// Alts-only pickSlot — no getLegalSlots no-alt fallback
// (getLegalSlots is multi-slot alts-derived OR of sparse AlternateInsts.)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleTest, B25_NoAltOpcodeDoesNotUseFlexMapPick) {
  // Opcode 0 (PHI-ish) has no PlacementAlternatives and no sparse alt row
  // (getLegalSlots == 0). Bundle/HR alts-only pickSlot must not invent a
  // placement: empty escape still accepts; non-empty rejects.
  HaydnMCFormats Fmts;
  EXPECT_FALSE(hasPlacementAlternatives(Fmts, /*Opcode=*/0));
  EXPECT_EQ(Fmts.getLegalSlots(0), 0u);

  Bundle<MCInst> Empty(&Fmts);
  EXPECT_TRUE(Empty.canAdd(/*Opcode=*/0u)); // standalone escape only

  Bundle<MCInst> Occupied(&Fmts);
  MCInst A;
  A.setOpcode(Haydn::ADD32);
  Occupied.add(&A);
  // After occupancy, no-alt opcode cannot place (alts-only; no
  // getLegalSlots first-fit fallback).
  EXPECT_FALSE(Occupied.canAdd(/*Opcode=*/0u));
}

TEST(HaydnBundleTest, B25_AltsBearingUnchangedTryAdd) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, L;
  A.setOpcode(Haydn::ADD32);
  L.setOpcode(Haydn::S_LW_WITH_IMM);
  B.add(&A); // S2
  B.add(&L); // prefers high free load field (S1)
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_P32 | Haydn::SLOT_P31));
}

//===----------------------------------------------------------------------===//
// Bundle / ResourceCycle / PreRA FormatID frontier (product size-1 Full)
//===----------------------------------------------------------------------===//
//
// AIE peers: AIEBundle.h:150-156 getFormatOrNull;
// AIEHazardRecognizer.cpp:173-214 ResourceCycle canReserve/reserve via Bundle.
// Haydn exposes getFeasibleFormatMask (FormatID bitset) without freezing
// FormatID or setDesc (plan §7.1).

TEST(HaydnBundleTest, B41_GetFeasibleFormatMaskAfterAddAndReserve) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);

  // Empty: Full frontier.
  EXPECT_EQ(B.getFeasibleFormatMask(), ProductFormatMask);
  EXPECT_EQ(B.getFeasibleFormatMask(), productFeasibleFormatMask(0));

  MCInst St, A0, A1;
  St.setOpcode(Haydn::S_SW_WITH_IMM);
  A0.setOpcode(Haydn::ADD32);
  A1.setOpcode(Haydn::ADD64);
  ASSERT_TRUE(B.canAdd(St.getOpcode()));
  B.add(&St);
  EXPECT_EQ(B.getFeasibleFormatMask(), ProductFormatMask);
  EXPECT_EQ(B.getFeasibleFormatMask(),
            productFeasibleFormatMask(B.getOccupiedSlots()));

  ASSERT_TRUE(B.canAdd(A0.getOpcode()));
  B.add(&A0);
  EXPECT_EQ(B.getFeasibleFormatMask(), ProductFormatMask);

  // SMS reserve path: same frontier vocabulary.
  Bundle<MCInst> R(&Fmts);
  R.reserveByOpcode(Haydn::ADD32);
  R.reserveByOpcode(Haydn::ADD32);
  R.reserveByOpcode(Haydn::ADD32);
  EXPECT_EQ(R.getOccupiedSlots(), SlotBits(Haydn::SLOT_SET_E3));
  EXPECT_EQ(R.getFeasibleFormatMask(), ProductFormatMask)
      << "saturated Full still covers SLOT_SET_E3";
  EXPECT_FALSE(R.canAdd(Haydn::ADD32));
}

TEST(HaydnBundleTest, B41_ResourceCycleThreeADD32KeepFullFrontier) {
  // AIE AIEHazardRecognizer.cpp:173-214 ResourceCycle canReserve/reserve.
  // Three ADD32 reserves keep ProductFormatMask; fourth canReserve false.
  HaydnResourceCycle RC;
  EXPECT_EQ(RC.getFeasibleFormatMask(), ProductFormatMask);
  EXPECT_EQ(RC.getOccupiedSlots(), 0u);

  for (unsigned I = 0; I < 3; ++I) {
    ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD32)) << "ADD32 #" << I;
    RC.reserveByOpcode(Haydn::ADD32);
    EXPECT_EQ(RC.getFeasibleFormatMask(), ProductFormatMask) << "after #" << I;
  }
  EXPECT_EQ(RC.getOccupiedSlots(), SlotBits(Haydn::SLOT_SET_E3));
  EXPECT_FALSE(RC.canReserveByOpcode(Haydn::ADD32))
      << "fourth ADD32 must not fit once S0|S1|S2 reserved";
  EXPECT_EQ(RC.getFeasibleFormatMask(), ProductFormatMask);
  // ADD64 is S1|S2 only — also saturated.
  EXPECT_FALSE(RC.canReserveByOpcode(Haydn::ADD64));

  // MID overload (SMS ResourceManager primary path) agrees with opcode path.
  MCInstrDesc Desc{};
  Desc.Opcode = Haydn::ADD32;
  EXPECT_FALSE(RC.canReserveResources(&Desc));
  RC.clearResources();
  EXPECT_EQ(RC.getFeasibleFormatMask(), ProductFormatMask);
  EXPECT_TRUE(RC.canReserveResources(&Desc));
  RC.reserveResources(&Desc);
  EXPECT_EQ(RC.getFeasibleFormatMask(), ProductFormatMask);
  EXPECT_NE(RC.getOccupiedSlots(), 0u);
}

TEST(HaydnBundleTest, B41_PreRAProductFeasibleFormatMaskMatchesSolver) {
  // Pre-RA static helper is the same productFeasibleFormatMask (logical only).
  EXPECT_EQ(HaydnPreRASchedStrategy::productFeasibleFormatMask(0),
            ProductFormatMask);
  EXPECT_EQ(HaydnPreRASchedStrategy::productFeasibleFormatMask(Haydn::SLOT_SET_E3),
            productFeasibleFormatMask(Haydn::SLOT_SET_E3));
  EXPECT_EQ(HaydnPreRASchedStrategy::productFeasibleFormatMask(Haydn::SLOT_P30),
            ProductFormatMask);
}

//===----------------------------------------------------------------------===//
// SMS ResourceCycle live CycleState = post-RA tryAdd depth
//===----------------------------------------------------------------------===//
//
// AIE peers: AIEHazardRecognizer.cpp:173-214 ResourceCycle canReserve/reserve;
// AIEHazardRecognizer.h:315-328 Bundle-backed ResourceCycle.
// Haydn: live CycleState + tryAddProduct (HR CurrentCycleState peer).

/// Sequential ResourceCycle packing cycle count — mirrors calculateResMIIDFA
/// open-new-cycle-on-canReserve-false (MachinePipeliner ResourceManager).
static unsigned sequentialResourceCycleCount(ArrayRef<unsigned> Opcodes) {
  if (Opcodes.empty())
    return 0;
  unsigned Cycles = 0;
  HaydnResourceCycle RC;
  bool CycleOpen = false;
  for (unsigned Opc : Opcodes) {
    if (!RC.canReserveByOpcode(Opc)) {
      RC.clearResources();
      ++Cycles;
      CycleOpen = false;
      // Fresh product cycle must accept alts-bearing logicals (tests only
      // pass placeable opcodes).
      if (!RC.canReserveByOpcode(Opc))
        return ~0u;
    }
    RC.reserveByOpcode(Opc);
    CycleOpen = true;
  }
  if (CycleOpen)
    ++Cycles;
  return Cycles;
}

TEST(HaydnBundleTest, B42_ResourceCycleLiveState_ThreeADD32Members) {
  // Live State accumulates members + FeasibleFormatMask like post-RA HR.
  HaydnResourceCycle RC;
  EXPECT_TRUE(RC.getCycleState().empty());
  EXPECT_EQ(RC.getFeasibleFormatMask(), ProductFormatMask);
  EXPECT_EQ(RC.getFeasibleFormatMask(), RC.getCycleState().FeasibleFormatMask);

  for (unsigned I = 0; I < 3; ++I) {
    ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD32)) << "ADD32 #" << I;
    RC.reserveByOpcode(Haydn::ADD32);
    EXPECT_EQ(RC.getMemberCount(), I + 1u);
    EXPECT_EQ(RC.getFeasibleFormatMask(), RC.getCycleState().FeasibleFormatMask)
        << "live mask is State.FeasibleFormatMask after tryAdd #" << I;
    EXPECT_EQ(RC.getOccupiedSlots(), RC.getCycleState().OccupiedSlots);
  }
  EXPECT_EQ(RC.getOccupiedSlots(), SlotBits(Haydn::SLOT_SET_E3));
  // Product size-1: live mask still Full; occupancy rebuild agrees.
  EXPECT_EQ(RC.getFeasibleFormatMask(), ProductFormatMask);
  EXPECT_EQ(RC.getFeasibleFormatMask(),
            productFeasibleFormatMask(RC.getOccupiedSlots()));
  // Slot order S2 → S1 → S0 (same as HR tryAdd).
  EXPECT_EQ(RC.getCycleState().Members[0].FieldSlots, SlotBits(Haydn::SLOT_P32));
  EXPECT_EQ(RC.getCycleState().Members[1].FieldSlots, SlotBits(Haydn::SLOT_P31));
  EXPECT_EQ(RC.getCycleState().Members[2].FieldSlots, SlotBits(Haydn::SLOT_P30));
  EXPECT_FALSE(RC.canReserveByOpcode(Haydn::ADD32));
}

TEST(HaydnBundleTest, B42_ResourceCycleCountEqualsComputeProductResMII) {
  // Sequential canReserve/reserve cycle-count == pure computeProductResMII
  // on the same multiset (SMS live path ≡ pure greedy).
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_EQ(sequentialResourceCycleCount(Ops), 2u);
    EXPECT_EQ(computeProductResMII(Ops), 2u);
    EXPECT_EQ(sequentialResourceCycleCount(Ops), computeProductResMII(Ops));
  }
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_EQ(sequentialResourceCycleCount(Ops), 1u);
    EXPECT_EQ(computeProductResMII(Ops), 1u);
  }
  {
    unsigned Ops[] = {Haydn::S_LW_WITH_IMM, Haydn::S_LW_WITH_IMM, Haydn::X2MULA32};
    EXPECT_EQ(sequentialResourceCycleCount(Ops), 1u);
    EXPECT_EQ(computeProductResMII(Ops), 1u);
  }
  {
    // ST32 is S0-only — two ST32 need two cycles.
    unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::S_SW_WITH_IMM};
    EXPECT_EQ(sequentialResourceCycleCount(Ops), 2u);
    EXPECT_EQ(computeProductResMII(Ops), 2u);
  }
}

TEST(HaydnBundleTest, B42_MIDPathAgreesWithOpcodeAndClearResetsMask) {
  // MID overload (SMS ResourceManager primary) ≡ opcode path; clear resets.
  HaydnResourceCycle RC;
  MCInstrDesc Desc{};
  Desc.Opcode = Haydn::ADD32;

  EXPECT_TRUE(RC.canReserveResources(&Desc));
  EXPECT_TRUE(RC.canReserveByOpcode(Haydn::ADD32));
  RC.reserveResources(&Desc);
  EXPECT_EQ(RC.getMemberCount(), 1u);
  EXPECT_EQ(RC.getFeasibleFormatMask(), ProductFormatMask);
  EXPECT_EQ(RC.getFeasibleFormatMask(), RC.getCycleState().FeasibleFormatMask);

  // Fill remaining two slots via opcode path.
  RC.reserveByOpcode(Haydn::ADD32);
  RC.reserveByOpcode(Haydn::ADD32);
  EXPECT_FALSE(RC.canReserveResources(&Desc));
  EXPECT_FALSE(RC.canReserveByOpcode(Haydn::ADD32));

  RC.clearResources();
  EXPECT_EQ(RC.getMemberCount(), 0u);
  EXPECT_EQ(RC.getOccupiedSlots(), 0u);
  EXPECT_EQ(RC.getFeasibleFormatMask(), ProductFormatMask)
      << "clearResources resets ProductFormatMask";
  EXPECT_TRUE(RC.getCycleState().empty());
  EXPECT_TRUE(RC.canReserveResources(&Desc));
}

TEST(HaydnBundleTest, B42_ResourceCycleLDLDMACPacksOneCycle) {
  HaydnResourceCycle RC;
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::S_LW_WITH_IMM));
  RC.reserveByOpcode(Haydn::S_LW_WITH_IMM);
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::S_LW_WITH_IMM));
  RC.reserveByOpcode(Haydn::S_LW_WITH_IMM);
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::X2MULA32))
      << "MAC must co-issue with dual LD32 under live tryAdd; occ="
      << RC.getOccupiedSlots();
  RC.reserveByOpcode(Haydn::X2MULA32);
  EXPECT_EQ(RC.getMemberCount(), 3u);
  EXPECT_EQ(RC.getFeasibleFormatMask(), ProductFormatMask);
  EXPECT_EQ(RC.getFeasibleFormatMask(), RC.getCycleState().FeasibleFormatMask);
}

} // namespace
