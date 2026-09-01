//===- HaydnHWLoopDemoteTest.cpp - demote helper laws -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// Dedicated unit seal for the HaydnHWLoopDemote helpers that decide the
// software-loop demote's value-preserve law (CB-162/CB-165):
//
//   * regClobberedNonCountdownIn — "does the loop BODY redefine Prefer?"
//     CB-165 makes this load-bearing: when true, Prefer's exit value
//     originates from that body def (MachinePipeliner routinely assigns
//     the loop-carried stage value to the same physreg the ZOL trip used),
//     so a preheader trip save + exit restore would overwrite the live
//     body value (pr51581-2 @ -O2: c[N-1] received the raw trip).
//   * isCountdownStepOf — the closed predicate separating a proven ±1
//     residual countdown (strippable) from real compute on the same reg
//     (a clobber). A misclassification here is silent wrong code in BOTH
//     directions: strip erases live compute, or a live trip survives as a
//     bogus counter.
//   * regMentionedInBlocks / collectLoopBlocks — the CFG-blocks authority
//     the above walk (layout ranges miss a latch earlier in the function).
//
// The pass-level shape (which save/restore variant each scratch/body
// combination emits) is pinned end-to-end by
// llvm/test/CodeGen/Haydn/cb165-hwloop-demote-body-redefined-tripreg.ll
// and cb162-hwloop-demote-liveout-tripreg.ll.
//
//===----------------------------------------------------------------------===//

#include "HaydnHWLoopDemote.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
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

namespace {

class HaydnHWLoopDemoteTest : public testing::Test {
protected:
  std::unique_ptr<HaydnTargetMachine> TM;
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<MachineModuleInfo> MMI;
  std::unique_ptr<HaydnSubtarget> ST;
  std::unique_ptr<MachineFunction> MF;
  MachineBasicBlock *Preheader = nullptr;
  MachineBasicBlock *Header = nullptr;
  MachineBasicBlock *Latch = nullptr;
  MachineBasicBlock *Exit = nullptr;

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
    M = std::make_unique<Module>("HaydnHWLoopDemote", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);
    Preheader = MF->CreateMachineBasicBlock();
    Header = MF->CreateMachineBasicBlock();
    Latch = MF->CreateMachineBasicBlock();
    Exit = MF->CreateMachineBasicBlock();
    MF->push_back(Preheader);
    MF->push_back(Header);
    MF->push_back(Latch);
    MF->push_back(Exit);
    Preheader->addSuccessor(Header);
    Header->addSuccessor(Latch);
    Latch->addSuccessor(Header);
    Latch->addSuccessor(Exit);
  }

  const HaydnInstrInfo &TII() const { return *ST->getInstrInfo(); }

  haydn::hwloop::LoopBlockSet loopBlocks() const {
    haydn::hwloop::LoopBlockSet Blocks;
    haydn::hwloop::collectLoopBlocks(Header, Latch, Preheader, Blocks);
    return Blocks;
  }

  /// LD32 Rd, Rs, imm — a body compute def (the CB-165 shape: the SMS
  /// loop-carried value lands in the trip physreg via a load dest).
  MachineInstr &ld32(Register Rd, Register Rs, int64_t Imm) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::LD32),
                    Rd)
                .addReg(Rs)
                .addImm(Imm);
  }

  /// ADDI32 Rd, Rs, imm.
  MachineInstr &addi32(Register Rd, Register Rs, int64_t Imm) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(),
                    TII().get(Haydn::ADDI32), Rd)
                .addReg(Rs)
                .addImm(Imm);
  }

  /// SUBI32 Rd, Rs, imm.
  MachineInstr &subi32(Register Rd, Register Rs, int64_t Imm) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(),
                    TII().get(Haydn::SUBI32), Rd)
                .addReg(Rs)
                .addImm(Imm);
  }

  /// ADD32 Rd, Rs, Rt — same-reg register-rhs step: NOT a provable
  /// countdown.
  MachineInstr &add32(Register Rd, Register Rs, Register Rt) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::ADD32),
                    Rd)
                .addReg(Rs)
                .addReg(Rt);
  }

  /// MOVE32 Rd, Rs.
  MachineInstr &move32(Register Rd, Register Rs) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(),
                    TII().get(Haydn::MOVE32), Rd)
                .addReg(Rs);
  }
};

// collectLoopBlocks is the CFG-blocks authority: header, latch, and the
// interior, without the preheader or exit.
TEST_F(HaydnHWLoopDemoteTest, CollectLoopBlocksOwnsHeaderLatchNotExits) {
  auto Blocks = loopBlocks();
  EXPECT_TRUE(Blocks.contains(Header));
  EXPECT_TRUE(Blocks.contains(Latch));
  EXPECT_FALSE(Blocks.contains(Preheader));
  EXPECT_FALSE(Blocks.contains(Exit));
}

// CB-165 core law: a body LOAD dest in the trip physreg is a non-countdown
// redefinition. This is exactly the pr51581-2 shape — the pipelined body
// defines the loop-carried value in the same physreg the ZOL trip used, so
// the demote's exit value originates from the body, never from the trip.
TEST_F(HaydnHWLoopDemoteTest, BodyLoadDestIsNonCountdownRedefinition) {
  ld32(Haydn::R5, Haydn::R4, 0);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// A pure body MOVE into the trip physreg is equally a redefinition.
TEST_F(HaydnHWLoopDemoteTest, BodyMoveDestIsNonCountdownRedefinition) {
  move32(Haydn::R5, Haydn::R6);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// The CB-162 shape: the body never redefines Prefer (only a residual ±1
// countdown may touch it) — regClobberedNonCountdownIn must be false so
// the preheader trip save + exit restore stays sound.
TEST_F(HaydnHWLoopDemoteTest, PureCountdownBodyIsNotARedefinition) {
  addi32(Haydn::R5, Haydn::R5, -1);
  subi32(Haydn::R5, Haydn::R5, 1);
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// An untouched register (mentioned nowhere) is not redefined.
TEST_F(HaydnHWLoopDemoteTest, UntouchedRegIsNotARedefinition) {
  ld32(Haydn::R6, Haydn::R4, 0);
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// isCountdownStepOf: proven ±1 immediate steps on the SAME register are
// countdowns (either addi -1 or subi +1, logical-op mapped).
TEST_F(HaydnHWLoopDemoteTest, ProvenImmediateStepsAreCountdowns) {
  MachineInstr &Dec = addi32(Haydn::R5, Haydn::R5, -1);
  MachineInstr &Inc = subi32(Haydn::R5, Haydn::R5, 1);
  EXPECT_TRUE(haydn::hwloop::isCountdownStepOf(Dec, Haydn::R5));
  EXPECT_TRUE(haydn::hwloop::isCountdownStepOf(Inc, Haydn::R5));
}

// isCountdownStepOf: any other step width is compute, not a countdown.
TEST_F(HaydnHWLoopDemoteTest, NonUnitStepsAreNotCountdowns) {
  MachineInstr &Big = addi32(Haydn::R5, Haydn::R5, -4);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(Big, Haydn::R5));
}

// isCountdownStepOf: same-reg REGISTER-rhs ADD32/SUB32 is an unverifiable
// addend — post-RA may reuse the dead trip physreg as a pointer bump.
// Misclassifying it as a countdown lets stripResidualCountdown erase live
// compute.
TEST_F(HaydnHWLoopDemoteTest, RegisterRhsAddIsNotACountdown) {
  MachineInstr &PtrBump = add32(Haydn::R5, Haydn::R5, Haydn::R2);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(PtrBump, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// A def of a DIFFERENT register is not a countdown of Reg even when the
// shape matches (isCountdownStepOf must def Reg itself).
TEST_F(HaydnHWLoopDemoteTest, OtherRegDefIsNotCountdownOfReg) {
  MachineInstr &Other = addi32(Haydn::R6, Haydn::R6, -1);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(Other, Haydn::R5));
}

// regMentionedInBlocks sees uses too: a body READ of the trip reg keeps
// the register mentioned (counter candidates must be fully absent), while
// regClobberedNonCountdownIn stays false because a read cannot change the
// exit value.
TEST_F(HaydnHWLoopDemoteTest, BodyUseMentionsButDoesNotRedefine) {
  MachineInstr &Use = add32(Haydn::R6, Haydn::R5, Haydn::R2);
  (void)Use;
  auto Blocks = loopBlocks();
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R5, Blocks));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// The demote decision walks CFG blocks, not the layout range: a latch
// placed BEFORE the header in layout must still be inside the set (the
// lc_dp_lis law). Reorder layout: Latch, Header after Preheader.
TEST_F(HaydnHWLoopDemoteTest, BlocksFollowCFGNotLayout) {
  MF->remove(Latch);
  MF->insert(MF->begin(), Latch);
  ld32(Haydn::R5, Haydn::R4, 0);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(Blocks.contains(Latch));
  EXPECT_TRUE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// --- CB-165 value-preserve placement: the pure three-arm decision ---
//
// The gtest seam for the save/restore law the demote emits (the emit site
// in demoteHardwareLoopToSoftware only translates this decision into MIs).
// Arm inputs are the resolved facts (scratch identity, body redefinition),
// so every arm and the CB-162 legacy shape are pinned without constructing
// a full demote site.

// CB-165 miscompile shape: latch scratch is a DIFFERENT register, so
// nothing the demote installs touches the trip reg — any save/restore
// would reload the stale trip over the live body value (pr51581-2:
// c[N-1] = trip 4095). Dominates the body-redefinition fact.
TEST(HaydnHWLoopDemotePlacementTest, OtherLatchScratchIsNoSave) {
  using K = haydn::hwloop::HwLoopDemoteSaveKind;
  EXPECT_EQ(haydn::hwloop::demoteSavePlacement(
                /*PreferIsLatchScratch=*/false,
                /*PreferRedefinedInBody=*/true),
            K::NoSave);
  EXPECT_EQ(haydn::hwloop::demoteSavePlacement(
                /*PreferIsLatchScratch=*/false,
                /*PreferRedefinedInBody=*/false),
            K::NoSave);
}

// CB-162 legacy shape: scratch == Prefer and the body never redefines
// Prefer — Prefer carries the trip through, so the preheader trip save +
// exit restore stays sound.
TEST(HaydnHWLoopDemotePlacementTest,
     ScratchIsPreferWithoutBodyRedefIsPreheaderSave) {
  using K = haydn::hwloop::HwLoopDemoteSaveKind;
  EXPECT_EQ(haydn::hwloop::demoteSavePlacement(
                /*PreferIsLatchScratch=*/true,
                /*PreferRedefinedInBody=*/false),
            K::PreheaderSave);
}

// CB-165 body-redefined shape: scratch == Prefer and the body redefines
// Prefer (SMS loop-carried value in the trip physreg) — the exit value is
// the body's final def, so the save must execute at latch end, before the
// scratch window opens.
TEST(HaydnHWLoopDemotePlacementTest,
     ScratchIsPreferWithBodyRedefIsLatchEndSave) {
  using K = haydn::hwloop::HwLoopDemoteSaveKind;
  EXPECT_EQ(haydn::hwloop::demoteSavePlacement(
                /*PreferIsLatchScratch=*/true,
                /*PreferRedefinedInBody=*/true),
            K::LatchEndSave);
}

// Register-fact wrapper: same arms resolved from real body blocks.
// The pr51581-2 -O2 shape end-to-end: LD32 dest in the trip physreg plus
// a distinct latch scratch r6 -> NoSave; scratch == Prefer -> LatchEndSave.
TEST_F(HaydnHWLoopDemoteTest, PlacementWrapperResolvesFromBodyBlocks) {
  using K = haydn::hwloop::HwLoopDemoteSaveKind;
  ld32(Haydn::R5, Haydn::R4, 0); // body redefines the trip physreg
  auto Blocks = loopBlocks();
  EXPECT_EQ(haydn::hwloop::demoteSavePlacement(Haydn::R5, Haydn::R6, Blocks),
            K::NoSave); // other scratch dominates
  EXPECT_EQ(haydn::hwloop::demoteSavePlacement(Haydn::R5, Haydn::R5, Blocks),
            K::LatchEndSave); // scratch == Prefer + body redef
}

// Register-fact wrapper, CB-162 shape end-to-end: ±1-only body is not a
// redefinition, scratch == Prefer -> PreheaderSave.
TEST_F(HaydnHWLoopDemoteTest, PlacementWrapperPureCountdownIsPreheaderSave) {
  using K = haydn::hwloop::HwLoopDemoteSaveKind;
  addi32(Haydn::R5, Haydn::R5, -1);
  auto Blocks = loopBlocks();
  EXPECT_EQ(haydn::hwloop::demoteSavePlacement(Haydn::R5, Haydn::R5, Blocks),
            K::PreheaderSave);
}

// --- D1.19 stack-counter demote admission: the closed case matrix ---
//
// The gtest seam for the stack-counter arm's admission law (the gate in
// demoteHardwareLoopToSoftware only translates this decision into a
// refuse). Inputs are the resolved facts at the gate: latch scratch
// validity, imm-trip form, preheader scratch validity, nonzero LoopStart
// adjust, latch scratch identity. Exhaustive cross product so a future
// scratch fact that reopens a hole fails a named cell here first.

// Hand-derived expectation for the full matrix (see demoteStackCounter
// Admissible declaration for the lettered cells):
//  (a) !LatchScrValid                  -> refuse, everything else moot.
//  (f) HasImm                          -> PreheaderScrValid decides.
//  (b) !AdjNonZero (reg-trip, Adj==0)  -> admissible: remaining trip IS N,
//      Prefer stored directly.
//  (e) AdjNonZero, !PreheaderScrValid, LatchScrIsPrefer -> REFUSE (D1.19:
//      ADDI dest must differ from Prefer; storing Prefer stores full N).
//  (c)/(d) AdjNonZero otherwise        -> admissible (probed PreheaderScr
//      or the LatchScr!=Prefer copy fallback).
static bool expectStackCounterAdmissible(bool LatchScrValid, bool HasImm,
                                         bool PreheaderScrValid,
                                         bool AdjNonZero,
                                         bool LatchScrIsPrefer) {
  if (!LatchScrValid)
    return false;
  if (HasImm)
    return PreheaderScrValid;
  if (!AdjNonZero)
    return true;
  return PreheaderScrValid || !LatchScrIsPrefer;
}

TEST(HaydnHWLoopDemotePlacementTest, StackCounterDemoteAdmissibleFullMatrix) {
  unsigned Checked = 0;
  for (bool LatchScrValid : {false, true}) {
    for (bool HasImm : {false, true}) {
      for (bool PreheaderScrValid : {false, true}) {
        for (bool AdjNonZero : {false, true}) {
          for (bool LatchScrIsPrefer : {false, true}) {
            ASSERT_EQ(haydn::hwloop::demoteStackCounterAdmissible(
                          LatchScrValid, HasImm, PreheaderScrValid,
                          AdjNonZero, LatchScrIsPrefer),
                      expectStackCounterAdmissible(LatchScrValid, HasImm,
                                                   PreheaderScrValid,
                                                   AdjNonZero,
                                                   LatchScrIsPrefer))
                << "cell (LatchScrValid=" << LatchScrValid
                << ", HasImm=" << HasImm
                << ", PreheaderScrValid=" << PreheaderScrValid
                << ", AdjNonZero=" << AdjNonZero
                << ", LatchScrIsPrefer=" << LatchScrIsPrefer << ")";
            ++Checked;
          }
        }
      }
    }
  }
  EXPECT_EQ(Checked, 32u); // exhaustive 2^5 cross product, no hole
}

// The load-bearing named cells, pinned independently of the derived
// expectation so a rewrite of the reference above cannot hide them:

// Cell (e) — the D1.19 defect: Adj!=0, no PreheaderScr, LatchScr==Prefer.
// The only sound store of the remaining trip Prefer+Adj needs an ADDI
// dest != Prefer; with none, ST32 Prefer would store the FULL trip N.
TEST(HaydnHWLoopDemotePlacementTest,
     StackCounterAdjNoPreheaderLatchIsPreferRefuses) {
  EXPECT_FALSE(haydn::hwloop::demoteStackCounterAdmissible(
      /*LatchScrValid=*/true, /*HasImm=*/false,
      /*PreheaderScrValid=*/false, /*AdjNonZero=*/true,
      /*LatchScrIsPrefer=*/true));
}

// Cell (c) — same shape but a probed PreheaderScr exists: ADDI scratch,
// Prefer, -S; ST32 scratch (the cb166 stack arm). Admissible.
TEST(HaydnHWLoopDemotePlacementTest,
     StackCounterAdjWithPreheaderScratchAdmits) {
  EXPECT_TRUE(haydn::hwloop::demoteStackCounterAdmissible(
      /*LatchScrValid=*/true, /*HasImm=*/false,
      /*PreheaderScrValid=*/true, /*AdjNonZero=*/true,
      /*LatchScrIsPrefer=*/true));
}

// Cell (d) — copy fallback: no PreheaderScr but LatchScr != Prefer.
TEST(HaydnHWLoopDemotePlacementTest,
     StackCounterAdjCopyFallbackLatchNotPreferAdmits) {
  EXPECT_TRUE(haydn::hwloop::demoteStackCounterAdmissible(
      /*LatchScrValid=*/true, /*HasImm=*/false,
      /*PreheaderScrValid=*/false, /*AdjNonZero=*/true,
      /*LatchScrIsPrefer=*/false));
}

// Cell (b) — Adj==0 with LatchScr==Prefer: remaining trip IS the full
// trip; Prefer stored directly. The refuse must be scoped to Adj!=0.
TEST(HaydnHWLoopDemotePlacementTest,
     StackCounterAdjZeroLatchIsPreferAdmits) {
  EXPECT_TRUE(haydn::hwloop::demoteStackCounterAdmissible(
      /*LatchScrValid=*/true, /*HasImm=*/false,
      /*PreheaderScrValid=*/false, /*AdjNonZero=*/false,
      /*LatchScrIsPrefer=*/true));
}

// Cell (a) — no latch scratch at all: refuse regardless of everything else.
TEST(HaydnHWLoopDemotePlacementTest, StackCounterNoLatchScratchRefuses) {
  for (bool HasImm : {false, true})
    for (bool PreheaderScrValid : {false, true})
      for (bool AdjNonZero : {false, true})
        for (bool LatchScrIsPrefer : {false, true})
          EXPECT_FALSE(haydn::hwloop::demoteStackCounterAdmissible(
              /*LatchScrValid=*/false, HasImm, PreheaderScrValid, AdjNonZero,
              LatchScrIsPrefer));
}

// Cell (f) — imm trip: admission is exactly PreheaderScrValid (the imm
// materialize window); Adj cannot be nonzero on this form.
TEST(HaydnHWLoopDemotePlacementTest, StackCounterImmTripNeedsPreheaderScratch) {
  EXPECT_TRUE(haydn::hwloop::demoteStackCounterAdmissible(
      /*LatchScrValid=*/true, /*HasImm=*/true,
      /*PreheaderScrValid=*/true, /*AdjNonZero=*/false,
      /*LatchScrIsPrefer=*/false));
  EXPECT_FALSE(haydn::hwloop::demoteStackCounterAdmissible(
      /*LatchScrValid=*/true, /*HasImm=*/true,
      /*PreheaderScrValid=*/false, /*AdjNonZero=*/false,
      /*LatchScrIsPrefer=*/false));
}

} // namespace
