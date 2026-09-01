//===- HaydnDR64PackLazyFITest.cpp - lazy pack-slot FI after PEI -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// expandPostRAPseudo of LOADI64 (both-halves-nonzero) and MOV_GPR_TO_DR64
// (two live GPRs) resolves DR64PackFI / DR64PackBaseSpillFI. Generic
// ExpandPostRAPseudos runs after PEI but before the earliest freeze snapshot,
// so a reservation-scan miss that CreateStackObject'd here grew the frame
// invisibly. Gate: FI<0 && MachineFrameInfo::isCalleeSavedInfoValid (PEI ran)
// is fatal. CSI invalid is the -run-pass=postrapseudos probe lane and still
// lazy-creates. determineCalleeSaves uses haydnInstrNeedsDR64PackSlot — the
// same predicate as expandPostRAPseudo. RISC-V getMoveF64FrameIndex lazy
// create is ISel-time, not a post-RA pattern (RISCVMachineFunctionInfo.h).
//
//===----------------------------------------------------------------------===//

#include "HaydnFrameLowering.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
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

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTarget();
extern "C" void LLVMInitializeHaydnTargetMC();

using namespace llvm;

namespace llvm {
bool haydnInstrNeedsDR64PackSlot(const MachineInstr &MI);
} // namespace llvm

namespace {

class HaydnDR64PackLazyFITest : public testing::Test {
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
    M = std::make_unique<Module>("HaydnDR64PackLazyFI", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);
    MF->initTargetMachineFunctionInfo(*ST);
    MF->getRegInfo().freezeReservedRegs();
    MBB = MF->CreateMachineBasicBlock();
    MF->push_back(MBB);
  }

  const HaydnInstrInfo &TII() const { return *ST->getInstrInfo(); }
  HaydnMachineFunctionInfo &Info() {
    return *MF->getInfo<HaydnMachineFunctionInfo>();
  }
  MachineFrameInfo &Frame() { return MF->getFrameInfo(); }

  MachineInstr &movPack(Register Dst, Register Lo, Register Hi) {
    return *BuildMI(*MBB, MBB->end(), DebugLoc(),
                    TII().get(Haydn::MOV_GPR_TO_DR64), Dst)
                .addReg(Lo)
                .addReg(Hi);
  }

  MachineInstr &loadI64BothHalves() {
    // Hi=5, Lo=0x36 — neither half is the register-only fast path.
    return *BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::LOADI64),
                    Haydn::D0)
                .addImm(0x500000036LL);
  }

  /// Live-out every scavenger candidate so withDR64PackBase must spill.
  void makeAllScratchGPRsLiveOut() {
    MachineBasicBlock *Succ = MF->CreateMachineBasicBlock();
    MF->push_back(Succ);
    MBB->addSuccessor(Succ);
    const MCPhysReg Live[] = {Haydn::R1, Haydn::R2,  Haydn::R3,  Haydn::R4,
                              Haydn::R5, Haydn::R6,  Haydn::R7,  Haydn::R8,
                              Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
    for (MCPhysReg R : Live)
      Succ->addLiveIn(R);
  }

  /// Offset 256 → scaled elements 64/65/32, none of which fit isInt<6>.
  int reservePackFIWithLargeOffset() {
    int FI = Frame().CreateStackObject(/*Size=*/8, /*Alignment=*/Align(8),
                                       /*SpillSlot=*/true);
    Frame().setObjectOffset(FI, 256);
    Info().setDR64PackFI(FI);
    return FI;
  }
};

TEST_F(HaydnDR64PackLazyFITest, ProbeLaneCSIInvalidStillLazyCreatesPackFI) {
  EXPECT_FALSE(Frame().isCalleeSavedInfoValid());
  EXPECT_LT(Info().getDR64PackFI(), 0);
  const unsigned NumBefore = Frame().getNumObjects();
  MachineInstr &Pack = movPack(Haydn::D0, Haydn::R1, Haydn::R2);
  EXPECT_TRUE(TII().expandPostRAPseudo(Pack));
  EXPECT_GE(Info().getDR64PackFI(), 0);
  EXPECT_GT(Frame().getNumObjects(), NumBefore);
}

TEST_F(HaydnDR64PackLazyFITest, CSIValidWithReservedPackFIDoesNotFatal) {
  int FI = Frame().CreateStackObject(/*Size=*/8, /*Alignment=*/Align(8),
                                     /*SpillSlot=*/true);
  Info().setDR64PackFI(FI);
  Frame().setCalleeSavedInfoValid(true);
  MachineInstr &Pack = movPack(Haydn::D0, Haydn::R1, Haydn::R2);
  EXPECT_TRUE(TII().expandPostRAPseudo(Pack));
  EXPECT_EQ(Info().getDR64PackFI(), FI);
}

TEST_F(HaydnDR64PackLazyFITest,
       ProbeLaneCSIInvalidStillLazyCreatesBaseSpillFI) {
  reservePackFIWithLargeOffset();
  makeAllScratchGPRsLiveOut();
  EXPECT_FALSE(Frame().isCalleeSavedInfoValid());
  EXPECT_LT(Info().getDR64PackBaseSpillFI(), 0);
  MachineInstr &Pack = movPack(Haydn::D0, Haydn::R1, Haydn::R2);
  EXPECT_TRUE(TII().expandPostRAPseudo(Pack));
  EXPECT_GE(Info().getDR64PackBaseSpillFI(), 0);
}

TEST_F(HaydnDR64PackLazyFITest, PredicateMatchesTwoLiveGprPack) {
  EXPECT_TRUE(haydnInstrNeedsDR64PackSlot(movPack(Haydn::D0, Haydn::R1, Haydn::R2)));
}

TEST_F(HaydnDR64PackLazyFITest, PredicateRejectsR0Half) {
  EXPECT_FALSE(
      haydnInstrNeedsDR64PackSlot(movPack(Haydn::D0, Haydn::R0, Haydn::R2)));
  EXPECT_FALSE(
      haydnInstrNeedsDR64PackSlot(movPack(Haydn::D0, Haydn::R1, Haydn::R0)));
  EXPECT_FALSE(
      haydnInstrNeedsDR64PackSlot(movPack(Haydn::D0, Haydn::R0, Haydn::R0)));
}

TEST_F(HaydnDR64PackLazyFITest, PredicateMatchesLoadI64BothHalves) {
  EXPECT_TRUE(haydnInstrNeedsDR64PackSlot(loadI64BothHalves()));
}

TEST_F(HaydnDR64PackLazyFITest, PredicateRejectsLoadI64RegisterOnly) {
  MachineInstr &Zext = *BuildMI(*MBB, MBB->end(), DebugLoc(),
                                TII().get(Haydn::LOADI64), Haydn::D0)
                            .addImm(0x36);
  EXPECT_FALSE(haydnInstrNeedsDR64PackSlot(Zext));
  MachineInstr &Sext = *BuildMI(*MBB, MBB->end(), DebugLoc(),
                                TII().get(Haydn::LOADI64), Haydn::D0)
                            .addImm(-1);
  EXPECT_FALSE(haydnInstrNeedsDR64PackSlot(Sext));
}

TEST_F(HaydnDR64PackLazyFITest, DetermineCalleeSavesReservesForTwoLiveGpr) {
  movPack(Haydn::D0, Haydn::R1, Haydn::R2);
  BitVector SavedRegs;
  ST->getFrameLowering()->determineCalleeSaves(*MF, SavedRegs, nullptr);
  EXPECT_GE(Info().getDR64PackFI(), 0);
  EXPECT_GE(Info().getDR64PackBaseSpillFI(), 0);
}

TEST_F(HaydnDR64PackLazyFITest, DetermineCalleeSavesSkipsR0Half) {
  movPack(Haydn::D0, Haydn::R0, Haydn::R2);
  BitVector SavedRegs;
  ST->getFrameLowering()->determineCalleeSaves(*MF, SavedRegs, nullptr);
  EXPECT_LT(Info().getDR64PackFI(), 0);
  EXPECT_LT(Info().getDR64PackBaseSpillFI(), 0);
}

TEST_F(HaydnDR64PackLazyFITest, DetermineCalleeSavesReservesForLoadI64BothHalves) {
  loadI64BothHalves();
  BitVector SavedRegs;
  ST->getFrameLowering()->determineCalleeSaves(*MF, SavedRegs, nullptr);
  EXPECT_GE(Info().getDR64PackFI(), 0);
  EXPECT_GE(Info().getDR64PackBaseSpillFI(), 0);
}

TEST_F(HaydnDR64PackLazyFITest, DetermineCalleeSavesSkipsLoadI64ZeroExtend) {
  BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::LOADI64), Haydn::D0)
      .addImm(0x36);
  BitVector SavedRegs;
  ST->getFrameLowering()->determineCalleeSaves(*MF, SavedRegs, nullptr);
  EXPECT_LT(Info().getDR64PackFI(), 0);
}

TEST_F(HaydnDR64PackLazyFITest, CSIValidAfterReservationScanDoesNotFatal) {
  MachineInstr &Pack = movPack(Haydn::D0, Haydn::R1, Haydn::R2);
  BitVector SavedRegs;
  ST->getFrameLowering()->determineCalleeSaves(*MF, SavedRegs, nullptr);
  Frame().setCalleeSavedInfoValid(true);
  EXPECT_TRUE(TII().expandPostRAPseudo(Pack));
  EXPECT_GE(Info().getDR64PackFI(), 0);
}

TEST_F(HaydnDR64PackLazyFITest, CSIValidRegisterOnlyLoadI64DoesNotFatal) {
  Frame().setCalleeSavedInfoValid(true);
  MachineInstr &LI = *BuildMI(*MBB, MBB->end(), DebugLoc(),
                              TII().get(Haydn::LOADI64), Haydn::D0)
                          .addImm(0x36);
  EXPECT_TRUE(TII().expandPostRAPseudo(LI));
  EXPECT_LT(Info().getDR64PackFI(), 0);
}

TEST_F(HaydnDR64PackLazyFITest, CSIValidR0HalfMovDoesNotFatal) {
  Frame().setCalleeSavedInfoValid(true);
  MachineInstr &Pack = movPack(Haydn::D0, Haydn::R0, Haydn::R2);
  EXPECT_TRUE(TII().expandPostRAPseudo(Pack));
  EXPECT_LT(Info().getDR64PackFI(), 0);
}

using HaydnDR64PackLazyFIDeathTest = HaydnDR64PackLazyFITest;

#if GTEST_HAS_DEATH_TEST
TEST_F(HaydnDR64PackLazyFIDeathTest, CSIValidUnsetPackFIIsFatal) {
  Frame().setCalleeSavedInfoValid(true);
  EXPECT_LT(Info().getDR64PackFI(), 0);
  MachineInstr &Pack = movPack(Haydn::D0, Haydn::R1, Haydn::R2);
  EXPECT_DEATH(TII().expandPostRAPseudo(Pack),
               "lazy DR64PackFI CreateStackObject after PEI");
}

TEST_F(HaydnDR64PackLazyFIDeathTest, CSIValidUnsetPackFILoadI64IsFatal) {
  Frame().setCalleeSavedInfoValid(true);
  MachineInstr &LI = loadI64BothHalves();
  EXPECT_DEATH(TII().expandPostRAPseudo(LI),
               "lazy DR64PackFI CreateStackObject after PEI");
}

TEST_F(HaydnDR64PackLazyFIDeathTest, CSIValidUnsetBaseSpillFIIsFatal) {
  reservePackFIWithLargeOffset();
  makeAllScratchGPRsLiveOut();
  Frame().setCalleeSavedInfoValid(true);
  EXPECT_LT(Info().getDR64PackBaseSpillFI(), 0);
  MachineInstr &Pack = movPack(Haydn::D0, Haydn::R1, Haydn::R2);
  EXPECT_DEATH(TII().expandPostRAPseudo(Pack),
               "lazy DR64PackBaseSpillFI CreateStackObject after PEI");
}
#endif

} // namespace
