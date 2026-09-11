//===- MachineSchedStrategyTest.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "gtest/gtest.h"
#include <iterator>
#include <memory>

using namespace llvm;

namespace {
#include "MFCommon.inc"

class StubStrategy : public MachineSchedStrategy {
public:
  void initialize(ScheduleDAGMI *) override {}
  SUnit *pickNode(bool &) override { return nullptr; }
  void schedNode(SUnit *, bool) override {}
  void releaseTopNode(SUnit *) override {}
  void releaseBottomNode(SUnit *) override {}
};

class RepeatFirstTwiceStrategy : public StubStrategy {
  MachineBasicBlock *First = nullptr;
  unsigned Returns = 0;

public:
  void enterFunction(MachineFunction *MF) override {
    MachineSchedStrategy::enterFunction(MF);
    First = MF->empty() ? nullptr : &*MF->begin();
    Returns = 0;
  }

  MachineBasicBlock *nextBlock() override {
    if (First && Returns < 2) {
      ++Returns;
      return First;
    }
    return nullptr;
  }

  // Default leaveFunction asserts NextMBB==end after a single layout walk.
  // Repeaters must override it and not call the base (AIE dual-iterator quirk).
  void leaveFunction() override {}
};

class CountingEnterFunctionStrategy : public StubStrategy {
public:
  unsigned Enters = 0;
  MachineFunction *Seen = nullptr;

  void enterFunction(MachineFunction *MF) override {
    ++Enters;
    Seen = MF;
    MachineSchedStrategy::enterFunction(MF);
  }
};

class CountingBuildGraphStrategy : public StubStrategy {
public:
  unsigned Calls = 0;
  AAResults *LastAA = reinterpret_cast<AAResults *>(1);
  const RegPressureTracker *LastRP = reinterpret_cast<RegPressureTracker *>(1);
  const PressureDiffs *LastPDiffs = reinterpret_cast<PressureDiffs *>(1);
  const LiveIntervals *LastLIS = reinterpret_cast<LiveIntervals *>(1);
  bool LastLaneMasks = true;

  void buildGraph(ScheduleDAGMI &DAG, AAResults *AA,
                  RegPressureTracker *RPTracker, PressureDiffs *PDiffs,
                  LiveIntervals *LIS, bool TrackLaneMasks) override {
    ++Calls;
    LastAA = AA;
    LastRP = RPTracker;
    LastPDiffs = PDiffs;
    LastLIS = LIS;
    LastLaneMasks = TrackLaneMasks;
  }
};

class TestScheduleDAGMI : public ScheduleDAGMI {
public:
  using ScheduleDAGMI::ScheduleDAGMI;

  // Same AA-only shape as ScheduleDAGMI::schedule and the no-pressure arm of
  // ScheduleDAGMILive::buildDAGWithRegPressure, with explicit nullptrs.
  void invokeBuildGraph() {
    SchedImpl->buildGraph(*this, AA, /*RPTracker=*/nullptr, /*PDiffs=*/nullptr,
                          /*LIS=*/nullptr, /*TrackLaneMasks=*/false);
  }
};

std::unique_ptr<MachineFunction> makeLayoutMF(LLVMContext &Ctx, Module &Mod) {
  auto MF = createMachineFunction(Ctx, Mod);
  MachineBasicBlock *A = MF->CreateMachineBasicBlock();
  MachineBasicBlock *B = MF->CreateMachineBasicBlock();
  MachineBasicBlock *C = MF->CreateMachineBasicBlock();
  // Layout C, A, B is not creation order (0,1,2).
  MF->push_back(C);
  MF->push_back(A);
  MF->push_back(B);
  return MF;
}

TEST(MachineSchedStrategyTest, DefaultNextBlockLayoutOrderOnce) {
  LLVMContext Ctx;
  Module Mod("Module", Ctx);
  auto MF = makeLayoutMF(Ctx, Mod);
  MachineBasicBlock *C = &*MF->begin();
  MachineBasicBlock *A = &*std::next(MF->begin());
  MachineBasicBlock *B = &*std::next(MF->begin(), 2);

  StubStrategy S;
  S.enterFunction(MF.get());
  EXPECT_EQ(S.nextBlock(), C);
  EXPECT_EQ(S.nextBlock(), A);
  EXPECT_EQ(S.nextBlock(), B);
  EXPECT_EQ(S.nextBlock(), nullptr);
  S.leaveFunction();
}

TEST(MachineSchedStrategyTest, OverrideMayReturnSameMBBTwice) {
  LLVMContext Ctx;
  Module Mod("Module", Ctx);
  auto MF = makeLayoutMF(Ctx, Mod);
  MachineBasicBlock *First = &*MF->begin();

  RepeatFirstTwiceStrategy S;
  S.enterFunction(MF.get());
  EXPECT_EQ(S.nextBlock(), First);
  EXPECT_EQ(S.nextBlock(), First);
  EXPECT_EQ(S.nextBlock(), nullptr);
  S.leaveFunction();
}

TEST(MachineSchedStrategyTest, ScheduleDAGMINextBlockForwards) {
  LLVMContext Ctx;
  Module Mod("Module", Ctx);
  auto MF = makeLayoutMF(Ctx, Mod);
  MachineBasicBlock *C = &*MF->begin();
  MachineBasicBlock *A = &*std::next(MF->begin());
  MachineBasicBlock *B = &*std::next(MF->begin(), 2);

  MachineSchedContext SchedCtx;
  SchedCtx.MF = MF.get();
  auto Impl = std::make_unique<StubStrategy>();
  ScheduleDAGMI DAG(&SchedCtx, std::move(Impl), /*RemoveKillFlags=*/true);
  DAG.startSchedule(MF.get());
  EXPECT_EQ(DAG.nextBlock(), C);
  EXPECT_EQ(DAG.nextBlock(), A);
  EXPECT_EQ(DAG.nextBlock(), B);
  EXPECT_EQ(DAG.nextBlock(), nullptr);
  DAG.finalizeSchedule();
}

TEST(MachineSchedStrategyTest, ScheduleDAGMIStartScheduleForwardsEnterFunction) {
  LLVMContext Ctx;
  Module Mod("Module", Ctx);
  auto MF = makeLayoutMF(Ctx, Mod);

  MachineSchedContext SchedCtx;
  SchedCtx.MF = MF.get();
  auto Impl = std::make_unique<CountingEnterFunctionStrategy>();
  CountingEnterFunctionStrategy *Raw = Impl.get();
  ScheduleDAGMI DAG(&SchedCtx, std::move(Impl), /*RemoveKillFlags=*/true);
  DAG.startSchedule(MF.get());
  EXPECT_EQ(Raw->Enters, 1u);
  EXPECT_EQ(Raw->Seen, MF.get());
  EXPECT_EQ(DAG.nextBlock(), &*MF->begin());
  EXPECT_NE(DAG.nextBlock(), nullptr);
  EXPECT_NE(DAG.nextBlock(), nullptr);
  EXPECT_EQ(DAG.nextBlock(), nullptr);
  DAG.finalizeSchedule();
}

TEST(MachineSchedStrategyTest, IdentityBuildGraphClearsDAG) {
  LLVMContext Ctx;
  Module Mod("Module", Ctx);
  auto MF = createMachineFunction(Ctx, Mod);
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineSchedContext SchedCtx;
  SchedCtx.MF = MF.get();
  TestScheduleDAGMI DAG(&SchedCtx, std::make_unique<StubStrategy>(),
                        /*RemoveKillFlags=*/true);
  DAG.SUnits.emplace_back();
  ASSERT_FALSE(DAG.SUnits.empty());
  DAG.startBlock(MBB);
  DAG.enterRegion(MBB, MBB->begin(), MBB->end(), /*regioninstrs=*/0);
  DAG.invokeBuildGraph();
  EXPECT_TRUE(DAG.SUnits.empty());
  DAG.exitRegion();
  DAG.finishBlock();
}

TEST(MachineSchedStrategyTest, OverrideBuildGraphIsDispatched) {
  LLVMContext Ctx;
  Module Mod("Module", Ctx);
  auto MF = createMachineFunction(Ctx, Mod);
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineSchedContext SchedCtx;
  SchedCtx.MF = MF.get();
  auto Impl = std::make_unique<CountingBuildGraphStrategy>();
  CountingBuildGraphStrategy *Raw = Impl.get();
  TestScheduleDAGMI DAG(&SchedCtx, std::move(Impl), /*RemoveKillFlags=*/true);
  DAG.SUnits.emplace_back();
  DAG.startBlock(MBB);
  DAG.enterRegion(MBB, MBB->begin(), MBB->end(), /*regioninstrs=*/0);
  DAG.invokeBuildGraph();
  EXPECT_EQ(Raw->Calls, 1u);
  EXPECT_EQ(Raw->LastAA, nullptr);
  EXPECT_EQ(Raw->LastRP, nullptr);
  EXPECT_EQ(Raw->LastPDiffs, nullptr);
  EXPECT_EQ(Raw->LastLIS, nullptr);
  EXPECT_FALSE(Raw->LastLaneMasks);
  EXPECT_EQ(DAG.SUnits.size(), 1u);
  DAG.exitRegion();
  DAG.finishBlock();
}

} // end namespace
