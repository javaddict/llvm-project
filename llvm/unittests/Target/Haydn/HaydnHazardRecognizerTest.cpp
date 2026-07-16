//===- HaydnHazardRecognizerTest.cpp - resource-model tests ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the Haydn post-RA hazard-recognizer RESOURCE MODEL — the
// `HaydnFuncUnitWrapper::conflict` predicate that the HR's slot-selection
// (D335 slice 2, codex P0) will call to decide whether a candidate slot fits
// the current cycle. This file pins the EXISTING rules (slot exclusivity,
// 3-issue cap, GPR 4R2W, DR64 7R3W, AR 2R2W) as a TDD baseline so the
// behavioral slot-selection change is tested against a known-correct model.
//
// HaydnFuncUnitWrapper is pure data (no MachineInstr/MachineFunction needed),
// so these tests construct wrappers directly via the ResourceSet ctor + the
// port setters. Slot bit indices: SLOT0=0, SLOT1=1, SLOT2=2 (per
// HaydnHazardRecognizer.cpp:109, matching the itinerary FuncUnits).
//
//===----------------------------------------------------------------------===//

#include "HaydnHazardRecognizer.h"
#include "HaydnPortModel.h"
#include "HaydnStaticBitSet.h"
#include "gtest/gtest.h"

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
  // slot — it can always find a free slot. This is the L171/B2 rule: rejecting
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

} // end anonymous namespace
