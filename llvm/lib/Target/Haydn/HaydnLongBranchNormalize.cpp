//===- HaydnLongBranchNormalize.cpp - GR2.7 in-block long form ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions; See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// See HaydnLongBranchNormalize.h for the GR2.7 ownership contract. The
// mechanism here is one reusable law with zero new authority:
//
//   * the SAME layout estimate BranchRelaxation scans with
//     (TII.getInstSizeInBytes offset walk, layout order, entering-MBB
//     alignment pad — estimateMBBDistance law) and the SAME window test
//     (TII.isBranchOffsetInRange with the BranchRelaxSafetyBufferBytes
//     inflation) decide "far";
//   * the SAME terminal in-block long-form vocabulary the pre-S1
//     trampoline promotion, insertIndirectBranch, and the demote
//     long-latch template use (LUI + ADDI32_W + JALR_W on the Dest MBB
//     symbol, committed exact late singletons);
//   * Scratch proof (D1.49, one law — never a stored MBB livein list):
//     a GPR proven dead by COMPUTED liveness on every clobber-obligation
//     out-edge (guarded-tail one-block LivePhysRegs walk union,
//     pristines included) and disjoint (TRI::regsOverlap) from every
//     explicit AND implicit register operand of the tail control-flow
//     MIs. ZOL far-exit B is dead-on-exit only (parcels after END).
//     Stored liveins are stale after BranchRelaxation split tails
//     and never represent pristine unsaved CSRs. The successor walk is
//     GUARDED: a JALR_W this pass already installed behind a near-cond
//     executes only on the fail edge, so its implicit defs kill nothing
//     on the taken edge (the ls_reg_scalar CHECK(13) class). Refusal
//     precedes every mutation; the retired "last-resort unused CSR" arm
//     was the pristine-CSR smash class and is deleted.
//
// Because every rewrite is inside one MBB (terminator-area surgery only,
// no successor/predecessor edits), MF.size() never changes and the
// postcommit CFG-creation wall keeps holding. BranchRelaxation then runs
// on a function with zero far short-branch sites: its fixup arms are
// unreachable, exactly the GR2.7 product-unreachable end state.
//
// Rewrites (tail = MBB ending in the branch(es)):
//   A. analyzable tail (analyzeBranch ok), far target T:
//      remove branch MIs; emit committed LUI+ADDI (scratch);
//      if Cond non-empty emit Cond to T; emit JALR_W last.
//      (Cond stays uninverted: JALR is unconditional, so the cond must
//      guard the JUMP, and fallthrough order keeps the original
//      condition sense with no reverseBranchCondition needed.)
//   B. unanalyzable tail whose LAST terminator is an out-of-range
//      B/uncond: JALR_W replaces exactly that terminator; the two
//      address parcels insert before the first terminator.
// ZOL metas (PseudoLoopEnd/LoopJNZ) and always-in-range opcodes
// (JAL/JALR/PseudoCALL/BR_JT) are never touched (isBranchOffsetInRange
// returns true; getBranchDestBlock has no MBB form for them here).
//
//===----------------------------------------------------------------------===//

#include "HaydnLongBranchNormalize.h"
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnFormatERecords.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnHWLoopDemote.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-longbranch-normalize"

// D1.34: the byte-walk authority (block starts AND intra-block site
// offsets) is haydn::hwloop::computeLayoutBlockStarts /
// estimateLayoutInstrOffset in HaydnHWLoopDemote.cpp — the ONE law the
// demote LongLatch decision and the Fixup walks consume. The private
// static forks this file used to carry (computeBlockStarts /
// estimateInstrOffset with their own debug/CFI skip-set) are deleted;
// a divergence between two copies is exactly the class that let a site
// measure in-range here while BranchRelaxation's scan measured far.
using haydn::hwloop::computeBlockLiveIns;
using haydn::hwloop::computeLayoutBlockStarts;
using haydn::hwloop::emitExactLate;
using haydn::hwloop::emitExactLateDef;
using haydn::hwloop::estimateLayoutInstrOffset;

// (Arm A's Last anchor is kept only for the post-rewrite re-emitted-site
// measures — see the NOTE at destInRange; the pre-rewrite far-scan uses
// each TI's own site offset.)

namespace {

// D1.49: computed out-edge liveness, the one authority. For EVERY CFG
// successor of MBB, run the established one-block LivePhysRegs walk
// (haydn::hwloop::computeBlockLiveIns) and union the results. The
// obligation set is the block's full CFG successor set as recorded —
// every rewrite arm keeps MBB's successor edges (Arm A's near cond
// carries the not-taken edge, JALR the far one; Arm B and the ZOL arm
// leave retained controls' edges in place), so the pre-rewrite successor
// set IS the post-rewrite obligation set, computed one walk per arm.
// Pristines are included through the successor walk's addLiveOuts when
// callee-saved info is valid: an unsaved CSR the function never
// mentioned is live-out of the return block and therefore live through
// every block on its path — clobbering it destroys the caller's value
// with no PEI save/restore to repair it. Stored MBB livein lists are
// NOT read: BranchRelaxation split tails leave them stale (empty) and
// they never contain pristines.
//
// Guarded-tail law (D1.49 wrong-code repair, ls_reg_scalar class): a
// successor already promoted by THIS pass ends in the guarded tail
// LUI/ADDI(+near-cond)/JALR_W, and the trailing JALR_W carries the
// call-saved implicit defs $r1..$r7/$r12/$d0..$d7. stepBackward's
// removeDefs treats those as unconditional kills, but the JALR executes
// ONLY on the guarded fail edge — liveness is a MAY property, and a def
// that may not execute kills nothing. This pass's fixed-point re-scan
// then walks an already-promoted successor and "proves" every near-path
// live-through GPR dead, so the next site's LUI/ADDI/JALR clobbers it
// (ls_reg_scalar CHECK(13): r6 = -91 defined bb.0, last read bb.12,
// picked as scratch for bb.7/bb.6/bb.5 — guest_exit=13).
//
// The guard is POSITIONAL, not the terminator's own conditionality: the
// FIRST terminator of the block always executes (its defs kill); every
// terminator strictly AFTER it is skipped whenever an earlier
// conditional takes its edge (defs and regmask kills drop out, uses
// still seed — a guarded JALR reads its scratch, and a use on ANY path
// is live-in). The promoted shape's trailing JALR_W is isBarrier (never
// isConditionalBranch), so keying on the last terminator's flags misses
// exactly the promoted form. The verifier forbids non-terminators after
// the first terminator, so "strictly after FirstTerm" is exactly the
// guarded terminator region. Ignoring a guarded def can only leave a
// register LIVE (refusal/QoR), never prove a live one dead — the
// fail-closed direction. Blocks with 0-1 terminators walk identically
// to haydn::hwloop::computeBlockLiveIns.
static void computeGuardedBlockLiveIns(LivePhysRegs &Live,
                                       const MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  Live.init(*MF.getRegInfo().getTargetRegisterInfo());
  Live.addLiveOuts(MBB);
  const MachineInstr *FirstTerm = nullptr;
  for (const MachineInstr &MI : MBB) {
    if (MI.isTerminator() || MI.isBranch() || MI.isIndirectBranch()) {
      FirstTerm = &MI;
      break;
    }
  }
  for (const MachineInstr &MI : llvm::reverse(MBB)) {
    if (FirstTerm && &MI != FirstTerm) {
      // Guarded terminator: may be skipped on an earlier conditional's
      // taken edge. Defs/regmask kills may never execute — kill nothing.
      Live.addUses(MI);
      continue;
    }
    Live.stepBackward(MI);
  }
}

// Out-edge live set over the rewrite's clobber obligation. Null DeadOn
// = every CFG successor (Arm A/B: LUI/ADDI always execute). Non-null
// DeadOn = that successor only (ZOL arm: parcels sit AFTER PseudoLoopEnd
// and execute only on the software-exit fallthrough; the hardware
// backedge is taken at END, so Header live-ins are not clobbered).
//
// Per successor: stored liveins if nonempty (the edge-precise set the
// allocator left; walking the successor body would union OTHER
// predecessors' live-ins through that block's addLiveOuts — a backedge
// to this MBB then occupies every loop-carried GPR, the FIR
// bkfir32x16_process bb.3 class). Empty stored liveins are the BR
// split-tail stale case: fall back to the guarded computed walk
// (pristines included).
static void computeOutEdgeLiveSet(const MachineBasicBlock &MBB,
                                  const TargetRegisterInfo &TRI,
                                  LivePhysRegs &Out,
                                  const MachineBasicBlock *DeadOn) {
  Out.init(TRI);
  auto addSucc = [&](const MachineBasicBlock *S) {
    if (!S)
      return;
    if (S->livein_empty()) {
      LivePhysRegs SuccLive;
      computeGuardedBlockLiveIns(SuccLive, *S);
      for (MCPhysReg R : SuccLive)
        Out.addReg(R);
      return;
    }
    Out.addLiveIns(*S);
  };
  if (DeadOn) {
    addSucc(DeadOn);
    return;
  }
  for (const MachineBasicBlock *S : MBB.successors())
    addSucc(S);
}

// D1.49: every explicit AND implicit register operand of the tail
// control-flow MIs, unwrapped through bundles. The retired proof tested
// only the analyzeBranch Cond registers by exact ID; a committed branch
// cycle's implicit operands (implicit-def $sfr, implicit uses) and every
// register of EARLIER retained terminators in an unanalyzable multi-cond
// tail were invisible, and the LUI/ADDI parcels land BEFORE the retained
// reads, so an overlap is a live-value clobber, not a dead write.
static void collectTailControlOperands(
    const MachineBasicBlock &MBB,
    SmallVectorImpl<MCPhysReg> &TailRegs) {
  // isBranch/isTerminator default to AnyInBundle, so a committed BUNDLE
  // root whose interior holds the control flow matches here; MBB::range
  // never yields interior children, so unwrap and walk BOTH the root's
  // own operand summary (which finalizeBundle keeps authoritative) and
  // every interior child's operands. Duplicates are harmless — the
  // consumer is an overlap test, not a count.
  for (const MachineInstr &MI : MBB) {
    if (!MI.isBranch() && !MI.isTerminator() && !MI.isReturn())
      continue;
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.getReg().isPhysical())
        TailRegs.push_back(MO.getReg().asMCReg());
    }
    if (MI.isBundle()) {
      for (const MachineInstr *C : haydn::bundle::members(MI)) {
        for (const MachineOperand &MO : C->operands()) {
          if (MO.isReg() && MO.getReg().isPhysical())
            TailRegs.push_back(MO.getReg().asMCReg());
        }
      }
    }
  }
}

// Pick a scratch GPR for the in-block address materialization (D1.49
// law). The proof, in refusal order:
//   1. not reserved (R0 soft-zero, R13 SP, R15 LR, R14 when hasFP);
//   2. no TRI::regsOverlap with any explicit/implicit operand of any
//      tail control-flow MI — this subsumes the retired exact-ID Forbid
//      set of analyzeBranch Cond registers and adds aliases/subregs,
//      committed-cycle implicit operands, and earlier retained
//      terminators of an unanalyzable tail;
//   3. a CSR only when the function mentioned it (MRI.isPhysRegUsed):
//      a mentioned CSR has a PEI save/restore, an unmentioned one is
//      PRISTINE — live for the caller with no repair — and is refused
//      regardless of callee-saved-info validity (the computed walk
//      below refuses pristines too, but its seeding depends on valid
//      CSI, which mid-pipeline -run-pass probes do not have);
//   4. dead on EVERY clobber-obligation out-edge by computed LivePhysRegs
//      (DeadOn restricts the walk to that successor; null = all succs).
// Failure returns 0 BEFORE any mutation; the caller refuses or defers.
// The retired "last-resort AllowUnusedCSR" arm is deleted: it was the
// pristine-CSR smash class this row closes, not a promotion enabler.
static Register pickInBlockScratch(const MachineBasicBlock &MBB,
                                   const TargetRegisterInfo &TRI,
                                   const MachineBasicBlock *DeadOn = nullptr) {
  // Same priority shape as the demote long-latch probe (D1.32). Order
  // among proven-dead registers is QoR only; every refusal above is a
  // correctness law.
  static const MCPhysReg CandsGPR[] = {
      Haydn::R11, Haydn::R10, Haydn::R9, Haydn::R8, Haydn::R7,
      Haydn::R6,  Haydn::R5,  Haydn::R4, Haydn::R3, Haydn::R2,
      Haydn::R1,  Haydn::R12, Haydn::R14};
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  auto isHaydnCSR = [](MCPhysReg R) {
    return R == Haydn::R8 || R == Haydn::R9 || R == Haydn::R10 ||
           R == Haydn::R11 || R == Haydn::R14;
  };

  SmallVector<MCPhysReg, 8> TailRegs;
  collectTailControlOperands(MBB, TailRegs);

  LivePhysRegs OutLive;
  computeOutEdgeLiveSet(MBB, TRI, OutLive, DeadOn);

  auto overlapsAny = [&](MCPhysReg R) {
    for (MCPhysReg T : TailRegs)
      if (TRI.regsOverlap(R, T))
        return true;
    return false;
  };

  for (MCPhysReg R : CandsGPR) {
    if (MRI.isReserved(R))
      continue;
    if (overlapsAny(R))
      continue;
    if (isHaydnCSR(R) && !MRI.isPhysRegUsed(R))
      continue;
    if (!OutLive.available(MRI, R))
      continue;
    return R;
  }
  return Register();
}

static bool isHwLoopAnalyzeCond(const SmallVectorImpl<MachineOperand> &Cond) {
  if (Cond.empty() || !Cond[0].isImm())
    return false;
  const unsigned Opc = Cond[0].getImm();
  return Opc == Haydn::PseudoLoopEnd || Opc == Haydn::LoopJNZ;
}

// D1.32 order: LUI then ADDI32_W then JALR, one captured insert point
// advanced forward (never re-evaluate getFirstTerminator — that lands
// ADDI before its LUI def). Same vocabulary as emitLongLatch.
static void emitInBlockLongJump(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator Ins,
                                const DebugLoc &DL, const HaydnInstrInfo &TII,
                                Register Scratch, MachineBasicBlock *Dest) {
  Ins = std::next(emitExactLateDef(MBB, Ins, DL, TII, Haydn::LUI, Scratch,
                                   [&](MachineInstrBuilder MIB) {
                                     MIB.addMBB(Dest);
                                   })
                      ->getIterator());
  Ins = std::next(emitExactLateDef(MBB, Ins, DL, TII, Haydn::ADDI32_W, Scratch,
                                   [&](MachineInstrBuilder MIB) {
                                     MIB.addReg(Scratch).addMBB(Dest);
                                   })
                      ->getIterator());
  // Keep logical JALR_W (isBarrier=1). The E96 member drops isBarrier;
  // applyFinalDirectCompatibleOpcode also refuses this bake (CB-129).
  MachineInstrBuilder J =
      BuildMI(MBB, Ins, DL, TII.get(Haydn::JALR_W), Scratch)
          .addReg(Scratch)
          .addImm(0);
  haydn::bundle::finalizeExactLateSingleton(*J);
}

// Erase only the trailing unconditional (the ZOL exit B). Leaves
// PseudoLoopEnd / LoopJNZ in place. Bundle-root erase is the same
// contract removeBranch uses for a committed uncond cycle.
static bool eraseTrailingUncondBranch(MachineBasicBlock &MBB) {
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return false;
  if (!I->isUnconditionalBranch() &&
      !I->isUnconditionalBranch(MachineInstr::IgnoreBundle))
    return false;
  MBB.erase(I);
  return true;
}

} // namespace

char HaydnLongBranchNormalize::ID = 0;

HaydnLongBranchNormalize::HaydnLongBranchNormalize()
    : MachineFunctionPass(ID) {
  initializeHaydnLongBranchNormalizePass(*PassRegistry::getPassRegistry());
}

void HaydnLongBranchNormalize::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool HaydnLongBranchNormalize::runOnMachineFunction(MachineFunction &MF) {
  // GR2.7 phase law: post-stamp, this pass owns long-form promotion.
  // Pre-stamp, the pre-S1 normalization BR still owns the FIRST full
  // promotion pass (trampoline forms are legal there) — but its
  // fixupConditionalBranch can assert on a multi-cond/unanalyzable tail
  // after earlier same-seat rewrites shift layout (nsichneu: bb.0's
  // inverted-B rewrite made bb.2's tail unanalyzable before BR reached
  // it). Running here pre-stamp too keeps every far site's terminal form
  // in ONE owner: the in-block rewrite runs first, BR only verifies.
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();

  const auto &TII =
      *static_cast<const HaydnInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  bool Changed = false;

  // Fixed point: each in-block rewrite grows the function (3-4 parcels),
  // which can push a LATER site out of range. Re-scan until no far short
  // site remains (every iteration rewrites at least one site or stops).
  // Bounded by the site census exactly like BranchRelaxation's own loop.
  unsigned Guard = 0;
  const unsigned MaxIters = 2 * MF.size() + 8;
  for (;;) {
    if (++Guard > MaxIters)
      report_fatal_error(DEBUG_TYPE + Twine(": iteration cap on ") +
                             MF.getName(),
                         /*GenCrashDiag=*/false);
    SmallVector<int64_t, 32> BlockStarts;
    computeLayoutBlockStarts(MF, TII, BlockStarts);

    bool IterChanged = false;

  for (MachineBasicBlock &MBB : MF) {
    if (MBB.empty())
      continue;

    // Already the in-block long form (LUI+ADDI+[cond+]JALR). Do not
    // re-promote: removeBranch stops at JALR and a second LUI+ADDI+JALR
    // would stack forever (matmult-int hang).
    {
      MachineBasicBlock::iterator LastJ = MBB.getLastNonDebugInstr();
      if (LastJ != MBB.end()) {
        unsigned LastLog = haydn::format_e::logicalOpcodeOrSelf(
            LastJ->getOpcode());
        if (LastJ->isBundle()) {
          for (const MachineInstr *C : haydn::bundle::members(*LastJ)) {
            unsigned L = haydn::format_e::logicalOpcodeOrSelf(C->getOpcode());
            if (L == Haydn::JALR || L == Haydn::JALR_W)
              LastLog = L;
          }
        }
        if (LastLog == Haydn::JALR || LastLog == Haydn::JALR_W)
          continue;
      }
    }

    // --- Arm A: analyzable tail with a far target.
    MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
    SmallVector<MachineOperand, 4> Cond;
    const bool Unanalyzable =
        TII.analyzeBranch(MBB, TBB, FBB, Cond, /*AllowModify=*/false);

    LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": " << MF.getName()
                      << " bb." << MBB.getNumber()
                      << (Unanalyzable ? " unanalyzable" : " analyzable")
                      << '\n');

    if (!Unanalyzable) {
      // Range authority: the SAME estimate BranchRelaxation scans with
      // (signed dest-start minus branch-site offset) and the SAME
      // TII.isBranchOffsetInRange window test (safety-buffer inflated).
      // The far-scan below measures EACH tail TI from ITS OWN site
      // offset (D1.34 two-terminator law): in a cond+uncond tail the
      // earlier terminator's site precedes the trailing one, so its true
      // displacement is LARGER than any Last-anchored measure — an
      // under-estimate left the site short here and BR promoted it
      // postcommit into the CFG wall.
      MachineBasicBlock::iterator Last = MBB.getLastNonDebugInstr();
      // Bundle-aware classification (BR's own relax walk sees BUNDLE
      // roots whose interior is the branch; IgnoreBundle looks inside).
      auto destOf = [&TII](MachineInstr &MI) -> MachineBasicBlock * {
        const MachineInstr *CF = &MI;
        if (MI.isBundle()) {
          for (const MachineInstr *C : haydn::bundle::members(MI)) {
            if (C->isBranch(MachineInstr::IgnoreBundle) ||
                C->isIndirectBranch(MachineInstr::IgnoreBundle) ||
                C->isBarrier(MachineInstr::IgnoreBundle)) {
              CF = C;
              break;
            }
          }
        }
        const unsigned Log =
            haydn::format_e::logicalOpcodeOrSelf(CF->getOpcode());
        // Hardware-loop metas and the already-long JALR form are not
        // PC-relative short branches. Range-testing a BUNDLE wrapper as
        // simm12 would re-promote JALR sites.
        if (Log == Haydn::PseudoLoopEnd || Log == Haydn::LoopJNZ ||
            Log == Haydn::JALR || Log == Haydn::JALR_W)
          return nullptr;
        if (CF->isBranch(MachineInstr::IgnoreBundle) ||
            CF->isUnconditionalBranch() || CF->isConditionalBranch())
          return TII.getBranchDestBlock(MI);
        return nullptr;
      };
      MachineBasicBlock *FarDest = nullptr;
      // Walk the TAIL as bundle roots (MBB::iterator yields roots; a
      // BUNDLE root's own descriptor has no isTerminator/isBranch flag,
      // so terminators() skips committed branch cycles — analyzeBranch's
      // unwrap law instead). Stop at the first non-branch tail instr.
      for (MachineBasicBlock::iterator TI = MBB.getLastNonDebugInstr();
           TI != MBB.end(); --TI) {
        MachineBasicBlock *Dest = destOf(*TI);
        if (Dest && Dest->getNumber() >= 0 &&
            Dest->getNumber() < (int)BlockStarts.size() &&
            BlockStarts[Dest->getNumber()] >= 0) {
          const int64_t DestStart = BlockStarts[Dest->getNumber()];
          // Per-TI site offset (D1.34): THIS terminator's own offset,
          // not the trailing Last MI's. For the last terminator the two
          // coincide, so single-terminator tails are byte-identical.
          const int64_t BrOff =
              DestStart - (BlockStarts[MBB.getNumber()] +
                           estimateLayoutInstrOffset(MBB, TI, TII));
          LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": " << MF.getName() << " bb."
                            << MBB.getNumber() << " -> bb."
                            << Dest->getNumber() << " brOff=" << BrOff
                            << '\n');
          // Range-test the unwrapped child opcode, not a BUNDLE root
          // (BUNDLE is always-in-range at TII so BR will not trampoline
          // committed packets).
          unsigned RangeOpc = TI->getOpcode();
          if (TI->isBundle()) {
            for (const MachineInstr *C : haydn::bundle::members(*TI)) {
              unsigned L = haydn::format_e::logicalOpcodeOrSelf(C->getOpcode());
              if (L == Haydn::JALR || L == Haydn::JALR_W)
                RangeOpc = C->getOpcode();
              else if (C->isBranch(MachineInstr::IgnoreBundle))
                RangeOpc = C->getOpcode();
            }
          }
          if (!TII.isBranchOffsetInRange(RangeOpc, BrOff)) {
            FarDest = Dest;
            break;
          }
        }
        if (!TI->isBranch(MachineInstr::IgnoreBundle) &&
            !TI->isTerminator() && !TI->isUnconditionalBranch() &&
            !TI->isConditionalBranch())
          break; // left the branch tail
        if (TI == MBB.begin())
          break;
      }

      if (FarDest) {
        const DebugLoc DL = Last->getDebugLoc();
        const bool PostCommit =
            FuncInfo && FuncInfo->hasPostCommitBlockBudget();

        // ZOL latch + far software exit: `PseudoLoopEnd Header ; B Exit`
        // (fir_convol32x16 bb.4, firinterp2_proc bb.9, arm_cfft_f32 bb.7).
        // analyzeBranch reports Cond=PseudoLoopEnd, which is uninvertible
        // and must stay as the hardware backedge. Promote ONLY the trailing
        // uncond B; the address parcels run AFTER END, so they execute
        // only on the software-exit fallthrough. Header live-ins are not
        // a clobber obligation (the hardware backedge is taken at END).
        // D1.49 computed-liveness still applies: DeadOn=FarDest is the
        // one-edge obligation, never a stored MBB livein list.
        if (isHwLoopAnalyzeCond(Cond)) {
          const Register Scratch = pickInBlockScratch(MBB, TRI, FarDest);
          if (!Scratch) {
            if (!PostCommit)
              continue; // pre-stamp: BR RestoreBB / trampoline
            report_fatal_error(
                DEBUG_TYPE +
                    Twine(": no dead-on-edge GPR for the in-block long form (") +
                    MF.getName() + " bb." + Twine(MBB.getNumber()) +
                    "); postcommit far site cannot be promoted without CFG "
                    "creation",
                /*GenCrashDiag=*/false);
          }
          if (!eraseTrailingUncondBranch(MBB)) {
            report_fatal_error(
                DEBUG_TYPE + Twine(": ZOL latch has no trailing uncond to "
                                   "promote at ") +
                    MF.getName() + " bb." + Twine(MBB.getNumber()),
                /*GenCrashDiag=*/false);
          }
          emitInBlockLongJump(MBB, MBB.end(), DL, TII, Scratch, FarDest);
          LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": " << MF.getName() << " bb."
                            << MBB.getNumber()
                            << " promoted ZOL-exit B -> bb."
                            << FarDest->getNumber()
                            << " (kept hwloop meta; in-block LUI+ADDI+JALR)\n");
          Changed = true;
          IterChanged = true;
          continue;
        }

        // D1.49: the retired explicit Cond-register Forbid set is subsumed
        // by collectTailControlOperands (regsOverlap over every explicit +
        // implicit operand of every tail control-flow MI, incl. aliases).
        const Register Scratch = pickInBlockScratch(MBB, TRI);
        if (!Scratch) {
          if (!PostCommit)
            continue; // pre-stamp: BR RestoreBB / trampoline
          report_fatal_error(
              DEBUG_TYPE +
                  Twine(": no dead-on-edge GPR for the in-block long form (") +
                  MF.getName() + " bb." + Twine(MBB.getNumber()) +
                  "); postcommit far site cannot be promoted without CFG "
                  "creation",
              /*GenCrashDiag=*/false);
        }

        // Invert the original cond to the not-taken dest so JALR takes
        // the far edge. Never reverse PseudoLoopEnd (handled above).
        SmallVector<MachineOperand, 4> NearCond;
        MachineBasicBlock *NearDest = nullptr;
        auto isSucc = [&](MachineBasicBlock *D) {
          return D && llvm::is_contained(MBB.successors(), D);
        };
        // NOTE (D1.34): destInRange and Arm B below deliberately keep
        // the Last-anchored site — they model the POST-rewrite
        // re-emitted near-cond / replaced last terminator, whose site is
        // the last-terminator position. Do NOT "unify" them onto the
        // per-TI offset; that would test the wrong site.
        auto destInRange = [&](MachineBasicBlock *D) {
          if (!D || D->getNumber() < 0 ||
              D->getNumber() >= (int)BlockStarts.size() ||
              BlockStarts[D->getNumber()] < 0)
            return false;
          const int64_t Off =
              BlockStarts[D->getNumber()] -
              (BlockStarts[MBB.getNumber()] +
               estimateLayoutInstrOffset(MBB, Last, TII));
          return TII.isBranchOffsetInRange(Haydn::BEQZ_W, Off);
        };
        if (!Cond.empty()) {
          MachineFunction::iterator NextIt =
              std::next(MachineFunction::iterator(MBB));
          MachineBasicBlock *LayoutNext =
              NextIt != MF.end() ? &*NextIt : nullptr;
          if (TBB == FarDest) {
            SmallVector<MachineOperand, 4> Inv(Cond);
            if (TII.reverseBranchCondition(Inv)) {
              if (!PostCommit)
                continue; // pre-stamp: leave for BR trampoline
              report_fatal_error(
                  DEBUG_TYPE + Twine(": uninvertible condition at ") +
                      MF.getName() + " bb." + Twine(MBB.getNumber()) +
                      " (postcommit far cond site)",
                  /*GenCrashDiag=*/false);
            }
            NearCond = Inv;
            // Skip-over-JALR is CFG-correct iff layout-next IS the
            // not-taken successor. Else invert to in-range FBB. Else
            // pre-stamp defer to BR (trampoline is still legal).
            if (isSucc(LayoutNext) && LayoutNext != TBB)
              NearDest = LayoutNext;
            else if (FBB && destInRange(FBB))
              NearDest = FBB;
            else if (!PostCommit)
              continue;
          } else if (TBB && destInRange(TBB)) {
            NearCond.assign(Cond.begin(), Cond.end());
            NearDest = TBB;
          } else if (!PostCommit) {
            continue;
          }
          if (!NearDest)
            report_fatal_error(
                DEBUG_TYPE + Twine(": no near dest for inverted cond at ") +
                    MF.getName() + " bb." + Twine(MBB.getNumber()),
                /*GenCrashDiag=*/false);
        }

        TII.removeBranch(MBB);

        // D1.32 captured-iterator law: LUI, ADDI32_W, optional near cond,
        // JALR — never ADDI before its LUI def.
        MachineBasicBlock::iterator Ins = MBB.end();
        Ins = std::next(emitExactLateDef(MBB, Ins, DL, TII, Haydn::LUI, Scratch,
                                         [&](MachineInstrBuilder MIB) {
                                           MIB.addMBB(FarDest);
                                         })
                            ->getIterator());
        Ins = std::next(
            emitExactLateDef(MBB, Ins, DL, TII, Haydn::ADDI32_W, Scratch,
                             [&](MachineInstrBuilder MIB) {
                               MIB.addReg(Scratch).addMBB(FarDest);
                             })
                ->getIterator());
        if (!NearCond.empty()) {
          unsigned Opc = NearCond[0].getImm();
          auto MIB = BuildMI(MBB, Ins, DL, TII.get(Opc));
          if (NearCond.size() == 2)
            MIB.addReg(NearCond[1].getReg());
          else
            MIB.addReg(NearCond[1].getReg()).addReg(NearCond[2].getReg());
          MIB.addMBB(NearDest);
          // Leave the near cond BARE so subsequent BR sees BEQZ_W (in
          // range to the not-taken dest), not a BUNDLE root that the
          // opcode-only range API treats as simm12-short and then
          // trampolines in a loop. Late Finalize wraps it.
        }
        MachineInstrBuilder J =
            BuildMI(MBB, Ins, DL, TII.get(Haydn::JALR_W), Scratch)
                .addReg(Scratch)
                .addImm(0);
        haydn::bundle::finalizeExactLateSingleton(*J);

        LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": " << MF.getName() << " bb."
                          << MBB.getNumber() << " promoted far site -> bb."
                          << FarDest->getNumber()
                          << " (in-block LUI+ADDI+cond+JALR)\n");
        Changed = true;
        IterChanged = true;
        continue; // tail rewritten; next MBB
      }
    }

    // --- Arm B: unanalyzable tail whose LAST terminator is an
    // out-of-range unconditional B. Replace exactly that terminator.
    // Do NOT match a trailing conditional via isBranch: that would
    // drop the condition and JALR unconditionally (MEMORY_FAULT class).
    MachineBasicBlock::iterator Last = MBB.getLastNonDebugInstr();
    if (Last == MBB.end())
      continue;
    if (!Last->isUnconditionalBranch())
      continue;
    MachineBasicBlock *Dest = TII.getBranchDestBlock(*Last);
    if (!Dest || Dest->getNumber() < 0 ||
        Dest->getNumber() >= (int)BlockStarts.size())
      continue;
    const int64_t DestStart = BlockStarts[Dest->getNumber()];
    if (DestStart < 0)
      continue;
    const int64_t BrOff =
        DestStart - (BlockStarts[MBB.getNumber()] +
                     estimateLayoutInstrOffset(MBB, Last, TII));
    if (TII.isBranchOffsetInRange(Last->getOpcode(), BrOff))
      continue;

    const bool PostCommitB =
        FuncInfo && FuncInfo->hasPostCommitBlockBudget();
    // D1.49: ALL CFG successors are obligations. Arm B keeps every
    // successor edge (removeBranch only erases the trailing branch
    // sequence; earlier retained conditions keep their edges), and the
    // unanalyzable tail can hold retained controls whose destinations are
    // not Dest — the retired DeadOn=Dest narrowing could prove "dead on
    // Dest" while a live-through value on another successor was clobbered.
    const Register Scratch = pickInBlockScratch(MBB, TRI);
    if (!Scratch) {
      if (!PostCommitB)
        continue;
      report_fatal_error(
          DEBUG_TYPE +
              Twine(": no dead-on-edge GPR for the in-block long form (") +
              MF.getName() + " bb." + Twine(MBB.getNumber()) +
              "); postcommit far site cannot be promoted without CFG "
              "creation",
          /*GenCrashDiag=*/false);
    }
    const DebugLoc DL = Last->getDebugLoc();

    // Erase the tail branch sequence through the same bundle-aware
    // authority Arm A uses (removeBranch erases committed roots and
    // re-stamps coissued survivors). The tail here is unanalyzable but
    // ends in the far uncond B; removeBranch stops at the first
    // non-branch control flow (JALR sites break), which this arm never
    // rewrites.
    TII.removeBranch(MBB);

    emitInBlockLongJump(MBB, MBB.end(), DL, TII, Scratch, Dest);
    LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": " << MF.getName() << " bb."
                      << MBB.getNumber() << " promoted far uncond -> bb."
                      << Dest->getNumber() << " (in-block LUI+ADDI+JALR)\n");
    Changed = true;
    IterChanged = true;
  }

    if (!IterChanged)
      break;
  }

  return Changed;
}

INITIALIZE_PASS(HaydnLongBranchNormalize, DEBUG_TYPE,
                "Haydn Long-Branch Normalize", false, false)

FunctionPass *llvm::createHaydnLongBranchNormalizePass() {
  return new HaydnLongBranchNormalize();
}
