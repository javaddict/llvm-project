//===- HaydnPortModelSFRMemberTest.cpp - CB-161 SFR member law -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// CB-161 (2026-08-21) unit seal for the SFR port law at the committed-member
// layer:
//   * haydnIsPrivateFormatEMemberOpcode classifies generated private Format E
//     members (complete-inverse rows) and rejects logical shells / NOP.
//   * countSFRPorts charges member-shape ANONYMOUS implicit(-def) $sfr
//     operands — generated member descs may drop the logical's
//     Uses/Defs=[SFR] naming (X2MOVT32_E3_*_R has none), so the operand set
//     is the port truth, not the desc.
//   * haydnCycleMembersExceedPortBudget rejects a cycle pooling more than
//     one member SFR write or more than two member SFR reads (golden
//     VLIW_Engine_Compiler_Constraints.md §Registers: "Only one instruction
//     per bundle is allowed to write to an SFR"; SFR read ports 2).
//
// Silent regression shape: countSFRPorts going back to desc-named-only
// charging — three member SFR readers (MOVESFR2GPR + two MOVTs) then pass
// every HR / commit / verify gate and the 2R ceiling is unenforced again.
//
// Lit peer: llvm/test/CodeGen/Haydn/cb161-dual-sfr-mux-separate.ll
// (end-to-end x2mux32 + x4mux16 epoch separation).
//
//===----------------------------------------------------------------------===//

#include "HaydnBundlePortBudget.h"
#include "HaydnPortModel.h"
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

class HaydnPortModelSFRMemberTest : public testing::Test {
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
    M = std::make_unique<Module>("HaydnPortModelSFRMember", *Ctx);
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

  /// X2MOVT32_E3_E2_ALU2_R rd, rsd, implicit $sfr — the member whose
  /// generated desc dropped the logical's Uses=[SFR] naming. The anonymous
  /// implicit use is exactly the operand the old desc-named-only walk
  /// skipped.
  MachineInstr &x2movt32Member(Register Rd, Register Acc, Register Src) {
    return *BuildMI(*MBB, MBB->end(), DebugLoc(),
                    TII().get(Haydn::X2MOVT32_E3_E2_ALU2_R), Rd)
                .addReg(Acc)
                .addReg(Src)
                .addReg(Haydn::SFR, RegState::Implicit);
  }

  /// X2SLT32_E3_E2_ALU2_R rs1, rs2, implicit-def $sfr — SFR-writing member.
  /// The regenerated member desc carries Defs=[SFR] (golden
  /// SFR_Write_Port law, 2026-08-21), and BuildMI materializes the
  /// desc's implicit def automatically — no manual operand (adding one
  /// would double-count the write).
  MachineInstr &x2slt32Member(Register Rs1, Register Rs2) {
    return *BuildMI(*MBB, MBB->end(), DebugLoc(),
                    TII().get(Haydn::X2SLT32_E3_E2_ALU2_R))
                .addReg(Rs1)
                .addReg(Rs2);
  }

  /// MOVESFR2GPR_E3_E2_ALU2_SFR rd, implicit $sfr — SFR-reading member.
  MachineInstr &movesfr2gprMember(Register Rd) {
    return *BuildMI(*MBB, MBB->end(), DebugLoc(),
                    TII().get(Haydn::MOVESFR2GPR_E3_E2_ALU2_SFR), Rd)
                .addReg(Haydn::SFR, RegState::Implicit);
  }
};

TEST_F(HaydnPortModelSFRMemberTest, MemberOpcodeClassification) {
  // Generated private members (complete-inverse rows) classify true.
  EXPECT_TRUE(
      haydnIsPrivateFormatEMemberOpcode(Haydn::X2MOVT32_E3_E2_ALU2_R));
  EXPECT_TRUE(
      haydnIsPrivateFormatEMemberOpcode(Haydn::X2SLT32_E3_E2_ALU2_R));
  EXPECT_TRUE(
      haydnIsPrivateFormatEMemberOpcode(Haydn::MOVESFR2GPR_E3_E2_ALU2_SFR));
  // Authored logical shells and NOP are not members.
  EXPECT_FALSE(haydnIsPrivateFormatEMemberOpcode(Haydn::X2MOVT32));
  EXPECT_FALSE(haydnIsPrivateFormatEMemberOpcode(Haydn::ADD32));
  EXPECT_FALSE(haydnIsPrivateFormatEMemberOpcode(Haydn::NOP));
}

TEST_F(HaydnPortModelSFRMemberTest, AnonymousMemberSfrOperandCharges) {
  // X2MOVT32 member desc carries no Uses=[SFR]; the anonymous implicit
  // $sfr use must still charge one SFR read (operand truth, CB-161).
  MachineInstr &Reader = x2movt32Member(Haydn::D0, Haydn::D0, Haydn::D1);
  EXPECT_FALSE(haydnDescNamesSfrPort(Reader));
  auto [Reads, Writes] = countSFRPorts(Reader);
  EXPECT_EQ(Reads, 1u);
  EXPECT_EQ(Writes, 0u);

  // X2SLT32 member's anonymous implicit-def $sfr charges one write.
  MachineInstr &Writer = x2slt32Member(Haydn::D0, Haydn::D1);
  std::tie(Reads, Writes) = countSFRPorts(Writer);
  EXPECT_EQ(Reads, 0u);
  EXPECT_EQ(Writes, 1u);
}

TEST_F(HaydnPortModelSFRMemberTest, PortBudgetRejectsMemberSfrOverload) {
  // One reader + one writer + one reader-writer chain member pools to
  // 2R1W — legal (matches the accepted epoch packs).
  MachineInstr &R1 = movesfr2gprMember(Haydn::R1);
  MachineInstr &W1 = x2slt32Member(Haydn::D0, Haydn::D1);
  MachineInstr &R2 = x2movt32Member(Haydn::D0, Haydn::D0, Haydn::D1);
  SmallVector<MachineInstr *, 3> Legal = {&R1, &W1, &R2};
  EXPECT_FALSE(haydn::bundle::haydnCycleMembersExceedPortBudget(Legal));

  // CB-161 shape: TWO SFR-writing members in one cycle — over 1W.
  MachineInstr &W2 = x2slt32Member(Haydn::D2, Haydn::D3);
  SmallVector<MachineInstr *, 3> DualWriters = {&W1, &W2};
  EXPECT_TRUE(haydn::bundle::haydnCycleMembersExceedPortBudget(DualWriters));

  // THREE SFR-reading members in one cycle — over 2R.
  MachineInstr &R3 = movesfr2gprMember(Haydn::R2);
  SmallVector<MachineInstr *, 3> TripleReaders = {&R1, &R2, &R3};
  EXPECT_TRUE(haydn::bundle::haydnCycleMembersExceedPortBudget(TripleReaders));
}

} // namespace
