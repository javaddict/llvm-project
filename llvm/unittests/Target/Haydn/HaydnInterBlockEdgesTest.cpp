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
#include "llvm/ADT/STLExtras.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
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

// Cross-boundary edges must carry the producer's conservative itinerary
// latency on their SDep: the register-form SDep constructor hardcodes
// Data latency to 1 (and binds its third argument as a REGISTER, not a
// latency). The effective-latency cut prices Remaining =
// EdgeLat - Depth(Succ); a latency-1 memory edge understates a
// Data_Latency=2 producer and lets S2 issue the consumer early.
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

  // Memory edge Store1 -> Store2: Data kind, latency = the writer's
  // published Data_Latency = max OperandCycles (ST32 memory scaffold = 2;
  // every product InstrStage is single-cycle, so the stage latency would
  // wrongly say 1), Reg stays 0 (no register bound).
  const InstrItineraryData *Itin = ST->getInstrItineraryData();
  const unsigned SchedClass = Store1->getDesc().getSchedClass();
  unsigned Expected = 0;
  for (unsigned I = 0;; ++I) {
    std::optional<unsigned> OpLat = Itin->getOperandCycle(SchedClass, I);
    if (!OpLat)
      break;
    Expected = std::max(Expected, *OpLat);
  }
  ASSERT_TRUE(Itin && !Itin->isEmpty()) << "haydn itinerary must be present";
  EXPECT_EQ(Expected, 2u) << "ST32 memory scaffold Data_Latency";
  SmallVector<const SDep *, 4> MemEdges =
      DDG.getCrossBoundaryEdges(DDG.SUnits[0]);
  ASSERT_EQ(MemEdges.size(), 1u);
  EXPECT_EQ(MemEdges[0]->getKind(), SDep::Data);
  EXPECT_EQ(MemEdges[0]->getReg(), 0u);
  EXPECT_EQ(MemEdges[0]->getLatency(), Expected);

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

} // namespace
