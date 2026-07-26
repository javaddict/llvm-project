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
// Product: BUNDLE128_FULL only. Synthetic 2-row FormatDesc is unit/solver
// only (not product emit).
//
//===----------------------------------------------------------------------===//

#include "HaydnBundle.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnBundlePlan.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
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
  EXPECT_EQ(Plan->FID, FormatID::Bundle128Full);
  EXPECT_EQ(Plan->Bytes.Value, 16u);
  EXPECT_TRUE(Plan->isProductLegal());
}

//===----------------------------------------------------------------------===//
// ST32 + ADD64 pack (disjoint slots)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, ST32_ADD64_Pack) {
  // ST32 is S0-only; ADD64 is S1|S2. Disjoint → both fit (mirrors
  // HaydnBundleTest DisjointSlotsFit).
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();

  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ST32));
  EXPECT_EQ(S.memberCount(), 1u);
  EXPECT_EQ(S.OccupiedSlots & Haydn::SLOT0, SlotBits(Haydn::SLOT0));
  EXPECT_EQ(S.Members[0].LogicalOpcode, Haydn::ST32);
  EXPECT_EQ(S.Members[0].MemberOpcode, Haydn::ST32_S0);
  EXPECT_EQ(S.Members[0].FieldSlots, SlotBits(Haydn::SLOT0));

  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD64));
  EXPECT_EQ(S.memberCount(), 2u);
  EXPECT_NE(S.OccupiedSlots & (Haydn::SLOT1 | Haydn::SLOT2), 0u);
  // Prefer S2 first (Bundle.pickSlot order).
  EXPECT_EQ(S.Members[1].MemberOpcode, Haydn::ADD64_S2);
  EXPECT_EQ(S.Members[1].FieldSlots, SlotBits(Haydn::SLOT2));

  auto Plan = commitProduct(S);
  ASSERT_TRUE(Plan.has_value());
  EXPECT_TRUE(Plan->isProductLegal());
  EXPECT_EQ(Plan->memberCount(), 2u);
  EXPECT_EQ(Plan->MemberOpcodes[0], Haydn::ST32);
  EXPECT_EQ(Plan->MemberOpcodes[1], Haydn::ADD64);
  EXPECT_EQ(Plan->OccupiedSlots, S.OccupiedSlots);
  EXPECT_EQ(Plan->FID, FormatID::Bundle128Full);
}

//===----------------------------------------------------------------------===//
// Three ADD32 fill; reject fourth
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, ThreeADD32_RejectFourth) {
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();

  for (unsigned I = 0; I < 3; ++I) {
    ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32)) << "ADD32 #" << I;
  }
  EXPECT_EQ(S.memberCount(), 3u);
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT_ALL));

  // Slot order preference S2 → S1 → S0.
  EXPECT_EQ(S.Members[0].MemberOpcode, Haydn::ADD32_S2);
  EXPECT_EQ(S.Members[1].MemberOpcode, Haydn::ADD32_S1);
  EXPECT_EQ(S.Members[2].MemberOpcode, Haydn::ADD32_S0);

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
  HaydnMCFormats Fmts;
  SmallVector<PlacementAlternative, 4> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD32, Alts));
  ASSERT_EQ(Alts.size(), 3u);
  EXPECT_EQ(Alts[0].MemberOpcode, Haydn::ADD32_S0);
  EXPECT_EQ(Alts[0].FieldSlots, SlotBits(Haydn::SLOT0));
  EXPECT_EQ(Alts[1].MemberOpcode, Haydn::ADD32_S1);
  EXPECT_EQ(Alts[1].FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_EQ(Alts[2].MemberOpcode, Haydn::ADD32_S2);
  EXPECT_EQ(Alts[2].FieldSlots, SlotBits(Haydn::SLOT2));
  for (const PlacementAlternative &A : Alts) {
    EXPECT_EQ(A.CompatibleFormatMask, ProductFormatMask);
  }

  Alts.clear();
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD64, Alts));
  ASSERT_EQ(Alts.size(), 2u);
  EXPECT_EQ(Alts[0].FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_EQ(Alts[1].FieldSlots, SlotBits(Haydn::SLOT2));
}

//===----------------------------------------------------------------------===//
// Synthetic 2-row FormatDesc + restricted CompatibleFormatMask → narrow
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, SyntheticTwoRow_PrefersNarrowPriority) {
  // Unit-only multi-row table (AIE BundleTest.cpp:33-41 FormatData shape).
  // Narrow Priority 0 covers S0|S1; Full Priority 1 is wide fallback.
  constexpr FormatID SynthNarrow = static_cast<FormatID>(1);
  const FormatDesc Table[] = {
      {SynthNarrow, /*Priority=*/0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT0 | Haydn::SLOT1)},
      {FormatID::Bundle128Full, /*Priority=*/1, Bundle128EncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT_ALL)},
  };

  HaydnMCFormats Fmts;
  CycleState S = makeInitialCycleState(Table);
  EXPECT_EQ(S.FeasibleFormatMask,
            formatIDBit(SynthNarrow) | formatIDBit(FormatID::Bundle128Full));

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
  NarrowOnly.OccupiedSlots = Haydn::SLOT0;
  NarrowOnly.FeasibleFormatMask =
      formatIDBit(SynthNarrow) | formatIDBit(FormatID::Bundle128Full);
  NarrowOnly.Members.push_back(
      CycleMember{Haydn::ADD32, Haydn::ADD32_S0, Haydn::SLOT0});

  auto NarrowPlan = commit(NarrowOnly, Table);
  ASSERT_TRUE(NarrowPlan.has_value());
  EXPECT_EQ(NarrowPlan->FID, SynthNarrow);
  EXPECT_EQ(NarrowPlan->Bytes.Value, 8u);
  EXPECT_FALSE(NarrowPlan->isProductLegal());

  // Occupancy needing S2 → Full only.
  CycleState NeedsFull;
  NeedsFull.OccupiedSlots = Haydn::SLOT2;
  NeedsFull.FeasibleFormatMask =
      formatIDBit(SynthNarrow) | formatIDBit(FormatID::Bundle128Full);
  NeedsFull.Members.push_back(
      CycleMember{Haydn::ADD32, Haydn::ADD32_S2, Haydn::SLOT2});
  auto FullPlan = commit(NeedsFull, Table);
  ASSERT_TRUE(FullPlan.has_value());
  EXPECT_EQ(FullPlan->FID, FormatID::Bundle128Full);
  EXPECT_TRUE(FullPlan->isProductLegal());
}

TEST(HaydnBundleFormatSolver, SyntheticTwoRow_RestrictedMaskDropsFull) {
  // Member CompatibleFormatMask = Narrow only: Feasible shrinks to Narrow
  // when covering holds. Exercise coveringFormatMask + selectFeasible.
  constexpr FormatID SynthNarrow = static_cast<FormatID>(1);
  const FormatDesc Table[] = {
      {SynthNarrow, 0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT0 | Haydn::SLOT1)},
      {FormatID::Bundle128Full, 1, Bundle128EncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT_ALL)},
  };
  const uint64_t NarrowMask = formatIDBit(SynthNarrow);
  const uint64_t BothMask =
      formatIDBit(SynthNarrow) | formatIDBit(FormatID::Bundle128Full);

  // S0 under Narrow-only allowed → NewMask = Narrow only.
  uint64_t New = coveringFormatMask(Table, Haydn::SLOT0, NarrowMask);
  EXPECT_EQ(New, NarrowMask);

  // S2 under Narrow-only → no cover.
  New = coveringFormatMask(Table, Haydn::SLOT2, NarrowMask);
  EXPECT_EQ(New, 0u);

  // S2 under Both → Full only.
  New = coveringFormatMask(Table, Haydn::SLOT2, BothMask);
  EXPECT_EQ(New, formatIDBit(FormatID::Bundle128Full));

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
  HaydnMCFormats Fmts;
  const unsigned Opcodes[] = {
      Haydn::ADD32, Haydn::ST32, Haydn::ADD64, Haydn::LD32, Haydn::SUB32,
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
    unsigned Seq[] = {Haydn::ST32, Haydn::ADD64, Haydn::ADD32};
    runSeq(Seq);
  }
  {
    unsigned Seq[] = {Haydn::LD32, Haydn::ADD32, Haydn::ADD64};
    runSeq(Seq);
  }
  {
    unsigned Seq[] = {Haydn::ST32, Haydn::ST32}; // second ST32 must fail
    runSeq(Seq);
  }
}

TEST(HaydnBundleFormatSolver, TryAddRejectLeavesStateUnchanged) {
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ST32));
  const CycleState Before = S;
  EXPECT_FALSE(tryAddProduct(S, Fmts, Haydn::ST32)); // S0 conflict
  EXPECT_EQ(S.memberCount(), Before.memberCount());
  EXPECT_EQ(S.OccupiedSlots, Before.OccupiedSlots);
  EXPECT_EQ(S.FeasibleFormatMask, Before.FeasibleFormatMask);
  EXPECT_EQ(S.Members[0].MemberOpcode, Before.Members[0].MemberOpcode);
}

TEST(HaydnBundleFormatSolver, UnknownOpcodeNoAltsRejected) {
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  EXPECT_FALSE(tryAddProduct(S, Fmts, /*LogicalOpc=*/0));
  EXPECT_TRUE(S.empty());
}

//===----------------------------------------------------------------------===//
// Helpers — from-occupied rebuild + probe + fieldSlotsToIndex
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, MakeFromOccupiedAndCanTryAdd) {
  HaydnMCFormats Fmts;
  CycleState Empty = makeProductCycleStateFromOccupied(0);
  EXPECT_TRUE(Empty.empty());
  EXPECT_EQ(Empty.OccupiedSlots, 0u);
  EXPECT_TRUE(canTryAddProduct(Empty, Fmts, Haydn::ADD32));

  CycleState OccS0 = makeProductCycleStateFromOccupied(Haydn::SLOT0);
  EXPECT_EQ(OccS0.OccupiedSlots, SlotBits(Haydn::SLOT0));
  EXPECT_NE(OccS0.FeasibleFormatMask, 0u);
  // ST32 is S0-only — cannot add onto occupied S0.
  EXPECT_FALSE(canTryAddProduct(OccS0, Fmts, Haydn::ST32));
  // ADD64 is S1|S2 — still fits.
  EXPECT_TRUE(canTryAddProduct(OccS0, Fmts, Haydn::ADD64));
  // Probe must not mutate.
  EXPECT_EQ(OccS0.OccupiedSlots, SlotBits(Haydn::SLOT0));
  EXPECT_TRUE(OccS0.empty()) << "from-occupied has no member history";
}

TEST(HaydnBundleFormatSolver, FieldSlotsToIndex) {
  EXPECT_EQ(fieldSlotsToIndex(Haydn::SLOT0), std::optional<unsigned>(0u));
  EXPECT_EQ(fieldSlotsToIndex(Haydn::SLOT1), std::optional<unsigned>(1u));
  EXPECT_EQ(fieldSlotsToIndex(Haydn::SLOT2), std::optional<unsigned>(2u));
  EXPECT_FALSE(fieldSlotsToIndex(0).has_value());
  EXPECT_FALSE(
      fieldSlotsToIndex(Haydn::SLOT0 | Haydn::SLOT1).has_value());
}

TEST(HaydnBundleFormatSolver, B24_TryAddS2FirstThenS1S0) {
  // Pin S2→S1→S0 order that Bundle/HR adapters inherit.
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.Members.back().FieldSlots, SlotBits(Haydn::SLOT2));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.Members.back().FieldSlots, SlotBits(Haydn::SLOT1));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.Members.back().FieldSlots, SlotBits(Haydn::SLOT0));
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
  // Empty and every Full-covering occupancy → ProductFormatMask (size-1).
  EXPECT_EQ(productFeasibleFormatMask(/*Occupied=*/0), ProductFormatMask);
  EXPECT_EQ(productFeasibleFormatMask(Haydn::SLOT0), ProductFormatMask);
  EXPECT_EQ(productFeasibleFormatMask(Haydn::SLOT1 | Haydn::SLOT2),
            ProductFormatMask);
  EXPECT_EQ(productFeasibleFormatMask(Haydn::SLOT_ALL), ProductFormatMask);

  // makeProductCycleStateFromOccupied rebuilds the same frontier.
  EXPECT_EQ(makeProductCycleStateFromOccupied(0).FeasibleFormatMask,
            ProductFormatMask);
  EXPECT_EQ(makeProductCycleStateFromOccupied(Haydn::SLOT_ALL).FeasibleFormatMask,
            ProductFormatMask);
}

TEST(HaydnBundleFormatSolver, B41_SyntheticSecondFormatCanShrinkFrontier) {
  // N-format-ready: synthetic 2nd FormatDesc row can drop out of the mask
  // when occupancy no longer covers (no product emit).
  // AIE BundleTest.cpp:33-41 FormatData[] multi-row shape.
  constexpr FormatID SynthNarrow = static_cast<FormatID>(1);
  const FormatDesc Table[] = {
      {SynthNarrow, /*Priority=*/0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT0 | Haydn::SLOT1)},
      {FormatID::Bundle128Full, /*Priority=*/1, Bundle128EncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT_ALL)},
  };
  const uint64_t Both =
      formatIDBit(SynthNarrow) | formatIDBit(FormatID::Bundle128Full);

  // Empty / S0: both formats cover.
  EXPECT_EQ(feasibleFormatMask(Table, /*Occupied=*/0, Both), Both);
  EXPECT_EQ(feasibleFormatMask(Table, Haydn::SLOT0, Both), Both);
  EXPECT_EQ(feasibleFormatMask(Table, Haydn::SLOT0 | Haydn::SLOT1, Both), Both);

  // S2 needs Full — Narrow drops (frontier shrinks).
  EXPECT_EQ(feasibleFormatMask(Table, Haydn::SLOT2, Both),
            formatIDBit(FormatID::Bundle128Full));
  EXPECT_EQ(feasibleFormatMask(Table, Haydn::SLOT_ALL, Both),
            formatIDBit(FormatID::Bundle128Full));

  // Product helper remains Full-only even when occupancy is S2.
  EXPECT_EQ(productFeasibleFormatMask(Haydn::SLOT2), ProductFormatMask);
}

TEST(HaydnBundleFormatSolver, B41_TryAddKeepsProductFrontier) {
  // After packing, CycleState.FeasibleFormatMask stays ProductFormatMask.
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  EXPECT_EQ(S.FeasibleFormatMask, ProductFormatMask);
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.FeasibleFormatMask, ProductFormatMask);
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ST32));
  EXPECT_EQ(S.FeasibleFormatMask, ProductFormatMask);
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
  unsigned Ops[] = {Haydn::LD32, Haydn::LD32, Haydn::X2MULA32};
  EXPECT_EQ(computeProductResMII(Ops), 1u);

  // Reverse order still one cycle.
  unsigned Ops2[] = {Haydn::X2MULA32, Haydn::LD32, Haydn::LD32};
  EXPECT_EQ(computeProductResMII(Ops2), 1u);
}

TEST(HaydnBundleFormatSolver, B42_LiveMaskVsOccupiedRebuild_Synthetic) {
  // Live FeasibleFormatMask after tryAdd can differ from occupancy-only
  // rebuild when CompatibleFormatMask restricts members (N-format-ready).
  // Product alts are Full-only so product path matches; exercise covering
  // with restricted Allowed mask on a synthetic table.
  constexpr FormatID SynthNarrow = static_cast<FormatID>(1);
  const FormatDesc Table[] = {
      {SynthNarrow, /*Priority=*/0, EncodedBytes{8},
       static_cast<SlotBits>(Haydn::SLOT0 | Haydn::SLOT1)},
      {FormatID::Bundle128Full, /*Priority=*/1, Bundle128EncodedBytes,
       static_cast<SlotBits>(Haydn::SLOT_ALL)},
  };
  const uint64_t Both =
      formatIDBit(SynthNarrow) | formatIDBit(FormatID::Bundle128Full);
  const uint64_t NarrowOnly = formatIDBit(SynthNarrow);

  // Occupancy-only rebuild with Both seed keeps both for S0.
  EXPECT_EQ(feasibleFormatMask(Table, Haydn::SLOT0, Both), Both);

  // Live-style: member Compatible = Narrow only shrinks frontier to Narrow
  // (same accumulation tryAdd does via Allowed = Feasible ∩ Compatible).
  uint64_t Live = coveringFormatMask(Table, Haydn::SLOT0, NarrowOnly);
  EXPECT_EQ(Live, NarrowOnly);
  EXPECT_NE(Live, feasibleFormatMask(Table, Haydn::SLOT0, Both))
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

} // namespace
