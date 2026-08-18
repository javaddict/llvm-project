//===- HaydnIntraCycleWAWTest.cpp - no-dual-write WAW law contract -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// REGRESSION TEST: W39 — the same-cycle WAW law existed as THREE divergent
// predicates. materialize's cycleMembersHaveWAW skipped dead defs, so
// commitExactMultiMIProductCycle could commit a dead-def dual write the
// HR/ResourceCycle law ("no dual write regardless of liveness") rejects —
// one law, two models, hard constraint #7 violation.
//
// Fix: golden's no-dual-write law ("Instructions within the same bundle must
// not write to the same register"; "Only one instruction per bundle is
// allowed to write to an SFR") hoisted into ONE shared predicate
// (HaydnIntraCycleWAW.h). materialize (cycleMembersHaveWAW), HR
// (hasSameBundleWAW/appendDefs), and ResourceCycle
// (canReserveResources(MI)/reserveResources(MI)) all route through it.
//
// Contract:
//   * A DEAD def dual-writing a register another member already def'd IS a
//     WAW collision (the old materialize law wrongly skipped it).
//   * Live-def dual writes collide (unchanged, both old laws agreed).
//   * Two dead implicit-def $sfr members collide (one SFR writer per cycle).
//   * Dead def vs a READ of the same reg is NOT WAW (that is the RAW law's
//     business; dead-def cohabitation stays RAW-legal).
//   * Disjoint registers (any liveness) do not collide.
//   * R0 counts (soft-zero is a real register).
//   * Virtual registers match by identity (pre-RA); the list fold
//     (haydnCycleMembersHaveWAW) is order-independent.
//
// What breaks if this regresses: commitExactMultiMIProductCycle silently
// accepts a dead-def dual write again; SMS/HR keep rejecting it — two
// placement authorities disagree on one hardware law and the committed MIR
// contains a WRITE_CONFLICT bundle.
//
//===----------------------------------------------------------------------===//

#include "HaydnIntraCycleWAW.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallSet.h"
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

class HaydnIntraCycleWAWTest : public testing::Test {
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
    M = std::make_unique<Module>("HaydnIntraCycleWAW", *Ctx);
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

  /// ADD32 Rd, Rs, Rt.
  MachineInstr &add32(Register Rd, Register Rs, Register Rt) {
    return *BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::ADD32), Rd)
                .addReg(Rs)
                .addReg(Rt);
  }

  /// Append an `implicit-def [dead] $reg` operand to MI and return it.
  MachineInstr &withImplicitDef(MachineInstr &MI, Register Reg, bool Dead) {
    MI.addOperand(MachineOperand::CreateReg(
        Reg, /*isDef=*/true, /*isImp=*/true, /*isKill=*/false,
        /*isDead=*/Dead, /*isUndef=*/false, /*isEarlyClobber=*/false,
        /*SubReg=*/0));
    return MI;
  }
};

// THE W39 regression: a DEAD def dual-writing a register a previous member
// already def'd IS a WAW collision. The old materialize predicate skipped
// dead defs and committed this shape as a bundle.
TEST_F(HaydnIntraCycleWAWTest, DeadDefDualWriteCollides) {
  MachineInstr &First = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &Second = add32(Haydn::R1, Haydn::R4, Haydn::R5);
  Second.getOperand(0).setIsDead();

  SmallSet<Register, 8> Defs;
  haydnAppendCycleDefs(First, Defs);
  EXPECT_TRUE(Defs.contains(Haydn::R1));
  EXPECT_TRUE(haydnHasIntraCycleWAW(Second, Defs, TRI()));

  MachineInstr *List[] = {&First, &Second};
  EXPECT_TRUE(haydnCycleMembersHaveWAW(List, TRI()));
  MachineInstr *Rev[] = {&Second, &First};
  EXPECT_TRUE(haydnCycleMembersHaveWAW(Rev, TRI()))
      << "dual writes are order-independent";
}

// Both defs dead still collides: liveness never gates the write port.
TEST_F(HaydnIntraCycleWAWTest, BothDeadDualWriteCollides) {
  MachineInstr &First = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  First.getOperand(0).setIsDead();
  MachineInstr &Second = add32(Haydn::R1, Haydn::R4, Haydn::R5);
  Second.getOperand(0).setIsDead();

  SmallSet<Register, 8> Defs;
  haydnAppendCycleDefs(First, Defs);
  EXPECT_TRUE(Defs.contains(Haydn::R1)) << "dead defs enter the WAW set";
  EXPECT_TRUE(haydnHasIntraCycleWAW(Second, Defs, TRI()));
}

// Two dead implicit-def $sfr members collide: one SFR writer per cycle.
// This is the exact shape the old materialize law committed (two ALU
// members each carrying implicit-def dead $sfr) while HR rejected it.
TEST_F(HaydnIntraCycleWAWTest, DualDeadSFRDefsCollide) {
  MachineInstr &First = withImplicitDef(add32(Haydn::R1, Haydn::R2, Haydn::R3),
                                        Haydn::SFR, /*Dead=*/true);
  MachineInstr &Second =
      withImplicitDef(add32(Haydn::R4, Haydn::R5, Haydn::R6), Haydn::SFR,
                      /*Dead=*/true);

  SmallSet<Register, 8> Defs;
  haydnAppendCycleDefs(First, Defs);
  EXPECT_TRUE(Defs.contains(Haydn::SFR)) << "dead SFR defs enter the WAW set";
  EXPECT_TRUE(haydnHasIntraCycleWAW(Second, Defs, TRI()));
  MachineInstr *List[] = {&First, &Second};
  EXPECT_TRUE(haydnCycleMembersHaveWAW(List, TRI()));
}

// Live dual write still collides (both old laws agreed on this half).
TEST_F(HaydnIntraCycleWAWTest, LiveDualWriteCollides) {
  MachineInstr &First = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &Second = add32(Haydn::R1, Haydn::R4, Haydn::R5);

  SmallSet<Register, 8> Defs;
  haydnAppendCycleDefs(First, Defs);
  EXPECT_TRUE(haydnHasIntraCycleWAW(Second, Defs, TRI()));
}

// Dead def vs a READ of the same register is NOT WAW — that shape is the
// RAW law's domain (dead-def cohabitation stays RAW-legal). The WAW
// predicate must not swallow it.
TEST_F(HaydnIntraCycleWAWTest, DeadDefVersusReadIsNotWAW) {
  MachineInstr &First = add32(Haydn::R4, Haydn::R5, Haydn::R6);
  First.getOperand(0).setIsDead();
  MachineInstr &Reader = add32(Haydn::R7, Haydn::R4, Haydn::R5);

  SmallSet<Register, 8> Defs;
  haydnAppendCycleDefs(First, Defs);
  EXPECT_FALSE(haydnHasIntraCycleWAW(Reader, Defs, TRI()))
      << "a read is not a write; WAW only sees defs";
  MachineInstr *List[] = {&First, &Reader};
  EXPECT_FALSE(haydnCycleMembersHaveWAW(List, TRI()));
}

// Disjoint registers never collide regardless of liveness.
TEST_F(HaydnIntraCycleWAWTest, DisjointRegsNoWAW) {
  MachineInstr &First = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &Second = add32(Haydn::R4, Haydn::R5, Haydn::R6);
  Second.getOperand(0).setIsDead();

  SmallSet<Register, 8> Defs;
  haydnAppendCycleDefs(First, Defs);
  EXPECT_FALSE(haydnHasIntraCycleWAW(Second, Defs, TRI()));
  MachineInstr *List[] = {&First, &Second};
  EXPECT_FALSE(haydnCycleMembersHaveWAW(List, TRI()));
}

// R0 is soft-zero, a real register: a dual R0 write collides.
TEST_F(HaydnIntraCycleWAWTest, R0IsNotExcluded) {
  MachineInstr &First = add32(Haydn::R0, Haydn::R2, Haydn::R3);
  MachineInstr &Second = add32(Haydn::R0, Haydn::R4, Haydn::R5);
  Second.getOperand(0).setIsDead();

  SmallSet<Register, 8> Defs;
  haydnAppendCycleDefs(First, Defs);
  EXPECT_TRUE(haydnHasIntraCycleWAW(Second, Defs, TRI()));
}

// Physreg alias overlap (sub/super-register) collides under TRI; regs not in
// the set (including SFR when it was never def'd) do not match.
TEST_F(HaydnIntraCycleWAWTest, PhysregAliasOverlaps) {
  MachineInstr &First = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &Second = add32(Haydn::R1, Haydn::R4, Haydn::R5);
  Second.getOperand(0).setIsDead();

  SmallSet<Register, 8> Defs;
  haydnAppendCycleDefs(First, Defs);
  EXPECT_TRUE(haydnRegOverlapsDefSet(Haydn::R1, Defs, TRI()));
  EXPECT_FALSE(haydnRegOverlapsDefSet(Haydn::R7, Defs, TRI()));
  EXPECT_FALSE(haydnRegOverlapsDefSet(Haydn::SFR, Defs, TRI()))
      << "SFR matches only when def'd — it is not special-cased out";
}

// Pre-RA virtual registers match by Register identity.
TEST_F(HaydnIntraCycleWAWTest, VirtualRegIdentity) {
  MachineRegisterInfo &MRI = MF->getRegInfo();
  Register VDef = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
  Register VOther = MRI.createVirtualRegister(&Haydn::GPR32RegClass);

  MachineInstr &First = add32(VDef, Haydn::R2, Haydn::R3);
  MachineInstr &Second = add32(VDef, Haydn::R4, Haydn::R5);
  Second.getOperand(0).setIsDead();
  MachineInstr &Unrelated = add32(VOther, Haydn::R6, Haydn::R7);

  SmallSet<Register, 8> Defs;
  haydnAppendCycleDefs(First, Defs);
  EXPECT_TRUE(haydnHasIntraCycleWAW(Second, Defs, TRI()));
  EXPECT_FALSE(haydnHasIntraCycleWAW(Unrelated, Defs, TRI()));
}

// The three authorities share ONE law: the materialize list-fold form and
// the incremental HR/RC form must agree on every shape above. This is the
// W39 pin — a second WAW model beside this predicate is a hard-#7 bug.
TEST_F(HaydnIntraCycleWAWTest, ListFoldAgreesWithIncrementalWalk) {
  MachineInstr &A = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &B = add32(Haydn::R1, Haydn::R4, Haydn::R5);
  B.getOperand(0).setIsDead();
  MachineInstr &C = add32(Haydn::R8, Haydn::R9, Haydn::R10);
  MachineInstr *List[] = {&A, &B, &C};

  // Incremental (HR/RC shape): writer-issues-first accumulation.
  bool Incremental = false;
  {
    SmallSet<Register, 8> Defs;
    haydnAppendCycleDefs(A, Defs);
    Incremental = haydnHasIntraCycleWAW(B, Defs, TRI());
    if (!Incremental) {
      haydnAppendCycleDefs(B, Defs);
      Incremental = haydnHasIntraCycleWAW(C, Defs, TRI());
    }
  }

  EXPECT_TRUE(haydnCycleMembersHaveWAW(List, TRI()));
  EXPECT_TRUE(Incremental)
      << "incremental (HR/RC) and list-fold (materialize) forms must agree";

  // Control: disjoint trio agrees on NO collision in both forms.
  MachineInstr &D = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &E = add32(Haydn::R4, Haydn::R5, Haydn::R6);
  MachineInstr &F = add32(Haydn::R8, Haydn::R9, Haydn::R10);
  E.getOperand(0).setIsDead();
  MachineInstr *Clean[] = {&D, &E, &F};
  EXPECT_FALSE(haydnCycleMembersHaveWAW(Clean, TRI()));
  {
    SmallSet<Register, 8> Defs;
    haydnAppendCycleDefs(D, Defs);
    EXPECT_FALSE(haydnHasIntraCycleWAW(E, Defs, TRI()));
    haydnAppendCycleDefs(E, Defs);
    EXPECT_FALSE(haydnHasIntraCycleWAW(F, Defs, TRI()));
  }
}

} // end anonymous namespace
