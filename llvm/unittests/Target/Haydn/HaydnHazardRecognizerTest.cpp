//===- HaydnHazardRecognizerTest.cpp - resource-model tests ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the Haydn post-RA hazard-recognizer RESOURCE MODEL — the
// `HaydnFuncUnitWrapper::conflict` predicate and CycleState tryAdd
// placement authority (AIEHazardRecognizer.cpp:174-214 alt try).
// Pins slot exclusivity, 3-issue cap, GPR 4R2W, DR64 7R3W, AR 2R2W, and
// pure product tryAdd S2→S1→S0 field order the HR commits on Emit.
//
// HaydnFuncUnitWrapper is pure data (no MachineInstr/MachineFunction needed).
// Slot bit indices: SLOT0=0, SLOT1=1, SLOT2=2 (itinerary FuncUnits).
//
//===----------------------------------------------------------------------===//

#include "HaydnBundleFormatSolver.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnPackLegality.h"
#include "HaydnPlacementAlternative.h"
#include "HaydnPortModel.h"
#include "HaydnStaticBitSet.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "gtest/gtest.h"

// Opcode enums come via HaydnPortModel → HaydnMCTargetDesc (GET_INSTRINFO_ENUM).

using namespace llvm;

namespace {

using SlotSet = StaticBitSet<HAYDN_NUM_FU_BITS>;

/// Single-slot set: {slot N} (Required.count() == 1).
static SlotSet singleSlot(unsigned N) { return SlotSet(static_cast<int>(N)); }

/// Multi-slot set: {S0, S1, S2} (the Slot012_ALU "any-slot" footprint).
static SlotSet allSlots() {
  return singleSlot(0) | singleSlot(1) | singleSlot(2);
}

/// A single-issue wrapper occupying slot \p N with no register-port demand.
static HaydnFuncUnitWrapper singleIssueInSlot(unsigned N) {
  HaydnFuncUnitWrapper W(singleSlot(N));
  W.setIssueCountOne();
  return W;
}

TEST(HaydnHazardRecognizerTest, SlotExclusivitySameSingleSlotConflicts) {
  // Two single-slot instrs needing the SAME exclusive slot conflict.
  EXPECT_TRUE(singleIssueInSlot(0).conflict(singleIssueInSlot(0)));
  EXPECT_TRUE(singleIssueInSlot(1).conflict(singleIssueInSlot(1)));
  EXPECT_TRUE(singleIssueInSlot(2).conflict(singleIssueInSlot(2)));
}

TEST(HaydnHazardRecognizerTest, DistinctSingleSlotsDoNotConflict) {
  // S0 + S1, S1 + S2, S0 + S2 — distinct exclusive slots, no conflict.
  EXPECT_FALSE(singleIssueInSlot(0).conflict(singleIssueInSlot(1)));
  EXPECT_FALSE(singleIssueInSlot(1).conflict(singleIssueInSlot(2)));
  EXPECT_FALSE(singleIssueInSlot(0).conflict(singleIssueInSlot(2)));
}

TEST(HaydnHazardRecognizerTest, MultiSlotNeverSlotConflicts) {
  // A multi-slot (Slot012_ALU) footprint never slot-conflicts with a single
  // slot — it can always find a free slot. This is the L171 rule: rejecting
  // a multi-slot candidate against a single-slot cycle was the IPC regression.
  HaydnFuncUnitWrapper Multi(allSlots());
  Multi.setIssueCountOne();
  EXPECT_FALSE(Multi.conflict(singleIssueInSlot(0)));
  EXPECT_FALSE(singleIssueInSlot(1).conflict(Multi));
  // Two multi-slot footprints also do not slot-conflict (issue cap governs).
  EXPECT_FALSE(Multi.conflict(Multi));
}

TEST(HaydnHazardRecognizerTest, IssueCountCapThree) {
  // A cycle that has already issued 3 instrs (IssueCount=3) rejects a 4th.
  // Build IssueCount=3 by unioning three single-issue wrappers.
  HaydnFuncUnitWrapper Cycle(singleSlot(0));
  HaydnFuncUnitWrapper A(singleSlot(0));
  A.setIssueCountOne();
  HaydnFuncUnitWrapper B(singleSlot(1));
  B.setIssueCountOne();
  HaydnFuncUnitWrapper C(singleSlot(2));
  C.setIssueCountOne();
  Cycle |= A;
  Cycle |= B;
  Cycle |= C; // Cycle.IssueCount == 3, slots {S0,S1,S2}.
  // Candidate single-issue in any slot: 3 + 1 > 3 -> conflict.
  EXPECT_TRUE(Cycle.conflict(singleIssueInSlot(0)));
  // Two single-issue instrs (1 + 1 = 2 <= 3) do not hit the cap by themselves.
  EXPECT_FALSE(singleIssueInSlot(0).conflict(singleIssueInSlot(1)));
}

TEST(HaydnHazardRecognizerTest, GPRPortBudget4R2W) {
  // GPR 4R2W: combined reads > 4 OR combined writes > 2 conflicts.
  HaydnFuncUnitWrapper Reader(singleSlot(0));
  Reader.setGPRPorts(HAYDN_GPR_READ_PORTS, 0); // 4R 0W
  HaydnFuncUnitWrapper OneMoreRead(singleSlot(1));
  OneMoreRead.setGPRPorts(1, 0);
  EXPECT_TRUE(Reader.conflict(OneMoreRead)); // 4 + 1 = 5 > 4

  HaydnFuncUnitWrapper Writer(singleSlot(0));
  Writer.setGPRPorts(0, HAYDN_GPR_WRITE_PORTS); // 0R 2W
  HaydnFuncUnitWrapper OneMoreWrite(singleSlot(1));
  OneMoreWrite.setGPRPorts(0, 1);
  EXPECT_TRUE(Writer.conflict(OneMoreWrite)); // 2 + 1 = 3 > 2

  // At-budget does NOT conflict: 4R+0R = 4, 2W+0W = 2 (both == cap, not > cap).
  HaydnFuncUnitWrapper AtRead(singleSlot(0));
  AtRead.setGPRPorts(HAYDN_GPR_READ_PORTS, 0);
  HaydnFuncUnitWrapper NoRead(singleSlot(1));
  NoRead.setGPRPorts(0, 0);
  EXPECT_FALSE(AtRead.conflict(NoRead));
}

TEST(HaydnHazardRecognizerTest, DR64PortBudget7R3W) {
  // DR64 7R3W: combined reads > 7 OR combined writes > 3 conflicts.
  HaydnFuncUnitWrapper DrReader(singleSlot(0));
  DrReader.setDRPorts(HAYDN_DR_READ_PORTS, 0); // 7R
  HaydnFuncUnitWrapper OneMore(singleSlot(1));
  OneMore.setDRPorts(1, 0);
  EXPECT_TRUE(DrReader.conflict(OneMore)); // 7 + 1 = 8 > 7

  HaydnFuncUnitWrapper DrWriter(singleSlot(0));
  DrWriter.setDRPorts(0, HAYDN_DR_WRITE_PORTS); // 3W
  HaydnFuncUnitWrapper OneMoreW(singleSlot(1));
  OneMoreW.setDRPorts(0, 1);
  EXPECT_TRUE(DrWriter.conflict(OneMoreW)); // 3 + 1 = 4 > 3
}

TEST(HaydnHazardRecognizerTest, ARPortBudget2R2W) {
  // AR 2R2W (forward-compat; AR demand is 0 today): > 2 reads/writes conflicts.
  HaydnFuncUnitWrapper ArReader(singleSlot(0));
  ArReader.setARPorts(HAYDN_AR_READ_PORTS, 0); // 2R
  HaydnFuncUnitWrapper OneMore(singleSlot(1));
  OneMore.setARPorts(1, 0);
  EXPECT_TRUE(ArReader.conflict(OneMore)); // 2 + 1 = 3 > 2
}

TEST(HaydnHazardRecognizerTest, EmptyAndBlocked) {
  // An empty cycle accepts anything (no conflict).
  HaydnFuncUnitWrapper Empty;
  EXPECT_TRUE(Empty.isEmpty());
  EXPECT_FALSE(Empty.conflict(singleIssueInSlot(0)));

  // A blocked cycle rejects everything.
  HaydnFuncUnitWrapper Blocked;
  Blocked.blockResources();
  EXPECT_TRUE(Blocked.conflict(singleIssueInSlot(0)));
  EXPECT_TRUE(Blocked.conflict(Empty) || Empty.conflict(Blocked));
}

//===----------------------------------------------------------------------===//
// Pack-legality oracle pin (HaydnPackLegality.h) — pure resource half.
// MI-level R0 WAW / SFR / locked-DSP are covered by postmisched MIR lits.
//===----------------------------------------------------------------------===//

TEST(HaydnPackLegalityTest, ResourcesConflictMatchesWrapper) {
  using namespace llvm::haydn::pack;
  EXPECT_TRUE(resourcesConflict(singleIssueInSlot(0), singleIssueInSlot(0)));
  EXPECT_FALSE(resourcesConflict(singleIssueInSlot(0), singleIssueInSlot(1)));
}

TEST(HaydnPackLegalityTest, MaxIssueCapIsThree) {
  EXPECT_EQ(llvm::haydn::pack::MaxIssuePerCycle, 3u);
}

TEST(HaydnPackLegalityTest, ThreeSlotFillThenReject) {
  // Exhaustive-ish: fill S0+S1+S2 then a fourth single-issue conflicts.
  HaydnFuncUnitWrapper Cycle;
  for (unsigned S = 0; S < 3; ++S) {
    HaydnFuncUnitWrapper I = singleIssueInSlot(S);
    EXPECT_FALSE(Cycle.conflict(I)) << "slot " << S;
    Cycle |= I;
  }
  EXPECT_EQ(Cycle.getIssueCount(), 3u);
  EXPECT_TRUE(Cycle.conflict(singleIssueInSlot(0)));
}

//===----------------------------------------------------------------------===//
// Extended resource-model coverage — schedule half of pack-legality dual
// authority. MI-level alone-in-bundle / WAW / RAW stay in postmisched MIR lits
// (need MachineInstr + scoreboard). Pure wrappers pin ports/issue/slots here.
//===----------------------------------------------------------------------===//

TEST(HaydnHazardRecognizerTest, UnionPreservesDistinctSlots) {
  // Cycle |= single slots accumulates issue and occupancy without false
  // conflict until the 4th issue or same-slot collision.
  HaydnFuncUnitWrapper Cycle;
  Cycle |= singleIssueInSlot(0);
  Cycle |= singleIssueInSlot(1);
  EXPECT_EQ(Cycle.getIssueCount(), 2u);
  EXPECT_FALSE(Cycle.conflict(singleIssueInSlot(2)));
  Cycle |= singleIssueInSlot(2);
  EXPECT_EQ(Cycle.getIssueCount(), 3u);
  EXPECT_TRUE(Cycle.conflict(singleIssueInSlot(0)));
}

TEST(HaydnHazardRecognizerTest, SameSlotConflictIndependentOfPorts) {
  // Slot exclusivity is orthogonal to port budget: two S0 ops conflict even
  // with zero register-port demand (encode/schedule dual authority agrees).
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  HaydnFuncUnitWrapper B = singleIssueInSlot(0);
  A.setGPRPorts(0, 0);
  B.setGPRPorts(0, 0);
  EXPECT_TRUE(A.conflict(B));
}

TEST(HaydnHazardRecognizerTest, PortConflictWithDistinctSlots) {
  // Distinct slots still conflict when GPR write ports overflow (2W cap).
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  A.setGPRPorts(0, 2);
  HaydnFuncUnitWrapper B = singleIssueInSlot(1);
  B.setGPRPorts(0, 1);
  EXPECT_TRUE(A.conflict(B));
}

TEST(HaydnHazardRecognizerTest, CombinedReadWriteAtBudgetOk) {
  // 4R2W at budget with a zero-port companion on another slot is legal.
  HaydnFuncUnitWrapper Heavy = singleIssueInSlot(0);
  Heavy.setGPRPorts(HAYDN_GPR_READ_PORTS, HAYDN_GPR_WRITE_PORTS);
  HaydnFuncUnitWrapper Light = singleIssueInSlot(1);
  Light.setGPRPorts(0, 0);
  EXPECT_FALSE(Heavy.conflict(Light));
}

TEST(HaydnHazardRecognizerTest, DRAndGPRPortBudgetsIndependent) {
  // Exhausting GPR does not imply DR conflict when DR demand is zero.
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  A.setGPRPorts(HAYDN_GPR_READ_PORTS, 0);
  A.setDRPorts(0, 0);
  HaydnFuncUnitWrapper B = singleIssueInSlot(1);
  B.setGPRPorts(0, 0);
  B.setDRPorts(HAYDN_DR_READ_PORTS, 0);
  EXPECT_FALSE(A.conflict(B));

  // Two ops that each take half of DR read budget co-issue; +1 overflows.
  HaydnFuncUnitWrapper D0 = singleIssueInSlot(0);
  D0.setDRPorts(4, 0);
  HaydnFuncUnitWrapper D1 = singleIssueInSlot(1);
  D1.setDRPorts(3, 0);
  EXPECT_FALSE(D0.conflict(D1)); // 4+3=7 at budget
  HaydnFuncUnitWrapper D2 = singleIssueInSlot(2);
  D2.setDRPorts(1, 0);
  HaydnFuncUnitWrapper Combined = D0;
  Combined |= D1;
  EXPECT_TRUE(Combined.conflict(D2)); // 7+1 > 7
}

TEST(HaydnPackLegalityTest, ResourcesConflictIsSymmetric) {
  using namespace llvm::haydn::pack;
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  HaydnFuncUnitWrapper B = singleIssueInSlot(0);
  EXPECT_EQ(resourcesConflict(A, B), resourcesConflict(B, A));
  HaydnFuncUnitWrapper C = singleIssueInSlot(1);
  EXPECT_EQ(resourcesConflict(A, C), resourcesConflict(C, A));
  EXPECT_FALSE(resourcesConflict(A, C));
}

TEST(HaydnPackLegalityTest, ProductRulesDocumentedInHeader) {
  // Pin constants that encode product law so a silent edit trips CI.
  using namespace llvm::haydn::pack;
  EXPECT_EQ(MaxIssuePerCycle, 3u);
  EXPECT_EQ(HAYDN_GPR_READ_PORTS, 4u);
  EXPECT_EQ(HAYDN_GPR_WRITE_PORTS, 2u);
  EXPECT_EQ(HAYDN_DR_READ_PORTS, 7u);
  EXPECT_EQ(HAYDN_DR_WRITE_PORTS, 3u);
  EXPECT_EQ(HAYDN_AR_READ_PORTS, 2u);
  EXPECT_EQ(HAYDN_AR_WRITE_PORTS, 2u);
  // Rule 4 (ARCTAN/SIN_COS alone-in-bundle, no multi-cycle lock) is owned by
  // HaydnHazardRecognizer::isLockedSlotDspOp — covered by postmisched MIR lits
  // and PackLegality header contract text, not duplicated as a second opcode
  // table here.
}

//===----------------------------------------------------------------------===//
// Extended port / issue matrix
//===----------------------------------------------------------------------===//

TEST(HaydnHazardRecognizerTest, TwoIssueWithPartialPortsOk) {
  // Dual-issue with 2R1W each stays under 4R2W.
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  A.setGPRPorts(2, 1);
  HaydnFuncUnitWrapper B = singleIssueInSlot(1);
  B.setGPRPorts(2, 1);
  EXPECT_FALSE(A.conflict(B));
}

TEST(HaydnHazardRecognizerTest, TripleIssueAtGPRReadBudget) {
  // 2+1+1 = 4 reads at budget with distinct slots.
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  A.setGPRPorts(2, 0);
  HaydnFuncUnitWrapper B = singleIssueInSlot(1);
  B.setGPRPorts(1, 0);
  HaydnFuncUnitWrapper C = singleIssueInSlot(2);
  C.setGPRPorts(1, 0);
  HaydnFuncUnitWrapper Cycle = A;
  Cycle |= B;
  EXPECT_FALSE(Cycle.conflict(C));
  Cycle |= C;
  EXPECT_EQ(Cycle.getIssueCount(), 3u);
  // +1 read overflows.
  HaydnFuncUnitWrapper Extra = singleIssueInSlot(0);
  Extra.setGPRPorts(1, 0);
  EXPECT_TRUE(Cycle.conflict(Extra));
}

TEST(HaydnHazardRecognizerTest, DRWriteBudgetTripleIssue) {
  // 1W each on three slots = 3W at DR budget.
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  A.setDRPorts(0, 1);
  HaydnFuncUnitWrapper B = singleIssueInSlot(1);
  B.setDRPorts(0, 1);
  HaydnFuncUnitWrapper C = singleIssueInSlot(2);
  C.setDRPorts(0, 1);
  HaydnFuncUnitWrapper Cycle = A;
  Cycle |= B;
  EXPECT_FALSE(Cycle.conflict(C));
  Cycle |= C;
  HaydnFuncUnitWrapper Extra = singleIssueInSlot(0);
  Extra.setDRPorts(0, 1);
  EXPECT_TRUE(Cycle.conflict(Extra));
}

TEST(HaydnHazardRecognizerTest, BlockedDominatesEmpty) {
  HaydnFuncUnitWrapper Blocked;
  Blocked.blockResources();
  HaydnFuncUnitWrapper Empty;
  EXPECT_TRUE(Blocked.conflict(Empty) || Empty.conflict(Blocked));
  EXPECT_TRUE(Blocked.conflict(singleIssueInSlot(2)));
}

TEST(HaydnPackLegalityTest, DualAuthorityIssueCapMatchesBundle) {
  // pack::MaxIssuePerCycle must equal Haydn ISSUE_SLOT_COUNT / Bundle fill.
  using namespace llvm::haydn::pack;
  EXPECT_EQ(MaxIssuePerCycle, 3u);
  EXPECT_EQ(MaxIssuePerCycle, Haydn::ISSUE_SLOT_COUNT);
}

//===----------------------------------------------------------------------===//
// HR placement authority is product CycleState tryAdd (not getLegalSlots
// S0-first auction). Pure solver pins the order EmitInstruction stamps.
//===----------------------------------------------------------------------===//

TEST(HaydnHazardRecognizerTest, B24_TryAddIsPlacementAuthorityS2First) {
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  ASSERT_TRUE(hasPlacementAlternatives(Fmts, Haydn::ADD32));
  CycleState S = makeProductCycleState();
  // Empty cycle accepts ADD32 on S2 (not S0).
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT2));
  EXPECT_EQ(fieldSlotsToIndex(S.Members.back().FieldSlots),
            std::optional<unsigned>(2u));

  // Second ADD32 → S1; third → S0; fourth Hazard-shaped reject.
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(fieldSlotsToIndex(S.Members.back().FieldSlots),
            std::optional<unsigned>(1u));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(fieldSlotsToIndex(S.Members.back().FieldSlots),
            std::optional<unsigned>(0u));
  EXPECT_FALSE(canTryAddProduct(S, Fmts, Haydn::ADD32));
}

TEST(HaydnHazardRecognizerTest, B24_ST32BlocksSecondStore) {
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ST32));
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT0));
  EXPECT_FALSE(canTryAddProduct(S, Fmts, Haydn::ST32));
  // Multi-slot ALU still fits on S2.
  EXPECT_TRUE(canTryAddProduct(S, Fmts, Haydn::ADD32));
}

TEST(HaydnHazardRecognizerTest, B24_DualLoadThenMac) {
  // Dual LD32 (S0|S1) + MAC (S1|S2) product pack — tryAdd order must allow
  // LD@S1, LD@S0, MAC@S2 when loads issue first (or MAC@S2 then loads).
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::LD32)); // prefers S1 (S0|S1, high first)
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::LD32)); // remaining load slot
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::X2MULA32));
  EXPECT_EQ(S.OccupiedSlots,
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
}

//===----------------------------------------------------------------------===//
// Alts-only placement — no getLegalSlots no-alt fallback path
//===----------------------------------------------------------------------===//

TEST(HaydnHazardRecognizerTest, B25_NoAltSkipsPlacementGate) {
  // No PlacementAlternatives → tryAdd returns false; getLegalSlots is not a
  // placement fallback (AIEHazardRecognizer.cpp:186-187: no alts → fixed-slot
  // canAdd; Haydn no-alt means no multi-slot auction).
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  EXPECT_FALSE(hasPlacementAlternatives(Fmts, /*Opcode=*/0));
  CycleState S = makeProductCycleState();
  EXPECT_FALSE(canTryAddProduct(S, Fmts, 0));
  EXPECT_FALSE(tryAddProduct(S, Fmts, 0));
  EXPECT_TRUE(S.empty());
  EXPECT_EQ(S.OccupiedSlots, 0u);
}

TEST(HaydnHazardRecognizerTest, B25_FieldSlotsFromSparseIndex) {
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  SmallVector<PlacementAlternative, 4> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD64, Alts));
  // Sparse {0, S1, S2}: non-zero rows carry FieldSlots by index.
  ASSERT_EQ(Alts.size(), 2u);
  EXPECT_EQ(Alts[0].FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_EQ(Alts[1].FieldSlots, SlotBits(Haydn::SLOT2));
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD64));
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT2));
}

} // end anonymous namespace
