//===- HaydnEstimateCyclesTest.cpp - pre-RA cycle advisory contract -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// W68.1: estimateCyclesAcrossAvailableFormats is the contract's ONLY pre-RA
// format API (contracts/pipeline.md). It must:
//   * return nullopt when any packable body MI has no golden Format E row
//     (fail closed — an RA-legal tuple with no alternate is a schema gap);
//   * pack packable MIs at the admitted E3 entry capacity (2/3 -> ceil),
//     never below one cycle;
//   * exclude loop-control, PHI and metadata MIs (they own no Format E
//     entry);
//   * stay witness-free: the result carries a cycle count only.
//
//===----------------------------------------------------------------------===//

#include "HaydnPipelinerLoopInfo.h"
#include "HaydnFormatERecords.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
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

class HaydnEstimateCyclesTest : public testing::Test {
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
    M = std::make_unique<Module>("HaydnEstimateCycles", *Ctx);
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

  /// ADD32 Rd, Rs, Rt.
  MachineInstr &add32(Register Rd, Register Rs, Register Rt) {
    return *BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::ADD32), Rd)
                .addReg(Rs)
                .addReg(Rt);
  }

  /// SEQ32 Rd, Rs, Rt (GPR32 compare — golden-admitted).
  MachineInstr &seq32(Register Rd, Register Rs, Register Rt) {
    return *BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::SEQ32), Rd)
                .addReg(Rs)
                .addReg(Rt);
  }

  HaydnPipelinerLoopInfo loopInfo() const {
    return HaydnPipelinerLoopInfo(MF.get(), &TII(), nullptr, nullptr,
                                  /*TripCountReg=*/Register());
  }
};

// E3 capacity is 3: three packable MIs pack into one cycle.
TEST_F(HaydnEstimateCyclesTest, ThreePackableOneCycle) {
  MachineInstr &A = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &B = seq32(Haydn::R4, Haydn::R5, Haydn::R6);
  MachineInstr &C = add32(Haydn::R7, Haydn::R1, Haydn::R4);
  SmallVector<MachineInstr *, 3> Body{&A, &B, &C};

  auto Est = loopInfo().estimateCyclesAcrossAvailableFormats(Body);
  ASSERT_TRUE(Est.has_value());
  EXPECT_EQ(Est->Cycles, 1u);
}

// 4..6 packable MIs -> 2 cycles (ceil at E3 capacity 3).
TEST_F(HaydnEstimateCyclesTest, SixPackableTwoCycles) {
  MachineInstr &A = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &B = seq32(Haydn::R4, Haydn::R5, Haydn::R6);
  MachineInstr &C = add32(Haydn::R7, Haydn::R1, Haydn::R4);
  MachineInstr &D = seq32(Haydn::R8, Haydn::R9, Haydn::R10);
  MachineInstr &E = add32(Haydn::R11, Haydn::R8, Haydn::R9);
  MachineInstr &F = seq32(Haydn::R12, Haydn::R11, Haydn::R10);
  SmallVector<MachineInstr *, 6> Body{&A, &B, &C, &D, &E, &F};

  auto Est = loopInfo().estimateCyclesAcrossAvailableFormats(Body);
  ASSERT_TRUE(Est.has_value());
  EXPECT_EQ(Est->Cycles, 2u);
}

// An empty / all-metadata body costs exactly one architectural cycle.
TEST_F(HaydnEstimateCyclesTest, EmptyBodyOneCycle) {
  SmallVector<MachineInstr *, 1> Body;
  auto Est = loopInfo().estimateCyclesAcrossAvailableFormats(Body);
  ASSERT_TRUE(Est.has_value());
  EXPECT_EQ(Est->Cycles, 1u);
}

// PHIs and terminators own no Format E entry; they do not extend the bound.
TEST_F(HaydnEstimateCyclesTest, PhiAndTerminatorExcluded) {
  MachineInstr &A = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr *Phi = BuildMI(*MBB, MBB->end(), DebugLoc(),
                              TII().get(TargetOpcode::PHI))
                          .addReg(Haydn::R1)
                          .addReg(Haydn::R2)
                          .addReg(Haydn::R3)
                          .getInstr();
  MachineInstr *Bnz = BuildMI(*MBB, MBB->end(), DebugLoc(),
                              TII().get(Haydn::BNEZ), 0)
                          .addReg(Haydn::R1)
                          .addImm(0)
                          .getInstr();
  // One packable (ADD32) + PHI + terminator -> still one cycle.
  SmallVector<MachineInstr *, 3> Body{&A, Phi, Bnz};
  auto Est = loopInfo().estimateCyclesAcrossAvailableFormats(Body);
  ASSERT_TRUE(Est.has_value());
  EXPECT_EQ(Est->Cycles, 1u);
}

// A packable MI with no golden Format E row fails closed (nullopt), never a
// silently invented cycle number. LOADI32 is a codegen-only pseudo (expanded
// post-RA; no golden row of its own), so it is packable-shape but uncovered.
TEST_F(HaydnEstimateCyclesTest, UncoveredLogicalFailsClosed) {
  MachineInstr &Uncovered = *BuildMI(*MBB, MBB->end(), DebugLoc(),
                                     TII().get(Haydn::LOADI32), Haydn::R1)
                                .addReg(Haydn::R2)
                                .addImm(0);
  SmallVector<MachineInstr *, 1> Body{&Uncovered};
  EXPECT_FALSE(
      loopInfo().estimateCyclesAcrossAvailableFormats(Body).has_value());
}

// The result is witness-free: it carries only the cycle count.
static_assert(std::is_same<decltype(CycleEstimate{}.Cycles), unsigned>::value,
              "CycleEstimate must carry only a cycle count");

} // namespace
