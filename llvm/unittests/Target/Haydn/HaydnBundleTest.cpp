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
#include "HaydnPostRAMultiStage.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "gtest/gtest.h"
#include <utility>

namespace llvm {
const MCInstrInfo &getHaydnSharedMCInstrInfo();
}

// Opcode / regclass enums come via HaydnResourceCycle → HaydnPortModel →
// HaydnMCTargetDesc (GET_INSTRINFO_ENUM / GET_REGINFO_ENUM). Do not re-include
// HaydnGenInstrInfo.inc here — double GET_*_ENUM is a hard error.

using namespace llvm;
using namespace llvm::Haydn;
using namespace llvm::haydn::bundle;

namespace {

// Three occupied issue bits drop E96TwoEntry. ProductFormatMask (E2|E3=3) is
// the empty/≤2-member frontier, not a retired size-1 Full mask.
constexpr uint64_t E3OnlyFormatMask =
    formatRowBit(BundleFormatRowID::E96ThreeEntry);

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
            SlotBits(Haydn::SLOT0) | Haydn::SLOT1 | Haydn::SLOT2);
  A[3].setOpcode(Haydn::ADD32);
  EXPECT_FALSE(B.canAdd(A[3].getOpcode()))
      << "fourth ADD32 must conflict once S0|S1|S2 are full";
}

// Encode-time pack: four multi-slot ALU ops cannot all fit (issue/slot
// saturation). Three can fill one product cycle.
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
            SlotBits(Haydn::SLOT0) | Haydn::SLOT1 | Haydn::SLOT2);
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
  Ld.setOpcode(Haydn::LD32);
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
  EXPECT_NE(Occ & (Haydn::SLOT0 | Haydn::SLOT1), 0u);
  EXPECT_NE(Occ & (Haydn::SLOT1 | Haydn::SLOT2), 0u);
  EXPECT_EQ(Occ & Haydn::SLOT0 ? 1 : 0, (Occ & Haydn::SLOT0) ? 1 : 0);
}

TEST(HaydnBundleTest, StoreThenTwoAluFillsBundle) {
  // ST32 is S0-only; two multi-slot ALUs take S2 then S1 → full cycle.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St, A, X;
  St.setOpcode(Haydn::ST32);
  A.setOpcode(Haydn::ADD32);
  X.setOpcode(Haydn::XOR32);
  B.add(&St);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT0));
  ASSERT_TRUE(B.canAdd(A.getOpcode()));
  B.add(&A);
  ASSERT_TRUE(B.canAdd(X.getOpcode()));
  B.add(&X);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
  EXPECT_TRUE(B.hasValidFormat());
  EXPECT_FALSE(B.canAdd(Haydn::ADD32));
}

TEST(HaydnBundleTest, TwoS0OnlyOpsConflict) {
  // Two ST32 cannot share a cycle (both S0-only).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St0, St1;
  St0.setOpcode(Haydn::ST32);
  St1.setOpcode(Haydn::ST32);
  B.add(&St0);
  EXPECT_FALSE(B.canAdd(St1.getOpcode()));
}

TEST(HaydnBundleTest, LoadStore0StoresConflictAcrossFieldSlots) {
  // AIE canAdd is slot + isFormatAvailable (AIEBundle.h:62-105). Residual
  // FieldSlots would accept D_SW_L_WITH_IMM on S2 and ST8 on S0; Format E
  // unit injectivity must still refuse (both LOADSTORE0 e0 only).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst Sw, Or, Sb;
  Sw.setOpcode(Haydn::D_SW_L_WITH_IMM);
  Or.setOpcode(Haydn::OR64);
  Sb.setOpcode(Haydn::ST8);
  ASSERT_TRUE(B.canAdd(Sw.getOpcode()));
  B.add(&Sw);
  ASSERT_TRUE(B.canAdd(Or.getOpcode()));
  B.add(&Or);
  EXPECT_FALSE(B.canAdd(Sb.getOpcode()))
      << "ST8 must not canAdd beside D_SW_L_WITH_IMM; occ="
      << B.getOccupiedSlots();

  Bundle<MCInst> B2(&Fmts);
  B2.add(&Sw);
  EXPECT_FALSE(B2.canAdd(Sb.getOpcode()));
}

TEST(HaydnBundleTest, DualLoadCanShareCycle) {
  // LD32 is S0|S1 — two loads must pack (dual-load product).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst L0, L1;
  L0.setOpcode(Haydn::LD32);
  L1.setOpcode(Haydn::LD32);
  B.add(&L0);
  ASSERT_TRUE(B.canAdd(L1.getOpcode()))
      << "second LD32 must fit on the free load slot; occ="
      << B.getOccupiedSlots();
  B.add(&L1);
  EXPECT_EQ(B.size(), 2u);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1));
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
  B.add(&A, MCSlotKind::Haydn_SLOT_S0);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT0));
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_S0), &A);
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_S2), nullptr);
}

TEST(HaydnBundleTest, HintSlotFallsBackWhenOccupied) {
  // Hint S2 after S2 is taken → fall back to first free legal (S1).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, X;
  A.setOpcode(Haydn::ADD32);
  X.setOpcode(Haydn::XOR32);
  B.add(&A); // prefers S2
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT2));
  ASSERT_TRUE(B.canAdd(X.getOpcode()));
  B.add(&X, MCSlotKind::Haydn_SLOT_S2); // conflict → fallback
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT2 | Haydn::SLOT1));
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_S1), &X);
}

TEST(HaydnBundleTest, HintIllegalSlotFallsBack) {
  // ST32 is not legal on S2; hint S2 must fall back to S0.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St;
  St.setOpcode(Haydn::ST32);
  ASSERT_TRUE(B.canAdd(St.getOpcode()));
  B.add(&St, MCSlotKind::Haydn_SLOT_S2);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT0));
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_S0), &St);
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
  EXPECT_FALSE(B.canAdd(Haydn::LD32));
  EXPECT_FALSE(B.canAdd(Haydn::X2MULA32));
  EXPECT_FALSE(B.canAdd(Haydn::ST32));
}

TEST(HaydnBundleTest, ReserveThenAddSharesOccupancy) {
  // SMS reserve + later real add must see the same OccupiedSlots budget.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  B.reserveByOpcode(Haydn::ST32); // claims S0 without Instrs
  EXPECT_TRUE(B.empty());
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT0));
  EXPECT_FALSE(B.canAdd(Haydn::ST32)); // second store blocked
  MCInst A;
  A.setOpcode(Haydn::ADD32);
  ASSERT_TRUE(B.canAdd(A.getOpcode()));
  B.add(&A);
  EXPECT_EQ(B.size(), 1u);
  EXPECT_NE(B.getOccupiedSlots() & Haydn::SLOT0, 0u);
  EXPECT_NE(B.getOccupiedSlots() & (Haydn::SLOT1 | Haydn::SLOT2), 0u);
}

TEST(HaydnBundleTest, Alu64CannotPairWithTwoS0Only) {
  // ADD64 is S1|S2 only; one ST32 (S0) + ADD64 is legal; second ST32 is not.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St, Add64;
  St.setOpcode(Haydn::ST32);
  Add64.setOpcode(Haydn::ADD64);
  B.add(&St);
  ASSERT_TRUE(B.canAdd(Add64.getOpcode()));
  B.add(&Add64);
  EXPECT_TRUE(B.hasValidFormat());
  EXPECT_FALSE(B.canAdd(Haydn::ST32));
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
  Ld.setOpcode(Haydn::LD32);
  B.add(&Alu);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT2));
  ASSERT_TRUE(B.canAdd(Ld.getOpcode()));
  B.add(&Ld);
  EXPECT_NE(B.getOccupiedSlots() & (Haydn::SLOT0 | Haydn::SLOT1), 0u);
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_S2), &Alu);
}

//===----------------------------------------------------------------------===//
// Extended pack matrix (encode half)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleTest, LoadMacAluClassicDspFill) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst Ld, Mac, Alu;
  Ld.setOpcode(Haydn::LD32);
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
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
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
            SlotBits(Haydn::SLOT1 | Haydn::SLOT2));
  EXPECT_FALSE(B.canAdd(Haydn::X2MULA32));
  // S0 free for a store.
  MCInst St;
  St.setOpcode(Haydn::ST32);
  ASSERT_TRUE(B.canAdd(St.getOpcode()));
  B.add(&St);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
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
  EXPECT_TRUE(B.canAdd(Haydn::ST32));
}

TEST(HaydnBundleTest, DualLoadThenRejectThirdLoad) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst L0, L1, L2;
  L0.setOpcode(Haydn::LD32);
  L1.setOpcode(Haydn::LD32);
  L2.setOpcode(Haydn::LD32);
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
  L0.setOpcode(Haydn::LD32);
  L1.setOpcode(Haydn::LD32);
  B.add(&L0, MCSlotKind::Haydn_SLOT_S0);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT0));
  ASSERT_TRUE(B.canAdd(L1.getOpcode()));
  B.add(&L1, MCSlotKind::Haydn_SLOT_S1);
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_S1), &L1);
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1));
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
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
  EXPECT_FALSE(ByRes.canAdd(Haydn::ADD32));
  EXPECT_FALSE(ByAdd.canAdd(Haydn::ADD32));
}

TEST(HaydnBundleTest, AllSingleSlotCombosHaveValidFormat) {
  // Any subset occupancy after packing real ops must remain format-valid.
  HaydnMCFormats Fmts;
  const unsigned Seeds[] = {Haydn::ST32, Haydn::ADD32, Haydn::ADD64,
                            Haydn::LD32,  Haydn::X2MULA32};
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
  const unsigned Rejects[] = {Haydn::ADD32, Haydn::LD32, Haydn::ST32,
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
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT2));
  EXPECT_EQ(B.lastPickedSlot(), MCSlotKind(MCSlotKind::Haydn_SLOT_S2));
  EXPECT_EQ(B.at(MCSlotKind::Haydn_SLOT_S2), &A);
}

TEST(HaydnBundleTest, B24_BundleOccupancyMatchesSolver) {
  // Adapter contract: sequential Bundle.add occupancy == pure exactTryAdd
 // preferred candidate.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  haydn::bundle::CycleCandidateSet Cands =
      haydn::bundle::makeProductCandidateSet();
  MCInst Ops[3];
  const unsigned Seq[] = {Haydn::ST32, Haydn::ADD64, Haydn::ADD32};
  for (unsigned I = 0; I < 3; ++I) {
    Ops[I].setOpcode(Seq[I]);
    ASSERT_TRUE(B.canAdd(Seq[I]));
    ASSERT_TRUE(haydn::bundle::canExactTryAddProduct(Cands, Fmts, Seq[I]));
    B.add(&Ops[I]);
    ASSERT_TRUE(haydn::bundle::exactTryAddProduct(Cands, Fmts, Seq[I]));
    EXPECT_EQ(B.getOccupiedSlots(),
              haydn::bundle::selectPreferredCandidate(Cands).OccupiedSlots)
        << "idx " << I;
  }
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
}

TEST(HaydnBundleTest, VF21_ExactRematchPacksADD32_2xADD64) {
  // Bundle must accept ADD32 + 2×ADD64 via exact rematch (first-fit dead-end).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, B1, B2;
  A.setOpcode(Haydn::ADD32);
  B1.setOpcode(Haydn::ADD64);
  B2.setOpcode(Haydn::ADD64);
  ASSERT_TRUE(B.canAdd(Haydn::ADD32));
  B.add(&A);
  ASSERT_TRUE(B.canAdd(Haydn::ADD64));
  B.add(&B1);
  ASSERT_TRUE(B.canAdd(Haydn::ADD64))
      << "Bundle exact matching must not first-fit dead-end";
  B.add(&B2);
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL));
  EXPECT_EQ(B.size(), 3u);
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
  EXPECT_TRUE(B.canAdd(Haydn::ST32));
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
  L.setOpcode(Haydn::LD32);
  B.add(&A); // S2
  B.add(&L); // prefers high free load field (S1)
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT2 | Haydn::SLOT1));
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
  St.setOpcode(Haydn::ST32);
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

  // SMS reserve path: 3×ADD32 occupies SLOT_ALL → E3-only (E2 holds 2).
  Bundle<MCInst> R(&Fmts);
  R.reserveByOpcode(Haydn::ADD32);
  R.reserveByOpcode(Haydn::ADD32);
  R.reserveByOpcode(Haydn::ADD32);
  EXPECT_EQ(R.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL));
  EXPECT_EQ(R.getFeasibleFormatMask(), E3OnlyFormatMask)
      << "three occupied entries drop E2; E3 still covers SLOT_ALL";
  EXPECT_FALSE(R.canAdd(Haydn::ADD32));
}

TEST(HaydnBundleTest, B41_ResourceCycleThreeADD32KeepFullFrontier) {
  // AIE AIEHazardRecognizer.cpp:173-214 ResourceCycle canReserve/reserve.
  // One/two ADD32 keep ProductFormatMask; third drops E2 (E3-only); fourth
  // canReserve false.
  HaydnResourceCycle RC;
  EXPECT_EQ(RC.getFeasibleFormatMask(), ProductFormatMask);
  EXPECT_EQ(RC.getOccupiedSlots(), 0u);

  for (unsigned I = 0; I < 3; ++I) {
    ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD32)) << "ADD32 #" << I;
    RC.reserveByOpcode(Haydn::ADD32);
    const uint64_t Expect =
        (I + 1 >= 3) ? E3OnlyFormatMask : ProductFormatMask;
    EXPECT_EQ(RC.getFeasibleFormatMask(), Expect) << "after #" << I;
  }
  EXPECT_EQ(RC.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL));
  EXPECT_FALSE(RC.canReserveByOpcode(Haydn::ADD32))
      << "fourth ADD32 must not fit once S0|S1|S2 reserved";
  EXPECT_EQ(RC.getFeasibleFormatMask(), E3OnlyFormatMask);
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
  EXPECT_EQ(HaydnPreRASchedStrategy::productFeasibleFormatMask(Haydn::SLOT_ALL),
            productFeasibleFormatMask(Haydn::SLOT_ALL));
  EXPECT_EQ(HaydnPreRASchedStrategy::productFeasibleFormatMask(Haydn::SLOT0),
            ProductFormatMask);
}

//===----------------------------------------------------------------------===//
// SMS ResourceCycle live CycleCandidateSet = post-RA exact tryAdd depth
//===----------------------------------------------------------------------===//
//
// AIE peers: AIEHazardRecognizer.cpp:173-214 ResourceCycle canReserve/reserve;
// AIEHazardRecognizer.h:315-328 Bundle-backed ResourceCycle.
// Haydn: live CycleCandidateSet + exactTryAddProduct (HR peer).

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
  EXPECT_EQ(RC.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL));
  // Three members drop E2; occupancy rebuild agrees (E3-only).
  EXPECT_EQ(RC.getFeasibleFormatMask(), E3OnlyFormatMask);
  EXPECT_EQ(RC.getFeasibleFormatMask(),
            productFeasibleFormatMask(RC.getOccupiedSlots()));
  // Slot order S2 → S1 → S0 (same as HR tryAdd).
  EXPECT_EQ(RC.getCycleState().Members[0].FieldSlots, SlotBits(Haydn::SLOT2));
  EXPECT_EQ(RC.getCycleState().Members[1].FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_EQ(RC.getCycleState().Members[2].FieldSlots, SlotBits(Haydn::SLOT0));
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
    unsigned Ops[] = {Haydn::LD32, Haydn::LD32, Haydn::X2MULA32};
    EXPECT_EQ(sequentialResourceCycleCount(Ops), 1u);
    EXPECT_EQ(computeProductResMII(Ops), 1u);
  }
  {
    // ST32 is S0-only — two ST32 need two cycles.
    unsigned Ops[] = {Haydn::ST32, Haydn::ST32};
    EXPECT_EQ(sequentialResourceCycleCount(Ops), 2u);
    EXPECT_EQ(computeProductResMII(Ops), 2u);
  }
  {
 // : ADD32+2×ADD64 packs in one cycle via exact rematch.
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    EXPECT_EQ(sequentialResourceCycleCount(Ops), 1u);
    EXPECT_EQ(computeProductResMII(Ops), 1u);
  }
}

// Three-ready rematch through SMS ResourceCycle (list-sched product peer):
// preferred first-fit freezes ADD32 on S2 then ADD64 on S1 and loses the
// second ADD64; live CycleCandidateSet rematches so all three pack in one
// modulo cycle (ADD32 → S0). SMS-RESMII: exact/DFA ResMII = 1; preferred
// collapse overestimate is positive (qualification contrast only).
TEST(HaydnBundleTest, SMS_ThreeReadyRematch_ADD32_2xADD64) {
  HaydnMCFormats Fmts;

  // Preferred freeze dead-end — baseline exact matching must defeat.
  {
    CycleState FirstFit = makeProductCycleState();
    ASSERT_TRUE(tryAddProduct(FirstFit, Fmts, Haydn::ADD32));
    EXPECT_EQ(FirstFit.Members.back().FieldSlots, SlotBits(Haydn::SLOT2));
    ASSERT_TRUE(tryAddProduct(FirstFit, Fmts, Haydn::ADD64));
    EXPECT_EQ(FirstFit.Members.back().FieldSlots, SlotBits(Haydn::SLOT1));
    EXPECT_FALSE(tryAddProduct(FirstFit, Fmts, Haydn::ADD64))
        << "preferred freeze must dead-end the scarce S1|S2 pair";
  }

  // ResourceCycle exact path: three canReserve/reserve succeed via rematch.
  HaydnResourceCycle RC;
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD32));
  RC.reserveByOpcode(Haydn::ADD32);
  EXPECT_EQ(RC.getMatchingFrontierSize(), 3u)
      << "frontier retains S0|S1|S2 matchings after one ADD32";
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD64));
  RC.reserveByOpcode(Haydn::ADD64);
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD64))
      << "SMS ResourceCycle must rematch, not first-fit freeze";
  RC.reserveByOpcode(Haydn::ADD64);
  EXPECT_EQ(RC.getMemberCount(), 3u);
  EXPECT_EQ(RC.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL));
  EXPECT_GE(RC.getCandidates().size(), 1u);
  // Preferred survivor after rematch: ADD32 on S0 so both ADD64 take S1|S2.
  EXPECT_EQ(RC.getCycleState().Members[0].LogicalOpcode, Haydn::ADD32);
  EXPECT_EQ(RC.getCycleState().Members[0].FieldSlots, SlotBits(Haydn::SLOT0));

  // MID overload (SMS ResourceManager placement) agrees — descriptor ports
  // only; format rematch is opcode-keyed.
  HaydnResourceCycle RCmid;
  MCInstrDesc D32{}, D64{};
  D32.Opcode = Haydn::ADD32;
  D64.Opcode = Haydn::ADD64;
  ASSERT_TRUE(RCmid.canReserveResources(&D32));
  RCmid.reserveResources(&D32);
  ASSERT_TRUE(RCmid.canReserveResources(&D64));
  RCmid.reserveResources(&D64);
  ASSERT_TRUE(RCmid.canReserveResources(&D64))
      << "MID placement path must rematch the three-ready triple";
  RCmid.reserveResources(&D64);
  EXPECT_EQ(RCmid.getMemberCount(), 3u);
  EXPECT_EQ(RCmid.getCycleState().Members[0].FieldSlots,
            SlotBits(Haydn::SLOT0));

  // SMS-RESMII: DFA/RC walk == exhaustive; preferred overestimates.
  unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
  EXPECT_EQ(sequentialResourceCycleCount(Ops), 1u);
  EXPECT_EQ(computeProductResMII(Ops), 1u);
  EXPECT_EQ(computeExhaustiveProductResMII(Ops), 1u);
  EXPECT_EQ(productResMIIOverestimate(Ops), 0);
  EXPECT_GE(computePreferredProductResMII(Ops), 2u);
  EXPECT_GE(preferredProductResMIIOverestimate(Ops), 1);
}

// Matching frontier (durable-rules 8a): after one multi-slot logical, retain
// every nondominated partial matching (S0 / S1 / S2 for ADD32), not a single
// first-fit freeze. canReserve probes expand the whole set.
TEST(HaydnBundleTest, SMS_MatchingFrontier_MultiCandidateAfterOneADD32) {
  HaydnResourceCycle RC;
  EXPECT_EQ(RC.getMatchingFrontierSize(), 1u) << "empty seed is one candidate";
  EXPECT_EQ(RC.getMatchingFrontierFormatMask(), ProductFormatMask);
  EXPECT_EQ(RC.getMatchingFrontierFormatMask(), RC.getFeasibleFormatMask());

  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD32));
  RC.reserveByOpcode(Haydn::ADD32);

  // Three disjoint single-slot placements under Full — none packing-dominates
  // another (Occupied bits differ; FeasibleFormatMask stays ProductFormatMask).
  EXPECT_EQ(RC.getMatchingFrontierSize(), 3u)
      << "frontier must retain S0|S1|S2 matchings after one ADD32; first-fit "
         "would freeze size 1";
  EXPECT_EQ(RC.getMemberCount(), 1u);
  EXPECT_EQ(RC.getMatchingFrontierFormatMask(), ProductFormatMask);

  // Count distinct OccupiedSlots among survivors — must cover all three slots.
  SlotBits Seen = 0;
  for (const CycleState &S : RC.getCandidates()) {
    EXPECT_EQ(S.memberCount(), 1u);
    EXPECT_EQ(S.FeasibleFormatMask, ProductFormatMask);
    Seen |= S.OccupiedSlots;
  }
  EXPECT_EQ(Seen, SlotBits(Haydn::SLOT_ALL));

  // Second ADD32 still expands the full frontier (not preferred-only).
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD32));
  RC.reserveByOpcode(Haydn::ADD32);
  EXPECT_EQ(RC.getMemberCount(), 2u);
  EXPECT_GE(RC.getMatchingFrontierSize(), 1u);
  EXPECT_EQ(RC.getMatchingFrontierFormatMask(), ProductFormatMask);
}

// SMS ResourceCycle frontier ≡ pure exactTryAddProduct candidate set (HR peer).
TEST(HaydnBundleTest, SMS_MatchingFrontier_ParityWithExactTryAddProduct) {
  HaydnMCFormats Fmts;
  CycleCandidateSet Pure = makeProductCandidateSet(Fmts.getPacketFormats());
  HaydnResourceCycle RC;

  const unsigned Seq[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ST32};
  for (unsigned Opc : Seq) {
    ASSERT_TRUE(canExactTryAddProduct(Pure, Fmts, Opc)) << "pure " << Opc;
    ASSERT_TRUE(exactTryAddProduct(Pure, Fmts, Opc));
    ASSERT_TRUE(RC.canReserveByOpcode(Opc)) << "RC " << Opc;
    RC.reserveByOpcode(Opc);

    uint64_t PureUnion = 0;
    for (const CycleState &S : Pure)
      PureUnion |= S.FeasibleFormatMask;

    EXPECT_EQ(RC.getMatchingFrontierSize(), Pure.size()) << "after " << Opc;
    EXPECT_EQ(RC.getOccupiedSlots(),
              selectPreferredCandidate(Pure).OccupiedSlots)
        << "after " << Opc;
    EXPECT_EQ(RC.getMatchingFrontierFormatMask(), PureUnion) << "after " << Opc;
    EXPECT_EQ(RC.getMemberCount(), selectPreferredCandidate(Pure).memberCount())
        << "after " << Opc;
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
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::LD32));
  RC.reserveByOpcode(Haydn::LD32);
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::LD32));
  RC.reserveByOpcode(Haydn::LD32);
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::X2MULA32))
      << "MAC must co-issue with dual LD32 under live tryAdd; occ="
      << RC.getOccupiedSlots();
  RC.reserveByOpcode(Haydn::X2MULA32);
  EXPECT_EQ(RC.getMemberCount(), 3u);
  EXPECT_EQ(RC.getFeasibleFormatMask(), E3OnlyFormatMask);
  EXPECT_EQ(RC.getFeasibleFormatMask(), RC.getCycleState().FeasibleFormatMask);
}

//===----------------------------------------------------------------------===//
// : SMS ResourceCycle port demand + alone-ops (descriptor-derived capacity)
//===----------------------------------------------------------------------===//
//
// Format-only canReserveByOpcode still packs 3×ADD32 (slot cap). Port-aware
// path enforces GPR 4R2W / DR 7R3W / AR 2R2W. SMS placement uses MID ports;
// ResMII packing uses MI count*Ports (MRI-correct for vregs).

TEST(HaydnBundleTest, VF3_ResourceCycleGPRWriteBudgetTwo) {
  // Two independent 2R1W ALU ops fill the 2W budget; a third write must fail
  // even though a Free slot remains under Full (3-issue).
  HaydnResourceCycle RC;
  HaydnCyclePortDemand ALU; // ADD32-shaped: 2R1W GPR
  ALU.GPRReads = 2;
  ALU.GPRWrites = 1;

  ASSERT_TRUE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, ALU));
  RC.reserveByOpcodeWithPorts(Haydn::ADD32, ALU);
  ASSERT_TRUE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, ALU));
  RC.reserveByOpcodeWithPorts(Haydn::ADD32, ALU);
  EXPECT_EQ(RC.getPortDemand().GPRWrites, 2u);
  EXPECT_EQ(RC.getPortDemand().GPRReads, 4u);
  EXPECT_FALSE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, ALU))
      << "third 1W must exceed HAYDN_GPR_WRITE_PORTS=2; occ slots still free="
      << (RC.getOccupiedSlots() != SlotBits(Haydn::SLOT_ALL));
  // Format-only path still accepts a third (no ports) — documents MID vs opcode.
  EXPECT_TRUE(RC.canReserveByOpcode(Haydn::ADD32));
}

TEST(HaydnBundleTest, VF3_ResourceCycleGPRReadBudgetFour) {
  HaydnResourceCycle RC;
  HaydnCyclePortDemand Heavy; // 3R0W — two fill 6R > 4R
  Heavy.GPRReads = 3;
  ASSERT_TRUE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, Heavy));
  RC.reserveByOpcodeWithPorts(Haydn::ADD32, Heavy);
  EXPECT_FALSE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, Heavy))
      << "3R+3R exceeds HAYDN_GPR_READ_PORTS=4";
}

TEST(HaydnBundleTest, VF3_ResourceCycleDRWriteBudgetThree) {
  // ADD64 is only S1|S2 (2 slots), so charge DR writes via ADD32 format
  // path to isolate the DR 3W pool from slot occupancy.
  HaydnResourceCycle RC;
  HaydnCyclePortDemand DRW;
  DRW.DRWrites = 1;
  for (unsigned I = 0; I < HAYDN_DR_WRITE_PORTS; ++I) {
    ASSERT_TRUE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, DRW)) << I;
    RC.reserveByOpcodeWithPorts(Haydn::ADD32, DRW);
  }
  EXPECT_EQ(RC.getPortDemand().DRWrites, HAYDN_DR_WRITE_PORTS);
  EXPECT_FALSE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, DRW))
      << "fourth DR write must exceed HAYDN_DR_WRITE_PORTS=3";
}

TEST(HaydnBundleTest, VF3_ResourceCycleARBudgetTwo) {
  HaydnResourceCycle RC;
  HaydnCyclePortDemand AR;
  AR.ARReads = 1;
  AR.ARWrites = 1;
  ASSERT_TRUE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, AR));
  RC.reserveByOpcodeWithPorts(Haydn::ADD32, AR);
  // Second 1R1W → 2R2W at cap.
  ASSERT_TRUE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, AR));
  RC.reserveByOpcodeWithPorts(Haydn::ADD32, AR);
  EXPECT_FALSE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, AR));
}

TEST(HaydnBundleTest, VF3_ResourceCycleClearResetsPortsAndAlone) {
  HaydnResourceCycle RC;
  HaydnCyclePortDemand ALU;
  ALU.GPRReads = 2;
  ALU.GPRWrites = 1;
  RC.reserveByOpcodeWithPorts(Haydn::ADD32, ALU);
  EXPECT_NE(RC.getPortDemand().GPRReads, 0u);
  RC.clearResources();
  EXPECT_EQ(RC.getPortDemand().GPRReads, 0u);
  EXPECT_EQ(RC.getPortDemand().GPRWrites, 0u);
  EXPECT_FALSE(RC.hasAloneOp());
  EXPECT_TRUE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, ALU));
}

TEST(HaydnBundleTest, VF3_ResourceCycleArctanIssueAlone) {
  // ARCTAN/SIN_COS must issue alone (PackLegality / HR peer). Opcode-keyed.
  HaydnResourceCycle RC;
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ARCTAN));
  RC.reserveByOpcode(Haydn::ARCTAN);
  EXPECT_TRUE(RC.hasAloneOp());
  EXPECT_FALSE(RC.canReserveByOpcode(Haydn::ADD32))
      << "nothing co-issues with ARCTAN";
  EXPECT_FALSE(RC.canReserveByOpcode(Haydn::SIN_COS));

  RC.clearResources();
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD32));
  RC.reserveByOpcode(Haydn::ADD32);
  EXPECT_FALSE(RC.canReserveByOpcode(Haydn::ARCTAN))
      << "ARCTAN refuses a non-empty cycle";
}

// Regression: reserveWithPorts must charge format/alone BEFORE Ports += Demand.
// isPackingEmpty/canReserveFormatAndAlone read Ports; charging first made
// alone-with-ports and no-alt empty-escape re-assert fail (SMS crash class).
TEST(HaydnBundleTest, ResourceCycleReservePortsAfterFormatAlone) {
  HaydnCyclePortDemand SomePorts;
  SomePorts.GPRReads = 1;
  SomePorts.GPRWrites = 1;

  // Alone + non-zero ports on an empty cycle must not assert / must stick.
  {
    HaydnResourceCycle RC;
    ASSERT_TRUE(RC.canReserveByOpcodeWithPorts(Haydn::ARCTAN, SomePorts));
    RC.reserveByOpcodeWithPorts(Haydn::ARCTAN, SomePorts);
    EXPECT_TRUE(RC.hasAloneOp());
    EXPECT_EQ(RC.getPortDemand().GPRWrites, 1u);
    EXPECT_FALSE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, SomePorts));
  }

  // No-alt empty-escape (RET has no PlacementAlternatives) with ports.
  {
    HaydnResourceCycle RC;
    ASSERT_TRUE(RC.canReserveByOpcodeWithPorts(Haydn::RET, SomePorts));
    RC.reserveByOpcodeWithPorts(Haydn::RET, SomePorts);
    EXPECT_EQ(RC.getPortDemand().GPRWrites, 1u);
    // Escape does not consume slots; cycle is no longer empty for another no-alt.
    EXPECT_FALSE(RC.canReserveByOpcodeWithPorts(Haydn::RET, SomePorts));
  }
}

// SMS-HOOK II-wrap false-accept differential (plan §8.4 #8): two independent
// ResourceCycles (ResourceManager per-modulo-phase model) both accept an
// issue-alone op. Catalog multi-cycle StageCycles=2 under II=2 spans phase 1
// from issue 0, so issue-time-only acceptance is a false accept relative to
// multi-cycle wrap — SMS-HOOK fail-closes multi-cycle stages (product class-3
// disabled). Does not implement multi-cycle booking; pure demonstration.
// Lit peer: sms-format-hook-iiwrap-reject.mir.
TEST(HaydnBundleTest, SMSHookIIWrapIssueTimeOnlyFalseAccept) {
  using namespace llvm::haydn::restriction;
  EXPECT_TRUE(HaydnResourceCycle::issueTimeOnlyFalseAcceptsIIWrapAloneConflict());
  // Explicit dual-cycle accept (same model as the helper).
  HaydnResourceCycle C0, C1;
  ASSERT_TRUE(C0.canReserveByOpcode(Haydn::ARCTAN));
  ASSERT_TRUE(C1.canReserveByOpcode(Haydn::ARCTAN));
  C0.reserveByOpcode(Haydn::ARCTAN);
  C1.reserveByOpcode(Haydn::ARCTAN);
  EXPECT_TRUE(C0.hasAloneOp());
  EXPECT_TRUE(C1.hasAloneOp());
  // II-wrap phase math: 2-cycle occupancy from phase 0 under II=2 claims 1.
  EXPECT_TRUE(smsIIWrapOccupiesPhase(0, 2, 2, 1));
  EXPECT_TRUE(smsHookRejectsIIWrapFalseAccept(2, 2));
  // Single-cycle alone is class-1 and does not trip II-wrap reject polarity.
  EXPECT_FALSE(smsHookRejectsIIWrapFalseAccept(1, 2));
}

// FE5B WP4 whole-kernel periodic certificate (G-SMS-PRE-RA-HEXAGON):
// original-loop retain until final accept, recoverable rollback on fail,
// II-wrap/long occupancy fail-closed, same-bank simultaneous defs fail-closed.
// Does not enable product multi-stage (WP5). Authority: HaydnResourceCycle.
TEST(HaydnBundleTest, SMSPeriodicCertificatePins) {
  using RC = HaydnResourceCycle;
  EXPECT_TRUE(RC::productPeriodicCertificatePins());

  // Lifecycle retain / discard / rollback polarity.
  EXPECT_TRUE(SMSPeriodicCertificate::originalLoopMustRemain(
      SMSCertLifecycle::OriginalRetained));
  EXPECT_TRUE(SMSPeriodicCertificate::originalLoopMustRemain(
      SMSCertLifecycle::PreRewriteProved));
  EXPECT_TRUE(SMSPeriodicCertificate::originalLoopMustRemain(
      SMSCertLifecycle::PostRewriteValid));
  EXPECT_FALSE(SMSPeriodicCertificate::mayDiscardOriginalLoop(
      SMSCertLifecycle::OriginalRetained));
  EXPECT_TRUE(SMSPeriodicCertificate::mayDiscardOriginalLoop(
      SMSCertLifecycle::Accepted));
  EXPECT_TRUE(SMSPeriodicCertificate::mustRollback(SMSCertLifecycle::Rejected));

  // Legal two-phase single-cycle kernel certifies to Accepted.
  const SMSCertPhaseOp Legal[] = {
      {0, Haydn::ADD32, 1, 1, 1},
      {1, Haydn::XOR32, 1, 1, 2},
  };
  EXPECT_TRUE(RC::proveWholeKernelPeriodicPhases(/*II=*/2, Legal));
  EXPECT_EQ(RC::runPeriodicCertificate(/*II=*/2, Legal, /*Post=*/true),
            SMSCertLifecycle::Accepted);
  // Post-rewrite fail → Rejected (rollback); original was retained until then.
  EXPECT_EQ(RC::runPeriodicCertificate(/*II=*/2, Legal, /*Post=*/false),
            SMSCertLifecycle::Rejected);

  // Multi-cycle / II-wrap fail-closed.
  const SMSCertPhaseOp Multi[] = {{0, Haydn::ADD32, 2, 1, 1}};
  EXPECT_FALSE(RC::proveWholeKernelPeriodicPhases(/*II=*/2, Multi));
  EXPECT_TRUE(RC::productIIWrapLongOccupancyFailsClosed(/*Stage=*/2, /*II=*/2));

  // Same-phase same-reg WAW fail-closed.
  const SMSCertPhaseOp Waw[] = {
      {0, Haydn::ADD32, 1, 1, 9},
      {0, Haydn::XOR32, 1, 1, 9},
  };
  EXPECT_TRUE(RC::sameBankSimultaneousDefsFailClosed(Waw, /*II=*/1));
  EXPECT_FALSE(RC::proveWholeKernelPeriodicPhases(/*II=*/1, Waw));
}


// Soft-exit format-SMS QoR (SMS slice): II floors + post-RA exact-pack
// metrics; no HANDOFF invent. Pins HaydnResourceCycle::softExitIIFloor =
// max(format exhaustive ResMII, port ResMII). RecMII remains DDG/itinerary
// (macc-acc-feedback lit); ResourceCycle never fabricates recurrence numbers
// or durable BUNDLE roots. Dual-run Full-only product vs generic residual
// ranking freeze lives in sms-format-generic-baseline.ll (PROD/GEN/RP
// ResMII/II parity, VF3-G2) and sms-format-ilp-crit-dual-run.ll (ILP multi-
// load / dual-acc + critical-path chain residual attribution, VF3-G3).
// Sibling pre-RA: HaydnPortModelTest.PreRASoftExitQoRFloorsAndExactPack.
TEST(HaydnBundleTest, SMSSoftExitQoRFloorsAndExactPack) {
  using RC = HaydnResourceCycle;
  using namespace llvm::haydn::bundle;

  // Classic port-binds-II body: three independent 1W GPR ops.
  // Format-only exhaustive ResMII = 1 (Full three slots); port floor = 2.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_EQ(computeExhaustiveProductResMII(Ops), 1u);
    EXPECT_EQ(RC::portLowerBoundResMII(/*GPRR=*/0, /*GPRW=*/3), 2u);
    EXPECT_EQ(RC::softExitIIFloor(Ops, /*GPRR=*/0, /*GPRW=*/3), 2u);
    EXPECT_GT(RC::softExitIIFloor(Ops, 0, 3),
              computeExhaustiveProductResMII(Ops));
    EXPECT_TRUE(RC::qualKernelExactlyPackable(Ops));
    EXPECT_EQ(productResMIIOverestimate(Ops), 0);
    EXPECT_TRUE(RC::qualKernelFormsOneExactCycle(Ops));
    // Port-aware sequential packing peer: VF3_PortAwareResMIITwoWritesNeedTwoCycles.
  }

  // Qualification ALU coissue: format packs one cycle; full 3×(2R1W) ports
  // bind soft-exit II ≥ 2. Exact-pack remains true (multi-cycle cover OK).
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
    EXPECT_TRUE(RC::qualKernelFormsOneExactCycle(Ops));
    EXPECT_TRUE(RC::qualKernelCoissuePackable(Ops));
    EXPECT_TRUE(RC::qualKernelExactlyPackable(Ops));
    EXPECT_EQ(computeExhaustiveProductResMII(Ops), 1u);
    EXPECT_EQ(RC::portLowerBoundResMII(/*GPRR=*/6, /*GPRW=*/3), 2u);
    EXPECT_EQ(RC::softExitIIFloor(Ops, 6, 3), 2u);
    EXPECT_EQ(RC::portLowerBoundResMII(4, 2), 1u);
    EXPECT_EQ(RC::softExitIIFloor(Ops, 4, 2), 1u);
  }

  // Three-ready rematch: format ResMII 1; soft-exit with light ports stays 1.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    EXPECT_TRUE(RC::qualKernelFormsOneExactCycle(Ops));
    EXPECT_EQ(computeExhaustiveProductResMII(Ops), 1u);
    EXPECT_EQ(RC::softExitIIFloor(Ops, /*GPRR=*/2, /*GPRW=*/1, /*DRR=*/4,
                                  /*DRW=*/2),
              1u);
    EXPECT_TRUE(RC::qualKernelExactlyPackable(Ops));
    EXPECT_FALSE(productResMIIFailsQualification(Ops));
  }

  // Empty body: floors 0.
  EXPECT_EQ(RC::softExitIIFloor(ArrayRef<unsigned>(), 0, 0), 0u);

  // Greedy order-trap: qualification still fail-closes on overestimate;
  // exact-pack stays true (cover exists; overestimate is an II floor).
  {
    unsigned Ops[] = {Haydn::ST32, Haydn::ST32, Haydn::ADD32, Haydn::ADD32,
                      Haydn::ADD32};
    EXPECT_TRUE(productResMIIFailsQualification(Ops));
    EXPECT_TRUE(RC::qualKernelExactlyPackable(Ops));
    EXPECT_EQ(computeExhaustiveProductResMII(Ops), 2u);
    EXPECT_EQ(RC::softExitIIFloor(Ops, 0, 0), 2u);
  }

  // MOVE32-class MI path: 3×2R1W (per-field, 0ad0d5d64088) → port floor 2;
  // format-only still 1.
  {
    unsigned Ops[] = {Haydn::MOVE32, Haydn::MOVE32, Haydn::MOVE32};
    EXPECT_EQ(computeExhaustiveProductResMII(Ops), 1u);
    EXPECT_EQ(RC::portLowerBoundResMII(
                  3 * HaydnMove32ClassMiRepeatedSrcGprReads,
                  3 * HaydnMove32ClassMiRepeatedSrcGprWrites),
              2u);
    EXPECT_EQ(RC::softExitIIFloor(
                  Ops, 3 * HaydnMove32ClassMiRepeatedSrcGprReads,
                  3 * HaydnMove32ClassMiRepeatedSrcGprWrites),
              2u);
  }
}

// SMS-HANDOFF ResourceCycle surface — metrics-only packability of
// qualification co-issue sets (no setDesc / BUNDLE invent / analyzeLoop).
// Peer: HaydnPortModelTest.PreRASMSHandoffPackabilityOracleSurface (pre-RA).
TEST(HaydnBundleTest, SMSHandoff_QualKernelPackabilityOracleSurface) {
  using namespace llvm::haydn::bundle;

  // Vacuous empty body.
  EXPECT_TRUE(
      HaydnResourceCycle::qualKernelFormsOneExactCycle(ArrayRef<unsigned>()));
  EXPECT_TRUE(
      HaydnResourceCycle::qualKernelCoissuePackable(ArrayRef<unsigned>()));
  EXPECT_TRUE(
      HaydnResourceCycle::qualKernelExactlyPackable(ArrayRef<unsigned>()));

  // Independent ALU triple from postmisched-exact-nosplit qualification —
  // one Full cycle under exact matching; zero greedy overestimate.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
    EXPECT_TRUE(HaydnResourceCycle::qualKernelFormsOneExactCycle(Ops));
    EXPECT_TRUE(HaydnResourceCycle::qualKernelCoissuePackable(Ops));
    EXPECT_TRUE(HaydnResourceCycle::qualKernelExactlyPackable(Ops));
    EXPECT_EQ(computeExhaustiveProductResMII(Ops), 1u);
    EXPECT_EQ(productResMIIOverestimate(Ops), 0);
    EXPECT_FALSE(productResMIIFailsQualification(Ops));
  }

  // Three-ready rematch triple (ADD32 + 2×ADD64) — preferred first-fit can
  // dead-end; exact cycle still packs. Metrics only: no setDesc claim.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    EXPECT_TRUE(HaydnResourceCycle::qualKernelFormsOneExactCycle(Ops));
    EXPECT_TRUE(HaydnResourceCycle::qualKernelCoissuePackable(Ops));
    EXPECT_TRUE(HaydnResourceCycle::qualKernelExactlyPackable(Ops));
    EXPECT_EQ(computeExhaustiveProductResMII(Ops), 1u);
    EXPECT_EQ(productResMIIOverestimate(Ops), 0);
  }

  // Two ST32: format-incompatible same cycle (both Slot0) → not one cycle;
  // body still exactly packable across two cycles with zero overestimate.
  {
    unsigned Ops[] = {Haydn::ST32, Haydn::ST32};
    EXPECT_FALSE(HaydnResourceCycle::qualKernelFormsOneExactCycle(Ops));
    EXPECT_FALSE(HaydnResourceCycle::qualKernelCoissuePackable(Ops));
    EXPECT_TRUE(HaydnResourceCycle::qualKernelExactlyPackable(Ops));
    EXPECT_EQ(computeExhaustiveProductResMII(Ops), 2u);
    EXPECT_EQ(productResMIIOverestimate(Ops), 0);
  }

  // Over-width coissue (4 ops) cannot form one cycle.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32, Haydn::AND32};
    EXPECT_FALSE(HaydnResourceCycle::qualKernelFormsOneExactCycle(Ops));
    EXPECT_FALSE(HaydnResourceCycle::qualKernelCoissuePackable(Ops));
    EXPECT_TRUE(HaydnResourceCycle::qualKernelExactlyPackable(Ops));
    EXPECT_GE(computeExhaustiveProductResMII(Ops), 2u);
  }

  // Greedy order-trap: overestimate fail-closes qualification; exact-pack
  // remains true (finite exhaustive cover).
  {
    unsigned Ops[] = {Haydn::ST32, Haydn::ST32, Haydn::ADD32, Haydn::ADD32,
                      Haydn::ADD32};
    EXPECT_TRUE(productResMIIFailsQualification(Ops));
    EXPECT_TRUE(HaydnResourceCycle::qualKernelExactlyPackable(Ops));
  }

  // N > MaxExhaustiveProductResMIIOps: exhaustive falls back to greedy.
  // Streaming SMS bodies (bkfir / dual-load MAC) land here — must remain
  // exactly packable (no false reject on the inexact oracle).
  {
    unsigned Big[MaxExhaustiveProductResMIIOps + 2];
    for (unsigned &O : Big)
      O = Haydn::ADD32;
    EXPECT_FALSE(productResMIIFailsQualification(Big));
    EXPECT_TRUE(HaydnResourceCycle::qualKernelExactlyPackable(Big));
    EXPECT_GE(computeExhaustiveProductResMII(Big), 1u);
    // Co-issue requires size ≤ ISSUE_SLOT_COUNT; large bodies never coissue.
    EXPECT_FALSE(HaydnResourceCycle::qualKernelCoissuePackable(Big));
  }
}

TEST(HaydnBundleTest, VF3_DescPortEstimateADD32Shape) {
  // Synthetic: three GPR register operands, one def → 2R1W (no dedup needed).
  // Real SMS MID path uses table-backed MCInstrDesc; this pins the classifier.
  HaydnCyclePortDemand D;
  haydnClassifyPortBankClassID(Haydn::GPR32RegClassID, /*IsDef=*/true, D);
  haydnClassifyPortBankClassID(Haydn::GPR32RegClassID, /*IsDef=*/false, D);
  haydnClassifyPortBankClassID(Haydn::GPR32RegClassID, /*IsDef=*/false, D);
  EXPECT_EQ(D.GPRReads, 2u);
  EXPECT_EQ(D.GPRWrites, 1u);
  EXPECT_EQ(D.DRReads, 0u);
  EXPECT_TRUE(D.fitsBudget());

  HaydnCyclePortDemand DR;
  haydnClassifyPortBankClassID(Haydn::DR64RegClassID, true, DR);
  haydnClassifyPortBankClassID(Haydn::DR64RegClassID, false, DR);
  EXPECT_EQ(DR.DRWrites, 1u);
  EXPECT_EQ(DR.DRReads, 1u);
}

TEST(HaydnBundleTest, VF3_DescPortEstimateEmptyMIDNoPorts) {
  // Unit-test style MID with only Opcode set (NumOperands=0) → zero ports.
  // Existing format-only MID tests remain valid.
  MCInstrDesc Desc{};
  Desc.Opcode = Haydn::ADD32;
  Desc.NumOperands = 0;
  Desc.NumDefs = 0;
  HaydnCyclePortDemand D = estimateHaydnPortsFromDesc(Desc);
  EXPECT_EQ(D.GPRReads, 0u);
  EXPECT_EQ(D.GPRWrites, 0u);

  HaydnResourceCycle RC;
  // Placement MID path with empty operands ≡ format-only (3×ADD32 pack).
  ASSERT_TRUE(RC.canReserveResources(&Desc));
  RC.reserveResources(&Desc);
  ASSERT_TRUE(RC.canReserveResources(&Desc));
  RC.reserveResources(&Desc);
  ASSERT_TRUE(RC.canReserveResources(&Desc));
  RC.reserveResources(&Desc);
  EXPECT_FALSE(RC.canReserveResources(&Desc));
  EXPECT_EQ(RC.getPortDemand().GPRWrites, 0u)
      << "empty MID must not invent port demand";
}

/// Sequential packing with explicit port demand — mirrors calculateResMIIDFA
/// MI path (count*Ports) where each open-cycle rejection bumps the cycle count.
static unsigned
sequentialPortAwareCycleCount(ArrayRef<std::pair<unsigned, HaydnCyclePortDemand>> Ops) {
  if (Ops.empty())
    return 0;
  unsigned Cycles = 0;
  HaydnResourceCycle RC;
  bool CycleOpen = false;
  for (const auto &Op : Ops) {
    if (!RC.canReserveByOpcodeWithPorts(Op.first, Op.second)) {
      RC.clearResources();
      ++Cycles;
      CycleOpen = false;
      if (!RC.canReserveByOpcodeWithPorts(Op.first, Op.second))
        return ~0u;
    }
    RC.reserveByOpcodeWithPorts(Op.first, Op.second);
    CycleOpen = true;
  }
  if (CycleOpen)
    ++Cycles;
  return Cycles;
}

TEST(HaydnBundleTest, VF3_PortAwareResMIITwoWritesNeedTwoCycles) {
  // Format-only packs three ADD32 in one cycle (slot cap). Port-aware packing
  // (SMS ResMII MI path / placement MID path) binds at GPR 2W: three 1W ops
  // need two modulo cycles — port-forced ResMII ≥ 2 while format oracle stays
  // at 1 (lit: sms-format-resmii-port-forced.mir). ResourceCycle must not
  // silently ignore pooled ports under Full.
  HaydnCyclePortDemand ALU1W; // one write, zero reads (isolates 2W pool)
  ALU1W.GPRWrites = 1;

  std::pair<unsigned, HaydnCyclePortDemand> ThreeWrites[] = {
      {Haydn::ADD32, ALU1W},
      {Haydn::ADD32, ALU1W},
      {Haydn::ADD32, ALU1W},
  };
  EXPECT_EQ(sequentialPortAwareCycleCount(ThreeWrites), 2u)
      << "3×1W must open a second cycle under HAYDN_GPR_WRITE_PORTS=2";

  // Format-only baseline still reports 1 cycle (slot-only, no ports).
  unsigned FormatOnly[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
  EXPECT_EQ(sequentialResourceCycleCount(FormatOnly), 1u);

  // Two writes fit one cycle; a third 1W+1R still needs cycle 2.
  HaydnCyclePortDemand ALU2R1W;
  ALU2R1W.GPRReads = 2;
  ALU2R1W.GPRWrites = 1;
  std::pair<unsigned, HaydnCyclePortDemand> TwoThenThird[] = {
      {Haydn::ADD32, ALU2R1W},
      {Haydn::ADD32, ALU2R1W},
      {Haydn::ADD32, ALU2R1W},
  };
  EXPECT_EQ(sequentialPortAwareCycleCount(TwoThenThird), 2u);
}

TEST(HaydnBundleTest, VF3_PortAwareResMIIReadCapAndAlone) {
  // 3R + 3R exceeds 4R → two cycles (read pool).
  HaydnCyclePortDemand R3;
  R3.GPRReads = 3;
  std::pair<unsigned, HaydnCyclePortDemand> TwoHeavyReads[] = {
      {Haydn::ADD32, R3},
      {Haydn::ADD32, R3},
  };
  EXPECT_EQ(sequentialPortAwareCycleCount(TwoHeavyReads), 2u);

  // ARCTAN alone: co-issue with ADD32 is illegal → two cycles.
  HaydnCyclePortDemand Zero;
  std::pair<unsigned, HaydnCyclePortDemand> AloneThenALU[] = {
      {Haydn::ARCTAN, Zero},
      {Haydn::ADD32, Zero},
  };
  EXPECT_EQ(sequentialPortAwareCycleCount(AloneThenALU), 2u);

  HaydnResourceCycle RC;
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::SIN_COS));
  RC.reserveByOpcode(Haydn::SIN_COS);
  EXPECT_TRUE(RC.hasAloneOp());
  EXPECT_FALSE(RC.canReserveByOpcode(Haydn::ADD32));
}

//===----------------------------------------------------------------------===//
// MOVE32-class MI-versus-descriptor SMS port / placement differential
//===----------------------------------------------------------------------===//
//
// REGRESSION TEST REBASE (2026-08-21). The owning layer unified the GPR
// port law to per-field charging in 0ad0d5d64088 (2026-08-18): every
// explicit GPR operand field reserves one read/write port — no same-register
// identity dedup. MOVE32 rd, rs, rs is therefore 2R1W on the MI path
// (countGPRPorts / countHaydnPortsFromMI / ResMII MI packing) AND on the
// descriptor path (estimateHaydnPortsFromDesc / ResourceCycle MID overload):
// copyPhysReg emits rs twice because the FmtALU32 encoding has two physical
// source fields (rs1/rs2), and the register file sees both read ports.
//
// The pre-rebase expectations here (MI dedup to 1R1W, descriptor overcount,
// descriptor saturating the 4R pool earlier) pinned a dedup the MI
// accounting never actually performed — even the pre-reshape countGPRPorts
// walked operands per-field. If a dedup law is ever reintroduced, the
// static_asserts in HaydnResourceCycle.cpp and the lit doc-pin
// sched-resource-truth-homes.s (HaydnMove32ClassMiRepeatedSrcGprReads = 2)
// fire alongside these tests — fix the owning layer, not the pins.

TEST(HaydnBundleTest, SMS_Move32ClassMiVsDescPortShapes) {
  EXPECT_FALSE(haydnMove32ClassDescOvercountsMiPorts());
  EXPECT_EQ(HaydnMove32ClassMiRepeatedSrcGprReads, 2u);
  EXPECT_EQ(HaydnMove32ClassMiRepeatedSrcGprWrites, 1u);
  EXPECT_EQ(HaydnMove32ClassDescShapeGprReads, 2u);
  EXPECT_EQ(HaydnMove32ClassDescShapeGprWrites, 1u);

  // Synthetic descriptor shape via classifier (1 def + 2 uses) ≡ constants.
  // (Table-backed estimateHaydnPortsFromDesc needs a real MCInstrDesc table
  // entry; the MF peer lives on HaydnBundleBoundaryTest. Live packing below
  // uses the same demand helpers ResourceCycle MID/MI overloads charge.)
  HaydnCyclePortDemand D;
  haydnClassifyPortBankClassID(Haydn::GPR32RegClassID, /*IsDef=*/true, D);
  haydnClassifyPortBankClassID(Haydn::GPR32RegClassID, /*IsDef=*/false, D);
  haydnClassifyPortBankClassID(Haydn::GPR32RegClassID, /*IsDef=*/false, D);
  EXPECT_EQ(D.GPRReads, HaydnMove32ClassDescShapeGprReads);
  EXPECT_EQ(D.GPRWrites, HaydnMove32ClassDescShapeGprWrites);
  EXPECT_EQ(D.GPRReads, haydnMove32ClassDescShapeDemand().GPRReads);
  EXPECT_EQ(D.GPRWrites, haydnMove32ClassDescShapeDemand().GPRWrites);

  // MI repeated-src helpers match constants (2R1W, per-field).
  EXPECT_EQ(haydnMove32ClassMiRepeatedSrcDemand().GPRReads,
            HaydnMove32ClassMiRepeatedSrcGprReads);
  EXPECT_EQ(haydnMove32ClassMiRepeatedSrcDemand().GPRWrites,
            HaydnMove32ClassMiRepeatedSrcGprWrites);
  EXPECT_EQ(haydnMove32ClassDescShapeDemand().GPRReads,
            haydnMove32ClassMiRepeatedSrcDemand().GPRReads)
      << "per-field law: MI and descriptor paths see the same read demand";
}

TEST(HaydnBundleTest, SMS_Move32ClassPlacementDescMoreConservativeThanMI) {
  // Rebased 2026-08-21 (see SMS_Move32ClassMiVsDescPortShapes header): both
  // shapes are 2R1W, so placement (MID) and MI paths leave identical
  // residual capacity. The conservative-differential probes now pin the
  // unified law: after one MOVE32-class reserve, 2R headroom remains under
  // HAYDN_GPR_READ_PORTS=4 — a 3R0W probe is rejected on BOTH paths.
  HaydnCyclePortDemand HeavyRead;
  HeavyRead.GPRReads = 3;

  {
    HaydnResourceCycle RC;
    ASSERT_TRUE(RC.canReserveByOpcodeWithPorts(
        Haydn::MOVE32, haydnMove32ClassMiRepeatedSrcDemand()));
    RC.reserveByOpcodeWithPorts(Haydn::MOVE32,
                                haydnMove32ClassMiRepeatedSrcDemand());
    EXPECT_EQ(RC.getPortDemand().GPRReads,
              HaydnMove32ClassMiRepeatedSrcGprReads);
    EXPECT_FALSE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, HeavyRead))
        << "per-field MOVE32 (2R) leaves only 2R; 3R probe must fail the "
           "MI path too under HAYDN_GPR_READ_PORTS=4";
  }
  {
    HaydnResourceCycle RC;
    ASSERT_TRUE(RC.canReserveByOpcodeWithPorts(
        Haydn::MOVE32, haydnMove32ClassDescShapeDemand()));
    RC.reserveByOpcodeWithPorts(Haydn::MOVE32,
                                haydnMove32ClassDescShapeDemand());
    EXPECT_EQ(RC.getPortDemand().GPRReads, HaydnMove32ClassDescShapeGprReads);
    EXPECT_FALSE(RC.canReserveByOpcodeWithPorts(Haydn::ADD32, HeavyRead))
        << "Desc-shape MOVE32 leaves only 2R; 3R probe must fail placement";
  }
}

TEST(HaydnBundleTest, SMS_Move32ClassSequentialPackingDescVsMI) {
  // Rebased 2026-08-21 (see SMS_Move32ClassMiVsDescPortShapes header): both
  // shapes are 2R1W per-field. N=2 fits one cycle (4R2W exactly at both
  // pools); N=3 opens a second cycle on both pools simultaneously.
  const HaydnCyclePortDemand Mi = haydnMove32ClassMiRepeatedSrcDemand();
  const HaydnCyclePortDemand Desc = haydnMove32ClassDescShapeDemand();
  std::pair<unsigned, HaydnCyclePortDemand> TwoMi[] = {
      {Haydn::MOVE32, Mi},
      {Haydn::MOVE32, Mi},
  };
  std::pair<unsigned, HaydnCyclePortDemand> TwoDesc[] = {
      {Haydn::MOVE32, Desc},
      {Haydn::MOVE32, Desc},
  };
  EXPECT_EQ(sequentialPortAwareCycleCount(TwoMi), 1u);
  EXPECT_EQ(sequentialPortAwareCycleCount(TwoDesc), 1u);

  // N=3: both pools blow together (6R > 4R and 3W > 2W) — the retired
  // saturates-earlier differential is never true under the per-field law.
  std::pair<unsigned, HaydnCyclePortDemand> ThreeMi[] = {
      {Haydn::MOVE32, Mi},
      {Haydn::MOVE32, Mi},
      {Haydn::MOVE32, Mi},
  };
  std::pair<unsigned, HaydnCyclePortDemand> ThreeDesc[] = {
      {Haydn::MOVE32, Desc},
      {Haydn::MOVE32, Desc},
      {Haydn::MOVE32, Desc},
  };
  EXPECT_EQ(sequentialPortAwareCycleCount(ThreeMi), 2u);
  EXPECT_EQ(sequentialPortAwareCycleCount(ThreeDesc), 2u);
  EXPECT_FALSE(haydnMove32ClassDescSaturatesReadPoolEarlier(3));
  EXPECT_FALSE(haydnMove32ClassDescSaturatesReadPoolEarlier(2));

  // Format-only three MOVE32 still pack in one Full cycle (slots, no ports).
  unsigned FormatOnly[] = {Haydn::MOVE32, Haydn::MOVE32, Haydn::MOVE32};
  EXPECT_EQ(sequentialResourceCycleCount(FormatOnly), 1u);
  EXPECT_TRUE(HaydnResourceCycle::qualKernelFormsOneExactCycle(FormatOnly));
  EXPECT_TRUE(HaydnResourceCycle::qualKernelExactlyPackable(FormatOnly));
}

TEST(HaydnBundleTest, VF3_DescClassifierSubclassIDs) {
  // Descriptor path must charge GPR subclasses the same as GPR32 (parity with
  // PortModel hasSubClassEq for vreg regclasses used pre-RA).
  HaydnCyclePortDemand D;
  haydnClassifyPortBankClassID(Haydn::GPR32LoRegClassID, /*IsDef=*/true, D);
  haydnClassifyPortBankClassID(Haydn::GPR32NoSPNoLRRegClassID, /*IsDef=*/false,
                               D);
  EXPECT_EQ(D.GPRWrites, 1u);
  EXPECT_EQ(D.GPRReads, 1u);
  EXPECT_TRUE(D.fitsBudget());

  // Unknown class IDs stay existential (no invented bank demand).
  HaydnCyclePortDemand U;
  haydnClassifyPortBankClassID(/*RegClassID=*/-1, true, U);
  haydnClassifyPortBankClassID(/*RegClassID=*/9999, false, U);
  EXPECT_EQ(U.GPRReads, 0u);
  EXPECT_EQ(U.GPRWrites, 0u);
}

//===----------------------------------------------------------------------===//
// SMS-RESMII — ResourceCycle DFA walk vs exhaustive format oracle
//===----------------------------------------------------------------------===//

TEST(HaydnBundleTest, VF3_SMSResMII_ResourceCycleEqualsExhaustiveOracle) {
  // sequentialResourceCycleCount mirrors calculateResMIIDFA packing through
  // HaydnResourceCycle (exact candidate set). Must match exhaustive oracle on
  // qualification multisets — zero SMS-RESMII overestimate.
  using namespace haydn::bundle;
  const unsigned Cases[][4] = {
      {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, Haydn::ADD32},
      {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64, 0},
      {Haydn::LD32, Haydn::LD32, Haydn::X2MULA32, 0},
      {Haydn::ST32, Haydn::ST32, 0, 0},
  };
  const unsigned Lens[] = {4, 3, 3, 2};
  for (unsigned C = 0; C < 4; ++C) {
    ArrayRef<unsigned> Ops(Cases[C], Lens[C]);
    const unsigned DFA = sequentialResourceCycleCount(Ops);
    const unsigned Greedy = computeProductResMII(Ops);
    const unsigned Exact = computeExhaustiveProductResMII(Ops);
    EXPECT_EQ(DFA, Greedy) << "DFA≡greedy case " << C;
    EXPECT_EQ(DFA, Exact) << "DFA≡exhaustive case " << C;
    EXPECT_EQ(productResMIIOverestimate(Ops), 0) << "case " << C;
  }
}

TEST(HaydnBundleTest, VF3_SMSResMII_PreferredCollapseOverestimateVsRC) {
  // ResourceCycle uses exact expand (not preferred freeze). Preferred
  // sequential overestimates on ADD32+2×ADD64; RC/DFA/exhaustive stay at 1.
  using namespace haydn::bundle;
  unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
  EXPECT_EQ(sequentialResourceCycleCount(Ops), 1u);
  EXPECT_EQ(computeExhaustiveProductResMII(Ops), 1u);
  EXPECT_GE(computePreferredProductResMII(Ops), 2u);
  EXPECT_GE(preferredProductResMIIOverestimate(Ops), 1);
  EXPECT_EQ(productResMIIOverestimate(Ops), 0);
}

//===----------------------------------------------------------------------===//
// — MultiSlot_Pseudo books issue/slots; true meta does not
//===----------------------------------------------------------------------===//

TEST(HaydnBundleTest, VF23_IsNoHazardMetaOnlyIMPLICIT_DEFAndKILL) {
  // AIE twin: only IMPLICIT_DEF/KILL. MultiSlot_Pseudo (ADD32_MSP) is NOT
  // meta — old HR isPseudo() would have exempted it (miscompile risk).
  EXPECT_TRUE(MachineBundle::isNoHazardMetaInstruction(
      TargetOpcode::IMPLICIT_DEF));
  EXPECT_TRUE(
      MachineBundle::isNoHazardMetaInstruction(TargetOpcode::KILL));
  EXPECT_FALSE(
      MachineBundle::isNoHazardMetaInstruction(TargetOpcode::BUNDLE));
  EXPECT_FALSE(MachineBundle::isNoHazardMetaInstruction(Haydn::ADD32_MSP));
  EXPECT_FALSE(MachineBundle::isNoHazardMetaInstruction(Haydn::ADD32));
  EXPECT_FALSE(MachineBundle::isNoHazardMetaInstruction(Haydn::ST32));
}

TEST(HaydnBundleTest, VF23_MultiSlotPseudoBooksSlotsTrueMetaDoesNot) {
  // ADD32_MSP peels to ADD32; occupancy fills residual indices from
  // Format E members (same surface as sparse ADD32). Three fill the
  // cycle; fourth rejects. IMPLICIT_DEF/KILL never consume OccupiedSlots.
  HaydnMCFormats Fmts;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  ASSERT_TRUE(hasPlacementAlternatives(Fmts, Haydn::ADD32_MSP));
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD32_MSP);
  ASSERT_NE(Alts, nullptr);
  ASSERT_EQ(Alts->size(), 3u);
  ASSERT_NE((*Alts)[0], 0u);
  ASSERT_NE((*Alts)[1], 0u);
  ASSERT_NE((*Alts)[2], 0u);
  EXPECT_TRUE(StringRef(MII.getName((*Alts)[0])).contains("_E2_"))
      << MII.getName((*Alts)[0]);
  EXPECT_TRUE(StringRef(MII.getName((*Alts)[1])).contains("_E3_"))
      << MII.getName((*Alts)[1]);
  EXPECT_TRUE(StringRef(MII.getName((*Alts)[2])).contains("_E3_"))
      << MII.getName((*Alts)[2]);

  Bundle<MCInst> B(&Fmts);
  MCInst Msp[4];
  for (int I = 0; I < 3; ++I) {
    Msp[I].setOpcode(Haydn::ADD32_MSP);
    ASSERT_TRUE(B.canAdd(Msp[I].getOpcode())) << "ADD32_MSP #" << I;
    B.add(&Msp[I]);
  }
  EXPECT_EQ(B.getOccupiedSlots(),
            SlotBits(Haydn::SLOT0) | Haydn::SLOT1 | Haydn::SLOT2);
  EXPECT_EQ(B.size(), 3u);
  Msp[3].setOpcode(Haydn::ADD32_MSP);
  EXPECT_FALSE(B.canAdd(Msp[3].getOpcode()))
      << "fourth MultiSlot_Pseudo must conflict once slots full";

  // True meta always canAdd and does not grow occupancy / Instrs.
  Bundle<MCInst> MetaB(&Fmts);
  MCInst Def, Kill;
  Def.setOpcode(TargetOpcode::IMPLICIT_DEF);
  Kill.setOpcode(TargetOpcode::KILL);
  ASSERT_TRUE(MetaB.canAdd(Def.getOpcode()));
  MetaB.add(&Def);
  ASSERT_TRUE(MetaB.canAdd(Kill.getOpcode()));
  MetaB.add(&Kill);
  EXPECT_EQ(MetaB.getOccupiedSlots(), 0u);
  EXPECT_EQ(MetaB.size(), 0u) << "meta goes to MetaInstrs, not Instrs";

  // ResourceCycle: MultiSlot books like ADD32; meta is no-op.
  HaydnResourceCycle RC;
  for (unsigned I = 0; I < 3; ++I) {
    ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD32_MSP)) << "MSP #" << I;
    RC.reserveByOpcode(Haydn::ADD32_MSP);
  }
  EXPECT_EQ(RC.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL));
  EXPECT_FALSE(RC.canReserveByOpcode(Haydn::ADD32_MSP));
  EXPECT_TRUE(RC.canReserveByOpcode(TargetOpcode::IMPLICIT_DEF));
  RC.reserveByOpcode(TargetOpcode::IMPLICIT_DEF);
  EXPECT_EQ(RC.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL))
      << "IMPLICIT_DEF must not free or consume slots";
  EXPECT_TRUE(RC.canReserveByOpcode(TargetOpcode::KILL));
  RC.reserveByOpcode(TargetOpcode::KILL);
  EXPECT_EQ(RC.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL));
}

TEST(HaydnBundleTest, VF23_MultiSlotPseudoExactTryAddS2First) {
  // HR commitPlacement / Bundle exact path: ADD32_MSP prefers S2→S1→S0.
  HaydnMCFormats Fmts;
  CycleCandidateSet C = makeProductCandidateSet();
  ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::ADD32_MSP));
  EXPECT_EQ(selectPreferredCandidate(C).OccupiedSlots, SlotBits(Haydn::SLOT2));
  ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::ADD32_MSP));
  ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::ADD32_MSP));
  EXPECT_EQ(selectPreferredCandidate(C).OccupiedSlots,
            SlotBits(Haydn::SLOT_ALL));
  EXPECT_FALSE(canExactTryAddProduct(C, Fmts, Haydn::ADD32_MSP));
  // True meta is not an alts-bearing logical — no placement tryAdd surface.
  EXPECT_FALSE(hasPlacementAlternatives(Fmts, TargetOpcode::IMPLICIT_DEF));
  EXPECT_FALSE(hasPlacementAlternatives(Fmts, TargetOpcode::KILL));
}

//===----------------------------------------------------------------------===//
// — PostRA pack-skip: meta/alts, not blanket isPseudo
//===----------------------------------------------------------------------===//
//
// leaveRegion reconstruction used MI.isPseudo() as a cycle-member skip, so
// MultiSlot_Pseudo vanished from cycle lists despite HR booking. Opcode-level
// twin of HaydnPostRASchedStrategy::isBundleSkippable (no MachineFunction).

TEST(HaydnBundleTest, VF23b_PackSkipMetaAndAltsNotBlanketPseudo) {
  HaydnMCFormats Fmts;
  // True meta always skippable.
  EXPECT_TRUE(MachineBundle::isBundlePackSkippableOpcode(
      TargetOpcode::IMPLICIT_DEF, /*IsPseudo=*/true, Fmts));
  EXPECT_TRUE(MachineBundle::isBundlePackSkippableOpcode(
      TargetOpcode::KILL, /*IsPseudo=*/true, Fmts));
  // MultiSlot_Pseudo is isPseudo=1 but alts-bearing — never skip.
  ASSERT_TRUE(hasPlacementAlternatives(Fmts, Haydn::ADD32_MSP));
  EXPECT_FALSE(MachineBundle::isBundlePackSkippableOpcode(
      Haydn::ADD32_MSP, /*IsPseudo=*/true, Fmts))
      << "ADD32_MSP must remain a cycle member for reconstruction/splice";
  // Sparse non-pseudo control also not skippable.
  ASSERT_TRUE(hasPlacementAlternatives(Fmts, Haydn::ADD32));
  EXPECT_FALSE(MachineBundle::isBundlePackSkippableOpcode(
      Haydn::ADD32, /*IsPseudo=*/false, Fmts));
  // No-alt expand residual stays skippable until residual expansion is
  // guaranteed before pack (LOADI32 has no PlacementAlternatives).
  EXPECT_FALSE(hasPlacementAlternatives(Fmts, Haydn::LOADI32));
  EXPECT_TRUE(MachineBundle::isBundlePackSkippableOpcode(
      Haydn::LOADI32, /*IsPseudo=*/true, Fmts))
      << "no-alt expand pseudo must stay skippable until residual expand";
}

//===----------------------------------------------------------------------===//
// Exhaustive ≤3 all-subset / all-perm: Bundle + ResourceCycle vs exact oracle
//===----------------------------------------------------------------------===//

namespace {

static constexpr unsigned kBundleOracleAlpha[] = {
    Haydn::ADD32, Haydn::ADD64, Haydn::ST32, Haydn::LD32, Haydn::X2MULA32,
};
static constexpr unsigned kBundleOracleAlphaN =
    sizeof(kBundleOracleAlpha) / sizeof(kBundleOracleAlpha[0]);

/// Sequential Bundle canAdd/add packs the full list into one cycle.
static bool bundlePacksSequence(HaydnMCFormats &Fmts, ArrayRef<unsigned> Seq) {
  Bundle<MCInst> B(&Fmts);
  SmallVector<MCInst, 3> Storage;
  Storage.resize(Seq.size());
  for (unsigned I = 0, E = Seq.size(); I != E; ++I) {
    Storage[I].setOpcode(Seq[I]);
    if (!B.canAdd(Storage[I].getOpcode()))
      return false;
    B.add(&Storage[I]);
  }
  return true;
}

/// ResourceCycle reserves the full list without opening a second cycle.
static bool resourceCyclePacksOne(ArrayRef<unsigned> Seq) {
  HaydnResourceCycle RC;
  for (unsigned Opc : Seq) {
    if (!RC.canReserveByOpcode(Opc))
      return false;
    RC.reserveByOpcode(Opc);
  }
  return true;
}

} // namespace

TEST(HaydnBundleTest, VF24_ExhaustiveLe3BundleAndResourceCycleOracle) {
  // Bundle encode path and SMS ResourceCycle must agree with the pure exact
  // sequence oracle on every ordered ≤3 tuple from the alphabet. Occupancy
  // after a successful pack matches selectPreferredCandidate.
  HaydnMCFormats Fmts;
  unsigned Checked = 0;

  auto CheckSeq = [&](ArrayRef<unsigned> Seq) {
    ++Checked;
    const bool Exact = exactCanPackProductSequence(Fmts, Seq);
    const bool BundleOk = bundlePacksSequence(Fmts, Seq);
    const bool RCOk = resourceCyclePacksOne(Seq);
    EXPECT_EQ(BundleOk, Exact) << "Bundle canAdd drift";
    EXPECT_EQ(RCOk, Exact) << "ResourceCycle reserve drift";

    if (Exact) {
      // Bundle occupancy ≡ preferred exact candidate.
      Bundle<MCInst> B(&Fmts);
      SmallVector<MCInst, 3> Storage(Seq.size());
      CycleCandidateSet C = makeProductCandidateSet();
      for (unsigned I = 0, E = Seq.size(); I != E; ++I) {
        Storage[I].setOpcode(Seq[I]);
        ASSERT_TRUE(B.canAdd(Storage[I].getOpcode()));
        B.add(&Storage[I]);
        ASSERT_TRUE(exactTryAddProduct(C, Fmts, Seq[I]));
      }
      EXPECT_EQ(B.getOccupiedSlots(),
                selectPreferredCandidate(C).OccupiedSlots);
      EXPECT_EQ(B.size(), Seq.size());
      // One-cycle sequential ResourceCycle count is 1.
      EXPECT_EQ(sequentialResourceCycleCount(Seq), 1u);
      EXPECT_EQ(computeExhaustiveProductResMII(Seq), 1u);
    } else if (!Seq.empty()) {
      // Full list does not co-issue: sequential ResourceCycle needs ≥2 when
      // each singleton is placeable.
      EXPECT_GE(sequentialResourceCycleCount(Seq), 2u);
    }
  };

  for (unsigned A : kBundleOracleAlpha)
    CheckSeq(ArrayRef<unsigned>(&A, 1));
  for (unsigned A : kBundleOracleAlpha)
    for (unsigned B : kBundleOracleAlpha) {
      unsigned Seq[2] = {A, B};
      CheckSeq(Seq);
    }
  for (unsigned A : kBundleOracleAlpha)
    for (unsigned B : kBundleOracleAlpha)
      for (unsigned C : kBundleOracleAlpha) {
        unsigned Seq[3] = {A, B, C};
        CheckSeq(Seq);
      }

  EXPECT_EQ(Checked, kBundleOracleAlphaN +
                         kBundleOracleAlphaN * kBundleOracleAlphaN +
                         kBundleOracleAlphaN * kBundleOracleAlphaN *
                             kBundleOracleAlphaN);
}

TEST(HaydnBundleTest, VF24_ClosestLegalIllegalBundlePins) {
  HaydnMCFormats Fmts;

  // Closest pair around S0 exclusivity.
  {
    unsigned Legal[] = {Haydn::ST32, Haydn::ADD64};
    unsigned Illegal[] = {Haydn::ST32, Haydn::ST32};
    EXPECT_TRUE(bundlePacksSequence(Fmts, Legal));
    EXPECT_FALSE(bundlePacksSequence(Fmts, Illegal));
    EXPECT_TRUE(resourceCyclePacksOne(Legal));
    EXPECT_FALSE(resourceCyclePacksOne(Illegal));
  }

  // Issue-width: three pack, fourth rejected on both Bundle and ResourceCycle.
  {
    unsigned Three[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_TRUE(bundlePacksSequence(Fmts, Three));
    EXPECT_TRUE(resourceCyclePacksOne(Three));
    Bundle<MCInst> B(&Fmts);
    MCInst Ops[4];
    for (int I = 0; I < 3; ++I) {
      Ops[I].setOpcode(Haydn::ADD32);
      ASSERT_TRUE(B.canAdd(Ops[I].getOpcode()));
      B.add(&Ops[I]);
    }
    Ops[3].setOpcode(Haydn::ADD32);
    EXPECT_FALSE(B.canAdd(Ops[3].getOpcode()));
    HaydnResourceCycle RC;
    for (unsigned I = 0; I < 3; ++I) {
      ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD32));
      RC.reserveByOpcode(Haydn::ADD32);
    }
    EXPECT_FALSE(RC.canReserveByOpcode(Haydn::ADD32));
  }

  // Rematch: Bundle sequential accepts ADD32+2×ADD64 (exact path).
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    EXPECT_TRUE(bundlePacksSequence(Fmts, Ops));
    EXPECT_TRUE(resourceCyclePacksOne(Ops));
    Bundle<MCInst> B(&Fmts);
    SmallVector<MCInst, 3> Storage(3);
    for (unsigned I = 0; I < 3; ++I) {
      Storage[I].setOpcode(Ops[I]);
      ASSERT_TRUE(B.canAdd(Storage[I].getOpcode()));
      B.add(&Storage[I]);
    }
    EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL));
  }

  // MultiSlot_Pseudo fills like ADD32; true meta does not consume slots.
  {
    unsigned Msp[] = {Haydn::ADD32_MSP, Haydn::ADD32_MSP, Haydn::ADD32_MSP};
    EXPECT_TRUE(bundlePacksSequence(Fmts, Msp));
    EXPECT_TRUE(resourceCyclePacksOne(Msp));
    EXPECT_FALSE(MachineBundle::isNoHazardMetaInstruction(Haydn::ADD32_MSP));
  }

  // Preferred order pin on Bundle: first multi-slot lands S2.
  {
    Bundle<MCInst> B(&Fmts);
    MCInst A;
    A.setOpcode(Haydn::ADD32);
    ASSERT_TRUE(B.canAdd(A.getOpcode()));
    B.add(&A);
    EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT2));
  }
}

//===----------------------------------------------------------------------===//
// Format-acceptance differential — live ResourceCycle ≡ pure exact ≡ HR peer
// (plan §8.4 #7 SMS surface)
//===----------------------------------------------------------------------===//
//
// Descriptor-derived per-cycle format legality is pure exactTryAddProduct.
// Live canReserveByOpcode/reserveByOpcode must agree with pure exact packing
// and preferred OccupiedSlots / FieldSlots order. Format is opcode-keyed
// (MI ≡ desc); MOVE32-class port demand (per-field 2R1W, both paths) is
// orthogonal to format legality (SMS-PORT). Sibling
// pre-RA owns list-sched pure-exact polarity; live post-RA HR Emit peer is
// HaydnHazardRecognizerTest.SMS_FormatAcceptance_LiveHRMatchesResourceCycle.
// Metrics-only — no setDesc / no durable BUNDLE invent.

TEST(HaydnBundleTest, SMS_FormatAcceptanceDifferentialPins) {
  EXPECT_TRUE(HaydnResourceCycle::formatAcceptanceDifferentialPins());
}

TEST(HaydnBundleTest, SMS_FormatAcceptance_LiveRCMatchesPureExact) {
  using RC = HaydnResourceCycle;
  unsigned ThreeADD[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
  unsigned TwoST[] = {Haydn::ST32, Haydn::ST32};
  unsigned Rematch[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
  unsigned Coissue[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
  unsigned DualLoadMac[] = {Haydn::LD32, Haydn::LD32, Haydn::X2MULA32};
  unsigned AloneThenALU[] = {Haydn::ARCTAN, Haydn::ADD32};
  unsigned FullPlusOne[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32,
                            Haydn::ADD32};

  // Field-format sequences: live RC ≡ pure exact (alone-op is separate class-1).
  auto expectMatch = [](ArrayRef<unsigned> Ops) {
    EXPECT_TRUE(RC::formatAcceptanceMatchesPureExact(Ops))
        << "live RC must match pure exact polarity";
  };
  expectMatch(ThreeADD);
  expectMatch(TwoST);
  expectMatch(Rematch);
  expectMatch(Coissue);
  expectMatch(DualLoadMac);
  expectMatch(FullPlusOne);

  EXPECT_TRUE(RC::formatCanPackSequence(ThreeADD));
  EXPECT_TRUE(RC::formatPureExactCanPackSequence(ThreeADD));
  EXPECT_TRUE(RC::formatPreferredStateAgreesWithPureExact(ThreeADD));
  EXPECT_EQ(RC::formatSequentialCycleCount(ThreeADD), 1u);
  EXPECT_EQ(RC::formatSequentialCycleCount(ThreeADD),
            sequentialResourceCycleCount(ThreeADD));

  EXPECT_FALSE(RC::formatCanPackSequence(TwoST));
  EXPECT_FALSE(RC::formatPureExactCanPackSequence(TwoST));
  EXPECT_EQ(RC::formatSequentialCycleCount(TwoST), 2u);

  EXPECT_TRUE(RC::formatCanPackSequence(Rematch));
  EXPECT_TRUE(RC::formatPreferredStateAgreesWithPureExact(Rematch));
  // Preferred rematch: ADD32 on S0 after both ADD64 join.
  {
    HaydnResourceCycle Live;
    for (unsigned Opc : Rematch) {
      ASSERT_TRUE(Live.canReserveByOpcode(Opc));
      Live.reserveByOpcode(Opc);
    }
    EXPECT_EQ(Live.getMemberCount(), 3u);
    EXPECT_EQ(Live.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL));
    EXPECT_EQ(Live.getCycleState().Members[0].LogicalOpcode, Haydn::ADD32);
    EXPECT_EQ(Live.getCycleState().Members[0].FieldSlots,
              SlotBits(Haydn::SLOT0));
  }

  EXPECT_TRUE(RC::formatCanPackSequence(Coissue));
  EXPECT_TRUE(RC::formatCanPackSequence(DualLoadMac));

  // Alone-op class-1 (RC/HR): co-issue reject; pure field tryAdd may still
  // accept ARCTAN+ADD32 — not a field-format differential failure.
  EXPECT_FALSE(RC::formatCanPackSequence(AloneThenALU));
  EXPECT_EQ(RC::formatSequentialCycleCount(AloneThenALU), 2u);
  EXPECT_FALSE(RC::formatAcceptanceMatchesPureExact(AloneThenALU))
      << "alone capacity is RC/HR, not pure field exactTryAdd";

  // Full cycle + one more: sequential count 2; one-cycle pack fails.
  EXPECT_FALSE(RC::formatCanPackSequence(FullPlusOne));
  EXPECT_EQ(RC::formatSequentialCycleCount(FullPlusOne), 2u);
  EXPECT_EQ(RC::formatSequentialCycleCount(FullPlusOne),
            sequentialResourceCycleCount(FullPlusOne));

  // MI ≡ desc for same opcodes (format opcode-keyed).
  EXPECT_TRUE(RC::formatAcceptanceAgrees(Rematch, Rematch));
  EXPECT_TRUE(RC::formatAcceptanceAgrees(TwoST, TwoST));
  // Different multisets may disagree.
  EXPECT_FALSE(RC::formatAcceptanceAgrees(ThreeADD, TwoST));
}

TEST(HaydnBundleTest, SMS_FormatAcceptance_MIDPathAgreesWithOpcode) {
  // SMS ResourceManager primary is MCInstrDesc: rematch triple under MID
  // ports must still rematch (descriptor ports do not bind this shape).
  HaydnResourceCycle RCmid;
  MCInstrDesc D32{}, D64{}, Dst{};
  D32.Opcode = Haydn::ADD32;
  D64.Opcode = Haydn::ADD64;
  Dst.Opcode = Haydn::ST32;

  ASSERT_TRUE(RCmid.canReserveResources(&D32));
  RCmid.reserveResources(&D32);
  ASSERT_TRUE(RCmid.canReserveResources(&D64));
  RCmid.reserveResources(&D64);
  ASSERT_TRUE(RCmid.canReserveResources(&D64))
      << "MID path must rematch like canReserveByOpcode";
  RCmid.reserveResources(&D64);
  EXPECT_EQ(RCmid.getMemberCount(), 3u);
  EXPECT_EQ(RCmid.getCycleState().Members[0].FieldSlots,
            SlotBits(Haydn::SLOT0));

  // Two ST32: second rejects after first MID reserve (Slot0 exclusivity).
  HaydnResourceCycle RCst;
  ASSERT_TRUE(RCst.canReserveResources(&Dst));
  RCst.reserveResources(&Dst);
  EXPECT_FALSE(RCst.canReserveResources(&Dst));
  EXPECT_FALSE(HaydnResourceCycle::formatCanPackSequence(
      ArrayRef<unsigned>({Haydn::ST32, Haydn::ST32})));
}

} // namespace

TEST(HaydnBundleTest, PostRAMultiStageHostPins) {
  EXPECT_FALSE(HaydnMultiStageSMS::productDefaultEnabled());
  EXPECT_FALSE(EnableHaydnMultiStageSMS);
  EXPECT_EQ(HaydnMultiStageSMS::preflightSeatNames().size(), 8u);
  EXPECT_EQ(HaydnMultiStageSMS::journalSeatNames().size(), 7u);
  EXPECT_STREQ(HaydnMultiStageSMS::preflightSeatNames()[0], "PF-CFG");
  EXPECT_STREQ(HaydnMultiStageSMS::journalSeatNames()[0], "JM-ALLOC");
  EXPECT_STREQ(HaydnMultiStageSMS::journalSeatNames()[4], "JM-LIVE");
  EXPECT_STREQ(HaydnMultiStageSMS::journalSeatNames()[5], "JM-ALT");
  EXPECT_STREQ(HaydnMultiStageSMS::journalSeatNames()[6], "JM-META");
  bool IsPF = false;
  unsigned Idx = 99;
  EXPECT_TRUE(parseHaydnMultiStageForceFailSeat("PF-CFG", IsPF, Idx));
  EXPECT_TRUE(IsPF);
  EXPECT_EQ(Idx, 0u);
  HaydnMultiStageNodeInfo N;
  N.Cycle = 5;
  N.update(3);
  EXPECT_EQ(N.ModuloCycle, 2);
  EXPECT_EQ(N.Stage, 1);
}
