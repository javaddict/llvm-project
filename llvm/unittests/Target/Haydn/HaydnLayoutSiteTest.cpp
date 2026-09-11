//===- HaydnLayoutSiteTest.cpp - GR1.3 typed site table -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// collect binds published RelocFieldInfo + root/member; raiseRank is
// monotone; missing site is fail-closed. dropMember before erase so
// Prev cannot alias a recycled MI (MachineFunction.cpp:493). ARM
// ImmBranch is MI+MaxDisp only (ARMConstantIslandPass.cpp:188-197).
//
//===----------------------------------------------------------------------===//

#include "HaydnInstrInfo.h"
#include "HaydnLayoutSite.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "MCTargetDesc/HaydnRelocLayout.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"
#include "gtest/gtest.h"

#include <memory>
#include <string>

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTarget();
extern "C" void LLVMInitializeHaydnTargetMC();

using namespace llvm;
using namespace llvm::haydn;

namespace {

class HaydnLayoutSiteTest : public testing::Test {
protected:
  std::unique_ptr<HaydnTargetMachine> TM;
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<MachineModuleInfo> MMI;
  std::unique_ptr<HaydnSubtarget> ST;
  std::unique_ptr<MachineFunction> MF;
  MachineBasicBlock *BB0 = nullptr;
  MachineBasicBlock *BB1 = nullptr;

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
    M = std::make_unique<Module>("HaydnLayoutSite", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);
    BB0 = MF->CreateMachineBasicBlock();
    BB1 = MF->CreateMachineBasicBlock();
    MF->push_back(BB0);
    MF->push_back(BB1);
    BB0->addSuccessor(BB1);
  }

  const HaydnInstrInfo &TII() const { return *ST->getInstrInfo(); }
};

TEST_F(HaydnLayoutSiteTest, CollectBindsRelocFieldInfoRootMember) {
  MachineInstr &Br =
      *BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::B)).addMBB(BB1);
  BuildMI(*BB1, BB1->end(), DebugLoc(), TII().get(Haydn::RET));

  LayoutSiteTable Table;
  std::string Err;
  ASSERT_TRUE(Table.collect(*MF, TII(), Err)) << Err;
  LayoutSite *S = Table.findByMember(&Br);
  ASSERT_NE(S, nullptr);
  EXPECT_EQ(S->Kind, LayoutSiteKind::Branch);
  EXPECT_EQ(S->Root, &Br);
  EXPECT_EQ(S->Member, &Br);
  EXPECT_EQ(S->Dest, BB1);
  EXPECT_EQ(S->FieldKind, HaydnReloc::RelocKind::WIDE_BranchSImm12);
  EXPECT_EQ(&S->fieldInfo(), &HaydnReloc::getRelocFieldInfo(
                                 HaydnReloc::RelocKind::WIDE_BranchSImm12));
  EXPECT_EQ(S->fieldInfo().FieldSize,
            HaydnReloc::getRelocFieldInfo(HaydnReloc::RelocKind::WIDE_BranchSImm12)
                .FieldSize);
  EXPECT_EQ(S->Rank, LayoutSiteRank::FitPatch);
  EXPECT_TRUE(S->canFitPatch());
}

TEST_F(HaydnLayoutSiteTest, CollectHwLoopBindsOff1) {
  MachineInstr &Set =
      *BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::SET_HWLOOP_W))
           .addImm(0)
           .addMBB(BB1)
           .addMBB(BB1)
           .addImm(4);
  BuildMI(*BB1, BB1->end(), DebugLoc(), TII().get(Haydn::RET));

  LayoutSiteTable Table;
  std::string Err;
  ASSERT_TRUE(Table.collect(*MF, TII(), Err)) << Err;
  LayoutSite *S = Table.findByMember(&Set);
  ASSERT_NE(S, nullptr);
  EXPECT_EQ(S->Kind, LayoutSiteKind::HWLoop);
  EXPECT_EQ(S->Root, S->Member);
  EXPECT_EQ(S->Dest, BB1);
  EXPECT_EQ(S->Dest2, BB1);
  EXPECT_EQ(S->FieldKind, HaydnReloc::RelocKind::HWLoopOff1);
  EXPECT_EQ(S->FieldKind2, HaydnReloc::RelocKind::HWLoopOff2);
  EXPECT_EQ(&S->fieldInfo(),
            &HaydnReloc::getRelocFieldInfo(HaydnReloc::RelocKind::HWLoopOff1));
  EXPECT_EQ(S->Rank, LayoutSiteRank::FitPatch);
}

TEST_F(HaydnLayoutSiteTest, CollectPreservesRaisedHwLoopRank) {
  MachineInstr &Set =
      *BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::SET_HWLOOP_W))
           .addImm(0)
           .addMBB(BB1)
           .addMBB(BB1)
           .addImm(4);
  BuildMI(*BB1, BB1->end(), DebugLoc(), TII().get(Haydn::RET));

  LayoutSiteTable Table;
  std::string Err;
  ASSERT_TRUE(Table.collect(*MF, TII(), Err)) << Err;
  LayoutSite *S = Table.findByMember(&Set);
  ASSERT_NE(S, nullptr);
  S->raiseRank(LayoutSiteRank::NeutralizedNop);
  S->Template = LayoutTemplateId::HwLoopSoftLatch;
  ASSERT_TRUE(Table.collect(*MF, TII(), Err)) << Err;
  S = Table.findByMember(&Set);
  ASSERT_NE(S, nullptr);
  EXPECT_EQ(S->Rank, LayoutSiteRank::NeutralizedNop);
  EXPECT_EQ(S->Template, LayoutTemplateId::HwLoopSoftLatch);
  EXPECT_EQ(S->Kind, LayoutSiteKind::HWLoop);
  EXPECT_EQ(S->FieldKind, HaydnReloc::RelocKind::HWLoopOff1);
}

TEST_F(HaydnLayoutSiteTest, DropMemberBeforeEraseDoesNotResurrectOnRecycledMI) {
  // D1.172: InstructionRecycler (MachineFunction.cpp:493) can hand the
  // erased SET address to a fresh BNEZ. dropMember nulls Site.Member so
  // collect Prev cannot restore NeutralizedNop / HwLoopSoftLatch.
  MachineInstr *Set =
      BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::SET_HWLOOP_W))
          .addImm(0)
          .addMBB(BB1)
          .addMBB(BB1)
          .addImm(4);
  BuildMI(*BB1, BB1->end(), DebugLoc(), TII().get(Haydn::RET));

  LayoutSiteTable Table;
  std::string Err;
  ASSERT_TRUE(Table.collect(*MF, TII(), Err)) << Err;
  LayoutSite *S = Table.findByMember(Set);
  ASSERT_NE(S, nullptr);
  S->raiseRank(LayoutSiteRank::NeutralizedNop);
  S->Template = LayoutTemplateId::HwLoopSoftLatch;
  Table.dropMember(Set);
  EXPECT_EQ(Table.findByMember(Set), nullptr);

  Set->eraseFromParent();
  MachineInstr *Bnez =
      BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::BNEZ))
          .addReg(Haydn::R1)
          .addMBB(BB1);
  ASSERT_TRUE(Table.collect(*MF, TII(), Err)) << Err;
  S = Table.findByMember(Bnez);
  ASSERT_NE(S, nullptr);
  EXPECT_EQ(S->Kind, LayoutSiteKind::Branch);
  EXPECT_EQ(S->Rank, LayoutSiteRank::FitPatch);
  EXPECT_EQ(S->Template, LayoutTemplateId::None);
  EXPECT_TRUE(S->canFitPatch());
}

TEST_F(HaydnLayoutSiteTest, CollectKindMismatchDoesNotRestoreHwLoopRank) {
  MachineInstr *Set =
      BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::SET_HWLOOP_W))
          .addImm(0)
          .addMBB(BB1)
          .addMBB(BB1)
          .addImm(4);
  BuildMI(*BB1, BB1->end(), DebugLoc(), TII().get(Haydn::RET));

  LayoutSiteTable Table;
  std::string Err;
  ASSERT_TRUE(Table.collect(*MF, TII(), Err)) << Err;
  LayoutSite *S = Table.findByMember(Set);
  ASSERT_NE(S, nullptr);
  S->raiseRank(LayoutSiteRank::NeutralizedNop);
  S->Template = LayoutTemplateId::HwLoopSoftLatch;
  Set->eraseFromParent();
  MachineInstr *Br =
      BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::B)).addMBB(BB1);
  ASSERT_TRUE(Table.collect(*MF, TII(), Err)) << Err;
  S = Table.findByMember(Br);
  ASSERT_NE(S, nullptr);
  EXPECT_EQ(S->Kind, LayoutSiteKind::Branch);
  EXPECT_EQ(S->Rank, LayoutSiteRank::FitPatch);
  EXPECT_NE(S->Template, LayoutTemplateId::HwLoopSoftLatch);
}

TEST_F(HaydnLayoutSiteTest, MissingSiteFailClosed) {
  MachineInstr &Add =
      *BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::ADD32), Haydn::R1)
           .addReg(Haydn::R2)
           .addReg(Haydn::R3);
  BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::B)).addMBB(BB1);
  BuildMI(*BB1, BB1->end(), DebugLoc(), TII().get(Haydn::RET));

  LayoutSiteTable Table;
  std::string Err;
  ASSERT_TRUE(Table.collect(*MF, TII(), Err)) << Err;
  EXPECT_EQ(Table.findByMember(&Add), nullptr);
}

TEST_F(HaydnLayoutSiteTest, RaiseRankMonotoneAndFitPatchIdempotent) {
  LayoutSite S;
  S.FieldKind = HaydnReloc::RelocKind::WIDE_BranchSImm12;
  EXPECT_EQ(S.Rank, LayoutSiteRank::FitPatch);
  S.raiseRank(LayoutSiteRank::FitPatch);
  EXPECT_EQ(S.Rank, LayoutSiteRank::FitPatch);
  EXPECT_TRUE(S.canFitPatch());
  S.raiseRank(LayoutSiteRank::LongTemplate);
  EXPECT_EQ(S.Rank, LayoutSiteRank::LongTemplate);
  EXPECT_FALSE(S.canFitPatch());
}

TEST(HaydnLayoutSiteStandalone, JalrSImm12FitPatchFailClosed) {
  LayoutSite S;
  S.FieldKind = HaydnReloc::RelocKind::JALRSImm12;
  S.Rank = LayoutSiteRank::FitPatch;
  EXPECT_FALSE(S.canFitPatch());
  EXPECT_EQ(&S.fieldInfo(),
            &HaydnReloc::getRelocFieldInfo(HaydnReloc::RelocKind::JALRSImm12));
}

using HaydnLayoutSiteDeathTest = HaydnLayoutSiteTest;

#if GTEST_HAS_DEATH_TEST
TEST_F(HaydnLayoutSiteDeathTest, RaiseRankDecreaseIsFatal) {
  LayoutSite S;
  S.raiseRank(LayoutSiteRank::LongTemplate);
  EXPECT_DEATH(S.raiseRank(LayoutSiteRank::FitPatch), "rank decrease");
}

TEST_F(HaydnLayoutSiteDeathTest, MissingRequireMemberIsFatal) {
  MachineInstr &Add =
      *BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::ADD32), Haydn::R1)
           .addReg(Haydn::R2)
           .addReg(Haydn::R3);
  BuildMI(*BB1, BB1->end(), DebugLoc(), TII().get(Haydn::RET));
  LayoutSiteTable Table;
  std::string Err;
  ASSERT_TRUE(Table.collect(*MF, TII(), Err)) << Err;
  EXPECT_DEATH(Table.requireMember(&Add, "HaydnLayoutSiteTest"),
               "missing layout site");
}
#endif

} // namespace
