//===- HaydnPostCommitBlockBudgetTest.cpp - D1.40 CFG identity wall ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit seal for the GR2.7/D1.40 postcommit CFG identity wall pair on
// HaydnMachineFunctionInfo (stampPostCommitCfgSnapshot +
// postCommitCfgCreationViolation, the sole wall predicate consumed by
// the independent Verify seats):
//   * no stamp -> no violation (probes / MIR fixtures that never run
//     Finalize stay legal — GR2.7 I4 seat-bound law)
//   * stamp + identity-unchanged CFG -> empty violation
//   * L1: stamp + block creation -> violation names the law and the owning
//     forms (BranchRelaxation trampoline/RestoreBB/split postcommit)
//   * L2: stamp + block erasure (shrink) -> refused
//   * L3: stamp + shrink-regrow that recycles the SAME identity (count and
//     tokens match the stamp) -> refused via the numbering-slot trace
//   * L4: stamp + equal-count MBB replacement with a DIFFERENT identity
//     (erase + re-add; split-and-merge shape) -> refused, names the first
//     diverging layout position
//   * L3: stamp + create-then-delete (slack grew at an unchanged epoch)
//     -> refused
//   * renumber after the stamp with no mutation stays legal (pins the
//     epoch guard that tolerates BranchRelaxation's entry renumber)
//   * L5: stamp + successor rewrite with unchanged block identity/token
//     sequence (edge-only mutation) -> refused, names the offending edge
//     source layout position. No admitted-transition exception (GR2.10).
//   * L6: stamp + RenumberBlocks + create-then-delete (L3 retired by
//     the epoch bump; live count and IR-bound tokens match) -> refused
//     via the CreationID high-water
//   * L4: equal-count null-BB/no-BBID replacement after RenumberBlocks
//     -> refused via identity tokens (CreationID folded in; no shared
//     NoBBIDSentinel)
//   * stamp-once: a second stamp keeps the first snapshot (monotone
//     ratchet — later Finalize seats never re-stamp)
//   * clone(): the destination MFI carries no stamp and no inherited
//     identity tokens or L5 admission; the source snapshot keeps firing
//
//===----------------------------------------------------------------------===//

#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"
#include "gtest/gtest.h"
#include <iterator>
#include <memory>

using namespace llvm;

namespace {

class HaydnPostCommitBlockBudgetTest : public testing::Test {
protected:
  std::unique_ptr<HaydnTargetMachine> TM;
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<MachineModuleInfo> MMI;
  std::unique_ptr<HaydnSubtarget> ST;
  std::unique_ptr<MachineFunction> MF;
  std::unique_ptr<HaydnMachineFunctionInfo> HFInfo;
  Function *F = nullptr;
  BasicBlock *EntryIR = nullptr;
  BasicBlock *ExitIR = nullptr;

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
    M = std::make_unique<Module>("HaydnPostCommitBlockBudget", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);
    // Two IR blocks so the two MBBs below carry DISTINCT identity tokens
    // (getBasicBlock()); null-BB MBBs share one token and the equal-count
    // replacement law (L4) could not distinguish them. Each IR block gets
    // a terminator BEFORE any MBB binds it: MachineBasicBlock(MF, BB)
    // dereferences the terminator (getIrrLoopHeaderWeight walks the
    // predecessor edge), and a terminator-less BB is malformed IR.
    EntryIR = BasicBlock::Create(*Ctx, "entry", F);
    ExitIR = BasicBlock::Create(*Ctx, "exit", F);
    BranchInst::Create(ExitIR, EntryIR);
    ReturnInst::Create(*Ctx, ExitIR);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);
    HFInfo = std::make_unique<HaydnMachineFunctionInfo>(*F, ST.get());

    // Two initial IR-bound blocks so every law is observable.
    MF->push_back(MF->CreateMachineBasicBlock(EntryIR));
    MF->push_back(MF->CreateMachineBasicBlock(ExitIR));
  }

  HaydnMachineFunctionInfo &Info() { return *HFInfo; }
  MachineFunction &Func() { return *MF; }

  /// Drop the layout-tail MBB (erase + null its numbering slot).
  void eraseTail() { MF->erase(std::prev(MF->end())); }
};

TEST_F(HaydnPostCommitBlockBudgetTest, NoStampPasses) {
  EXPECT_FALSE(Info().hasPostCommitBlockBudget());
  EXPECT_TRUE(Info().postCommitCfgCreationViolation(Func()).empty());
}

TEST_F(HaydnPostCommitBlockBudgetTest, StampedUnchangedPasses) {
  Info().stampPostCommitCfgSnapshot(Func());
  EXPECT_TRUE(Info().hasPostCommitBlockBudget());
  EXPECT_TRUE(Info().postCommitCfgCreationViolation(Func()).empty());
}

TEST_F(HaydnPostCommitBlockBudgetTest, PostCommitCFGCreationRefused) {
  Info().stampPostCommitCfgSnapshot(Func());
  MachineBasicBlock *NewBB = MF->CreateMachineBasicBlock();
  MF->push_back(NewBB); // BranchRelaxation trampoline/RestoreBB/split shape
  const std::string V = Info().postCommitCfgCreationViolation(Func());
  EXPECT_NE(V.find("postcommit CFG creation"), std::string::npos) << V;
}

TEST_F(HaydnPostCommitBlockBudgetTest, StampIsWriteOnce) {
  Info().stampPostCommitCfgSnapshot(Func());
  MachineBasicBlock *NewBB = MF->CreateMachineBasicBlock();
  MF->push_back(NewBB);
  // A later Finalize seat cannot legitimize postcommit growth.
  Info().stampPostCommitCfgSnapshot(Func());
  EXPECT_FALSE(Info().postCommitCfgCreationViolation(Func()).empty());
}

// L2: postcommit MBB erasure is a block reassignment (constraint 11) and
// is refused exactly like creation. The erase also nulls a numbering slot,
// but the count law names the shape first.
TEST_F(HaydnPostCommitBlockBudgetTest, ShrinkRefused) {
  Info().stampPostCommitCfgSnapshot(Func());
  eraseTail();
  const std::string V = Info().postCommitCfgCreationViolation(Func());
  EXPECT_NE(V.find("postcommit CFG shrink"), std::string::npos) << V;
}

// L3 via regrow: erase the tail and re-add a block carrying the SAME IR
// identity. Count and token sequence match the stamp exactly, so only the
// numbering-slot trace (the erased slot stays null while the re-added MBB
// appends a fresh block-ID) exposes the mutation.
TEST_F(HaydnPostCommitBlockBudgetTest, ShrinkRegrowRefused) {
  Info().stampPostCommitCfgSnapshot(Func());
  eraseTail();
  MF->push_back(MF->CreateMachineBasicBlock(ExitIR));
  ASSERT_EQ(Func().size(), 2u);
  const std::string V = Info().postCommitCfgCreationViolation(Func());
  EXPECT_NE(V.find("postcommit CFG numbering"), std::string::npos) << V;
  EXPECT_NE(V.find("numbering trace"), std::string::npos) << V;
}

// D1.46 arm: shrink-then-OVERSHOOT (erase one, re-add two). The live count
// ends strictly above the stamp, so the growth law (L1) names the shape
// even though an erasure also happened — the laws run count-first and the
// net is what the stamp compares. A cardinality ratchet keyed only on
// "shrink" would have missed this composite.
TEST_F(HaydnPostCommitBlockBudgetTest, ShrinkRegrowOvershootRefused) {
  Info().stampPostCommitCfgSnapshot(Func());
  eraseTail();
  MF->push_back(MF->CreateMachineBasicBlock(ExitIR));
  MF->push_back(MF->CreateMachineBasicBlock(EntryIR));
  ASSERT_EQ(Func().size(), 3u);
  const std::string V = Info().postCommitCfgCreationViolation(Func());
  EXPECT_NE(V.find("postcommit CFG creation"), std::string::npos) << V;
}

// D1.46 arm: stamp-vs-live boundary semantics. A CFG that shrinks and
// regrows back to EXACTLY the stamped count with the SAME ordered tokens
// but at a different numbering epoch (no RenumberBlocks ran, so the epoch
// guard does not retire the slack law) is still refused by the numbering
// trace — MF.size() equality alone is never the pass condition.
TEST_F(HaydnPostCommitBlockBudgetTest, EqualCountRegrowStillNumberedRefused) {
  Info().stampPostCommitCfgSnapshot(Func());
  const unsigned EpochAtStamp = Func().getBlockNumberEpoch();
  eraseTail();
  MF->push_back(MF->CreateMachineBasicBlock(ExitIR));
  ASSERT_EQ(Func().size(), 2u);
  ASSERT_EQ(Func().getBlockNumberEpoch(), EpochAtStamp)
      << "no renumber ran; the slack law must stay armed";
  const std::string V = Info().postCommitCfgCreationViolation(Func());
  EXPECT_FALSE(V.empty()) << "equal count + equal tokens + equal epoch must "
                             "still fire on the numbering trace";
}

// L4 at equal count: erase the tail and re-add a DIFFERENTLY-bound block.
// This is the erase+add-replacement / split-and-merge shape the
// cardinality-only budget could not see; the violation names the first
// diverging layout position.
TEST_F(HaydnPostCommitBlockBudgetTest, EqualCountReplacementRefused) {
  Info().stampPostCommitCfgSnapshot(Func());
  eraseTail();
  BasicBlock *FreshIR = BasicBlock::Create(*Ctx, "equalcount", F);
  // Terminator before the MBB binds it (same SetUp law: the MBB ctor walks
  // the predecessor edge of the IR terminator).
  ReturnInst::Create(*Ctx, FreshIR);
  MF->push_back(MF->CreateMachineBasicBlock(FreshIR));
  ASSERT_EQ(Func().size(), 2u);
  const std::string V = Info().postCommitCfgCreationViolation(Func());
  EXPECT_NE(V.find("postcommit CFG identity"), std::string::npos) << V;
  EXPECT_NE(V.find("position 1"), std::string::npos) << V;
}

// L3 via create-then-delete: the block-ID table keeps the dead slot at an
// unchanged numbering epoch even though MF.size() and the token sequence
// match the stamp.
TEST_F(HaydnPostCommitBlockBudgetTest, CreateThenDeleteRefused) {
  Info().stampPostCommitCfgSnapshot(Func());
  MF->push_back(MF->CreateMachineBasicBlock());
  eraseTail();
  ASSERT_EQ(Func().size(), 2u);
  ASSERT_GT(Func().getNumBlockIDs(), 2u) << "erased slot must stay numbered";
  const std::string V = Info().postCommitCfgCreationViolation(Func());
  EXPECT_NE(V.find("postcommit CFG numbering"), std::string::npos) << V;
  EXPECT_NE(V.find("numbering trace"), std::string::npos) << V;
}

// RenumberBlocks (BranchRelaxation calls it unconditionally at entry) is
// legal after the stamp: it changes MBB numbers, never identity tokens,
// and the epoch guard retires the slack law rather than firing it.
TEST_F(HaydnPostCommitBlockBudgetTest, RenumberAfterStampStaysLegal) {
  Info().stampPostCommitCfgSnapshot(Func());
  const unsigned EpochBefore = Func().getBlockNumberEpoch();
  Func().RenumberBlocks();
  EXPECT_NE(Func().getBlockNumberEpoch(), EpochBefore);
  EXPECT_TRUE(Info().postCommitCfgCreationViolation(Func()).empty());
}

// clone()/reset: the destination MFI carries no stamp and no inherited
// identity tokens (it stamps its own at its own first Finalize); the
// source snapshot keeps firing on source mutation. GR2.10: there is no
// L5 admission API to inherit — dest edge rewrite is a hard freeze.
TEST_F(HaydnPostCommitBlockBudgetTest, CloneResetsSnapshot) {
  MachineBasicBlock *Entry = &*Func().begin();
  MachineBasicBlock *Exit = &*std::next(Func().begin());
  Entry->addSuccessor(Exit);
  Info().stampPostCommitCfgSnapshot(Func());
  ASSERT_TRUE(Info().hasPostCommitBlockBudget());

  // The clone signature takes an allocator, but the copy is allocated from
  // the DESTINATION MF's own allocator (cloneInfo), so Dest's destructor
  // owns the copied MFI — no manual teardown here.
  BumpPtrAllocator SignatureAlloc;
  MachineFunction Dest(*F, *TM, *ST, MMI->getContext(),
                       /*FunctionNum=*/1);
  ASSERT_EQ(Dest.getInfo<HaydnMachineFunctionInfo>(), nullptr);
  Dest.push_back(Dest.CreateMachineBasicBlock(EntryIR));
  DenseMap<MachineBasicBlock *, MachineBasicBlock *> Src2DstMBB;
  MachineFunctionInfo *Copy =
      Info().clone(SignatureAlloc, Dest, Src2DstMBB);
  ASSERT_NE(Copy, nullptr);
  auto *DestInfo = static_cast<HaydnMachineFunctionInfo *>(Copy);
  EXPECT_EQ(Dest.getInfo<HaydnMachineFunctionInfo>(), DestInfo);
  EXPECT_FALSE(DestInfo->hasPostCommitBlockBudget());
  EXPECT_TRUE(DestInfo->postCommitCfgCreationViolation(Dest).empty());

  // The source snapshot is untouched by the clone.
  MF->push_back(MF->CreateMachineBasicBlock());
  EXPECT_FALSE(Info().postCommitCfgCreationViolation(Func()).empty());

  // The destination stamps its own snapshot at its own first Finalize and
  // observes no violation on its own (different) CFG.
  DestInfo->stampPostCommitCfgSnapshot(Dest);
  EXPECT_TRUE(DestInfo->hasPostCommitBlockBudget());
  EXPECT_TRUE(DestInfo->postCommitCfgCreationViolation(Dest).empty());

  // No inherited admission: dest edge rewrite fires (L5 is a no-exception
  // wall; clone cannot launder a source edge permit that no longer exists).
  MachineBasicBlock *DestEntry = &*Dest.begin();
  DestEntry->addSuccessor(DestEntry);
  EXPECT_FALSE(DestInfo->postCommitCfgCreationViolation(Dest).empty())
      << "clone must not inherit a source L5 admission";
}

// L5 (D1.40 Phase 2 / GR2.10): successor digest law. The fixture wires a
// real CFG (entry -> exit) before stamping; an edge-only rewrite —
// removeSuccessor + addSuccessor on the SAME block, block identity/token
// sequence unchanged — is refused and the violation names the offending
// edge SOURCE layout position. No admitted-transition exception.
TEST_F(HaydnPostCommitBlockBudgetTest, EdgeOnlyRewriteRefused) {
  MachineBasicBlock *Entry = &*Func().begin();
  MachineBasicBlock *Exit = &*std::next(Func().begin());
  Entry->addSuccessor(Exit);
  Info().stampPostCommitCfgSnapshot(Func());
  // Edge-only mutation: retarget the entry edge to itself. No block is
  // created/erased/replaced; L1-L4 all pass.
  Entry->removeSuccessor(Exit);
  Entry->addSuccessor(Entry);
  const std::string V = Info().postCommitCfgCreationViolation(Func());
  EXPECT_NE(V.find("postcommit CFG edges"), std::string::npos) << V;
  EXPECT_NE(V.find("position 0"), std::string::npos) << V;
}

// L6: BR-entry RenumberBlocks is legal (epoch guard retires L3). A
// subsequent create-then-delete restores live count and the IR-bound
// token sequence, so L1/L2/L4/L3 are silent; the CreationID high-water
// is the un-launderable twin of L3.
TEST_F(HaydnPostCommitBlockBudgetTest,
       RenumberThenCreateThenDeleteRefusedByL6) {
  Info().stampPostCommitCfgSnapshot(Func());
  const unsigned EpochAtStamp = Func().getBlockNumberEpoch();
  const unsigned HighWaterAtStamp = Func().getMBBCreationHighWater();
  Func().RenumberBlocks();
  EXPECT_NE(Func().getBlockNumberEpoch(), EpochAtStamp);
  ASSERT_TRUE(Info().postCommitCfgCreationViolation(Func()).empty())
      << "RenumberBlocks after the stamp must stay legal";
  MF->push_back(MF->CreateMachineBasicBlock());
  eraseTail();
  ASSERT_EQ(Func().size(), 2u);
  ASSERT_GT(Func().getMBBCreationHighWater(), HighWaterAtStamp);
  const std::string V = Info().postCommitCfgCreationViolation(Func());
  EXPECT_NE(V.find("postcommit CFG high-water"), std::string::npos) << V;
  EXPECT_EQ(V.find("postcommit CFG numbering"), std::string::npos) << V;
  EXPECT_EQ(V.find("postcommit CFG identity"), std::string::npos) << V;
  EXPECT_EQ(V.find("postcommit CFG creation:"), std::string::npos) << V;
  EXPECT_EQ(V.find("postcommit CFG shrink"), std::string::npos) << V;
  EXPECT_EQ(V.find("postcommit CFG edges"), std::string::npos) << V;
}

// L4: equal-count replacement among null-BB/no-BBID blocks after
// RenumberBlocks. L3 is retired; CreationID in the existing token half
// distinguishes the replacement that used to share NoBBIDSentinel.
TEST_F(HaydnPostCommitBlockBudgetTest,
       EqualCountNullBBReplacementAfterRenumberRefusedByL4) {
  MachineBasicBlock *NullBB = MF->CreateMachineBasicBlock();
  ASSERT_EQ(NullBB->getBasicBlock(), nullptr);
  ASSERT_FALSE(NullBB->getBBID().has_value());
  MF->push_back(NullBB);
  Info().stampPostCommitCfgSnapshot(Func());
  const unsigned EpochAtStamp = Func().getBlockNumberEpoch();
  Func().RenumberBlocks();
  EXPECT_NE(Func().getBlockNumberEpoch(), EpochAtStamp);
  ASSERT_TRUE(Info().postCommitCfgCreationViolation(Func()).empty());
  eraseTail();
  MachineBasicBlock *Replacement = MF->CreateMachineBasicBlock();
  ASSERT_EQ(Replacement->getBasicBlock(), nullptr);
  ASSERT_FALSE(Replacement->getBBID().has_value());
  MF->push_back(Replacement);
  ASSERT_EQ(Func().size(), 3u);
  const std::string V = Info().postCommitCfgCreationViolation(Func());
  EXPECT_NE(V.find("postcommit CFG identity"), std::string::npos) << V;
  EXPECT_NE(V.find("position 2"), std::string::npos) << V;
  EXPECT_EQ(V.find("postcommit CFG numbering"), std::string::npos) << V;
  EXPECT_EQ(V.find("postcommit CFG high-water"), std::string::npos) << V;
}

} // namespace
