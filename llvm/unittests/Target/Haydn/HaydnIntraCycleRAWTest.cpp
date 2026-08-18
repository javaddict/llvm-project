//===- HaydnIntraCycleRAWTest.cpp - no-forwarding RAW walk contract -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// REGRESSION TEST: F49 — haydnHasIntraCycleRAW is writer-first (reader vs
// PREVIOUS live defs only). A later live def vs an earlier read in the same
// LiveDefs walk was a silent miss. Safe for HR/RC via Data latency >= 1.
//
// Contract:
//   * Incremental walk trips writer-then-reader (true RAW).
//   * Incremental walk misses reader-then-writer (legal WAR / silent miss).
//   * haydnPairHasIntraCycleRAW is the closed both-direction check (same
//     read-vs-live-def mechanism, both orders) — not a pack-reject.
//   * haydnLiveDefsWalkHidesLaterDef is true iff the incremental walk hid
//     that later-def / earlier-read shape.
//   * Dead defs and SFR are not live-def RAW; R0 is a real register.
//
// What breaks if this regresses: a LiveDefs walk can again miss a later
// writer against an earlier reader with no pair/hide detector.
//
//===----------------------------------------------------------------------===//

#include "HaydnIntraCycleRAW.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallSet.h"
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

class HaydnIntraCycleRAWTest : public testing::Test {
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
    M = std::make_unique<Module>("HaydnIntraCycleRAW", *Ctx);
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
};

// Writer-then-reader: incremental LiveDefs walk trips (true RAW).
TEST_F(HaydnIntraCycleRAWTest, IncrementalWriterThenReaderTrips) {
  MachineInstr &Writer = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &Reader = add32(Haydn::R4, Haydn::R1, Haydn::R5);

  SmallSet<Register, 8> LiveDefs;
  EXPECT_FALSE(haydnHasIntraCycleRAW(Writer, LiveDefs, TRI()));
  haydnAppendLiveDefs(Writer, LiveDefs);
  EXPECT_TRUE(LiveDefs.contains(Haydn::R1));
  EXPECT_TRUE(haydnHasIntraCycleRAW(Reader, LiveDefs, TRI()));
}

// Reader-then-writer: incremental walk misses (legal WAR / F49 silent miss).
TEST_F(HaydnIntraCycleRAWTest, IncrementalReaderThenWriterMisses) {
  MachineInstr &Reader = add32(Haydn::R4, Haydn::R1, Haydn::R5);
  MachineInstr &Writer = add32(Haydn::R1, Haydn::R2, Haydn::R3);

  SmallSet<Register, 8> LiveDefs;
  EXPECT_FALSE(haydnHasIntraCycleRAW(Reader, LiveDefs, TRI()));
  haydnAppendLiveDefs(Reader, LiveDefs);
  EXPECT_FALSE(LiveDefs.contains(Haydn::R1));
  EXPECT_FALSE(haydnHasIntraCycleRAW(Writer, LiveDefs, TRI()))
      << "incremental predicate is writer-first; later def must not trip it";
}

// Closed both-direction pair check uses the same mechanism in both orders.
TEST_F(HaydnIntraCycleRAWTest, PairCheckBothOrders) {
  MachineInstr &Writer = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &Reader = add32(Haydn::R4, Haydn::R1, Haydn::R5);

  EXPECT_TRUE(haydnPairHasIntraCycleRAW(Writer, Reader, TRI()));
  EXPECT_TRUE(haydnPairHasIntraCycleRAW(Reader, Writer, TRI()));
}

// Hide detector: reader-then-writer is the silent-miss shape; writer-first
// is not hidden (incremental already trips).
TEST_F(HaydnIntraCycleRAWTest, WalkHidesLaterDefOnlyWhenReaderFirst) {
  MachineInstr &Writer = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &Reader = add32(Haydn::R4, Haydn::R1, Haydn::R5);

  const MachineInstr *WriterFirst[] = {&Writer, &Reader};
  const MachineInstr *ReaderFirst[] = {&Reader, &Writer};

  EXPECT_FALSE(haydnLiveDefsWalkHidesLaterDef(WriterFirst, TRI()));
  EXPECT_TRUE(haydnLiveDefsWalkHidesLaterDef(ReaderFirst, TRI()));

  haydnAssertWriterFirstLiveDefsWalk(WriterFirst, TRI());
}

// Legal WAR snapshot {add32 r1,r1,r2; add32 r2,r3,r4}: incremental does not
// trip (pack-legal). Pair trips because a live def vs read exists either
// order — that is why pair is not a pack-reject.
TEST_F(HaydnIntraCycleRAWTest, SnapshotWARIncrementalLegalPairReports) {
  MachineInstr &First = add32(Haydn::R1, Haydn::R1, Haydn::R2);
  MachineInstr &Second = add32(Haydn::R2, Haydn::R3, Haydn::R4);

  SmallSet<Register, 8> LiveDefs;
  EXPECT_FALSE(haydnHasIntraCycleRAW(First, LiveDefs, TRI()));
  haydnAppendLiveDefs(First, LiveDefs);
  EXPECT_FALSE(haydnHasIntraCycleRAW(Second, LiveDefs, TRI()));

  EXPECT_TRUE(haydnPairHasIntraCycleRAW(First, Second, TRI()));
  const MachineInstr *Order[] = {&First, &Second};
  EXPECT_TRUE(haydnLiveDefsWalkHidesLaterDef(Order, TRI()));
}

// Dead write has no consumer — not a live-def RAW in either order.
TEST_F(HaydnIntraCycleRAWTest, DeadDefIsNotLiveRAW) {
  MachineInstr &Writer = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  Writer.getOperand(0).setIsDead();
  MachineInstr &Reader = add32(Haydn::R4, Haydn::R1, Haydn::R5);

  SmallSet<Register, 8> LiveDefs;
  haydnAppendLiveDefs(Writer, LiveDefs);
  EXPECT_TRUE(LiveDefs.empty());
  EXPECT_FALSE(haydnHasIntraCycleRAW(Reader, LiveDefs, TRI()));
  EXPECT_FALSE(haydnPairHasIntraCycleRAW(Writer, Reader, TRI()));
}

// SFR is excluded from the live-def RAW set (WAW/ports own one-SFR-writer).
TEST_F(HaydnIntraCycleRAWTest, SFRDefExcludedFromLiveRAW) {
  MachineInstr &Writer = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  Writer.addOperand(MachineOperand::CreateReg(Haydn::SFR, /*isDef=*/true));
  MachineInstr &SFRReader = add32(Haydn::R4, Haydn::R5, Haydn::R6);
  SFRReader.addOperand(MachineOperand::CreateReg(Haydn::SFR, /*isDef=*/false));

  SmallSet<Register, 8> LiveDefs;
  haydnAppendLiveDefs(Writer, LiveDefs);
  EXPECT_TRUE(LiveDefs.contains(Haydn::R1));
  EXPECT_FALSE(LiveDefs.contains(Haydn::SFR));
  EXPECT_FALSE(haydnHasIntraCycleRAW(SFRReader, LiveDefs, TRI()));
  EXPECT_FALSE(haydnRegOverlapsLiveSet(Haydn::SFR, LiveDefs, TRI()));
}

// R0 is a real register (soft-zero); a live R0 def vs a same-cycle reader
// is true RAW under the incremental walk.
TEST_F(HaydnIntraCycleRAWTest, R0IsNotExcluded) {
  MachineInstr &Writer = add32(Haydn::R0, Haydn::R2, Haydn::R3);
  MachineInstr &Reader = add32(Haydn::R4, Haydn::R0, Haydn::R5);

  SmallSet<Register, 8> LiveDefs;
  haydnAppendLiveDefs(Writer, LiveDefs);
  EXPECT_TRUE(LiveDefs.contains(Haydn::R0));
  EXPECT_TRUE(haydnHasIntraCycleRAW(Reader, LiveDefs, TRI()));
  EXPECT_TRUE(haydnPairHasIntraCycleRAW(Writer, Reader, TRI()));
}

// Independent registers: no RAW either order.
TEST_F(HaydnIntraCycleRAWTest, IndependentRegsNoRAW) {
  MachineInstr &A = add32(Haydn::R1, Haydn::R2, Haydn::R3);
  MachineInstr &B = add32(Haydn::R4, Haydn::R5, Haydn::R6);

  SmallSet<Register, 8> LiveDefs;
  haydnAppendLiveDefs(A, LiveDefs);
  EXPECT_FALSE(haydnHasIntraCycleRAW(B, LiveDefs, TRI()));
  EXPECT_FALSE(haydnPairHasIntraCycleRAW(A, B, TRI()));
  const MachineInstr *Order[] = {&A, &B};
  EXPECT_FALSE(haydnLiveDefsWalkHidesLaterDef(Order, TRI()));
  haydnAssertWriterFirstLiveDefsWalk(Order, TRI());
}

// Pre-RA virtual registers match by Register identity.
TEST_F(HaydnIntraCycleRAWTest, VirtualRegIdentity) {
  MachineRegisterInfo &MRI = MF->getRegInfo();
  Register VDef = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
  Register VOther = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
  Register VUse = VDef;

  MachineInstr &Writer = add32(VDef, Haydn::R2, Haydn::R3);
  MachineInstr &Reader = add32(Haydn::R4, VUse, Haydn::R5);
  MachineInstr &Unrelated = add32(Haydn::R6, VOther, Haydn::R7);

  SmallSet<Register, 8> LiveDefs;
  haydnAppendLiveDefs(Writer, LiveDefs);
  EXPECT_TRUE(haydnHasIntraCycleRAW(Reader, LiveDefs, TRI()));
  EXPECT_FALSE(haydnHasIntraCycleRAW(Unrelated, LiveDefs, TRI()));
  EXPECT_TRUE(haydnPairHasIntraCycleRAW(Writer, Reader, TRI()));
  EXPECT_FALSE(haydnPairHasIntraCycleRAW(Writer, Unrelated, TRI()));
}

} // end anonymous namespace
