//===- HaydnRegisterBankInfoTest.cpp - AR bank mapping invariant ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// REGRESSION TEST: inverted AR-bank assert in
// GISel/HaydnRegisterBankInfo.cpp getInstrMapping (GOALS W36, peer-compare
// audit 2026-08-15 N5).
//
// Bug: the 64-bit operand mapping path asserted
//   (!RC || RC != &Haydn::ARRegClass)
// immediately BEFORE the `if (RC == &Haydn::ARRegClass)` branch that maps
// AR-constrained operands to the AR bank. The assert fired exactly on the
// correct path (RC == ARRegClass), making the AR branch unreachable and
// aborting assertions-ON builds on the first AR-constrained 64-bit operand.
//
// Fix: the assert now sits after the branch and states the real invariant —
// an AR-constrained 64-bit operand must map to the AR bank, never to the
// size-derived DR64 default.
//
// Test design: drives getInstrMapping directly on a G_IMPLICIT_DEF whose
// result vreg is typed s64 and constrained to ARRegClass. A MIR/lit test is
// not possible: AR is isAllocatable=0 (see ar-unallocatable-class.mir), MIR
// parsing rejects `class: ar` vregs, and MRI::setRegClass /
// createVirtualRegister assert allocatability. The unit test installs the AR
// constraint via MRI::setRegClassOrRegBank, the one legal entry that performs
// no allocatability check — the same state RegBankSelect would see if an AR
// constraint ever became reachable. Before the fix this test aborts on the
// assert; after it, the operand maps to the AR bank. A DR64-classed s64
// control pins that the size-derived default still applies when the operand
// is not AR-constrained.
//
//===----------------------------------------------------------------------===//

#include "GISel/HaydnRegisterBankInfo.h"
#include "Haydn.h"
#include "HaydnRegisterInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
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

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTarget();
extern "C" void LLVMInitializeHaydnTargetMC();

#include "gtest/gtest.h"

#include <memory>

using namespace llvm;

namespace {

class HaydnRegisterBankInfoTest : public testing::Test {
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
    M = std::make_unique<Module>("HaydnRBI", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);
  }

  const HaydnRegisterBankInfo &RBI() const {
    return *static_cast<const HaydnRegisterBankInfo *>(ST->getRegBankInfo());
  }
};

// An s64 operand constrained to ARRegClass must map to the AR bank, not the
// size-derived DR64 default. Aborted on the old inverted assert.
TEST_F(HaydnRegisterBankInfoTest, ARConstrained64BitMapsToARBank) {
  MachineRegisterInfo &MRI = MF->getRegInfo();

  Register ARReg = MRI.createIncompleteVirtualRegister("ar64");
  MRI.setType(ARReg, LLT::scalar(64));
  // The one constraint-entry with no allocatability check; AR is
  // isAllocatable=0 so every other setter refuses this class.
  MRI.setRegClassOrRegBank(ARReg, &Haydn::ARRegClass);
  ASSERT_EQ(MRI.getRegClassOrNull(ARReg), &Haydn::ARRegClass);

  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);
  MachineInstr &MI =
      *BuildMI(*MBB, MBB->end(), DebugLoc(),
               ST->getInstrInfo()->get(TargetOpcode::G_IMPLICIT_DEF), ARReg);

  const RegisterBankInfo::InstructionMapping &Mapping = RBI().getInstrMapping(MI);
  EXPECT_TRUE(Mapping.isValid());
  ASSERT_EQ(Mapping.getNumOperands(), 1u);
  const RegisterBankInfo::ValueMapping &OpMapping = Mapping.getOperandMapping(0);
  ASSERT_EQ(OpMapping.NumBreakDowns, 1u);
  ASSERT_NE(OpMapping.BreakDown->RegBank, nullptr);
  EXPECT_EQ(OpMapping.BreakDown->RegBank->getID(), Haydn::ARRegBankID);
}

// Control: an unconstrained s64 operand (no RC) still picks the DR64
// size-derived default, so the AR branch is a true refinement, not a
// replacement.
TEST_F(HaydnRegisterBankInfoTest, Unconstrained64BitStillMapsToDR64) {
  MachineRegisterInfo &MRI = MF->getRegInfo();

  Register DRReg = MRI.createIncompleteVirtualRegister("s64");
  MRI.setType(DRReg, LLT::scalar(64));
  ASSERT_EQ(MRI.getRegClassOrNull(DRReg), nullptr);

  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);
  MachineInstr &MI =
      *BuildMI(*MBB, MBB->end(), DebugLoc(),
               ST->getInstrInfo()->get(TargetOpcode::G_IMPLICIT_DEF), DRReg);

  const RegisterBankInfo::InstructionMapping &Mapping = RBI().getInstrMapping(MI);
  EXPECT_TRUE(Mapping.isValid());
  ASSERT_EQ(Mapping.getNumOperands(), 1u);
  const RegisterBankInfo::ValueMapping &OpMapping = Mapping.getOperandMapping(0);
  ASSERT_EQ(OpMapping.NumBreakDowns, 1u);
  ASSERT_NE(OpMapping.BreakDown->RegBank, nullptr);
  EXPECT_EQ(OpMapping.BreakDown->RegBank->getID(), Haydn::DR64RegBankID);
}

// REGRESSION TEST (GOALS W45): a BANK-ONLY AR-constrained 64-bit vreg
// (ARRegBank set, no register class) must also map to the AR bank.
//
// Bug: getInstrMapping's generic 64-bit branch recognized only the
// ARRegClass form; a bank-only AR vreg fell to the size-derived DR64
// default, and the COPY-like early path consulted size only (W36 residual).
// RegBankSelect applies this mapping as a bank reassign, so the AR
// constraint was silently dropped. The selector-side twin (W45) hoisted
// preConstrainBankOnlyVRegs to once-per-MF and made it bank-aware; this
// test pins the RBI half of the same one rule (isARConstrained).
//
// If this regresses, the operand maps to DR64RegBankID and the EXPECT_EQ
// on ARRegBankID fails — the exact mis-bank the MIR lit test
// gisel/bank-aware-preconstrain.mir observes end-to-end.
TEST_F(HaydnRegisterBankInfoTest, BankOnlyARConstrained64BitMapsToARBank) {
  MachineRegisterInfo &MRI = MF->getRegInfo();

  Register ARBankReg = MRI.createIncompleteVirtualRegister("ar64bank");
  MRI.setType(ARBankReg, LLT::scalar(64));
  // Bank-only constraint (the state RegBankSelect/selector-twin see before
  // any class is set). The RBI must honor it, not the size default.
  const RegisterBank *ARBank =
      &ST->getRegBankInfo()->getRegBank(Haydn::ARRegBankID);
  MRI.setRegClassOrRegBank(ARBankReg, ARBank);
  ASSERT_EQ(MRI.getRegClassOrNull(ARBankReg), nullptr);
  ASSERT_NE(MRI.getRegBankOrNull(ARBankReg), nullptr);
  ASSERT_EQ(MRI.getRegBankOrNull(ARBankReg)->getID(), Haydn::ARRegBankID);

  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);
  MachineInstr &MI =
      *BuildMI(*MBB, MBB->end(), DebugLoc(),
               ST->getInstrInfo()->get(TargetOpcode::G_IMPLICIT_DEF), ARBankReg);

  const RegisterBankInfo::InstructionMapping &Mapping = RBI().getInstrMapping(MI);
  EXPECT_TRUE(Mapping.isValid());
  ASSERT_EQ(Mapping.getNumOperands(), 1u);
  const RegisterBankInfo::ValueMapping &OpMapping = Mapping.getOperandMapping(0);
  ASSERT_EQ(OpMapping.NumBreakDowns, 1u);
  ASSERT_NE(OpMapping.BreakDown->RegBank, nullptr);
  EXPECT_EQ(OpMapping.BreakDown->RegBank->getID(), Haydn::ARRegBankID);
}

// P10 alternative mappings wait for a measured T-DSP8 miss. AIE alts are
// PTR-vs-GPR; Haydn has no PTR bank. Pin the empty set so a later invent
// without a measured miss fails this test.
TEST_F(HaydnRegisterBankInfoTest, NoAlternativeMappingsUntilMeasuredMiss) {
  MachineRegisterInfo &MRI = MF->getRegInfo();

  Register Dst = MRI.createIncompleteVirtualRegister("s32dst");
  Register Src = MRI.createIncompleteVirtualRegister("s32src");
  MRI.setType(Dst, LLT::scalar(32));
  MRI.setType(Src, LLT::scalar(32));

  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);
  MachineInstr &Add =
      *BuildMI(*MBB, MBB->end(), DebugLoc(),
               ST->getInstrInfo()->get(TargetOpcode::G_ADD), Dst)
           .addReg(Src)
           .addReg(Src);
  MachineInstr &Copy =
      *BuildMI(*MBB, MBB->end(), DebugLoc(),
               ST->getInstrInfo()->get(TargetOpcode::COPY), Dst)
           .addReg(Src);

  EXPECT_TRUE(RBI().getInstrAlternativeMappings(Add).empty());
  EXPECT_TRUE(RBI().getInstrAlternativeMappings(Copy).empty());
}

} // end anonymous namespace
