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
//   * regUsedNonCountdownIn — "does the loop BODY still READ Prefer
//     outside residual ±1/LoopDec and Latch-block terminator zero-tests?"
//     D1.64: both Prefer-as-counter and LatchScr=Prefer require this false.
//     A trip value read in the body but dead after the loop is not a
//     clobber and not live-after-exit, yet latch SUBI32 Prefer,1 still
//     corrupts every later body read. Non-countdown body reads are
//     uses-true/clobber-false; countdown-only bodies and Latch terminator
//     LoopJNZ/BNEZ/BNEZ_W/BEQZ/BEQZ_W of the same reg are uses-false.
//     Header/interior BNEZ of the trip stays uses-true. Defs-only CB-165
//     redefinitions stay clobber-true and are not inverted into uses.
//   * pickDeadLatchScratch — pass 1a is dead-at-latch-end with an incoming
//     value. Pass 1b is a dead unused temp whose late def is CSI-sound
//     (D1.87: unused R14 is LatchScr iff the prologue saved it). After
//     1a/1b miss, return empty: occupancy
//     miss, never a pass-2 steal of an unmentioned live-through. Unused
//     unsaved R14 is occupancy miss, not a steal. pickDeadLatchScratch skips
//     unmentioned Exit-read FA (r3=sp+off) and live-into-exit tail-imm
//     (ADDI r0,imm after SET / COPY-MOVE chain) on every pass, including
//     empty Exclude, so LatchScr cannot stay that GPR. Empty LatchScr after
//     a correct skip is occupancy miss, not a steal.
//     SET-bundle coissue, a reaching def before SET, and a pred
//     live-through. A frame-address killed before SET stays a
//     pass-1a dead temp. An SMS-guard ADDI r0,imm with no Exit-path read
//     stays a sound latch temp. The caller must not copy LatchScr onto Adj
//     PreheaderScr when live at SET (va-arg-22). Not a second LoopBlocks
//     uses walker.
//   * regIsLiveIntoExitTailImm — after-SET ADDI32/ADDI32_W r0,imm copy-
//     chain live into Exit via an actual Exit-path read of any member
//     (CImm ADDI, bundled-SET last-def rewind from the BUNDLE root not
//     std::next, COPY/MOVE of the ADDI dest, COPY-kill of the ADDI dest
//     so a header redef of r14 cannot hide it, cfgPathReadsPhysReg
//     BUNDLE header implicits and bundled members). Beside FA last-def:
//     r3=sp+off stays FA; SMS-guard ADDI r0,imm with no Exit use of the
//     family stays a sound latch temp. Do not fold into isCountdownStepOf
//     or occupiedAtSet. Caller LatchExcl (FA ∪ tail-imm copy-chain)
//     re-picks so LatchScr cannot stay r3 or r14; remaining miss is
//     occupancy miss, not a live-through steal.
//   * regIsUnsoundLatchScratchAtSet — unmentioned FA last-def that Exit
//     actually reads ∪ live-into-exit tail-imm copy-chain. Unmentioned
//     $r3=ADDI SP,imm with an Exit read is unsound; the same last-def
//     with no Exit-path read stays pass-1a (PEI SP+off dead temps,
//     bqriir). $r14 ADDI r0,imm copy-chain with an Exit read is unsound.
//     Unused R14 and SMS-guard ADDI with no Exit read are not. Mentioned
//     FA ($r7 PEI SP+off) stays eligible pass-1a. Header redef of the
//     ADDI dest is a LoopBlocks mention and must not hide the chain.
//     va-arg-22 main() fill-loop: SET then a cluster of unmentioned
//     SP+off dests live into Exit (r4–r12 address temps) plus after-SET
//     ADDI r0,imm r14. LatchExcl must take every Exit-read FA, not only
//     r3; occupying the mentioned PEI dest is occupancy miss, never
//     LatchScr=Prefer / r4 / r14. Occupancy stays UseFromSet /
//     (LivePhysRegs-live && !DefInTail), not any-mention.
//   * skipLatchScrReason — production LatchExcl at all four seats
//     (NoSpill, pickDeadLatchScratch, re-pick, LatchScr=Prefer last-resort):
//     PreferHasBodyUse ∪ live-into-exit tail-imm ∪ unsound FA ∪ unmentioned
//     incoming whose Exit-path read is unproven-absent. Tail-imm is
//     independent of unsound (a helper miss must still Exclude $r14).
//     addUnmentionedLiveThrough GPRs (Exit ST32, no Latch mention) are
//     LatchExcl; WAR/snapshot bundle Exit read-then-def and header-only
//     Reads&&Defs (no member readsReg) are the same arm
//     (HaydnIntraCycleRAW.h:218-219). SMS-guard ADDI with no Exit use and
//     mentioned PEI dests stay pass-1a. PreferHasBodyUse stays a separate
//     Exclude. Occupancy miss last-resort is not Prefer. Do not fold
//     occupiedAtSet into these pins. Unused unsaved R14 is D1.87 CSI
//     refusal (pass 1b).
//   * regMentionedInPreheaderTail — SET is not a scheduling boundary; a
//     GPR dead at the SET iterator can still be the address scratch in
//     the tail (va-arg-22). instrs() walk; skip the setup MI and SET
//     member, not other members of the setup bundle. isSoundDemoteCounter
//     and Prefer-as-LatchScr still use this any-mention walker (defs).
//   * regUsedFromSetInPreheaderTail / regDefdInPreheaderTail — occupancy
//     arms beside the any-mention walker. Use-from-SET is a tail read
//     whose reaching def is at/before Ins (same-MI: collect reads vs
//     defs first). A pure tail DEF of a SET-dead CSR is mention-true,
//     use-from-SET false, defd-true (countdown_high_pressure R14) and
//     kills SET-site occupancy, including LivePhysRegs live-out
//     false-positives. Use-then-def stays occupied so cell-(d) copy
//     cannot clobber the tail use. Occupancy is
//     UseFromSet || (!available && !DefInTail).
//   * isCountdownStepOf — the closed predicate separating a proven ±1
//     residual countdown (strippable) from real compute on the same reg
//     (a clobber). A misclassification here is silent wrong code in BOTH
//     directions: strip erases live compute, or a live trip survives as a
//     bogus counter. Do not fold latch/header zero-tests into this.
//   * residualCountdownEquivalentAtLatch — header-side leftover ±1 of
//     Reg is latch-equivalent iff there is no residual outside Latch or
//     the only consumers are L2-erased latch zero-tests. Header residual
//     plus a body ST32 is a one-iteration value shift (false). Vacuous
//     and latch-only residuals are true. stripResidualCountdown of
//     LatchScr must not erase Prefer when Prefer != LatchScr.
//     Bundled leftover is one root-level erase-and-recommit: survivors
//     go through recommitSurvivingCycleMembers (exact multi-member or
//     singleton), never eraseInstrSafe of a child that leaves a stale
//     BUNDLE shell.
//   * regMentionedInBlocks / collectLoopBlocks — the CFG-blocks authority
//     the above walk (layout ranges miss a latch earlier in the function).
//   * countedSoftwareLatchViolation — D1.36 complete latch-sequence order
//     (legal long/short, swapped LUI/ADDI, follower after BNEZ, missing
//     counted edge under RequireLatch, stack-counter FI home mismatch).
//     D1.110: baked S_LW_WITH_IMM / S_SW_WITH_IMM is the same census as
//     LD32/ST32. D1.102: home is this latch's assigned FI, not pool
//     membership.
//   * demoteHardwareLoopToSoftware long-latch identity — D1.51: no LongScr
//     falls back to short BNEZ (nsichneu extra_03); D1.37 Prefer live-in
//     of Exit, long body, demote succeeds, JALR dest != countdown
//     (trip-live-after-exit; not CountReg reuse as the link).
//
// The pass-level shape (which save/restore variant each scratch/body
// combination emits) is pinned end-to-end by
// llvm/test/CodeGen/Haydn/cb165-hwloop-demote-body-redefined-tripreg.ll
// and cb162-hwloop-demote-liveout-tripreg.ll.
//
//===----------------------------------------------------------------------===//

#include "HaydnHWLoopDemote.h"
#include "HaydnHardwareLoops.h"
#include "HaydnBundlePlan.h"
#include "HaydnBundleVerify.h"
#include "HaydnInstrInfo.h"
#include "HaydnPostRAScratch.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MIRPrinter.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"

#include <utility>
#include <vector>
#include "gtest/gtest.h"

#include <iterator>
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
    // D1.36 FI-home arm reads HaydnMachineFunctionInfo scratch homes.
    MF->initTargetMachineFunctionInfo(*ST);
    // D1.61: TracksLiveness is required for LivePhysRegs walks (AIE
    // SpillExpandHelper::tracksLiveness) even though the owner never
    // reads stored MBB liveins; isLiveIn goes through
    // LivePhysRegs::available, which asserts reserved regs are frozen
    // (same fixture law as the D1.36 seat at line ~816 — a hand-built
    // MF must opt in explicitly).
    MF->getProperties().set(
        MachineFunctionProperties::Property::TracksLiveness);
    MF->getRegInfo().freezeReservedRegs();
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

  /// LUI Rd, Header — long-latch address materialize (MBB operand).
  MachineInstr &luiMBB(Register Rd, MachineBasicBlock *Tgt) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::LUI),
                    Rd)
                .addMBB(Tgt);
  }

  /// ADDI32_W Rd, Rs, Header — long-latch LO20 (MBB operand).
  MachineInstr &addi32wMBB(Register Rd, Register Rs, MachineBasicBlock *Tgt) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(),
                    TII().get(Haydn::ADDI32_W), Rd)
                .addReg(Rs)
                .addMBB(Tgt);
  }

  /// BEQZ_W Rs, Tgt — long-latch near exit test.
  MachineInstr &beqz(Register Rs, MachineBasicBlock *Tgt) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::BEQZ_W))
                .addReg(Rs)
                .addMBB(Tgt);
  }

  /// BNEZ_W Rs, Tgt — short-latch counted back-edge.
  MachineInstr &bnez(Register Rs, MachineBasicBlock *Tgt) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::BNEZ_W))
                .addReg(Rs)
                .addMBB(Tgt);
  }

  /// JALR_W Rd, Rs, 0 — long-latch register-indirect backedge.
  MachineInstr &jalr(Register Rd, Register Rs) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::JALR_W))
                .addReg(Rd, RegState::Define)
                .addReg(Rs)
                .addImm(0);
  }

  /// B Tgt — exact-commit exit shell after a short latch.
  MachineInstr &bUncond(MachineBasicBlock *Tgt) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::B))
                .addMBB(Tgt);
  }

  /// ST32 Val, Base, imm — follower / stack-counter store.
  MachineInstr &st32(Register Val, Register Base, int64_t Imm) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::ST32))
                .addReg(Val)
                .addReg(Base)
                .addImm(Imm);
  }

  /// LoopJNZ Rs, Tgt — leftover counted latch zero-test (erased at L2).
  MachineInstr &loopJnz(Register Rs, MachineBasicBlock *Tgt) {
    return *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::LoopJNZ))
                .addReg(Rs)
                .addMBB(Tgt);
  }

  /// LivePhysRegs at Ins: FPL SeedPristines=false live-ins of successors
  /// (D1.71r: never stored MBB live-ins), then stepBackward top-level
  /// only (bundle interiors after SET are invisible). Not occupancy.
  bool livePhysAtSetOf(MCPhysReg R, MachineBasicBlock::iterator Ins) const {
    const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
    LivePhysRegs AtSet(TRI);
    haydn::hwloop::addComputedSuccessorLiveIns(AtSet, *Preheader);
    for (MachineBasicBlock::iterator II = Preheader->end(); II != Ins;) {
      --II;
      AtSet.stepBackward(*II);
    }
    return !AtSet.available(MF->getRegInfo(), R);
  }

  /// Occupancy at Ins: UseFromSet || (!LivePhysRegs.available && !DefInTail).
  /// Do not restamp this to any-mention (regMentionedInPreheaderTail) and
  /// do not fold occupancy into that walker. A pure tail DEF kills SET-site
  /// occupancy; a use of the SET-site value stays occupied even when the
  /// same tail later defs the register. Seed is FPL SeedPristines=false
  /// successor live-ins (D1.71r: never stored MBB live-ins). A Header
  /// live-in with no tail def occupies SET, including Header→E second-hop
  /// and Header→Latch live-through. Never addLiveOuts pristines.
  bool occupiedAtSetOf(MCPhysReg R, MachineBasicBlock::iterator Ins) const {
    const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
    LivePhysRegs AtSet(TRI);
    haydn::hwloop::addComputedSuccessorLiveIns(AtSet, *Preheader);
    for (MachineBasicBlock::iterator II = Preheader->end(); II != Ins;) {
      --II;
      AtSet.stepBackward(*II);
    }
    return haydn::hwloop::regUsedFromSetInPreheaderTail(R, Preheader, Ins,
                                                       TRI) ||
           (!AtSet.available(MF->getRegInfo(), R) &&
            !haydn::hwloop::regDefdInPreheaderTail(R, Preheader, Ins, TRI));
  }

  /// Mark \p R as prologue-saved (CSI valid). D1.87 pass-1b R14 pin.
  void markPrologueSaved(MCPhysReg R) {
    std::vector<CalleeSavedInfo> CSI;
    CSI.emplace_back(R);
    MF->getFrameInfo().setCalleeSavedInfo(std::move(CSI));
    MF->getFrameInfo().setCalleeSavedInfoValid(true);
  }

  /// Mentioned Latch XOR + Header/Exit live-ins so occupancy 1a/1b skip
  /// these GPRs. There is no pass-2 steal of a live-through.
  void occupyLatchWindow(ArrayRef<MCPhysReg> Occ) {
    for (MCPhysReg R : Occ) {
      Latch->addLiveIn(R);
      Header->addLiveIn(R);
      Exit->addLiveIn(R);
      BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), R)
          .addReg(R)
          .addReg(R);
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
          .addReg(R)
          .addReg(Haydn::R13)
          .addImm(0);
    }
  }

  /// Unmentioned live-through: incoming via live-in, Exit use, no Latch
  /// mention. skipLatchScrReasonOf must Exclude it (unproven-absent
  /// Exit-path). After 1a/1b miss the picker returns empty (occupancy miss).
  void addUnmentionedLiveThrough(MCPhysReg R) {
    Preheader->addLiveIn(R);
    Header->addLiveIn(R);
    Latch->addLiveIn(R);
    Exit->addLiveIn(R);
    BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
        .addReg(R)
        .addReg(Haydn::R13)
        .addImm(0);
  }

  /// Caller LatchExcl: FA last-def ∪ live-into-exit tail-imm copy-chain.
  /// A header redef of r14 is a LoopBlocks mention; the helper family
  /// still reports the ADDI dest so Exclude cannot keep r3 or r14.
  void excludeFaAndTailImmFamily(SmallVectorImpl<Register> &Excl,
                                 MachineBasicBlock::const_iterator From) const {
    const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
    auto Blocks = loopBlocks();
    SmallVector<MCPhysReg, 16> Family;
    auto contains = [&](MCPhysReg P) {
      for (MCPhysReg X : Family)
        if (X == P)
          return true;
      return false;
    };
    auto add = [&](MCPhysReg P) {
      if (P == Haydn::R0 || P == Haydn::R13 || P == Haydn::R15)
        return;
      if (contains(P))
        return;
      Family.push_back(P);
    };
    static const MCPhysReg Cands[] = {
        Haydn::R1, Haydn::R2,  Haydn::R3,  Haydn::R4,  Haydn::R5,  Haydn::R6,
        Haydn::R7, Haydn::R8,  Haydn::R9,  Haydn::R10, Haydn::R11, Haydn::R12,
        Haydn::R14};
    for (MCPhysReg P : Cands) {
      if (haydn::hwloop::regIsPreheaderTailFrameAddress(P, Preheader, From,
                                                        TRI) ||
          haydn::hwloop::regIsLiveIntoExitTailImm(P, Preheader, From, Exit,
                                                 TRI) ||
          haydn::hwloop::regIsUnsoundLatchScratchAtSet(P, Preheader, From, Exit,
                                                       Blocks, TRI))
        add(P);
    }
    bool Grew = true;
    while (Grew) {
      Grew = false;
      for (const MachineInstr &MI : Preheader->instrs()) {
        if (MI.getOpcode() != TargetOpcode::COPY &&
            MI.getOpcode() != Haydn::MOVE32 && !MI.isCopy())
          continue;
        if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isReg())
          continue;
        Register Dst = MI.getOperand(0).getReg();
        Register Src = MI.getOperand(1).getReg();
        if (!Dst.isPhysical() || !Src.isPhysical())
          continue;
        if (contains(Dst.asMCReg()) && !contains(Src.asMCReg())) {
          add(Src.asMCReg());
          Grew = true;
        }
        if (contains(Src.asMCReg()) && !contains(Dst.asMCReg())) {
          add(Dst.asMCReg());
          Grew = true;
        }
      }
    }
    for (MCPhysReg P : Family)
      Excl.push_back(P);
  }

  /// Production LatchExcl: GPR32NoSPNoLR candidates where
  /// regIsUnsoundLatchScratchAtSet. Header redef of the ADDI dest must
  /// not hide the copy-chain; mentioned FA stays eligible pass-1a.
  void excludeUnsoundLatchScr(SmallVectorImpl<Register> &Excl,
                              MachineBasicBlock::const_iterator From) const {
    const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
    auto Blocks = loopBlocks();
    static const MCPhysReg Cands[] = {
        Haydn::R1, Haydn::R2,  Haydn::R3,  Haydn::R4,  Haydn::R5,  Haydn::R6,
        Haydn::R7, Haydn::R8,  Haydn::R9,  Haydn::R10, Haydn::R11, Haydn::R12,
        Haydn::R14};
    for (MCPhysReg P : Cands) {
      if (haydn::hwloop::regIsUnsoundLatchScratchAtSet(P, Preheader, From, Exit,
                                                       Blocks, TRI))
        Excl.push_back(P);
    }
  }

  /// Twin of cfgPathReadsPhysReg for skipLatchScrReasonOf. Incoming-value
  /// Exit-path read of \p Reg: skip Preheader and LoopBlocks, do not return
  /// false on a 256-guard (Seen bounds well-formed CFGs). BUNDLE-header
  /// uses (finalizeBundle ExternUses) are incoming even when the same
  /// header also has aggregated Defs (Reads && Defs is WAR/snapshot;
  /// HaydnIntraCycleRAW.h:218-219). Header Defs-only are not
  /// KilledIncoming before members. InternalRead is not a read. Do not
  /// skip headers wholesale. Same-MI def+use on a non-bundle is the new
  /// value.
  bool exitPathReadsIncomingOf(MCPhysReg Reg) const {
    if (!Exit || !Register(Reg).isPhysical())
      return false;
    const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
    auto Blocks = loopBlocks();
    return haydn::hwloop::regExitPathReadsPhysReg(Reg, Exit, Preheader, Blocks,
                                                  TRI);
  }

  /// Production skipLatchScrReason (HaydnHardwareLoops.cpp): independent
  /// arms PreferHasBodyUse, then live-into-exit tail-imm, then unsound FA,
  /// then unmentioned incoming whose Exit-path read is unproven-absent.
  /// Do not gate tail-imm on unsound — a helper miss must still Exclude
  /// $r14. PreferHasBodyUse stays a separate Exclude. Occupancy is not this
  /// predicate — do not fold occupiedAtSet into LatchExcl. Mentioned PEI
  /// dests and SMS-guard ADDI with no Exit use stay nullptr. Unused R14
  /// with no Exit-path read stays nullptr (not D1.87 CSI refusal).
  const char *skipLatchScrReasonOf(
      Register R, Register Prefer, bool PreferHasBodyUse,
      MachineBasicBlock::const_iterator From) const {
    if (!R.isPhysical())
      return nullptr;
    const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
    if (PreferHasBodyUse && TRI.regsOverlap(R, Prefer))
      return "non-countdown body use";
    if (haydn::hwloop::regIsLiveIntoExitTailImm(R.asMCReg(), Preheader, From,
                                                Exit, TRI))
      return "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)";
    auto Blocks = loopBlocks();
    if (haydn::hwloop::regIsUnsoundLatchScratchAtSet(
            R.asMCReg(), Preheader, From, Exit, Blocks, TRI))
      return "preheader-tail frame address (va-arg-22)";
    if (haydn::hwloop::regExitPathReadsPhysReg(R.asMCReg(), Exit, Preheader,
                                              Blocks, TRI))
      return "exit-path read of incoming value";
    return nullptr;
  }

  /// Production countRegUnsoundAtSet: tail mention or FA last-def. Last-resort
  /// LatchScr=Prefer is gated by skipLatchScrReason AND this. Occupancy is
  /// not this predicate.
  bool countRegUnsoundAtSetOf(
      Register R, MachineBasicBlock::const_iterator From) const {
    if (!R.isPhysical())
      return false;
    const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
    return haydn::hwloop::regMentionedInPreheaderTail(R.asMCReg(), Preheader,
                                                      From, TRI) ||
           haydn::hwloop::regIsPreheaderTailFrameAddress(R.asMCReg(), Preheader,
                                                         From, TRI);
  }

  /// Production LatchExcl: GPR32NoSPNoLR where skipLatchScrReason fires.
  void excludeSkipLatchScr(SmallVectorImpl<Register> &Excl,
                           MachineBasicBlock::const_iterator From,
                           Register Prefer, bool PreferHasBodyUse) const {
    for (MCPhysReg P : Haydn::GPR32NoSPNoLRRegClass) {
      if (skipLatchScrReasonOf(Register(P), Prefer, PreferHasBodyUse, From))
        Excl.push_back(Register(P));
    }
  }

  static bool exclContains(ArrayRef<Register> Excl, MCPhysReg P) {
    for (Register R : Excl)
      if (R == P)
        return true;
    return false;
  }

  /// Four LatchScr seats: NoSpill, pickDeadLatchScratch, re-pick while
  /// skipLatchScrReason, last-resort Prefer iff !PreferHasBodyUse &&
  /// !skipLatchScrReason && !countRegUnsoundAtSet. Occupancy miss with
  /// PreferHasBodyUse must not become LatchScr=Prefer. Post-pick refuse
  /// matches production so a helper-arm miss cannot keep Prefer.
  Register pickLatchScrFourSeats(MachineBasicBlock::const_iterator From,
                                 Register Prefer,
                                 bool PreferHasBodyUse) const {
    SmallVector<Register, 16> LatchExcl;
    excludeSkipLatchScr(LatchExcl, From, Prefer, PreferHasBodyUse);
    MachineBasicBlock *PostSuccs[] = {Header, Exit};
    auto Blocks = loopBlocks();
    auto pickLatchScr = [&]() -> Register {
      Register Scr = findPostRAScratchNoSpill(
          *Latch, Latch->end(), /*PreferNotR12=*/true, PostSuccs, LatchExcl);
      if (!Scr.isPhysical())
        Scr = haydn::hwloop::pickDeadLatchScratch(*Latch, PostSuccs, Blocks,
                                                  LatchExcl, "test");
      return Scr;
    };
    Register LatchScr = pickLatchScr();
    while (LatchScr.isPhysical()) {
      if (skipLatchScrReasonOf(LatchScr, Prefer, PreferHasBodyUse, From)) {
        LatchExcl.push_back(LatchScr);
        LatchScr = pickLatchScr();
        continue;
      }
      break;
    }
    if (!LatchScr.isPhysical() && Prefer.isPhysical() &&
        Prefer != Haydn::R0 && Prefer != Haydn::R13 &&
        Prefer != Haydn::R15 && !PreferHasBodyUse &&
        !skipLatchScrReasonOf(Prefer, Prefer, PreferHasBodyUse, From) &&
        !countRegUnsoundAtSetOf(Prefer, From))
      LatchScr = Prefer;
    if (LatchScr.isPhysical() &&
        (skipLatchScrReasonOf(LatchScr, Prefer, PreferHasBodyUse, From) ||
         (PreferHasBodyUse && LatchScr == Prefer)))
      LatchScr = Register();
    return LatchScr;
  }

  MachineInstr &setHwLoopAtPreheaderEnd(Register Prefer = Haydn::R2) {
    return *BuildMI(*Preheader, Preheader->end(), DebugLoc(),
                    TII().get(Haydn::SET_HWLOOP_F2_W))
                .addImm(0)
                .addMBB(Header)
                .addMBB(Latch)
                .addReg(Prefer)
                .addReg(Haydn::SFR, RegState::ImplicitDefine);
  }
};

// collectLoopBlocks is the CFG-blocks authority: header, latch, and the
// interior, without the preheader or designated exit.
TEST_F(HaydnHWLoopDemoteTest, CollectLoopBlocksOwnsHeaderLatchNotExits) {
  auto Blocks = loopBlocks();
  EXPECT_TRUE(Blocks.contains(Header));
  EXPECT_TRUE(Blocks.contains(Latch));
  EXPECT_FALSE(Blocks.contains(Preheader));
  EXPECT_FALSE(Blocks.contains(Exit));
}

// D1.101: early-exit-only (Header → Break → Exit, Break not reverse-
// reachable from Latch) is in the region. Prefer use there is a body use.
TEST_F(HaydnHWLoopDemoteTest, CollectLoopBlocksIncludesEarlyExitOnlyPreferUse) {
  MachineBasicBlock *Break = MF->CreateMachineBasicBlock();
  MF->insert(Exit->getIterator(), Break);
  Header->addSuccessor(Break);
  Break->addSuccessor(Exit);
  BuildMI(*Break, Break->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R2)
      .addReg(Haydn::R13)
      .addImm(0);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(Blocks.contains(Break))
      << "early-exit-only block must be in the loop region";
  EXPECT_FALSE(Blocks.contains(Exit));
  EXPECT_FALSE(Blocks.contains(Preheader));
  EXPECT_TRUE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch))
      << "Prefer use on the break path is a body use (N-k if Prefer-as-counter)";
}

// CB-165 core law: a body LOAD dest in the trip physreg is a non-countdown
// redefinition. This is exactly the pr51581-2 shape — the pipelined body
// defines the loop-carried value in the same physreg the ZOL trip used, so
// the demote's exit value originates from the body, never from the trip.
// D1.64: dest-only is clobber-true / uses-false — do not invert this pin
// into a use (LD32 outs R5, ins R4; the trip is not read).
TEST_F(HaydnHWLoopDemoteTest, BodyLoadDestIsNonCountdownRedefinition) {
  ld32(Haydn::R5, Haydn::R4, 0);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
}

// A pure body MOVE into the trip physreg is equally a redefinition.
// D1.64: dest-only MOVE is clobber-true / uses-false (outs R5, ins R6).
TEST_F(HaydnHWLoopDemoteTest, BodyMoveDestIsNonCountdownRedefinition) {
  move32(Haydn::R5, Haydn::R6);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
}

// The CB-162 / D1.64 countdown-only shape: residual ±1 steps are neither a
// redefinition nor a non-countdown use (the ±1/LoopDec chain is skipped).
// Prefer stays a sound countdown candidate; do not treat the step's own
// src read as a body use of the trip.
TEST_F(HaydnHWLoopDemoteTest, PureCountdownBodyIsNotARedefinition) {
  addi32(Haydn::R5, Haydn::R5, -1);
  subi32(Haydn::R5, Haydn::R5, 1);
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
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
  // Not a countdown, so the src read of R5 is a non-countdown use as well.
  // Clobber stays true (CB-165); do not drop the def into uses-only.
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
}

// A def of a DIFFERENT register is not a countdown of Reg even when the
// shape matches (isCountdownStepOf must def Reg itself).
TEST_F(HaydnHWLoopDemoteTest, OtherRegDefIsNotCountdownOfReg) {
  MachineInstr &Other = addi32(Haydn::R6, Haydn::R6, -1);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(Other, Haydn::R5));
}

// D1.64: a non-countdown body READ of the trip reg is a use, not a
// redefinition. Counter candidates must refuse this (uses-true) even when
// the trip is dead after the loop; CB-165 save placement stays
// PreheaderSave (clobber-false: a read cannot change the exit value).
// mention stays true (pickCounterReg already consults that for non-Prefer).
TEST_F(HaydnHWLoopDemoteTest, BodyUseMentionsButDoesNotRedefine) {
  MachineInstr &Use = add32(Haydn::R6, Haydn::R5, Haydn::R2);
  (void)Use;
  auto Blocks = loopBlocks();
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R5, Blocks));
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// D1.64 product shape: a body ST32 of the trip (dead after the loop) is a
// use, not a redefinition. Latch SUBI32 Prefer,1 would corrupt this store.
TEST_F(HaydnHWLoopDemoteTest, BodyStoreOfTripIsUseNotRedefinition) {
  st32(Haydn::R5, Haydn::R13, 0);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R5, Blocks));
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// Header leftover ±1 plus a body ST32 of the same register is a
// one-iteration value shift if strip reinstalls SUBI32 at latch end.
TEST_F(HaydnHWLoopDemoteTest,
       HeaderResidualPlusBodyStoreIsNotLatchEquivalent) {
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R5)
      .addReg(Haydn::R5)
      .addImm(-1);
  st32(Haydn::R5, Haydn::R13, 0);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(haydn::hwloop::residualCountdownEquivalentAtLatch(
      Blocks, Haydn::R5, Latch))
      << "header ADDI32 Prefer,Prefer,-1 plus body ST32 of Prefer is a "
         "value shift, not latch leftover";
  EXPECT_TRUE(
      haydn::hwloop::isCountdownStepOf(*Header->begin(), Haydn::R5));
}

// Latch-only leftover is the reinstall position: equivalent even when
// the latch also has a terminator zero-test (L2-erased).
TEST_F(HaydnHWLoopDemoteTest, LatchOnlyResidualIsLatchEquivalent) {
  subi32(Haydn::R5, Haydn::R5, 1);
  bnez(Haydn::R5, Header);
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_TRUE(haydn::hwloop::residualCountdownEquivalentAtLatch(
      Blocks, Haydn::R5, Latch));
}

// Header leftover whose only consumer is an L2-erased latch BNEZ_W is
// equivalent (body never reads the shifted value). Header BNEZ stays a
// body use and is not this pin.
TEST_F(HaydnHWLoopDemoteTest,
       HeaderResidualWithOnlyLatchBnezIsLatchEquivalent) {
  MachineInstr &Step =
      *BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::ADDI32),
               Haydn::R5)
           .addReg(Haydn::R5)
           .addImm(-1);
  MachineInstr &T = bnez(Haydn::R5, Header);
  EXPECT_TRUE(haydn::hwloop::isCountdownStepOf(Step, Haydn::R5));
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(T, Haydn::R5))
      << "do not fold latch zero-tests into isCountdownStepOf";
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_TRUE(haydn::hwloop::residualCountdownEquivalentAtLatch(
      Blocks, Haydn::R5, Latch));
}

// Vacuous leftover (no ±1 of Reg) is latch-equivalent.
TEST_F(HaydnHWLoopDemoteTest, NoResidualIsLatchEquivalent) {
  st32(Haydn::R6, Haydn::R13, 0);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(haydn::hwloop::residualCountdownEquivalentAtLatch(
      Blocks, Haydn::R5, Latch));
}

// stack-arm Prefer != LatchScr is not the strip target: leftover ±1 of
// Prefer is a live IV. stripResidualCountdown(LatchScr) must leave it.
TEST_F(HaydnHWLoopDemoteTest,
       StripResidualCountdownLeavesPreferWhenTargetIsLatchScr) {
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R2)
      .addReg(Haydn::R2)
      .addImm(-1);
  st32(Haydn::R2, Haydn::R13, 0);
  auto Blocks = loopBlocks();
  EXPECT_FALSE(haydn::hwloop::residualCountdownEquivalentAtLatch(
      Blocks, Haydn::R2, Latch));
  EXPECT_TRUE(haydn::hwloop::residualCountdownEquivalentAtLatch(
      Blocks, Haydn::R14, Latch))
      << "LatchScr has no leftover; it is the strip target";
  haydn::hwloop::stripResidualCountdown(Blocks, Haydn::R14, TII());
  bool PreferStepRemains = false;
  for (const MachineInstr &MI : Header->instrs()) {
    if (haydn::hwloop::isCountdownStepOf(MI, Haydn::R2))
      PreferStepRemains = true;
  }
  EXPECT_TRUE(PreferStepRemains)
      << "stack-arm Prefer!=LatchScr is not the strip target";
}

// Bundled ST32 + countdown: dissolve the original root once and
// exact-commit the store as a singleton. eraseInstrSafe of the child
// would leave a stale BUNDLE shell (implicits/kills of the countdown).
TEST_F(HaydnHWLoopDemoteTest,
       StripResidualCountdownRecommitsBundledStoreSibling) {
  MachineInstr &St =
      *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::ST32))
           .addReg(Haydn::R6)
           .addReg(Haydn::R13)
           .addImm(0);
  MachineInstr &Dec =
      *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::ADDI32),
               Haydn::R5)
           .addReg(Haydn::R5)
           .addImm(-1);
  finalizeBundle(*Latch, St.getIterator(), std::next(Dec.getIterator()));
  ASSERT_TRUE(St.isBundled());
  ASSERT_TRUE(Dec.isBundled());
  MachineInstr &OldRoot = *getBundleStart(St.getIterator());
  ASSERT_TRUE(OldRoot.isBundle());
  auto Blocks = loopBlocks();
  EXPECT_TRUE(haydn::hwloop::isCountdownStepOf(Dec, Haydn::R5));
  haydn::hwloop::stripResidualCountdown(Blocks, Haydn::R5, TII());

  bool CountdownRemains = false;
  MachineInstr *Store = nullptr;
  for (MachineInstr &MI : Latch->instrs()) {
    if (haydn::hwloop::isCountdownStepOf(MI, Haydn::R5))
      CountdownRemains = true;
    if (!MI.isBundle() && MI.mayStore())
      Store = &MI;
  }
  EXPECT_FALSE(CountdownRemains) << "bundled leftover ±1 must be stripped";
  ASSERT_NE(Store, nullptr);
  EXPECT_TRUE(Store->isBundled())
      << "store sibling must be exact-committed, not a bare real";
  MachineInstr &NewRoot = *getBundleStart(Store->getIterator());
  EXPECT_TRUE(NewRoot.isBundle());
  EXPECT_TRUE(haydn::bundle::getBundleRowID(NewRoot).has_value())
      << "survivor packet must be stamped; constructive Finalize is not "
         "the recommit";
  SmallVector<MachineInstr *, 3> Kids = haydn::bundle::members(NewRoot);
  ASSERT_EQ(Kids.size(), 1u);
  EXPECT_EQ(Kids[0], Store);
  for (const MachineOperand &MO : NewRoot.operands()) {
    EXPECT_FALSE(MO.isReg() && MO.isDef() && MO.getReg() == Haydn::R5)
        << "stale countdown dest must not remain as a BUNDLE implicit-def";
  }
}

// Proven-disjoint ST32+LD32 coissued with a countdown: after strip the
// memory pair stays one product cycle (TII same-base, null AA).
TEST_F(HaydnHWLoopDemoteTest,
       StripResidualCountdownRecommitsProvenDisjointMemSiblings) {
  auto *GV = new GlobalVariable(*M, Type::getInt32Ty(*Ctx), /*isConstant=*/false,
                                GlobalValue::ExternalLinkage, nullptr, "obj");
  auto addMMO = [&](MachineInstr &MI, int64_t ByteOff, bool IsStore) {
    MachineMemOperand::Flags F =
        IsStore ? MachineMemOperand::MOStore : MachineMemOperand::MOLoad;
    MI.addMemOperand(*MF, MF->getMachineMemOperand(
                              MachinePointerInfo(GV, ByteOff), F, 4, Align(4)));
  };
  MachineInstr &St =
      *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::ST32))
           .addReg(Haydn::R6)
           .addReg(Haydn::R13)
           .addImm(0);
  addMMO(St, /*ByteOff=*/0, /*IsStore=*/true);
  MachineInstr &Ld =
      *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::LD32),
               Haydn::R7)
           .addReg(Haydn::R13)
           .addImm(1);
  addMMO(Ld, /*ByteOff=*/4, /*IsStore=*/false);
  MachineInstr &Dec =
      *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::ADDI32),
               Haydn::R5)
           .addReg(Haydn::R5)
           .addImm(-1);
  finalizeBundle(*Latch, St.getIterator(), std::next(Dec.getIterator()));
  auto Blocks = loopBlocks();
  haydn::hwloop::stripResidualCountdown(Blocks, Haydn::R5, TII());

  bool CountdownRemains = false;
  MachineInstr *Store = nullptr;
  MachineInstr *Load = nullptr;
  for (MachineInstr &MI : Latch->instrs()) {
    if (haydn::hwloop::isCountdownStepOf(MI, Haydn::R5))
      CountdownRemains = true;
    if (MI.isBundle())
      continue;
    if (MI.mayStore())
      Store = &MI;
    if (MI.mayLoad())
      Load = &MI;
  }
  EXPECT_FALSE(CountdownRemains);
  ASSERT_NE(Store, nullptr);
  ASSERT_NE(Load, nullptr);
  EXPECT_TRUE(Store->isBundled());
  EXPECT_TRUE(Load->isBundled());
  MachineInstr &NewRoot = *getBundleStart(Store->getIterator());
  EXPECT_EQ(&NewRoot, &*getBundleStart(Load->getIterator()))
      << "proven-disjoint ST+LD must stay one packet after countdown strip";
  EXPECT_TRUE(haydn::bundle::getBundleRowID(NewRoot).has_value());
  SmallVector<MachineInstr *, 3> Kids = haydn::bundle::members(NewRoot);
  ASSERT_EQ(Kids.size(), 2u);
  bool HasST = false;
  bool HasLD = false;
  for (MachineInstr *K : Kids) {
    HasST |= K->mayStore();
    HasLD |= K->mayLoad();
  }
  EXPECT_TRUE(HasST);
  EXPECT_TRUE(HasLD);
}

// Unbundled leftover still uses eraseInstrSafe (no BUNDLE root).
TEST_F(HaydnHWLoopDemoteTest, StripResidualCountdownErasesBareCountdown) {
  addi32(Haydn::R5, Haydn::R5, -1);
  auto Blocks = loopBlocks();
  haydn::hwloop::stripResidualCountdown(Blocks, Haydn::R5, TII());
  bool Remains = false;
  for (const MachineInstr &MI : Latch->instrs()) {
    if (haydn::hwloop::isCountdownStepOf(MI, Haydn::R5))
      Remains = true;
  }
  EXPECT_FALSE(Remains);
}

// Latch-block terminator zero-tests of the trip are erased by the L2
// latch-terminator sweep before SUBI32+BNEZ_W. They are not live-in-body
// reads. Do not fold them into isCountdownStepOf (that would let
// stripResidualCountdown erase live conds).
TEST_F(HaydnHWLoopDemoteTest, LatchBnezWOfTripIsNotABodyUse) {
  MachineInstr &T = bnez(Haydn::R5, Header);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(T, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, LatchBeqzWOfTripIsNotABodyUse) {
  MachineInstr &T = beqz(Haydn::R5, Exit);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(T, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, LatchLoopJnzOfTripIsNotABodyUse) {
  MachineInstr &T = loopJnz(Haydn::R5, Header);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(T, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// L2 erases every latch terminator, including bundle interiors. The
// zero-test skip must walk instrs() members, not top-level only.
TEST_F(HaydnHWLoopDemoteTest, LatchBundledBnezWOfTripIsNotABodyUse) {
  MachineInstr *Bund =
      BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *T =
      BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::BNEZ_W))
          .addReg(Haydn::R5)
          .addMBB(Header)
          .getInstr();
  T->bundleWithPred();
  (void)Bund;
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(*T, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "latch-terminator zero-test skip includes bundle interiors";
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// Header/interior early-exit BNEZ of the trip is a live body use even
// when the Latch terminator of the same opcode is skipped. Restrict the
// zero-test skip to the Latch block.
TEST_F(HaydnHWLoopDemoteTest, HeaderBnezOfTripIsABodyUse) {
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R5)
      .addMBB(Latch);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, InteriorBnezOfTripIsABodyUse) {
  MachineBasicBlock *Interior = MF->CreateMachineBasicBlock();
  MF->insert(Latch->getIterator(), Interior);
  Header->replaceSuccessor(Latch, Interior);
  Interior->addSuccessor(Latch);
  Interior->addSuccessor(Exit);
  BuildMI(*Interior, Interior->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R5)
      .addMBB(Exit);
  auto Blocks = loopBlocks();
  ASSERT_TRUE(Blocks.contains(Interior));
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// Same-opcode discrimination: header/interior/early-exit zero-tests of
// BNEZ_W/BEQZ_W/LoopJNZ stay body uses even when the latch terminator of
// that opcode is skipped. An opcode-only skip is a miscompile. Latch
// BNEZ/BEQZ (non-W) are the soft-latch family and are not body uses.
// Latch BNE_W is a two-reg compare — do not fold it into the zero-test skip.
// KEEP these header-vs-latch units as the D1.64 owner seal. Do not restamp
// as d171 overlay occupancy or cb162 live-OUT.
TEST_F(HaydnHWLoopDemoteTest, HeaderBnezWOfTripIsABodyUse) {
  MachineInstr &T =
      *BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ_W))
           .addReg(Haydn::R5)
           .addMBB(Exit);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(T, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, HeaderBeqzWOfTripIsABodyUse) {
  MachineInstr &T =
      *BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BEQZ_W))
           .addReg(Haydn::R5)
           .addMBB(Exit);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(T, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, HeaderLoopJnzOfTripIsABodyUse) {
  MachineInstr &T =
      *BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::LoopJNZ))
           .addReg(Haydn::R5)
           .addMBB(Exit);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(T, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, HeaderBnezWStaysUseWhenLatchBnezWIsSkipped) {
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ_W))
      .addReg(Haydn::R5)
      .addMBB(Exit);
  MachineInstr &LatchT = bnez(Haydn::R5, Header);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(LatchT, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "header BNEZ_W of Prefer is a body use; latch BNEZ_W skip is "
         "latch-block only";
}

TEST_F(HaydnHWLoopDemoteTest, HeaderBeqzWStaysUseWhenLatchBeqzWIsSkipped) {
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BEQZ_W))
      .addReg(Haydn::R5)
      .addMBB(Exit);
  beqz(Haydn::R5, Exit);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "header BEQZ_W of Prefer is a body use; latch BEQZ_W skip is "
         "latch-block only";
}

TEST_F(HaydnHWLoopDemoteTest, HeaderLoopJnzStaysUseWhenLatchLoopJnzIsSkipped) {
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::LoopJNZ))
      .addReg(Haydn::R5)
      .addMBB(Exit);
  MachineInstr &LatchT = loopJnz(Haydn::R5, Header);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(LatchT, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "header LoopJNZ of Prefer is a body use; latch LoopJNZ skip is "
         "latch-block only";
}

TEST_F(HaydnHWLoopDemoteTest, InteriorBnezWOfTripIsABodyUse) {
  MachineBasicBlock *Interior = MF->CreateMachineBasicBlock();
  MF->insert(Latch->getIterator(), Interior);
  Header->replaceSuccessor(Latch, Interior);
  Interior->addSuccessor(Latch);
  Interior->addSuccessor(Exit);
  BuildMI(*Interior, Interior->end(), DebugLoc(), TII().get(Haydn::BNEZ_W))
      .addReg(Haydn::R5)
      .addMBB(Exit);
  auto Blocks = loopBlocks();
  ASSERT_TRUE(Blocks.contains(Interior));
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "interior BNEZ_W of Prefer is a body use (same opcode as latch skip)";
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, InteriorBeqzWOfTripIsABodyUse) {
  MachineBasicBlock *Interior = MF->CreateMachineBasicBlock();
  MF->insert(Latch->getIterator(), Interior);
  Header->replaceSuccessor(Latch, Interior);
  Interior->addSuccessor(Latch);
  Interior->addSuccessor(Exit);
  BuildMI(*Interior, Interior->end(), DebugLoc(), TII().get(Haydn::BEQZ_W))
      .addReg(Haydn::R5)
      .addMBB(Exit);
  auto Blocks = loopBlocks();
  ASSERT_TRUE(Blocks.contains(Interior));
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "interior/early-exit BEQZ_W of Prefer is a body use (same opcode as "
         "latch skip)";
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, InteriorLoopJnzOfTripIsABodyUse) {
  MachineBasicBlock *Interior = MF->CreateMachineBasicBlock();
  MF->insert(Latch->getIterator(), Interior);
  Header->replaceSuccessor(Latch, Interior);
  Interior->addSuccessor(Latch);
  Interior->addSuccessor(Exit);
  BuildMI(*Interior, Interior->end(), DebugLoc(), TII().get(Haydn::LoopJNZ))
      .addReg(Haydn::R5)
      .addMBB(Exit);
  auto Blocks = loopBlocks();
  ASSERT_TRUE(Blocks.contains(Interior));
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "interior/early-exit LoopJNZ of Prefer is a body use (same opcode as "
         "latch skip)";
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// Interior/early-exit zero-tests stay body uses even when the latch
// terminator of the same opcode is skipped. An opcode-wide skip would
// let this look like a non-use and last-resort LatchScr=Prefer.
TEST_F(HaydnHWLoopDemoteTest, InteriorBnezWStaysUseWhenLatchBnezWIsSkipped) {
  MachineBasicBlock *Interior = MF->CreateMachineBasicBlock();
  MF->insert(Latch->getIterator(), Interior);
  Header->replaceSuccessor(Latch, Interior);
  Interior->addSuccessor(Latch);
  Interior->addSuccessor(Exit);
  BuildMI(*Interior, Interior->end(), DebugLoc(), TII().get(Haydn::BNEZ_W))
      .addReg(Haydn::R5)
      .addMBB(Exit);
  MachineInstr &LatchT = bnez(Haydn::R5, Header);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(LatchT, Haydn::R5));
  auto Blocks = loopBlocks();
  ASSERT_TRUE(Blocks.contains(Interior));
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "interior BNEZ_W of Prefer is a body use; latch BNEZ_W skip is "
         "latch-block only";
}

TEST_F(HaydnHWLoopDemoteTest, InteriorBeqzWStaysUseWhenLatchBeqzWIsSkipped) {
  MachineBasicBlock *Interior = MF->CreateMachineBasicBlock();
  MF->insert(Latch->getIterator(), Interior);
  Header->replaceSuccessor(Latch, Interior);
  Interior->addSuccessor(Latch);
  Interior->addSuccessor(Exit);
  BuildMI(*Interior, Interior->end(), DebugLoc(), TII().get(Haydn::BEQZ_W))
      .addReg(Haydn::R5)
      .addMBB(Exit);
  beqz(Haydn::R5, Exit);
  auto Blocks = loopBlocks();
  ASSERT_TRUE(Blocks.contains(Interior));
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "interior/early-exit BEQZ_W of Prefer is a body use; latch BEQZ_W "
         "skip is latch-block only";
}

TEST_F(HaydnHWLoopDemoteTest,
       InteriorLoopJnzStaysUseWhenLatchLoopJnzIsSkipped) {
  MachineBasicBlock *Interior = MF->CreateMachineBasicBlock();
  MF->insert(Latch->getIterator(), Interior);
  Header->replaceSuccessor(Latch, Interior);
  Interior->addSuccessor(Latch);
  Interior->addSuccessor(Exit);
  BuildMI(*Interior, Interior->end(), DebugLoc(), TII().get(Haydn::LoopJNZ))
      .addReg(Haydn::R5)
      .addMBB(Exit);
  MachineInstr &LatchT = loopJnz(Haydn::R5, Header);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(LatchT, Haydn::R5));
  auto Blocks = loopBlocks();
  ASSERT_TRUE(Blocks.contains(Interior));
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "interior/early-exit LoopJNZ of Prefer is a body use; latch LoopJNZ "
         "skip is latch-block only";
}

// Named early-exit (reverse-reachable from latch, Exit successor): BNEZ_W
// of Prefer stays PreferHasBodyUse. Four seats Exclude FA ∪ tail-imm ∪
// PreferHasBodyUse and keep pass-1a r7. Occupying r7 is occupancy miss,
// never LatchScr=Prefer / FA r3 / tail-imm r4. Unused unsaved R14 is
// D1.87 refuse (not LatchExcl, not pass-1b). Opcode-wide skip of BNEZ_W
// would make PreferHasBodyUse false here.
TEST_F(HaydnHWLoopDemoteTest,
       EarlyExitBnezWStaysUseFourSeatsRefusePreferAndR14Steal) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R5, Haydn::R6, Haydn::R8, Haydn::R9,
      Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), Haydn::R7)
      .addReg(Haydn::R7)
      .addReg(Haydn::R7);
  MachineBasicBlock *EarlyExit = MF->CreateMachineBasicBlock();
  MF->insert(Latch->getIterator(), EarlyExit);
  Header->replaceSuccessor(Latch, EarlyExit);
  EarlyExit->addSuccessor(Latch);
  EarlyExit->addSuccessor(Exit);
  BuildMI(*EarlyExit, EarlyExit->end(), DebugLoc(), TII().get(Haydn::BNEZ_W))
      .addReg(Haydn::R2)
      .addMBB(Exit);
  MachineInstr &LatchT = bnez(Haydn::R2, Header);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(LatchT, Haydn::R2));

  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R4)
      .addReg(Haydn::R0)
      .addImm(22);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(0);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R4)
      .addReg(Haydn::R13)
      .addImm(0);

  auto Blocks = loopBlocks();
  ASSERT_TRUE(Blocks.contains(EarlyExit));
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch))
      << "early-exit BNEZ_W of Prefer is a body use; latch BNEZ_W skip is "
         "latch-block only";
  EXPECT_FALSE(haydn::hwloop::regClobberedNonCountdownIn(Haydn::R2, Blocks))
      << "early-exit zero-test is a use, not a CB-165 clobber";

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse)
      << "opcode-wide BNEZ_W skip would hide early-exit PreferHasBodyUse";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R4, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "pass-1a dead temp r7 stays out of skipLatchScrReason LatchExcl";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "unused R14 stays out of skipLatchScrReason LatchExcl";

  SmallVector<Register, 16> LatchExcl;
  excludeSkipLatchScr(LatchExcl, From, Haydn::R2, PreferHasBodyUse);
  EXPECT_TRUE(exclContains(LatchExcl, Haydn::R2))
      << "skipLatchScrReason LatchExcl includes PreferHasBodyUse";
  EXPECT_TRUE(exclContains(LatchExcl, Haydn::R3));
  EXPECT_TRUE(exclContains(LatchExcl, Haydn::R4));
  EXPECT_FALSE(exclContains(LatchExcl, Haydn::R7));
  EXPECT_FALSE(exclContains(LatchExcl, Haydn::R14));

  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  Register AfterExcl = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, LatchExcl, "test");
  EXPECT_EQ(AfterExcl, Register(Haydn::R7))
      << "four seats: pass-1a dead temp after FA ∪ tail-imm ∪ PreferHasBodyUse "
         "Exclude";
  EXPECT_NE(AfterExcl, Register(Haydn::R2))
      << "PreferHasBodyUse must not become a pass-2 steal of Prefer";
  EXPECT_NE(AfterExcl, Register(Haydn::R3))
      << "populated Exclude must not keep Exit-read FA r3";
  EXPECT_NE(AfterExcl, Register(Haydn::R4))
      << "populated Exclude must not keep live-into-exit tail-imm r4";
  EXPECT_NE(AfterExcl, Register(Haydn::R14))
      << "defined dead temp wins over unused R14; R14 is not a pass-2 steal";

  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_EQ(Four, Register(Haydn::R7))
      << "four seats: pass-1a dead temp after PreferHasBodyUse Exclude";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
  EXPECT_NE(Four, Register(Haydn::R3))
      << "four seats must not steal Exit-read FA r3";
  EXPECT_NE(Four, Register(Haydn::R4))
      << "four seats must not steal live-into-exit tail-imm r4";
  EXPECT_NE(Four, Register(Haydn::R14))
      << "four seats must not take unused R14 while pass-1a lives";

  occupyLatchWindow(ArrayRef<MCPhysReg>({Haydn::R7}));
  Register FourMiss = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_NE(FourMiss, Register(Haydn::R2))
      << "occupancy miss with PreferHasBodyUse must not become LatchScr=Prefer";
  EXPECT_NE(FourMiss, Register(Haydn::R3))
      << "occupancy miss must not steal Exit-read FA r3";
  EXPECT_NE(FourMiss, Register(Haydn::R4))
      << "occupancy miss must not steal live-into-exit tail-imm r4";
  EXPECT_NE(FourMiss, Register(Haydn::R14))
      << "D1.87: unsaved unused R14 is not pass-1b LatchScr";
  EXPECT_FALSE(FourMiss.isPhysical())
      << "occupying pass-1a is occupancy miss, not last-resort Prefer or "
         "unsaved R14";
}

TEST_F(HaydnHWLoopDemoteTest, LatchBnezOfTripIsNotABodyUse) {
  MachineInstr &T =
      *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::BNEZ))
           .addReg(Haydn::R5)
           .addMBB(Header);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(T, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, LatchBeqzOfTripIsNotABodyUse) {
  MachineInstr &T =
      *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::BEQZ))
           .addReg(Haydn::R5)
           .addMBB(Exit);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(T, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch));
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, LatchBneWOfTripIsABodyUse) {
  MachineInstr &T =
      *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::BNE_W))
           .addReg(Haydn::R5)
           .addReg(Haydn::R6)
           .addMBB(Header);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(T, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "latch BNE_W is a two-reg compare, not a zero-test skip";
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, PureLoopDecBodyIsNotAUse) {
  MachineInstr &Dec =
      *BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::LoopDec),
               Haydn::R5)
           .addReg(Haydn::R5);
  EXPECT_TRUE(haydn::hwloop::isCountdownStepOf(Dec, Haydn::R5));
  auto Blocks = loopBlocks();
  EXPECT_FALSE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "LoopDec src-read is a residual countdown, not a body use";
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

TEST_F(HaydnHWLoopDemoteTest, ImplicitUseOfTripIsABodyUse) {
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::ADD32), Haydn::R6)
      .addReg(Haydn::R2)
      .addReg(Haydn::R4)
      .addReg(Haydn::R5, RegState::Implicit);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R5, Blocks, Latch))
      << "MO.readsReg() covers implicit uses of Prefer";
  EXPECT_FALSE(
      haydn::hwloop::regClobberedNonCountdownIn(Haydn::R5, Blocks));
}

// D1.87: unused unsaved R14 is not pass-1b LatchScr (no CSI proof).
TEST_F(HaydnHWLoopDemoteTest, PickDeadLatchScratchRefusesUnsavedUnusedR14) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2,  Haydn::R3,  Haydn::R4, Haydn::R5,  Haydn::R6,
      Haydn::R7, Haydn::R8,  Haydn::R9,  Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  EXPECT_FALSE(haydn::hwloop::calleeSavedLateDefIsSound(Haydn::R14, *MF, TRI))
      << "unsaved R14 is not a sound late def";
  EXPECT_TRUE(haydn::hwloop::calleeSavedLateDefIsSound(Haydn::R7, *MF, TRI))
      << "caller-saved is sound without CSI";
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_FALSE(Scr.isPhysical())
      << "D1.87: unsaved unused R14 is occupancy miss, not pass-1b";
  EXPECT_NE(Scr, Register(Haydn::R14));
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "unused R14 is not live at SET; no overlay ST";
}

// D1.87 saved-R14 pin: unused R14 with CSI is a sound pass-1b LD32 dest.
TEST_F(HaydnHWLoopDemoteTest, PickDeadLatchScratchPicksSavedUnusedR14) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2,  Haydn::R3,  Haydn::R4, Haydn::R5,  Haydn::R6,
      Haydn::R7, Haydn::R8,  Haydn::R9,  Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  markPrologueSaved(Haydn::R14);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  EXPECT_TRUE(haydn::hwloop::calleeSavedLateDefIsSound(Haydn::R14, *MF, TRI))
      << "CSI-saved R14 is a sound late def";
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_EQ(Scr, Register(Haydn::R14))
      << "occupancy 1b: CSI-saved unused R14 is a sound LD32 dest";
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "occupancy 1b unused R14 is not live at SET; no overlay ST";
}

// Occupancy 1a: a defined caller-saved temp dead at latch end vs
// {Header, Exit} wins over unused R14 (pass 1b). Overlay ST is not
// required: the SET site is not occupied.
TEST_F(HaydnHWLoopDemoteTest, PickDeadLatchScratchPicksDefinedDeadTemp) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R3,  Haydn::R4, Haydn::R5,  Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32),
          Haydn::R7)
      .addReg(Haydn::R7)
      .addReg(Haydn::R7);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_EQ(Scr, Register(Haydn::R7))
      << "occupancy 1a: defined dead R7 must win over unused R14";
  EXPECT_TRUE(occupiedAtSetOf(Haydn::R7, Setup.getIterator()))
      << "D1.71r: Latch XOR uses incoming R7; FPL Header→Latch live-through "
         "occupies SET. Latch-end 1a still picks R7 (XOR defines it)";
}

// D1.64 Exclude key: Prefer with a non-countdown body use is refused as
// LatchScr by the caller. Header BNEZ of Prefer is the body use; latch
// BNEZ_W of the same reg is the terminator skip (not XOR of Prefer — that
// would hide an opcode-wide skip). Unused unsaved R14 is D1.87 refuse,
// not pass-1b. Four seats must Exclude Prefer and keep pass-1a R7;
// occupancy miss is not LatchScr=Prefer.
TEST_F(HaydnHWLoopDemoteTest, PickDeadLatchScratchHonorsExcludePrefer) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R3, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32),
          Haydn::R7)
      .addReg(Haydn::R7)
      .addReg(Haydn::R7);
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R2)
      .addMBB(Latch);
  MachineInstr &LatchT = bnez(Haydn::R2, Header);
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(LatchT, Haydn::R2));
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch))
      << "header BNEZ of Prefer is a body use; latch BNEZ_W skip is "
         "latch-block only";
  EXPECT_FALSE(haydn::hwloop::regClobberedNonCountdownIn(Haydn::R2, Blocks))
      << "header zero-test is a use, not a CB-165 clobber";
  Register ExcludePei[] = {Haydn::R7};
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, ExcludePei, "test");
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "excluding mentioned PEI dest r7 must not keep r7 as LatchScr";
  EXPECT_NE(Scr, Register(Haydn::R2))
      << "header-vs-latch PreferHasBodyUse must not become a pass-2 steal";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "D1.87: unsaved unused R14 is not pass-1b after Exclude PEI dest";
  EXPECT_FALSE(Scr.isPhysical())
      << "excluding the only pass-1a dest is occupancy miss, not unsaved R14";
  Register ExcludePrefer[] = {Haydn::R2};
  Register ScrEx = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, ExcludePrefer, "test");
  EXPECT_NE(ScrEx, Register(Haydn::R2))
      << "PickDeadLatchScratchHonorsExcludePrefer: Prefer stays excluded";
  EXPECT_EQ(ScrEx, Register(Haydn::R7))
      << "Exclude Prefer leaves pass-1a dead temp R7";
  EXPECT_NE(ScrEx, Register(Haydn::R14))
      << "defined dead temp wins over unused R14";
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse)
      << "header-vs-latch is load-bearing PreferHasBodyUse; opcode-wide skip "
         "of header BNEZ would make occupancy miss last-resort Prefer";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "PEI dest r7 is not LatchExcl";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "unused R14 stays out of LatchExcl";
  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_EQ(Four, Register(Haydn::R7))
      << "four seats: pass-1a dead temp after PreferHasBodyUse Exclude";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
  EXPECT_NE(Four, Register(Haydn::R14))
      << "four seats must not steal unused R14";
}

// D1.136: Extra dest reads r5. {Header, Exit} would pick r5 as LatchScr
// (dead vs designated exits). The kept Extra successor occupies r5 so
// NoSpill and pickDeadLatchScratch must not steal it. AIE splitLoopEndJump
// (AIEBaseHardwareLoops.cpp:232-272) avoids Extra; Haydn D1.105 keeps it.
TEST_F(HaydnHWLoopDemoteTest, PickDeadLatchScratchProbesKeptExtraSuccessor) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R3, Haydn::R4, Haydn::R6,
      Haydn::R7, Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  MachineBasicBlock *Extra = MF->CreateMachineBasicBlock();
  MF->push_back(Extra);
  Latch->addSuccessor(Extra);
  Preheader->addLiveIn(Haydn::R5);
  Extra->addLiveIn(Haydn::R5);
  BuildMI(*Extra, Extra->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R5)
      .addReg(Haydn::R13)
      .addImm(0);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  (void)Setup;
  auto Blocks = loopBlocks();
  MachineBasicBlock *HeaderExit[] = {Header, Exit};
  Register ScrDrop = haydn::hwloop::pickDeadLatchScratch(
      *Latch, HeaderExit, Blocks, /*Exclude=*/{}, "test");
  EXPECT_EQ(ScrDrop, Register(Haydn::R5))
      << "setup: r5 is dead vs {Header, Exit} so the dropped-extra probe "
         "would steal it";
  SmallVector<MachineBasicBlock *, 4> PostSuccs;
  haydn::hwloop::collectPostRewriteLatchSuccessors(
      *Latch, Header, Exit, /*KeepExtraSuccs=*/true, PostSuccs);
  Register ScrKeep = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_NE(ScrKeep, Register(Haydn::R5))
      << "kept Extra dest read of r5 must occupy LatchScr";
  Register NoSpillDrop = findPostRAScratchNoSpill(
      *Latch, Latch->end(), /*PreferNotR12=*/true, HeaderExit, {});
  EXPECT_EQ(NoSpillDrop, Register(Haydn::R5))
      << "setup: NoSpill vs {Header, Exit} would pick Extra-live r5";
  Register NoSpillKeep = findPostRAScratchNoSpill(
      *Latch, Latch->end(), /*PreferNotR12=*/true, PostSuccs, {});
  EXPECT_NE(NoSpillKeep, Register(Haydn::R5))
      << "NoSpill vs kept Extra must not pick Extra-live r5";
}

// Extra as first Latch outside-pred has no SET. recoverDemoteSetupFrom
// must still take the SET-carrying Preheader so LatchExcl FA stays on.
// Header==Latch: Extra pred is not reverse-walked into LoopBlocks, and
// Extra is the first pred so the old first-outside-pred recover would
// vacate HaveSetup.
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchRecoversSetPreheaderNotExtraPred) {
  Latch->removeSuccessor(Header);
  Header->removeSuccessor(Latch);
  Latch->removeSuccessor(Exit);
  Preheader->removeSuccessor(Header);
  Header->addSuccessor(Header);
  Header->addSuccessor(Exit);
  MachineBasicBlock *Extra = MF->CreateMachineBasicBlock();
  MF->push_back(Extra);
  Extra->addSuccessor(Header);
  Preheader->addSuccessor(Header);
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R7, Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  for (MCPhysReg R : Occ) {
    Header->addLiveIn(R);
    Exit->addLiveIn(R);
    BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::XOR32), R)
        .addReg(R)
        .addReg(R);
    BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
        .addReg(R)
        .addReg(Haydn::R13)
        .addImm(0);
  }
  MachineInstr &Setup = *BuildMI(*Preheader, Preheader->end(), DebugLoc(),
                                 TII().get(Haydn::SET_HWLOOP_F2_W))
                             .addImm(0)
                             .addMBB(Header)
                             .addMBB(Header)
                             .addReg(Haydn::R2)
                             .addReg(Haydn::SFR, RegState::ImplicitDefine);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(0);
  haydn::hwloop::LoopBlockSet Blocks;
  haydn::hwloop::collectLoopBlocks(Header, Header, Preheader, Blocks);
  SmallVector<MachineBasicBlock *, 4> PostSuccs;
  haydn::hwloop::collectPostRewriteLatchSuccessors(
      *Header, Header, Exit, /*KeepExtraSuccs=*/false, PostSuccs);
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Header, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "Extra as Latch outside-pred must not vacate LatchExcl FA r3";
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R3, Preheader, From, Exit, Blocks, TRI))
      << "real Preheader+SET still names r3 as unsound FA";
}

// After 1a/1b miss, an unmentioned live-through with an incoming value
// and an Exit-path read is occupancy miss, not a pass-2 steal. Unused
// R14 is also live at {Header, Exit} with no incoming value — pass 1b
// cannot take it (not dead) and the picker must not steal it.
TEST_F(HaydnHWLoopDemoteTest, PickDeadLatchScratchMissesUnmentionedLiveThrough) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R3,  Haydn::R4, Haydn::R5,  Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  for (MCPhysReg R : Occ) {
    Latch->addLiveIn(R);
    Header->addLiveIn(R);
    Exit->addLiveIn(R);
    BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), R)
        .addReg(R)
        .addReg(R);
    BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
        .addReg(R)
        .addReg(Haydn::R13)
        .addImm(0);
  }
  Preheader->addLiveIn(Haydn::R7);
  Header->addLiveIn(Haydn::R7);
  Latch->addLiveIn(Haydn::R7);
  Exit->addLiveIn(Haydn::R7);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(0);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);
  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "must not steal unmentioned live-through R7";
  EXPECT_FALSE(Scr.isPhysical())
      << "unmentioned live-through is occupancy miss, not a pass-2 steal";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "must not steal unused live R14";
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "addUnmentionedLiveThrough R7 is LatchExcl, not an admitting skip";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "unused R14 stays out of skipLatchScrReason LatchExcl";
}

// Pass 2 must miss rather than steal unused R14 that is live at
// {Header, Exit} with no incoming value. LivePhysRegs/pristines would
// false-positive a CSR as live-into-exit; the helper requires an after-SET
// ADDI r0,imm last-def and an actual Exit-path read.
TEST_F(HaydnHWLoopDemoteTest, PickDeadLatchScratchPass2SkipsUnusedLiveR14) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2,  Haydn::R3,  Haydn::R4, Haydn::R5,  Haydn::R6,
      Haydn::R7, Haydn::R8,  Haydn::R9,  Haydn::R10, Haydn::R11, Haydn::R12};
  for (MCPhysReg R : Occ) {
    Latch->addLiveIn(R);
    Header->addLiveIn(R);
    Exit->addLiveIn(R);
    BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), R)
        .addReg(R)
        .addReg(R);
    BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
        .addReg(R)
        .addReg(Haydn::R13)
        .addImm(0);
  }
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "unused R14 is not live-into-exit; LivePhysRegs/pristines would "
         "false-positive a CSR";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "unused R14 is not FA";
  const bool R14Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  const bool R14Def = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  EXPECT_FALSE(R14Use);
  EXPECT_FALSE(R14Def);
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            R14Use || (livePhysAtSetOf(Haydn::R14, Setup.getIterator()) &&
                       !R14Def))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail); "
         "do not fold unused R14 into live-into-exit";
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "must not steal unused live R14";
  EXPECT_FALSE(Scr.isPhysical())
      << "occupied latch window must miss rather than steal unused R14";

  // Caller LatchExcl of FA ∪ live-into-exit must not swallow unused R14.
  SmallVector<Register, 8> Excl;
  if (haydn::hwloop::regIsPreheaderTailFrameAddress(Haydn::R14, Preheader, From,
                                                    TRI) ||
      haydn::hwloop::regIsLiveIntoExitTailImm(Haydn::R14, Preheader, From, Exit,
                                             TRI))
    Excl.push_back(Haydn::R14);
  EXPECT_TRUE(Excl.empty())
      << "unused R14 must not enter LatchExcl as FA or live-into-exit";
  Register ScrEx = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Excl, "test");
  EXPECT_FALSE(ScrEx.isPhysical())
      << "Exclude of unused R14 is a no-op; pass-2 still misses";

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse)
      << "latch XOR of occupied Prefer is a non-countdown body use";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "unused R14 stays out of skipLatchScrReason LatchExcl";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R14, Preheader, From, TRI) ||
                (livePhysAtSetOf(Haydn::R14, Setup.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader,
                                                        From, TRI)))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_FALSE(Four.isPhysical())
      << "four seats: occupied window is occupancy miss, not unused R14";
  EXPECT_NE(Four, Register(Haydn::R14))
      << "four seats must not steal unused R14";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
}

// Pass 2 must not steal unmentioned live-through r3 after NoSpill
// exhausts, even with empty Exclude. r3=sp+off lives in the tail, so the
// skip is the recovered LatchExcl predicate, not a LoopBlocks mention.
// Next unmentioned live-through is occupancy miss, not a steal.
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchPass2SkipsUnmentionedFrameAddress) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  for (MCPhysReg R : Occ) {
    Latch->addLiveIn(R);
    Header->addLiveIn(R);
    Exit->addLiveIn(R);
    BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), R)
        .addReg(R)
        .addReg(R);
    BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
        .addReg(R)
        .addReg(Haydn::R13)
        .addImm(0);
  }
  auto addLiveThrough = [&](MCPhysReg R) {
    Preheader->addLiveIn(R);
    Header->addLiveIn(R);
    Latch->addLiveIn(R);
    Exit->addLiveIn(R);
    BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
        .addReg(R)
        .addReg(Haydn::R13)
        .addImm(0);
  };
  addLiveThrough(Haydn::R3);
  addLiveThrough(Haydn::R7);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);

  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  SmallVector<Register, 8> Excl;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Excl, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal unmentioned Exit-read FA r3";
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "must not steal the next unmentioned live-through after FA skip";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of FA r3 is occupancy miss, not a pass-2 steal";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "must not steal unused live R14 after skipping r3";

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "r3=sp+off is the va-arg-22 LatchScr skip";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI))
      << "unmentioned live-through r7 is not a frame address";
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "addUnmentionedLiveThrough R7 is LatchExcl after FA skip";
}

// Same skip when r3=sp+off is a reaching def BEFORE SET (Ins is SET).
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchPass2SkipsReachingFrameAddressBeforeSet) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  for (MCPhysReg R : Occ) {
    Latch->addLiveIn(R);
    Header->addLiveIn(R);
    Exit->addLiveIn(R);
    BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), R)
        .addReg(R)
        .addReg(R);
    BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
        .addReg(R)
        .addReg(Haydn::R13)
        .addImm(0);
  }
  auto addLiveThrough = [&](MCPhysReg R) {
    Preheader->addLiveIn(R);
    Header->addLiveIn(R);
    Latch->addLiveIn(R);
    Exit->addLiveIn(R);
    BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
        .addReg(R)
        .addReg(Haydn::R13)
        .addImm(0);
  };
  addLiveThrough(Haydn::R3);
  addLiveThrough(Haydn::R7);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);

  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  MachineInstr &Setup =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(),
               TII().get(Haydn::SET_HWLOOP_F2_W))
           .addImm(0)
           .addMBB(Header)
           .addMBB(Latch)
           .addReg(Haydn::R2)
           .addReg(Haydn::SFR, RegState::ImplicitDefine);

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  SmallVector<Register, 8> Excl;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Excl, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal unmentioned FA r3 defined before SET";
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "must not steal the next unmentioned live-through after FA skip";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of FA r3 is occupancy miss, not a pass-2 steal";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "must not steal unused live R14 after skipping r3";

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI));
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "addUnmentionedLiveThrough R7 is LatchExcl after reaching-FA skip";
}

// va-arg-22: a GPR dead at the SET iterator can still be the address
// scratch in the preheader tail (SET is not a scheduling boundary).
// instrs() walk, skip the setup MI; a later ADDI of R7 is a mention.
TEST_F(HaydnHWLoopDemoteTest, PreheaderTailMentionAfterSetupIsVisible) {
  MachineInstr &Setup =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::NOP));
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R7)
      .addImm(4);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup);
  EXPECT_TRUE(haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R7, Preheader, From, TRI))
      << "tail ADDI of R7 after SET must refuse LatchScr→PreheaderScr copy";
  EXPECT_FALSE(haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R8, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R7, Preheader, Preheader->end(), TRI))
      << "empty tail is not a mention";
}

// va-arg-22 LatchScr skip is the frame-address materialize, not every
// tail mention. r3 = ADDI32_W r13, 16 is the address scratch; r14 = ADDI
// r0, 2 is an SMS guard dest and must remain a sound latch temp.
TEST_F(HaydnHWLoopDemoteTest, PreheaderTailFrameAddressIsSpAddi) {
  MachineInstr &Setup =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::NOP));
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup);
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "r3=sp+off must be refused as LatchScr";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "r14=ADDI r0,2 is not a frame address scratch";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI));
  auto Blocks = loopBlocks();
  // va-arg-22 CountReg class: pickCounterReg would copy the trip into r3
  // (dead at SET, unmentioned in the body) without this refusal.
  EXPECT_FALSE(haydn::hwloop::isSoundDemoteCounter(
      Haydn::R3, Blocks, Preheader, From, *MF, TRI))
      << "r3=sp+off in the preheader tail is not a sound countdown";
  EXPECT_TRUE(haydn::hwloop::isSoundDemoteCounter(
      Haydn::R7, Blocks, Preheader, From, *MF, TRI))
      << "untouched caller-saved R7 stays a sound countdown";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "SMS-guard ADDI r0,2 with no Exit use is not live-into-exit";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R3, Preheader, From, Exit, TRI))
      << "r3=sp+off is FA, not ADDI r0,imm";
}

// SMS-guard ADDI r0,imm after SET with no Exit-path read is a sound
// pass-1a latch temp (LD32 defines it). Caller LatchExcl of FA ∪
// live-into-exit must not swallow it. Occupancy is DefInTail so overlay
// ST is refused; do not restamp occupiedAtSet.
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchPicksSmsGuardAddiWithNoExitRead) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2,  Haydn::R3,  Haydn::R4, Haydn::R5,  Haydn::R6,
      Haydn::R7, Haydn::R8,  Haydn::R9,  Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "SMS-guard ADDI r0,imm with no Exit-path read is not live-into-exit";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "r14=ADDI r0,imm is not FA";
  const bool R14Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  const bool R14Def = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  EXPECT_TRUE(R14Def) << "SMS-guard ADDI is DefInTail";
  EXPECT_FALSE(R14Use);
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "DefInTail kills occupancy; overlay ST of SMS-guard is refused";
  EXPECT_NE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            haydn::hwloop::regMentionedInPreheaderTail(Haydn::R14, Preheader,
                                                       From, TRI))
      << "occupancy is not any-mention; do not restamp occupiedAtSet";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            R14Use || (livePhysAtSetOf(Haydn::R14, Setup.getIterator()) &&
                       !R14Def))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  SmallVector<Register, 8> Excl;
  if (haydn::hwloop::regIsPreheaderTailFrameAddress(Haydn::R14, Preheader, From,
                                                    TRI) ||
      haydn::hwloop::regIsLiveIntoExitTailImm(Haydn::R14, Preheader, From, Exit,
                                             TRI))
    Excl.push_back(Haydn::R14);
  EXPECT_TRUE(Excl.empty())
      << "SMS-guard must not enter LatchExcl as FA or live-into-exit";
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Excl, "test");
  EXPECT_EQ(Scr, Register(Haydn::R14))
      << "SMS-guard ADDI with no Exit-path read is a sound pass-1a latch temp";
}

// va-arg-22 remaining steal: last def ADDI32 r0,imm AFTER SET, live into
// Exit. Not FA. Occupancy is DefInTail so overlay will not save it.
TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmAfterSet) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  MachineInstr &TailImm =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
               Haydn::R14)
           .addReg(Haydn::R0)
           .addImm(2);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "r14=ADDI r0,imm after SET live into Exit is the remaining steal";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "keep FA last-def: r0,imm is not sp+off";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI));
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(TailImm, Haydn::R14))
      << "do not fold tail-imm into isCountdownStepOf";
  auto Blocks = loopBlocks();
  EXPECT_FALSE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R14, Blocks, Latch))
      << "keep body-read: Exit use is not a loop-body use";
}

TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmAdDi32W) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(16);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "ADDI32_W r0,imm after SET is the same class as ADDI32";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI));
}

TEST_F(HaydnHWLoopDemoteTest, TailImmBeforeSetIsNotThisClass) {
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "ADDI r0,imm before SET is not the after-SET tail class";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI));
}

TEST_F(HaydnHWLoopDemoteTest, TailImmLaterNonAddiRedefIsNotThisClass) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::XOR32),
          Haydn::R14)
      .addReg(Haydn::R14)
      .addReg(Haydn::R14);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "last def XOR is not ADDI r0,imm";
}

TEST_F(HaydnHWLoopDemoteTest, TailImmKilledBeforeHeaderIsNotThisClass) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(TargetOpcode::KILL))
      .addReg(Haydn::R14, RegState::Kill);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "killed tail-imm is a pass-1a dead temp, not live-into-exit";
}

TEST_F(HaydnHWLoopDemoteTest, TailImmFromNonR0IsNotThisClass) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R7)
      .addImm(4);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI))
      << "ADDI rd,rd,imm is not r0,imm";
}

// va-arg-22 remaining steal after the ADDI-only helper: last def is
// MOVE/COPY of an after-SET ADDI r0,imm, live into Exit. Same LatchScr
// steal as the ADDI (DefInTail, no overlay). Keep last-def after SET.
TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmCopyOfAfterSetAddi) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R14)
      .addReg(Haydn::R7);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "r14=MOVE of after-SET ADDI r0,imm live into Exit is the steal";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI))
      << "copy-chain: ADDI dest is unsound when the MOVE dest is live into Exit";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "keep FA last-def: MOVE of r0,imm is not sp+off";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI));
  auto Blocks = loopBlocks();
  EXPECT_FALSE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R14, Blocks, Latch))
      << "keep body-read: Exit use is not a loop-body use";
}

TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmCopyOpcode) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(TargetOpcode::COPY),
          Haydn::R14)
      .addReg(Haydn::R7);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "COPY of after-SET ADDI r0,imm is the same class as MOVE32";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI));
}

TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmKilledCopySource) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R14)
      .addReg(Haydn::R7, RegState::Kill);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "MOVE of killed after-SET ADDI dest is still the steal";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI))
      << "COPY/MOVE kill of the ADDI dest must not hide it from the family";
}

TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmCopyNoExitUseIsSoundTemp) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R14)
      .addReg(Haydn::R7);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "MOVE of SMS-guard ADDI with no Exit-path read stays a latch temp";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI));
}

TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmCopyOfBeforeSetAddiIsNotThisClass) {
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R0)
      .addImm(2);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R14)
      .addReg(Haydn::R7);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "keep last-def after SET: MOVE of pre-SET ADDI is not this class";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI));
}

TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmCopyOfFaIsNotThisClass) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R14)
      .addReg(Haydn::R3);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "MOVE of r3=sp+off is FA, not tail-imm";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R3, Preheader, From, Exit, TRI))
      << "r3=sp+off stays FA";
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "keep FA last-def: r3=sp+off";
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "keep FA: MOVE of sp+off is the same value";
}

// After FA skip of r3=sp+off, pass-2 must not keep unmentioned r14=ADDI
// r0,imm live into Exit, including empty Exclude. Occupancy miss after
// the skip is fail-closed, not a steal.
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchPass2SkipsLiveIntoExitTailImm) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R7, Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  SmallVector<Register, 8> Excl;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Excl, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal unmentioned Exit-read FA r3";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "empty Exclude must not steal live-into-exit tail-imm r14";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of FA ∪ tail-imm is occupancy miss, not a steal";

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R3, Preheader, From, Exit, TRI))
      << "keep FA: r3=sp+off is not tail-imm";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "keep FA: r14=ADDI r0,imm is not sp+off";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI));
  EXPECT_FALSE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R14, Blocks, Latch))
      << "keep body-read: r14 is unmentioned in LoopBlocks";

  const bool R14Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  const bool R14Def = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  EXPECT_TRUE(R14Def) << "after-SET ADDI r0,imm is DefInTail";
  EXPECT_FALSE(R14Use);
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "DefInTail kills occupancy; overlay ST would not save the steal";
  EXPECT_NE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            haydn::hwloop::regMentionedInPreheaderTail(Haydn::R14, Preheader,
                                                       From, TRI))
      << "occupancy is not any-mention; do not restamp occupiedAtSet";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            R14Use || (livePhysAtSetOf(Haydn::R14, Setup.getIterator()) &&
                       !R14Def))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";

  // Once Exclude is populated, skip FA ∪ tail-imm in the picker. Exclude
  // r3 alone must not keep live-into-exit r14 (occupancy miss, not steal).
  SmallVector<Register, 8> FaOnly;
  FaOnly.push_back(Haydn::R3);
  Register AfterFa = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, FaOnly, "test");
  EXPECT_NE(AfterFa, Register(Haydn::R3))
      << "populated Exclude must not keep Exit-read FA r3";
  EXPECT_NE(AfterFa, Register(Haydn::R14))
      << "populated Exclude must not keep live-into-exit r14";
  EXPECT_FALSE(AfterFa.isPhysical())
      << "FA ∪ tail-imm skip is occupancy miss, not a steal";

  while (Scr.isPhysical() &&
         (haydn::hwloop::regIsPreheaderTailFrameAddress(
              Scr.asMCReg(), Preheader, From, TRI) ||
          haydn::hwloop::regIsLiveIntoExitTailImm(Scr.asMCReg(), Preheader,
                                                 From, Exit, TRI))) {
    Excl.push_back(Scr);
    Scr = haydn::hwloop::pickDeadLatchScratch(*Latch, PostSuccs, Blocks, Excl,
                                             "test");
  }
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "FA skip must not keep r3=sp+off as LatchScr";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "tail-imm skip must not keep live-into-exit r14 as LatchScr";
  EXPECT_FALSE(Scr.isPhysical())
      << "after FA and tail-imm skips the window misses; occupancy miss";

  // Helper-green is not enough: four seats must Exclude FA r3 and tail-imm
  // r14 and refuse last-resort Prefer. Occupancy miss is not Prefer.
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R2)
      .addMBB(Latch);
  bnez(Haydn::R2, Header);
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse)
      << "header BNEZ / occupied Prefer XOR is PreferHasBodyUse; latch "
         "BNEZ_W skip is latch-block only";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_FALSE(Four.isPhysical())
      << "four seats: FA ∪ tail-imm is occupancy miss, not last-resort Prefer";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
  EXPECT_NE(Four, Register(Haydn::R3))
      << "pass-2 must not steal Exit-read FA r3";
  EXPECT_NE(Four, Register(Haydn::R14))
      << "pass-2 must not steal live-into-exit tail-imm r14";
}

// Same skip when last def is MOVE of the after-SET ADDI r0,imm.
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchPass2SkipsCopyOfLiveIntoExitTailImm) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R7, Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R14)
      .addReg(Haydn::R7);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  SmallVector<Register, 8> Excl;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Excl, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal unmentioned Exit-read FA r3";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "empty Exclude must not steal MOVE-of-tail-imm r14";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of FA ∪ copy-of-tail-imm is occupancy miss";

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R3, Preheader, From, Exit, TRI))
      << "keep FA: r3=sp+off is not tail-imm";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "keep FA: r14=MOVE of ADDI r0,imm is not sp+off";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "MOVE of after-SET ADDI r0,imm live into Exit is the steal";
  EXPECT_FALSE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R14, Blocks, Latch))
      << "keep body-read: r14 is unmentioned in LoopBlocks";

  while (Scr.isPhysical() &&
         (haydn::hwloop::regIsPreheaderTailFrameAddress(
              Scr.asMCReg(), Preheader, From, TRI) ||
          haydn::hwloop::regIsLiveIntoExitTailImm(Scr.asMCReg(), Preheader,
                                                 From, Exit, TRI))) {
    Excl.push_back(Scr);
    Scr = haydn::hwloop::pickDeadLatchScratch(*Latch, PostSuccs, Blocks, Excl,
                                             "test");
  }
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "FA skip must not keep r3=sp+off as LatchScr";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "copy-of-tail-imm skip must not keep live-into-exit r14 as LatchScr";
  EXPECT_FALSE(Scr.isPhysical())
      << "after FA and copy-of-tail-imm skips the window misses";
}

// copy.mir analog: r7 is the unmentioned MOVE dest of after-SET ADDI r0,imm
// and is live into Exit. Occupying r7 hid this steal (the existing copy-of
// test occupies r7 and only skips r14). Once Exclude is populated, the
// picker skips the whole copy-chain so FA-only Exclude cannot keep r7.
// Occupancy is DefInTail so overlay will not save; do not restamp
// occupiedAtSet. Unused R14 is the ADDI dest here, not a pass-2 unused-CSR
// steal.
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchPass2SkipsUnmentionedMoveDestOfTailImm) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  addUnmentionedLiveThrough(Haydn::R7);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R7)
      .addReg(Haydn::R14);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  SmallVector<Register, 8> Excl;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Excl, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal unmentioned Exit-read FA r3";
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "empty Exclude must not steal unmentioned MOVE dest of tail-imm";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "empty Exclude must not steal live-into-exit tail-imm r14";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of FA ∪ tail-imm copy-chain is occupancy miss";

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R3, Preheader, From, Exit, TRI))
      << "FA vs tail-imm: r3=sp+off is not ADDI r0,imm";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI))
      << "FA vs tail-imm: MOVE dest of r0,imm is not sp+off";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "FA vs tail-imm: r14=ADDI r0,imm is not sp+off";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "ADDI dest live into Exit is tail-imm";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI))
      << "unmentioned MOVE dest of that ADDI is the same steal";
  EXPECT_FALSE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R7, Blocks, Latch))
      << "keep body-read: Exit use is not a loop-body use";

  const bool R7Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R7, Preheader, From, TRI);
  const bool R7Def = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R7, Preheader, From, TRI);
  EXPECT_TRUE(R7Def) << "MOVE dest is DefInTail";
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R7, Setup.getIterator()))
      << "DefInTail kills occupancy; overlay ST would not save the steal";
  EXPECT_NE(occupiedAtSetOf(Haydn::R7, Setup.getIterator()),
            haydn::hwloop::regMentionedInPreheaderTail(Haydn::R7, Preheader,
                                                       From, TRI))
      << "occupancy is not any-mention; do not restamp occupiedAtSet";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R7, Setup.getIterator()),
            R7Use || (livePhysAtSetOf(Haydn::R7, Setup.getIterator()) &&
                      !R7Def))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";

  SmallVector<Register, 8> FaOnly;
  FaOnly.push_back(Haydn::R3);
  Register AfterFa = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, FaOnly, "test");
  EXPECT_NE(AfterFa, Register(Haydn::R3))
      << "populated Exclude must not keep Exit-read FA r3";
  EXPECT_NE(AfterFa, Register(Haydn::R7))
      << "populated Exclude must not keep unmentioned MOVE dest r7";
  EXPECT_NE(AfterFa, Register(Haydn::R14))
      << "populated Exclude must not keep live-into-exit r14";
  EXPECT_FALSE(AfterFa.isPhysical())
      << "FA ∪ tail-imm copy-chain skip is occupancy miss, not a steal";

  SmallVector<Register, 8> FaAndAddi;
  FaAndAddi.push_back(Haydn::R3);
  FaAndAddi.push_back(Haydn::R14);
  Register AfterFaAddi = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, FaAndAddi, "test");
  EXPECT_NE(AfterFaAddi, Register(Haydn::R7))
      << "populated Exclude must not keep COPY/MOVE dest r7";
  EXPECT_FALSE(AfterFaAddi.isPhysical())
      << "FA+ADDI Exclude still skips the MOVE dest via the copy-chain";

  while (Scr.isPhysical() &&
         (haydn::hwloop::regIsPreheaderTailFrameAddress(
              Scr.asMCReg(), Preheader, From, TRI) ||
          haydn::hwloop::regIsLiveIntoExitTailImm(Scr.asMCReg(), Preheader,
                                                 From, Exit, TRI))) {
    Excl.push_back(Scr);
    Scr = haydn::hwloop::pickDeadLatchScratch(*Latch, PostSuccs, Blocks, Excl,
                                             "test");
  }
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "FA skip must not keep r3=sp+off as LatchScr";
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "copy-dest skip must not keep live-into-exit r7 as LatchScr";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "tail-imm skip must not keep live-into-exit r14 as LatchScr";
  EXPECT_FALSE(Scr.isPhysical())
      << "after FA and copy-dest skips the window misses; occupancy miss";
}

// copyopc.mir analog: generic COPY dest, not MOVE32. A MOVE-only dest skip
// stays green while COPY of the ADDI dest still MEMORY_FAULTs.
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchPass2SkipsUnmentionedCopyDestOfTailImm) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  addUnmentionedLiveThrough(Haydn::R7);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(TargetOpcode::COPY),
          Haydn::R7)
      .addReg(Haydn::R14);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  SmallVector<Register, 8> Excl;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Excl, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal unmentioned Exit-read FA r3";
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "empty Exclude must not steal unmentioned COPY dest of tail-imm";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "empty Exclude must not steal live-into-exit tail-imm r14";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of FA ∪ COPY dest is occupancy miss";

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R3, Preheader, From, Exit, TRI))
      << "FA vs tail-imm: r3=sp+off is not ADDI r0,imm";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI))
      << "FA vs tail-imm: COPY dest of r0,imm is not sp+off";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI))
      << "unmentioned COPY dest of after-SET ADDI is the steal";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI));

  SmallVector<Register, 8> FaAndAddi;
  FaAndAddi.push_back(Haydn::R3);
  FaAndAddi.push_back(Haydn::R14);
  Register AfterFaAddi = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, FaAndAddi, "test");
  EXPECT_NE(AfterFaAddi, Register(Haydn::R7))
      << "populated Exclude must not keep generic COPY dest r7";
  EXPECT_NE(AfterFaAddi, Register(Haydn::R14))
      << "populated Exclude must not keep live-into-exit r14";
  EXPECT_FALSE(AfterFaAddi.isPhysical())
      << "FA+ADDI Exclude still skips the COPY dest via the copy-chain";

  while (Scr.isPhysical() &&
         (haydn::hwloop::regIsPreheaderTailFrameAddress(
              Scr.asMCReg(), Preheader, From, TRI) ||
          haydn::hwloop::regIsLiveIntoExitTailImm(Scr.asMCReg(), Preheader,
                                                 From, Exit, TRI))) {
    Excl.push_back(Scr);
    Scr = haydn::hwloop::pickDeadLatchScratch(*Latch, PostSuccs, Blocks, Excl,
                                             "test");
  }
  EXPECT_NE(Scr, Register(Haydn::R3));
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "COPY dest skip must not keep live-into-exit r7 as LatchScr";
  EXPECT_NE(Scr, Register(Haydn::R14));
  EXPECT_FALSE(Scr.isPhysical())
      << "after FA and COPY-dest skips the window misses";
}

// CImm ADDI r0,imm after SET is the same tail-imm class as isImm. An
// isImm-only last-def miss lets pass-2 keep r14.
TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmCImmAfterSet) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  MachineInstr &TailImm =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
               Haydn::R14)
           .addReg(Haydn::R0)
           .addCImm(ConstantInt::get(*Ctx, APInt(32, 2)));
  ASSERT_TRUE(TailImm.getOperand(2).isCImm());
  ASSERT_FALSE(TailImm.getOperand(2).isImm());
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "CImm ADDI r0,imm after SET live into Exit is tail-imm";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "keep FA: r0,imm is not sp+off";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, loopBlocks(), TRI));
  EXPECT_TRUE(haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader, From,
                                                    TRI));
  EXPECT_FALSE(haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, From, TRI));
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "occupancy stays UseFromSet/DefInTail; CImm ADDI is DefInTail";
}

// Bundled-SET last-def rewind: glueDefToUse places SET last, so the
// coissued ADDI r0,imm is a bundle member before SET. Production Ins is
// the BUNDLE root (topLevelForLayout). std::next(BUNDLE) skips the dest
// and pass-2 keeps r14.
TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmBundledSetLastDefRewind) {
  MachineInstr *Bund =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *Addi =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::ADDI32), Haydn::R14)
          .addReg(Haydn::R0)
          .addImm(2)
          .getInstr();
  Addi->bundleWithPred();
  MachineInstr *Setup =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::SET_HWLOOP_F2_W))
          .addImm(0)
          .addMBB(Header)
          .addMBB(Latch)
          .addReg(Haydn::R2)
          .addReg(Haydn::SFR, RegState::ImplicitDefine)
          .getInstr();
  Setup->bundleWithPred();
  finalizeBundle(*Preheader, Bund->getIterator());
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineInstr &Root = haydn::hwloop::topLevelForLayout(*Setup);
  ASSERT_TRUE(Root.isBundle());
  MachineBasicBlock::const_iterator From(Root.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "coissued ADDI r0,imm must be visible when Ins is BUNDLE root";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI));
  auto AfterBund = std::next(From);
  if (AfterBund == Preheader->end()) {
    EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
        Haydn::R14, Preheader, AfterBund, Exit, TRI))
        << "From==end is empty tail; rewind must start at BUNDLE not next";
  } else {
    EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
        Haydn::R14, Preheader, AfterBund, Exit, TRI))
        << "std::next(BUNDLE) skips the coissued last-def";
  }
}

// After-SET ADDI is the last-def even when From already sits on it
// (PastSetup pointer match on occupancy's walker would skip it).
TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmFromSitsOnAfterSetAddi) {
  setHwLoopAtPreheaderEnd();
  MachineInstr &TailImm =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
               Haydn::R14)
           .addReg(Haydn::R0)
           .addImm(2);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(TailImm.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "From sitting on the after-SET ADDI must still see that last-def";
}

// Exit-path read on the BUNDLE header only (implicits the member list
// omits). Top-level walks that skip isBundle() miss r14.
TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmExitBundleHeaderUse) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  MachineInstr *Bund =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *Nop =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::NOP)).getInstr();
  Nop->bundleWithPred();
  finalizeBundle(*Exit, Bund->getIterator());
  MachineInstr &Root = *Exit->begin();
  ASSERT_TRUE(Root.isBundle());
  Root.addOperand(
      *MF, MachineOperand::CreateReg(Haydn::R14, /*isDef=*/false, /*isImp=*/true));
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "cfgPathReadsPhysReg must inspect BUNDLE header uses";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI));
}

// Exit-path read is a bundled member, not a top-level MI. instrs() must
// visit interiors; a top-level iterator walk misses the use when the
// BUNDLE header has not copied it.
TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmExitBundledMemberUse) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  MachineInstr *Bund =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *St =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
          .addReg(Haydn::R14)
          .addReg(Haydn::R13)
          .addImm(0)
          .getInstr();
  St->bundleWithPred();
  (void)Bund;
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "cfgPathReadsPhysReg must see bundled member uses";
}

// Unmentioned live-through with an Exit ST32 is LatchExcl: skipLatchScrReasonOf
// must not return nullptr. Not FA, not tail-imm. Mentioned PEI dest and
// SMS-guard ADDI with no Exit use stay pass-1a. Unused R14 stays out.
TEST_F(HaydnHWLoopDemoteTest,
       SkipLatchScrReasonOfUnmentionedLiveThroughIsExitPath) {
  occupyLatchWindow(ArrayRef<MCPhysReg>(
      {Haydn::R1, Haydn::R2, Haydn::R3, Haydn::R4, Haydn::R5, Haydn::R6,
       Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12}));
  addUnmentionedLiveThrough(Haydn::R7);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), Haydn::R8)
      .addReg(Haydn::R8)
      .addReg(Haydn::R8);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(1);
  auto Blocks = loopBlocks();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_FALSE(haydn::hwloop::regMentionedInBlocks(Haydn::R7, Blocks));
  EXPECT_TRUE(haydn::hwloop::hasIncomingValue(Haydn::R7, *MF));
  EXPECT_TRUE(exitPathReadsIncomingOf(Haydn::R7));
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, *ST->getRegisterInfo()));
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, *ST->getRegisterInfo()));
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "addUnmentionedLiveThrough R7 must not admit LatchExcl";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R8, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "mentioned PEI dest stays pass-1a";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "SMS-guard ADDI with no Exit use stays pass-1a";
}

// WAR/snapshot bundle Exit: member ST32 reads incoming R7, later member
// ADDI defs R7. finalizeBundle aggregates header use+def. Header Defs must
// not hide the member read (HaydnIntraCycleRAW.h:218-219). Twin
// skipLatchScrReasonOf grows the same unproven-absent Exit-path arm.
TEST_F(HaydnHWLoopDemoteTest, SkipLatchScrReasonOfWarBundleExitReadThenDef) {
  Preheader->addLiveIn(Haydn::R7);
  Header->addLiveIn(Haydn::R7);
  Latch->addLiveIn(Haydn::R7);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  MachineInstr *Bund =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *St =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
          .addReg(Haydn::R7)
          .addReg(Haydn::R13)
          .addImm(0)
          .getInstr();
  St->bundleWithPred();
  MachineInstr *Def =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ADDI32),
              Haydn::R7)
          .addReg(Haydn::R0)
          .addImm(1)
          .getInstr();
  Def->bundleWithPred();
  finalizeBundle(*Exit, Bund->getIterator());
  MachineInstr &Root = *Exit->begin();
  ASSERT_TRUE(Root.isBundle());
  auto Blocks = loopBlocks();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_FALSE(haydn::hwloop::regMentionedInBlocks(Haydn::R7, Blocks));
  EXPECT_TRUE(haydn::hwloop::hasIncomingValue(Haydn::R7, *MF));
  EXPECT_TRUE(exitPathReadsIncomingOf(Haydn::R7))
      << "WAR/snapshot member read-then-def is an incoming Exit-path read";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, *ST->getRegisterInfo()))
      << "WAR live-through is not after-SET ADDI r0,imm";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, *ST->getRegisterInfo()));
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "BUNDLE header aggregated Defs must not hide WAR Exit read-then-def";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "unused R14 stays out of LatchExcl";
}

// WAR/snapshot bundle Exit, header-only: finalizeBundle then implicit
// header use of R7 plus later member ADDI def of R7, no member readsReg.
// Header Reads&&Defs (ExternUse + aggregated Def) is the incoming read
// (HaydnIntraCycleRAW.h:218-219). Member Reads&&!Defs is already covered
// by SkipLatchScrReasonOfWarBundleExitReadThenDef.
TEST_F(HaydnHWLoopDemoteTest, SkipLatchScrReasonOfWarBundleHeaderUseThenDef) {
  Preheader->addLiveIn(Haydn::R7);
  Header->addLiveIn(Haydn::R7);
  Latch->addLiveIn(Haydn::R7);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  MachineInstr *Bund =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *Nop =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::NOP)).getInstr();
  Nop->bundleWithPred();
  MachineInstr *Def =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ADDI32),
              Haydn::R7)
          .addReg(Haydn::R0)
          .addImm(1)
          .getInstr();
  Def->bundleWithPred();
  finalizeBundle(*Exit, Bund->getIterator());
  MachineInstr &Root = *Exit->begin();
  ASSERT_TRUE(Root.isBundle());
  Root.addOperand(
      *MF, MachineOperand::CreateReg(Haydn::R7, /*isDef=*/false, /*isImp=*/true));
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  bool HeaderReads = false;
  bool HeaderDefs = false;
  for (const MachineOperand &MO : Root.operands()) {
    if (!MO.isReg() || !MO.getReg().isPhysical() ||
        !TRI.regsOverlap(MO.getReg(), Haydn::R7))
      continue;
    if (MO.readsReg())
      HeaderReads = true;
    if (MO.isDef())
      HeaderDefs = true;
  }
  EXPECT_TRUE(HeaderReads && HeaderDefs)
      << "red shape is BUNDLE-header Reads&&Defs of R7";
  for (const MachineInstr &MI : Exit->instrs()) {
    if (MI.isBundle())
      continue;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.getReg().isPhysical() ||
          !TRI.regsOverlap(MO.getReg(), Haydn::R7))
        continue;
      EXPECT_FALSE(MO.readsReg())
          << "member must not readsReg R7; header Reads&&Defs is the red case";
    }
  }
  auto Blocks = loopBlocks();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_FALSE(haydn::hwloop::regMentionedInBlocks(Haydn::R7, Blocks));
  EXPECT_TRUE(haydn::hwloop::hasIncomingValue(Haydn::R7, *MF));
  EXPECT_TRUE(exitPathReadsIncomingOf(Haydn::R7))
      << "BUNDLE-header ExternUse is an incoming Exit-path read even with "
         "aggregated Defs";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI))
      << "WAR live-through is not after-SET ADDI r0,imm";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI));
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "BUNDLE header Reads&&Defs must report WAR incoming, not continue";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "unused R14 stays out of LatchExcl";
}

// Trampoline Exit: empty stored live-ins, successor ST32 of a GPR dead at
// latch end vs {Header, Exit}. one-block LivePhysRegs misses the successor
// read; skipLatchScrReasonOf must still Exclude it. Mentioned PEI dest and
// SMS-guard ADDI with no Exit use stay pass-1a.
TEST_F(HaydnHWLoopDemoteTest,
       SkipLatchScrReasonOfTrampolineExitSuccessorRead) {
  MachineBasicBlock *RealExit = MF->CreateMachineBasicBlock();
  MF->push_back(RealExit);
  Exit->addSuccessor(RealExit);
  Preheader->addLiveIn(Haydn::R7);
  Header->addLiveIn(Haydn::R7);
  Latch->addLiveIn(Haydn::R7);
  BuildMI(*RealExit, RealExit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(0);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), Haydn::R8)
      .addReg(Haydn::R8)
      .addReg(Haydn::R8);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(1);
  auto Blocks = loopBlocks();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_FALSE(haydn::hwloop::regMentionedInBlocks(Haydn::R7, Blocks));
  EXPECT_TRUE(exitPathReadsIncomingOf(Haydn::R7))
      << "trampoline Exit successor ST32 is an Exit-path read";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "empty stored live-ins on trampoline Exit must not admit LatchExcl";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R8, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "mentioned PEI dest stays pass-1a";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "SMS-guard ADDI with no Exit use stays pass-1a";
}

// Header redef of r14 is a LoopBlocks mention / Exit-walk kill of the
// ADDI dest (Exit reads the COPY dest). Copy-chain still reports r14.
// Occupancy stays UseFromSet/DefInTail.
TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmHeaderRedefDoesNotHideCopyDest) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R7)
      .addReg(Haydn::R14);
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::XOR32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addReg(Haydn::R0);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  auto Blocks = loopBlocks();
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R14, Blocks))
      << "header redef of r14 is a LoopBlocks mention";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "header redef must not hide ADDI dest of a live-into-exit COPY dest";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI))
      << "header redef of r14 must not hide COPY dest of after-SET ADDI";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI))
      << "mentioned ADDI dest stays in the live-into-exit copy-chain";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R7, Preheader, From, Exit, Blocks, TRI));
  EXPECT_TRUE(haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader, From,
                                                    TRI));
  EXPECT_FALSE(haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, From, TRI));
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "occupancy stays UseFromSet/DefInTail; do not restamp occupiedAtSet";
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R2)
      .addMBB(Latch);
  bnez(Haydn::R2, Header);
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse)
      << "header BNEZ of Prefer is a body use; opcode-wide skip would hide "
         "header-hide PreferHasBodyUse";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)")
      << "header redef must not hide ADDI dest in skipLatchScrReason";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";
}

// va-arg-22 O2: after SET, $r14=ADDI r0,imm then COPY to $r4 (kill),
// header $r14=COPY $r4, Exit uses r14. COPY-kill plus header redef must
// not hide r14 from live-into-exit / unsound. Occupancy stays DefInTail.
TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmHeaderCopyFromCopyDest) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  MachineInstr &TailImm =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
               Haydn::R14)
           .addReg(Haydn::R0)
           .addImm(4);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(TargetOpcode::COPY),
          Haydn::R4)
      .addReg(Haydn::R14, RegState::Kill);
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(TargetOpcode::COPY),
          Haydn::R14)
      .addReg(Haydn::R4);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  auto Blocks = loopBlocks();
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R14, Blocks))
      << "header $r14=COPY $r4 is a LoopBlocks mention";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "COPY-kill plus header redef must not hide ADDI dest r14";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R4, Preheader, From, Exit, TRI))
      << "COPY dest of after-SET ADDI is in the live-into-exit family";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "keep FA: r14=ADDI r0,imm is not sp+off";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI))
      << "header redef mention must not hide unsound LatchScr r14";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R4, Preheader, From, Exit, Blocks, TRI));
  EXPECT_TRUE(haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader, From,
                                                    TRI));
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(TailImm, Haydn::R14))
      << "do not fold tail-imm into isCountdownStepOf";
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "occupancy stays UseFromSet/DefInTail; do not restamp occupiedAtSet";
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R2)
      .addMBB(Latch);
  bnez(Haydn::R2, Header);
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse)
      << "header BNEZ of Prefer is a body use; opcode-wide skip would hide "
         "va-arg-22 header-hide PreferHasBodyUse";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)")
      << "COPY-kill plus header redef must not hide r14 in skipLatchScrReason";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R4, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";
}

// D1.64 skip-set / four-seat LatchExcl seal. PreferHasBodyUse is load-bearing
// on header BNEZ (latch BNEZ_W skipped; Prefer is not XOR-occupied). Helper
// miss of the FA cluster, header-hide of tail-imm r14, or opcode-wide BNEZ
// skip would last-resort LatchScr=Prefer or steal r3/r14 on occupancy miss
// without a unit failure. Mentioned PEI dest r7 stays pass-1a. Unused R14 is
// the tail-imm dest here (out-of-LatchExcl unused R14 is a sibling pin).
// Occupancy is not skipLatchScrReason.
TEST_F(HaydnHWLoopDemoteTest,
       SkipLatchScrFourSeatsHeaderVsLatchFaClusterHeaderHideOccupancyMissNotPrefer) {
  static const MCPhysReg Occ[] = {Haydn::R1, Haydn::R8, Haydn::R9, Haydn::R10,
                                  Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), Haydn::R7)
      .addReg(Haydn::R7)
      .addReg(Haydn::R7);
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R2)
      .addMBB(Latch);
  bnez(Haydn::R2, Header);

  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(32);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  auto addExitReadFa = [&](MCPhysReg R, int64_t Off) {
    Preheader->addLiveIn(R);
    Header->addLiveIn(R);
    Latch->addLiveIn(R);
    Exit->addLiveIn(R);
    BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
            R)
        .addReg(Haydn::R13)
        .addImm(Off);
    BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
        .addReg(R)
        .addReg(Haydn::R13)
        .addImm(0);
  };
  addExitReadFa(Haydn::R3, 16);
  addExitReadFa(Haydn::R5, 24);
  addExitReadFa(Haydn::R6, 28);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(4);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(),
          TII().get(TargetOpcode::COPY), Haydn::R4)
      .addReg(Haydn::R14, RegState::Kill);
  BuildMI(*Header, Header->begin(), DebugLoc(), TII().get(TargetOpcode::COPY),
          Haydn::R14)
      .addReg(Haydn::R4);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  auto Blocks = loopBlocks();

  EXPECT_TRUE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch))
      << "header BNEZ of Prefer is a body use; latch BNEZ_W skip is "
         "latch-block only";
  EXPECT_FALSE(haydn::hwloop::regClobberedNonCountdownIn(Haydn::R2, Blocks))
      << "header zero-test is a use, not a CB-165 clobber";
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R7, Blocks));
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R7, Preheader, From, Exit, Blocks, TRI))
      << "mentioned PEI dest r7 stays pass-1a";
  for (MCPhysReg R : {Haydn::R3, Haydn::R5, Haydn::R6}) {
    EXPECT_FALSE(haydn::hwloop::regMentionedInBlocks(R, Blocks)) << R;
    EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(R, Preheader, From,
                                                              TRI))
        << R;
    EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
        R, Preheader, From, Exit, Blocks, TRI))
        << "unmentioned Exit-read FA cluster is unsound LatchScr";
  }
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R14, Blocks))
      << "header $r14=COPY $r4 is a LoopBlocks mention";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "header-hide must not drop ADDI dest r14";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R4, Preheader, From, Exit, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI))
      << "header redef mention must not hide unsound LatchScr r14";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R4, Preheader, From, Exit, Blocks, TRI));

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse)
      << "opcode-wide BNEZ skip would hide PreferHasBodyUse";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R5, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R6, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)")
      << "header-hide of tail-imm r14 must stay skipLatchScrReason";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R4, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "mentioned PEI dest r7 stays out of skipLatchScrReason LatchExcl";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R8, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "occupied GPR with Exit ST32 is LatchExcl Exit-path, not occupancy";
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R3, Setup.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";

  SmallVector<Register, 16> LatchExcl;
  excludeSkipLatchScr(LatchExcl, From, Haydn::R2, PreferHasBodyUse);
  EXPECT_TRUE(exclContains(LatchExcl, Haydn::R2));
  EXPECT_TRUE(exclContains(LatchExcl, Haydn::R3));
  EXPECT_TRUE(exclContains(LatchExcl, Haydn::R5));
  EXPECT_TRUE(exclContains(LatchExcl, Haydn::R6));
  EXPECT_TRUE(exclContains(LatchExcl, Haydn::R14))
      << "skipLatchScrReason LatchExcl keeps header-hide ADDI dest r14";
  EXPECT_TRUE(exclContains(LatchExcl, Haydn::R4));
  EXPECT_FALSE(exclContains(LatchExcl, Haydn::R7));
  EXPECT_TRUE(exclContains(LatchExcl, Haydn::R8))
      << "Exit ST32 of occupied R8 is skipLatchScrReason Exit-path";

  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_EQ(Four, Register(Haydn::R7))
      << "four seats: mentioned PEI dest r7 stays pass-1a";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
  EXPECT_NE(Four, Register(Haydn::R3))
      << "four seats must not steal Exit-read FA r3";
  EXPECT_NE(Four, Register(Haydn::R5));
  EXPECT_NE(Four, Register(Haydn::R6));
  EXPECT_NE(Four, Register(Haydn::R14))
      << "four seats must not steal header-hide tail-imm r14";
  EXPECT_NE(Four, Register(Haydn::R4));

  occupyLatchWindow(ArrayRef<MCPhysReg>({Haydn::R7}));
  Register FourMiss = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_FALSE(FourMiss.isPhysical())
      << "occupancy miss with PreferHasBodyUse must not become LatchScr=Prefer";
  EXPECT_NE(FourMiss, Register(Haydn::R2))
      << "opcode-wide BNEZ skip would last-resort Prefer on occupancy miss";
  EXPECT_NE(FourMiss, Register(Haydn::R3))
      << "helper miss of the FA cluster would steal r3 on occupancy miss";
  EXPECT_NE(FourMiss, Register(Haydn::R5));
  EXPECT_NE(FourMiss, Register(Haydn::R6));
  EXPECT_NE(FourMiss, Register(Haydn::R14))
      << "header-hide of tail-imm r14 would steal r14 on occupancy miss";
  EXPECT_NE(FourMiss, Register(Haydn::R4));
}

// Unified LatchExcl predicate: unmentioned FA r3 and live-into-exit r14
// vs SMS-guard ADDI with no Exit read. Helper-green is not enough: four
// seats (NoSpill / pickDead / re-pick / last-resort Prefer) must Exclude
// r3 and r14, keep SMS-guard r7, and refuse LatchScr=Prefer.
TEST_F(HaydnHWLoopDemoteTest, UnsoundLatchScratchAtSetFaVsTailImmVsSmsGuard) {
  static const MCPhysReg Occ[] = {Haydn::R1, Haydn::R4, Haydn::R5, Haydn::R6,
                                  Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11,
                                  Haydn::R12};
  occupyLatchWindow(Occ);
  // Keep header-vs-latch: header BNEZ of Prefer is a body use; latch
  // BNEZ_W of the same reg is the terminator skip.
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R2)
      .addMBB(Latch);
  bnez(Haydn::R2, Header);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R0)
      .addImm(1);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(0);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  auto Blocks = loopBlocks();
  EXPECT_TRUE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch))
      << "header BNEZ of Prefer is a body use; latch BNEZ_W skip is "
         "latch-block only";
  EXPECT_FALSE(haydn::hwloop::regClobberedNonCountdownIn(Haydn::R2, Blocks))
      << "header zero-test is a use, not a CB-165 clobber";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R3, Preheader, From, Exit, Blocks, TRI))
      << "unmentioned r3=sp+off that Exit reads is unsound LatchScr";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI))
      << "unmentioned live-into-exit r14 is unsound LatchScr";
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R7, Preheader, From, Exit, Blocks, TRI))
      << "SMS-guard ADDI r0,imm with no Exit read stays a sound latch temp";
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R3, Preheader, From, Exit, TRI))
      << "keep FA vs tail-imm: r3=sp+off is not ADDI r0,imm";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI));

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "SMS-guard stays out of LatchExcl";
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R3, Setup.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R3, Setup.getIterator()))
      << "DefInTail FA: occupancy stays UseFromSet || (LivePhysRegs-live && "
         "!DefInTail)";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R3, Setup.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R3, Preheader, From, TRI) ||
                (livePhysAtSetOf(Haydn::R3, Setup.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R3, Preheader,
                                                        From, TRI)))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R14, Preheader, From, TRI) ||
                (livePhysAtSetOf(Haydn::R14, Setup.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader,
                                                        From, TRI)));
  EXPECT_EQ(occupiedAtSetOf(Haydn::R7, Setup.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R7, Preheader, From, TRI) ||
                (livePhysAtSetOf(Haydn::R7, Setup.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R7, Preheader,
                                                        From, TRI)));

  SmallVector<Register, 16> SkipExcl;
  excludeSkipLatchScr(SkipExcl, From, Haydn::R2, PreferHasBodyUse);
  EXPECT_TRUE(exclContains(SkipExcl, Haydn::R3))
      << "LatchExcl must contain Exit-read FA r3";
  EXPECT_TRUE(exclContains(SkipExcl, Haydn::R14))
      << "LatchExcl must contain live-into-exit tail-imm r14";
  EXPECT_TRUE(exclContains(SkipExcl, Haydn::R2))
      << "skipLatchScrReason LatchExcl includes PreferHasBodyUse";
  EXPECT_FALSE(exclContains(SkipExcl, Haydn::R7))
      << "SMS-guard must not enter LatchExcl";

  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_EQ(Four, Register(Haydn::R7))
      << "four seats: SMS-guard stays pass-1a after FA ∪ tail-imm Exclude";
  EXPECT_NE(Four, Register(Haydn::R3))
      << "pass-2 must not steal Exit-read FA r3";
  EXPECT_NE(Four, Register(Haydn::R14))
      << "pass-2 must not steal live-into-exit tail-imm r14";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
}

// Unmentioned $r3=ADDI SP,imm with no Exit-path read stays eligible
// pass-1a (PEI SP+off dead temp / bqriir). Unused R14 is not unsound.
// Occupancy stays UseFromSet / DefInTail, not any-mention.
TEST_F(HaydnHWLoopDemoteTest, UnsoundLatchScratchUnmentionedFaWithoutExitRead) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  auto Blocks = loopBlocks();
  EXPECT_TRUE(Exit->empty()) << "no Exit-path read of r3";
  EXPECT_FALSE(haydn::hwloop::regMentionedInBlocks(Haydn::R3, Blocks));
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "r3=ADDI SP,imm is FA last-def";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R3, Preheader, From, Exit, TRI))
      << "empty Exit is not a live-into-exit read of r3";
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R3, Preheader, From, Exit, Blocks, TRI))
      << "unmentioned r3=ADDI SP,imm without Exit read stays pass-1a";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI))
      << "unused R14 is not unsound LatchScr";
  const bool R3Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R3, Preheader, From, TRI);
  const bool R3Def =
      haydn::hwloop::regDefdInPreheaderTail(Haydn::R3, Preheader, From, TRI);
  EXPECT_TRUE(R3Def);
  EXPECT_FALSE(R3Use);
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R3, Setup.getIterator()))
      << "DefInTail kills occupancy; do not restamp occupiedAtSet";
  EXPECT_NE(occupiedAtSetOf(Haydn::R3, Setup.getIterator()),
            haydn::hwloop::regMentionedInPreheaderTail(Haydn::R3, Preheader,
                                                       From, TRI))
      << "occupancy is not any-mention";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R3, Setup.getIterator()),
            R3Use || (livePhysAtSetOf(Haydn::R3, Setup.getIterator()) && !R3Def))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, /*PreferHasBodyUse=*/false,
                                 From),
            nullptr)
      << "PEI dest without Exit read is not skipLatchScrReason LatchExcl";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2,
                                 /*PreferHasBodyUse=*/false, From),
            nullptr)
      << "unused R14 stays out of LatchExcl";
}

// $r14=ADDI r0,imm then COPY/MOVE dest live into Exit is unsound on every
// copy-chain member. SMS-guard ADDI r0,imm with no Exit read is not.
TEST_F(HaydnHWLoopDemoteTest, UnsoundLatchScratchTailImmCopyChainWithExitRead) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(22);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(TargetOpcode::COPY),
          Haydn::R4)
      .addReg(Haydn::R14);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R8)
      .addReg(Haydn::R0)
      .addImm(1);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R4)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  auto Blocks = loopBlocks();
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "ADDI dest of a live-into-exit COPY dest is the copy-chain";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R4, Preheader, From, Exit, TRI))
      << "COPY dest of after-SET ADDI r0,imm with Exit read is unsound";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R4, Preheader, From, Exit, Blocks, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "ADDI r0,imm is tail-imm, not FA";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R8, Preheader, From, Exit, TRI))
      << "SMS-guard ADDI r0,imm with no Exit read is not live-into-exit";
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R8, Preheader, From, Exit, Blocks, TRI))
      << "SMS-guard ADDI with no Exit read stays a sound latch temp";
  EXPECT_FALSE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R14, Blocks, Latch))
      << "keep body-read: Exit use is not a loop-body use";
  const bool R14Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  const bool R14Def = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  EXPECT_TRUE(R14Def);
  EXPECT_FALSE(R14Use);
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()));
  EXPECT_NE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            haydn::hwloop::regMentionedInPreheaderTail(Haydn::R14, Preheader,
                                                       From, TRI))
      << "occupancy is not any-mention; do not restamp occupiedAtSet";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            R14Use ||
                (livePhysAtSetOf(Haydn::R14, Setup.getIterator()) && !R14Def));
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2,
                                    /*PreferHasBodyUse=*/false, From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R4, Haydn::R2,
                                    /*PreferHasBodyUse=*/false, From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R8, Haydn::R2, /*PreferHasBodyUse=*/false,
                                 From),
            nullptr)
      << "SMS-guard stays out of LatchExcl";
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R14, Haydn::R2,
                                      /*PreferHasBodyUse=*/false, From)),
            occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";
}

// Body-mentioned $r7=ADDI SP,imm stays eligible pass-1a (PEI SP+off dest
// must not exhaust LatchScr). Exclude Prefer still honors unused R14.
TEST_F(HaydnHWLoopDemoteTest, UnsoundLatchScratchBodyMentionedFaStaysEligible) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R3,  Haydn::R4, Haydn::R5,  Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), Haydn::R7)
      .addReg(Haydn::R7)
      .addReg(Haydn::R7);
  // Keep header-vs-latch: header BNEZ of Prefer is a body use; latch
  // BNEZ_W of the same reg is the terminator skip.
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R2)
      .addMBB(Latch);
  bnez(Haydn::R2, Header);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(16);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  auto Blocks = loopBlocks();
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R7, Blocks))
      << "latch XOR of r7 is a LoopBlocks mention";
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI))
      << "r7=ADDI SP,imm is FA last-def";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R7, Preheader, From, Exit, Blocks, TRI))
      << "body-mentioned r7 FA stays eligible pass-1a";
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI))
      << "unused R14 is not unsound";
  const bool R7Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R7, Preheader, From, TRI);
  const bool R7Def =
      haydn::hwloop::regDefdInPreheaderTail(Haydn::R7, Preheader, From, TRI);
  EXPECT_TRUE(R7Def);
  EXPECT_FALSE(R7Use);
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R7, Setup.getIterator()))
      << "DefInTail kills occupancy; mentioned FA is not overlay ST";
  EXPECT_NE(occupiedAtSetOf(Haydn::R7, Setup.getIterator()),
            haydn::hwloop::regMentionedInPreheaderTail(Haydn::R7, Preheader,
                                                       From, TRI))
      << "occupancy is not any-mention";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R7, Setup.getIterator()),
            R7Use || (livePhysAtSetOf(Haydn::R7, Setup.getIterator()) && !R7Def));

  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_EQ(Scr, Register(Haydn::R7))
      << "body-mentioned r7 FA is a sound pass-1a LatchScr";
  Register ExcludePrefer[] = {Haydn::R2};
  Register ScrEx = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, ExcludePrefer, "test");
  EXPECT_NE(ScrEx, Register(Haydn::R2))
      << "PickDeadLatchScratchHonorsExcludePrefer: Prefer stays excluded";
  EXPECT_EQ(ScrEx, Register(Haydn::R7))
      << "Exclude Prefer still leaves body-mentioned r7 FA eligible";

  EXPECT_TRUE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch))
      << "header BNEZ of Prefer is a body use; latch BNEZ_W skip is "
         "latch-block only";
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse);
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "mentioned PEI dest r7 stays pass-1a";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "unused R14 stays out of LatchExcl";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R7, Setup.getIterator()))
      << "DefInTail PEI dest: occupancy stays UseFromSet || "
         "(LivePhysRegs-live && !DefInTail)";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R7, Setup.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R7, Preheader, From, TRI) ||
                (livePhysAtSetOf(Haydn::R7, Setup.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R7, Preheader,
                                                        From, TRI)))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_EQ(Four, Register(Haydn::R7))
      << "four seats: mentioned PEI dest r7 stays pass-1a";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
  EXPECT_NE(Four, Register(Haydn::R14))
      << "four seats must not steal unused R14";
}

// LatchExcl five-way split on one fixture (va-arg-22 NoSpill):
//   * Exit-read FA ($r3=sp+off, unmentioned, Exit ST32) is unsound — after
//     NoSpill exhausts it is occupancy miss, not a pass-2 steal.
//   * PEI SP+off dead temp ($r7=sp+off, latch XOR mention, Exit does not
//     read r7) stays eligible pass-1a. Over-wide unsound on that dest
//     exhausts NoSpill (bqriir).
//   * live-into-exit tail-imm ($r4=ADDI r0,imm, Exit ST32) is unsound.
//   * unused R14 is not unsound (not FA, not tail-imm, no Exit read).
//   * SMS-guard ADDI r0,imm ($r8, no Exit read) stays a sound latch temp.
// Production LatchExcl is regIsUnsoundLatchScratchAtSet only. Do not fold
// occupancy into that predicate (D1.71). Keep header-vs-latch zero-test
// and body-store uses-true on this fixture.
TEST_F(HaydnHWLoopDemoteTest,
       UnsoundLatchScratchLatchExclSplitExitReadFaVsPeiDeadTempVsTailImmVsUnusedR14VsSmsGuard) {
  static const MCPhysReg Occ[] = {Haydn::R1, Haydn::R5, Haydn::R6, Haydn::R9,
                                  Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  // PEI SP+off dead temp: loop mention so unsound cannot swallow it.
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), Haydn::R7)
      .addReg(Haydn::R7)
      .addReg(Haydn::R7);
  // Keep body-store uses-true: latch ST32 of Prefer is a non-countdown read.
  // Do not XOR Prefer — that would invert this into a CB-165 clobber.
  st32(Haydn::R2, Haydn::R13, 4);
  // Keep header-vs-latch: header BNEZ of Prefer is a body use; latch
  // BNEZ_W of the same reg is the terminator skip (not a body use).
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R2)
      .addMBB(Latch);
  bnez(Haydn::R2, Header);

  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(32);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R4)
      .addReg(Haydn::R0)
      .addImm(22);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R8)
      .addReg(Haydn::R0)
      .addImm(1);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(0);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R4)
      .addReg(Haydn::R13)
      .addImm(0);

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  auto Blocks = loopBlocks();

  EXPECT_TRUE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch))
      << "keep body-store uses-true and header-vs-latch: latch ST32 and "
         "header BNEZ of Prefer are body reads";
  EXPECT_FALSE(haydn::hwloop::regClobberedNonCountdownIn(Haydn::R2, Blocks))
      << "body store / header zero-test are uses, not CB-165 clobbers";

  EXPECT_FALSE(haydn::hwloop::regMentionedInBlocks(Haydn::R3, Blocks));
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "Exit-read FA: r3=sp+off is FA last-def";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R3, Preheader, From, Exit, TRI))
      << "keep FA vs tail-imm: r3=sp+off is not ADDI r0,imm";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R3, Preheader, From, Exit, Blocks, TRI))
      << "Exit-read FA r3 is unsound LatchScr";

  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R7, Blocks))
      << "PEI SP+off dest is a LoopBlocks mention";
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI))
      << "PEI r7=sp+off is FA last-def";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI))
      << "PEI dead temp: Exit does not read r7";
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R7, Preheader, From, Exit, Blocks, TRI))
      << "PEI SP+off dead temp must stay pass-1a; over-wide unsound exhausts "
         "NoSpill";

  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R4, Preheader, From, Exit, TRI))
      << "live-into-exit tail-imm r4=ADDI r0,imm with Exit read";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R4, Preheader, From, TRI))
      << "ADDI r0,imm is tail-imm, not FA";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R4, Preheader, From, Exit, Blocks, TRI))
      << "live-into-exit tail-imm is unsound LatchScr";

  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI))
      << "unused R14 is not unsound LatchScr";

  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R8, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R8, Preheader, From, Exit, TRI))
      << "SMS-guard ADDI r0,imm with no Exit read is not live-into-exit";
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R8, Preheader, From, Exit, Blocks, TRI))
      << "SMS-guard ADDI with no Exit read stays a sound latch temp";

  auto occLaw = [&](MCPhysReg R) {
    const bool Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
        R, Preheader, From, TRI);
    const bool Def =
        haydn::hwloop::regDefdInPreheaderTail(R, Preheader, From, TRI);
    EXPECT_EQ(occupiedAtSetOf(R, Setup.getIterator()),
              Use || (livePhysAtSetOf(R, Setup.getIterator()) && !Def))
        << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
    return occupiedAtSetOf(R, Setup.getIterator());
  };
  EXPECT_FALSE(occLaw(Haydn::R3))
      << "DefInTail FA: occupancy is not any-mention";
  EXPECT_NE(occupiedAtSetOf(Haydn::R3, Setup.getIterator()),
            haydn::hwloop::regMentionedInPreheaderTail(Haydn::R3, Preheader,
                                                       From, TRI))
      << "do not restamp occupiedAtSet as any-tail-mention";
  EXPECT_FALSE(occLaw(Haydn::R4))
      << "DefInTail tail-imm: occupancy is not any-mention";
  EXPECT_NE(occupiedAtSetOf(Haydn::R4, Setup.getIterator()),
            haydn::hwloop::regMentionedInPreheaderTail(Haydn::R4, Preheader,
                                                       From, TRI));
  EXPECT_TRUE(occLaw(Haydn::R7))
      << "D1.71r: Latch XOR live-through occupies SET; the pre-SET PEI ADDI "
         "is not DefInTail. Pass-1a at latch end is the XOR def";
  EXPECT_FALSE(occLaw(Haydn::R14))
      << "unused R14 is not occupied at SET; never overlay ST";

  SmallVector<Register, 16> LatchExcl;
  excludeUnsoundLatchScr(LatchExcl, From);
  auto exclHas = [&](MCPhysReg P) {
    for (Register R : LatchExcl)
      if (R == P)
        return true;
    return false;
  };
  EXPECT_TRUE(exclHas(Haydn::R3)) << "LatchExcl must contain Exit-read FA r3";
  EXPECT_TRUE(exclHas(Haydn::R4))
      << "LatchExcl must contain live-into-exit tail-imm r4";
  EXPECT_FALSE(exclHas(Haydn::R7))
      << "LatchExcl must not swallow PEI SP+off dead temp r7";
  EXPECT_FALSE(exclHas(Haydn::R14)) << "LatchExcl must not swallow unused R14";
  EXPECT_FALSE(exclHas(Haydn::R8))
      << "LatchExcl must not swallow SMS-guard ADDI r8";
  EXPECT_FALSE(exclHas(Haydn::R2))
      << "LatchExcl unsound is FA ∪ tail-imm; PreferHasBodyUse is a separate "
         "Exclude";

  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, LatchExcl, "test");
  EXPECT_EQ(Scr, Register(Haydn::R7))
      << "after LatchExcl, PEI SP+off dead temp remains pass-1a; NoSpill lives";
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "pass-2 must not keep Exit-read FA r3 as LatchScr";
  EXPECT_NE(Scr, Register(Haydn::R4))
      << "pass-2 must not keep live-into-exit tail-imm r4 as LatchScr";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "defined PEI dead temp wins over unused R14";
  EXPECT_NE(Scr, Register(Haydn::R2))
      << "header-vs-latch body-read Prefer is not LatchScr while pass-1a lives";

  Register NoSpill = findPostRAScratchNoSpill(
      *Latch, Latch->end(), /*PreferNotR12=*/true, PostSuccs, LatchExcl);
  EXPECT_EQ(NoSpill, Register(Haydn::R7))
      << "NoSpill + LatchExcl keeps mentioned PEI SP+off pass-1a";
  EXPECT_NE(NoSpill, Register(Haydn::R3));
  EXPECT_NE(NoSpill, Register(Haydn::R4));
  EXPECT_NE(NoSpill, Register(Haydn::R14))
      << "NoSpill priority omits unused R14; overlay of it is D1.71";

  // PreferHasBodyUse pushes Prefer into LatchExcl for both pickers and
  // gates last-resort LatchScr=Prefer. Pass-1a r7 still wins.
  SmallVector<Register, 16> LatchExclPrefer = LatchExcl;
  LatchExclPrefer.push_back(Haydn::R2);
  Register ScrPreferEx = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, LatchExclPrefer, "test");
  EXPECT_EQ(ScrPreferEx, Register(Haydn::R7));
  EXPECT_NE(ScrPreferEx, Register(Haydn::R2))
      << "PreferHasBodyUse: LatchScr=Prefer refused";
  Register NoSpillPreferEx = findPostRAScratchNoSpill(
      *Latch, Latch->end(), /*PreferNotR12=*/true, PostSuccs, LatchExclPrefer);
  EXPECT_EQ(NoSpillPreferEx, Register(Haydn::R7));
  EXPECT_NE(NoSpillPreferEx, Register(Haydn::R2));

  // Production skipLatchScrReason is PreferHasBodyUse ∪ unsound. Unsound
  // itself must not swallow Prefer, PEI dest r7, unused R14, or SMS-guard.
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse);
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R2, Preheader, From, Exit, Blocks, TRI))
      << "PreferHasBodyUse stays a separate Exclude; unsound is FA ∪ tail-imm";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R4, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, /*PreferHasBodyUse=*/false,
                                 From),
            nullptr)
      << "without PreferHasBodyUse, Prefer is not unsound LatchExcl";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "mentioned PEI dest r7 stays pass-1a";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "unused R14 stays out of LatchExcl";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R8, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "SMS-guard stays out of LatchExcl";
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R3, Setup.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";

  SmallVector<Register, 16> SkipExcl;
  excludeSkipLatchScr(SkipExcl, From, Haydn::R2, PreferHasBodyUse);
  EXPECT_TRUE(exclContains(SkipExcl, Haydn::R3));
  EXPECT_TRUE(exclContains(SkipExcl, Haydn::R4));
  EXPECT_TRUE(exclContains(SkipExcl, Haydn::R2))
      << "skipLatchScrReason LatchExcl includes PreferHasBodyUse";
  EXPECT_FALSE(exclContains(SkipExcl, Haydn::R7));
  EXPECT_FALSE(exclContains(SkipExcl, Haydn::R14));
  EXPECT_FALSE(exclContains(SkipExcl, Haydn::R8));

  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_EQ(Four, Register(Haydn::R7))
      << "four seats: mentioned PEI dest r7 stays pass-1a";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
  EXPECT_NE(Four, Register(Haydn::R3))
      << "pass-2 must not steal Exit-read FA r3";
  EXPECT_NE(Four, Register(Haydn::R4));
  EXPECT_NE(Four, Register(Haydn::R14))
      << "four seats must not steal unused R14";
  EXPECT_NE(Four, Register(Haydn::R8))
      << "defined PEI dest wins over SMS-guard";
}

// va-arg-22 main() fill-loop: SET then a cluster of unmentioned SP+off
// dests live into Exit (r4/r5/r6 address temps) plus after-SET ADDI r0,imm
// r14. Prefer is a body-read IV. Mentioned PEI SP+off dest r7 stays
// pass-1a — excluding the cluster must not swallow it. Occupying r7 is
// occupancy miss, never LatchScr=Prefer / cluster FA / r14.
TEST_F(HaydnHWLoopDemoteTest,
       UnsoundLatchScratchMainFillLoopFaClusterKeepsMentionedPeiDest) {
  static const MCPhysReg Occ[] = {Haydn::R1, Haydn::R8, Haydn::R9, Haydn::R10,
                                  Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::XOR32), Haydn::R7)
      .addReg(Haydn::R7)
      .addReg(Haydn::R7);
  st32(Haydn::R2, Haydn::R13, 4);
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R2)
      .addMBB(Latch);
  bnez(Haydn::R2, Header);

  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(32);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  auto addExitReadFa = [&](MCPhysReg R, int64_t Off) {
    Preheader->addLiveIn(R);
    Header->addLiveIn(R);
    Latch->addLiveIn(R);
    Exit->addLiveIn(R);
    BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
            R)
        .addReg(Haydn::R13)
        .addImm(Off);
    BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
        .addReg(R)
        .addReg(Haydn::R13)
        .addImm(0);
  };
  addExitReadFa(Haydn::R3, 16);
  addExitReadFa(Haydn::R4, 20);
  addExitReadFa(Haydn::R5, 24);
  addExitReadFa(Haydn::R6, 28);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(118);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  auto Blocks = loopBlocks();

  EXPECT_TRUE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch));
  EXPECT_FALSE(haydn::hwloop::regClobberedNonCountdownIn(Haydn::R2, Blocks));
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R7, Blocks));
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R7, Preheader, From, Exit, Blocks, TRI))
      << "mentioned PEI SP+off dest stays pass-1a; cluster FA must not swallow "
         "it";
  for (MCPhysReg R : {Haydn::R3, Haydn::R4, Haydn::R5, Haydn::R6}) {
    EXPECT_FALSE(haydn::hwloop::regMentionedInBlocks(R, Blocks)) << R;
    EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(R, Preheader, From,
                                                              TRI))
        << R;
    EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(R, Preheader, From,
                                                         Exit, TRI))
        << R;
    EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
        R, Preheader, From, Exit, Blocks, TRI))
        << "unmentioned Exit-read FA cluster is unsound LatchScr";
  }
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R8, Preheader, From, Exit, Blocks, TRI))
      << "occupied non-FA must not enter LatchExcl as FA";

  SmallVector<Register, 16> LatchExcl;
  excludeUnsoundLatchScr(LatchExcl, From);
  auto exclHas = [&](MCPhysReg P) {
    for (Register R : LatchExcl)
      if (R == P)
        return true;
    return false;
  };
  EXPECT_TRUE(exclHas(Haydn::R3));
  EXPECT_TRUE(exclHas(Haydn::R4));
  EXPECT_TRUE(exclHas(Haydn::R5));
  EXPECT_TRUE(exclHas(Haydn::R6));
  EXPECT_TRUE(exclHas(Haydn::R14));
  EXPECT_FALSE(exclHas(Haydn::R7))
      << "LatchExcl must not swallow mentioned PEI SP+off dest r7";
  EXPECT_FALSE(exclHas(Haydn::R2));
  EXPECT_FALSE(exclHas(Haydn::R8));

  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  Register EmptyScr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_EQ(EmptyScr, Register(Haydn::R7))
      << "empty Exclude skip of FA cluster ∪ r14 still leaves mentioned PEI dest";
  EXPECT_NE(EmptyScr, Register(Haydn::R3));
  EXPECT_NE(EmptyScr, Register(Haydn::R14));
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, LatchExcl, "test");
  EXPECT_EQ(Scr, Register(Haydn::R7))
      << "after FA-cluster LatchExcl, mentioned PEI dest remains pass-1a";
  EXPECT_NE(Scr, Register(Haydn::R3));
  EXPECT_NE(Scr, Register(Haydn::R4));
  EXPECT_NE(Scr, Register(Haydn::R5));
  EXPECT_NE(Scr, Register(Haydn::R6));
  EXPECT_NE(Scr, Register(Haydn::R14));
  EXPECT_NE(Scr, Register(Haydn::R2));
  Register NoSpill = findPostRAScratchNoSpill(
      *Latch, Latch->end(), /*PreferNotR12=*/true, PostSuccs, LatchExcl);
  EXPECT_EQ(NoSpill, Register(Haydn::R7));

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse);
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R2, Preheader, From, Exit, Blocks, TRI))
      << "PreferHasBodyUse stays a separate Exclude";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R4, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)")
      << "FA cluster r4 must enter skipLatchScrReason, not only r3";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R5, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R6, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "mentioned PEI dest r7 stays pass-1a";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R8, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "occupied GPR with Exit ST32 is LatchExcl Exit-path, not occupancy";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R3, Setup.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R3, Setup.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R3, Preheader, From, TRI) ||
                (livePhysAtSetOf(Haydn::R3, Setup.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R3, Preheader,
                                                        From, TRI)))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R14, Preheader, From, TRI) ||
                (livePhysAtSetOf(Haydn::R14, Setup.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader,
                                                        From, TRI)))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  Register FourKeep = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_EQ(FourKeep, Register(Haydn::R7))
      << "four seats keep mentioned PEI dest r7 pass-1a";
  EXPECT_NE(FourKeep, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
  EXPECT_NE(FourKeep, Register(Haydn::R3))
      << "four seats must not steal Exit-read FA r3";
  EXPECT_NE(FourKeep, Register(Haydn::R4))
      << "four seats must not steal FA cluster r4";
  EXPECT_NE(FourKeep, Register(Haydn::R5));
  EXPECT_NE(FourKeep, Register(Haydn::R6));
  EXPECT_NE(FourKeep, Register(Haydn::R14));

  occupyLatchWindow(ArrayRef<MCPhysReg>({Haydn::R7}));
  SmallVector<Register, 16> LatchExclMiss;
  excludeUnsoundLatchScr(LatchExclMiss, From);
  LatchExclMiss.push_back(Haydn::R2);
  Register Miss = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, LatchExclMiss, "test");
  EXPECT_FALSE(Miss.isPhysical())
      << "occupying the mentioned PEI dest is occupancy miss, not Prefer / "
         "cluster FA / r14";
  EXPECT_NE(Miss, Register(Haydn::R2));
  EXPECT_NE(Miss, Register(Haydn::R4));
  EXPECT_NE(Miss, Register(Haydn::R5));
  EXPECT_NE(Miss, Register(Haydn::R6));
  EXPECT_NE(Miss, Register(Haydn::R14));
  Register FourMiss = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_FALSE(FourMiss.isPhysical())
      << "four seats: occupying PEI dest is occupancy miss, not last-resort "
         "Prefer";
  EXPECT_NE(FourMiss, Register(Haydn::R2));
  EXPECT_NE(FourMiss, Register(Haydn::R3));
  EXPECT_NE(FourMiss, Register(Haydn::R4));
  EXPECT_NE(FourMiss, Register(Haydn::R5));
  EXPECT_NE(FourMiss, Register(Haydn::R6));
  EXPECT_NE(FourMiss, Register(Haydn::R14));
}

// va-arg-22 @main fill-loop residual: header==latch carries the SP+off
// cluster as live-ins with no loop-body mention (bb.1 only uses the IV).
// Exit reads r3–r7 as address temps; r14 is after-SET ADDI r0,imm.
// There is no mentioned PEI dest. LatchExcl of that cluster is occupancy
// miss, never pass-2 steal of r7/r14 or LatchScr=Prefer. Overlay of the
// stolen live-through is D1.71.
TEST_F(HaydnHWLoopDemoteTest,
       UnsoundLatchScratchMainFillLoopUnmentionedFaClusterIsOccupancyMissNotSteal) {
  static const MCPhysReg Occ[] = {Haydn::R1, Haydn::R8, Haydn::R9, Haydn::R10,
                                  Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  st32(Haydn::R2, Haydn::R13, 4);
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R2)
      .addMBB(Latch);
  bnez(Haydn::R2, Header);

  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  auto addExitReadFa = [&](MCPhysReg R, int64_t Off) {
    Preheader->addLiveIn(R);
    Header->addLiveIn(R);
    Latch->addLiveIn(R);
    Exit->addLiveIn(R);
    BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
            R)
        .addReg(Haydn::R13)
        .addImm(Off);
    BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
        .addReg(R)
        .addReg(Haydn::R13)
        .addImm(0);
  };
  addExitReadFa(Haydn::R3, 16);
  addExitReadFa(Haydn::R4, 20);
  addExitReadFa(Haydn::R5, 24);
  addExitReadFa(Haydn::R6, 28);
  addExitReadFa(Haydn::R7, 32);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(118);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  auto Blocks = loopBlocks();

  EXPECT_TRUE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch));
  EXPECT_FALSE(haydn::hwloop::regClobberedNonCountdownIn(Haydn::R2, Blocks));
  EXPECT_FALSE(haydn::hwloop::regMentionedInBlocks(Haydn::R7, Blocks))
      << "@main r7 is a live-in address temp, not a loop-body PEI dest";
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R7, Preheader, From, Exit, Blocks, TRI))
      << "unmentioned Exit-read FA r7 is unsound; mentioned PEI dest is the "
         "other class";
  for (MCPhysReg R : {Haydn::R3, Haydn::R4, Haydn::R5, Haydn::R6, Haydn::R7}) {
    EXPECT_FALSE(haydn::hwloop::regMentionedInBlocks(R, Blocks)) << R;
    EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
        R, Preheader, From, Exit, Blocks, TRI))
        << "unmentioned Exit-read FA cluster is unsound LatchScr";
  }
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI));

  SmallVector<Register, 16> LatchExcl;
  excludeUnsoundLatchScr(LatchExcl, From);
  LatchExcl.push_back(Haydn::R2);
  auto exclHas = [&](MCPhysReg P) {
    for (Register R : LatchExcl)
      if (R == P)
        return true;
    return false;
  };
  EXPECT_TRUE(exclHas(Haydn::R3));
  EXPECT_TRUE(exclHas(Haydn::R4));
  EXPECT_TRUE(exclHas(Haydn::R5));
  EXPECT_TRUE(exclHas(Haydn::R6));
  EXPECT_TRUE(exclHas(Haydn::R7))
      << "unmentioned Exit-read FA r7 must enter LatchExcl";
  EXPECT_TRUE(exclHas(Haydn::R14));
  EXPECT_TRUE(exclHas(Haydn::R2)) << "PreferHasBodyUse Exclude";
  EXPECT_FALSE(exclHas(Haydn::R8))
      << "occupied non-FA must not enter LatchExcl as FA";

  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, LatchExcl, "test");
  EXPECT_FALSE(Scr.isPhysical())
      << "@main fill-loop: FA cluster + tail-imm LatchExcl is occupancy miss, "
         "not pass-2 steal";
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "pass-2 must not keep unmentioned Exit-read FA r7 as LatchScr";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "pass-2 must not keep live-into-exit r14 as LatchScr";
  EXPECT_NE(Scr, Register(Haydn::R2))
      << "occupancy miss must not be LatchScr=Prefer";
  Register NoSpill = findPostRAScratchNoSpill(
      *Latch, Latch->end(), /*PreferNotR12=*/true, PostSuccs, LatchExcl);
  EXPECT_FALSE(NoSpill.isPhysical())
      << "NoSpill misses with the same LatchExcl; occupancy miss";
  EXPECT_NE(NoSpill, Register(Haydn::R7));
  EXPECT_NE(NoSpill, Register(Haydn::R14));

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)")
      << "@main r7 is unmentioned Exit-read FA, not a PEI dest";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_FALSE(Four.isPhysical())
      << "@main fill-loop: four seats occupancy miss, not last-resort Prefer";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "occupancy miss must not be LatchScr=Prefer countdown";
  EXPECT_NE(Four, Register(Haydn::R7))
      << "pass-2 must not steal unmentioned Exit-read FA r7";
  EXPECT_NE(Four, Register(Haydn::R14))
      << "pass-2 must not steal live-into-exit r14";
}

// Same LatchExcl five-way split after occupying the PEI pass-1a dest:
// window-miss after FA ∪ tail-imm skip is occupancy miss, not
// LatchScr=Prefer. Header BNEZ of Prefer is the only non-countdown body
// read (latch BNEZ_W is the terminator skip). Unused R14 with Header
// live-in is not stolen and is never overlay ST (no incoming value).
TEST_F(HaydnHWLoopDemoteTest,
       LatchExclWindowMissAfterFaAndTailImmIsOccupancyMissNotPrefer) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R5, Haydn::R6, Haydn::R7, Haydn::R8,
      Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  // Keep header-vs-latch: header BNEZ of Prefer is a body use; latch
  // BNEZ_W of the same reg is the terminator skip. No latch ST32 of
  // Prefer — PreferHasBodyUse is load-bearing on the header zero-test.
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::BNEZ))
      .addReg(Haydn::R2)
      .addMBB(Latch);
  bnez(Haydn::R2, Header);

  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(32);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R4)
      .addReg(Haydn::R0)
      .addImm(22);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(0);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R4)
      .addReg(Haydn::R13)
      .addImm(0);

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  auto Blocks = loopBlocks();

  EXPECT_TRUE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch))
      << "header BNEZ of Prefer is a body use; latch BNEZ_W skip is "
         "latch-block only";
  EXPECT_FALSE(haydn::hwloop::regClobberedNonCountdownIn(Haydn::R2, Blocks))
      << "header zero-test is a use, not a CB-165 clobber";
  EXPECT_FALSE(haydn::hwloop::isCountdownStepOf(*Header->begin(), Haydn::R2));

  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R3, Preheader, From, Exit, Blocks, TRI))
      << "Exit-read FA r3 is unsound LatchScr";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R4, Preheader, From, Exit, Blocks, TRI))
      << "live-into-exit tail-imm r4 is unsound LatchScr";
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R7, Blocks));
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R7, Preheader, From, Exit, Blocks, TRI))
      << "mentioned PEI SP+off stays eligible; occupancy occupies it here";
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI))
      << "unused R14 is not unsound LatchScr";

  const bool R3Def =
      haydn::hwloop::regDefdInPreheaderTail(Haydn::R3, Preheader, From, TRI);
  const bool R4Def =
      haydn::hwloop::regDefdInPreheaderTail(Haydn::R4, Preheader, From, TRI);
  const bool R14Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  const bool R14Def = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  EXPECT_TRUE(R3Def);
  EXPECT_TRUE(R4Def);
  EXPECT_FALSE(R14Use);
  EXPECT_FALSE(R14Def);
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R3, Setup.getIterator()))
      << "DefInTail FA kills occupancy; refuse overlay";
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R4, Setup.getIterator()))
      << "DefInTail tail-imm kills occupancy; refuse overlay";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            R14Use || (livePhysAtSetOf(Haydn::R14, Setup.getIterator()) &&
                       !R14Def))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  EXPECT_NE(occupiedAtSetOf(Haydn::R3, Setup.getIterator()),
            haydn::hwloop::regMentionedInPreheaderTail(Haydn::R3, Preheader,
                                                       From, TRI))
      << "do not restamp occupiedAtSet as any-tail-mention";

  SmallVector<Register, 16> LatchExcl;
  excludeUnsoundLatchScr(LatchExcl, From);
  LatchExcl.push_back(Haydn::R2);
  auto exclHas = [&](MCPhysReg P) {
    for (Register R : LatchExcl)
      if (R == P)
        return true;
    return false;
  };
  EXPECT_TRUE(exclHas(Haydn::R3));
  EXPECT_TRUE(exclHas(Haydn::R4));
  EXPECT_TRUE(exclHas(Haydn::R2)) << "PreferHasBodyUse Exclude";
  EXPECT_FALSE(exclHas(Haydn::R7));
  EXPECT_FALSE(exclHas(Haydn::R14));

  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, LatchExcl, "test");
  EXPECT_FALSE(Scr.isPhysical())
      << "after FA and tail-imm skips the window misses; occupancy miss";
  EXPECT_NE(Scr, Register(Haydn::R2))
      << "window-miss after FA+tail-imm skip must be occupancy miss, not "
         "LatchScr=Prefer";
  EXPECT_NE(Scr, Register(Haydn::R3));
  EXPECT_NE(Scr, Register(Haydn::R4));
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "must not steal unused live R14; overlay of unused R14 is D1.71";

  Register NoSpill = findPostRAScratchNoSpill(
      *Latch, Latch->end(), /*PreferNotR12=*/true, PostSuccs, LatchExcl);
  EXPECT_FALSE(NoSpill.isPhysical())
      << "NoSpill misses with the same LatchExcl; occupancy miss";
  EXPECT_NE(NoSpill, Register(Haydn::R2));
  EXPECT_NE(NoSpill, Register(Haydn::R14));

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_TRUE(PreferHasBodyUse)
      << "header BNEZ of Prefer is PreferHasBodyUse; latch BNEZ_W is skipped";
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R2, Preheader, From, Exit, Blocks, TRI))
      << "PreferHasBodyUse stays a separate Exclude";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R2, Haydn::R2, PreferHasBodyUse,
                                    From),
               "non-countdown body use");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R4, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "occupyLatchWindow Exit ST32 of PEI dest r7 is LatchExcl Exit-path";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "unused R14 stays out of skipLatchScrReason LatchExcl";
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R3, Setup.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";
  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_FALSE(Four.isPhysical())
      << "four seats: window-miss is occupancy miss, not last-resort Prefer";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "occupancy miss must not be LatchScr=Prefer countdown";
  EXPECT_NE(Four, Register(Haydn::R3));
  EXPECT_NE(Four, Register(Haydn::R14))
      << "four seats must not steal unused R14";
}

// Header redef kills incoming r14 at latch end, so pass-1a would keep the
// ADDI dest unless the recovered LatchExcl predicate skips the copy-chain.
// Empty Exclude must still skip it; occupancy miss is fail-closed.
// Occupancy stays UseFromSet/DefInTail; do not add a D1.71 overlay-undef case.
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchPass2SkipsHeaderRedefCopyChainOfTailImm) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R7)
      .addReg(Haydn::R14);
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::XOR32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addReg(Haydn::R0);
  Header->addLiveIn(Haydn::R7);
  Latch->addLiveIn(Haydn::R7);
  Exit->addLiveIn(Haydn::R7);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(0);

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "header redef must not hide ADDI dest of live-into-exit COPY dest";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI));
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R14, Blocks));
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI))
      << "mentioned ADDI dest stays in the live-into-exit copy-chain";

  const bool R14Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  const bool R14Def = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R14, Preheader, From, TRI);
  EXPECT_TRUE(R14Def);
  EXPECT_FALSE(R14Use);
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "occupancy stays UseFromSet/DefInTail; do not restamp occupiedAtSet";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            R14Use || (livePhysAtSetOf(Haydn::R14, Setup.getIterator()) &&
                       !R14Def));

  SmallVector<Register, 8> Empty;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Empty, "test");
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "empty Exclude must not keep header-redef copy-chain ADDI dest r14";
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal Exit-read FA r3";
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "empty Exclude must not steal live-into-exit COPY dest r7";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of FA ∪ header-redef copy-chain is occupancy miss";

  SmallVector<Register, 8> FaAndCopyDest;
  FaAndCopyDest.push_back(Haydn::R3);
  FaAndCopyDest.push_back(Haydn::R7);
  Register AfterFaCopy = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, FaAndCopyDest, "test");
  EXPECT_NE(AfterFaCopy, Register(Haydn::R14))
      << "populated Exclude must not keep the hidden ADDI dest r14";
  EXPECT_NE(AfterFaCopy, Register(Haydn::R3))
      << "populated Exclude must not keep Exit-read FA r3";
  EXPECT_FALSE(AfterFaCopy.isPhysical())
      << "header-redef ADDI dest is occupancy miss, not a steal";

  SmallVector<Register, 16> Family;
  excludeFaAndTailImmFamily(Family, From);
  bool FamilyHasR3 = false;
  bool FamilyHasR14 = false;
  bool FamilyHasR7 = false;
  for (Register R : Family) {
    FamilyHasR3 |= R == Haydn::R3;
    FamilyHasR14 |= R == Haydn::R14;
    FamilyHasR7 |= R == Haydn::R7;
  }
  EXPECT_TRUE(FamilyHasR3) << "flood LatchExcl must contain FA r3";
  EXPECT_TRUE(FamilyHasR14)
      << "copy-chain flood must Exclude ADDI dest r14 after header redef";
  EXPECT_TRUE(FamilyHasR7) << "flood LatchExcl must contain COPY dest r7";
  Register AfterFamily = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Family, "test");
  EXPECT_NE(AfterFamily, Register(Haydn::R3))
      << "pass-2 must not keep r3=sp+off as LatchScr";
  EXPECT_NE(AfterFamily, Register(Haydn::R14))
      << "pass-2 must not keep header-redef r14 as LatchScr";
  EXPECT_NE(AfterFamily, Register(Haydn::R7));
  EXPECT_FALSE(AfterFamily.isPhysical())
      << "after FA and copy-chain flood the window misses";

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)")
      << "header redef must not hide ADDI dest in skipLatchScrReason";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";
  SmallVector<Register, 16> SkipExcl;
  excludeSkipLatchScr(SkipExcl, From, Haydn::R2, PreferHasBodyUse);
  EXPECT_TRUE(exclContains(SkipExcl, Haydn::R14))
      << "skipLatchScrReason LatchExcl keeps header-redef ADDI dest";
  EXPECT_TRUE(exclContains(SkipExcl, Haydn::R3));
  EXPECT_TRUE(exclContains(SkipExcl, Haydn::R7));
  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_NE(Four, Register(Haydn::R14))
      << "four seats: header redef must not hide ADDI dest r14";
  EXPECT_NE(Four, Register(Haydn::R3))
      << "four seats must not steal FA r3";
  EXPECT_NE(Four, Register(Haydn::R7));
  EXPECT_TRUE(PreferHasBodyUse)
      << "occupancy miss is not last-resort Prefer; PreferHasBodyUse must stay "
         "true (header-vs-latch / latch XOR of Prefer)";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
  EXPECT_FALSE(Four.isPhysical())
      << "after unsound LatchExcl the window misses; occupancy miss";
}

// Bundled-SET last-def of live-into-exit ADDI r0,imm plus FA r3. Ins is
// the BUNDLE root. std::next(BUNDLE) would drop r14 from a caller
// LatchExcl scan; the picker recovers SET and still skips r14.
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchPass2SkipsBundledSetTailImmLastDef) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  addUnmentionedLiveThrough(Haydn::R7);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);

  MachineInstr *Bund =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *Addi =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::ADDI32), Haydn::R14)
          .addReg(Haydn::R0)
          .addImm(2)
          .getInstr();
  Addi->bundleWithPred();
  MachineInstr *Setup =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::SET_HWLOOP_F2_W))
          .addImm(0)
          .addMBB(Header)
          .addMBB(Latch)
          .addReg(Haydn::R2)
          .addReg(Haydn::SFR, RegState::ImplicitDefine)
          .getInstr();
  Setup->bundleWithPred();
  finalizeBundle(*Preheader, Bund->getIterator());
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineInstr &Root = haydn::hwloop::topLevelForLayout(*Setup);
  ASSERT_TRUE(Root.isBundle());
  MachineBasicBlock::const_iterator From(Root.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "bundled-SET last-def rewind must see coissued ADDI r0,imm";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "keep FA vs tail-imm: r14=ADDI r0,imm is not sp+off";

  SmallVector<Register, 8> Empty;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Empty, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal unmentioned Exit-read FA r3";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "empty Exclude must not steal bundled live-into-exit tail-imm r14";
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "must not steal unmentioned live-through r7 after FA ∪ tail-imm skip";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of FA ∪ bundled tail-imm is occupancy miss, not a steal";

  auto AfterBund = std::next(From);
  SmallVector<Register, 8> NextOnly;
  NextOnly.push_back(Haydn::R3);
  NextOnly.push_back(Haydn::R7);
  if (AfterBund != Preheader->end() &&
      haydn::hwloop::regIsLiveIntoExitTailImm(Haydn::R14, Preheader, AfterBund,
                                             Exit, TRI))
    NextOnly.push_back(Haydn::R14);
  Register AfterNext = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, NextOnly, "test");
  EXPECT_NE(AfterNext, Register(Haydn::R14))
      << "populated Exclude recovers SET; pass-2 must not keep bundled tail-imm r14";
  EXPECT_NE(AfterNext, Register(Haydn::R3))
      << "populated Exclude must not keep Exit-read FA r3";
  EXPECT_FALSE(AfterNext.isPhysical())
      << "std::next(BUNDLE) miss is occupancy miss, not a steal of r14";

  SmallVector<Register, 16> Family;
  excludeFaAndTailImmFamily(Family, From);
  Register AfterFamily = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Family, "test");
  EXPECT_NE(AfterFamily, Register(Haydn::R3))
      << "pass-2 must not keep r3=sp+off as LatchScr";
  EXPECT_NE(AfterFamily, Register(Haydn::R14))
      << "pass-2 must not keep bundled-SET tail-imm r14 as LatchScr";
  EXPECT_NE(AfterFamily, Register(Haydn::R7))
      << "must not steal the next unmentioned live-through";
  EXPECT_FALSE(AfterFamily.isPhysical())
      << "next unmentioned live-through is occupancy miss, not a pass-2 steal";

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "unmentioned live-through with Exit-path read is LatchExcl";
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R14, Root.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";
  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_NE(Four, Register(Haydn::R7))
      << "four seats must not steal unmentioned live-through r7";
  EXPECT_FALSE(Four.isPhysical())
      << "four seats: FA ∪ tail-imm skip is occupancy miss, not a pass-2 steal";
  EXPECT_NE(Four, Register(Haydn::R3))
      << "four seats must not steal Exit-read FA r3";
  EXPECT_NE(Four, Register(Haydn::R14))
      << "four seats must not steal bundled-SET tail-imm r14";
  EXPECT_TRUE(PreferHasBodyUse)
      << "occupancy miss is not last-resort Prefer; PreferHasBodyUse must stay "
         "true (header-vs-latch / latch XOR of Prefer)";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
}

// BUNDLE Exit use of live-into-exit r14 plus FA r3. LatchExcl from the
// BUNDLE-header read must not keep r3 or r14.
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchPass2SkipsTailImmWithBundleExitUse) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R7, Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);
  MachineInstr *Bund =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *Nop =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::NOP)).getInstr();
  Nop->bundleWithPred();
  finalizeBundle(*Exit, Bund->getIterator());
  MachineInstr &BundleRoot = haydn::hwloop::topLevelForLayout(*Nop);
  ASSERT_TRUE(BundleRoot.isBundle());
  BundleRoot.addOperand(
      *MF, MachineOperand::CreateReg(Haydn::R14, /*isDef=*/false, /*isImp=*/true));

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "BUNDLE header implicit use is an Exit-path read";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI));

  SmallVector<Register, 16> Family;
  excludeFaAndTailImmFamily(Family, From);
  Register AfterFamily = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Family, "test");
  EXPECT_NE(AfterFamily, Register(Haydn::R3))
      << "pass-2 must not keep r3=sp+off as LatchScr";
  EXPECT_NE(AfterFamily, Register(Haydn::R14))
      << "pass-2 must not keep BUNDLE-use r14 as LatchScr";
  EXPECT_FALSE(AfterFamily.isPhysical())
      << "after FA and BUNDLE-use tail-imm the window misses";

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_FALSE(Four.isPhysical())
      << "four seats: FA ∪ BUNDLE-use tail-imm is occupancy miss";
  EXPECT_NE(Four, Register(Haydn::R3));
  EXPECT_NE(Four, Register(Haydn::R14));
  EXPECT_TRUE(PreferHasBodyUse)
      << "occupancy miss is not last-resort Prefer; PreferHasBodyUse must stay "
         "true (header-vs-latch / latch XOR of Prefer)";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
}

// COPY/MOVE of a coissued after-SET ADDI r0,imm. Production Ins is the
// BUNDLE root or the SET member; afterSetTailStart must rewind so the
// ADDI last-def is visible. std::next(BUNDLE) skips it and the MOVE dest
// is no longer this class. Occupancy stays UseFromSet/DefInTail.
TEST_F(HaydnHWLoopDemoteTest,
       LiveIntoExitTailImmBundledSetCopyMoveRewindFromSetMember) {
  MachineInstr *Bund =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *Addi =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::ADDI32), Haydn::R14)
          .addReg(Haydn::R0)
          .addImm(2)
          .getInstr();
  Addi->bundleWithPred();
  MachineInstr *Setup =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::SET_HWLOOP_F2_W))
          .addImm(0)
          .addMBB(Header)
          .addMBB(Latch)
          .addReg(Haydn::R2)
          .addReg(Haydn::SFR, RegState::ImplicitDefine)
          .getInstr();
  Setup->bundleWithPred();
  finalizeBundle(*Preheader, Bund->getIterator());
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R7)
      .addReg(Haydn::R14);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineInstr &Root = haydn::hwloop::topLevelForLayout(*Setup);
  ASSERT_TRUE(Root.isBundle());
  ASSERT_TRUE(Setup->isBundledWithPred());
  MachineBasicBlock::const_iterator FromBund(Root.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, FromBund, Exit, TRI))
      << "BUNDLE-root Ins must rewind to the coissued ADDI last-def";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, FromBund, Exit, TRI))
      << "MOVE of coissued ADDI r0,imm is the same steal";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, FromBund, TRI))
      << "keep FA: r0,imm is not sp+off";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, FromBund, TRI));
  auto AfterBund = std::next(FromBund);
  ASSERT_NE(AfterBund, Preheader->end());
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, AfterBund, Exit, TRI))
      << "std::next(BUNDLE) skips the coissued ADDI last-def";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, AfterBund, Exit, TRI))
      << "MOVE dest is not tail-imm without the coissued ADDI last-def";
  EXPECT_TRUE(haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader,
                                                    FromBund, TRI));
  EXPECT_FALSE(haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, FromBund, TRI));
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Root.getIterator()))
      << "occupancy stays UseFromSet/DefInTail; do not restamp occupiedAtSet";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Root.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R14, Preheader, FromBund, TRI) ||
                (livePhysAtSetOf(Haydn::R14, Root.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader,
                                                        FromBund, TRI)))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2,
                                    /*PreferHasBodyUse=*/false, FromBund),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2,
                                    /*PreferHasBodyUse=*/false, FromBund),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R14, Haydn::R2,
                                      /*PreferHasBodyUse=*/false, FromBund)),
            occupiedAtSetOf(Haydn::R14, Root.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";
}

// cfgPathReadsPhysReg BUNDLE uses: Exit-path read is a bundled member of
// the COPY dest, not a top-level MI and not the ADDI dest. A walk that
// skips interiors misses the family.
TEST_F(HaydnHWLoopDemoteTest, LiveIntoExitTailImmCopyDestExitBundledMemberUse) {
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(),
          TII().get(TargetOpcode::COPY), Haydn::R7)
      .addReg(Haydn::R14);
  MachineInstr *Bund =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *St =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
          .addReg(Haydn::R7)
          .addReg(Haydn::R13)
          .addImm(0)
          .getInstr();
  St->bundleWithPred();
  (void)Bund;
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI))
      << "cfgPathReadsPhysReg must see bundled member use of the COPY dest";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "ADDI dest stays in the family when the COPY dest is a bundled Exit use";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI));
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2,
                                    /*PreferHasBodyUse=*/false, From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2,
                                    /*PreferHasBodyUse=*/false, From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
}

// CImm ADDI r0,imm is tail-imm, not FA. CImm r3=sp+off stays FA. Pass-2
// LatchExcl via unsound cannot keep r3 or r14. Occupancy stays DefInTail.
TEST_F(HaydnHWLoopDemoteTest,
       LiveIntoExitTailImmCImmIsNotFrameAddressPass2CannotKeepR14) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  addUnmentionedLiveThrough(Haydn::R7);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addCImm(ConstantInt::get(*Ctx, APInt(32, 16)));
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  MachineInstr &TailImm =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
               Haydn::R14)
           .addReg(Haydn::R0)
           .addCImm(ConstantInt::get(*Ctx, APInt(32, 2)));
  ASSERT_TRUE(TailImm.getOperand(2).isCImm());
  ASSERT_FALSE(TailImm.getOperand(2).isImm());
  BuildMI(*Preheader, Preheader->end(), DebugLoc(),
          TII().get(TargetOpcode::COPY), Haydn::R7)
      .addReg(Haydn::R14);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R14)
      .addReg(Haydn::R13)
      .addImm(0);

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "CImm r3=sp+off is FA last-def";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R3, Preheader, From, Exit, TRI))
      << "CImm FA is not ADDI r0,imm";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R14, Preheader, From, TRI))
      << "CImm ADDI r0,imm is not FA";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "CImm ADDI r0,imm live into Exit is tail-imm";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI))
      << "COPY of CImm ADDI dest is the same steal";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R3, Preheader, From, Exit, Blocks, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R7, Preheader, From, Exit, Blocks, TRI));
  EXPECT_TRUE(haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader, From,
                                                    TRI));
  EXPECT_FALSE(haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, From, TRI));
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "occupancy stays UseFromSet/DefInTail; CImm ADDI is DefInTail";
  EXPECT_NE(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            haydn::hwloop::regMentionedInPreheaderTail(Haydn::R14, Preheader,
                                                       From, TRI))
      << "occupancy is not any-mention; do not restamp occupiedAtSet";

  SmallVector<Register, 8> Empty;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Empty, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal unmentioned CImm FA r3";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "empty Exclude must not steal CImm tail-imm r14";
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "empty Exclude must not steal COPY dest of CImm ADDI";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of CImm FA ∪ tail-imm is occupancy miss";

  SmallVector<Register, 16> Family;
  excludeUnsoundLatchScr(Family, From);
  Register AfterFamily = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Family, "test");
  EXPECT_NE(AfterFamily, Register(Haydn::R3))
      << "pass-2 must not keep CImm r3=sp+off as LatchScr";
  EXPECT_NE(AfterFamily, Register(Haydn::R14))
      << "pass-2 must not keep CImm ADDI r14 as LatchScr";
  EXPECT_NE(AfterFamily, Register(Haydn::R7))
      << "pass-2 must not keep COPY dest of CImm ADDI as LatchScr";
  EXPECT_FALSE(AfterFamily.isPhysical())
      << "after unsound LatchExcl the window misses; occupancy miss";

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R14, Setup.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Setup.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R14, Preheader, From, TRI) ||
                (livePhysAtSetOf(Haydn::R14, Setup.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader,
                                                        From, TRI)))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_FALSE(Four.isPhysical())
      << "four seats: CImm FA ∪ tail-imm is occupancy miss, not last-resort "
         "Prefer";
  EXPECT_NE(Four, Register(Haydn::R3));
  EXPECT_NE(Four, Register(Haydn::R14));
  EXPECT_NE(Four, Register(Haydn::R7));
  EXPECT_TRUE(PreferHasBodyUse)
      << "occupancy miss is not last-resort Prefer; PreferHasBodyUse must stay "
         "true (header-vs-latch / latch XOR of Prefer)";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
}

// Combined O2 residual: COPY/MOVE of coissued bundled-SET ADDI r0,imm,
// header redef of the ADDI dest, Exit BUNDLE member use of the COPY dest,
// FA r3=sp+off vs SMS-guard ADDI with no Exit read. LatchExcl is
// regIsUnsoundLatchScratchAtSet. Occupancy stays UseFromSet/DefInTail.
// Header COPY r14,r7 is dest-only for r14 — do not invert CB-165 into a use.
TEST_F(HaydnHWLoopDemoteTest,
       UnsoundLatchScratchPass2O2CopyBundledSetHeaderRedefBundleExit) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);

  MachineInstr *Bund =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *Addi =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::ADDI32), Haydn::R14)
          .addReg(Haydn::R0)
          .addImm(2)
          .getInstr();
  Addi->bundleWithPred();
  MachineInstr *Setup =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::SET_HWLOOP_F2_W))
          .addImm(0)
          .addMBB(Header)
          .addMBB(Latch)
          .addReg(Haydn::R2)
          .addReg(Haydn::SFR, RegState::ImplicitDefine)
          .getInstr();
  Setup->bundleWithPred();
  finalizeBundle(*Preheader, Bund->getIterator());
  BuildMI(*Preheader, Preheader->end(), DebugLoc(),
          TII().get(TargetOpcode::COPY), Haydn::R7)
      .addReg(Haydn::R14, RegState::Kill);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R8)
      .addReg(Haydn::R0)
      .addImm(1);
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(TargetOpcode::COPY),
          Haydn::R14)
      .addReg(Haydn::R7);
  Header->addLiveIn(Haydn::R7);
  Latch->addLiveIn(Haydn::R7);
  Exit->addLiveIn(Haydn::R7);
  MachineInstr *ExitBund =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *St =
      BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
          .addReg(Haydn::R7)
          .addReg(Haydn::R13)
          .addImm(0)
          .getInstr();
  St->bundleWithPred();
  (void)ExitBund;

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineInstr &Root = haydn::hwloop::topLevelForLayout(*Setup);
  ASSERT_TRUE(Root.isBundle());
  ASSERT_TRUE(Setup->isBundledWithPred());
  MachineBasicBlock::const_iterator From(Root.getIterator());

  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "FA r3=sp+off vs SMS-guard: r3 is FA";
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R3, Preheader, From, Exit, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R3, Preheader, From, Exit, Blocks, TRI))
      << "unmentioned FA r3 that Exit reads (live-through ST32) is unsound";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R14, Preheader, From, Exit, TRI))
      << "header redef must not hide coissued ADDI dest of a bundled Exit COPY dest";
  EXPECT_TRUE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R7, Preheader, From, Exit, TRI))
      << "cfgPathReadsPhysReg must see bundled member use of the COPY dest";
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R14, Preheader, From, Exit, Blocks, TRI));
  EXPECT_TRUE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R7, Preheader, From, Exit, Blocks, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsLiveIntoExitTailImm(
      Haydn::R8, Preheader, From, Exit, TRI))
      << "SMS-guard ADDI r0,imm with no Exit read is not live-into-exit";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R8, Preheader, From, TRI));
  EXPECT_FALSE(haydn::hwloop::regIsUnsoundLatchScratchAtSet(
      Haydn::R8, Preheader, From, Exit, Blocks, TRI))
      << "SMS-guard ADDI with no Exit read stays a sound latch temp";
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R14, Blocks))
      << "header $r14=COPY $r7 is a LoopBlocks mention";
  EXPECT_TRUE(haydn::hwloop::regClobberedNonCountdownIn(Haydn::R14, Blocks))
      << "header COPY dest is a CB-165 defs-only clobber";
  EXPECT_FALSE(haydn::hwloop::regUsedNonCountdownIn(Haydn::R14, Blocks, Latch))
      << "do not invert CB-165 dest-only COPY into a use";
  EXPECT_TRUE(haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader, From,
                                                    TRI));
  EXPECT_FALSE(haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, From, TRI));
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Root.getIterator()))
      << "occupancy stays UseFromSet/DefInTail; do not restamp occupiedAtSet";
  EXPECT_NE(occupiedAtSetOf(Haydn::R14, Root.getIterator()),
            haydn::hwloop::regMentionedInPreheaderTail(Haydn::R14, Preheader,
                                                       From, TRI))
      << "occupancy is not any-mention";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Root.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R14, Preheader, From, TRI) ||
                (livePhysAtSetOf(Haydn::R14, Root.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader,
                                                        From, TRI)))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";

  SmallVector<Register, 16> Family;
  excludeUnsoundLatchScr(Family, From);
  Register AfterFamily = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Family, "test");
  EXPECT_NE(AfterFamily, Register(Haydn::R3))
      << "pass-2 must not keep r3=sp+off as LatchScr";
  EXPECT_NE(AfterFamily, Register(Haydn::R14))
      << "pass-2 must not keep header-redef ADDI dest r14 as LatchScr";
  EXPECT_NE(AfterFamily, Register(Haydn::R7))
      << "pass-2 must not keep COPY dest of coissued ADDI as LatchScr";
  EXPECT_EQ(AfterFamily, Register(Haydn::R8))
      << "SMS-guard ADDI with no Exit read remains a sound pass-1a LatchScr";

  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)");
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R8, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "SMS-guard stays out of LatchExcl";
  EXPECT_NE(bool(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse,
                                      From)),
            occupiedAtSetOf(Haydn::R14, Root.getIterator()))
      << "do not fold occupiedAtSet into skipLatchScrReason; DefInTail "
         "tail-imm is unsound and occupancy-false";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Root.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R14, Preheader, From, TRI) ||
                (livePhysAtSetOf(Haydn::R14, Root.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R14, Preheader,
                                                        From, TRI)))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R3, Root.getIterator()),
            haydn::hwloop::regUsedFromSetInPreheaderTail(
                Haydn::R3, Preheader, From, TRI) ||
                (livePhysAtSetOf(Haydn::R3, Root.getIterator()) &&
                 !haydn::hwloop::regDefdInPreheaderTail(Haydn::R3, Preheader,
                                                        From, TRI)))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  Register Four = pickLatchScrFourSeats(From, Haydn::R2, PreferHasBodyUse);
  EXPECT_EQ(Four, Register(Haydn::R8))
      << "four seats: SMS-guard stays pass-1a after FA ∪ tail-imm Exclude";
  EXPECT_NE(Four, Register(Haydn::R3))
      << "four seats must not steal Exit-read FA r3";
  EXPECT_NE(Four, Register(Haydn::R14))
      << "four seats must not steal header-redef tail-imm r14";
  EXPECT_NE(Four, Register(Haydn::R7));
  EXPECT_TRUE(PreferHasBodyUse)
      << "occupancy miss is not last-resort Prefer; PreferHasBodyUse must stay "
         "true (header-vs-latch / latch XOR of Prefer)";
  EXPECT_NE(Four, Register(Haydn::R2))
      << "PreferHasBodyUse last-resort LatchScr=Prefer refused";
}

// va-arg-22 coissue: SET is not a scheduling boundary. r3=sp+off can sit
// in the same bundle as SET (glueDefToUse places SET last). The walker
// must see that member; skipping the whole setup bundle hid the address
// scratch and let LatchScr copy/SUBI clobber it. SET reading Prefer is
// still not a tail mention.
TEST_F(HaydnHWLoopDemoteTest, SetupBundleLaterMemberIsTailMention) {
  MachineInstr *Bund =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *Addr =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::ADDI32), Haydn::R7)
          .addReg(Haydn::R13)
          .addImm(8)
          .getInstr();
  Addr->bundleWithPred();
  MachineInstr *Setup =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::SET_HWLOOP_F2_W))
          .addImm(0)
          .addMBB(Header)
          .addMBB(Latch)
          .addReg(Haydn::R2)
          .addReg(Haydn::SFR, RegState::ImplicitDefine)
          .getInstr();
  Setup->bundleWithPred();
  finalizeBundle(*Preheader, Bund->getIterator());
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Preheader->begin());
  ASSERT_TRUE(From->isBundle());
  EXPECT_TRUE(haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R7, Preheader, From, TRI))
      << "coissued ADDI of R7 in the SET bundle is the va-arg-22 tail";
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI))
      << "coissued r7=sp+off must be refused as LatchScr";
  EXPECT_FALSE(haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R2, Preheader, From, TRI))
      << "SET reading Prefer is not a tail mention";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R2, Preheader, From, TRI))
      << "SET reading Prefer is not a frame-address scratch";
  EXPECT_FALSE(haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R8, Preheader, From, TRI));
}

// Caller passes SET itself (unbundled). r3=sp+off before SET is the
// pass-2 unmentioned live-through steal (va-arg-22). Starting the walk
// at std::next(SET) hid this def.
TEST_F(HaydnHWLoopDemoteTest, FrameAddressReachingDefBeforeSet) {
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  MachineInstr &Setup =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(),
               TII().get(Haydn::SET_HWLOOP_F2_W))
           .addImm(0)
           .addMBB(Header)
           .addMBB(Latch)
           .addReg(Haydn::R2)
           .addReg(Haydn::SFR, RegState::ImplicitDefine);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "r3=sp+off before SET must refuse LatchScr";
  auto Blocks = loopBlocks();
  EXPECT_FALSE(haydn::hwloop::isSoundDemoteCounter(
      Haydn::R3, Blocks, Preheader, From, *MF, TRI))
      << "reaching-def-before-SET r3=sp+off is not a sound CountReg";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI));
}

// Post-RA COPY of SP is the same address scratch as MOVE32/ADDI.
TEST_F(HaydnHWLoopDemoteTest, FrameAddressCopyFromSpBeforeSet) {
  BuildMI(*Preheader, Preheader->end(), DebugLoc(),
          TII().get(TargetOpcode::COPY), Haydn::R3)
      .addReg(Haydn::R13);
  MachineInstr &Setup =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(),
               TII().get(Haydn::SET_HWLOOP_F2_W))
           .addImm(0)
           .addMBB(Header)
           .addMBB(Latch)
           .addReg(Haydn::R2)
           .addReg(Haydn::SFR, RegState::ImplicitDefine);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "COPY r3, sp before SET is a frame-address scratch";
}

TEST_F(HaydnHWLoopDemoteTest, FrameAddressMoveFromSpBeforeSet) {
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R3)
      .addReg(Haydn::R13);
  MachineInstr &Setup =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(),
               TII().get(Haydn::SET_HWLOOP_F2_W))
           .addImm(0)
           .addMBB(Header)
           .addMBB(Latch)
           .addReg(Haydn::R2)
           .addReg(Haydn::SFR, RegState::ImplicitDefine);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "MOVE32 r3, sp before SET is a frame-address scratch";
}

// Coissued ADDI before SET in the same bundle must be visible when Ins is
// the BUNDLE root (topLevelForLayout of SET). glueDefToUse places SET last.
TEST_F(HaydnHWLoopDemoteTest, FrameAddressCoissueBeforeSetInBundle) {
  MachineInstr *Bund =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *Addr =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::ADDI32), Haydn::R3)
          .addReg(Haydn::R13)
          .addImm(8)
          .getInstr();
  Addr->bundleWithPred();
  MachineInstr *Setup =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::SET_HWLOOP_F2_W))
          .addImm(0)
          .addMBB(Header)
          .addMBB(Latch)
          .addReg(Haydn::R2)
          .addReg(Haydn::SFR, RegState::ImplicitDefine)
          .getInstr();
  Setup->bundleWithPred();
  finalizeBundle(*Preheader, Bund->getIterator());
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Preheader->begin());
  ASSERT_TRUE(From->isBundle());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "coissued r3=sp+off before SET in the SET bundle is LatchScr skip";
}

// A frame-address def killed before SET is occupancy 1a, not a pass-2
// skip (bqriir PEI SP+off dest). The caller must keep the dead temp.
TEST_F(HaydnHWLoopDemoteTest, FrameAddressKilledBeforeSetIsDeadTemp) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R7, Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(),
          TII().get(TargetOpcode::KILL))
      .addReg(Haydn::R3, RegState::Kill);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "killed-before-SET r3=sp+off is not a LatchScr skip";

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_EQ(Scr, Register(Haydn::R3))
      << "killed-before-SET r3 stays occupancy 1a, not a pass-2 skip";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "defined dead r3 wins over unused R14";
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R3, Setup.getIterator()))
      << "killed-before-SET r3 is not occupied at SET; no overlay ST";
}

TEST_F(HaydnHWLoopDemoteTest, FrameAddressDeadDefBeforeSetIsDeadTemp) {
  MachineInstr &Addr =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(),
               TII().get(Haydn::ADDI32_W), Haydn::R3)
           .addReg(Haydn::R13)
           .addImm(16);
  Addr.getOperand(0).setIsDead();
  MachineInstr &Setup =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(),
               TII().get(Haydn::SET_HWLOOP_F2_W))
           .addImm(0)
           .addMBB(Header)
           .addMBB(Latch)
           .addReg(Haydn::R2)
           .addReg(Haydn::SFR, RegState::ImplicitDefine);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "dead r3=sp+off dest before SET stays a pass-1a dead temp";
}

// Pred live-through: r3=sp+off in a predecessor, live-in of the preheader,
// no def in the SET block. After FA skip, remaining unmentioned live-through
// is occupancy miss, not a pass-2 steal. Unused live R14 stays skipped.
TEST_F(HaydnHWLoopDemoteTest, FrameAddressPredLiveThrough) {
  MachineBasicBlock *Entry = MF->CreateMachineBasicBlock();
  MF->push_front(Entry);
  Entry->addSuccessor(Preheader);
  BuildMI(*Entry, Entry->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  Entry->addLiveIn(Haydn::R7);

  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  addUnmentionedLiveThrough(Haydn::R7);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);

  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  SmallVector<Register, 8> Excl;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Excl, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal unmentioned pred live-through FA r3";
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "must not steal unmentioned live-through r7 after pred FA skip";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of pred FA r3 is occupancy miss, not a pass-2 steal";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "must not steal unused live R14 after skipping r3";

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "pred live-through r3=sp+off must refuse LatchScr";
  EXPECT_FALSE(haydn::hwloop::isSoundDemoteCounter(
      Haydn::R3, Blocks, Preheader, From, *MF, TRI))
      << "unmentioned pred FA r3 is not a sound CountReg";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI))
      << "unmentioned live-through r7 is not a frame address";
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "addUnmentionedLiveThrough R7 is LatchExcl after pred FA skip";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "unused R14 stays out of skipLatchScrReason LatchExcl";
}

// Pred frame-address killed in the preheader before SET is occupancy 1a,
// not a pass-2 skip: the SET-site value is no longer sp+off.
TEST_F(HaydnHWLoopDemoteTest, FrameAddressPredKilledBeforeSetIsDeadTemp) {
  MachineBasicBlock *Entry = MF->CreateMachineBasicBlock();
  MF->push_front(Entry);
  Entry->addSuccessor(Preheader);
  BuildMI(*Entry, Entry->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  Preheader->addLiveIn(Haydn::R3);

  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R7, Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(),
          TII().get(TargetOpcode::KILL))
      .addReg(Haydn::R3, RegState::Kill);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "pred r3=sp+off killed before SET is not a LatchScr skip";

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_EQ(Scr, Register(Haydn::R3))
      << "killed-before-SET pred r3 stays occupancy 1a, not a pass-2 skip";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "defined dead r3 wins over unused R14";
}

// Last-def matching, not first dest: an earlier SMS-guard ADDI r0,imm must
// not hide a later SUBI32/SUB32/ADD32/ADDI32 r3=sp+off (the O2 miss when
// last-def matching only named ADD). Dest-only redefs stay CB-165.
TEST_F(HaydnHWLoopDemoteTest, FrameAddressLastDefMatchesSubiAddFamily) {
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R3)
      .addReg(Haydn::R0)
      .addImm(2);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::SUBI32),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::SUB32),
          Haydn::R4)
      .addReg(Haydn::R13)
      .addReg(Haydn::R0);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADD32),
          Haydn::R5)
      .addReg(Haydn::R13)
      .addReg(Haydn::R0);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R6)
      .addReg(Haydn::R13)
      .addImm(8);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "last def SUBI32 r3,sp,off is the frame-address skip (not first dest)";
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R4, Preheader, From, TRI))
      << "SUB32 r4,sp,r0 is a frame-address materialize";
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R5, Preheader, From, TRI))
      << "ADD32 r5,sp,r0 is a frame-address materialize";
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R6, Preheader, From, TRI))
      << "ADDI32 r6,sp,off is a frame-address materialize";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI));
}

// Inverse last-def: a later non-frame redef means the SET-site value is
// no longer sp+off. First dest ADDI32_W must not keep the skip.
TEST_F(HaydnHWLoopDemoteTest, FrameAddressLaterNonFrameRedefKillsSkip) {
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R3)
      .addReg(Haydn::R0)
      .addImm(2);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "last def ADDI r0,imm is not a frame address; first dest ADDI32_W "
         "must not keep the skip";
}

// COPY/MOVE of another FA last-def is the same value (va-arg-22 r3 =
// move of an SP+off temp). A later non-frame redef of the source must
// not hide dest.
TEST_F(HaydnHWLoopDemoteTest, FrameAddressCopyOfPriorFaLastDef) {
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R3)
      .addReg(Haydn::R7);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R0)
      .addImm(2);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "r3 = MOVE r7 of r7=sp+off is a frame-address last-def";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI))
      << "later ADDI r0,imm of r7 is not a frame address";
  auto Blocks = loopBlocks();
  EXPECT_FALSE(haydn::hwloop::isSoundDemoteCounter(
      Haydn::R3, Blocks, Preheader, From, *MF, TRI))
      << "COPY-of-FA r3 is not a sound CountReg";
}

TEST_F(HaydnHWLoopDemoteTest, FrameAddressCopyFromSpSrc2Add32) {
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADD32),
          Haydn::R3)
      .addReg(Haydn::R0)
      .addReg(Haydn::R13);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "ADD32 r3, r0, sp is commutable FA (SP in src2)";
}

TEST_F(HaydnHWLoopDemoteTest, FrameAddressLoadFromSpIsNotFa) {
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::LD32),
          Haydn::R3)
      .addReg(Haydn::R13)
      .addImm(0);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "LD32 from SP is a load, not a frame-address materialize";
}

TEST_F(HaydnHWLoopDemoteTest, FrameAddressFromFpWhenHasFP) {
  MF->getFrameInfo().setFrameAddressIsTaken(true);
  ASSERT_TRUE(ST->getFrameLowering()->hasFP(*MF))
      << "frameaddress taken forces hasFP so R14 is FP";
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R3)
      .addReg(Haydn::R14)
      .addImm(8);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "r3=fp+off is a frame-address last-def when hasFP";
}

TEST_F(HaydnHWLoopDemoteTest, FrameAddressFromR14WithoutHasFPIsNotFa) {
  ASSERT_FALSE(ST->getFrameLowering()->hasFP(*MF))
      << "fixture has no FP; R14 is a normal GPR";
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R3)
      .addReg(Haydn::R14)
      .addImm(8);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "ADDI from R14 is not FA when !hasFP";
}

// Pred r7=sp+off, preheader r3=MOVE r7. LastIsFA[r7] is unset in the SET
// block; pred last-def of the COPY source is the same value.
TEST_F(HaydnHWLoopDemoteTest, FrameAddressCopyOfPredFaLastDef) {
  MachineBasicBlock *Entry = MF->CreateMachineBasicBlock();
  MF->push_front(Entry);
  Entry->addSuccessor(Preheader);
  BuildMI(*Entry, Entry->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(16);
  Preheader->addLiveIn(Haydn::R7);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R3)
      .addReg(Haydn::R7);
  addUnmentionedLiveThrough(Haydn::R3);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "r3 = MOVE of pred r7=sp+off is a frame-address last-def";
  auto Blocks = loopBlocks();
  EXPECT_FALSE(haydn::hwloop::isSoundDemoteCounter(
      Haydn::R3, Blocks, Preheader, From, *MF, TRI))
      << "COPY-of-pred-FA r3 is not a sound CountReg";
}

// Pred COPY chain: r7=sp+off then r3=MOVE r7 in the predecessor, no def in
// the SET block. After FA skip of r3, remaining unmentioned live-through is
// occupancy miss, not a pass-2 steal.
TEST_F(HaydnHWLoopDemoteTest, FrameAddressPredCopyChainLiveThrough) {
  MachineBasicBlock *Entry = MF->CreateMachineBasicBlock();
  MF->push_front(Entry);
  Entry->addSuccessor(Preheader);
  BuildMI(*Entry, Entry->end(), DebugLoc(), TII().get(Haydn::ADDI32_W),
          Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(16);
  BuildMI(*Entry, Entry->end(), DebugLoc(), TII().get(Haydn::MOVE32),
          Haydn::R3)
      .addReg(Haydn::R7);
  Entry->addLiveIn(Haydn::R4);

  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R5, Haydn::R6, Haydn::R7, Haydn::R8,
      Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  addUnmentionedLiveThrough(Haydn::R4);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);

  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  SmallVector<Register, 8> Excl;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Excl, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal pred COPY-chain FA r3";
  EXPECT_NE(Scr, Register(Haydn::R4))
      << "must not steal unmentioned live-through r4 after pred COPY-chain FA skip";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of pred COPY-chain r3 is occupancy miss, not a steal";

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::const_iterator From(Setup.getIterator());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "pred r3 = MOVE of r7=sp+off must refuse LatchScr";
  EXPECT_FALSE(haydn::hwloop::isSoundDemoteCounter(
      Haydn::R3, Blocks, Preheader, From, *MF, TRI))
      << "pred COPY-chain r3 is not a sound CountReg";
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R4, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "addUnmentionedLiveThrough R4 is LatchExcl after pred COPY-chain FA skip";
}

// Pass-2 skip of unmentioned $r3=sp+off coissued in the SET bundle.
// Production Ins is topLevelForLayout(SET) = BUNDLE root; a walk that
// starts at std::next(BUNDLE) hid the coissued ADDI32_W member.
TEST_F(HaydnHWLoopDemoteTest,
       PickDeadLatchScratchPass2SkipsSetBundleCoissueFrameAddress) {
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2, Haydn::R4, Haydn::R5, Haydn::R6,
      Haydn::R8, Haydn::R9, Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  addUnmentionedLiveThrough(Haydn::R3);
  addUnmentionedLiveThrough(Haydn::R7);
  Header->addLiveIn(Haydn::R14);
  Latch->addLiveIn(Haydn::R14);
  Exit->addLiveIn(Haydn::R14);

  MachineInstr *Bund =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *Addr =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::ADDI32_W), Haydn::R3)
          .addReg(Haydn::R13)
          .addImm(16)
          .getInstr();
  Addr->bundleWithPred();
  MachineInstr *Setup =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::SET_HWLOOP_F2_W))
          .addImm(0)
          .addMBB(Header)
          .addMBB(Latch)
          .addReg(Haydn::R2)
          .addReg(Haydn::SFR, RegState::ImplicitDefine)
          .getInstr();
  Setup->bundleWithPred();
  finalizeBundle(*Preheader, Bund->getIterator());

  auto Blocks = loopBlocks();
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  SmallVector<Register, 8> Excl;
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, Excl, "test");
  EXPECT_NE(Scr, Register(Haydn::R3))
      << "empty Exclude must not steal coissued SET-bundle FA r3";
  EXPECT_NE(Scr, Register(Haydn::R7))
      << "must not steal unmentioned live-through r7 after coissued FA skip";
  EXPECT_FALSE(Scr.isPhysical())
      << "empty Exclude skip of coissued FA r3 is occupancy miss, not a pass-2 steal";
  EXPECT_NE(Scr, Register(Haydn::R14))
      << "must not steal unused live R14 after skipping r3";

  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  // finalizeBundle prepends a new BUNDLE header; the BuildMI BUNDLE is then
  // a bundled member. Production Ins is topLevelForLayout(SET) = that header.
  MachineBasicBlock::const_iterator From(Preheader->begin());
  ASSERT_TRUE(From->isBundle());
  ASSERT_TRUE(Addr->isBundledWithPred());
  ASSERT_TRUE(Setup->isBundledWithPred());
  EXPECT_TRUE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R3, Preheader, From, TRI))
      << "coissued ADDI32_W r3=sp+off must be visible when Ins is BUNDLE";
  EXPECT_FALSE(haydn::hwloop::regIsPreheaderTailFrameAddress(
      Haydn::R7, Preheader, From, TRI))
      << "unmentioned live-through r7 is not a frame address";
  const bool PreferHasBodyUse =
      haydn::hwloop::regUsedNonCountdownIn(Haydn::R2, Blocks, Latch);
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R3, Haydn::R2, PreferHasBodyUse,
                                    From),
               "preheader-tail frame address (va-arg-22)");
  EXPECT_STREQ(skipLatchScrReasonOf(Haydn::R7, Haydn::R2, PreferHasBodyUse,
                                    From),
               "exit-path read of incoming value")
      << "addUnmentionedLiveThrough R7 is LatchExcl after coissued FA skip";
  EXPECT_EQ(skipLatchScrReasonOf(Haydn::R14, Haydn::R2, PreferHasBodyUse, From),
            nullptr)
      << "unused R14 stays out of skipLatchScrReason LatchExcl";
}

// Occupancy arm (a): a pure tail DEF of a SET-dead CSR (countdown_high_pressure
// R14 / SMS-guard ADDI r0,imm) is not a SET-site use. The any-mention walker
// still sees the def (isSoundDemoteCounter / Prefer-as-LatchScr). Occupancy
// is UseFromSet || (LivePhysRegs-live && !DefInTail): the def kills SET-site
// occupancy. A Header stored live-in with no use is not LivePhysRegs-live
// (D1.71r: stored lists are not occupancy authority).
TEST_F(HaydnHWLoopDemoteTest, PureTailDefKillsSetSiteOccupancy) {
  Header->addLiveIn(Haydn::R14);
  Header->addLiveIn(Haydn::R8);
  MachineInstr &Setup =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(),
               TII().get(Haydn::SET_HWLOOP_F2_W))
           .addImm(0)
           .addMBB(Header)
           .addMBB(Latch)
           .addReg(Haydn::R2)
           .addReg(Haydn::SFR, RegState::ImplicitDefine);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R14)
      .addReg(Haydn::R0)
      .addImm(2);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::iterator Ins = Setup.getIterator();
  const bool R14Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, Ins, TRI);
  const bool R14Def = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R14, Preheader, Ins, TRI);
  const bool R14Mention = haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R14, Preheader, Ins, TRI);
  EXPECT_TRUE(R14Mention)
      << "any-mention walker still sees a pure tail DEF";
  EXPECT_TRUE(R14Def) << "R14 = ADDI r0,2 is a tail DEF";
  EXPECT_FALSE(R14Use) << "pure tail DEF does not use the SET-site value";
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, Ins))
      << "def-in-tail kills SET-site occupancy; overlay ST32 would be undef";
  EXPECT_NE(occupiedAtSetOf(Haydn::R14, Ins), R14Mention)
      << "occupancy is not any-mention; do not fold into "
         "regMentionedInPreheaderTail";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, Ins),
            R14Use || (livePhysAtSetOf(Haydn::R14, Ins) && !R14Def))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  EXPECT_FALSE(haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R8, Preheader, Ins, TRI));
  EXPECT_FALSE(haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R8, Preheader, Ins, TRI));
  EXPECT_FALSE(haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R8, Preheader, Ins, TRI));
  EXPECT_FALSE(livePhysAtSetOf(Haydn::R8, Ins))
      << "D1.71r: unused Header stored live-in is not FPL live-out";
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R8, Ins))
      << "D1.71r: stored successor live-ins are not SET occupancy";
  EXPECT_FALSE(haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R2, Preheader, Ins, TRI))
      << "SET reading Prefer is not a tail mention";
  EXPECT_FALSE(haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R2, Preheader, Ins, TRI));
  EXPECT_FALSE(haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R2, Preheader, Ins, TRI));
  EXPECT_FALSE(haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, Preheader->end(), TRI))
      << "empty tail is not a SET-site use";
  EXPECT_FALSE(haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R14, Preheader, Preheader->end(), TRI));

  // Coissued SET-bundle interior DEF (d171 CSR shape). Production Ins is
  // the BUNDLE root. The any-mention walker still sees the def; UseFromSet
  // does not. Occupancy stays false and is not any-mention.
  Preheader->clear();
  MachineInstr *Bund =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(TargetOpcode::BUNDLE))
          .getInstr();
  MachineInstr *Addi =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::ADDI32), Haydn::R14)
          .addReg(Haydn::R0)
          .addImm(2)
          .getInstr();
  Addi->bundleWithPred();
  MachineInstr *SetMI =
      BuildMI(*Preheader, Preheader->end(), DebugLoc(),
              TII().get(Haydn::SET_HWLOOP_F2_W))
          .addImm(0)
          .addMBB(Header)
          .addMBB(Latch)
          .addReg(Haydn::R2)
          .addReg(Haydn::SFR, RegState::ImplicitDefine)
          .getInstr();
  SetMI->bundleWithPred();
  finalizeBundle(*Preheader, Bund->getIterator());
  MachineBasicBlock::iterator BundIns =
      haydn::hwloop::topLevelForLayout(*SetMI).getIterator();
  ASSERT_TRUE(BundIns->isBundle());
  const bool BundUse = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R14, Preheader, BundIns, TRI);
  const bool BundDef = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R14, Preheader, BundIns, TRI);
  const bool BundMention = haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R14, Preheader, BundIns, TRI);
  EXPECT_TRUE(BundMention);
  EXPECT_TRUE(BundDef);
  EXPECT_FALSE(BundUse)
      << "coissued ADDI r0,imm does not use the SET-site value";
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R14, BundIns))
      << "def-in-tail kills SET-site occupancy; overlay ST32 would be undef";
  EXPECT_NE(occupiedAtSetOf(Haydn::R14, BundIns), BundMention)
      << "occupancy is not any-mention; do not fold into "
         "regMentionedInPreheaderTail";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R14, BundIns),
            BundUse || (livePhysAtSetOf(Haydn::R14, BundIns) && !BundDef))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
}

// Occupancy arm (b): a tail USE whose reaching def is at/before Ins occupies
// the SET site even when the same tail later defs the register. Same-MI
// ADDI rd,rd,imm collects reads vs defs first, so the use wins before the
// def is applied. Cell-(d) copy of LatchScr onto PreheaderScr cannot clobber
// that tail use.
TEST_F(HaydnHWLoopDemoteTest, UseThenDefStaysOccupiedAtSet) {
  MachineInstr &Setup =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::NOP));
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R7)
      .addImm(4);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::iterator Ins = Setup.getIterator();
  const bool R7Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R7, Preheader, Ins, TRI);
  const bool R7Def = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R7, Preheader, Ins, TRI);
  EXPECT_TRUE(haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R7, Preheader, Ins, TRI));
  EXPECT_TRUE(R7Use)
      << "same-MI ADDI rd,rd,imm uses the SET-site value before the def";
  EXPECT_TRUE(R7Def);
  EXPECT_TRUE(occupiedAtSetOf(Haydn::R7, Ins))
      << "use-then-def stays occupied so cell-(d) copy cannot clobber the use";
  EXPECT_TRUE(livePhysAtSetOf(Haydn::R7, Ins))
      << "same-MI use keeps LivePhysRegs live at SET";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R7, Ins),
            R7Use || (livePhysAtSetOf(Haydn::R7, Ins) && !R7Def))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  EXPECT_TRUE(R7Use && R7Def)
      << "UseFromSet is the occupancy arm; DefInTail alone would drop it";
  EXPECT_FALSE(haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R8, Preheader, Ins, TRI));
  EXPECT_FALSE(haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R8, Preheader, Ins, TRI));
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R8, Ins));
}

// D1.129: third successor of the preheader has a use of R7 and empty stored
// live-ins. addLiveOuts would miss it; computed successor live-ins occupy SET.
TEST_F(HaydnHWLoopDemoteTest,
       OccupiedAtSetUsesComputedSuccessorLiveInsNotStored) {
  MachineBasicBlock *Other = MF->CreateMachineBasicBlock();
  MF->push_back(Other);
  Preheader->addSuccessor(Other);
  Other->addSuccessor(Exit);
  BuildMI(*Other, Other->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(0);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  EXPECT_FALSE(Other->isLiveIn(Haydn::R7))
      << "stored live-ins of the third successor stay empty";
  EXPECT_TRUE(occupiedAtSetOf(Haydn::R7, Setup.getIterator()))
      << "computed live-in of a third preheader successor occupies SET";
  EXPECT_TRUE(livePhysAtSetOf(Haydn::R7, Setup.getIterator()));
}

// D1.138: Header→E second-hop. E is a D1.101 early-exit in LoopBlocks,
// uses R5, stored liveins empty. Header does not use R5 and does not
// list it as a stored live-in. llvm::computeLiveIns(Header) seeds from
// Header.liveouts() = successor stored liveins (E empty) and misses R5.
// skipCommon's mentioned-arm admits (body temps stay 1a-eligible; do not
// walk LoopBlocks as Exit-path). Pass-1a / occupiedAtSet seed from FPL
// SeedPristines=false so R5 is occupied. Never Prefer SUBI32/LD32.
TEST_F(HaydnHWLoopDemoteTest, OccupiedAtSetHeaderToEarlyExitSecondHop) {
  MachineBasicBlock *E = MF->CreateMachineBasicBlock();
  MF->insert(Exit->getIterator(), E);
  Header->addSuccessor(E);
  E->addSuccessor(Exit);
  Preheader->addLiveIn(Haydn::R5);
  BuildMI(*E, E->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R5)
      .addReg(Haydn::R13)
      .addImm(0);
  auto Blocks = loopBlocks();
  EXPECT_TRUE(Blocks.contains(E))
      << "D1.101 early-exit-only block must be in LoopBlocks";
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R5, Blocks))
      << "skipCommon mentioned-arm would admit; occupancy is the seed";
  EXPECT_FALSE(Header->isLiveIn(Haydn::R5));
  EXPECT_FALSE(E->isLiveIn(Haydn::R5))
      << "E stored liveins stay empty (BR-split stale-empty shape)";
  LivePhysRegs OneBlock;
  llvm::computeLiveIns(OneBlock, *Header);
  EXPECT_FALSE(OneBlock.contains(Haydn::R5))
      << "one-block computeLiveIns seeds from stored liveouts and misses "
         "the Header→E second-hop";
  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(*MF, /*SeedPristines=*/false);
  EXPECT_TRUE(FPL.isLiveIn(*E, Haydn::R5));
  EXPECT_TRUE(FPL.isLiveIn(*Header, Haydn::R5))
      << "no-seed FPL worklist closes Header→E";
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  EXPECT_TRUE(occupiedAtSetOf(Haydn::R5, Setup.getIterator()))
      << "Header→E second-hop occupies SET";
  EXPECT_TRUE(livePhysAtSetOf(Haydn::R5, Setup.getIterator()));
}

// D1.71r: Header stored live-ins list unused R8. Skip-overlay-era occupancy
// unioned S->liveins() and over-occupied SET. FPL SeedPristines=false is
// the authority; an extra stored name with no use is not live.
TEST_F(HaydnHWLoopDemoteTest, OccupiedAtSetIgnoresStaleStoredSuccessorLiveIn) {
  Header->addLiveIn(Haydn::R8);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  EXPECT_TRUE(Header->isLiveIn(Haydn::R8))
      << "setup: Header stored live-in is the skip-overlay-era extra name";
  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(*MF, /*SeedPristines=*/false);
  EXPECT_FALSE(FPL.isLiveIn(*Header, Haydn::R8))
      << "no-seed FPL does not occupy an unused stored extra name";
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R8, Setup.getIterator()))
      << "D1.71r: stored successor live-ins are not SET occupancy";
  EXPECT_FALSE(livePhysAtSetOf(Haydn::R8, Setup.getIterator()));
}

// D1.71r replacement for the stored-union live-through: Latch uses R5,
// Header has no use and empty stored liveins. addForwardLiveInsTo skips
// the Header→Latch backedge, so FPL.addLiveInsTo must occupy SET.
TEST_F(HaydnHWLoopDemoteTest, OccupiedAtSetLiveThroughLatchWithoutHeaderUse) {
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R5)
      .addReg(Haydn::R13)
      .addImm(0);
  EXPECT_FALSE(Header->isLiveIn(Haydn::R5))
      << "Header stored liveins stay empty; FPL must occupy the live-through";
  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(*MF, /*SeedPristines=*/false);
  EXPECT_TRUE(FPL.isLiveIn(*Latch, Haydn::R5));
  EXPECT_TRUE(FPL.isLiveIn(*Header, Haydn::R5))
      << "Header→Latch live-through with no Header use is FPL live-in";
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  EXPECT_TRUE(occupiedAtSetOf(Haydn::R5, Setup.getIterator()))
      << "D1.71r: FPL live-through occupies SET without stored Header live-in";
  EXPECT_TRUE(livePhysAtSetOf(Haydn::R5, Setup.getIterator()));
}

TEST_F(HaydnHWLoopDemoteTest, PickDeadLatchScratchHeaderToEarlyExitSecondHop) {
  MachineBasicBlock *E = MF->CreateMachineBasicBlock();
  MF->insert(Exit->getIterator(), E);
  Header->addSuccessor(E);
  E->addSuccessor(Exit);
  Preheader->addLiveIn(Haydn::R5);
  BuildMI(*E, E->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R5)
      .addReg(Haydn::R13)
      .addImm(0);
  static const MCPhysReg Occ[] = {
      Haydn::R1, Haydn::R2,  Haydn::R3,  Haydn::R4, Haydn::R6,  Haydn::R7,
      Haydn::R8, Haydn::R9,  Haydn::R10, Haydn::R11, Haydn::R12};
  occupyLatchWindow(Occ);
  MachineInstr &Setup = setHwLoopAtPreheaderEnd();
  auto Blocks = loopBlocks();
  EXPECT_TRUE(Blocks.contains(E));
  EXPECT_TRUE(haydn::hwloop::regMentionedInBlocks(Haydn::R5, Blocks));
  EXPECT_TRUE(haydn::hwloop::hasIncomingValue(Haydn::R5, *MF));
  MachineBasicBlock *PostSuccs[] = {Header, Exit};
  LivePhysRegs OneBlock;
  llvm::computeLiveIns(OneBlock, *Header);
  EXPECT_FALSE(OneBlock.contains(Haydn::R5))
      << "setup: stored-liveout seed would leave R5 1a-available";
  Register Scr = haydn::hwloop::pickDeadLatchScratch(
      *Latch, PostSuccs, Blocks, /*Exclude=*/{}, "test");
  EXPECT_NE(Scr, Register(Haydn::R5))
      << "mentioned Header→E temp must not be pass-1a LatchScr";
  EXPECT_FALSE(Scr.isPhysical())
      << "occupancy miss, never unsaved R14 / pass-2 steal";
  EXPECT_NE(Scr, Register(Haydn::R14));
  Register NoSpill = findPostRAScratchNoSpill(
      *Latch, Latch->end(), /*PreferNotR12=*/true, PostSuccs, {});
  EXPECT_NE(NoSpill, Register(Haydn::R5))
      << "NoSpill twin of the stored-livein second-hop seed";
  EXPECT_TRUE(occupiedAtSetOf(Haydn::R5, Setup.getIterator()));
}

// Arm (b) split-MI: ST32 reads R7, then a later ADDI defs it. The reaching
// def of the store is at/before Ins, so occupancy stays true. The inverse
// (def then store of the new value) is not a SET-site use.
TEST_F(HaydnHWLoopDemoteTest, TailUseThenLaterDefStaysOccupied) {
  MachineInstr &Setup =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::NOP));
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(0);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R0)
      .addImm(1);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::iterator Ins = Setup.getIterator();
  const bool R7Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R7, Preheader, Ins, TRI);
  const bool R7Def = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R7, Preheader, Ins, TRI);
  EXPECT_TRUE(R7Use)
      << "store of R7 before the tail def uses the SET-site value";
  EXPECT_TRUE(R7Def);
  EXPECT_TRUE(occupiedAtSetOf(Haydn::R7, Ins))
      << "use-then-later-def stays occupied";
  EXPECT_TRUE(livePhysAtSetOf(Haydn::R7, Ins))
      << "the store use keeps LivePhysRegs live at SET";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R7, Ins),
            R7Use || (livePhysAtSetOf(Haydn::R7, Ins) && !R7Def))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
  EXPECT_TRUE(R7Use && R7Def)
      << "UseFromSet is the occupancy arm; DefInTail alone would drop it";
}

TEST_F(HaydnHWLoopDemoteTest, DefThenTailUseIsNotUsedFromSet) {
  MachineInstr &Setup =
      *BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::NOP));
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R7)
      .addReg(Haydn::R0)
      .addImm(1);
  BuildMI(*Preheader, Preheader->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R7)
      .addReg(Haydn::R13)
      .addImm(0);
  const TargetRegisterInfo &TRI = *ST->getRegisterInfo();
  MachineBasicBlock::iterator Ins = Setup.getIterator();
  const bool R7Use = haydn::hwloop::regUsedFromSetInPreheaderTail(
      Haydn::R7, Preheader, Ins, TRI);
  const bool R7Def = haydn::hwloop::regDefdInPreheaderTail(
      Haydn::R7, Preheader, Ins, TRI);
  const bool R7Mention = haydn::hwloop::regMentionedInPreheaderTail(
      Haydn::R7, Preheader, Ins, TRI);
  EXPECT_FALSE(R7Use)
      << "store after a tail def reads the tail def, not the SET-site value";
  EXPECT_TRUE(R7Def);
  EXPECT_TRUE(R7Mention);
  EXPECT_FALSE(occupiedAtSetOf(Haydn::R7, Ins))
      << "def-in-tail kills SET-site occupancy; the later use is not at Ins";
  EXPECT_NE(occupiedAtSetOf(Haydn::R7, Ins), R7Mention)
      << "occupancy is not any-mention; do not fold into "
         "regMentionedInPreheaderTail";
  EXPECT_EQ(occupiedAtSetOf(Haydn::R7, Ins),
            R7Use || (livePhysAtSetOf(Haydn::R7, Ins) && !R7Def))
      << "occupancy stays UseFromSet || (LivePhysRegs-live && !DefInTail)";
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

// D1.150: dedicated save home only. Missing or alias with PostRA / BR /
// any counter-pool member is rejected; the helper never returns PostRA as
// a fallback.
TEST(HaydnHWLoopDemotePlacementTest,
     ResolveDemoteSaveHomeDedicatedAndDisjoint) {
  EXPECT_EQ(haydn::hwloop::resolveDemoteSaveHome(
                /*SaveFI=*/2, /*PostRAScratchFI=*/0,
                /*BranchRelaxationScratchFI=*/1, /*StackCounterFI=*/3),
            2);
  EXPECT_EQ(haydn::hwloop::resolveDemoteSaveHome(-1, 0, 1, 3), -1);
  EXPECT_EQ(haydn::hwloop::resolveDemoteSaveHome(0, 0, 1, 3), -1);
  EXPECT_EQ(haydn::hwloop::resolveDemoteSaveHome(1, 0, 1, 3), -1);
  EXPECT_EQ(haydn::hwloop::resolveDemoteSaveHome(3, 0, 1, 3), -1);
  const int Counters[] = {3, 7};
  EXPECT_EQ(haydn::hwloop::resolveDemoteSaveHome(2, 0, 1, ArrayRef(Counters)),
            2);
  EXPECT_EQ(haydn::hwloop::resolveDemoteSaveHome(7, 0, 1, ArrayRef(Counters)),
            -1);
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
// D1.36: countedSoftwareLatchViolation — complete latch-sequence order +
// stack-counter FI home match. Dual-seat with HaydnVerifyBundles (default
// RequireLatch=false; producer uses RequireLatch=true). Discriminator is
// countdown vocabulary (logical SUBI32, or stack-counter LD32+SUBI32+ST32)
// so ordinary branches do not false-fire.
//===----------------------------------------------------------------------===//

// Legal long form: SUBI32 countdown then LUI → ADDI32_W → BEQZ_W → JALR_W.
TEST_F(HaydnHWLoopDemoteTest, CountedLatchLegalLongSequenceIsClean) {
  subi32(Haydn::R5, Haydn::R5, 1);
  luiMBB(Haydn::R2, Header);
  addi32wMBB(Haydn::R2, Haydn::R2, Header);
  beqz(Haydn::R5, Exit);
  jalr(Haydn::R2, Haydn::R2);
  const std::string V = haydn::hwloop::countedSoftwareLatchViolation(*Latch);
  EXPECT_TRUE(V.empty()) << V;
}

// Legal short form: SUBI32 + BNEZ_W + exit B.
TEST_F(HaydnHWLoopDemoteTest, CountedLatchLegalShortSequenceIsClean) {
  subi32(Haydn::R5, Haydn::R5, 1);
  bnez(Haydn::R5, Header);
  bUncond(Exit);
  const std::string V = haydn::hwloop::countedSoftwareLatchViolation(*Latch);
  EXPECT_TRUE(V.empty()) << V;
}

// Swapped LUI/ADDI (the D1.32 ADDI-before-LUI misorder): long-form template
// terms are present but not in LUI → ADDI order.
TEST_F(HaydnHWLoopDemoteTest, CountedLatchSwappedLuiAddiIsViolation) {
  subi32(Haydn::R5, Haydn::R5, 1);
  addi32wMBB(Haydn::R2, Haydn::R2, Header);
  luiMBB(Haydn::R2, Header);
  beqz(Haydn::R5, Exit);
  jalr(Haydn::R2, Haydn::R2);
  const std::string V = haydn::hwloop::countedSoftwareLatchViolation(*Latch);
  EXPECT_FALSE(V.empty()) << "ADDI-before-LUI must fail the complete-order pin";
}

// Follower law: ST32 after the counted BNEZ is a per-iteration SP leak.
TEST_F(HaydnHWLoopDemoteTest, CountedLatchFollowerStoreAfterBnezIsViolation) {
  subi32(Haydn::R5, Haydn::R5, 1);
  bnez(Haydn::R5, Header);
  st32(Haydn::R5, Haydn::R13, 0);
  const std::string V = haydn::hwloop::countedSoftwareLatchViolation(*Latch);
  EXPECT_FALSE(V.empty()) << "ST32 after BNEZ is the demote follower leak";
}

// RequireLatch miss: countdown vocabulary without a counted edge. The Verify
// seat (default) must not false-fire; the producer RequireLatch=true call is
// fail-fast for limited pipelines that skip Verify.
TEST_F(HaydnHWLoopDemoteTest,
       CountedLatchMissingEdgeUnderRequireLatchIsViolation) {
  subi32(Haydn::R5, Haydn::R5, 1);
  EXPECT_TRUE(haydn::hwloop::countedSoftwareLatchViolation(*Latch).empty())
      << "Verify seat must not false-fire on SUBI32 without a counted edge";
  const std::string V =
      haydn::hwloop::countedSoftwareLatchViolation(*Latch,
                                                   /*RequireLatch=*/true);
  EXPECT_FALSE(V.empty())
      << "RequireLatch=true is fail-fast on a missing counted edge";
}

// Stack-counter LD/ST must match an assigned dedicated counter FI (D1.88),
// not membership in PostRAScratchFI / BranchRelaxationScratchFI /
// the demote-save pool.
TEST_F(HaydnHWLoopDemoteTest, CountedLatchStackCounterFiHomeMismatchIsViolation) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FI =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  Frame.setObjectOffset(FI, 0);
  Frame.setStackSize(16);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->addHwLoopStackCounterFI(FI);
  ASSERT_EQ(Info->takeHwLoopStackCounterFI(), FI);
  Info->bindHwLoopStackCounterFI(Latch, FI);

  Register FrameReg;
  const int64_t Off =
      ST->getFrameLowering()->getFrameIndexReference(*MF, FI, FrameReg).getFixed();
  ASSERT_EQ(Off % 4, 0);
  const int64_t HomeElem = Off / 4;
  const int64_t BadElem = HomeElem + 1;

  ld32(Haydn::R5, FrameReg, BadElem);
  subi32(Haydn::R5, Haydn::R5, 1);
  st32(Haydn::R5, FrameReg, BadElem);
  bnez(Haydn::R5, Header);

  const std::string V = haydn::hwloop::countedSoftwareLatchViolation(*Latch);
  EXPECT_FALSE(V.empty())
      << "stack-counter LD/ST must match an assigned dedicated counter home; "
         "got "
      << V;
}

// D1.88: membership in PostRAScratchFI is not a counter home.
TEST_F(HaydnHWLoopDemoteTest,
       CountedLatchStackCounterPostRAMembershipIsNotHome) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FI =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  Frame.setObjectOffset(FI, 0);
  Frame.setStackSize(16);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->setPostRAScratchFI(FI);
  EXPECT_FALSE(Info->isAssignedHwLoopStackCounterFI(FI));

  Register FrameReg;
  const int64_t Off =
      ST->getFrameLowering()->getFrameIndexReference(*MF, FI, FrameReg).getFixed();
  ASSERT_EQ(Off % 4, 0);
  const int64_t HomeElem = Off / 4;

  ld32(Haydn::R5, FrameReg, HomeElem);
  subi32(Haydn::R5, Haydn::R5, 1);
  st32(Haydn::R5, FrameReg, HomeElem);
  bnez(Haydn::R5, Header);

  const std::string V = haydn::hwloop::countedSoftwareLatchViolation(*Latch);
  EXPECT_FALSE(V.empty())
      << "PostRAScratchFI membership must not bless stack-counter LD/ST; got "
      << V;
}

// D1.88: assigned dedicated counter FI with matching elem is a legal home.
TEST_F(HaydnHWLoopDemoteTest, CountedLatchStackCounterAssignedHomeIsLegal) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FI =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  Frame.setObjectOffset(FI, 0);
  Frame.setStackSize(16);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->addHwLoopStackCounterFI(FI);
  ASSERT_EQ(Info->takeHwLoopStackCounterFI(), FI);
  Info->bindHwLoopStackCounterFI(Latch, FI);
  EXPECT_TRUE(Info->isAssignedHwLoopStackCounterFI(FI));

  Register FrameReg;
  const int64_t Off =
      ST->getFrameLowering()->getFrameIndexReference(*MF, FI, FrameReg).getFixed();
  ASSERT_EQ(Off % 4, 0);
  const int64_t HomeElem = Off / 4;

  ld32(Haydn::R5, FrameReg, HomeElem);
  subi32(Haydn::R5, Haydn::R5, 1);
  st32(Haydn::R5, FrameReg, HomeElem);
  bnez(Haydn::R5, Header);

  const std::string V = haydn::hwloop::countedSoftwareLatchViolation(*Latch);
  EXPECT_TRUE(V.empty())
      << "assigned dedicated counter FI must be a legal stack-counter home; got "
      << V;
}

// D1.102: a sibling assigned FI must not bless this latch's LD/ST.
TEST_F(HaydnHWLoopDemoteTest,
       CountedLatchStackCounterSiblingAssignedFiIsNotThisLatchHome) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FIThis =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  const int FISibling =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  Frame.setObjectOffset(FIThis, 0);
  Frame.setObjectOffset(FISibling, 4);
  Frame.setStackSize(16);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->addHwLoopStackCounterFI(FIThis);
  Info->addHwLoopStackCounterFI(FISibling);
  ASSERT_EQ(Info->takeHwLoopStackCounterFI(), FIThis);
  ASSERT_EQ(Info->takeHwLoopStackCounterFI(), FISibling);
  Info->bindHwLoopStackCounterFI(Latch, FIThis);

  Register FrameRegThis, FrameRegSib;
  const int64_t OffThis =
      ST->getFrameLowering()
          ->getFrameIndexReference(*MF, FIThis, FrameRegThis)
          .getFixed();
  const int64_t OffSib =
      ST->getFrameLowering()
          ->getFrameIndexReference(*MF, FISibling, FrameRegSib)
          .getFixed();
  ASSERT_EQ(OffThis % 4, 0);
  ASSERT_EQ(OffSib % 4, 0);
  ASSERT_NE(OffThis / 4, OffSib / 4);

  ld32(Haydn::R5, FrameRegSib, OffSib / 4);
  subi32(Haydn::R5, Haydn::R5, 1);
  st32(Haydn::R5, FrameRegSib, OffSib / 4);
  bnez(Haydn::R5, Header);

  const std::string V = haydn::hwloop::countedSoftwareLatchViolation(*Latch);
  EXPECT_FALSE(V.empty())
      << "sibling assigned FI must not bless this latch; got " << V;
}

// D1.110: baked S_LW/S_SW with a matching home is legal stack-counter glue.
TEST_F(HaydnHWLoopDemoteTest, CountedLatchBakedSlwSswMatchingHomeIsLegal) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FI =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  Frame.setObjectOffset(FI, 0);
  Frame.setStackSize(16);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->addHwLoopStackCounterFI(FI);
  ASSERT_EQ(Info->takeHwLoopStackCounterFI(), FI);
  Info->bindHwLoopStackCounterFI(Latch, FI);

  Register FrameReg;
  const int64_t Off =
      ST->getFrameLowering()->getFrameIndexReference(*MF, FI, FrameReg).getFixed();
  ASSERT_EQ(Off % 4, 0);
  const int64_t HomeElem = Off / 4;

  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::S_LW_WITH_IMM),
          Haydn::R5)
      .addReg(FrameReg)
      .addImm(HomeElem);
  subi32(Haydn::R5, Haydn::R5, 1);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::S_SW_WITH_IMM))
      .addReg(Haydn::R5)
      .addReg(FrameReg)
      .addImm(HomeElem);
  bnez(Haydn::R5, Header);

  const std::string V = haydn::hwloop::countedSoftwareLatchViolation(*Latch);
  EXPECT_TRUE(V.empty())
      << "baked S_LW/S_SW matching this latch's home must be legal; got " << V;
}

// D1.110: baked S_LW/S_SW with a non-matching home is a violation.
TEST_F(HaydnHWLoopDemoteTest, CountedLatchBakedSlwSswMismatchIsViolation) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FI =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  Frame.setObjectOffset(FI, 0);
  Frame.setStackSize(16);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->addHwLoopStackCounterFI(FI);
  ASSERT_EQ(Info->takeHwLoopStackCounterFI(), FI);
  Info->bindHwLoopStackCounterFI(Latch, FI);

  Register FrameReg;
  const int64_t Off =
      ST->getFrameLowering()->getFrameIndexReference(*MF, FI, FrameReg).getFixed();
  ASSERT_EQ(Off % 4, 0);
  const int64_t BadElem = Off / 4 + 1;

  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::S_LW_WITH_IMM),
          Haydn::R5)
      .addReg(FrameReg)
      .addImm(BadElem);
  subi32(Haydn::R5, Haydn::R5, 1);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::S_SW_WITH_IMM))
      .addReg(Haydn::R5)
      .addReg(FrameReg)
      .addImm(BadElem);
  bnez(Haydn::R5, Header);

  const std::string V = haydn::hwloop::countedSoftwareLatchViolation(*Latch);
  EXPECT_FALSE(V.empty())
      << "baked S_LW/S_SW with a non-matching home must violate; got " << V;
}

// D1.150: empty pool peek/take do not create or consume a slot (formed-ZOL
// / refused demote must pay 0).
TEST_F(HaydnHWLoopDemoteTest, DemoteSavePoolEmptyPeekDoesNotConsume) {
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  EXPECT_TRUE(Info->getHwLoopDemoteSavePool().empty());
  EXPECT_EQ(Info->peekHwLoopDemoteSaveFI(), -1);
  EXPECT_EQ(Info->peekHwLoopDemoteSaveFI(), -1);
  EXPECT_EQ(Info->takeHwLoopDemoteSaveFI(), -1);
  EXPECT_TRUE(Info->getAssignedHwLoopDemoteSaveFIs().empty());
  EXPECT_FALSE(Info->isAssignedHwLoopDemoteSaveFI(0));
}

// D1.150: peek does not consume; a refused path that never take()s leaves
// the next home available.
TEST_F(HaydnHWLoopDemoteTest, DemoteSavePoolPeekDoesNotConsume) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FI0 =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  const int FI1 =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->addHwLoopDemoteSaveFI(FI0);
  Info->addHwLoopDemoteSaveFI(FI1);
  EXPECT_EQ(Info->peekHwLoopDemoteSaveFI(), FI0);
  EXPECT_EQ(Info->peekHwLoopDemoteSaveFI(), FI0);
  EXPECT_TRUE(Info->getAssignedHwLoopDemoteSaveFIs().empty());
  EXPECT_FALSE(Info->isAssignedHwLoopDemoteSaveFI(FI0));
  EXPECT_EQ(Info->takeHwLoopDemoteSaveFI(), FI0);
  EXPECT_EQ(Info->peekHwLoopDemoteSaveFI(), FI1);
}

// D1.150: two latches take+bind distinct save homes; they cannot share.
TEST_F(HaydnHWLoopDemoteTest, DemoteSavePoolTakeBindTwoLatchesDistinct) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FI0 =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  const int FI1 =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->addHwLoopDemoteSaveFI(FI0);
  Info->addHwLoopDemoteSaveFI(FI1);
  MachineBasicBlock *Latch2 = MF->CreateMachineBasicBlock();
  MF->push_back(Latch2);
  ASSERT_EQ(Info->takeHwLoopDemoteSaveFI(), FI0);
  Info->bindHwLoopDemoteSaveFI(Latch, FI0);
  ASSERT_EQ(Info->takeHwLoopDemoteSaveFI(), FI1);
  Info->bindHwLoopDemoteSaveFI(Latch2, FI1);
  EXPECT_EQ(Info->getHwLoopDemoteSaveFIForLatch(Latch), FI0);
  EXPECT_EQ(Info->getHwLoopDemoteSaveFIForLatch(Latch2), FI1);
  EXPECT_NE(Info->getHwLoopDemoteSaveFIForLatch(Latch),
            Info->getHwLoopDemoteSaveFIForLatch(Latch2));
}

namespace {
void addFixedStackMMO(MachineFunction &MF, MachineInstr &MI, int FI,
                      MachineMemOperand::Flags Flags) {
  MI.addMemOperand(MF, MF.getMachineMemOperand(
                           MachinePointerInfo::getFixedStack(MF, FI), Flags, 4,
                           MF.getFrameInfo().getObjectAlign(FI)));
}
} // namespace

// D1.150: exact ST32/LD32 FixedStack pair on this latch's bound save FI.
TEST_F(HaydnHWLoopDemoteTest, DemoteSaveHomePairMatchingIsLegal) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FI =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  Frame.setObjectOffset(FI, 0);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->addHwLoopDemoteSaveFI(FI);
  ASSERT_EQ(Info->takeHwLoopDemoteSaveFI(), FI);
  Info->bindHwLoopDemoteSaveFI(Latch, FI);

  MachineInstr &St = st32(Haydn::R5, Haydn::R13, 0);
  addFixedStackMMO(*MF, St, FI, MachineMemOperand::MOStore);
  MachineInstr &Ld =
      *BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::LD32),
               Haydn::R5)
           .addReg(Haydn::R13)
           .addImm(0);
  addFixedStackMMO(*MF, Ld, FI, MachineMemOperand::MOLoad);

  const std::string V = haydn::hwloop::demoteSaveHomePairViolation(*Latch);
  EXPECT_TRUE(V.empty()) << "matching save ST/LD FixedStack pair must be legal; "
                            "got "
                         << V;
}

// D1.150: a sibling latch's save FI does not bless this latch's pair.
TEST_F(HaydnHWLoopDemoteTest, DemoteSaveHomePairSiblingDoesNotBless) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FIThis =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  const int FISibling =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  Frame.setObjectOffset(FIThis, 0);
  Frame.setObjectOffset(FISibling, 4);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->addHwLoopDemoteSaveFI(FIThis);
  Info->addHwLoopDemoteSaveFI(FISibling);
  MachineBasicBlock *Latch2 = MF->CreateMachineBasicBlock();
  MF->push_back(Latch2);
  ASSERT_EQ(Info->takeHwLoopDemoteSaveFI(), FIThis);
  Info->bindHwLoopDemoteSaveFI(Latch, FIThis);
  ASSERT_EQ(Info->takeHwLoopDemoteSaveFI(), FISibling);
  Info->bindHwLoopDemoteSaveFI(Latch2, FISibling);

  MachineInstr &St = st32(Haydn::R5, Haydn::R13, 1);
  addFixedStackMMO(*MF, St, FISibling, MachineMemOperand::MOStore);
  MachineInstr &Ld =
      *BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::LD32),
               Haydn::R5)
           .addReg(Haydn::R13)
           .addImm(1);
  addFixedStackMMO(*MF, Ld, FISibling, MachineMemOperand::MOLoad);

  const std::string V = haydn::hwloop::demoteSaveHomePairViolation(*Latch);
  EXPECT_FALSE(V.empty())
      << "sibling save FI must not bless this latch's pair; got " << V;
}

// D1.150: two latches bound to one save FI is a violation.
TEST_F(HaydnHWLoopDemoteTest, DemoteSaveHomePairSharedBindIsViolation) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FI =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  Frame.setObjectOffset(FI, 0);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->addHwLoopDemoteSaveFI(FI);
  MachineBasicBlock *Latch2 = MF->CreateMachineBasicBlock();
  MF->push_back(Latch2);
  ASSERT_EQ(Info->takeHwLoopDemoteSaveFI(), FI);
  Info->bindHwLoopDemoteSaveFI(Latch, FI);
  Info->bindHwLoopDemoteSaveFI(Latch2, FI);

  MachineInstr &St = st32(Haydn::R5, Haydn::R13, 0);
  addFixedStackMMO(*MF, St, FI, MachineMemOperand::MOStore);
  MachineInstr &Ld =
      *BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::LD32),
               Haydn::R5)
           .addReg(Haydn::R13)
           .addImm(0);
  addFixedStackMMO(*MF, Ld, FI, MachineMemOperand::MOLoad);

  const std::string V = haydn::hwloop::demoteSaveHomePairViolation(*Latch);
  EXPECT_FALSE(V.empty()) << "shared bound save FI must violate; got " << V;
}

// D1.154: save home aliasing this latch's assigned counter FI is a
// violation even when the ST/LD pair is otherwise well-formed.
TEST_F(HaydnHWLoopDemoteTest, DemoteSaveHomePairCounterAliasIsViolation) {
  MachineFrameInfo &Frame = MF->getFrameInfo();
  const int FI =
      Frame.CreateStackObject(/*Size=*/4, Align(4), /*SpillSlot=*/true);
  Frame.setObjectOffset(FI, 0);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->addHwLoopDemoteSaveFI(FI);
  Info->addHwLoopStackCounterFI(FI);
  ASSERT_EQ(Info->takeHwLoopDemoteSaveFI(), FI);
  Info->bindHwLoopDemoteSaveFI(Latch, FI);
  ASSERT_EQ(Info->takeHwLoopStackCounterFI(), FI);
  Info->bindHwLoopStackCounterFI(Latch, FI);

  MachineInstr &St = st32(Haydn::R5, Haydn::R13, 0);
  addFixedStackMMO(*MF, St, FI, MachineMemOperand::MOStore);
  MachineInstr &Ld =
      *BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::LD32),
               Haydn::R5)
           .addReg(Haydn::R13)
           .addImm(0);
  addFixedStackMMO(*MF, Ld, FI, MachineMemOperand::MOLoad);

  const std::string V = haydn::hwloop::demoteSaveHomePairViolation(*Latch);
  EXPECT_FALSE(V.empty())
      << "save home aliasing this latch's counter FI must violate; got " << V;
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

  /// SET_HWLOOP_F2_W sel, header, latch, Prefer — register-trip form
  /// (Prefer is the original trip GPR). D1.37 live-after-exit uses this
  /// so canUsePreferAsCounter cannot win and CountReg is a copy.
  MachineInstr &setHwloopReg(MachineBasicBlock &MBB, MachineBasicBlock *H,
                             MachineBasicBlock *L, Register Prefer) {
    return *BuildMI(MBB, MBB.end(), DebugLoc(),
                    TII().get(Haydn::SET_HWLOOP_F2_W))
                .addImm(0)
                .addMBB(H)
                .addMBB(L)
                .addReg(Prefer)
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
                           Haydn::R7,  Haydn::R6,  Haydn::R5,  Haydn::R4,
                           Haydn::R3,  Haydn::R2,  Haydn::R1,  Haydn::R12};
  for (MCPhysReg R : All)
    addi32In(*Latch, R, 1);
  const std::string Before = mirSnapshot();
  EXPECT_FALSE(demoteHardwareLoopToSoftware(SET, TII(), "D1.51-test"));
  EXPECT_EQ(mirSnapshot(), Before) << "refused demote mutated the function";
}

// Product: no computed-dead JALR scratch is not LLVM ERROR. The
// free-counter arm succeeds (R1 is mentioned nowhere -> CountReg = R1),
// the body overflows simm12 (LongLatch), and every OTHER LongCands
// register is live-through the header. Fall back to short BNEZ; later
// BR relaxes. Dedicated-exit split stays GR1.2/GR1.4. SET is dropped.
TEST_F(HaydnDemoteAtomicityTest, LongLatchNoScratchShortBnezDropsSet) {
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
  // Every LongCands register — including the D1.61-admitted R5/R6 —
  // except R1 (the free CountReg) is read in the latch body, so the
  // guarded transfer marks each live-in of Latch and (through the
  // Header successor) of the post-rewrite out-edge. Stored liveins
  // are a fixture cache only — the owner must not depend on them.
  // R1 is excluded by the CountdownReg identity law. No computed-dead
  // JALR scratch remains; product emits short BNEZ anyway.
  const MCPhysReg Used[] = {Haydn::R11, Haydn::R10, Haydn::R9,  Haydn::R8,
                            Haydn::R7,  Haydn::R6,  Haydn::R5,  Haydn::R4,
                            Haydn::R3,  Haydn::R2,  Haydn::R12};
  for (MCPhysReg R : Used) {
    addi32In(*Latch, R, 1);
    Latch->addLiveIn(R);
  }
  EXPECT_TRUE(demoteHardwareLoopToSoftware(SET, TII(), "D1.51-test"))
      << "no LongScr must drop SET and install short software countdown";
  bool FoundSet = false;
  bool FoundSubi = false;
  bool FoundJalr = false;
  for (MachineInstr &MI : *Preheader)
    if (TII().isHardwareLoopSetupInstr(MI))
      FoundSet = true;
  for (MachineInstr &MI : Latch->instrs()) {
    const StringRef Name = TII().getName(MI.getOpcode());
    if (MI.getOpcode() == Haydn::SUBI32 || Name.contains("SUBI"))
      FoundSubi = true;
    if (Name.contains("JALR"))
      FoundJalr = true;
  }
  EXPECT_FALSE(FoundSet) << "SET must be erased";
  EXPECT_TRUE(FoundSubi) << "software countdown must be installed";
  EXPECT_FALSE(FoundJalr)
      << "no LongScr must not emit the GR2.7 JALR template";
}

// D1.105: Header==Latch with a second live non-Header successor. Drop SET
// and install software countdown; keep Extra (never drop it, never fatal).
// Dedicated-exit split remains GR1.2/GR1.4.
TEST_F(HaydnDemoteAtomicityTest, MultiExitLatchKeepsExtraDropsSet) {
  Header->removeSuccessor(Latch);
  Latch->removeSuccessor(Header);
  Latch->removeSuccessor(Exit);
  Header->addSuccessor(Header);
  Header->addSuccessor(Exit);
  MachineBasicBlock *Extra = MF->CreateMachineBasicBlock();
  MF->push_back(Extra);
  Header->addSuccessor(Extra);
  BuildMI(*Extra, Extra->end(), DebugLoc(), TII().get(Haydn::ADDI32), Haydn::R4)
      .addReg(Haydn::R4)
      .addImm(1);

  MachineInstr &SET = setHwloopImm(*Preheader, Header, Header, 8);
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::PseudoLoopEnd))
      .addMBB(Header);
  BuildMI(*Header, Header->end(), DebugLoc(), TII().get(Haydn::B)).addMBB(Exit);
  EXPECT_TRUE(demoteHardwareLoopToSoftware(SET, TII(), "D1.105-test"))
      << "D1.105 multi-exit must drop SET and install software countdown";
  EXPECT_TRUE(Header->isSuccessor(Extra))
      << "D1.105 must keep Extra rather than drop it";
  bool FoundSet = false;
  bool FoundSubi = false;
  for (MachineInstr &MI : *Preheader)
    if (TII().isHardwareLoopSetupInstr(MI))
      FoundSet = true;
  for (const MachineInstr &MI : Header->instrs()) {
    if (MI.getOpcode() == Haydn::SUBI32 ||
        TII().getName(MI.getOpcode()).contains("SUBI"))
      FoundSubi = true;
  }
  EXPECT_FALSE(FoundSet) << "SET must be erased";
  EXPECT_TRUE(FoundSubi) << "software countdown must be installed";
}

// D1.37: trip-live-after-exit JALR identity. Prefer is a computed live-in
// of Exit (the original trip is consumed after the loop — CB-162 class);
// the body is long enough to overflow simm12 (LongLatch); demote succeeds.
// The JALR link dest must not be the countdown (retired LongScr=CountReg
// identity would clobber the loop-carried trip every backedge and, when
// CountReg==Prefer, zero the post-loop use). LongCands and the Header/Exit
// walk stay untouched; this is a success-path pin, not a picker change.
// Complementary to the D1.51 refusal / nsichneu occupancy arms above.
TEST_F(HaydnDemoteAtomicityTest,
       LongLatchPreferLiveInExitJalrDestIsNotCountdown) {
  const Register Prefer = Haydn::R2;
  MachineInstr &SET = setHwloopReg(*Preheader, Header, Latch, Prefer);
  for (unsigned I = 0; I < 12; ++I) {
    BuildMI(*Latch, Latch->begin(), DebugLoc(), TII().get(Haydn::LOADI64),
            Haydn::D0)
        .addImm(0x1234);
  }
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::PseudoLoopEnd))
      .addMBB(Header);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::B)).addMBB(Exit);
  // Post-loop use of the original trip: Prefer is a computed live-in of
  // Exit (computeBlockLiveIns reverse-walks this ADD32; stored liveins
  // alone are not the LongScr / isLiveAfterLoop probe).
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ADD32), Haydn::R6)
      .addReg(Prefer)
      .addReg(Haydn::R0);
  Exit->addLiveIn(Prefer);

  ASSERT_TRUE(haydn::hwloop::blockLiveInContains(*Exit, Prefer.asMCReg()))
      << "setup: Prefer must be a computed live-in of Exit";

  ASSERT_TRUE(demoteHardwareLoopToSoftware(SET, TII(), "D1.37-test"))
      << "long-latch demote with Prefer live-in of Exit must succeed";

  Register JalrDest, CountdownFromSub, CountdownFromBeqz;
  for (const MachineInstr &MI : Latch->instrs()) {
    if (MI.isBundle() || MI.isMetaInstruction())
      continue;
    // emitExactLate setDesc's to a Format E member; match by name so the
    // pin does not depend on residual logical opcodes surviving commit.
    const StringRef Name = TII().getName(MI.getOpcode());
    if (Name.contains("JALR") && MI.getNumOperands() > 0 &&
        MI.getOperand(0).isReg() && MI.getOperand(0).isDef())
      JalrDest = MI.getOperand(0).getReg();
    else if (Name.contains("SUBI32") && MI.getNumOperands() > 0 &&
             MI.getOperand(0).isReg())
      CountdownFromSub = MI.getOperand(0).getReg();
    else if (Name.contains("BEQZ") && MI.getNumOperands() > 0 &&
             MI.getOperand(0).isReg())
      CountdownFromBeqz = MI.getOperand(0).getReg();
  }
  ASSERT_TRUE(JalrDest.isPhysical())
      << "long body must emit a JALR backedge (LongLatch)";
  const Register Countdown =
      CountdownFromBeqz.isPhysical() ? CountdownFromBeqz : CountdownFromSub;
  ASSERT_TRUE(Countdown.isPhysical())
      << "long latch must test a countdown register";
  if (CountdownFromSub.isPhysical() && CountdownFromBeqz.isPhysical()) {
    EXPECT_EQ(CountdownFromSub, CountdownFromBeqz)
        << "SUBI32 dest and BEQZ src must be the same countdown";
  }
  EXPECT_NE(JalrDest, Countdown)
      << "JALR dest must not alias the countdown (D1.32(3)/D1.37 identity)";
  EXPECT_NE(JalrDest, Prefer)
      << "JALR dest must not clobber the post-loop trip Prefer";
  EXPECT_NE(Countdown, Prefer)
      << "countdown must not be the live-after-exit trip (CB-162 copy)";
}

// Wave 4 H: CFG-changing demote after PostCommitCfgSnapshot is refused.
// Formation unstamped demote stays legal; stamped product refuse leaves
// SET/CFG identical. closeRetainedHwLoops no-ops unstamped and never
// demotes. AIE addPreEmitPass empty (AIE2TargetMachine.cpp:92 / :238-257).
// No extender (Hexagon FixupHwLoops.cpp:136-148).
TEST_F(HaydnDemoteAtomicityTest, UnstampedImmDemoteDropsSet) {
  MachineInstr &SET = setHwloopImm(*Preheader, Header, Latch, 8);
  addi32In(*Latch, Haydn::R4, 1);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::PseudoLoopEnd))
      .addMBB(Header);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::B)).addMBB(Exit);
  EXPECT_FALSE(MF->getInfo<HaydnMachineFunctionInfo>()->hasPostCommitBlockBudget());
  EXPECT_TRUE(demoteHardwareLoopToSoftware(SET, TII(), "Wave4-H-unstamped"))
      << "unstamped demote must install a software latch";
  bool FoundSet = false;
  bool FoundSubi = false;
  for (MachineInstr &MI : *Preheader)
    if (TII().isHardwareLoopSetupInstr(MI))
      FoundSet = true;
  for (const MachineInstr &MI : Latch->instrs()) {
    if (MI.getOpcode() == Haydn::SUBI32 ||
        TII().getName(MI.getOpcode()).contains("SUBI"))
      FoundSubi = true;
  }
  EXPECT_FALSE(FoundSet) << "unstamped demote must erase SET";
  EXPECT_TRUE(FoundSubi) << "unstamped demote must install SUBI32";
}

TEST_F(HaydnDemoteAtomicityTest,
       PostCommitStampRefusesDemoteLeavesFunctionIdentical) {
  MachineInstr &SET = setHwloopImm(*Preheader, Header, Latch, 8);
  addi32In(*Latch, Haydn::R4, 1);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::PseudoLoopEnd))
      .addMBB(Header);
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::B)).addMBB(Exit);
  auto *Info = MF->getInfo<HaydnMachineFunctionInfo>();
  Info->stampPostCommitCfgSnapshot(*MF);
  ASSERT_TRUE(Info->hasPostCommitBlockBudget());
  const std::string Before = mirSnapshot();
  EXPECT_FALSE(demoteHardwareLoopToSoftware(SET, TII(), "Wave4-H"))
      << "stamped demote must refuse";
  EXPECT_EQ(mirSnapshot(), Before) << "stamped demote mutated the function";
}

//===----------------------------------------------------------------------===//
// D1.61: FunctionPhysLiveness — the whole-function fixed point behind
// late scratch picking. Pins the three laws the row closes:
//   1. TRANSITIVE: a value live only THROUGH a farther join block (its
//      direct successors all redefine it on one path) is still live —
//      the retired stored-livein-preference/stale-fallback hybrid
//      called it dead;
//   2. R5/R6 are reachable demote scratch candidates when proven dead
//      (the demote CandsGPR omission);
//   3. The post-rewrite obligation query ignores dropped pre-rewrite
//      successors (the nsichneu Header==Latch early-exit class) while
//      keeping the self-edge loop-carried set from the restricted
//      transfer, never from stored MBB liveins.
//===----------------------------------------------------------------------===//

TEST_F(HaydnHWLoopDemoteTest, FixedPointIsTransitiveThroughJoinBlocks) {
  // The retired hybrid's blind spot: a value whose ONLY use sits past a
  // join (Tail), reached through a middle block (Mid) that redefines it
  // on THIS path — while ANOTHER predecessor of Tail (Keep) does not
  // redefine it, so the value flows Latch -> Mid-or-Keep -> Tail. The
  // one-block walk of Mid (defs kill) and Mid's stored liveins (empty)
  // both called the register dead at Latch; the fixed point sees the
  // Keep path.
  MachineBasicBlock *Mid = MF->CreateMachineBasicBlock();
  MachineBasicBlock *Keep = MF->CreateMachineBasicBlock();
  MachineBasicBlock *Tail = MF->CreateMachineBasicBlock();
  MF->push_back(Mid);
  MF->push_back(Keep);
  MF->push_back(Tail);
  Latch->addSuccessor(Mid);
  Latch->addSuccessor(Keep);
  Mid->addSuccessor(Tail);
  Keep->addSuccessor(Tail);
  // Mid REDEFINES R5 without reading it (the kill that fooled the
  // one-block walk); Keep passes it through untouched.
  BuildMI(*Mid, Mid->end(), DebugLoc(), TII().get(Haydn::ADDI32),
          Haydn::R5)
      .addReg(Haydn::R0)
      .addImm(1);
  // Tail reads R5 — the join use, live on the Keep path.
  BuildMI(*Tail, Tail->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R5)
      .addReg(Haydn::R13)
      .addImm(0);

  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(*MF);
  EXPECT_TRUE(FPL.isLiveIn(*Keep, Haydn::R5))
      << "the Keep->Tail path carries the join-through use";
  EXPECT_TRUE(FPL.isLiveIn(*Latch, Haydn::R5))
      << "liveness through the Keep successor is transitive at the "
         "clobber-obligation block";
}

TEST_F(HaydnHWLoopDemoteTest, FixedPointAdmitsR5R6WhenDead) {
  // Nothing mentions R5/R6 anywhere: they must be dead everywhere and
  // therefore reachable scratch candidates under the D1.61 owner (the
  // demote-path list omitted them).
  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(*MF);
  EXPECT_FALSE(FPL.isLiveIn(*Header, Haydn::R5));
  EXPECT_FALSE(FPL.isLiveIn(*Header, Haydn::R6));
  EXPECT_FALSE(FPL.isLiveIn(*Exit, Haydn::R5));
  EXPECT_FALSE(FPL.isLiveIn(*Exit, Haydn::R6));
}

TEST_F(HaydnHWLoopDemoteTest, CollectPostRewriteLatchSuccsKeepsExtras) {
  MachineBasicBlock *Extra = MF->CreateMachineBasicBlock();
  MF->push_back(Extra);
  Latch->addSuccessor(Extra);
  SmallVector<MachineBasicBlock *, 4> Dropped;
  haydn::hwloop::collectPostRewriteLatchSuccessors(
      *Latch, Header, Exit, /*KeepExtraSuccs=*/false, Dropped);
  ASSERT_EQ(Dropped.size(), 2u);
  EXPECT_EQ(Dropped[0], Header);
  EXPECT_EQ(Dropped[1], Exit);
  SmallVector<MachineBasicBlock *, 4> Kept;
  haydn::hwloop::collectPostRewriteLatchSuccessors(
      *Latch, Header, Exit, /*KeepExtraSuccs=*/true, Kept);
  ASSERT_GE(Kept.size(), 3u);
  EXPECT_EQ(Kept[0], Header);
  EXPECT_EQ(Kept[1], Exit);
  bool SawExtra = false;
  for (MachineBasicBlock *S : Kept)
    if (S == Extra)
      SawExtra = true;
  EXPECT_TRUE(SawExtra)
      << "KeepExtra must put Extra after designated Header/Exit";
}

TEST_F(HaydnHWLoopDemoteTest, PostRewriteObligationDropsRemovedEdges) {
  // Header==Latch shape with an EXTRA pre-rewrite successor that reads
  // R5. The post-rewrite obligation is {self, Exit}; the extra edge is
  // dropped by the rewrite, so R5 must NOT be refused.
  Latch->removeSuccessor(Header);
  Header->removeSuccessor(Latch);
  Latch->removeSuccessor(Exit);
  Header->addSuccessor(Header);
  Header->addSuccessor(Exit);
  MachineBasicBlock *Extra = MF->CreateMachineBasicBlock();
  MF->push_back(Extra);
  Header->addSuccessor(Extra);
  BuildMI(*Extra, Extra->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R5)
      .addReg(Haydn::R13)
      .addImm(0);

  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(*MF);
  const MachineBasicBlock *PostSuccsArr[] = {Header, Exit};
  ArrayRef<const MachineBasicBlock *> PostSuccs = PostSuccsArr;
  EXPECT_FALSE(FPL.liveUnderPostRewriteSuccessors(*Header, PostSuccs,
                                                   Haydn::R5))
      << "dropped extra successor must not refuse a dead-on-obligation "
         "scratch";
  // The converged set of Header itself DOES see the extra edge (it is a
  // real CFG edge pre-rewrite) — the distinction between the two
  // queries is the nsichneu class.
  EXPECT_TRUE(FPL.isLiveIn(*Header, Haydn::R5))
      << "converged set must reflect the real pre-rewrite CFG edge";
}

TEST_F(HaydnHWLoopDemoteTest, PostRewriteObligationKeepsExtraEdges) {
  // Header==Latch so the self-entry restricted transfer does not join
  // Extra through the full-CFG Latch live-ins (the drop-extra twin).
  Latch->removeSuccessor(Header);
  Header->removeSuccessor(Latch);
  Latch->removeSuccessor(Exit);
  Header->addSuccessor(Header);
  Header->addSuccessor(Exit);
  MachineBasicBlock *Extra = MF->CreateMachineBasicBlock();
  MF->push_back(Extra);
  Header->addSuccessor(Extra);
  BuildMI(*Extra, Extra->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R5)
      .addReg(Haydn::R13)
      .addImm(0);

  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(*MF);
  SmallVector<MachineBasicBlock *, 4> PostSuccs;
  haydn::hwloop::collectPostRewriteLatchSuccessors(
      *Header, Header, Exit, /*KeepExtraSuccs=*/true, PostSuccs);
  SmallVector<const MachineBasicBlock *, 4> ConstPostSuccs(PostSuccs.begin(),
                                                           PostSuccs.end());
  EXPECT_TRUE(FPL.liveUnderPostRewriteSuccessors(*Header, ConstPostSuccs,
                                                 Haydn::R5))
      << "kept Extra dest read of r5 is a post-rewrite scratch obligation";
  const MachineBasicBlock *HeaderExit[] = {Header, Exit};
  EXPECT_FALSE(FPL.liveUnderPostRewriteSuccessors(*Header, HeaderExit,
                                                  Haydn::R5))
      << "setup: {Header, Exit} must not occupy Extra-only r5";
}

TEST_F(HaydnHWLoopDemoteTest, GuardedTailDefsDoNotKill) {
  // The POISON (ls_reg_scalar CHECK(13)) shape at the promoted-tail
  // seat: Latch already carries the fixture's {Header, Exit} successor
  // edges; install the guarded long form — near BEQZ to Header, trailing
  // JALR_W on the Exit edge. JALR_W's TableGen Defs carry the call-saved
  // clobbers $r1..$r7/$r12/$d0..$d7. R6's only use is in Exit (the
  // guarded JALR edge's target). An UNGUARDED transfer treats the
  // JALR_W implicit-def R6 as a kill, so live-in(Latch) drops R6 and an
  // earlier site "proves" R6 dead; the guarded transfer keeps it live
  // (defs of terminators strictly after the first kill nothing).
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::BEQZ_W))
      .addReg(Haydn::R2)
      .addMBB(Header);
  // The promoted form spells the call-saved implicit defs on the JALR_W
  // bundle (d149 POISON fixture shape) — include the R6 one explicitly
  // so the guarded/unguarded difference is load-bearing here.
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::JALR_W))
      .addReg(Haydn::R5, RegState::Define)
      .addReg(Haydn::R5)
      .addImm(0)
      .addReg(Haydn::R6, RegState::ImplicitDefine);
  // The far use of R6 — the ONLY use, on the guarded edge's target.
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R6)
      .addReg(Haydn::R13)
      .addImm(0);

  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(*MF);
  EXPECT_TRUE(FPL.isLiveIn(*Latch, Haydn::R6))
      << "guarded JALR_W implicit defs must not kill the live-through "
         "value whose only use is past the near edge";
}

TEST_F(HaydnHWLoopDemoteTest, GuardedTailBundleRootDefsDoNotKill) {
  // d149 POISON product shape: first terminator is bare BEQZ_W; the
  // trailing JALR lives in a committed BUNDLE whose root carries the
  // call-saved implicit-defs. The BUNDLE root is not isTerminator();
  // stepBackward on it must not kill R6.
  BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::BEQZ_W))
      .addReg(Haydn::R2)
      .addMBB(Header);
  MachineInstr *Bund =
      BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
          .addReg(Haydn::R6, RegState::ImplicitDefine)
          .getInstr();
  MachineInstr *Jalr =
      BuildMI(*Latch, Latch->end(), DebugLoc(), TII().get(Haydn::JALR_W))
          .addReg(Haydn::R5, RegState::Define)
          .addReg(Haydn::R5)
          .addImm(0)
          .addReg(Haydn::R6, RegState::ImplicitDefine)
          .getInstr();
  Jalr->bundleWithPred();
  finalizeBundle(*Latch, Bund->getIterator());
  BuildMI(*Exit, Exit->end(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R6)
      .addReg(Haydn::R13)
      .addImm(0);

  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(*MF);
  EXPECT_TRUE(FPL.isLiveIn(*Latch, Haydn::R6))
      << "trailing BUNDLE root implicit-defs must not kill a live-through "
         "value whose only use is past the near edge";
}

TEST_F(HaydnHWLoopDemoteTest, SeedPristinesFalseDoesNotOccupyUnusedSavedCSR) {
  markPrologueSaved(Haydn::R14);
  haydn::hwloop::FunctionPhysLiveness NoCsi;
  NoCsi.build(*MF, /*SeedPristines=*/false);
  EXPECT_FALSE(NoCsi.isLiveIn(*Header, Haydn::R14))
      << "1a/1b seed must not be default CSI FPL (D1.61r)";
  haydn::hwloop::FunctionPhysLiveness Csi;
  Csi.build(*MF);
  EXPECT_TRUE(Csi.isLiveIn(*Header, Haydn::R14))
      << "default FPL CSI seed stays pickCounterReg / LongScr / LBN";
}

TEST_F(HaydnHWLoopDemoteTest, StoredLiveinsDoNotSubstitute) {
  // Pipeline contract: stored MBB live-ins may cache the owner, they
  // never seed it (AIE addLiveIns is the intra-block cache, not
  // whole-function authority). A name that appears ONLY on the stored
  // list, with no use/def in the function, is not live.
  Header->addLiveIn(Haydn::R3);
  Latch->addLiveIn(Haydn::R3);
  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(*MF);
  EXPECT_FALSE(FPL.isLiveIn(*Header, Haydn::R3))
      << "stored livein without a transfer use must not be live";
  EXPECT_FALSE(FPL.isLiveIn(*Latch, Haydn::R3));
  const MachineBasicBlock *PostSuccsArr[] = {Header, Exit};
  EXPECT_FALSE(FPL.liveUnderPostRewriteSuccessors(*Header, PostSuccsArr,
                                                  Haydn::R3))
      << "post-rewrite self-edge must not read stored liveins";
}

TEST_F(HaydnHWLoopDemoteTest, SelfEdgeLoopCarriedUseNeedsNoStoredLivein) {
  // Header==Latch self-edge: a use at the top of Header is loop-carried
  // even when the stored livein list is empty (BR split-tail stale
  // case). The restricted transfer must see it.
  Latch->removeSuccessor(Header);
  Header->removeSuccessor(Latch);
  Header->addSuccessor(Header);
  Header->addSuccessor(Exit);
  BuildMI(*Header, Header->begin(), DebugLoc(), TII().get(Haydn::ST32))
      .addReg(Haydn::R4)
      .addReg(Haydn::R13)
      .addImm(0);

  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(*MF);
  EXPECT_TRUE(FPL.isLiveIn(*Header, Haydn::R4))
      << "self-edge use must be live without a stored livein";
  const MachineBasicBlock *PostSuccsArr[] = {Header, Exit};
  EXPECT_TRUE(FPL.liveUnderPostRewriteSuccessors(*Header, PostSuccsArr,
                                                 Haydn::R4))
      << "restricted self-edge transfer must keep the loop-carried use";
}

} // namespace
