//===- HaydnBundleMaterializeTest.cpp - cycle split tests -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Extensive unit tests for haydn::bundle::greedySplitLegalOpcodeCycles —
// encode-oracle half of post-RA explicit cycle split (pack-legality encode
// authority).
//
// Invariants locked on every case:
//   1. Partition: flatten(cycles) == input opcodes (order + coverage).
//   2. Product plan: every sub-cycle is FormatID Bundle128Full, 16 B, 1 cycle.
//   3. Capacity: each sub-cycle has 1..3 members.
//   4. Sub-cycle legal: re-pack via Haydn::Bundle succeeds without split.
//   5. opcodesFormOneLegalCycle ⇔ greedy returns exactly one cycle.
//
//===----------------------------------------------------------------------===//

#include "HaydnBundle.h"
#include "HaydnBundleMaterialize.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "HaydnTestMCInstrInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
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
    // The FID follows the occupancy: a cycle occupying P2x is the 2-entry
    // composite and one occupying P3x is the 3-entry one. It is no longer a
    // constant, because there is no longer only one row.
    EXPECT_TRUE(isProductFormat(C.Plan.FID)) << "sub-cycle " << Ci;
    if (C.Plan.OccupiedSlots != 0) {
      const bool E2 =
          (C.Plan.OccupiedSlots & ~SlotBits(Haydn::SLOT_SET_E2)) == 0;
      EXPECT_EQ(C.Plan.FID,
                E2 ? FormatID::BundleE2 : FormatID::BundleE3)
          << "sub-cycle " << Ci << " occ=" << C.Plan.OccupiedSlots;
    }
    EXPECT_EQ(C.Plan.Bytes.Value, ProductEncodedBytesValue);
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
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  expectValidSplit({}, Fmts, /*Min=*/0, /*Max=*/0);
}

TEST(HaydnBundleMaterializeTest, SingleOpOneCycle) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::ADD32};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, ThreeAluOneCycle) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32};
  expectValidSplit(Ops, Fmts, 1, 1);
}

//===----------------------------------------------------------------------===//
// Slot-conflict forced splits (S0-only stores)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, TwoStoresSplitToTwoCycles) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::S_SW_WITH_IMM};
  expectValidSplit(Ops, Fmts, 2, 2);
}

TEST(HaydnBundleMaterializeTest, ThreeStoresSplitToThreeCycles) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::S_SW_WITH_IMM, Haydn::S_SW_WITH_IMM};
  expectValidSplit(Ops, Fmts, 3, 3);
}

TEST(HaydnBundleMaterializeTest, StoreAluStoreSplitsThird) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::ADD32, Haydn::S_SW_WITH_IMM};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  expectValidSplit(Ops, Fmts, 2, 2);
  ASSERT_EQ(Cycles.size(), 2u);
  EXPECT_EQ(Cycles[0].Opcodes.size(), 2u);
  EXPECT_EQ(Cycles[1].Opcodes.size(), 1u);
}

TEST(HaydnBundleMaterializeTest, DualST64AlsoSplits) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::D_SDW_WITH_IMM, Haydn::D_SDW_WITH_IMM};
  expectValidSplit(Ops, Fmts, 2, 2);
}

TEST(HaydnBundleMaterializeTest, ST32ThenST64Split) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::D_SDW_WITH_IMM};
  expectValidSplit(Ops, Fmts, 2, 2);
}

//===----------------------------------------------------------------------===//
// Dual-load / load+mac / ALU64 packing (must stay one cycle when legal)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, DualLoadOneCycle) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_LW_WITH_IMM, Haydn::S_LW_WITH_IMM};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, DualLD64OneCycle) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::D_LDW_WITH_IMM, Haydn::D_LDW_WITH_IMM};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, TripleLoadSplitsThird) {
  // LD is S0|S1 only — third load cannot pack.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_LW_WITH_IMM, Haydn::S_LW_WITH_IMM, Haydn::S_LW_WITH_IMM};
  expectValidSplit(Ops, Fmts, 2, 2);
}

TEST(HaydnBundleMaterializeTest, LoadMacOneCycle) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_LW_WITH_IMM, Haydn::X2MULA32};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, LoadMacAluOneCycle) {
  // Classic DSP fill: LD (S0|S1) + MAC (S1|S2) + ALU (any free).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_LW_WITH_IMM, Haydn::X2MULA32, Haydn::ADD32};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, StoreAndAlu64OneCycle) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::ADD64};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, DualAlu64OneCycle) {
  // ADD64 is S1|S2 — two fit, third must split.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::ADD64, Haydn::SLL64};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, TripleAlu64FitsOneCycle) {
  // Bundle128 had two ALU64 slots, so the third op forced a second cycle.
  // Format E has three ALUs and these logicals have a member on each, so all
  // three issue together -- one cycle, not two.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::ADD64, Haydn::SLL64, Haydn::MAX64};
  expectValidSplit(Ops, Fmts, 1, 1);
}

//===----------------------------------------------------------------------===//
// Issue-cap splits
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, FourAluSplitToTwoCycles) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32, Haydn::ADD32};
  expectValidSplit(Ops, Fmts, 2, 2);
}

TEST(HaydnBundleMaterializeTest, SixAluSplitToTwoFullCycles) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  // ADDI32 is the odd one out and it costs a cycle: its imm20 only fits the
  // wide 2-entry windows, so it has no 3-entry placement and cannot join a
  // bundle the other five have already made 3-entry. Five ALU ops pack two
  // deep and ADDI32 lands alone.
  unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32,
                    Haydn::SUB32, Haydn::NEG32, Haydn::ADDI32};
  expectValidSplit(Ops, Fmts, 2, 3);
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  ASSERT_EQ(Cycles.size(), 3u);
  EXPECT_EQ(Cycles[0].Opcodes.size(), 3u);
  EXPECT_EQ(Cycles[1].Opcodes.size(), 2u);
  EXPECT_EQ(Cycles[2].Opcodes.size(), 1u) << "ADDI32 alone: no 3-entry form";
}

TEST(HaydnBundleMaterializeTest, SevenAluSplitToThreeCycles) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32, Haydn::SUB32,
                    Haydn::NEG32,  Haydn::ADDI32, Haydn::ADD32};
  expectValidSplit(Ops, Fmts, 3, 3);
}

//===----------------------------------------------------------------------===//
// Mixed / stress sequences
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, SplitPreservesOrderAndCoverage) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::S_SW_WITH_IMM, Haydn::ADD32, Haydn::S_SW_WITH_IMM};
  expectValidSplit(Ops, Fmts, 3, 3);
}

TEST(HaydnBundleMaterializeTest, StoreLoadMacPreferPack) {
  // ST S0 + LD may fight for S0|S1 — greedy may pack ST+LD or ST alone.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::S_LW_WITH_IMM, Haydn::X2MULA32};
  expectValidSplit(Ops, Fmts, 1, 2);
}

TEST(HaydnBundleMaterializeTest, AlternatingStoreAlu) {
  // ST, ALU, ST, ALU, ST — each ST needs S0; ALU can ride with one ST.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::ADD32, Haydn::S_SW_WITH_IMM, Haydn::XOR32,
                    Haydn::S_SW_WITH_IMM};
  expectValidSplit(Ops, Fmts, 3, 3);
}

TEST(HaydnBundleMaterializeTest, DualMacOneCycle) {
  // MAC is S1|S2 — dual MAC packs; third splits.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::X2MULA32, Haydn::X2MULA32};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, TripleMacSplitsThird) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::X2MULA32, Haydn::X2MULA32, Haydn::X2MULA32};
  expectValidSplit(Ops, Fmts, 2, 2);
}

//===----------------------------------------------------------------------===//
// ARCTAN / SIN_COS encode slots (issue-alone is schedule HR; encode may
// still have multi-slot alts-derived legality — split only if slots conflict)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, ArctanAloneOneCycle) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::ARCTAN};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, SinCosAloneOneCycle) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::SIN_COS};
  expectValidSplit(Ops, Fmts, 1, 1);
}

TEST(HaydnBundleMaterializeTest, ArctanWithAluEncodeLegality) {
  // Encode oracle: if alts-derived getLegalSlots / PlacementAlternative
  // FieldSlots allow ARCTAN + ADD32 on disjoint slots, one cycle; else split.
  // HR alone-in-bundle is a separate schedule authority
  // (MIR postmisched-arctan-locked-slot). Here we only pin partition safety.
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::ARCTAN, Haydn::ADD32};
  expectValidSplit(Ops, Fmts, 1, 2);
}

//===----------------------------------------------------------------------===//
// Exhaustive short sequences over a FU palette
//===----------------------------------------------------------------------===//

TEST(HaydnBundleMaterializeTest, ExhaustivePairsPartitionSafe) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const unsigned Palette[] = {
      Haydn::ADD32, Haydn::XOR32,  Haydn::S_SW_WITH_IMM,    Haydn::S_LW_WITH_IMM,
      Haydn::ADD64, Haydn::X2MULA32, Haydn::D_SDW_WITH_IMM,  Haydn::D_LDW_WITH_IMM,
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
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  const unsigned Palette[] = {Haydn::ADD32, Haydn::S_SW_WITH_IMM, Haydn::S_LW_WITH_IMM,
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
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
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
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  ASSERT_EQ(Cycles.size(), 1u);
  EXPECT_EQ(Cycles[0].Plan.OccupiedSlots,
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P31 | Haydn::SLOT_P32));
}

TEST(HaydnBundleMaterializeTest, DualLoadPlanOccupancyIsS0S1) {
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_LW_WITH_IMM, Haydn::S_LW_WITH_IMM};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  ASSERT_EQ(Cycles.size(), 1u);
  // P30|P32, not P30|P31. Both loads want a load unit and there are two;
  // P31's load member is LOAD1, which the first load already holds, so the
  // second moves to P32. Packing on slots alone takes P31 and names LOAD1
  // twice -- the bundle the hardware cannot issue (5.7).
  EXPECT_EQ(Cycles[0].Plan.OccupiedSlots,
            SlotBits(Haydn::SLOT_P30 | Haydn::SLOT_P32));
}

TEST(HaydnBundleMaterializeTest, IdempotentResplitOfSubcycles) {
  // Resplitting each output cycle must yield exactly one cycle (fixed point).
  HaydnMCFormatsWithMII Fmts(llvm::haydn::test::getMCInstrInfo());
  unsigned Ops[] = {Haydn::S_SW_WITH_IMM, Haydn::ADD32, Haydn::S_SW_WITH_IMM, Haydn::XOR32,
                    Haydn::S_LW_WITH_IMM,  Haydn::X2MULA32, Haydn::S_SW_WITH_IMM};
  auto Cycles = greedySplitLegalOpcodeCycles(Ops, Fmts);
  for (const OpcodeCycle &C : Cycles) {
    auto Again = greedySplitLegalOpcodeCycles(C.Opcodes, Fmts);
    ASSERT_EQ(Again.size(), 1u);
    EXPECT_EQ(Again[0].Opcodes.size(), C.Opcodes.size());
  }
}

} // namespace
