//===- HaydnBundleFormatSolverTest.cpp - CycleState solver -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for pure CycleState tryAdd/commit.
//
// AIE peers:
//   AIEBundle.h:62-105 canAdd / :110-145 add
//   AIEHazardRecognizer.cpp:174-214 getAlternateInstsOpcode alt try
//   AIEFormat.cpp:18-27 PacketFormats::getFormat first-covering
//   BundleTest.cpp:33-41 FormatData[] synthetic multi-row shape
//
// Product: BUNDLE_E3 only. Synthetic 2-row FormatDesc is unit/solver
// only (not product emit).
//
//===----------------------------------------------------------------------===//

#include "HaydnBundle.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnBundlePlan.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "HaydnTestMCInstrInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/MC/MCInst.h"
#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

//===----------------------------------------------------------------------===//
// Empty commit → stall
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, EmptyCommitStall) {
  CycleState S = makeProductCycleState();
  EXPECT_TRUE(S.empty());
  EXPECT_EQ(S.OccupiedSlots, 0u);
  EXPECT_EQ(S.FeasibleFormatMask, ProductFormatMask);

  auto Plan = commitProduct(S);
  ASSERT_TRUE(Plan.has_value());
  EXPECT_TRUE(Plan->empty());
  EXPECT_EQ(Plan->OccupiedSlots, 0u);
  // An empty cycle has no occupancy to derive a composite from, so it takes
  // the default row rather than a chosen one.
  EXPECT_EQ(Plan->FID, ProductFormatID);
  EXPECT_EQ(Plan->Bytes.Value, ProductEncodedBytesValue);
  EXPECT_TRUE(Plan->isProductLegal());
}

//===----------------------------------------------------------------------===//
// ST32 + ADD64 pack (disjoint slots)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, ST32_ADD64_Pack) {
  // ST32 is S0-only; ADD64 is S1|S2. Disjoint → both fit (mirrors
  // HaydnBundleTest DisjointSlotsFit).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  CycleState S = makeProductCycleState();

  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::S_SW_WITH_IMM));
  EXPECT_EQ(S.memberCount(), 1u);
  EXPECT_EQ(S.OccupiedSlots & Haydn::SLOT_P30, SlotBits(Haydn::SLOT_P30));
  EXPECT_EQ(S.Members[0].LogicalOpcode, Haydn::S_SW_WITH_IMM);
  EXPECT_EQ(S.Members[0].MemberOpcode, Haydn::S_SW_WITH_IMM_P30_LOADSTORE0);
  EXPECT_EQ(S.Members[0].FieldSlots, SlotBits(Haydn::SLOT_P30));

  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD64));
  EXPECT_EQ(S.memberCount(), 2u);
  EXPECT_NE(S.OccupiedSlots & (Haydn::SLOT_P31 | Haydn::SLOT_P32), 0u);
  // Prefer S2 first (Bundle.pickSlot order).
  EXPECT_EQ(S.Members[1].MemberOpcode, Haydn::ADD64_P32_ALU0);
  EXPECT_EQ(S.Members[1].FieldSlots, SlotBits(Haydn::SLOT_P32));

  auto Plan = commitProduct(S);
  ASSERT_TRUE(Plan.has_value());
  EXPECT_TRUE(Plan->isProductLegal());
  EXPECT_EQ(Plan->memberCount(), 2u);
  EXPECT_EQ(Plan->MemberOpcodes[0], Haydn::S_SW_WITH_IMM);
  EXPECT_EQ(Plan->MemberOpcodes[1], Haydn::ADD64);
  EXPECT_EQ(Plan->OccupiedSlots, S.OccupiedSlots);
  EXPECT_EQ(Plan->FID, FormatID::BundleE3);
}

//===----------------------------------------------------------------------===//
// Three ADD32 fill; reject fourth
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, ThreeADD32_RejectFourth) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  CycleState S = makeProductCycleState();

  for (unsigned I = 0; I < 3; ++I) {
    ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32)) << "ADD32 #" << I;
  }
  EXPECT_EQ(S.memberCount(), 3u);
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT_SET_E3));

  // Slot order preference S2 → S1 → S0.
  // P32/ALU0, P31/ALU1, P30/ALU2 — one per unit. The solver used to be
  // free to put all three on ALU0 because nothing modelled units;
  // that bundle cannot issue (FORMAT-E-SWITCH-PLAN.md 3, 7.1).
  EXPECT_EQ(S.Members[0].MemberOpcode, Haydn::ADD32_P32_ALU0);
  EXPECT_EQ(S.Members[1].MemberOpcode, Haydn::ADD32_P31_ALU1);
  EXPECT_EQ(S.Members[2].MemberOpcode, Haydn::ADD32_P30_ALU2);

  EXPECT_FALSE(tryAddProduct(S, Fmts, Haydn::ADD32))
      << "fourth ADD32 must conflict once S0|S1|S2 are full";
  EXPECT_EQ(S.memberCount(), 3u) << "reject must leave state unchanged";

  auto Plan = commitProduct(S);
  ASSERT_TRUE(Plan.has_value());
  EXPECT_TRUE(Plan->isProductLegal());
  EXPECT_EQ(Plan->memberCount(), 3u);
}

//===----------------------------------------------------------------------===//
// FieldSlots stamped on PlacementAlternative (no getLegalSlots authority)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, EnumerateStampsFieldSlots) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  SmallVector<PlacementAlternative, 4> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD32, Alts));
  // Seven placements, not three: ADD32 has two ALUs at each 3-entry position
  // plus one at entry 0 of the 2-entry form. The stamp is what matters here,
  // and it comes from the member, so assert it that way rather than by index.
  EXPECT_EQ(Alts.size(), 7u);
  SlotBits Union = 0;
  for (const PlacementAlternative &A : Alts) {
    EXPECT_EQ(A.FieldSlots, fieldSlotsForMember(Fmts, A.MemberOpcode));
    Union |= A.FieldSlots;
  }
  EXPECT_EQ(Union, Fmts.getLegalSlots(Haydn::ADD32));
  for (const PlacementAlternative &A : Alts) {
    EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
  }

  Alts.clear();
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD64, Alts));
  // ADD64 is no longer the sparse counter-example: it has the same seven
  // placements ADD32 has.
  EXPECT_EQ(Alts.size(), 7u);
  SlotBits Add64Union = 0;
  for (const PlacementAlternative &A : Alts) {
    EXPECT_EQ(A.FieldSlots, fieldSlotsForMember(Fmts, A.MemberOpcode));
    Add64Union |= A.FieldSlots;
  }
  EXPECT_EQ(Add64Union, Fmts.getLegalSlots(Haydn::ADD64));
}

//===----------------------------------------------------------------------===//
// Synthetic 2-row FormatDesc + restricted CompatibleFormatMask → narrow
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, SyntheticTwoRow_PrefersNarrowPriority) {
  // Unit-only multi-row table (AIE BundleTest.cpp:33-41 FormatData shape).
  // Narrow Priority 0 covers S0|S1; Full Priority 1 is wide fallback.
  // Past both live composites: FormatID 1 is BundleE3, not a spare value.
  constexpr FormatID SynthNarrow = static_cast<FormatID>(2);
  const FormatDesc Table[] = {
      {SynthNarrow, /*Priority=*/0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT_P30 | Haydn::SLOT_P31)},
      {FormatID::BundleE3, /*Priority=*/1, ProductEncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT_SET_E3)},
  };

  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  CycleState S = makeInitialCycleState(Table);
  EXPECT_EQ(S.FeasibleFormatMask,
            formatIDBit(SynthNarrow) | formatIDBit(FormatID::BundleE3));

  // ADD32 with product-only alts: first free field under both formats.
  // S2 is preferred but Narrow does not cover S2 — only Full covers S2.
  // tryAdd prefers S2 first: Full still covers → accepts S2 under Full only?
  // Actually Allowed = Feasible ∩ Compatible = both bits initially (product
  // alts have ProductFormatMask = Full only). Product alts are Full-only, so
  // Narrow is never selected via product alts.
  //
  // To exercise restricted mask preferring narrow, hand-build alts path is
  // not exposed on tryAdd (uses enumerate). Instead: after tryAdd of an op
  // that lands on S0, commit with multi-row still uses Full if Feasible is
  // Full-only from product alts.
  //
  // Direct Priority selection on synthetic occupancy:
  CycleState NarrowOnly;
  NarrowOnly.OccupiedSlots = Haydn::SLOT_P30;
  NarrowOnly.FeasibleFormatMask =
      formatIDBit(SynthNarrow) | formatIDBit(FormatID::BundleE3);
  NarrowOnly.Members.push_back(
      CycleMember{Haydn::ADD32, Haydn::ADD32_P30_ALU0, Haydn::SLOT_P30});

  auto NarrowPlan = commit(NarrowOnly, Table);
  ASSERT_TRUE(NarrowPlan.has_value());
  EXPECT_EQ(NarrowPlan->FID, SynthNarrow);
  EXPECT_EQ(NarrowPlan->Bytes.Value, 8u);
  EXPECT_FALSE(NarrowPlan->isProductLegal());

  // Occupancy needing S2 → Full only.
  CycleState NeedsFull;
  NeedsFull.OccupiedSlots = Haydn::SLOT_P32;
  NeedsFull.FeasibleFormatMask =
      formatIDBit(SynthNarrow) | formatIDBit(FormatID::BundleE3);
  NeedsFull.Members.push_back(
      CycleMember{Haydn::ADD32, Haydn::ADD32_P32_ALU0, Haydn::SLOT_P32});
  auto FullPlan = commit(NeedsFull, Table);
  ASSERT_TRUE(FullPlan.has_value());
  EXPECT_EQ(FullPlan->FID, FormatID::BundleE3);
  EXPECT_TRUE(FullPlan->isProductLegal());
}

TEST(HaydnBundleFormatSolver, SyntheticTwoRow_RestrictedMaskDropsFull) {
  // Member CompatibleFormatMask = Narrow only: Feasible shrinks to Narrow
  // when covering holds. Exercise coveringFormatMask + selectFeasible.
  // Past both live composites: FormatID 1 is BundleE3, not a spare value.
  constexpr FormatID SynthNarrow = static_cast<FormatID>(2);
  const FormatDesc Table[] = {
      {SynthNarrow, 0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT_P30 | Haydn::SLOT_P31)},
      {FormatID::BundleE3, 1, ProductEncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT_SET_E3)},
  };
  const uint64_t NarrowMask = formatIDBit(SynthNarrow);
  const uint64_t BothMask =
      formatIDBit(SynthNarrow) | formatIDBit(FormatID::BundleE3);

  // S0 under Narrow-only allowed → NewMask = Narrow only.
  uint64_t New = coveringFormatMask(Table, Haydn::SLOT_P30, NarrowMask);
  EXPECT_EQ(New, NarrowMask);

  // S2 under Narrow-only → no cover.
  New = coveringFormatMask(Table, Haydn::SLOT_P32, NarrowMask);
  EXPECT_EQ(New, 0u);

  // S2 under Both → Full only.
  New = coveringFormatMask(Table, Haydn::SLOT_P32, BothMask);
  EXPECT_EQ(New, formatIDBit(FormatID::BundleE3));

  // tryAdd with product alts: Full mask only; empty commit on multi-row
  // prefers Narrow Priority 0 (covers 0).
  auto Stall = commit(makeInitialCycleState(Table), Table);
  ASSERT_TRUE(Stall.has_value());
  EXPECT_EQ(Stall->FID, SynthNarrow);
  EXPECT_EQ(Stall->Bytes.Value, 8u);
}

//===----------------------------------------------------------------------===//
// Brute-force small opcode sets vs Bundle canAdd oracle (Full-only)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, BruteForceVsBundleCanAddOracle) {
  // Sequential packing: solver tryAdd accept/reject must match Bundle.canAdd
  // for multi-slot logicals that have PlacementAlternatives (Full product).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const unsigned Opcodes[] = {
      Haydn::ADD32, Haydn::S_SW_WITH_IMM, Haydn::ADD64, Haydn::S_LW_WITH_IMM, Haydn::SUB32,
  };

  // All non-empty sequences of length <= 4 from a small alphabet.
  // Compare step-by-step: for each next op, canAdd vs tryAdd (fresh copy).
  auto runSeq = [&](ArrayRef<unsigned> Seq) {
    Haydn::Bundle<MCInst> B(&Fmts);
    SmallVector<MCInst, 4> Storage;
    Storage.resize(Seq.size());
    CycleState S = makeProductCycleState();

    for (unsigned I = 0, E = Seq.size(); I != E; ++I) {
      const unsigned Opc = Seq[I];
      // Bundle empty-escape accepts ops with no slots; pure solver only
      // handles PlacementAlternative-bearing logicals. Skip ops with no alts.
      if (!hasPlacementAlternatives(Fmts, Opc))
        return;

      const bool BundleOk = B.canAdd(Opc);
      CycleState Probe = S;
      const bool SolverOk = tryAddProduct(Probe, Fmts, Opc);
      EXPECT_EQ(BundleOk, SolverOk)
          << "seq idx " << I << " opc " << Opc
          << " occupied bundle=" << B.getOccupiedSlots()
          << " solver=" << S.OccupiedSlots;

      if (BundleOk) {
        Storage[I].setOpcode(Opc);
        B.add(&Storage[I]);
        ASSERT_TRUE(tryAddProduct(S, Fmts, Opc));
        EXPECT_EQ(B.getOccupiedSlots(), S.OccupiedSlots)
            << "occupancy drift at idx " << I;
      }
    }
  };

  // Single ops.
  for (unsigned O : Opcodes)
    runSeq(ArrayRef<unsigned>(&O, 1));

  // Pairs.
  for (unsigned A : Opcodes)
    for (unsigned B : Opcodes) {
      unsigned Seq[2] = {A, B};
      runSeq(Seq);
    }

  // Selected triples / saturating patterns.
  {
    unsigned Seq[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    runSeq(Seq);
  }
  {
    unsigned Seq[] = {Haydn::S_SW_WITH_IMM, Haydn::ADD64, Haydn::ADD32};
    runSeq(Seq);
  }
  {
    unsigned Seq[] = {Haydn::S_LW_WITH_IMM, Haydn::ADD32, Haydn::ADD64};
    runSeq(Seq);
  }
  {
    unsigned Seq[] = {Haydn::S_SW_WITH_IMM, Haydn::S_SW_WITH_IMM}; // second ST32 must fail
    runSeq(Seq);
  }
}

TEST(HaydnBundleFormatSolver, TryAddRejectLeavesStateUnchanged) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::S_SW_WITH_IMM));
  const CycleState Before = S;
  EXPECT_FALSE(tryAddProduct(S, Fmts, Haydn::S_SW_WITH_IMM)); // S0 conflict
  EXPECT_EQ(S.memberCount(), Before.memberCount());
  EXPECT_EQ(S.OccupiedSlots, Before.OccupiedSlots);
  EXPECT_EQ(S.FeasibleFormatMask, Before.FeasibleFormatMask);
  EXPECT_EQ(S.Members[0].MemberOpcode, Before.Members[0].MemberOpcode);
}

TEST(HaydnBundleFormatSolver, UnknownOpcodeNoAltsRejected) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  CycleState S = makeProductCycleState();
  EXPECT_FALSE(tryAddProduct(S, Fmts, /*LogicalOpc=*/0));
  EXPECT_TRUE(S.empty());
}

//===----------------------------------------------------------------------===//
// Helpers — from-occupied rebuild + probe + fieldSlotsToIndex
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, MakeFromOccupiedAndCanTryAdd) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  CycleState Empty = makeProductCycleStateFromOccupied(0);
  EXPECT_TRUE(Empty.empty());
  EXPECT_EQ(Empty.OccupiedSlots, 0u);
  EXPECT_TRUE(canTryAddProduct(Empty, Fmts, Haydn::ADD32));

  CycleState OccS0 = makeProductCycleStateFromOccupied(Haydn::SLOT_P30);
  EXPECT_EQ(OccS0.OccupiedSlots, SlotBits(Haydn::SLOT_P30));
  EXPECT_NE(OccS0.FeasibleFormatMask, 0u);
  // ST32 is S0-only — cannot add onto occupied S0.
  EXPECT_FALSE(canTryAddProduct(OccS0, Fmts, Haydn::S_SW_WITH_IMM));
  // ADD64 is S1|S2 — still fits.
  EXPECT_TRUE(canTryAddProduct(OccS0, Fmts, Haydn::ADD64));
  // Probe must not mutate.
  EXPECT_EQ(OccS0.OccupiedSlots, SlotBits(Haydn::SLOT_P30));
  EXPECT_TRUE(OccS0.empty()) << "from-occupied has no member history";
}

TEST(HaydnBundleFormatSolver, FieldSlotsToIndex) {
  // The index is the slot's own bit position, and P20/P21 now occupy 0 and 1,
  // so the 3-entry slots start at 2. This is a slot-kind index, NOT an entry
  // number: P30 is entry 0 of its composite and index 2.
  EXPECT_EQ(fieldSlotsToIndex(Haydn::SLOT_P20), std::optional<unsigned>(0u));
  EXPECT_EQ(fieldSlotsToIndex(Haydn::SLOT_P21), std::optional<unsigned>(1u));
  EXPECT_EQ(fieldSlotsToIndex(Haydn::SLOT_P30), std::optional<unsigned>(2u));
  EXPECT_EQ(fieldSlotsToIndex(Haydn::SLOT_P31), std::optional<unsigned>(3u));
  EXPECT_EQ(fieldSlotsToIndex(Haydn::SLOT_P32), std::optional<unsigned>(4u));
  EXPECT_FALSE(fieldSlotsToIndex(0).has_value());
  EXPECT_FALSE(
      fieldSlotsToIndex(Haydn::SLOT_P30 | Haydn::SLOT_P31).has_value());
}

TEST(HaydnBundleFormatSolver, B24_TryAddS2FirstThenS1S0) {
  // Pin S2→S1→S0 order that Bundle/HR adapters inherit.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.Members.back().FieldSlots, SlotBits(Haydn::SLOT_P32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.Members.back().FieldSlots, SlotBits(Haydn::SLOT_P31));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.Members.back().FieldSlots, SlotBits(Haydn::SLOT_P30));
  EXPECT_FALSE(canTryAddProduct(S, Fmts, Haydn::ADD32));
}

//===----------------------------------------------------------------------===//
// productFeasibleFormatMask (Pre-RA/SMS FormatID frontier, size-1 Full)
//===----------------------------------------------------------------------===//
//
// AIE peers: AIEBundle.h:150-156 getFormatOrNull;
// AIEFormat.cpp:18-27 PacketFormats::getFormat first-covering.
// Haydn keeps a FormatID *mask* frontier until post-RA freeze (plan §7.1).

TEST(HaydnBundleFormatSolver, B41_ProductFeasibleFormatMaskEmptyOccupied) {
  // An empty occupancy leaves both composites open -- that is the frontier the
  // solver narrows. The moment a slot is taken the frontier collapses to the
  // one composite that slot belongs to, which is the entry-count decision
  // making itself rather than being decided (5.2).
  EXPECT_EQ(productFeasibleFormatMask(/*Occupied=*/0), ProductFormatMask);
  EXPECT_EQ(productFeasibleFormatMask(Haydn::SLOT_P20),
            formatIDBit(FormatID::BundleE2));
  EXPECT_EQ(productFeasibleFormatMask(Haydn::SLOT_P30),
            formatIDBit(FormatID::BundleE3));
  EXPECT_EQ(productFeasibleFormatMask(Haydn::SLOT_P31 | Haydn::SLOT_P32),
            formatIDBit(FormatID::BundleE3));
  EXPECT_EQ(productFeasibleFormatMask(Haydn::SLOT_SET_E3),
            formatIDBit(FormatID::BundleE3));
  // A mask spanning both is not a bundle: no row covers it.
  EXPECT_EQ(productFeasibleFormatMask(Haydn::SLOT_P20 | Haydn::SLOT_P30), 0u);

  // makeProductCycleStateFromOccupied rebuilds the same frontier.
  EXPECT_EQ(makeProductCycleStateFromOccupied(0).FeasibleFormatMask,
            ProductFormatMask);
  EXPECT_EQ(makeProductCycleStateFromOccupied(Haydn::SLOT_SET_E3).FeasibleFormatMask,
            formatIDBit(FormatID::BundleE3));
}

TEST(HaydnBundleFormatSolver, B41_SyntheticSecondFormatCanShrinkFrontier) {
  // N-format-ready: synthetic 2nd FormatDesc row can drop out of the mask
  // when occupancy no longer covers (no product emit).
  // AIE BundleTest.cpp:33-41 FormatData[] multi-row shape.
  // Past both live composites: FormatID 1 is BundleE3, not a spare value.
  constexpr FormatID SynthNarrow = static_cast<FormatID>(2);
  const FormatDesc Table[] = {
      {SynthNarrow, /*Priority=*/0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT_P30 | Haydn::SLOT_P31)},
      {FormatID::BundleE3, /*Priority=*/1, ProductEncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT_SET_E3)},
  };
  const uint64_t Both =
      formatIDBit(SynthNarrow) | formatIDBit(FormatID::BundleE3);

  // Empty / S0: both formats cover.
  EXPECT_EQ(feasibleFormatMask(Table, /*Occupied=*/0, Both), Both);
  EXPECT_EQ(feasibleFormatMask(Table, Haydn::SLOT_P30, Both), Both);
  EXPECT_EQ(feasibleFormatMask(Table, Haydn::SLOT_P30 | Haydn::SLOT_P31, Both), Both);

  // S2 needs Full — Narrow drops (frontier shrinks).
  EXPECT_EQ(feasibleFormatMask(Table, Haydn::SLOT_P32, Both),
            formatIDBit(FormatID::BundleE3));
  EXPECT_EQ(feasibleFormatMask(Table, Haydn::SLOT_SET_E3, Both),
            formatIDBit(FormatID::BundleE3));

  // Product helper remains Full-only even when occupancy is S2.
  EXPECT_EQ(productFeasibleFormatMask(Haydn::SLOT_P32), formatIDBit(FormatID::BundleE3));
}

TEST(HaydnBundleFormatSolver, B41_TryAddKeepsProductFrontier) {
  // After packing, CycleState.FeasibleFormatMask stays ProductFormatMask.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  CycleState S = makeProductCycleState();
  EXPECT_EQ(S.FeasibleFormatMask, ProductFormatMask);
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.FeasibleFormatMask, formatIDBit(FormatID::BundleE3));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::S_SW_WITH_IMM));
  EXPECT_EQ(S.FeasibleFormatMask, formatIDBit(FormatID::BundleE3));
  EXPECT_EQ(productFeasibleFormatMask(S.OccupiedSlots), S.FeasibleFormatMask);
}

//===----------------------------------------------------------------------===//
// computeProductResMII greedy bin-pack (post-RA tryAdd depth)
//===----------------------------------------------------------------------===//
//
// AIE peers: AIEHazardRecognizer.cpp:173-214 ResourceCycle canReserve/reserve;
// MachinePipeliner calculateResMIIDFA walks ResourceCycle packing.
// Haydn: pure computeProductResMII via tryAddProduct (plan §7.1).

TEST(HaydnBundleFormatSolver, B42_ComputeProductResMII_ADD32) {
  // Three ADD32 fill S2|S1|S0 under Full → ResMII 1; fourth needs cycle 2.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_EQ(computeProductResMII(Ops), 2u);
  }
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_EQ(computeProductResMII(Ops), 1u);
  }
  {
    unsigned Ops[] = {Haydn::ADD32};
    EXPECT_EQ(computeProductResMII(Ops), 1u);
  }
  EXPECT_EQ(computeProductResMII(ArrayRef<unsigned>{}), 0u);
}

TEST(HaydnBundleFormatSolver, B42_ComputeProductResMII_LD_LD_MAC) {
  // LD32 (S0|S1) ×2 + X2MULA32 (S1|S2): tryAdd S2→S1→S0 packs all three.
  // AIE-shaped dual-load + MAC density (ResMII 1).
  unsigned Ops[] = {Haydn::S_LW_WITH_IMM, Haydn::S_LW_WITH_IMM, Haydn::X2MULA32};
  EXPECT_EQ(computeProductResMII(Ops), 1u);

  // Reverse order still one cycle.
  unsigned Ops2[] = {Haydn::X2MULA32, Haydn::S_LW_WITH_IMM, Haydn::S_LW_WITH_IMM};
  EXPECT_EQ(computeProductResMII(Ops2), 1u);
}

TEST(HaydnBundleFormatSolver, B42_LiveMaskVsOccupiedRebuild_Synthetic) {
  // Live FeasibleFormatMask after tryAdd can differ from occupancy-only
  // rebuild when CompatibleFormatMask restricts members (N-format-ready).
  // Product alts are Full-only so product path matches; exercise covering
  // with restricted Allowed mask on a synthetic table.
  // Past both live composites: FormatID 1 is BundleE3, not a spare value.
  constexpr FormatID SynthNarrow = static_cast<FormatID>(2);
  const FormatDesc Table[] = {
      {SynthNarrow, /*Priority=*/0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT_P30 | Haydn::SLOT_P31)},
      {FormatID::BundleE3, /*Priority=*/1, ProductEncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT_SET_E3)},
  };
  const uint64_t Both =
      formatIDBit(SynthNarrow) | formatIDBit(FormatID::BundleE3);
  const uint64_t NarrowOnly = formatIDBit(SynthNarrow);

  // Occupancy-only rebuild with Both seed keeps both for S0.
  EXPECT_EQ(feasibleFormatMask(Table, Haydn::SLOT_P30, Both), Both);

  // Live-style: member Compatible = Narrow only shrinks frontier to Narrow
  // (same accumulation tryAdd does via Allowed = Feasible ∩ Compatible).
  uint64_t Live = coveringFormatMask(Table, Haydn::SLOT_P30, NarrowOnly);
  EXPECT_EQ(Live, NarrowOnly);
  EXPECT_NE(Live, feasibleFormatMask(Table, Haydn::SLOT_P30, Both))
      << "Occupied-only rebuild with full seed drifts from live Compatible "
         "intersection — SMS must hold live CycleState";
}

TEST(HaydnBundleFormatSolver, B42_ComputeProductResMII_SixADD32) {
  // 6 × ADD32 → 2 full cycles.
  unsigned Ops[6];
  for (unsigned &O : Ops)
    O = Haydn::ADD32;
  EXPECT_EQ(computeProductResMII(Ops), 2u);
}

//===----------------------------------------------------------------------===//
// Unit exclusivity is exact here, and CB-143's residual said otherwise
//===----------------------------------------------------------------------===//

// Three ADD32 fit because [ALU0, ALU1, ALU2] is three units (above). A MAC
// is the case that cannot: X2MUL32's five placements are P20/MAC0, P21/MAC1,
// P30/MAC0, P31/MAC0, P32/MAC1, so the machine offers it exactly two units
// and a third one has nowhere to go.
//
// CB-143's residual line claimed the opposite — that three instructions
// sharing a two-unit Available set "pass and cannot all issue". They do not
// pass: tryAdd tracks OccupiedUnits per member, so this is exact. The claim
// described HaydnFuncUnitWrapper::conflict, whose Required bits are entry
// POSITIONS (PlacementAlternative::FieldSlots), not units.
TEST(HaydnBundleFormatSolver, TwoMACUnitsRejectThird) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  CycleState S = makeProductCycleState();

  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::X2MUL32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::X2MUL32));
  EXPECT_EQ(S.memberCount(), 2u);
  EXPECT_EQ(llvm::popcount(S.OccupiedUnits), 2)
      << "two MACs must hold two distinct units";

  EXPECT_FALSE(tryAddProduct(S, Fmts, Haydn::X2MUL32))
      << "the machine has MAC0 and MAC1 only — a third MAC cannot issue even "
         "though a third entry slot is free";
  EXPECT_EQ(S.memberCount(), 2u) << "reject must leave state unchanged";
}

//===----------------------------------------------------------------------===//
// CB-147 — the first placement chooses the format, and E2-only ops lose
//===----------------------------------------------------------------------===//

// The two product rows have disjoint slot sets ({P20,P21} vs {P30,P31,P32}),
// so no occupancy is covered by both and the FIRST member placed decides the
// format for the whole cycle. tryAdd walks alternatives by descending slot bit,
// so anything holding a P3x placement takes E3 at once.
//
// ADDI32 has no P3x placement at all — a wide immediate only fits a 2-entry
// entry — so after ADD32 has taken P32 it can never join, even though
// {ADDI32, ADD32} is a legal 2-entry bundle. This is a density loss, not an
// illegal bundle, and it is the reproducer for CB-147.
TEST(HaydnBundleFormatSolver, CB147_E3FirstLocksOutE2OnlyADDI32) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());

  // Order that loses: the flexible op goes first and takes E3.
  {
    CycleState S = makeProductCycleState();
    ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
    EXPECT_EQ(S.Members[0].FieldSlots, SlotBits(Haydn::SLOT_P32))
        << "descending walk takes the highest slot, which is E3-only";
    EXPECT_FALSE(tryAddProduct(S, Fmts, Haydn::ADDI32))
        << "ADDI32 is E2-only; the cycle is already committed to E3";
    EXPECT_EQ(S.memberCount(), 1u);
  }

  // Same two instructions, other order: both fit, in E2. The pair is legal —
  // only the placement order made it look otherwise.
  {
    CycleState S = makeProductCycleState();
    ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADDI32));
    EXPECT_EQ(S.Members[0].FieldSlots, SlotBits(Haydn::SLOT_P21))
        << "ADDI32's highest placement is P21, which commits the cycle to E2";
    ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32))
        << "ADD32 has a P20 placement, so it can still join an E2 cycle";
    EXPECT_EQ(S.memberCount(), 2u);
    EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT_SET_E2));
  }
}

} // namespace
