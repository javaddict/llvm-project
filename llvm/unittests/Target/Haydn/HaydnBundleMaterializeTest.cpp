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
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "gtest/gtest.h"

#include <memory>

// Opcode enums come via HaydnPortModel → HaydnMCTargetDesc (GET_INSTRINFO_ENUM).
// Do not re-include the enum; a second include conflicts.

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTarget();
extern "C" void LLVMInitializeHaydnTargetMC();

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

//===----------------------------------------------------------------------===//
// MAC twin coherent-row exact solve (quad-mac-coissue regression)
//===----------------------------------------------------------------------===//

// REGRESSION TEST (quad-mac-coissue): two same-logical MAC accumulators must
// exact-solve onto ONE coherent Format E row with distinct MAC units.
//
// Bug: canCoissueProductCycle / commitExactMultiMIProductCycle rejected a
// row-mixed member set (cycleHasMixedFormatEModes) BEFORE
// resolveMixedMemberCycleOnce could rematch it onto one row, so the four
// FMULAA32X16 of the quad-accumulator loop each landed in their own parcel.
// The rematcher peels members to logicals (productSolveLogicalOpcode) and
// exact-re-solves; this pins the solve half of that contract: the OUTPUT
// members must all carry one Mode (E2 or E3 row) and injective MAC units
// (MAC0 vs MAC1 — golden seven-unit injectivity law).
TEST(HaydnBundleMaterializeTest, ExactSolveDualMacTwinCoherentRow) {
  HaydnMCFormats Fmts;
  // Two FMULAA32X16_H1_L0 accumulators (the quad4acc.c shape).
  unsigned Ops[] = {Haydn::FMULAA32X16_H1_L0, Haydn::FMULAA32X16_H1_L0};
  auto Exact = exactSolveProductOpcodes(Ops, Fmts);
  ASSERT_TRUE(Exact.has_value());
  ASSERT_EQ(Exact->MemberOpcodes.size(), 2u);
  EXPECT_TRUE(Exact->Plan.isProductLegal());

  // Coherence: every member sits on the SAME row as the plan (CB-153b law).
  const bool PlanE3 = Exact->Plan.Row == BundleFormatRowID::E96ThreeEntry;
  for (unsigned M : Exact->MemberOpcodes) {
    const MCSlotKind Kind = Fmts.getSlotKind(M);
    if (PlanE3)
      EXPECT_TRUE(formatECompositeSlotIsE3(Kind));
    else
      EXPECT_TRUE(formatECompositeSlotIsE2(Kind));
  }

  // Unit injectivity under one row: peel names, the two members must seat on
  // distinct MAC units (MAC0 vs MAC1). Member names spell the unit:
  // FMULAA32X16_H1_L0_E2_E0_MAC0_RR vs ..._E2_E1_MAC1_RR (E2 row), or
  // E3_E1_MAC0 vs E3_E2_MAC1 (E3 row).
  SmallVector<StringRef, 2> Names;
  for (unsigned M : Exact->MemberOpcodes)
    Names.push_back(haydnOpcodeName(M));
  EXPECT_NE(Names[0], Names[1]) << "unit-duplicated MAC members";
  for (StringRef N : Names) {
    EXPECT_TRUE(N.contains("_MAC0_") || N.contains("_MAC1_"))
        << "member " << N << " is not a MAC-unit member";
    if (PlanE3)
      EXPECT_TRUE(N.contains("_E3_")) << "E3 plan with E2 member " << N;
    else
      EXPECT_TRUE(N.contains("_E2_")) << "E2 plan with E3 member " << N;
  }
  EXPECT_TRUE((Names[0].contains("_MAC0_") && Names[1].contains("_MAC1_")) ||
              (Names[0].contains("_MAC1_") && Names[1].contains("_MAC0_")))
      << "members do not use distinct MAC units";

  // The row-mixed INPUT (per-MI greedy bake) must rematch to the same law:
  // peel + resolve is one mechanism, single-shot.
  unsigned MixedOps[] = {Haydn::FMULAA32X16_H1_L0_E3_E2_MAC1_RR,
                         Haydn::FMULAA32X16_H1_L0_E2_E1_MAC1_RR};
  auto Remixed = exactSolveProductOpcodes(MixedOps, Fmts);
  ASSERT_TRUE(Remixed.has_value());
  ASSERT_EQ(Remixed->MemberOpcodes.size(), 2u);
  const bool RemixE3 = Remixed->Plan.Row == BundleFormatRowID::E96ThreeEntry;
  SmallVector<StringRef, 2> RemixNames;
  for (unsigned M : Remixed->MemberOpcodes) {
    const MCSlotKind Kind = Fmts.getSlotKind(M);
    EXPECT_TRUE(RemixE3 ? formatECompositeSlotIsE3(Kind)
                        : formatECompositeSlotIsE2(Kind));
    RemixNames.push_back(haydnOpcodeName(M));
  }
  EXPECT_NE(RemixNames[0], RemixNames[1]) << "rematch unit-duplicated MACs";
  EXPECT_TRUE((RemixNames[0].contains("_MAC0_") &&
               RemixNames[1].contains("_MAC1_")) ||
              (RemixNames[0].contains("_MAC1_") &&
               RemixNames[1].contains("_MAC0_")))
      << "rematch does not use distinct MAC units";
  // Row coherence held on input members too (post-rematch set never mixes).
  for (StringRef N : RemixNames) {
    if (RemixE3)
      EXPECT_TRUE(N.contains("_E3_")) << "E3 plan with E2 member " << N;
    else
      EXPECT_TRUE(N.contains("_E2_")) << "E2 plan with E3 member " << N;
  }
}

//===----------------------------------------------------------------------===//
// D1.53: selected-member unit injectivity before as-is commit
//===----------------------------------------------------------------------===//
//
// REGRESSION TEST GROUP (D1.53): the as-is arm of
// commitExactMultiMIProductCycle (asIsGeneratedMembersFormLegalCycle) accepts
// already-baked generated members and commits them with NO descriptor
// mutation and NO explicit selected-member Format E unit-injectivity
// validation. The explicit opcodesHaveFormatEUnitCover seats sit only AFTER
// the mutation-prone bake arms; the as-is acceptance relied on the INDIRECT
// MachineBundle::canAdd logical-cover overlay -- which asks whether SOME
// member assignment is injective, never whether the CHOSEN entry-resident
// records are.
//
// The corruption shape this leaves open: two members sharing ONE execution
// unit while their LOGICAL cover is injective (MOVE32+MOVE32 -> ALU0/1/2
// exist) and both residual FieldSlots are free. Slot occupancy and the
// canAdd overlay both accept the pair; the as-is arm committed it into a
// BUNDLE and the structural inverse verifier only fatalled LATE, after MIR
// mutation. Law (D1.53): one reusable selected-member injectivity check over
// the generated member records runs BEFORE any as-is private-member commit
// mutates MIR. Rejection is pre-mutation refusal, not post-hoc detection.
class HaydnBundleMaterializeMIFixture : public testing::Test {
protected:
  std::unique_ptr<HaydnTargetMachine> TM;
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<MachineModuleInfo> MMI;
  std::unique_ptr<HaydnSubtarget> ST;
  std::unique_ptr<MachineFunction> MF;
  MachineBasicBlock *MBB = nullptr;

  static void SetUpTestSuite() {
    LLVMInitializeHaydnTargetInfo();
    LLVMInitializeHaydnTarget();
    LLVMInitializeHaydnTargetMC();
  }

  void SetUp() override {
    std::string Error;
    Triple TT("haydn-unknown-elf");
    const Target *TheTarget = TargetRegistry::lookupTarget(TT, Error);
    ASSERT_NE(TheTarget, nullptr) << Error;

    TargetOptions Options;
    TM.reset(static_cast<HaydnTargetMachine *>(TheTarget->createTargetMachine(
        TT, "generic", "", Options, std::nullopt, std::nullopt,
        CodeGenOptLevel::Default)));
    ASSERT_NE(TM, nullptr);

    Ctx = std::make_unique<LLVMContext>();
    M = std::make_unique<Module>("HaydnBundleMaterialize", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);
    MBB = MF->CreateMachineBasicBlock();
    MF->push_back(MBB);
  }

  const HaydnInstrInfo &TII() const { return *ST->getInstrInfo(); }
  const TargetRegisterInfo *TRI() const { return ST->getRegisterInfo(); }

  /// MOVE32 member (dest, src): identical unary shape at every entry/unit --
  /// the twin-swap bake repair is shape-legal, so ONLY the selected-member
  /// law distinguishes the duplicate-unit pair.
  MachineInstr &move32Member(unsigned MemberOpc, Register Rd, Register Rs) {
    return *BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(MemberOpc), Rd)
                .addReg(Rs)
                .getInstr();
  }
};

// THE D1.53 duplicate-unit corruption probe named in GOALS: two
// LOADSTORE0-only member stores (same unit, same encoded entry e0) cannot
// coexist in one issue cycle (golden seven-unit injectivity; units !=
// encoded entry identity). Shapes are identical and the same-base MMOs are
// proven disjoint, so every OTHER same-cycle law passes and the only live
// reject is the duplicate execution unit. Pre-fix, refusal came from the
// INDIRECT MachineBundle::canAdd logical-cover overlay deep inside the as-is
// walk -- an ordering accident, not a direct pre-mutation law; this probe
// pins the refusal (and its pre-mutation fail-closed state) at the commit
// seat itself, so the overlay can never be the only thing standing between
// this corruption shape and a committed BUNDLE.
TEST_F(HaydnBundleMaterializeMIFixture,
       AsIsCommitRefusesDuplicateUnitMembersBeforeMutation) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;

  auto *GV = new GlobalVariable(*M, Type::getInt32Ty(*Ctx), /*isConstant=*/false,
                                GlobalValue::ExternalLinkage, nullptr, "obj");
  auto addMMO = [&](MachineInstr *MI, int64_t ByteOff) {
    MI->addMemOperand(*MF, MF->getMachineMemOperand(
                               MachinePointerInfo(GV, ByteOff),
                               MachineMemOperand::MOStore, 4, Align(4)));
  };

  // S_SW_WITH_IMM member shape: (ins GPR32:$dest1_0, GPR32:$dest2_1,
  // simm6:$imm_2) -- a pure store member used AS-IS at entry e0 unit
  // LOADSTORE0 (no defs, no tie: the writeback tie lives only on the
  // Slot0_LS_WbLat POST/WbLat family).
  auto swMember = [&](Register Rbase, Register Rsrc, int64_t ByteOff) {
    MachineInstr *MI =
        BuildMI(*MBB, MBB->end(), DL,
                II.get(Haydn::S_SW_WITH_IMM_E3_E0_LOADSTORE0_RI6))
            .addReg(Rbase)
            .addReg(Rsrc)
            .addImm(1)
            .getInstr();
    addMMO(MI, ByteOff);
    return MI;
  };

  // Precondition: this IS the corruption shape -- the member record exists
  // and is pinned to unit LOADSTORE0 (=4 in the generated Unit enum).
  const haydn::format_e::FormatEMemberRec *Rec =
      lookupPrivateFormatEMember(Haydn::S_SW_WITH_IMM_E3_E0_LOADSTORE0_RI6);
  ASSERT_NE(Rec, nullptr);
  ASSERT_EQ(Rec->Unit, 4u) << "golden LOADSTORE0 unit id drifted";

  // Proven-disjoint same-base MMOs (byte 0 vs 4, width 4): the store/load
  // overlap law does NOT mask the unit law under null AA. GPR ports stay
  // within 4R (2 bases + 2 srcs).
  MachineInstr *St0 = swMember(Haydn::R4, Haydn::R6, /*ByteOff=*/0);
  MachineInstr *St1 = swMember(Haydn::R5, Haydn::R7, /*ByteOff=*/4);

  SmallVector<MachineInstr *, 2> DupUnit = {St0, St1};
  // PRE-MUTATION refusal: both entry points reject the duplicate-unit
  // selected-member set BEFORE any setDesc/bake, with no heal possible
  // (LOADSTORE0 is the only unit either logical can take).
  EXPECT_FALSE(canCoissueProductCycle(DupUnit))
      << "probe must refuse two LOADSTORE0-only members sharing one unit";
  EXPECT_FALSE(commitExactMultiMIProductCycle(DupUnit))
      << "as-is commit must refuse duplicate-unit members before mutation";

  // Fail-closed contract: the refusal is PRE-mutation. Opcode identity,
  // operand count, parent, and bundle topology are unchanged -- the MIR
  // holds the original member opcodes as bare MIs, never an orphaned
  // partial bake and never a committed BUNDLE root.
  EXPECT_EQ(St0->getOpcode(), Haydn::S_SW_WITH_IMM_E3_E0_LOADSTORE0_RI6);
  EXPECT_EQ(St1->getOpcode(), Haydn::S_SW_WITH_IMM_E3_E0_LOADSTORE0_RI6);
  EXPECT_EQ(St0->getNumOperands(), 3u);
  EXPECT_EQ(St1->getNumOperands(), 3u);
  EXPECT_EQ(St0->getParent(), MBB);
  EXPECT_EQ(St1->getParent(), MBB);
  EXPECT_FALSE(St0->isBundled() || St0->isBundledWithPred() ||
               St0->isBundledWithSucc());
  EXPECT_FALSE(St1->isBundled() || St1->isBundledWithPred() ||
               St1->isBundledWithSucc());
  for (MachineBasicBlock::instr_iterator It = MBB->instr_begin();
       It != MBB->instr_end(); ++It)
    EXPECT_FALSE(It->isBundle()) << "refusal must not leave a BUNDLE root";

  // Control (distinct units, same family): the law is unit-keyed, not a
  // blanket member-pair reject. MOVE32 twins at distinct entries with
  // DISTINCT units (ALU1 vs ALU2) must still commit through the as-is arm
  // and stamp the E3 row.
  MachineInstr *Mv1 =
      &move32Member(Haydn::MOVE32_E3_E1_ALU1_R, Haydn::R2, Haydn::R8);
  MachineInstr *Mv2 =
      &move32Member(Haydn::MOVE32_E3_E2_ALU2_R, Haydn::R3, Haydn::R9);
  SmallVector<MachineInstr *, 2> DistinctUnit = {Mv1, Mv2};
  EXPECT_TRUE(canCoissueProductCycle(DistinctUnit))
      << "distinct-unit member pair must stay as-is coissuable";
  EXPECT_TRUE(commitExactMultiMIProductCycle(DistinctUnit))
      << "distinct-unit member pair must stay as-is committable";
  MachineBasicBlock::instr_iterator RootIt = getBundleStart(Mv1->getIterator());
  ASSERT_TRUE(RootIt->isBundle()) << "control pair must have committed a root";
  auto Row = getBundleRowID(*RootIt);
  ASSERT_TRUE(Row.has_value());
  EXPECT_TRUE(*Row == BundleFormatRowID::E96ThreeEntry)
      << "E3-member as-is commit must stamp the E96ThreeEntry row";
}

// THE red-then-green regression: adjacent-entry same-unit members whose
// LOGICAL cover is injective. Entries are distinct (e1 vs e2) and both
// FieldSlots are free, and the peeled logical cover (MOVE32+MOVE32 ->
// ALU0/1/2 exist) is assignable, so pre-fix NOTHING rejected this pair:
// the indirect canAdd overlay passed, and the as-is arm committed the
// duplicate-unit (ALU0 + ALU0) members verbatim into a BUNDLE -- the
// structural inverse verifier only fatalled LATE, after MIR mutation.
//
// Post-fix law: the as-is selected set is refused BEFORE mutation, and the
// SAME selected-member law re-runs on the POST-bake set before the
// irreversible applyFormatOrdering (per-slot representative picks can
// re-bind a duplicate unit while the logical cover stays assignable). The
// pinned invariant is therefore total: the duplicate-unit INPUT identity is
// never the committed set -- the commit is either refused outright (opcodes
// unchanged, no bundle) or lands on a healed, unit-injective member set.
// Pre-fix this test is RED (as-is commits the input identity verbatim);
// post-fix GREEN on either arm.
TEST_F(HaydnBundleMaterializeMIFixture,
       AsIsCommitNeverCommitsAdjacentEntrySameUnitIdentity) {
  using namespace llvm::haydn::bundle;
  MachineInstr *MvE1 =
      &move32Member(Haydn::MOVE32_E3_E1_ALU0_R, Haydn::R2, Haydn::R8);
  MachineInstr *MvE2 =
      &move32Member(Haydn::MOVE32_E3_E2_ALU0_R, Haydn::R3, Haydn::R9);
  SmallVector<MachineInstr *, 2> SameUnit = {MvE1, MvE2};

  const bool Committed = commitExactMultiMIProductCycle(SameUnit);
  if (Committed) {
    // Heal arm owned the commit: the committed selected set must be
    // unit-injective and must NOT be the as-is duplicate-unit identity.
    const haydn::format_e::FormatEMemberRec *R1 =
        lookupPrivateFormatEMember(MvE1->getOpcode());
    const haydn::format_e::FormatEMemberRec *R2 =
        lookupPrivateFormatEMember(MvE2->getOpcode());
    ASSERT_NE(R1, nullptr);
    ASSERT_NE(R2, nullptr);
    EXPECT_NE(R1->Unit, R2->Unit) << "committed members share one unit";
    EXPECT_FALSE(MvE1->getOpcode() == Haydn::MOVE32_E3_E1_ALU0_R &&
                 MvE2->getOpcode() == Haydn::MOVE32_E3_E2_ALU0_R)
        << "as-is duplicate-unit identity must never be the committed set";
    MachineBasicBlock::instr_iterator RootIt =
        getBundleStart(MvE1->getIterator());
    ASSERT_TRUE(RootIt->isBundle());
  } else {
    // Refused outright: pre-mutation state (opcodes unchanged, no bundle).
    EXPECT_EQ(MvE1->getOpcode(), Haydn::MOVE32_E3_E1_ALU0_R);
    EXPECT_EQ(MvE2->getOpcode(), Haydn::MOVE32_E3_E2_ALU0_R);
    EXPECT_FALSE(MvE1->isBundled() || MvE1->isBundledWithPred() ||
                 MvE1->isBundledWithSucc());
    EXPECT_FALSE(MvE2->isBundled() || MvE2->isBundledWithPred() ||
                 MvE2->isBundledWithSucc());
  }
}

//===----------------------------------------------------------------------===//
// Occupancy-drop of BUNDLE-header implicits (one helper)
//===----------------------------------------------------------------------===//
//
// Generic finalizeBundle (MachineInstrBundle.cpp:184-220) copies member
// all_defs/all_uses onto the header. After same-row NOP, prune in place —
// do not re-run finalizeBundle. KEEP $sfr iff a remaining member occupies
// SFR (Desc-named writer or leftover physical implicit-def $sfr). DROP on
// tii5's surviving ADD32 (no Defs=[SFR], no SFR operand). Jump/RET JALR
// does not occupy leftover catalog caller-saved GPRs; JALR_CALL ABI
// clobbers stay.

static bool hasImplicitDefOf(const MachineInstr &MI, Register R) {
  for (const MachineOperand &MO : MI.operands())
    if (MO.isReg() && MO.isImplicit() && MO.isDef() && MO.getReg() == R)
      return true;
  return false;
}

TEST_F(HaydnBundleMaterializeMIFixture,
       NeutralizeBeqzDropsRootSfrOnAdd32Survivor) {
  MachineBasicBlock *Tgt = MF->CreateMachineBasicBlock();
  MF->push_back(Tgt);
  MBB->addSuccessor(Tgt);

  MachineInstr *Add =
      BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::ADD32), Haydn::R3)
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .getInstr();
  MachineInstr *Br =
      BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::BEQZ_W))
          .addReg(Haydn::R0)
          .addMBB(Tgt)
          .addReg(Haydn::SFR, RegState::ImplicitDefine | RegState::Dead)
          .getInstr();

  finalizeBundle(*MBB, Add->getIterator(), std::next(Br->getIterator()));
  MachineInstr &Root = *getBundleStart(Add->getIterator());
  ASSERT_TRUE(Root.isBundle());
  ASSERT_TRUE(hasImplicitDefOf(Root, Haydn::SFR));

  neutralizeSameRowNop(*Br, TII());
  dropBundleImplicitRegsAbsentFromMembers(Root);

  EXPECT_FALSE(hasImplicitDefOf(Root, Haydn::SFR))
      << "tii5 ADD32 survivor does not occupy SFR";
  EXPECT_EQ(Add->getOpcode(), Haydn::ADD32);
  EXPECT_NE(Br->getOpcode(), Haydn::BEQZ_W);
  EXPECT_EQ(Br->getNumOperands(), 0u);
}

TEST_F(HaydnBundleMaterializeMIFixture,
       LeftoverPhysicalSfrOnSurvivorKeepsRootSfr) {
  MachineBasicBlock *Tgt = MF->CreateMachineBasicBlock();
  MF->push_back(Tgt);
  MBB->addSuccessor(Tgt);

  MachineInstr *Add =
      BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::ADD32), Haydn::R3)
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .addReg(Haydn::SFR, RegState::ImplicitDefine | RegState::Dead)
          .getInstr();
  MachineInstr *Br =
      BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::BEQZ_W))
          .addReg(Haydn::R0)
          .addMBB(Tgt)
          .addReg(Haydn::SFR, RegState::ImplicitDefine | RegState::Dead)
          .getInstr();

  finalizeBundle(*MBB, Add->getIterator(), std::next(Br->getIterator()));
  MachineInstr &Root = *getBundleStart(Add->getIterator());
  ASSERT_TRUE(Root.isBundle());
  ASSERT_TRUE(hasImplicitDefOf(Root, Haydn::SFR));

  neutralizeSameRowNop(*Br, TII());
  dropBundleImplicitRegsAbsentFromMembers(Root);

  EXPECT_TRUE(hasImplicitDefOf(Root, Haydn::SFR))
      << "leftover physical $sfr on a surviving member occupies the root";
  EXPECT_TRUE(hasImplicitDefOf(*Add, Haydn::SFR));
}

TEST_F(HaydnBundleMaterializeMIFixture,
       CatalogJalrJumpDropsCallerSavedRootImplicits) {
  MachineInstr *Jalr =
      BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::JALR_W), Haydn::R15)
          .addReg(Haydn::R9)
          .addImm(0)
          .getInstr();

  finalizeBundle(*MBB, Jalr->getIterator(), std::next(Jalr->getIterator()));
  MachineInstr &Root = *getBundleStart(Jalr->getIterator());
  ASSERT_TRUE(Root.isBundle());
  ASSERT_TRUE(hasImplicitDefOf(Root, Haydn::R1))
      << "generic finalizeBundle copies catalog JALR_W caller-saved Defs";

  dropBundleImplicitRegsAbsentFromMembers(Root);

  EXPECT_FALSE(hasImplicitDefOf(Root, Haydn::R1))
      << "JALR jump/RET does not occupy leftover catalog caller-saved GPRs";
  EXPECT_TRUE(hasImplicitDefOf(Root, Haydn::R15))
      << "explicit $rt occupancy stays on the root";
}

TEST_F(HaydnBundleMaterializeMIFixture, JalrCallKeepsAbiClobberRootImplicits) {
  MachineInstr *Call =
      BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::JALR_CALL),
              Haydn::R15)
          .addReg(Haydn::R9)
          .addImm(0)
          .getInstr();

  finalizeBundle(*MBB, Call->getIterator(), std::next(Call->getIterator()));
  MachineInstr &Root = *getBundleStart(Call->getIterator());
  ASSERT_TRUE(Root.isBundle());
  ASSERT_TRUE(hasImplicitDefOf(Root, Haydn::R1));

  dropBundleImplicitRegsAbsentFromMembers(Root);

  EXPECT_TRUE(hasImplicitDefOf(Root, Haydn::R1))
      << "JALR_CALL ABI clobbers occupy the root";
  EXPECT_TRUE(hasImplicitDefOf(Root, Haydn::R15));
}
