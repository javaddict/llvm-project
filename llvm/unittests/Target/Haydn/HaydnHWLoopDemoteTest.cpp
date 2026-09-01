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
#include "HaydnHardwareLoops.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/MIRPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/raw_ostream.h"
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

// --- D1.34: one byte-walk authority — joint-grid pad law ---

// The pad must land on the JOINT parcel/alignment grid: lcm(Align,
// Parcel=12). The old parcel-only rounding of the raw alignTo gap
// returned 24 for (12, align 16) — neither 16-aligned nor on the 48-byte
// joint grid; the first point satisfying both is lcm(16, 12) = 48.
TEST_F(HaydnHWLoopDemoteTest, PadSolvesJointParcelGrid) {
  // Function alignment >= every MBB alignment below: the BR ParentAlign
  // uncertainty term stays out of this pin (isolates the joint grid law).
  MF->setAlignment(Align(32));
  Latch->setAlignment(Align(16));
  EXPECT_EQ(haydn::hwloop::padLayoutBytesForMBBAlign(12, *Latch), 48);
  Latch->setAlignment(Align(8));
  EXPECT_EQ(haydn::hwloop::padLayoutBytesForMBBAlign(12, *Latch), 24);
  // align 4 divides the parcel size: no pad, the start already satisfies
  // both the alignment and the grid.
  Latch->setAlignment(Align(4));
  EXPECT_EQ(haydn::hwloop::padLayoutBytesForMBBAlign(12, *Latch), 12);
  // Unequal grid (lcm(32,12) = 96).
  Latch->setAlignment(Align(32));
  EXPECT_EQ(haydn::hwloop::padLayoutBytesForMBBAlign(12, *Latch), 96);
  // Alignment 1: the early-out, zero pad (the entire gr27/D1.33 corpus).
  Latch->setAlignment(Align(1));
  EXPECT_EQ(haydn::hwloop::padLayoutBytesForMBBAlign(12, *Latch), 12);
  // Negative input: early-out verbatim (sentinel propagation).
  Latch->setAlignment(Align(16));
  EXPECT_EQ(haydn::hwloop::padLayoutBytesForMBBAlign(-1, *Latch), -1);
}

// The load-bearing BR-uncertainty pin: when the MBB alignment exceeds
// the FUNCTION alignment (ParentAlign), generic BranchRelaxation's
// postOffset model charges alignTo(96,32) + (32-1) = 127 bytes worst
// case. The joint grid alone would stop at 96 — a span this seat
// measures near and BR's post-stamp re-scan measures far. The pad must
// cover 127 rounded up to whole parcels: 132.
TEST_F(HaydnHWLoopDemoteTest, PadCoversBranchRelaxationParentAlignUncertainty) {
  // Explicit function alignment: the default is target/state dependent,
  // and the (A > PA) uncertainty term must be live for this pin.
  MF->setAlignment(Align(1));
  Latch->setAlignment(Align(32));
  EXPECT_EQ(haydn::hwloop::padLayoutBytesForMBBAlign(96, *Latch), 132);
  // Same alignment with the function alignment already >= it: no
  // uncertainty term, the joint grid alone governs (96 is already on
  // the lcm(32,12) grid).
  MF->setAlignment(Align(32));
  EXPECT_EQ(haydn::hwloop::padLayoutBytesForMBBAlign(96, *Latch), 96);
}

// --- D1.34: sentinel law — a -1 span is a direction refusal, not a size ---

// Latch precedes Header in layout: the span walk must return the -1
// sentinel (fixture order Header-then-Latch, so From=Latch / To=Header
// exercises the rotated direction with zero rearrangement). The consumer
// law (HaydnHardwareLoops demote) then resolves the OTHER direction on
// the same walk — the Latch-end..Header-begin FORWARD distance — and
// fails closed only when NEITHER resolves. Pin that both halves hold:
// the span sentinel, and the resolvable forward distance the demote
// consumes for a rotated (MBP-rotated, nsichneu-class) loop.
TEST_F(HaydnHWLoopDemoteTest, SpanAndDistanceSentinelWhenToPrecedesFrom) {
  EXPECT_EQ(haydn::hwloop::estimateLayoutSpanBytes(*MF, Latch, Header,
                                                   TII()),
            -1);
  EXPECT_EQ(haydn::hwloop::estimateLayoutMBBDistance(*MF, Latch,
                                                     Latch->begin(), Header,
                                                     TII()),
            -1);
}

// The rotated-direction resolution the demote performs on the sentinel:
// lay the fixture out Latch-before-Header (splice the Latch to the
// front), then the span walk (Header-begin..Latch-end) returns -1 and
// the SAME walk in the other direction — Latch-end..Header-begin, the
// BNEZ site displacement — returns a real FORWARD distance. One parcel
// in the Header, none in the Latch: forward distance 12. This is the
// exact pair the HaydnHardwareLoops demote consumes for an
// MBP-rotated (nsichneu-class) loop.
TEST_F(HaydnHWLoopDemoteTest, RotatedSpanResolvesForwardDirection) {
  MF->setAlignment(Align(1));
  // One parcel in the block between Latch and Header so the forward
  // distance is non-zero (12 B).
  MachineInstr &P = *BuildMI(*Preheader, Preheader->end(), DebugLoc(),
                             TII().get(Haydn::ADDI32), Haydn::R5)
                         .addReg(Haydn::R5)
                         .addImm(0);
  (void)P;
  // Latch-before-Header layout (MBP rotation shape).
  MF->splice(MF->begin(), Latch->getIterator());
  EXPECT_EQ(haydn::hwloop::estimateLayoutSpanBytes(*MF, Header, Latch,
                                                   TII()),
            -1);
  EXPECT_EQ(haydn::hwloop::estimateLayoutMBBDistance(*MF, Latch,
                                                     Latch->end(), Header,
                                                     TII()),
            12);
}

// Positive arm: the span authority charges Header-begin..Latch-end with
// the entering-MBB pad, from the one shared walk. One parcel per block,
// alignment 1: span 24; over-aligned Latch pads the entering offset to
// the joint grid first (12 -> 48) then adds the Latch parcel: 60.
TEST_F(HaydnHWLoopDemoteTest, SpanChargesEnteringMBBPadFromSharedWalk) {
  MF->setAlignment(Align(1));
  MachineInstr &H = *BuildMI(*Header, Header->end(), DebugLoc(),
                             TII().get(Haydn::ADDI32), Haydn::R5)
                         .addReg(Haydn::R5)
                         .addImm(0);
  MachineInstr &L = subi32(Haydn::R5, Haydn::R5, 1);
  (void)H;
  (void)L;
  EXPECT_EQ(haydn::hwloop::estimateLayoutSpanBytes(*MF, Header, Latch,
                                                   TII()),
            24);
  Latch->setAlignment(Align(16));
  EXPECT_EQ(haydn::hwloop::estimateLayoutSpanBytes(*MF, Header, Latch,
                                                   TII()),
            60);
  // Distance arm: from Header begin (exclusive) to Latch start is the
  // Header parcel alone (12), alignment 1.
  Latch->setAlignment(Align(1));
  EXPECT_EQ(haydn::hwloop::estimateLayoutMBBDistance(*MF, Header,
                                                     Header->begin(), Latch,
                                                     TII()),
            12);
}

//===----------------------------------------------------------------------===//
// D1.51: demoteHardwareLoopToSoftware refusal-atomicity.
//
// Law: every refusal (exit/range/scratch/FI/save-home/Exit==Header)
// returns BEFORE the first MIR or CFG mutation. The helper once emitted
// trip state (materializeTripCount / emitExactLateDef), stripped
// countdowns, erased SET/PLE, swept latch terminators, and rewrote latch
// successors BEFORE the Exit==Header / unknown-span / no-long-latch-
// scratch refusals returned false — a half-demoted function behind a
// returned-false (pipeline repair-theorem breach; unsafe for any
// retry/nonfatal caller).
//
// Pin: snapshot-and-compare MIR. printMIR before and after a refused
// demote must be BYTE-IDENTICAL (instructions, successors, liveins).
//===----------------------------------------------------------------------===//

class HaydnDemoteAtomicityTest : public testing::Test {
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
    M = std::make_unique<Module>("HaydnDemoteAtomicity", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);
    // The demote reads HaydnMachineFunctionInfo (scratch FI homes); a real
    // pipeline MF always has it attached via this hook.
    MF->initTargetMachineFunctionInfo(*ST);
    MF->getRegInfo().freezeReservedRegs();
    MF->setAlignment(Align(1));
    // blockLiveInContains walks stored liveins (liveout_iterator asserts
    // the TracksLiveness property); a hand-built MF must opt in.
    MF->getProperties().set(
        MachineFunctionProperties::Property::TracksLiveness);

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

  /// SET_HWLOOP_W sel, header, latch, imm-trip — the fixup-seat form
  /// (same operand shape as hwloop-fixup-demote-range.mir).
  MachineInstr &setHwloopImm(MachineBasicBlock &MBB, MachineBasicBlock *H,
                             MachineBasicBlock *L, uint64_t Trip) {
    return *BuildMI(MBB, MBB.end(), DebugLoc(),
                    TII().get(Haydn::SET_HWLOOP_W))
                .addImm(0)
                .addMBB(H)
                .addMBB(L)
                .addImm(Trip)
                .addReg(Haydn::SFR, RegState::ImplicitDefine);
  }

  /// ADDI32 Rd, Rs, imm in the given block.
  void addi32In(MachineBasicBlock &MBB, Register R, int64_t Imm) {
    BuildMI(MBB, MBB.end(), DebugLoc(), TII().get(Haydn::ADDI32), R)
        .addReg(R)
        .addImm(Imm);
  }

  std::string mirSnapshot() {
    std::string Str;
    raw_string_ostream OS(Str);
    printMIR(OS, *MMI, *MF);
    return OS.str();
  }
};

// Refusal 1 — no live exit: the latch's only CFG successor is the header
// and no layout block follows the latch. The refusal must return before
// any mutation; snapshot identity is the pin.
TEST_F(HaydnDemoteAtomicityTest, NoLiveExitRefusalLeavesFunctionIdentical) {
  // Self-latch region with no exit: latch -> header only, and the exit
  // block is moved to the FRONT of the layout so it is neither a latch
  // successor nor the latch's layout fallthrough.
  Latch->removeSuccessor(Exit);
  MF->splice(MF->begin(), Exit->getIterator());
  MachineInstr &SET = setHwloopImm(*Preheader, Header, Latch, 8);
  (void)SET;
  const std::string Before = mirSnapshot();
  EXPECT_FALSE(demoteHardwareLoopToSoftware(SET, TII(), "D1.51-test"));
  EXPECT_EQ(mirSnapshot(), Before) << "refused demote mutated the function";
}

// Refusal 2 — no free counter GPR (the hwloop-demote-fatal-live class):
// every pickCounterReg candidate is mentioned in the loop body and no
// scratch FI exists (both stay -1 in this fixture). The refusal must
// return before any mutation — in particular the deferred emission arms
// must not have emitted the trip materialize first.
TEST_F(HaydnDemoteAtomicityTest, NoFreeCounterRefusalLeavesFunctionIdentical) {
  MachineInstr &SET = setHwloopImm(*Preheader, Header, Latch, 8);
  (void)SET;
  const MCPhysReg All[] = {Haydn::R11, Haydn::R10, Haydn::R9,  Haydn::R8,
                           Haydn::R7,  Haydn::R4,  Haydn::R3,  Haydn::R2,
                           Haydn::R1,  Haydn::R12};
  for (MCPhysReg R : All)
    addi32In(*Latch, R, 1);
  const std::string Before = mirSnapshot();
  EXPECT_FALSE(demoteHardwareLoopToSoftware(SET, TII(), "D1.51-test"));
  EXPECT_EQ(mirSnapshot(), Before) << "refused demote mutated the function";
}

// Refusal 3 — the D1.51-HOISTED long-latch scratch law. This is the exact
// decision that used to sit AFTER stripResidualCountdown /
// eraseHardwareLoopSetup / the latch terminator sweep / the successor
// rewrite: the old code refused over a half-demoted function. Here the
// free-counter arm succeeds (R1 is mentioned nowhere -> CountReg = R1,
// InstallSoftLoop = true), the body is long enough to overflow the
// simm12 window (LongLatch), and every OTHER LongCands register is
// live-in of the latch's post-rewrite out-edges (used in the latch body,
// so live-through the header), leaving no computed-dead JALR scratch.
TEST_F(HaydnDemoteAtomicityTest,
       LongLatchNoScratchRefusalLeavesFunctionIdentical) {
  MachineInstr &SET = setHwloopImm(*Preheader, Header, Latch, 8);
  (void)SET;
  // Long span: LOADI64 charges 17 product parcels (getInstSizeInBytes
  // bound) = 204B each; 12 of them = 2448B, past the WIDE_BranchSImm12
  // window (2048) even before the safety-buffer inflation -> LongLatch.
  for (unsigned I = 0; I < 12; ++I) {
    BuildMI(*Latch, Latch->begin(), DebugLoc(), TII().get(Haydn::LOADI64),
            Haydn::D0)
        .addImm(0x1234);
  }
  // Every LongCands register except R1 (the free CountReg) is read in the
  // latch body and live-in of the latch (stored liveins feed the one-block
  // computeBlockLiveIns walk's addLiveOuts), so each is live-in of the
  // post-rewrite out-edge Header too; R1 is excluded by the CountdownReg
  // identity law. No computed-dead scratch remains.
  const MCPhysReg Used[] = {Haydn::R11, Haydn::R10, Haydn::R9,  Haydn::R8,
                            Haydn::R7,  Haydn::R4,  Haydn::R3,  Haydn::R2,
                            Haydn::R12};
  for (MCPhysReg R : Used) {
    addi32In(*Latch, R, 1);
    Latch->addLiveIn(R);
  }
  const std::string Before = mirSnapshot();
  EXPECT_FALSE(demoteHardwareLoopToSoftware(SET, TII(), "D1.51-test"));
  EXPECT_EQ(mirSnapshot(), Before)
      << "hoisted long-latch scratch refusal mutated the function";
}

// Header==Latch with an extra pre-rewrite successor that occupies every
// LongCands GPR: the post-rewrite obligation is {Header, Exit}, so the
// extra edge must not refuse a dead-on-{Header,Exit} LongScr (nsichneu
// Role-A early-exit compile-fatal class).
TEST_F(HaydnDemoteAtomicityTest,
       LongLatchSelfLoopExtraSuccessorStillFindsScratch) {
  Header->removeSuccessor(Latch);
  Latch->removeSuccessor(Header);
  Latch->removeSuccessor(Exit);
  Header->addSuccessor(Header);
  Header->addSuccessor(Exit);
  MachineBasicBlock *Extra = MF->CreateMachineBasicBlock();
  MF->push_back(Extra);
  Header->addSuccessor(Extra);

  MachineInstr &SET = setHwloopImm(*Preheader, Header, Header, 8);
  for (unsigned I = 0; I < 12; ++I) {
    BuildMI(*Header, Header->begin(), DebugLoc(), TII().get(Haydn::LOADI64),
            Haydn::D0)
        .addImm(0x1234);
  }
  const MCPhysReg ExtraUsed[] = {
      Haydn::R11, Haydn::R10, Haydn::R9, Haydn::R8, Haydn::R7,
      Haydn::R4,  Haydn::R3,  Haydn::R2, Haydn::R12};
  for (MCPhysReg R : ExtraUsed) {
    addi32In(*Extra, R, 1);
    Extra->addLiveIn(R);
  }
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::PseudoLoopEnd))
      .addMBB(Header);
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::B)).addMBB(Exit);

  EXPECT_TRUE(demoteHardwareLoopToSoftware(SET, TII(), "D1.51-test"))
      << "extra pre-rewrite successor occupied LongScr under the retired "
         "all-current-successor walk";
}

} // namespace
