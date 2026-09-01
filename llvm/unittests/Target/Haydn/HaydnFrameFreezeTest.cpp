//===- HaydnFrameFreezeTest.cpp - frame-deadline snapshot law -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// Unit seal for the frame-deadline snapshot pair on HaydnMachineFunctionInfo:
//   * no snapshot -> no violation (MIR fixtures bypassing the post-PEI lane)
//   * snapshot + unchanged frame -> empty violation
//   * snapshot + new object (CreateStackObject) -> violation names objects
//   * snapshot + stack-size change -> violation names stack
//   * earliest-wins: a second take keeps the first values
//   * equal-size offset swap (count+size unchanged) is a violation
//
// Live-FI offset/size vectors sit on HaydnMachineFunctionInfo (no snapshot
// type; AIE has none). Dead FIs are skipped (getObjectOffset asserts).
//
//===----------------------------------------------------------------------===//

#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

class HaydnFrameFreezeTest : public testing::Test {
protected:
  std::unique_ptr<HaydnTargetMachine> TM;
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<MachineModuleInfo> MMI;
  std::unique_ptr<HaydnSubtarget> ST;
  std::unique_ptr<MachineFunction> MF;

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
    M = std::make_unique<Module>("HaydnFrameFreeze", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);
    HFInfo = std::make_unique<HaydnMachineFunctionInfo>(*F, ST.get());
  }

  HaydnMachineFunctionInfo &Info() { return *HFInfo; }
  MachineFrameInfo &Frame() { return MF->getFrameInfo(); }

private:
  std::unique_ptr<HaydnMachineFunctionInfo> HFInfo;
};

TEST_F(HaydnFrameFreezeTest, NoSnapshotNoViolation) {
  EXPECT_FALSE(Info().hasFrameFreezeSnapshot());
  EXPECT_TRUE(Info().frameFreezeViolation(Frame()).empty());
}

TEST_F(HaydnFrameFreezeTest, UnchangedFrameIsClean) {
  Info().takeFrameFreezeSnapshot(Frame());
  EXPECT_TRUE(Info().hasFrameFreezeSnapshot());
  EXPECT_TRUE(Info().frameFreezeViolation(Frame()).empty());
}

TEST_F(HaydnFrameFreezeTest, ObjectCreationIsAViolation) {
  Info().takeFrameFreezeSnapshot(Frame());
  Frame().CreateStackObject(/*Size=*/4, /*Alignment=*/Align(4),
                            /*SpillSlot=*/true);
  const std::string V = Info().frameFreezeViolation(Frame());
  EXPECT_NE(V.find("frame grew"), std::string::npos) << V;
  EXPECT_NE(V.find("objects"), std::string::npos) << V;
}

TEST_F(HaydnFrameFreezeTest, StackSizeChangeIsAViolation) {
  Info().takeFrameFreezeSnapshot(Frame());
  Frame().setStackSize(Frame().getStackSize() + 8);
  const std::string V = Info().frameFreezeViolation(Frame());
  EXPECT_NE(V.find("stack"), std::string::npos) << V;
}

TEST_F(HaydnFrameFreezeTest, EarliestSnapshotWins) {
  Info().takeFrameFreezeSnapshot(Frame());
  Frame().CreateStackObject(/*Size=*/4, /*Alignment=*/Align(4),
                            /*SpillSlot=*/true);
  Info().takeFrameFreezeSnapshot(Frame());
  const std::string V = Info().frameFreezeViolation(Frame());
  EXPECT_FALSE(V.empty()) << "second take must keep the earliest values";
}

TEST_F(HaydnFrameFreezeTest, EqualSizeOffsetSwapIsAViolation) {
  const int A = Frame().CreateStackObject(/*Size=*/8, /*Alignment=*/Align(8),
                                          /*SpillSlot=*/true);
  const int B = Frame().CreateStackObject(/*Size=*/8, /*Alignment=*/Align(8),
                                          /*SpillSlot=*/true);
  const int Dead =
      Frame().CreateStackObject(/*Size=*/8, /*Alignment=*/Align(8),
                                /*SpillSlot=*/true);
  Frame().RemoveStackObject(Dead);
  Frame().setObjectOffset(A, -8);
  Frame().setObjectOffset(B, -16);
  Frame().setStackSize(16);

  Info().takeFrameFreezeSnapshot(Frame());
  ASSERT_TRUE(Info().frameFreezeViolation(Frame()).empty());

  const int64_t OffA = Frame().getObjectOffset(A);
  Frame().setObjectOffset(A, Frame().getObjectOffset(B));
  Frame().setObjectOffset(B, OffA);

  EXPECT_EQ(Frame().getNumObjects(), 3u);
  EXPECT_EQ(Frame().getStackSize(), 16u);

  const std::string V = Info().frameFreezeViolation(Frame());
  EXPECT_FALSE(V.empty()) << "equal-size offset swap must be a violation";
  EXPECT_NE(V.find("offset"), std::string::npos) << V;
}

} // namespace
