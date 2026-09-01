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
// postCommitCfgCreationViolation, the sole wall predicate consumed by the
// LateConvergence immediate seat and the independent Verify seat):
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
//     source layout position
//   * L5: the demote's recorded latch-successor rewrite (the single
//     admitted-transition record from demoteHardwareLoopToSoftware) stays
//     legal
//   * L5: one record admits exactly ONE divergence of its source; a
//     second unrecorded rewrite of the same source still fires
//   * stamp-once: a second stamp keeps the first snapshot (monotone
//     ratchet — later Finalize seats never re-stamp)
//   * clone(): the destination MFI carries no stamp and no inherited
//     identity tokens; the source snapshot keeps firing
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
// source snapshot keeps firing on source mutation.
TEST_F(HaydnPostCommitBlockBudgetTest, CloneResetsSnapshot) {
  MachineBasicBlock *Entry = &*Func().begin();
  MachineBasicBlock *Exit = &*std::next(Func().begin());
  Entry->addSuccessor(Exit);
  Info().stampPostCommitCfgSnapshot(Func());
  ASSERT_TRUE(Info().hasPostCommitBlockBudget());
  // D1.46: the source holds one L5 admission record at clone time. Record
  // it and spend it on a real edge rewrite (retarget entry's edge to
  // itself) so the record's lifecycle is exercised before the clone — the
  // clone must still start from ZERO admissions either way (an inherited
  // record would silently admit one edge rewrite on a CFG the source never
  // vetted).
  Info().recordPostCommitAdmittedEdgeTransition(Func(), *Entry);
  Entry->removeSuccessor(Exit);
  Entry->addSuccessor(Entry);
  EXPECT_TRUE(Info().postCommitCfgCreationViolation(Func()).empty())
      << "the one recorded admission covers exactly this rewrite";

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

  // D1.46 (the copy/reset law): the destination inherited NO admission —
  // an unrecorded edge rewrite on the destination fires, where an
  // inherited source record would have silently admitted it.
  MachineBasicBlock *DestEntry = &*Dest.begin();
  DestEntry->addSuccessor(DestEntry);
  EXPECT_FALSE(DestInfo->postCommitCfgCreationViolation(Dest).empty())
      << "clone must not inherit the source's admission records";
}

// L5 (D1.40 Phase 2): successor digest law. The fixture wires a real CFG
// (entry -> exit) before stamping; an edge-only rewrite — removeSuccessor +
// addSuccessor on the SAME block, block identity/token sequence unchanged —
// is refused and the violation names the offending edge SOURCE layout
// position. Nothing recorded an admitted transition.
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

// L5 admitted transition: the ONE recording site is the demote latch
// rewrite (llvm::demoteHardwareLoopToSoftware records the latch's layout
// position). A recorded latch-successor rewrite stays legal — the demote
// at FixupHwLoops/addPreEmitPass legitimately rewrites latch successors
// post-stamp; an unconditional edge freeze would false-fire on it.
TEST_F(HaydnPostCommitBlockBudgetTest, RecordedDemoteLatchRewriteStaysLegal) {
  MachineBasicBlock *Header = &*Func().begin();
  MachineBasicBlock *Latch = &*std::next(Func().begin());
  Latch->addSuccessor(Latch); // ZOL self-latch before the demote
  Info().stampPostCommitCfgSnapshot(Func());
  // The demote shape: drop every latch successor, then install the soft
  // edges (Header backedge; Exit == Header case emits one edge only).
  Info().recordPostCommitAdmittedEdgeTransition(Func(), *Latch);
  while (!Latch->succ_empty())
    Latch->removeSuccessor(Latch->succ_begin());
  Latch->addSuccessor(Header);
  EXPECT_TRUE(Info().postCommitCfgCreationViolation(Func()).empty());
}

// L5 admission is one-shot per record: a second, unrecorded rewrite of an
// already-admitted source still fires (the record was consumed by the
// first divergence; admissions can never become a standing edge-write
// permit on the recorded block). Three blocks so the second rewrite lands
// on a THIRD successor state — rewriting back to the stamped digest is
// genuinely legal (the CFG then equals the stamp) and cannot pin the law.
TEST_F(HaydnPostCommitBlockBudgetTest, AdmissionIsOneShotPerRewrite) {
  MachineBasicBlock *Header = &*Func().begin();
  MachineBasicBlock *Latch = &*std::next(Func().begin());
  MachineBasicBlock *Third = MF->CreateMachineBasicBlock();
  MF->push_back(Third); // before the stamp: no L1 growth
  Latch->addSuccessor(Header);
  Info().stampPostCommitCfgSnapshot(Func());
  Info().recordPostCommitAdmittedEdgeTransition(Func(), *Latch);
  // First rewrite (the recorded demote shape): admitted.
  while (!Latch->succ_empty())
    Latch->removeSuccessor(Latch->succ_begin());
  Latch->addSuccessor(Latch);
  ASSERT_TRUE(Info().postCommitCfgCreationViolation(Func()).empty());
  // Second rewrite of the same source, no new record: refused.
  Latch->removeSuccessor(Latch);
  Latch->addSuccessor(Third);
  const std::string V = Info().postCommitCfgCreationViolation(Func());
  EXPECT_NE(V.find("postcommit CFG edges"), std::string::npos) << V;
  EXPECT_NE(V.find("position 1"), std::string::npos) << V;
}

} // namespace
