//===- HaydnBundleMaterializeTest.cpp - exact commit + diagnostic split -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for haydn::bundle materialize surface:
//
//   * exactSolveProductOpcodes / exactPackOneOpcodeCycle — production exact
//     no-split one-cycle solve (never splits).
//   * opcodesFormOneLegalCycle — sole one-cycle legality authority (PostRA
//     instrsFormOneLegalCycle is the MI-list view; no strategy dual).
//   * auctionReadySubsetCycle / auctionFocusFillScore — bounded ready-subset
//     cycle auction (post-RA list ranking; max issued under exact product).
//   * greedySplitLegalOpcodeCycles — DIAGNOSTIC only (ResMII / partition
//     tests). Production post-RA must not use this to repair schedules.
//   * BundlePlan.Bytes always from planFromPacketFormats (generated Full);
//     the tests-only full-width plan is never a production fallback.
//
// Invariants locked on greedy diagnostic cases:
//   1. Partition: flatten(cycles) == input opcodes (order + coverage).
//   2. Product plan: every sub-cycle is ProductFormatID, productParcelBytes, 1 cycle.
//   3. Capacity: each sub-cycle has 1..3 members.
//   4. Sub-cycle legal: re-pack via Haydn::Bundle succeeds without split.
//   5. opcodesFormOneLegalCycle ⇔ greedy returns exactly one cycle.
//
//===----------------------------------------------------------------------===//

#include "HaydnBundle.h"
#include "HaydnBundleMaterialize.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::Haydn;
using namespace llvm::haydn::bundle;

namespace {

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

static SmallVector<unsigned, 16> flatten(ArrayRef<OpcodeCycle> Cycles) {
  SmallVector<unsigned, 16> Flat;
  for (const OpcodeCycle &C : Cycles)
    Flat.append(C.Opcodes.begin(), C.Opcodes.end());
  return Flat;
}

/// Assert full split contract for one input sequence.
static void expectValidSplit(ArrayRef<unsigned> Ops, HaydnMCFormats &Fmts,
                             unsigned MinCycles = 1,
                             unsigned MaxCycles = 16) {
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  if (Ops.empty()) {
    EXPECT_TRUE(Cycles.empty());
    return;
  }
  EXPECT_GE(Cycles.size(), MinCycles);
  EXPECT_LE(Cycles.size(), MaxCycles);

  // (1) Partition.
  auto Flat = flatten(Cycles);
  ASSERT_EQ(Flat.size(), Ops.size());
  for (size_t I = 0; I < Ops.size(); ++I)
    EXPECT_EQ(Flat[I], Ops[I]) << "order/coverage break at index " << I;

  // (2)(3) Product plan + capacity.
  for (size_t Ci = 0; Ci < Cycles.size(); ++Ci) {
    const OpcodeCycle &C = Cycles[Ci];
    EXPECT_FALSE(C.Opcodes.empty()) << "empty sub-cycle " << Ci;
    EXPECT_LE(C.Opcodes.size(), 3u) << "overfull sub-cycle " << Ci;
    EXPECT_TRUE(C.Plan.isProductLegal()) << "illegal plan sub-cycle " << Ci;
    EXPECT_TRUE(isProductBundleRow(C.Plan.Row));
    EXPECT_EQ(C.Plan.Bytes, productParcelBytes());
    EXPECT_EQ(C.Plan.Bytes.Value, productParcelBytes().Value);
    EXPECT_EQ(C.Plan.Cycles.Value, 1u);
    EXPECT_EQ(C.Plan.memberCount(), C.Opcodes.size());
  }

  // (4) Each sub-cycle packs alone without further split.
  for (size_t Ci = 0; Ci < Cycles.size(); ++Ci) {
    EXPECT_TRUE(opcodesFormOneLegalCycle(Cycles[Ci].Opcodes, Fmts))
        << "sub-cycle " << Ci << " is not self-legal";
    // Bundle canAdd/add mirror.
    Bundle<MCInst> B(&Fmts);
    SmallVector<MCInst, 3> Storage;
    for (unsigned Opc : Cycles[Ci].Opcodes) {
      Storage.emplace_back();
      Storage.back().setOpcode(Opc);
      ASSERT_TRUE(B.canAdd(Opc)) << "sub-cycle " << Ci << " canAdd fail";
      B.add(&Storage.back());
    }
    if (B.getOccupiedSlots() != 0)
      EXPECT_TRUE(B.hasValidFormat());
  }

  // (5) One-cycle predicate consistency.
  bool One = opcodesFormOneLegalCycle(Ops, Fmts);
  EXPECT_EQ(One, Cycles.size() == 1u && Cycles[0].Opcodes.size() == Ops.size());
}

//===----------------------------------------------------------------------===//
// Smoke / baseline
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, EmptyInput) {
  HaydnMCFormats Fmts;
  expectValidSplit({}, Fmts, /*Min=*/0, /*Max=*/0);
}

TEST(HaydnBundleMaterializeTest, SingleOpOneCycle) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ADD32};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, ThreeAluOneCycle) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32};
  expectValidSplit(Ops, Fmts, 1, 1);
}

//===----------------------------------------------------------------------===//
// Slot-conflict forced splits (S0-only stores)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, TwoStoresSplitToTwoCycles) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::ST32};
  expectValidSplit(Ops, Fmts, 2, 2);
}

TEST(HaydnBundleMaterializeTest, ThreeStoresSplitToThreeCycles) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::ST32, Haydn::ST32};
  expectValidSplit(Ops, Fmts, 3, 3);
}

TEST(HaydnBundleMaterializeTest, StoreAluStoreSplitsThird) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::ADD32, Haydn::ST32};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  expectValidSplit(Ops, Fmts, 2, 2);
  ASSERT_EQ(Cycles.size(), 2u);
  EXPECT_EQ(Cycles[0].Opcodes.size(), 2u);
  EXPECT_EQ(Cycles[1].Opcodes.size(), 1u);
}

TEST(HaydnBundleMaterializeTest, DualST64AlsoSplits) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST64, Haydn::ST64};
  expectValidSplit(Ops, Fmts, 2, 2);
}

TEST(HaydnBundleMaterializeTest, ST32ThenST64Split) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::ST64};
  expectValidSplit(Ops, Fmts, 2, 2);
}

//===----------------------------------------------------------------------===//
// Dual-load / load+mac / ALU64 packing (must stay one cycle when legal)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, DualLoadOneCycle) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::LD32, Haydn::LD32};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, DualLD64OneCycle) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::LD64, Haydn::LD64};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, TripleLoadSplitsThird) {
  // LD is S0|S1 only — third load cannot pack.
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::LD32, Haydn::LD32, Haydn::LD32};
  expectValidSplit(Ops, Fmts, 2, 2);
}

TEST(HaydnBundleMaterializeTest, LoadMacOneCycle) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::LD32, Haydn::X2MULA32};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, LoadMacAluOneCycle) {
  // Classic DSP fill: LD (S0|S1) + MAC (S1|S2) + ALU (any free).
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::LD32, Haydn::X2MULA32, Haydn::ADD32};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, StoreAndAlu64OneCycle) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::ADD64};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, DualAlu64OneCycle) {
  // ADD64 is S1|S2 — two fit, third must split.
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ADD64, Haydn::SLL64};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, TripleAlu64SplitsThird) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ADD64, Haydn::SLL64, Haydn::MAX64};
  expectValidSplit(Ops, Fmts, 2, 2);
}

//===----------------------------------------------------------------------===//
// Issue-cap splits
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, FourAluSplitToTwoCycles) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32, Haydn::ADD32};
  expectValidSplit(Ops, Fmts, 2, 2);
}

TEST(HaydnBundleMaterializeTest, SixAluSplitToTwoFullCycles) {
  HaydnMCFormats Fmts;
  // Six E2+E3 ALU logicals pack 3+3. ADDI32 is Format E E2-only and cannot
  // join a 3-wide E3 cycle (HaydnAlternateDescriptors.cpp E2-only list).
  unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32,
                    Haydn::SUB32, Haydn::NEG32, Haydn::OR32};
  expectValidSplit(Ops, Fmts, 2, 2);
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  ASSERT_EQ(Cycles.size(), 2u);
  EXPECT_EQ(Cycles[0].Opcodes.size(), 3u);
  EXPECT_EQ(Cycles[1].Opcodes.size(), 3u);
}

TEST(HaydnBundleMaterializeTest, SevenAluSplitToThreeCycles) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32, Haydn::SUB32,
                    Haydn::NEG32,  Haydn::ADDI32, Haydn::ADD32};
  expectValidSplit(Ops, Fmts, 3, 3);
}

//===----------------------------------------------------------------------===//
// Mixed / stress sequences
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, SplitPreservesOrderAndCoverage) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::ST32, Haydn::ADD32, Haydn::ST32};
  expectValidSplit(Ops, Fmts, 3, 3);
}

TEST(HaydnBundleMaterializeTest, StoreLoadMacPreferPack) {
  // ST S0 + LD may fight for S0|S1 — greedy may pack ST+LD or ST alone.
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::LD32, Haydn::X2MULA32};
  expectValidSplit(Ops, Fmts, 1, 2);
}

TEST(HaydnBundleMaterializeTest, AlternatingStoreAlu) {
  // ST, ALU, ST, ALU, ST — each ST needs S0; ALU can ride with one ST.
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::ADD32, Haydn::ST32, Haydn::XOR32,
                    Haydn::ST32};
  expectValidSplit(Ops, Fmts, 3, 3);
}

TEST(HaydnBundleMaterializeTest, DualMacOneCycle) {
  // MAC is S1|S2 — dual MAC packs; third splits.
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::X2MULA32, Haydn::X2MULA32};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, TripleMacSplitsThird) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::X2MULA32, Haydn::X2MULA32, Haydn::X2MULA32};
  expectValidSplit(Ops, Fmts, 2, 2);
}

//===----------------------------------------------------------------------===//
// ARCTAN / SIN_COS encode slots (issue-alone is schedule HR; encode may
// still have multi-slot alts-derived legality — split only if slots conflict)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, ArctanAloneOneCycle) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ARCTAN};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, SinCosAloneOneCycle) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::SIN_COS};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, ArctanWithAluEncodeLegality) {
  // Encode oracle: if alts-derived getLegalSlots / PlacementAlternative
  // FieldSlots allow ARCTAN + ADD32 on disjoint slots, one cycle; else split.
  // HR alone-in-bundle is a separate schedule authority
  // (MIR postmisched-arctan-locked-slot). Here we only pin partition safety.
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ARCTAN, Haydn::ADD32};
  expectValidSplit(Ops, Fmts, 1, 2);
}

//===----------------------------------------------------------------------===//
// Exhaustive short sequences over a FU palette
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, ExhaustivePairsPartitionSafe) {
  HaydnMCFormats Fmts;
  const unsigned Palette[] = {
      Haydn::ADD32, Haydn::XOR32,  Haydn::ST32,    Haydn::LD32,
      Haydn::ADD64, Haydn::X2MULA32, Haydn::ST64,  Haydn::LD64,
      Haydn::NOT32, Haydn::ADDI32, Haydn::SLL64};
  for (unsigned A : Palette) {
    for (unsigned B : Palette) {
      unsigned Ops[] = {A, B};
      expectValidSplit(Ops, Fmts, 1, 2);
    }
  }
}

TEST(HaydnBundleMaterializeTest, ExhaustiveTriplesSelectedPartitionSafe) {
  // Full 11^3 is large but cheap; keep all for regression density.
  HaydnMCFormats Fmts;
  const unsigned Palette[] = {Haydn::ADD32, Haydn::ST32, Haydn::LD32,
                              Haydn::ADD64, Haydn::X2MULA32, Haydn::NOT32};
  for (unsigned A : Palette) {
    for (unsigned B : Palette) {
      for (unsigned C : Palette) {
        unsigned Ops[] = {A, B, C};
        expectValidSplit(Ops, Fmts, 1, 3);
      }
    }
  }
}

TEST(HaydnBundleMaterializeTest, SingletonAndEmptyFormOneCyclePredicate) {
  HaydnMCFormats Fmts;
  EXPECT_FALSE(opcodesFormOneLegalCycle({}, Fmts));
  unsigned One[] = {Haydn::ADD32};
  EXPECT_TRUE(opcodesFormOneLegalCycle(One, Fmts));
  unsigned Four[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
  EXPECT_FALSE(opcodesFormOneLegalCycle(Four, Fmts));
}

//===----------------------------------------------------------------------===//
// Plan occupancy on packed cycles
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, FullThreeSlotPlanOccupancy) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  ASSERT_EQ(Cycles.size(), 1u);
  EXPECT_EQ(Cycles[0].Plan.OccupiedSlots,
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
}

TEST(HaydnBundleMaterializeTest, DualLoadPlanOccupancyIsS0S1) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::LD32, Haydn::LD32};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  ASSERT_EQ(Cycles.size(), 1u);
  // Bundle prefers high slots first for multi-slot, but LD only S0|S1 → both.
  EXPECT_EQ(Cycles[0].Plan.OccupiedSlots,
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1));
}

TEST(HaydnBundleMaterializeTest, IdempotentResplitOfSubcycles) {
  // Resplitting each output cycle must yield exactly one cycle (fixed point).
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::ADD32, Haydn::ST32, Haydn::XOR32,
                    Haydn::LD32,  Haydn::X2MULA32, Haydn::ST32};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  for (const OpcodeCycle &C : Cycles) {
    auto Again = greedySplitLegalOpcodeCycles(C.Opcodes, Fmts);
    ASSERT_EQ(Again.size(), 1u);
    EXPECT_EQ(Again[0].Opcodes.size(), C.Opcodes.size());
  }
}

//===----------------------------------------------------------------------===//
// exact no-split production surface
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, ExactSolveThreeAluOneCycle) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32};
  auto Exact = exactSolveProductOpcodes(Ops, Fmts);
  ASSERT_TRUE(Exact.has_value());
  EXPECT_EQ(Exact->LogicalOpcodes.size(), 3u);
  EXPECT_EQ(Exact->MemberOpcodes.size(), 3u);
  EXPECT_TRUE(Exact->Plan.isProductLegal());
  EXPECT_TRUE(isProductBundleRow(Exact->Plan.Row));
  EXPECT_EQ(Exact->Plan.Bytes, productParcelBytes());
  EXPECT_EQ(Exact->Plan.Bytes.Value, productParcelBytes().Value);
  // Preferred members are distinct fields (S2/S1/S0).
  EXPECT_EQ(Exact->State.Members.size(), 3u);
  EXPECT_TRUE(opcodesFormOneLegalCycle(Ops, Fmts));
}

TEST(HaydnBundleMaterializeTest, ExactSolveDualLoadOneCycle) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::LD32, Haydn::LD32};
  auto Exact = exactSolveProductOpcodes(Ops, Fmts);
  ASSERT_TRUE(Exact.has_value());
  EXPECT_EQ(Exact->MemberOpcodes.size(), 2u);
  EXPECT_EQ(Exact->State.OccupiedSlots,
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1));
}

TEST(HaydnBundleMaterializeTest, ExactSolveDualStoreFailsClosed) {
  // Two S0-only stores cannot share one parcel — exact solve nullopt
  // (no production split repair).
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::ST32};
  EXPECT_FALSE(exactSolveProductOpcodes(Ops, Fmts).has_value());
  EXPECT_FALSE(exactPackOneOpcodeCycle(Ops, Fmts).has_value());
  EXPECT_FALSE(opcodesFormOneLegalCycle(Ops, Fmts));
  // Diagnostic greedy still partitions for ResMII / unit analysis.
  auto Split = greedySplitLegalOpcodeCycles(Ops, Fmts);
  EXPECT_EQ(Split.size(), 2u);
}

TEST(HaydnBundleMaterializeTest, ExactSolveLaneStoreAndST8FailsClosed) {
  // D_SW_L_WITH_IMM + OR64 + ST8 is the libc bf16mull illegal residual.
  // FieldSlots would accept S2+S1+S0; unit injectivity must refuse.
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::D_SW_L_WITH_IMM, Haydn::OR64, Haydn::ST8};
  EXPECT_FALSE(opcodesHaveFormatEUnitCover(Ops));
  EXPECT_FALSE(exactSolveProductOpcodes(Ops, Fmts).has_value());
  EXPECT_FALSE(exactPackOneOpcodeCycle(Ops, Fmts).has_value());
  EXPECT_FALSE(opcodesFormOneLegalCycle(Ops, Fmts));
}

TEST(HaydnBundleMaterializeTest, ExactSolveOverwidthFailsClosed) {
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
  EXPECT_FALSE(exactSolveProductOpcodes(Ops, Fmts).has_value());
  EXPECT_FALSE(exactPackOneOpcodeCycle(Ops, Fmts).has_value());
  EXPECT_FALSE(opcodesFormOneLegalCycle(Ops, Fmts));
}

TEST(HaydnBundleMaterializeTest, ExactPackOneMatchesGreedySingletonCycles) {
  // Every greedy sub-cycle must exact-pack as one cycle (fixed point).
  HaydnMCFormats Fmts;
  unsigned Ops[] = {Haydn::ST32, Haydn::ADD32, Haydn::ST32, Haydn::XOR32};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  for (const OpcodeCycle &C : Cycles) {
    auto Packed = exactPackOneOpcodeCycle(C.Opcodes, Fmts);
    ASSERT_TRUE(Packed.has_value());
    EXPECT_EQ(Packed->Opcodes.size(), C.Opcodes.size());
    EXPECT_TRUE(Packed->Plan.isProductLegal());
  }
}

TEST(HaydnBundleMaterializeTest, ExactSolveEmptyIsNullopt) {
  HaydnMCFormats Fmts;
  EXPECT_FALSE(exactSolveProductOpcodes({}, Fmts).has_value());
  EXPECT_FALSE(exactPackOneOpcodeCycle({}, Fmts).has_value());
  EXPECT_FALSE(opcodesFormOneLegalCycle({}, Fmts));
  // MI-list view: empty / over-width reject (no fake MIs required).
  EXPECT_FALSE(instrsFormOneLegalCycle({}, Fmts));
  EXPECT_FALSE(instrsFormOneLegalCycle(ArrayRef<MachineInstr *>(), Fmts));
}

// Materialize BundlePlan.Bytes come from generated VLIWFormat::Size
// (planFromPacketFormats), not a hard-coded test plan.
TEST(HaydnBundleMaterializeTest, PlanBytesFromGeneratedFullSize) {
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  const VLIWFormat *Full = productVLIWFormat(Packets);
  ASSERT_NE(Full, nullptr);
  EncodedBytes Size = vliwFormatSizeAsBytes(Full->getSize());
  EXPECT_EQ(Size.Value, productParcelBytes().Value);

  unsigned Ops[] = {Haydn::ADD32, Haydn::LD32};
  auto Exact = exactSolveProductOpcodes(Ops, Fmts);
  ASSERT_TRUE(Exact.has_value());
  EXPECT_EQ(Exact->Plan.Bytes, Size);
  EXPECT_TRUE(Exact->Plan.isProductLegal());

  auto Packed = exactPackOneOpcodeCycle(Ops, Fmts);
  ASSERT_TRUE(Packed.has_value());
  EXPECT_EQ(Packed->Plan.Bytes, Size);

  auto Late = commitLateProductCycle(Haydn::ADD32, Fmts);
  ASSERT_TRUE(Late.has_value());
  EXPECT_EQ(Late->Plan.Bytes, Size);
}

// Fail-closed production plan authority: every materialize surface that
// yields a BundlePlan derives EncodedBytes from the same generated Full
// row as planFromPacketFormats. No hard test-plan rebuild.
TEST(HaydnBundleMaterializeTest, FailClosedPlanAuthorityFromGeneratedFull) {
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  const VLIWFormat *Full = productVLIWFormat(Packets);
  ASSERT_NE(Full, nullptr);
  EXPECT_TRUE(StringRef(Full->Name).starts_with("BUNDLE_E96_"));
  EncodedBytes Size = vliwFormatSizeAsBytes(Full->getSize());

  unsigned Multi[] = {Haydn::ADD32, Haydn::LD32};
  auto Exact = exactSolveProductOpcodes(Multi, Fmts);
  ASSERT_TRUE(Exact.has_value());
  auto FromTable = planFromPacketFormats(Packets, Exact->Plan.OccupiedSlots,
                                         Exact->Plan.MemberOpcodes);
  ASSERT_TRUE(FromTable.has_value());
  EXPECT_EQ(Exact->Plan.Bytes, FromTable->Bytes);
  EXPECT_EQ(Exact->Plan.Bytes, Size);
  EXPECT_TRUE(isProductBundleRow(Exact->Plan.Row));
  EXPECT_EQ(Exact->Plan.Bytes, productParcelBytes());

  auto Packed = exactPackOneOpcodeCycle(Multi, Fmts);
  ASSERT_TRUE(Packed.has_value());
  EXPECT_EQ(Packed->Plan.Bytes, Size);
  EXPECT_TRUE(isProductBundleRow(Packed->Plan.Row));
  EXPECT_EQ(Packed->Plan.Bytes, productParcelBytes());

  // Diagnostic greedy still pins Full Size (no hard rebuild).
  unsigned SplitOps[] = {Haydn::ST32, Haydn::ADD32, Haydn::ST32, Haydn::XOR32};
  auto Cycles = greedySplitLegalOpcodeCycles(SplitOps, Fmts);
  ASSERT_FALSE(Cycles.empty());
  for (const OpcodeCycle &C : Cycles) {
    auto P = planFromPacketFormats(Packets, C.Plan.OccupiedSlots, C.Opcodes);
    ASSERT_TRUE(P.has_value());
    EXPECT_EQ(C.Plan.Bytes, P->Bytes);
    EXPECT_TRUE(isProductBundleRow(C.Plan.Row));
    EXPECT_EQ(C.Plan.Bytes, productParcelBytes());
    EXPECT_TRUE(C.Plan.isProductLegal());
  }

  // Late bare MI: product singleton from generated Full only.
  for (unsigned Opc : {Haydn::ADD32, Haydn::NOP, Haydn::BNEZ_W}) {
    auto Late = commitLateProductCycle(Opc, Fmts);
    ASSERT_TRUE(Late.has_value()) << "opc=" << Opc;
    EXPECT_EQ(Late->Plan.Bytes, Size) << "opc=" << Opc;
    EXPECT_TRUE(isProductBundleRow(Late->Plan.Row));
    EXPECT_EQ(Late->Plan.Bytes, productParcelBytes());
    EXPECT_TRUE(Late->Plan.isProductLegal());
  }

  // Tests-only convenience agrees under product row freeze but is not authority.
  BundlePlan Hand = makeProductPlan(Haydn::SLOT0, {Haydn::ADD32});
  EXPECT_EQ(Hand.Bytes, Size);
}

//===----------------------------------------------------------------------===//
// bounded ready-subset cycle auction
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, AuctionEmptyIsNullopt) {
  HaydnMCFormats Fmts;
  EXPECT_FALSE(auctionReadySubsetCycle({}, {}, Fmts).has_value());
}

TEST(HaydnBundleMaterializeTest, AuctionThreeAluFillsOneCycle) {
  HaydnMCFormats Fmts;
  unsigned Ready[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32};
  auto A = auctionReadySubsetCycle({}, Ready, Fmts);
  ASSERT_TRUE(A.has_value());
  EXPECT_EQ(A->IssuedCount, 3u);
  EXPECT_EQ(A->ReadyIndices.size(), 3u);
  EXPECT_EQ(A->CycleOpcodes.size(), 3u);
  EXPECT_TRUE(opcodesFormOneLegalCycle(A->CycleOpcodes, Fmts));
  EXPECT_TRUE(A->Exact.has_value());
}

TEST(HaydnBundleMaterializeTest, AuctionDualStorePicksSingleton) {
  // Two S0-only stores cannot co-issue — densest legal fill is size 1.
  HaydnMCFormats Fmts;
  unsigned Ready[] = {Haydn::ST32, Haydn::ST32};
  auto A = auctionReadySubsetCycle({}, Ready, Fmts);
  ASSERT_TRUE(A.has_value());
  EXPECT_EQ(A->IssuedCount, 1u);
  EXPECT_EQ(A->ReadyIndices.size(), 1u);
  // MustInclude index 0: focus first store alone (cannot take both).
  auto Focus = auctionReadySubsetCycle({}, Ready, Fmts, /*MustInclude=*/0u);
  ASSERT_TRUE(Focus.has_value());
  EXPECT_EQ(Focus->IssuedCount, 1u);
  EXPECT_TRUE(llvm::is_contained(Focus->ReadyIndices, 0u));
}

TEST(HaydnBundleMaterializeTest, AuctionBasePlusReadyCompletesCycle) {
  // Base already holds one store; ready has ALU + second store — pick ALU.
  HaydnMCFormats Fmts;
  unsigned Base[] = {Haydn::ST32};
  unsigned Ready[] = {Haydn::ST32, Haydn::ADD32};
  auto A = auctionReadySubsetCycle(Base, Ready, Fmts);
  ASSERT_TRUE(A.has_value());
  EXPECT_EQ(A->IssuedCount, 2u);
  EXPECT_EQ(A->CycleOpcodes[0], Haydn::ST32);
  EXPECT_TRUE(llvm::is_contained(A->CycleOpcodes, Haydn::ADD32));
  EXPECT_FALSE(llvm::count(A->CycleOpcodes, Haydn::ST32) > 1)
      << "must not co-issue dual store with base ST32";
}

TEST(HaydnBundleMaterializeTest, AuctionThreeReadyRematchAdd32_2xAdd64) {
  // Preferred first-fit can starve the triple; exact rematch packs all three.
  // Selected MemberOpcodes match exactSolveProductOpcodes (setDesc targets).
  HaydnMCFormats Fmts;
  unsigned Ready[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
  auto A = auctionReadySubsetCycle({}, Ready, Fmts);
  ASSERT_TRUE(A.has_value());
  EXPECT_EQ(A->IssuedCount, 3u) << "exact rematch must fill one cycle";
  EXPECT_TRUE(opcodesFormOneLegalCycle(A->CycleOpcodes, Fmts));
  EXPECT_EQ(auctionFocusFillScore({}, Ready, Fmts), 3u);
  auto Solved = exactSolveProductOpcodes(A->CycleOpcodes, Fmts);
  ASSERT_TRUE(Solved.has_value());
  ASSERT_EQ(Solved->MemberOpcodes.size(), 3u);
  // Auction may reorder ready indices; occupancy is residual S0/S1/S2,
  // setDesc targets are Format E members at those entries.
  unsigned SawAdd32E0 = 0, SawAdd64E1 = 0, SawAdd64E2 = 0;
  for (unsigned Opc : Solved->MemberOpcodes) {
    const StringRef Name = haydnOpcodeName(Opc);
    EXPECT_TRUE(Name.contains("_E2_") || Name.contains("_E3_")) << Name;
    if (Name.starts_with("ADD32_") && Name.contains("_E0_"))
      ++SawAdd32E0;
    else if (Name.starts_with("ADD64_") && Name.contains("_E1_"))
      ++SawAdd64E1;
    else if (Name.starts_with("ADD64_") && Name.contains("_E2_"))
      ++SawAdd64E2;
    else
      ADD_FAILURE() << "unexpected member " << Name;
  }
  EXPECT_EQ(SawAdd32E0, 1u);
  EXPECT_EQ(SawAdd64E1, 1u);
  EXPECT_EQ(SawAdd64E2, 1u);
}

TEST(HaydnBundleMaterializeTest, AuctionFocusPrefersDenserPartner) {
  // Focus ADD32 with ready ST32 + XOR32 + NOT32: densest includes focus + 2 ALU.
  HaydnMCFormats Fmts;
  unsigned Ready[] = {Haydn::ADD32, Haydn::ST32, Haydn::XOR32, Haydn::NOT32};
  EXPECT_EQ(auctionFocusFillScore({}, Ready, Fmts), 3u);
  auto A = auctionReadySubsetCycle({}, Ready, Fmts, /*MustInclude=*/0u);
  ASSERT_TRUE(A.has_value());
  EXPECT_EQ(A->IssuedCount, 3u);
  EXPECT_TRUE(llvm::is_contained(A->ReadyIndices, 0u));
}

TEST(HaydnBundleMaterializeTest, AuctionOverwidthBaseNullopt) {
  HaydnMCFormats Fmts;
  unsigned Base[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32, Haydn::SUB32};
  unsigned Ready[] = {Haydn::ADD32};
  EXPECT_FALSE(auctionReadySubsetCycle(Base, Ready, Fmts).has_value());
}

} // namespace

//===----------------------------------------------------------------------===//
// score-only auction twin equivalence (CB-153a)
//===----------------------------------------------------------------------===//

// auctionFocusFillScoreOnly exists so post-RA tryCandidate ranking does not
// pay the full auction's per-order exact-rematch (which the score never
// reads). Its contract is VALUE EQUALITY with auctionFocusFillScore on every
// input; this test enforces that over a systematic sweep of real opcodes
// covering ALU / E2-only immediate / load / store / MAC / 64-bit families,
// with and without base members, so any future drift between the twins (or
// an order-sensitivity change in the legality oracle that breaks the twin's
// single-walk assumption) fails here rather than as a silent scheduling
// change.
TEST(HaydnBundleMaterializeTest, ScoreOnlyTwinMatchesFullAuction) {
  HaydnMCFormats Fmts;
  const unsigned Pool[] = {Haydn::ADD32,  Haydn::XOR32, Haydn::ADDI32,
                           Haydn::LD32,   Haydn::LD64,  Haydn::ST32,
                           Haydn::ST64,   Haydn::X2MULA32};
  const unsigned N = std::size(Pool);

  auto check = [&](ArrayRef<unsigned> Base, ArrayRef<unsigned> Ready) {
    AuctionAnyOrderLegalMemo Memo;
    const unsigned Full = auctionFocusFillScore(Base, Ready, Fmts);
    const unsigned Lean = auctionFocusFillScoreOnly(Base, Ready, Fmts);
    const unsigned LeanMemo =
        auctionFocusFillScoreOnly(Base, Ready, Fmts, &Memo);
    // Memoized twice: second pass must serve from the memo identically.
    const unsigned LeanMemo2 =
        auctionFocusFillScoreOnly(Base, Ready, Fmts, &Memo);
    EXPECT_EQ(Full, Lean) << "twin drift (no memo)";
    EXPECT_EQ(Full, LeanMemo) << "twin drift (memo cold)";
    EXPECT_EQ(Full, LeanMemo2) << "twin drift (memo warm)";
  };

  // Ready singles, pairs, triples over the pool (with repetition), base empty.
  for (unsigned A = 0; A < N; ++A) {
    check({}, {Pool[A]});
    for (unsigned B = 0; B < N; ++B) {
      unsigned R2[] = {Pool[A], Pool[B]};
      check({}, R2);
      for (unsigned C = 0; C < N; ++C) {
        unsigned R3[] = {Pool[A], Pool[B], Pool[C]};
        check({}, R3);
      }
    }
  }

  // One base member from each family, ready pairs.
  for (unsigned Bi = 0; Bi < N; ++Bi)
    for (unsigned A = 0; A < N; ++A)
      for (unsigned B = 0; B < N; ++B) {
        unsigned Base1[] = {Pool[Bi]};
        unsigned R2[] = {Pool[A], Pool[B]};
        check(Base1, R2);
      }

  // Two base members (cycle nearly full), ready singles.
  {
    unsigned Base2[] = {Haydn::LD32, Haydn::X2MULA32};
    for (unsigned A = 0; A < N; ++A)
      check(Base2, {Pool[A]});
  }

  // Wide ready list (> cap, mixed families) exercising the 8-entry bound.
  {
    unsigned Wide[] = {Haydn::ST32, Haydn::ADD32,    Haydn::ADDI32,
                       Haydn::LD32, Haydn::X2MULA32, Haydn::ST64,
                       Haydn::LD64, Haydn::XOR32};
    check({}, Wide);
    unsigned Base1[] = {Haydn::ST32};
    check(Base1, Wide);
  }
}
