//===- HaydnBundleFormatSolverTest.cpp - CycleState solver -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for pure CycleState tryAdd/commit + exact candidate set.
//
// AIE peers:
//   AIEBundle.h:62-105 canAdd / :110-145 add
//   AIEHazardRecognizer.cpp:174-214 getAlternateInstsOpcode alt try
//   AIEFormat.cpp:18-27 PacketFormats::getFormat first-covering
//   BundleTest.cpp:33-41 FormatData[] synthetic multi-row shape
//
// product: generated PacketFormats BUNDLE_E96_* only (tryAddProduct
// commitProduct / productFeasibleFormatMask / RA-hint / materialize / verify).
// Synthetic 2-row FormatDesc is unit/solver only (not product emit).
//
// exactTryAddProduct on nondominated CycleCandidateSet replaces first-fit
// freeze for Bundle/HR/SMS; tryAddProduct is preferred collapse.
// BundlePlan.Bytes always from planFromPacketFormats (no hard rebuild).
//
//===----------------------------------------------------------------------===//

#include "HaydnBundle.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCInst.h"
#include "gtest/gtest.h"
#include <algorithm>

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

// Three occupied issue bits cannot keep E96TwoEntry (2 entries). Wave 2
// coveringFormatMaskFromPackets strips E2 → E3-only. ProductFormatMask (E2|E3)
// is the empty/≤2-member frontier, not a retired size-1 Full mask=3.
constexpr uint64_t E3OnlyFormatMask =
    formatRowBit(BundleFormatRowID::E96ThreeEntry);

//===----------------------------------------------------------------------===//
// Empty commit → stall
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, EmptyCommitStall) {
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  CycleState S = makeProductCycleState(Packets);
  EXPECT_TRUE(S.empty());
  EXPECT_EQ(S.OccupiedSlots, 0u);
  EXPECT_EQ(S.FeasibleFormatMask, ProductFormatMask);

  auto Plan = commitProduct(S, Packets);
  ASSERT_TRUE(Plan.has_value());
  EXPECT_TRUE(Plan->empty());
  EXPECT_EQ(Plan->OccupiedSlots, 0u);
  EXPECT_EQ(Plan->Row, BundleFormatRowID::E96TwoEntry);
  EXPECT_EQ(Plan->Bytes.Value, productParcelBytes().Value);
  EXPECT_TRUE(Plan->isProductLegal());
  // Commit bytes come from registry EncodedBytes.
  auto FromPackets = productEncodedBytesFromPackets(Packets);
  ASSERT_TRUE(FromPackets.has_value());
  EXPECT_EQ(Plan->Bytes, *FromPackets);
}

// tryAddProduct / commitProduct read generated PacketFormats coverage.
// Product identity is Format E (E2|E3); EncodedBytes from registry.
TEST(HaydnBundleFormatSolver, ProductAuthorityIsGeneratedPacketFormats) {
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  const VLIWFormat *Prod = productVLIWFormat(Packets);
  ASSERT_NE(Prod, nullptr);
  // Product FormatData is E96 composites (Size=12).
  EXPECT_TRUE(StringRef(Prod->Name).starts_with("BUNDLE_E96_"));
  EXPECT_EQ(vliwFormatSizeAsBytes(Prod->getSize()), productParcelBytes());

  CycleState S = makeProductCycleState(Packets);
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ST32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD64));
  EXPECT_EQ(S.FeasibleFormatMask, ProductFormatMask);
  EXPECT_EQ(productFeasibleFormatMask(Packets, S.OccupiedSlots),
            ProductFormatMask);

  auto Plan = commitProduct(S, Packets);
  ASSERT_TRUE(Plan.has_value());
  EXPECT_TRUE(Plan->isProductLegal());
  EXPECT_EQ(Plan->Bytes, productParcelBytes());
  EXPECT_EQ(Plan->memberCount(), 2u);
  EXPECT_EQ(Plan->Row, BundleFormatRowID::E96TwoEntry);

  // SLOT_ALL is three issue bits → E3-only (E2 cannot hold three entries).
  EXPECT_EQ(coveringFormatMaskFromPackets(Packets, Haydn::SLOT_ALL,
                                          ProductFormatMask),
            E3OnlyFormatMask);
  EXPECT_EQ(coveringFormatMaskFromPackets(Packets, Haydn::SLOT0,
                                          /*AllowedMask=*/0),
            0u);
}

// Product Format E rows reach HR/SMS frontier, RA-hint eligibility,
// commit (post-RA), and registry EncodedBytes size.
TEST(HaydnBundleFormatSolver, OneFullRowReachesHRSMSRAHintCommitSize) {
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  const VLIWFormat *Prod = productVLIWFormat(Packets);
  ASSERT_NE(Prod, nullptr);
  EXPECT_TRUE(StringRef(Prod->Name).starts_with("BUNDLE_E96_"));
  EncodedBytes Size = productParcelBytes();
  EXPECT_EQ(vliwFormatSizeAsBytes(Prod->getSize()), Size);

  // HR / SMS row frontier (pre-setDesc). Empty keeps E2|E3; SLOT_ALL is E3.
  EXPECT_EQ(productFeasibleFormatMask(Packets, 0), ProductFormatMask);
  EXPECT_EQ(productFeasibleFormatMask(Packets, Haydn::SLOT_ALL),
            E3OnlyFormatMask);

  // RA-hint thin eligibility (product PacketFormats present).
  EXPECT_TRUE(productRAHintEligible(Packets));

  // Post-RA tryAdd/commitProduct → BundlePlan.Bytes == registry parcel.
  CycleState S = makeProductCycleState(Packets);
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  auto Plan = commitProduct(S, Packets);
  ASSERT_TRUE(Plan.has_value());
  EXPECT_EQ(Plan->Bytes, Size);
  EXPECT_EQ(Plan->Row, BundleFormatRowID::E96TwoEntry);
  EXPECT_TRUE(Plan->isProductLegal());

  // Size model: productParcelBytes / registry agree; only E2/E3 rows are
  // product.
  EXPECT_EQ(productParcelBytes(), Size);
  EXPECT_EQ(*productEncodedBytesFromPackets(Packets), Size);
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
  EXPECT_TRUE(formatEMemberOccupiesEntry(S.Members[0].MemberOpcode, 0));
  EXPECT_EQ(S.Members[0].FieldSlots, SlotBits(Haydn::SLOT0));

  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD64));
  EXPECT_EQ(S.memberCount(), 2u);
  EXPECT_NE(S.OccupiedSlots & (Haydn::SLOT1 | Haydn::SLOT2), 0u);
  // Prefer S2 first (Bundle.pickSlot order).
  EXPECT_TRUE(formatEMemberOccupiesEntry(S.Members[1].MemberOpcode, 2));
  EXPECT_EQ(S.Members[1].FieldSlots, SlotBits(Haydn::SLOT2));

  auto Plan = commitProduct(S);
  ASSERT_TRUE(Plan.has_value());
  EXPECT_TRUE(Plan->isProductLegal());
  EXPECT_EQ(Plan->memberCount(), 2u);
  EXPECT_EQ(Plan->MemberOpcodes[0], Haydn::ST32);
  EXPECT_EQ(Plan->MemberOpcodes[1], Haydn::ADD64);
  EXPECT_EQ(Plan->OccupiedSlots, S.OccupiedSlots);
  EXPECT_TRUE(isProductBundleRow(Plan->Row));
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

  // Slot order preference S2 → S1 → S0; setDesc targets are Format E members.
  EXPECT_TRUE(formatEMemberOccupiesEntry(S.Members[0].MemberOpcode, 2));
  EXPECT_TRUE(formatEMemberOccupiesEntry(S.Members[1].MemberOpcode, 1));
  EXPECT_TRUE(formatEMemberOccupiesEntry(S.Members[2].MemberOpcode, 0));

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
  EXPECT_TRUE(formatEMemberOccupiesEntry(Alts[0].MemberOpcode, 0));
  EXPECT_EQ(Alts[0].FieldSlots, SlotBits(Haydn::SLOT0));
  EXPECT_TRUE(formatEMemberOccupiesEntry(Alts[1].MemberOpcode, 1));
  EXPECT_EQ(Alts[1].FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_TRUE(formatEMemberOccupiesEntry(Alts[2].MemberOpcode, 2));
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
// Brute-force small opcode sets vs Bundle canAdd oracle (E2/E3 only)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, BruteForceVsBundleCanAddOracle) {
  // Sequential packing: exact solver accept/reject must match Bundle.canAdd
  // for multi-slot logicals that have PlacementAlternatives (Full product).
  // Both sides use nondominated CycleCandidateSet expand.
  HaydnMCFormats Fmts;
  const unsigned Opcodes[] = {
      Haydn::ADD32, Haydn::ST32, Haydn::ADD64, Haydn::LD32, Haydn::SUB32,
  };

  // All non-empty sequences of length <= 4 from a small alphabet.
  // Compare step-by-step: for each next op, canAdd vs exactTryAdd.
  auto runSeq = [&](ArrayRef<unsigned> Seq) {
    Haydn::Bundle<MCInst> B(&Fmts);
    SmallVector<MCInst, 4> Storage;
    Storage.resize(Seq.size());
    CycleCandidateSet Cands = makeProductCandidateSet();

    for (unsigned I = 0, E = Seq.size(); I != E; ++I) {
      const unsigned Opc = Seq[I];
      // Bundle empty-escape accepts ops with no slots; pure solver only
      // handles PlacementAlternative-bearing logicals. Skip ops with no alts.
      if (!hasPlacementAlternatives(Fmts, Opc))
        return;

      const bool BundleOk = B.canAdd(Opc);
      const bool SolverOk = canExactTryAddProduct(Cands, Fmts, Opc);
      EXPECT_EQ(BundleOk, SolverOk)
          << "seq idx " << I << " opc " << Opc
          << " occupied bundle=" << B.getOccupiedSlots()
          << " solver pref="
          << selectPreferredCandidate(Cands).OccupiedSlots;

      if (BundleOk) {
        Storage[I].setOpcode(Opc);
        B.add(&Storage[I]);
        ASSERT_TRUE(exactTryAddProduct(Cands, Fmts, Opc));
        EXPECT_EQ(B.getOccupiedSlots(),
                  selectPreferredCandidate(Cands).OccupiedSlots)
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
 // first-fit dead-end: ADD32 then two ADD64 needs rematch.
  {
    unsigned Seq[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
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

TEST(HaydnBundleFormatSolver, ProductFeasibleFormatMaskEmptyOccupied) {
  // Empty and ≤2-bit occupancy keep ProductFormatMask (E2|E3). Three-bit
  // SLOT_ALL drops E2 (coveringFormatMaskFromPackets OccCount>2).
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  EXPECT_EQ(productFeasibleFormatMask(Packets, /*Occupied=*/0),
            ProductFormatMask);
  EXPECT_EQ(productFeasibleFormatMask(Packets, Haydn::SLOT0),
            ProductFormatMask);
  EXPECT_EQ(productFeasibleFormatMask(Packets, Haydn::SLOT1 | Haydn::SLOT2),
            ProductFormatMask);
  EXPECT_EQ(productFeasibleFormatMask(Packets, Haydn::SLOT_ALL),
            E3OnlyFormatMask);
  // Convenience overload agrees.
  EXPECT_EQ(productFeasibleFormatMask(/*Occupied=*/0), ProductFormatMask);

  // makeProductCycleStateFromOccupied rebuilds the same frontier.
  EXPECT_EQ(
      makeProductCycleStateFromOccupied(Packets, 0).FeasibleFormatMask,
      ProductFormatMask);
  EXPECT_EQ(makeProductCycleStateFromOccupied(Packets, Haydn::SLOT_ALL)
                .FeasibleFormatMask,
            E3OnlyFormatMask);
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

TEST(HaydnBundleFormatSolver, B42_ComputeProductResMII_SixADD32) {
  // 6 × ADD32 → 2 full cycles.
  unsigned Ops[6];
  for (unsigned &O : Ops)
    O = Haydn::ADD32;
  EXPECT_EQ(computeProductResMII(Ops), 2u);
}

//===----------------------------------------------------------------------===//
// — exact nondominated matching (defeat first-fit freeze)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, VF21_FirstFitDeadEndRematchADD32_2xADD64) {
  // Classic first-fit dead-end under S2→S1→S0 preferred collapse:
  //   ADD32 → S2, ADD64 → S1, second ADD64 has no free S1|S2 field.
  // Exact candidate set rematches ADD32 onto S0 so both ADD64 fit.
  HaydnMCFormats Fmts;

  CycleState FirstFit = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(FirstFit, Fmts, Haydn::ADD32));
  EXPECT_EQ(FirstFit.Members.back().FieldSlots, SlotBits(Haydn::SLOT2));
  ASSERT_TRUE(tryAddProduct(FirstFit, Fmts, Haydn::ADD64));
  EXPECT_EQ(FirstFit.Members.back().FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_FALSE(tryAddProduct(FirstFit, Fmts, Haydn::ADD64))
      << "preferred collapse must dead-end the scarce S1|S2 pair";

  CycleCandidateSet Exact = makeProductCandidateSet();
  ASSERT_TRUE(exactTryAddProduct(Exact, Fmts, Haydn::ADD32));
  ASSERT_TRUE(exactTryAddProduct(Exact, Fmts, Haydn::ADD64));
  ASSERT_TRUE(exactTryAddProduct(Exact, Fmts, Haydn::ADD64))
      << "exact nondominated expand must rematch ADD32 off S2";
  const CycleState &Pref = selectPreferredCandidate(Exact);
  EXPECT_EQ(Pref.memberCount(), 3u);
  EXPECT_EQ(Pref.OccupiedSlots, SlotBits(Haydn::SLOT_ALL));
  // ADD32 rematches to S0 (only free field for three-way pack). Selected
  // format-member opcodes are the setDesc targets (logical→member bridge).
  EXPECT_EQ(Pref.Members[0].LogicalOpcode, Haydn::ADD32);
  EXPECT_EQ(Pref.Members[0].FieldSlots, SlotBits(Haydn::SLOT0));
  EXPECT_TRUE(formatEMemberOccupiesEntry(Pref.Members[0].MemberOpcode, 0));
  EXPECT_EQ(Pref.Members[1].LogicalOpcode, Haydn::ADD64);
  EXPECT_EQ(Pref.Members[1].FieldSlots, SlotBits(Haydn::SLOT2));
  EXPECT_TRUE(formatEMemberOccupiesEntry(Pref.Members[1].MemberOpcode, 2));
  EXPECT_EQ(Pref.Members[2].LogicalOpcode, Haydn::ADD64);
  EXPECT_EQ(Pref.Members[2].FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_TRUE(formatEMemberOccupiesEntry(Pref.Members[2].MemberOpcode, 1));
}

TEST(HaydnBundleFormatSolver, DualLoadStore0StoreRefusesExactTryAdd) {
  // Overlay on AIEBundle.h:62-105: FieldSlots S2 vs S0 are free, but both
  // stores need LOADSTORE0. exactTryAddProduct must refuse so HR canAdd does.
  HaydnMCFormats Fmts;
  CycleCandidateSet C = makeProductCandidateSet();
  ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::D_SW_L_WITH_IMM));
  ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::OR64));
  EXPECT_FALSE(exactTryAddProduct(C, Fmts, Haydn::ST8));
  unsigned Seq[] = {Haydn::D_SW_L_WITH_IMM, Haydn::OR64, Haydn::ST8};
  EXPECT_FALSE(exactCanPackProductSequence(Fmts, Seq));
}

// Assembler Bundle.canAdd/add and exactSolveProductOpcodes must agree on the
// selected MemberOpcode vector for the rematch triple (same pure expand +
// preferred collapse; SlotMap resync tracks FieldSlots after rematch).
TEST(HaydnBundleFormatSolver, RematchMemberOpcodeBridgeAsmAndExactSolve) {
  HaydnMCFormats Fmts;
  unsigned Seq[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};

  CycleCandidateSet Exact = makeProductCandidateSet();
  for (unsigned Opc : Seq)
    ASSERT_TRUE(exactTryAddProduct(Exact, Fmts, Opc));
  const CycleState &Pref = selectPreferredCandidate(Exact);
  ASSERT_EQ(Pref.memberCount(), 3u);
  EXPECT_TRUE(formatEMemberOccupiesEntry(Pref.Members[0].MemberOpcode, 0));
  EXPECT_TRUE(formatEMemberOccupiesEntry(Pref.Members[1].MemberOpcode, 2));
  EXPECT_TRUE(formatEMemberOccupiesEntry(Pref.Members[2].MemberOpcode, 1));

  // exactSolveProductOpcodes is the shared setDesc MemberOpcode vector.
  auto Solved = exactSolveProductOpcodes(Seq, Fmts);
  ASSERT_TRUE(Solved.has_value());
  ASSERT_EQ(Solved->MemberOpcodes.size(), 3u);
  EXPECT_EQ(Solved->MemberOpcodes[0], Pref.Members[0].MemberOpcode);
  EXPECT_EQ(Solved->MemberOpcodes[1], Pref.Members[1].MemberOpcode);
  EXPECT_EQ(Solved->MemberOpcodes[2], Pref.Members[2].MemberOpcode);
  EXPECT_TRUE(formatEMemberOccupiesEntry(Solved->MemberOpcodes[0], 0));
  EXPECT_TRUE(formatEMemberOccupiesEntry(Solved->MemberOpcodes[1], 2));
  EXPECT_TRUE(formatEMemberOccupiesEntry(Solved->MemberOpcodes[2], 1));

  // Standalone assembler path: Bundle canAdd/add of the same logical sequence
  // ends with SlotMap FieldSlots matching preferred Members (rematch sync).
  Haydn::Bundle<MCInst> B(&Fmts);
  MCInst Kids[3];
  for (unsigned I = 0; I < 3; ++I) {
    Kids[I].setOpcode(Seq[I]);
    ASSERT_TRUE(B.canAdd(Seq[I])) << "assembler canAdd at member " << I;
    B.add(&Kids[I]);
  }
  EXPECT_EQ(B.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL));
  auto SlotMap = B.getSlotMap();
  ASSERT_EQ(SlotMap.size(), 3u);
  EXPECT_EQ(SlotMap[0].first, haydnSlotMaskToKind(Pref.Members[0].FieldSlots));
  EXPECT_EQ(SlotMap[1].first, haydnSlotMaskToKind(Pref.Members[1].FieldSlots));
  EXPECT_EQ(SlotMap[2].first, haydnSlotMaskToKind(Pref.Members[2].FieldSlots));
  // Pack-friendly source order (two ADD64 then ADD32) selects the same
  // FieldSlots multiset / Full occupancy — encode identity is order-stable.
  CycleCandidateSet PackFriendly = makeProductCandidateSet();
  unsigned PackSeq[] = {Haydn::ADD64, Haydn::ADD64, Haydn::ADD32};
  for (unsigned Opc : PackSeq)
    ASSERT_TRUE(exactTryAddProduct(PackFriendly, Fmts, Opc));
  const CycleState &PF = selectPreferredCandidate(PackFriendly);
  EXPECT_EQ(PF.OccupiedSlots, Pref.OccupiedSlots);
  EXPECT_EQ(PF.memberCount(), 3u);
  // Selected members cover ADD32_S0 + ADD64 + ADD64 regardless of
  // source order (parity with MC rematch/pack-friendly objdump lines).
  SmallVector<unsigned, 3> PrefMem, PFMem;
  for (const CycleMember &M : Pref.Members)
    PrefMem.push_back(M.MemberOpcode);
  for (const CycleMember &M : PF.Members)
    PFMem.push_back(M.MemberOpcode);
  llvm::sort(PrefMem);
  llvm::sort(PFMem);
  EXPECT_EQ(PrefMem, PFMem);
}

TEST(HaydnBundleFormatSolver, VF21_ExactCanPackProductSetOracle) {
  HaydnMCFormats Fmts;
  // Empty / single always pack.
  EXPECT_TRUE(exactCanPackProductSet(Fmts, ArrayRef<unsigned>{}));
  {
    unsigned Ops[] = {Haydn::ADD32};
    EXPECT_TRUE(exactCanPackProductSet(Fmts, Ops));
  }
  // Three ADD32 fill Full.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_TRUE(exactCanPackProductSet(Fmts, Ops));
  }
  // Four members > issue width.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_FALSE(exactCanPackProductSet(Fmts, Ops));
  }
  // ADD32 + 2×ADD64: sequential preferred freeze fails, set oracle succeeds.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    EXPECT_TRUE(exactCanPackProductSequence(Fmts, Ops));
    EXPECT_TRUE(exactCanPackProductSet(Fmts, Ops));
  }
  // Two ST32 never pack (S0-only conflict) under any order.
  {
    unsigned Ops[] = {Haydn::ST32, Haydn::ST32};
    EXPECT_FALSE(exactCanPackProductSet(Fmts, Ops));
  }
  // Dual LD32 + MAC — all permutations pack.
  {
    unsigned Ops[] = {Haydn::LD32, Haydn::LD32, Haydn::X2MULA32};
    EXPECT_TRUE(exactCanPackProductSet(Fmts, Ops));
  }
}

TEST(HaydnBundleFormatSolver, VF21_ExactVsPreferredCollapseParityOnEasySeq) {
  // Sequences where preferred collapse never dead-ends must match exact.
  HaydnMCFormats Fmts;
  const unsigned Seqs[][3] = {
      {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32},
      {Haydn::ST32, Haydn::ADD64, Haydn::ADD32},
      {Haydn::LD32, Haydn::LD32, Haydn::X2MULA32},
  };
  for (const auto &Seq : Seqs) {
    CycleState S = makeProductCycleState();
    CycleCandidateSet C = makeProductCandidateSet();
    for (unsigned Opc : Seq) {
      ASSERT_TRUE(tryAddProduct(S, Fmts, Opc));
      ASSERT_TRUE(exactTryAddProduct(C, Fmts, Opc));
    }
    EXPECT_EQ(S.OccupiedSlots, selectPreferredCandidate(C).OccupiedSlots);
    EXPECT_EQ(S.memberCount(), selectPreferredCandidate(C).memberCount());
  }
}

TEST(HaydnBundleFormatSolver, VF21_NondominatedPruneKeepsIncomparableOcc) {
  // After one ADD32, S0/S1/S2 partials are pairwise incomparable — retain >1.
  HaydnMCFormats Fmts;
  CycleCandidateSet C = makeProductCandidateSet();
  ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::ADD32));
  EXPECT_GE(C.size(), 2u) << "must retain multiple single-slot partials";
  // Preferred materialize is still S2.
  EXPECT_EQ(selectPreferredCandidate(C).Members.back().FieldSlots,
            SlotBits(Haydn::SLOT2));
}

TEST(HaydnBundleFormatSolver, VF21_ResMIIUsesExactPacking) {
  // Preferred collapse would need 2 cycles for ADD32+ADD64+ADD64; exact = 1.
  unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
  EXPECT_EQ(computeProductResMII(Ops), 1u);
}

//===----------------------------------------------------------------------===//
// SMS-RESMII — exhaustive ≤3 format oracle vs greedy / preferred
//===----------------------------------------------------------------------===//

TEST(HaydnBundleFormatSolver, VF3_SMSResMII_ExhaustiveEqualsGreedyOnEasy) {
  // Qualification corpus shapes: greedy exactTryAdd ≡ exhaustive partition.
  const unsigned Cases[][6] = {
      {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, 0, 0, 0},
      {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, 0, 0},
      {Haydn::LD32, Haydn::LD32, Haydn::X2MULA32, 0, 0, 0},
      {Haydn::ST32, Haydn::ST32, 0, 0, 0, 0},
      {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64, 0, 0, 0},
  };
  const unsigned Lens[] = {3, 4, 3, 2, 3};
  for (unsigned C = 0; C < 5; ++C) {
    ArrayRef<unsigned> Ops(Cases[C], Lens[C]);
    const unsigned G = computeProductResMII(Ops);
    const unsigned E = computeExhaustiveProductResMII(Ops);
    EXPECT_EQ(G, E) << "case " << C;
    EXPECT_EQ(productResMIIOverestimate(Ops), 0)
        << "SMS-RESMII: zero overestimate on qualification shape " << C;
    // Lower bound: ceil(N / ISSUE_SLOT_COUNT).
    EXPECT_GE(E, (Lens[C] + Haydn::ISSUE_SLOT_COUNT - 1) /
                     Haydn::ISSUE_SLOT_COUNT);
  }
  EXPECT_EQ(computeExhaustiveProductResMII(ArrayRef<unsigned>{}), 0u);
  {
    unsigned One[] = {Haydn::ADD32};
    EXPECT_EQ(computeExhaustiveProductResMII(One), 1u);
  }
}

TEST(HaydnBundleFormatSolver, VF3_SMSResMII_OracleDetectsPreferredOverestimate) {
  // Preferred first-fit freezes ADD32 on S2 then dead-ends second ADD64 →
  // sequential preferred ResMII = 2. Exact candidate set + exhaustive oracle
  // pack all three in one cycle. SMS-RESMII gate must see Over > 0 on the
  // weaker baseline while product greedy (exact) overestimate stays 0.
  unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
  EXPECT_EQ(computeExhaustiveProductResMII(Ops), 1u);
  EXPECT_EQ(computeProductResMII(Ops), 1u);
  EXPECT_EQ(productResMIIOverestimate(Ops), 0);

  EXPECT_GE(computePreferredProductResMII(Ops), 2u)
      << "preferred collapse must inflate ResMII on first-fit dead-end";
  EXPECT_GE(preferredProductResMIIOverestimate(Ops), 1)
      << "oracle must detect preferred-collapse overestimate";
}

TEST(HaydnBundleFormatSolver, VF3_SMSResMII_SixADD32AndSTConflict) {
  unsigned Six[6];
  for (unsigned &O : Six)
    O = Haydn::ADD32;
  EXPECT_EQ(computeExhaustiveProductResMII(Six), 2u);
  EXPECT_EQ(computeProductResMII(Six), 2u);
  EXPECT_EQ(productResMIIOverestimate(Six), 0);

  // Two S0-only ST32 never co-issue → ResMII 2 under any partition.
  HaydnMCFormats Fmts;
  unsigned TwoST[] = {Haydn::ST32, Haydn::ST32};
  EXPECT_EQ(computeExhaustiveProductResMII(TwoST), 2u);
  EXPECT_FALSE(exactCanFormOneProductCycle(Fmts, TwoST));
  EXPECT_TRUE(
      exactCanFormOneProductCycle(Fmts, ArrayRef<unsigned>(TwoST, 1)));
}

TEST(HaydnBundleFormatSolver, VF3_SMSResMII_ExhaustivePermutationMatrix) {
  // All permutations of dual-load + MAC pack in one cycle; ST+ST never does.
  HaydnMCFormats Fmts;
  unsigned LDM[] = {Haydn::LD32, Haydn::LD32, Haydn::X2MULA32};
  // Heap's algorithm over 3! — every order packs as a set and greedy=1.
  SmallVector<unsigned, 3> P(LDM, LDM + 3);
  SmallVector<unsigned, 3> C(3, 0);
  auto Check = [&](ArrayRef<unsigned> Seq) {
    EXPECT_TRUE(exactCanPackProductSet(Fmts, Seq));
    EXPECT_EQ(computeExhaustiveProductResMII(Seq), 1u);
    EXPECT_EQ(computeProductResMII(Seq), 1u);
    EXPECT_EQ(productResMIIOverestimate(Seq), 0);
  };
  Check(P);
  unsigned I = 0;
  while (I < 3) {
    if (C[I] < I) {
      if ((I & 1) == 0)
        std::swap(P[0], P[I]);
      else
        std::swap(P[C[I]], P[I]);
      Check(P);
      ++C[I];
      I = 0;
    } else {
      C[I] = 0;
      ++I;
    }
  }

  unsigned Bad[] = {Haydn::ST32, Haydn::ST32, Haydn::ADD32};
  // ST+ST conflict forces ≥2 cycles; ADD may co-issue with one ST → oracle 2.
  EXPECT_EQ(computeExhaustiveProductResMII(Bad), 2u);
  EXPECT_EQ(productResMIIOverestimate(Bad), 0);
}

TEST(HaydnBundleFormatSolver, VF3_SMSResMII_GreedyOrderTrapOverestimate) {
  // Left-to-right greedy can open a third cycle on a bad order while a
  // different partition needs only two: two ST32 (S0-only) then three ADD32.
  // Greedy [ST32, ST32, ADD32, ADD32, ADD32]:
  //   c1=ST32; c2=ST32+ADD32+ADD32; c3=ADD32 → 3
  // Exact: {ST32,ADD32,ADD32} + {ST32,ADD32} → 2
  // This is the SMS-RESMII positive overestimate pin for exactTryAdd greedy.
  unsigned Ops[] = {Haydn::ST32, Haydn::ST32, Haydn::ADD32, Haydn::ADD32,
                    Haydn::ADD32};
  EXPECT_EQ(computeProductResMII(Ops), 3u);
  EXPECT_EQ(computeExhaustiveProductResMII(Ops), 2u);
  EXPECT_EQ(productResMIIOverestimate(Ops), 1)
      << "SMS-RESMII must detect greedy overestimate on this multiset";

  // Better body order makes greedy match exact (order-sensitive upper bound).
  unsigned GoodOrder[] = {Haydn::ST32, Haydn::ADD32, Haydn::ADD32, Haydn::ST32,
                          Haydn::ADD32};
  EXPECT_EQ(computeProductResMII(GoodOrder), 2u);
  EXPECT_EQ(computeExhaustiveProductResMII(GoodOrder), 2u);
  EXPECT_EQ(productResMIIOverestimate(GoodOrder), 0);
}

//===----------------------------------------------------------------------===//
// Exhaustive ≤3 all-subset / all-permutation pack oracle matrix
//===----------------------------------------------------------------------===//
//
// Dense Full-only oracle: every ordered sequence and multiset of length ≤3
// from a fixed alphabet is checked against exact sequence packing, exact set
// packing (any perm), preferred-collapse parity where both succeed, and
// deterministic preferred field assignment. Closest legal/illegal pairs pin
// issue-width and exclusive-slot boundaries. No second legality table — only
// pure CycleCandidateSet APIs already used by Bundle / HR / SMS / verifier.

namespace {

// Alphabet mixes exclusive, dual-slot, and triple-slot logicals (all have
// PlacementAlternatives under product Full).
static constexpr unsigned kOracleAlpha[] = {
    Haydn::ADD32,    // S0|S1|S2
    Haydn::ADD64,    // S1|S2
    Haydn::ST32,     // S0
    Haydn::LD32,     // S0|S1
    Haydn::X2MULA32, // S1|S2 MAC family
};
static constexpr unsigned kOracleAlphaN =
    sizeof(kOracleAlpha) / sizeof(kOracleAlpha[0]);

/// Preferred member FieldSlots after exact expand of \p Seq (must pack).
static SmallVector<SlotBits, 3>
preferredFieldsAfterExact(const HaydnMCFormats &Fmts,
                          ArrayRef<unsigned> Seq) {
  CycleCandidateSet C =
      makeProductCandidateSet(Fmts.getPacketFormats());
  for (unsigned Opc : Seq)
    if (!exactTryAddProduct(C, Fmts, Opc))
      return {};
  const CycleState &P = selectPreferredCandidate(C);
  SmallVector<SlotBits, 3> Out;
  for (const CycleMember &M : P.Members)
    Out.push_back(M.FieldSlots);
  return Out;
}

/// True iff some unique permutation of \p Ops packs under exact sequence.
static bool anyPermExactSequencePacks(const HaydnMCFormats &Fmts,
                                      ArrayRef<unsigned> Ops) {
  SmallVector<unsigned, 3> P(Ops.begin(), Ops.end());
  llvm::sort(P);
  do {
    if (exactCanPackProductSequence(Fmts, P))
      return true;
  } while (std::next_permutation(P.begin(), P.end()));
  return false;
}

} // namespace

TEST(HaydnBundleFormatSolver, VF24_ExhaustiveLe3AllOrderedSeqOracle) {
  // Every ordered tuple length 1..3: exact sequence packing is the sole
  // authority. Preferred collapse must agree whenever both accept; when
  // preferred freezes into a dead-end, exact must still match set-oracle
  // truth (pinned separately). Deterministic preferred fields: two exact
  // expands of the same sequence yield identical FieldSlots.
  HaydnMCFormats Fmts;
  unsigned Checked = 0;

  auto CheckSeq = [&](ArrayRef<unsigned> Seq) {
    ++Checked;
    const bool ExactSeq = exactCanPackProductSequence(Fmts, Seq);
    // Preferred single-state collapse.
    CycleState Pref = makeProductCycleState();
    bool PrefOk = true;
    for (unsigned Opc : Seq) {
      if (!tryAddProduct(Pref, Fmts, Opc)) {
        PrefOk = false;
        break;
      }
    }
    if (PrefOk) {
      EXPECT_TRUE(ExactSeq)
          << "preferred accept must imply exact sequence accept";
      EXPECT_EQ(Pref.memberCount(), Seq.size());
      // Preferred fields match selectPreferredCandidate after exact expand.
      auto ExactFields = preferredFieldsAfterExact(Fmts, Seq);
      ASSERT_EQ(ExactFields.size(), Pref.memberCount());
      for (unsigned I = 0, E = Pref.memberCount(); I != E; ++I)
        EXPECT_EQ(Pref.Members[I].FieldSlots, ExactFields[I])
            << "field drift at member " << I;
    }
    // Determinism: two independent exact expands agree on preferred fields.
    if (ExactSeq) {
      auto A = preferredFieldsAfterExact(Fmts, Seq);
      auto B = preferredFieldsAfterExact(Fmts, Seq);
      ASSERT_EQ(A.size(), Seq.size());
      ASSERT_EQ(B.size(), Seq.size());
      for (unsigned I = 0; I < Seq.size(); ++I)
        EXPECT_EQ(A[I], B[I]) << "nondeterministic preferred field " << I;
      // Exact set oracle must accept any sequence that packs in order.
      EXPECT_TRUE(exactCanPackProductSet(Fmts, Seq));
      EXPECT_TRUE(exactCanFormOneProductCycle(Fmts, Seq));
      EXPECT_EQ(computeExhaustiveProductResMII(Seq), 1u);
    } else if (!Seq.empty() && Seq.size() <= Haydn::ISSUE_SLOT_COUNT) {
      // One-cycle form fails; exhaustive ResMII is at least 2 when every
      // singleton is placeable (alphabet members always pack alone).
      EXPECT_GE(computeExhaustiveProductResMII(Seq), 2u);
    }
  };

  for (unsigned A : kOracleAlpha)
    CheckSeq(ArrayRef<unsigned>(&A, 1));

  for (unsigned A : kOracleAlpha)
    for (unsigned B : kOracleAlpha) {
      unsigned Seq[2] = {A, B};
      CheckSeq(Seq);
    }

  for (unsigned A : kOracleAlpha)
    for (unsigned B : kOracleAlpha)
      for (unsigned C : kOracleAlpha) {
        unsigned Seq[3] = {A, B, C};
        CheckSeq(Seq);
      }

  // 5 + 25 + 125
  EXPECT_EQ(Checked, kOracleAlphaN + kOracleAlphaN * kOracleAlphaN +
                         kOracleAlphaN * kOracleAlphaN * kOracleAlphaN);
}

TEST(HaydnBundleFormatSolver, VF24_ExhaustiveLe3AllMultisetOracle) {
  // Combinations-with-repetition length 1..3: set oracle ≡ any-perm sequence.
  HaydnMCFormats Fmts;
  unsigned Checked = 0;

  auto CheckMulti = [&](ArrayRef<unsigned> Ops) {
    ++Checked;
    const bool SetOk = exactCanPackProductSet(Fmts, Ops);
    const bool AnyPerm = anyPermExactSequencePacks(Fmts, Ops);
    EXPECT_EQ(SetOk, AnyPerm)
        << "set oracle must equal some-permutation sequence pack";
    if (SetOk) {
      EXPECT_EQ(computeExhaustiveProductResMII(Ops), 1u);
      EXPECT_TRUE(exactCanFormOneProductCycle(Fmts, Ops));
    } else if (!Ops.empty()) {
      EXPECT_GE(computeExhaustiveProductResMII(Ops), 2u);
    }
  };

  for (unsigned I = 0; I < kOracleAlphaN; ++I)
    CheckMulti(ArrayRef<unsigned>(&kOracleAlpha[I], 1));

  for (unsigned I = 0; I < kOracleAlphaN; ++I)
    for (unsigned J = I; J < kOracleAlphaN; ++J) {
      unsigned Ops[2] = {kOracleAlpha[I], kOracleAlpha[J]};
      CheckMulti(Ops);
    }

  for (unsigned I = 0; I < kOracleAlphaN; ++I)
    for (unsigned J = I; J < kOracleAlphaN; ++J)
      for (unsigned K = J; K < kOracleAlphaN; ++K) {
        unsigned Ops[3] = {kOracleAlpha[I], kOracleAlpha[J], kOracleAlpha[K]};
        CheckMulti(Ops);
      }

  // C(5,1)+C(5+1,2)+C(5+2,3) with repetition = 5 + 15 + 35 = 55
  EXPECT_EQ(Checked, 5u + 15u + 35u);
}

TEST(HaydnBundleFormatSolver, VF24_ClosestLegalIllegalAndPreferredOrder) {
  HaydnMCFormats Fmts;

  // Closest legal / illegal around exclusive S0 (one ST swapped for ADD64).
  {
    unsigned Legal[] = {Haydn::ST32, Haydn::ADD64};
    unsigned Illegal[] = {Haydn::ST32, Haydn::ST32};
    EXPECT_TRUE(exactCanPackProductSet(Fmts, Legal));
    EXPECT_TRUE(exactCanPackProductSequence(Fmts, Legal));
    EXPECT_FALSE(exactCanPackProductSet(Fmts, Illegal));
    EXPECT_FALSE(exactCanPackProductSequence(Fmts, Illegal));
  }

  // Issue-width boundary: three multi-slot ALU pack; four never do.
  {
    unsigned Three[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    unsigned Four[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_TRUE(exactCanPackProductSet(Fmts, Three));
    EXPECT_FALSE(exactCanPackProductSet(Fmts, Four));
    EXPECT_EQ(computeExhaustiveProductResMII(Three), 1u);
    EXPECT_EQ(computeExhaustiveProductResMII(Four), 2u);
  }

  // Rematch triple: preferred sequential freezes then dead-ends; exact set
  // and exact sequence (with rematch) still pack in one cycle.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    CycleState Pref = makeProductCycleState();
    ASSERT_TRUE(tryAddProduct(Pref, Fmts, Haydn::ADD32));
    ASSERT_TRUE(tryAddProduct(Pref, Fmts, Haydn::ADD64));
    EXPECT_FALSE(tryAddProduct(Pref, Fmts, Haydn::ADD64))
        << "preferred collapse must dead-end second ADD64";
    EXPECT_TRUE(exactCanPackProductSequence(Fmts, Ops));
    EXPECT_TRUE(exactCanPackProductSet(Fmts, Ops));
    auto Fields = preferredFieldsAfterExact(Fmts, Ops);
    ASSERT_EQ(Fields.size(), 3u);
    // Rematch places ADD32 on S0 so both ADD64 take S1|S2.
    EXPECT_EQ(Fields[0], SlotBits(Haydn::SLOT0));
    EXPECT_EQ(Fields[1] | Fields[2],
              SlotBits(Haydn::SLOT1) | SlotBits(Haydn::SLOT2));
  }

  // Dual-load + MAC: every permutation is a legal one-cycle set.
  {
    unsigned Ops[] = {Haydn::LD32, Haydn::LD32, Haydn::X2MULA32};
    SmallVector<unsigned, 3> P(Ops, Ops + 3);
    llvm::sort(P);
    unsigned Perms = 0;
    do {
      EXPECT_TRUE(exactCanPackProductSequence(Fmts, P)) << "perm " << Perms;
      ++Perms;
    } while (std::next_permutation(P.begin(), P.end()));
    // Multiset perms of {LD,LD,MAC}: 3 distinct orders.
    EXPECT_EQ(Perms, 3u);
    EXPECT_TRUE(exactCanPackProductSet(Fmts, Ops));
  }

  // ST+ST+ADD: two S0-only never co-issue under any partition of one cycle.
  {
    unsigned Ops[] = {Haydn::ST32, Haydn::ST32, Haydn::ADD32};
    EXPECT_FALSE(exactCanPackProductSet(Fmts, Ops));
    EXPECT_EQ(computeExhaustiveProductResMII(Ops), 2u);
  }

  // Deterministic preferred order: multi-slot alone prefers S2; progressive
  // fill is S2 → S1 → S0; ST32 alone is S0; ST32+ADD64 pins ADD64 on S2.
  {
    unsigned One[] = {Haydn::ADD32};
    auto F1 = preferredFieldsAfterExact(Fmts, One);
    ASSERT_EQ(F1.size(), 1u);
    EXPECT_EQ(F1[0], SlotBits(Haydn::SLOT2));

    unsigned Two[] = {Haydn::ADD32, Haydn::ADD32};
    auto F2 = preferredFieldsAfterExact(Fmts, Two);
    ASSERT_EQ(F2.size(), 2u);
    EXPECT_EQ(F2[0], SlotBits(Haydn::SLOT2));
    EXPECT_EQ(F2[1], SlotBits(Haydn::SLOT1));

    unsigned Three[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    auto F3 = preferredFieldsAfterExact(Fmts, Three);
    ASSERT_EQ(F3.size(), 3u);
    EXPECT_EQ(F3[0], SlotBits(Haydn::SLOT2));
    EXPECT_EQ(F3[1], SlotBits(Haydn::SLOT1));
    EXPECT_EQ(F3[2], SlotBits(Haydn::SLOT0));

    unsigned St[] = {Haydn::ST32};
    auto FS = preferredFieldsAfterExact(Fmts, St);
    ASSERT_EQ(FS.size(), 1u);
    EXPECT_EQ(FS[0], SlotBits(Haydn::SLOT0));

    unsigned StAdd[] = {Haydn::ST32, Haydn::ADD64};
    auto FSA = preferredFieldsAfterExact(Fmts, StAdd);
    ASSERT_EQ(FSA.size(), 2u);
    EXPECT_EQ(FSA[0], SlotBits(Haydn::SLOT0));
    EXPECT_EQ(FSA[1], SlotBits(Haydn::SLOT2));
  }

  // MultiSlot_Pseudo ADD32_MSP books like sparse ADD32 (same preferred fields
  // and set-oracle outcomes on the rematch triple / three-fill shapes).
  {
    EXPECT_TRUE(hasPlacementAlternatives(Fmts, Haydn::ADD32_MSP));
    unsigned MspThree[] = {Haydn::ADD32_MSP, Haydn::ADD32_MSP, Haydn::ADD32_MSP};
    unsigned AddThree[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_EQ(exactCanPackProductSet(Fmts, MspThree),
              exactCanPackProductSet(Fmts, AddThree));
    auto FM = preferredFieldsAfterExact(Fmts, MspThree);
    auto FA = preferredFieldsAfterExact(Fmts, AddThree);
    ASSERT_EQ(FM.size(), 3u);
    ASSERT_EQ(FA.size(), 3u);
    for (unsigned I = 0; I < 3; ++I)
      EXPECT_EQ(FM[I], FA[I]) << "MSP field parity at " << I;

    unsigned MspRematch[] = {Haydn::ADD32_MSP, Haydn::ADD64, Haydn::ADD64};
    EXPECT_TRUE(exactCanPackProductSequence(Fmts, MspRematch));
    auto FR = preferredFieldsAfterExact(Fmts, MspRematch);
    ASSERT_EQ(FR.size(), 3u);
    EXPECT_EQ(FR[0], SlotBits(Haydn::SLOT0));
  }
}

} // namespace
