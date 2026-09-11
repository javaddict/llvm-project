//===- HaydnInterBlockEdgesTest.cpp - inter-block DDG substrate ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// W68.2 substrate contract: boundary split semantics of HaydnInterBlockEdges
// (pre/post SUnit identity for the same MI, cross-boundary edge selection,
// post-depth recording, successor bundle occupancy for Bot scoreboard replay).
// Extra conservative edges are safe; missing/stale occupancy keeps full
// latency. No HC#0 walker hooks.
//
//===----------------------------------------------------------------------===//

#include "HaydnInterBlockScheduling.h"
#include "HaydnInstrInfo.h"
#include "HaydnSchedMutations.h"
#include "llvm/ADT/STLExtras.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "gtest/gtest.h"
#include <memory>
#include <optional>

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTarget();
extern "C" void LLVMInitializeHaydnTargetMC();

using namespace llvm;

namespace {

class HaydnInterBlockEdgesTest : public testing::Test {
protected:
  std::unique_ptr<HaydnTargetMachine> TM;
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<MachineModuleInfo> MMI;
  std::unique_ptr<HaydnSubtarget> ST;
  std::unique_ptr<MachineFunction> MF;
  MachineBasicBlock *Pred = nullptr;
  MachineBasicBlock *Succ = nullptr;
  MachineSchedContext MC;

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
    M = std::make_unique<Module>("IBEdges", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "t", *M);
    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(), 0);
    MF->initTargetMachineFunctionInfo(*ST);
    MC.MF = MF.get();
    Pred = MF->CreateMachineBasicBlock();
    Succ = MF->CreateMachineBasicBlock();
    MF->push_back(Pred);
    MF->push_back(Succ);
  }

  const HaydnInstrInfo &TII() const { return *ST->getInstrInfo(); }

  MachineInstr &add32(MachineBasicBlock *BB, Register Rd, Register Rs,
                      Register Rt) {
    return *BuildMI(*BB, BB->end(), DebugLoc(), TII().get(Haydn::ADD32), Rd)
                .addReg(Rs)
                .addReg(Rt);
  }

  // Intra-graph latency edge. addPred wires both Preds and Succs so a
  // height walk (Succs) and a depth walk (Preds) disagree on a chain.
  static void addLatEdge(SUnit &PredSU, SUnit &SuccSU, unsigned Latency) {
    SDep Dep(&PredSU, SDep::Artificial);
    Dep.setLatency(Latency);
    SuccSU.addPred(Dep);
  }
};

// Boundary bookkeeping: pre vs post SUnit identity for the same MI, and
// cross-boundary edge selection by boundary index (manual SDep wiring —
// buildEdges itself awaits the HC#0 walker exception).
TEST_F(HaydnInterBlockEdgesTest, BoundarySplitSemantics) {
  MachineInstr &P1 = add32(Pred, Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &S1 = add32(Succ, Haydn::R4, Haydn::R1, Haydn::R5);
  HaydnInterBlockEdges DDG(MC, Pred, Succ);
  // Reserve covers the real-MIR bound (Pred.size + Succ.size); the dual-
  // side probe below re-adds one MI, so add one slot for it.
  DDG.reserveForBlocks(*Pred, *Succ);
  DDG.reserveMore(1);
  DDG.addNode(&P1);
  DDG.markBoundary();
  DDG.addNode(&S1);
  ASSERT_EQ(DDG.SUnits.size(), 2u);
  EXPECT_TRUE(DDG.isPreBoundaryNode(&DDG.SUnits[0]));
  EXPECT_TRUE(DDG.isPostBoundaryNode(&DDG.SUnits[1]));
  // Same MI on both sides (self-edge shape) keeps distinct SUnits.
  DDG.addNode(&P1);
  EXPECT_EQ(DDG.SUnits.size(), 3u);
  EXPECT_EQ(DDG.getPreBoundaryNode(&P1), &DDG.SUnits[0]);
  EXPECT_EQ(DDG.getPostBoundaryNode(&P1), &DDG.SUnits[2]);
}

// Cross-boundary memory edges must carry getMemoryLatency, not dest
// OperandCycles. The register-form SDep constructor hardcodes Data
// latency to 1 (and binds its third argument as a REGISTER, not a
// latency). The effective-latency cut prices Remaining =
// EdgeLat - Depth(Succ); a latency-1 store→store edge understates the
// published MemoryCycle window and lets S2 issue the consumer early.
//
// Golden store Data_Latency is 0 — ST32 has no dest claim. Slot0_LS
// OperandCycles [2] is the load dest scaffold; Slot0_LS_WbLat [1] is
// writeback-register latency. Store→store authority is the published
// MemoryCycle pair (first=0, last=1) → Last-First+1 = 2, the same
// getMemoryLatency API intra-block MemoryEdges already consumes
// (AIE MemInstrItinData / AIEBaseInstrInfo.cpp:1038-1049).
TEST_F(HaydnInterBlockEdgesTest, CrossBoundaryEdgeLatency) {
  const HaydnInstrInfo &II = TII();
  MachineInstr *Store1 =
      BuildMI(*Pred, Pred->end(), DebugLoc(), II.get(Haydn::ST32))
          .addReg(Haydn::R3)
          .addReg(Haydn::R2)
          .addImm(0)
          .getInstr();
  // Disjoint register sets: the only dependence is the memory edge
  // store -> store (both mayStore; null AA = conservative alias).
  MachineInstr *Store2 =
      BuildMI(*Succ, Succ->end(), DebugLoc(), II.get(Haydn::ST32))
          .addReg(Haydn::R6)
          .addReg(Haydn::R7)
          .addImm(0)
          .getInstr();
  MachineInstr &Def = add32(Pred, Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &Use = add32(Succ, Haydn::R4, Haydn::R1, Haydn::R5);

  HaydnInterBlockEdges DDG(MC, Pred, Succ);
  DDG.reserveForBlocks(*Pred, *Succ);
  DDG.addNode(Store1);
  DDG.addNode(&Def);
  DDG.markBoundary();
  DDG.addNode(Store2);
  DDG.addNode(&Use);
  const TargetRegisterInfo *TRI = MF->getRegInfo().getTargetRegisterInfo();
  DDG.buildCrossBoundaryEdges(/*AA=*/nullptr, &II, TRI,
                              &DDG.getSchedModelRef());

  const unsigned SrcSC = Store1->getDesc().getSchedClass();
  const unsigned DstSC = Store2->getDesc().getSchedClass();
  std::optional<int> First = II.getFirstMemoryCycle(SrcSC);
  std::optional<int> Last = II.getLastMemoryCycle(SrcSC);
  ASSERT_TRUE(First.has_value() && Last.has_value())
      << "ST32 is a published Slot0_LS memory class";
  EXPECT_EQ(*Last - *First + 1, 2) << "ST32 MemoryCycle pair (0,1)";
  std::optional<int> MemLat = II.getMemoryLatency(SrcSC, DstSC);
  ASSERT_TRUE(MemLat.has_value());
  EXPECT_EQ(*MemLat, 2) << "ST32 MemoryCycle last-first+1=2";
  SmallVector<const SDep *, 4> MemEdges =
      DDG.getCrossBoundaryEdges(DDG.SUnits[0]);
  ASSERT_EQ(MemEdges.size(), 1u);
  EXPECT_EQ(MemEdges[0]->getKind(), SDep::Data);
  EXPECT_EQ(MemEdges[0]->getReg(), 0u);
  EXPECT_EQ(MemEdges[0]->getLatency(), static_cast<unsigned>(*MemLat));

  // Register edge Def(ADD32 def R1) -> Use(ADD32 read R1): Data kind, Reg
  // bound to R1, latency >= the writer's itinerary latency.
  SmallVector<const SDep *, 4> RegEdges =
      DDG.getCrossBoundaryEdges(DDG.SUnits[1]);
  ASSERT_EQ(RegEdges.size(), 1u);
  EXPECT_EQ(RegEdges[0]->getKind(), SDep::Data);
  EXPECT_EQ(RegEdges[0]->getReg(), Haydn::R1);
  EXPECT_GE(RegEdges[0]->getLatency(), 1u);

  // Static depths exist for the latency cut; occupancy is not scheduled, so
  // Bot replay must keep full latency (AIE empty/unknown successor).
  EXPECT_FALSE(DDG.hasScheduledSuccessorOccupancy());
  EXPECT_FALSE(DDG.canReplaySuccessorOccupancy());
  EXPECT_GE(DDG.getPostDepthOr(&DDG.SUnits[3], -1), 0);
}

// Scheduled successor occupancy is the Bot replay view. Real members occupy
// their issue cycle; BUNDLE roots and depth-only records do not.
TEST_F(HaydnInterBlockEdgesTest, SuccessorOccupancyReplayView) {
  MachineInstr &P1 = add32(Pred, Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &S0 = add32(Succ, Haydn::R4, Haydn::R1, Haydn::R5);
  MachineInstr &S1 = add32(Succ, Haydn::R6, Haydn::R4, Haydn::R7);
  HaydnInterBlockEdges DDG(MC, Pred, Succ);
  DDG.reserveForBlocks(*Pred, *Succ);
  DDG.addNode(&P1);
  DDG.markBoundary();
  DDG.addNode(&S0);
  DDG.addNode(&S1);

  EXPECT_FALSE(DDG.canReplaySuccessorOccupancy());
  DDG.recordPostDepth(&S0, 0);
  DDG.recordPostDepth(&S1, 0); // co-issue in successor cycle 0
  EXPECT_TRUE(DDG.hasRecordedPostDepths());
  EXPECT_TRUE(DDG.hasScheduledSuccessorOccupancy());
  EXPECT_TRUE(DDG.successorOccupancyIsLive());
  EXPECT_TRUE(DDG.canReplaySuccessorOccupancy());
  ASSERT_EQ(DDG.getSuccessorOccupancy().size(), 1u);
  ASSERT_EQ(DDG.getSuccessorOccupancy()[0].size(), 2u);
  EXPECT_EQ(DDG.getSuccessorOccupancy()[0][0], &S0);
  EXPECT_EQ(DDG.getSuccessorOccupancy()[0][1], &S1);
  EXPECT_EQ(DDG.getPostDepth(*DDG.getPostBoundaryNode(&S0)), 0);

  DDG.recordPostDepth(&S1, 1); // later cycle grows the view, keeps S0 at 0
  ASSERT_EQ(DDG.getSuccessorOccupancy().size(), 2u);
  EXPECT_TRUE(is_contained(DDG.getSuccessorOccupancy()[1], &S1));

  // Depth-only record does not invent occupancy (missing view = full latency).
  HaydnInterBlockEdges DepthOnly(MC, Pred, Succ);
  DepthOnly.reserveForBlocks(*Pred, *Succ);
  DepthOnly.addNode(&P1);
  DepthOnly.markBoundary();
  DepthOnly.addNode(&S0);
  DepthOnly.recordPostDepth(3);
  EXPECT_TRUE(DepthOnly.hasRecordedPostDepths());
  EXPECT_EQ(DepthOnly.getPostRegionMaxDepth(), 3);
  EXPECT_FALSE(DepthOnly.hasScheduledSuccessorOccupancy());
  EXPECT_FALSE(DepthOnly.canReplaySuccessorOccupancy());

  MachineInstr *BundleRoot =
      BuildMI(*Succ, Succ->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  HaydnInterBlockEdges BundleSeed(MC, Pred, Succ);
  BundleSeed.reserveForBlocks(*Pred, *Succ);
  BundleSeed.addNode(&P1);
  BundleSeed.markBoundary();
  BundleSeed.addNode(&S0);
  BundleSeed.recordPostDepth(BundleRoot, 0);
  EXPECT_TRUE(BundleSeed.hasRecordedPostDepths());
  EXPECT_FALSE(BundleSeed.hasScheduledSuccessorOccupancy());
  EXPECT_FALSE(BundleSeed.canReplaySuccessorOccupancy());
}

// S2 gather may already have DepthsAreScheduled from BUNDLE-root seeding;
// occupancy must still inherit from S1. Stale MIs (erased from Succ) keep
// full latency.
TEST_F(HaydnInterBlockEdgesTest, OccupancyInheritAndStaleKeepsFullLatency) {
  MachineInstr &P1 = add32(Pred, Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &S0 = add32(Succ, Haydn::R4, Haydn::R1, Haydn::R5);
  HaydnInterBlockEdges S1(MC, Pred, Succ);
  S1.reserveForBlocks(*Pred, *Succ);
  S1.addNode(&P1);
  S1.markBoundary();
  S1.addNode(&S0);
  S1.recordPostDepth(&S0, 0);
  ASSERT_TRUE(S1.canReplaySuccessorOccupancy());

  MachineInstr *BundleRoot =
      BuildMI(*Succ, Succ->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  HaydnInterBlockEdges S2(MC, Pred, Succ);
  S2.reserveForBlocks(*Pred, *Succ);
  S2.addNode(&P1);
  S2.markBoundary();
  S2.addNode(&S0);
  S2.recordPostDepth(BundleRoot, 0); // depths already scheduled, no occupancy
  EXPECT_TRUE(S2.hasRecordedPostDepths());
  EXPECT_FALSE(S2.hasScheduledSuccessorOccupancy());
  S2.inheritRecordedPostDepths(S1);
  EXPECT_TRUE(S2.hasScheduledSuccessorOccupancy());
  EXPECT_TRUE(S2.canReplaySuccessorOccupancy());
  ASSERT_EQ(S2.getSuccessorOccupancy().size(), 1u);
  ASSERT_EQ(S2.getSuccessorOccupancy()[0].size(), 1u);
  EXPECT_EQ(S2.getSuccessorOccupancy()[0][0], &S0);

  S0.eraseFromParent();
  EXPECT_FALSE(S2.successorOccupancyIsLive());
  EXPECT_FALSE(S2.canReplaySuccessorOccupancy());
}

// Earliest-execution depth is longest-upward over post-boundary Preds
// (ScheduleDAG ComputeDepth), not height over Succs. A two-node chain
// S0 -lat3-> S1 is Depth(S0)=0, Depth(S1)=3; height is the swap.
// Pre-boundary preds (P1 -lat9-> S0) must not inflate the fill, and
// recomputePostDepths must reuse that fill rather than SU.getDepth().
TEST_F(HaydnInterBlockEdgesTest, PostBoundaryChainIsDepthNotHeight) {
  MachineInstr &P1 = add32(Pred, Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &S0 = add32(Succ, Haydn::R4, Haydn::R1, Haydn::R5);
  MachineInstr &S1 = add32(Succ, Haydn::R6, Haydn::R4, Haydn::R7);
  HaydnInterBlockEdges DDG(MC, Pred, Succ);
  DDG.reserveForBlocks(*Pred, *Succ);
  DDG.addNode(&P1);
  DDG.markBoundary();
  DDG.addNode(&S0);
  DDG.addNode(&S1);
  ASSERT_EQ(DDG.SUnits.size(), 3u);
  addLatEdge(DDG.SUnits[0], DDG.SUnits[1], 9); // pre -> post, ignored by fill
  addLatEdge(DDG.SUnits[1], DDG.SUnits[2], 3); // post -> post

  DDG.recomputePostDepthsFromEdges(&DDG.getSchedModelRef(), &TII());
  EXPECT_EQ(DDG.getPostDepth(DDG.SUnits[1]), 0);
  EXPECT_EQ(DDG.getPostDepth(DDG.SUnits[2]), 3);
  EXPECT_EQ(DDG.getPostRegionMaxDepth(), 3);
  // Height of the same chain would stamp S0=3, S1=0.
  EXPECT_NE(DDG.getPostDepth(DDG.SUnits[1]), 3);
  EXPECT_NE(DDG.getPostDepth(DDG.SUnits[2]), 0);

  EXPECT_EQ(DDG.SUnits[1].getDepth(), 9u);
  EXPECT_EQ(DDG.SUnits[2].getDepth(), 12u);
  DDG.recomputePostDepths();
  EXPECT_EQ(DDG.getPostDepth(DDG.SUnits[1]), 0)
      << "skipped-region fill must not stamp generic SU.getDepth()";
  EXPECT_EQ(DDG.getPostDepth(DDG.SUnits[2]), 3);
}

// Unrecorded / no-post-pred depth is 0, never PostRegionMaxDepth. The
// remaining-latency cut is Remaining = EdgeLat - getPostDepthOr(Dst, 0);
// a max-cut fallback over-cuts early consumers.
TEST_F(HaydnInterBlockEdgesTest, MissingDepthDefaultsToZeroNotRegionMax) {
  MachineInstr &P1 = add32(Pred, Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &S0 = add32(Succ, Haydn::R4, Haydn::R1, Haydn::R5);
  MachineInstr &S1 = add32(Succ, Haydn::R6, Haydn::R4, Haydn::R7);
  MachineInstr &Iso = add32(Succ, Haydn::R8, Haydn::R9, Haydn::R10);
  HaydnInterBlockEdges DDG(MC, Pred, Succ);
  DDG.reserveForBlocks(*Pred, *Succ);
  DDG.addNode(&P1);
  DDG.markBoundary();
  DDG.addNode(&S0);
  DDG.addNode(&S1);
  DDG.addNode(&Iso);
  addLatEdge(DDG.SUnits[1], DDG.SUnits[2], 4);

  DDG.recomputePostDepthsFromEdges(&DDG.getSchedModelRef(), &TII());
  EXPECT_EQ(DDG.getPostDepth(DDG.SUnits[1]), 0);
  EXPECT_EQ(DDG.getPostDepth(DDG.SUnits[2]), 4);
  EXPECT_EQ(DDG.getPostDepth(DDG.SUnits[3]), 0);
  EXPECT_EQ(DDG.getPostRegionMaxDepth(), 4);
  EXPECT_EQ(DDG.getPostDepthOr(&DDG.SUnits[3], 0), 0);
  EXPECT_NE(DDG.getPostDepthOr(&DDG.SUnits[3], 0),
            DDG.getPostRegionMaxDepth());

  // Record only S1: Iso is missing from PostDepths → default 0, not max.
  HaydnInterBlockEdges Partial(MC, Pred, Succ);
  Partial.reserveForBlocks(*Pred, *Succ);
  Partial.addNode(&P1);
  Partial.markBoundary();
  Partial.addNode(&S0);
  Partial.addNode(&S1);
  Partial.addNode(&Iso);
  Partial.recordPostDepth(&S1, 7);
  EXPECT_EQ(Partial.getPostRegionMaxDepth(), 7);
  EXPECT_EQ(Partial.getPostDepth(*Partial.getPostBoundaryNode(&S1)), 7);
  EXPECT_EQ(Partial.getPostDepth(*Partial.getPostBoundaryNode(&Iso)), -1);
  EXPECT_EQ(Partial.getPostDepthOr(Partial.getPostBoundaryNode(&Iso), 0), 0);
  EXPECT_NE(Partial.getPostDepthOr(Partial.getPostBoundaryNode(&Iso), 0),
            Partial.getPostRegionMaxDepth());
}

// S2 gather may already have DepthsAreScheduled from BUNDLE-root seeding;
// per-MI depths still inherit from S1. An MI whose parent is not Succ is
// skipped (stale after splice/erase).
TEST_F(HaydnInterBlockEdgesTest, DepthInheritDespiteScheduledSkipsForeignParent) {
  MachineInstr &P1 = add32(Pred, Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &SKeep = add32(Succ, Haydn::R4, Haydn::R1, Haydn::R5);
  MachineInstr &SForeign = add32(Succ, Haydn::R6, Haydn::R4, Haydn::R7);
  HaydnInterBlockEdges S1(MC, Pred, Succ);
  S1.reserveForBlocks(*Pred, *Succ);
  S1.addNode(&P1);
  S1.markBoundary();
  S1.addNode(&SKeep);
  S1.addNode(&SForeign);
  S1.recordPostDepth(&SKeep, 5);
  S1.recordPostDepth(&SForeign, 6);
  ASSERT_TRUE(S1.hasRecordedPostDepths());

  MachineInstr *BundleRoot =
      BuildMI(*Succ, Succ->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  HaydnInterBlockEdges S2(MC, Pred, Succ);
  S2.reserveForBlocks(*Pred, *Succ);
  S2.addNode(&P1);
  S2.markBoundary();
  S2.addNode(&SKeep);
  S2.addNode(&SForeign);
  S2.recordPostDepth(BundleRoot, 0);
  EXPECT_TRUE(S2.hasRecordedPostDepths());
  EXPECT_EQ(S2.getPostDepth(*S2.getPostBoundaryNode(&SKeep)), -1);

  MachineBasicBlock *Other = MF->CreateMachineBasicBlock();
  MF->push_back(Other);
  Other->splice(Other->end(), Succ, SForeign.getIterator());
  EXPECT_NE(SForeign.getParent(), Succ);

  S2.inheritRecordedPostDepths(S1);
  EXPECT_EQ(S2.getPostDepth(*S2.getPostBoundaryNode(&SKeep)), 5);
  EXPECT_EQ(S2.getPostDepth(*S2.getPostBoundaryNode(&SForeign)), -1)
      << "MI whose parent is not Succ must not inherit a depth";
  EXPECT_TRUE(S2.hasScheduledSuccessorOccupancy());
  ASSERT_FALSE(S2.getSuccessorOccupancy().empty());
  EXPECT_TRUE(is_contained(S2.getSuccessorOccupancy()[5], &SKeep));
}

// Re-publish for the same (Pred, Succ) keeps the newest graph (S2 after
// inherit), not the first/oldest S1 record.
TEST_F(HaydnInterBlockEdgesTest, RegistrySupersedeKeepsNewestGraph) {
  MachineInstr &P1 = add32(Pred, Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &S0 = add32(Succ, Haydn::R4, Haydn::R1, Haydn::R5);
  MachineInstr &Extra = add32(Succ, Haydn::R6, Haydn::R4, Haydn::R7);

  auto MakeS1 = [&]() {
    auto G = std::make_unique<HaydnInterBlockEdges>(MC, Pred, Succ);
    G->reserveForBlocks(*Pred, *Succ);
    G->addNode(&P1);
    G->markBoundary();
    G->addNode(&S0);
    G->recordPostDepth(&S0, 1);
    return G;
  };
  auto MakeS2 = [&]() {
    auto G = std::make_unique<HaydnInterBlockEdges>(MC, Pred, Succ);
    G->reserveForBlocks(*Pred, *Succ);
    G->addNode(&P1);
    G->markBoundary();
    G->addNode(&S0);
    G->addNode(&Extra);
    G->recordPostDepth(&Extra, 9);
    return G;
  };

  HaydnIBEdgesByPredMap First;
  First[Pred].push_back(MakeS1());
  setHaydnInterBlockEdgesForFunction(*MF, &First);
  HaydnIBEdgesByPredMap Second;
  Second[Pred].push_back(MakeS2());
  setHaydnInterBlockEdgesForFunction(*MF, &Second);

  HaydnIBEdgesByPredMap *Reg = haydnGetInterBlockEdgesRegistry(*MF);
  ASSERT_NE(Reg, nullptr);
  auto It = Reg->find(Pred);
  ASSERT_NE(It, Reg->end());
  ASSERT_EQ(It->second.size(), 1u);
  EXPECT_EQ(It->second[0]->getSucc(), Succ);
  EXPECT_EQ(It->second[0]->SUnits.size(), 3u)
      << "supersede must keep the newest graph, not the oldest";
  EXPECT_NE(It->second[0]->getPostBoundaryNode(&Extra), nullptr);
}

// D1.38 (1b): a pre-boundary regmask with NO implicit-def clobber operands
// (hand MIR / future CC shape — today's compiled JAL_W materializes its
// clobbers as implicit-defs) must still edge to a post reader of a clobbered
// register, and the regmask must contribute NO edge for a CSR_Haydn-preserved
// register (the exact-match arm still owns Pre's own def->read edge).
TEST_F(HaydnInterBlockEdgesTest, RegMaskClobberEdges) {
  const HaydnInstrInfo &II = TII();
  // Pre: ADD32 on preserved regs, carrying ONLY a regmask (no implicit-defs).
  const TargetRegisterInfo *TRI = MF->getRegInfo().getTargetRegisterInfo();
  const uint32_t *Mask = TRI->getCallPreservedMask(
      *MF, MF->getFunction().getCallingConv());
  ASSERT_NE(Mask, nullptr);
  MachineInstr *Pre =
      BuildMI(*Pred, Pred->end(), DebugLoc(), II.get(Haydn::ADD32), Haydn::R9)
          .addReg(Haydn::R10)
          .addReg(Haydn::R11)
          .addRegMask(Mask)
          .getInstr();
  // Post: reader of R5 (clobbered: CSR = R8-R11,R14,R15,D8-D15) and reader
  // of R9 (preserved, also defined by Pre — the exact-match arm owns that
  // edge, so the regmask arm must not add a second one).
  MachineInstr &PostClob = add32(Succ, Haydn::R4, Haydn::R5, Haydn::R3);
  MachineInstr &PostKeep = add32(Succ, Haydn::R6, Haydn::R9, Haydn::R7);

  HaydnInterBlockEdges DDG(MC, Pred, Succ);
  DDG.reserveForBlocks(*Pred, *Succ);
  DDG.addNode(Pre);
  DDG.markBoundary();
  DDG.addNode(&PostClob);
  DDG.addNode(&PostKeep);
  DDG.buildCrossBoundaryEdges(/*AA=*/nullptr, &II, TRI,
                              &DDG.getSchedModelRef());

  // Clobbered R5: Data edge (regmask def -> post read), Reg bound to R5.
  SmallVector<const SDep *, 4> ClobEdges =
      DDG.getCrossBoundaryEdges(DDG.SUnits[0]);
  bool SawClobData = false, SawKeepData = false;
  for (const SDep *Dep : ClobEdges) {
    if (Dep->getKind() != SDep::Data)
      continue;
    if (Dep->getReg() == Haydn::R5 && Dep->getSUnit() == DDG.getPostBoundaryNode(&PostClob))
      SawClobData = true;
    if (Dep->getReg() == Haydn::R9 && Dep->getSUnit() == DDG.getPostBoundaryNode(&PostKeep))
      SawKeepData = true;
  }
  EXPECT_TRUE(SawClobData) << "regmask clobber of R5 must reach the post reader";
  EXPECT_TRUE(SawKeepData)
      << "exact-match R9 def->read edge must survive (not first-match dropped)";
}

// D1.38 (2): calls are memory agents on BOTH sides. Generated JAL_W carries
// no MayLoad/MayStore, so the flag-only PreMem walk never saw it; mayAlias
// answers true for isCall, so admission is the only fix. A post-boundary
// call is also a memory reader of pre stores.
TEST_F(HaydnInterBlockEdgesTest, CallMemoryEdges) {
  const HaydnInstrInfo &II = TII();
  const TargetRegisterInfo *TRI = MF->getRegInfo().getTargetRegisterInfo();
  // Pre JAL_W: call with disjoint explicit regs (rt=R12) and no mem operands.
  MachineInstr *Call =
      BuildMI(*Pred, Pred->end(), DebugLoc(), II.get(Haydn::JAL_W), Haydn::R12)
          .addImm(0)
          .getInstr();
  MachineInstr *PostLoad =
      BuildMI(*Succ, Succ->end(), DebugLoc(), II.get(Haydn::LD32), Haydn::R4)
          .addReg(Haydn::R1)
          .addImm(0)
          .getInstr();
  // Second graph: pre store vs post call (post-call memory reader).
  MachineInstr *Store =
      BuildMI(*Pred, Pred->end(), DebugLoc(), II.get(Haydn::ST32))
          .addReg(Haydn::R3)
          .addReg(Haydn::R2)
          .addImm(0)
          .getInstr();
  MachineInstr *PostCall =
      BuildMI(*Succ, Succ->end(), DebugLoc(), II.get(Haydn::JAL_W), Haydn::R12)
          .addImm(0)
          .getInstr();

  HaydnInterBlockEdges DDG(MC, Pred, Succ);
  DDG.reserveForBlocks(*Pred, *Succ);
  DDG.addNode(Call);
  DDG.markBoundary();
  DDG.addNode(PostLoad);
  DDG.buildCrossBoundaryEdges(/*AA=*/nullptr, &II, TRI,
                              &DDG.getSchedModelRef());
  SmallVector<const SDep *, 4> Edges = DDG.getCrossBoundaryEdges(DDG.SUnits[0]);
  bool SawCallMem = false;
  for (const SDep *Dep : Edges)
    if (Dep->getSUnit() == DDG.getPostBoundaryNode(PostLoad) &&
        Dep->getKind() == SDep::Data && Dep->getReg() == 0u)
      SawCallMem = true;
  EXPECT_TRUE(SawCallMem) << "pre call must memory-edge to a post load";

  HaydnInterBlockEdges DDG2(MC, Pred, Succ);
  DDG2.reserveForBlocks(*Pred, *Succ);
  DDG2.addNode(Store);
  DDG2.markBoundary();
  DDG2.addNode(PostCall);
  DDG2.buildCrossBoundaryEdges(/*AA=*/nullptr, &II, TRI,
                               &DDG2.getSchedModelRef());
  SmallVector<const SDep *, 4> Edges2 =
      DDG2.getCrossBoundaryEdges(DDG2.SUnits[0]);
  bool SawStoreToCall = false;
  for (const SDep *Dep : Edges2)
    if (Dep->getKind() == SDep::Data && Dep->getReg() == 0u)
      SawStoreToCall = true;
  EXPECT_TRUE(SawStoreToCall) << "pre store must memory-edge to a post call";
}

// D1.38 (3): real (non-pseudo) carriers of MCID::UnmodeledSideEffects —
// MOVESFR2GPR/MOVEGPR2SFR, FLAR, generated JAL_E* — model effects the
// register/memory arms cannot price. FLAR is the cleanest pin: no reg
// operands, no memory flags (CSRW/CSRW_W/WFI are isPseudo and never become
// DDG nodes, and CSRW_W is hasSideEffects=0 anyway). Only the Order arm
// sees the pair; latency 0 keeps it cut-inert (Remaining <= 0 skips).
TEST_F(HaydnInterBlockEdgesTest, UnmodeledSideEffectOrderEdges) {
  const HaydnInstrInfo &II = TII();
  const TargetRegisterInfo *TRI = MF->getRegInfo().getTargetRegisterInfo();
  // Pre/post FLAR: hasSideEffects=1 (HaydnInstrInfo.td:1702), no Defs/Uses.
  MachineInstr *PreFlar =
      BuildMI(*Pred, Pred->end(), DebugLoc(), II.get(Haydn::FLAR))
          .addImm(0)
          .getInstr();
  MachineInstr *PostFlar =
      BuildMI(*Succ, Succ->end(), DebugLoc(), II.get(Haydn::FLAR))
          .addImm(1)
          .getInstr();

  HaydnInterBlockEdges DDG(MC, Pred, Succ);
  DDG.reserveForBlocks(*Pred, *Succ);
  DDG.addNode(PreFlar);
  DDG.markBoundary();
  DDG.addNode(PostFlar);
  DDG.buildCrossBoundaryEdges(/*AA=*/nullptr, &II, TRI,
                              &DDG.getSchedModelRef());
  SmallVector<const SDep *, 4> Edges = DDG.getCrossBoundaryEdges(DDG.SUnits[0]);
  ASSERT_EQ(Edges.size(), 1u)
      << "no reg operands and no memory ops: only the Order edge may exist";
  EXPECT_EQ(Edges[0]->getKind(), SDep::Order);
  EXPECT_EQ(Edges[0]->getLatency(), 0u);
}

// D1.38 (strongest-edge law): one Pre MI whose operand order places a USE of
// R1 before a DEF of R1 used to stop at the first match (Anti) and drop the
// true Data edge. Every matching operand must contribute its class.
TEST_F(HaydnInterBlockEdgesTest, ReadDefOperandKeepsStrongestEdge) {
  const HaydnInstrInfo &II = TII();
  const TargetRegisterInfo *TRI = MF->getRegInfo().getTargetRegisterInfo();
  // Pre: ADD32 R1 = R1 + R2 (use of R1 precedes its def in operand order).
  MachineInstr *Pre =
      BuildMI(*Pred, Pred->end(), DebugLoc(), II.get(Haydn::ADD32), Haydn::R1)
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .getInstr();
  MachineInstr &Post = add32(Succ, Haydn::R4, Haydn::R1, Haydn::R5);

  HaydnInterBlockEdges DDG(MC, Pred, Succ);
  DDG.reserveForBlocks(*Pred, *Succ);
  DDG.addNode(Pre);
  DDG.markBoundary();
  DDG.addNode(&Post);
  DDG.buildCrossBoundaryEdges(/*AA=*/nullptr, &II, TRI,
                              &DDG.getSchedModelRef());
  SmallVector<const SDep *, 4> Edges = DDG.getCrossBoundaryEdges(DDG.SUnits[0]);
  // The post side only reads R1, so exactly one edge may exist and it must
  // be the Data edge: the pre-fix first-match break hit the USE operand of
  // R1 first (readsReg, no PreWrites on that operand) and fell through to
  // `continue`, never reaching the DEF operand's Data class.
  ASSERT_EQ(Edges.size(), 1u);
  EXPECT_EQ(Edges[0]->getKind(), SDep::Data)
      << "the Data edge (def->read) must survive, not only Anti";
  EXPECT_EQ(Edges[0]->getReg(), Haydn::R1);
  EXPECT_GE(Edges[0]->getLatency(), 1u);
}

} // namespace
