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
//   * the SAME terminal in-block long-form vocabulary (LUI + ADDI32_W +
//     JALR_W on the Dest MBB symbol, committed exact late singletons);
//     insertIndirectBranch is the pre-S1 BR trampoline only and leaves
//     RestoreBB empty;
//   * Scratch proof (D1.49/D1.61, one law — never a stored MBB livein
//     list): a GPR proven dead by the WHOLE-FUNCTION physical-liveness
//     fixed point (haydn::hwloop::FunctionPhysLiveness: alias-aware,
//     edge-aware, pristines/tail-operators/regmasks in the seed and
//     transfer) on every clobber-obligation out-edge, and disjoint
//     (TRI::regsOverlap) from every explicit AND implicit register
//     operand of the tail control-flow MIs. ZOL far-exit B is
//     dead-on-exit only (parcels after END). Stored liveins are stale
//     after BranchRelaxation split tails and never represent pristine
//     unsaved CSRs. The fixed point's per-block transfer is GUARDED:
//     a JALR_W this pass already installed behind a near-cond executes
//     only on the fail edge, so its implicit defs kill nothing on the
//     taken edge (the ls_reg_scalar CHECK(13) class). Refusal precedes
//     every mutation; the retired "last-resort unused CSR" arm was the
//     pristine-CSR smash class and is deleted.
//
// Because every rewrite is inside one MBB (terminator-area surgery only,
// no successor/predecessor edits), MF.size() never changes and the
// postcommit CFG-creation wall keeps holding. BranchRelaxation then runs
// on a function with zero far short-branch sites: its fixup arms are
// unreachable, exactly the GR2.7 product-unreachable end state.
//
// Rewrites (tail = MBB ending in the branch(es)):
//   A. analyzable tail (analyzeBranch ok), far TBB or FBB: scanned
//      FarDest must be that TBB or FBB (HexagonBranchRelaxation.cpp:
//      153-181 range-tests analyzed TBB/FBB). Invert to FBB only when
//      FBB is a CFG successor and in-range; layout-next only when FBB
//      is null and isLayoutSuccessor && successor && != TBB && in-range
//      (BranchRelaxation.cpp:477-490 / 520-530). Invert/retarget the
//      analyzed cond in place (do not dissolve the committed root). Erase
//      only the far uncond member (HaydnInstrInfo::eraseSelectedBranch;
//      AIEBaseInstrInfo.cpp:237-266 / RISCVInstrInfo.cpp:1361-1390 erase
//      at most the trailing uncond + preceding cond). LUI+ADDI+JALR are
//      complete late singleton packets. One-way (no uncond): append JALR
//      only. If a trailing uncond was selected, a failed erase is fatal
//      so short B and JALR cannot both remain.
//   B. unanalyzable tail whose LAST terminator is an out-of-range
//      B/uncond: erase only that selected uncond root/member; preceding
//      unanalyzable controls survive. LUI+ADDI before firstControlMI;
//      append JALR last. A failed erase of the selected uncond is fatal.
// ZOL metas (PseudoLoopEnd/LoopJNZ) stay erase-trailing-uncond +
// emit-after-END through the same helper. Always-in-range opcodes
// (JAL/JALR/PseudoCALL/BR_JT) are never touched.
//
// D1.50 ordered-terminator law (describeOrderedTerminatorTail below):
// every erase arm names the control member it replaces through ONE
// ordered description — control cycles named in order, the selected
// uncond unique and trailing, its dest the analyzed FBB/TBB, the
// analyzed cond owning the analyzed TBB — checked BEFORE any mutation.
// A violation is pre-stamp deferred (BR owns the site) and post-stamp a
// named fatal; no arm may erase a member the law did not name.
//
// GR1.4 / D1.173: each iteration rebuilds LayoutSite and iterates the
// table (ARM ImmBranch is MI+MaxDisp only). Rank>=LongTemplate is not
// re-promoted. HWLoop sites stay closeRetainedHwLoops. Span math stays
// computeLayoutBlockStarts (D1.34); Rank is not a second size oracle.
//
//===----------------------------------------------------------------------===//

#include "HaydnLongBranchNormalize.h"
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnFixupHwLoops.h"
#include "HaydnFormatERecords.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnHWLoopDemote.h"
#include "HaydnInstrInfo.h"
#include "HaydnLayoutSite.h"
#include "HaydnMachineAlignment.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

#include <string>

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
using haydn::hwloop::computeLayoutBlockStarts;
using haydn::hwloop::emitExactLate;
using haydn::hwloop::emitExactLateDef;
using haydn::hwloop::estimateLayoutInstrOffset;

// D1.140: destInRange measures the retained near-cond from the cond's own
// cycle (same per-TI authority as the far-scan). Arm B range-tests the
// trailing uncond from Last because Last IS that uncond's cycle.

namespace {

// D1.49/D1.61: computed out-edge liveness, the one authority — now the
// whole-function fixed point (haydn::hwloop::FunctionPhysLiveness),
// built once per outer iteration of this pass's fixed-point re-scan
// (every in-block rewrite mutates MIR; the rebuild keeps the converged
// sets honest for later sites in the same pass).
//
// The obligation set is the block's full CFG successor set as recorded
// — every rewrite arm keeps MBB's successor edges (Arm A's near cond
// carries the not-taken edge, JALR the far one; Arm B and the ZOL arm
// leave retained controls' edges in place), so the pre-rewrite successor
// set IS the post-rewrite obligation set.
//
// Guarded-tail law (D1.49 wrong-code repair, ls_reg_scalar class): the
// per-block transfer of the fixed point ignores defs/regmask kills of
// terminators strictly after the block's FIRST terminator (they execute
// only on the guarded fail edge; uses still seed). A successor already
// promoted by THIS pass ends in exactly that guarded tail
// LUI/ADDI(+near-cond)/JALR_W whose trailing JALR_W carries the
// call-saved implicit defs $r1..$r7/$r12/$d0..$d7; treating those as
// unconditional kills "proved" every near-path live-through GPR dead
// (ls_reg_scalar CHECK(13): r6 = -91 defined bb.0, last read bb.12,
// picked as scratch for bb.7/bb.6/bb.5 — guest_exit=13).
//
// The retired hybrid here (stored liveins when nonempty, one-block
// computed walk when empty) was NOT a transitive proof: a live-through
// value whose only use sits past a farther join block was invisible to
// both arms when each direct successor redefined it on one path. The
// fixed point is transitive by construction. Stored MBB livein lists
// are never consulted (BranchRelaxation split tails leave them
// stale/empty); pristines are in the seed.

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
      // Retained reads only. Implicit-defs (JALR_W caller-saved clobbers)
      // sit on the barrier after the insert point and are not a clobber of
      // a live tail value (D1.98 both-far overlay).
      if (MO.isReg() && MO.getReg().isPhysical() && MO.readsReg())
        TailRegs.push_back(MO.getReg().asMCReg());
    }
    if (MI.isBundle()) {
      for (const MachineInstr *C : haydn::bundle::members(MI)) {
        for (const MachineOperand &MO : C->operands()) {
          if (MO.isReg() && MO.getReg().isPhysical() && MO.readsReg())
            TailRegs.push_back(MO.getReg().asMCReg());
        }
      }
    }
  }
}

// Pick a scratch GPR for the in-block address materialization (D1.49
// law; D1.61: consults the whole-function fixed point). The proof, in
// refusal order:
//   1. not reserved (R0 soft-zero, R13 SP, R15 LR, R14 when hasFP);
//   2. no TRI::regsOverlap with any explicit/implicit operand of any
//      tail control-flow MI — this subsumes the retired exact-ID Forbid
//      set of analyzeBranch Cond registers and adds aliases/subregs,
//      committed-cycle implicit operands, and earlier retained
//      terminators of an unanalyzable tail;
//   3. a CSR only when the function mentioned it (MRI.isPhysRegUsed):
//      a mentioned CSR has a PEI save/restore, an unmentioned one is
//      PRISTINE — live for the caller with no repair — and is refused
//      regardless of callee-saved-info validity (the fixed point's
//      pristine seeding depends on valid CSI, which mid-pipeline
//      -run-pass probes do not have);
//   4. dead on EVERY clobber-obligation out-edge by the D1.61 fixed
//      point (DeadOn restricts the obligation to that successor; null
//      = all succs). Transitive by construction.
// Failure returns 0 BEFORE any mutation; the caller refuses or defers.
// The retired "last-resort AllowUnusedCSR" arm is deleted: it was the
// pristine-CSR smash class this row closes, not a promotion enabler.
static Register
pickInBlockScratch(const MachineBasicBlock &MBB, const TargetRegisterInfo &TRI,
                   const haydn::hwloop::FunctionPhysLiveness &FPL,
                   const MachineBasicBlock *DeadOn = nullptr,
                   Register Exclude = Register()) {
  // Every allocatable legal GPR candidate (D1.61: the demote path's
  // omission of R5/R6 was the same fragmentation this owner closes).
  // Order among proven-dead registers is QoR only; every refusal above
  // is a correctness law. Same priority shape as the demote probe.
  static const MCPhysReg CandsGPR[] = {
      Haydn::R11, Haydn::R10, Haydn::R9,  Haydn::R8, Haydn::R7,
      Haydn::R6,  Haydn::R5,  Haydn::R4,  Haydn::R3, Haydn::R2,
      Haydn::R1,  Haydn::R12, Haydn::R14};
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  auto isHaydnCSR = [](MCPhysReg R) {
    return R == Haydn::R8 || R == Haydn::R9 || R == Haydn::R10 ||
           R == Haydn::R11 || R == Haydn::R14;
  };

  SmallVector<MCPhysReg, 8> TailRegs;
  collectTailControlOperands(MBB, TailRegs);

  auto overlapsAny = [&](MCPhysReg R) {
    for (MCPhysReg T : TailRegs)
      if (TRI.regsOverlap(R, T))
        return true;
    return false;
  };

  for (MCPhysReg R : CandsGPR) {
    if (MRI.isReserved(R))
      continue;
    if (Exclude && TRI.regsOverlap(R, Exclude))
      continue;
    if (overlapsAny(R))
      continue;
    if (isHaydnCSR(R) && !MRI.isPhysRegUsed(R))
      continue;
    if (FPL.liveOnSuccessor(MBB, DeadOn, R))
      continue;
    return R;
  }
  // Post-RA PEI already ran: unused CSRs have no save. Prefer a used
  // GPR that is dead on the obligation edge. If every used GPR is live
  // or tail-overlaps, a mentioned-and-dead CSR is already allowed
  // above. Last resort: a used non-CSR that only looks live because a
  // BUNDLE implicit-def survived dropPoisoned — walk MRI use_nodbg and
  // still refuse reserved/tail/Exclude.
  LLVM_DEBUG(dbgs() << "haydn-longbranch-normalize: pickInBlockScratch miss "
                    << MBB.getParent()->getName() << " bb."
                    << MBB.getNumber() << "\n");
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
// SplitJalrToEnd: Arm A/B insert LUI+ADDI before firstControlMI (not
// getFirstTerminator) and append JALR last so address parcels are not
// non-terminators after retained controls. ZOL keeps SplitJalrToEnd=false
// (parcels sit after PseudoLoopEnd and execute only on software-exit).
//
// Insert point after a just-emitted exact late singleton (child or root).
// Walks off bundled interiors (HaydnFixupHwLoops.cpp:238-247
// nextBundleBoundary). AIE inserts complete packets; RISC-V RestoreBB
// (RISCVInstrInfo.cpp:1433-1498) is unused.
static MachineBasicBlock::iterator afterEmittedCycle(MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineBasicBlock::instr_iterator II = std::next(MI.getIterator());
  while (II != MBB->instr_end() && II->isBundledWithPred())
    ++II;
  if (II == MBB->instr_end())
    return MBB->end();
  return MachineBasicBlock::iterator(II);
}

static void emitInBlockLongJump(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator Ins,
                                const DebugLoc &DL, const HaydnInstrInfo &TII,
                                Register Scratch, MachineBasicBlock *Dest,
                                bool SplitJalrToEnd = false) {
  // LongTemplate is three complete late singleton packets (LUI, ADDI32_W,
  // JALR). emitExactLateDef already finalizeExactLateSingleton; advance
  // by the committed root so the next parcel cannot land inside the
  // previous wrap.
  MachineInstr *Lui = emitExactLateDef(MBB, Ins, DL, TII, Haydn::LUI, Scratch,
                                       [&](MachineInstrBuilder MIB) {
                                         MIB.addMBB(Dest);
                                       });
  Ins = afterEmittedCycle(*Lui);
  MachineInstr *Addi =
      emitExactLateDef(MBB, Ins, DL, TII, Haydn::ADDI32_W, Scratch,
                       [&](MachineInstrBuilder MIB) {
                         MIB.addReg(Scratch).addMBB(Dest);
                       });
  Ins = afterEmittedCycle(*Addi);
  MachineBasicBlock::iterator JalrIns = SplitJalrToEnd ? MBB.end() : Ins;
  // Keep logical JALR_W (isBarrier=1). The E96 member drops isBarrier;
  // applyFinalDirectCompatibleOpcode also refuses this bake (CB-129).
  MachineInstrBuilder J =
      BuildMI(MBB, JalrIns, DL, TII.get(Haydn::JALR_W), Scratch)
          .addReg(Scratch)
          .addImm(0);
  haydn::bundle::finalizeExactLateSingleton(*J);
}

// Returning fnptr call (JALR_CALL): isCall, not terminator/barrier. Opcode
// identity, not peel — catalog JALR/JALR_W stay CFG RET/jump. An opcode-only
// already-long test must not skip a mid-block printf call as LUI+ADDI+JALR.
static bool isReturningJalrCall(const MachineInstr &MI) {
  if (MI.getOpcode() == Haydn::JALR_CALL)
    return true;
  unsigned Log = haydnLogicalOpcode(MI.getOpcode());
  if (Log != Haydn::JALR && Log != Haydn::JALR_W)
    return false;
  return MI.isCall(MachineInstr::IgnoreBundle) &&
         !MI.isTerminator(MachineInstr::IgnoreBundle) &&
         !MI.isIndirectBranch(MachineInstr::IgnoreBundle) &&
         !MI.isBarrier(MachineInstr::IgnoreBundle);
}

static bool isReturningJalrCallIn(const MachineInstr &MI) {
  if (isReturningJalrCall(MI))
    return true;
  if (MI.isBundle()) {
    for (const MachineInstr *C : haydn::bundle::members(MI))
      if (isReturningJalrCall(*C))
        return true;
  }
  return false;
}

static unsigned jalrLogicalOrZero(const MachineInstr &MI) {
  // Returning JALR_CALL is not CFG JALR.
  if (isReturningJalrCall(MI))
    return 0;
  unsigned Log = haydnLogicalOpcode(MI.getOpcode());
  if (Log == Haydn::JALR || Log == Haydn::JALR_W)
    return Log;
  return 0;
}

static unsigned logicalOpcOf(const MachineInstr &MI) {
  return haydnLogicalOpcode(MI.getOpcode());
}

// In-block long form is LUI+ADDI+[near-cond+]JALR on one scratch with the
// Dest MBB on LUI and ADDI — the same sequence emitInBlockLongJump /
// insertIndirectBranch emit (RISCVInstrInfo.cpp:1294-1323 AUIPC+JALR;
// Hexagon A2_tfrsi+J2_jumpr; AIE has no insertIndirectBranch).
// Skip only that CFG form. A returning JALR_CALL is not it (va-arg-24:
// opcode-only / dest-recovery skips a far short uncond and LateConvergence
// BR then insertIndirectBranch, refused postcommit). Dest-less RET /
// computed-goto / last-JALR without the addr pair are not it. A last cycle
// that still holds a short uncond sibling is not already-long.
static bool isAlreadyLongCfgJalr(const MachineInstr &MI,
                                 const HaydnInstrInfo &TII) {
  const MachineInstr *Jalr = nullptr;
  bool SawShortUncond = false;
  auto consider = [&](const MachineInstr &C) {
    if (jalrLogicalOrZero(C) && !Jalr)
      Jalr = &C;
    if (!jalrLogicalOrZero(C) &&
        C.isUnconditionalBranch(MachineInstr::IgnoreBundle))
      SawShortUncond = true;
  };
  consider(MI);
  if (MI.isBundle()) {
    for (const MachineInstr *C : haydn::bundle::members(MI))
      consider(*C);
  }
  if (!Jalr || SawShortUncond)
    return false;
  // One D1.117/D1.153 owner: complete ordered LUI then ADDI %bb pair on
  // JALR rs (COPY/MOVE32 allowed). Returning JALR_CALL / dest-less /
  // incomplete / incoherent / swapped ADDI->LUI are not this form.
  return TII.getJalrAddrMaterializeChain(*Jalr).Complete;
}

static Register jalrJumpReg(const MachineInstr &Jalr) {
  if (Jalr.getNumOperands() >= 2 && Jalr.getOperand(1).isReg())
    return Jalr.getOperand(1).getReg();
  if (Jalr.getNumOperands() && Jalr.getOperand(0).isReg())
    return Jalr.getOperand(0).getReg();
  return Register();
}

static MachineInstr *trailingCfgJalr(MachineInstr &Last) {
  auto take = [&](MachineInstr &C) -> MachineInstr * {
    return jalrLogicalOrZero(C) ? &C : nullptr;
  };
  if (MachineInstr *J = take(Last))
    return J;
  if (Last.isBundle()) {
    for (MachineInstr *C : haydn::bundle::members(Last))
      if (MachineInstr *J = take(*C))
        return J;
  }
  return nullptr;
}

// Rewrite the complete typed LUI+ADDI pair (D1.117/D1.153 ordered
// reaching-def). Missing half, dest-mismatched, swapped-order, broken
// source, or clobbered pair named-refuses without mutating either half
// (D1.124). Do not walk other same-rs materializes (D1.103).
static bool retargetJalrAddrDest(MachineInstr &Jalr, MachineBasicBlock *NewDest,
                                 const HaydnInstrInfo &TII) {
  if (!NewDest)
    return false;
  HaydnJalrAddrMaterializeChain Chain = TII.getJalrAddrMaterializeChain(Jalr);
  if (!Chain.Complete || Chain.Incoherent)
    return false;
  auto rewriteDest = [&](MachineInstr *AMI) {
    if (!AMI)
      return;
    for (MachineOperand &MO : AMI->operands())
      if (MO.isMBB())
        MO.setMBB(NewDest);
  };
  rewriteDest(Chain.Lui);
  rewriteDest(Chain.Addi);
  return true;
}



static MachineBasicBlock *shortBranchDestOf(const HaydnInstrInfo &TII,
                                            MachineInstr &MI) {
  // Prefer a coissued short uncond over a cond. Do not match via isBranch:
  // that class includes trailing conds / ZOL metas and would drop a cond
  // (MEMORY_FAULT) or hide B beside JALR_CALL.
  const MachineInstr *Uncond = nullptr;
  const MachineInstr *Cond = nullptr;
  auto take = [&](const MachineInstr &C) {
    if (isReturningJalrCall(C) || jalrLogicalOrZero(C))
      return;
    if (C.isUnconditionalBranch(MachineInstr::IgnoreBundle))
      Uncond = &C;
    else if (C.isConditionalBranch(MachineInstr::IgnoreBundle))
      Cond = &C;
  };
  take(MI);
  if (MI.isBundle()) {
    for (const MachineInstr *C : haydn::bundle::members(MI))
      take(*C);
  }
  const MachineInstr *CF = Uncond ? Uncond : Cond;
  if (!CF)
    return nullptr;
  const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(CF->getOpcode());
  if (Log == Haydn::PseudoLoopEnd || Log == Haydn::LoopJNZ)
    return nullptr;
  return TII.getBranchDestBlock(*CF);
}

// Short uncond dest of a last cycle. A coissued returning JALR_CALL is
// isCall/isIndirect and poisons isUnconditionalBranch(AnyInBundle), which
// left a far B for LateConvergence insertIndirectBranch (va-arg-24).
static MachineBasicBlock *shortUncondDestOf(const HaydnInstrInfo &TII,
                                            MachineInstr &MI) {
  const MachineInstr *Uncond = nullptr;
  auto take = [&](const MachineInstr &C) {
    if (isReturningJalrCall(C) || jalrLogicalOrZero(C))
      return;
    if (C.isUnconditionalBranch(MachineInstr::IgnoreBundle))
      Uncond = &C;
  };
  take(MI);
  if (MI.isBundle()) {
    for (const MachineInstr *C : haydn::bundle::members(MI))
      take(*C);
  }
  if (!Uncond)
    return nullptr;
  return TII.getBranchDestBlock(*Uncond);
}

// Analyzed cond is the last conditional before trailing unconds
// (AIEBaseInstrInfo.cpp:209-222 / RISCVInstrInfo.cpp:1349-1354: TBB from
// the cond, FBB from the trailing uncond). Walk past a returning JALR_CALL
// (va-arg-24: last may be the call with a preceding far cond) and past a
// dest-recovered CFG JALR (long-form trailing uncond). Dest-less CFG
// JALR/RET is a barrier: do not walk past it.
//
// Haydn VLIW overlay: a trailing uncond cycle may coissue real ALU
// (BUNDLE{ADD32,B} + two BEQZ). AIE analyzeBranch counts terminators
// only (AIEBaseInstrInfo.cpp:164-192) so an ALU sibling is not a
// stop; stopping on it here left "analyzed TBB has no cond child" and
// Arm A skipped "selected trailing uncond missing" (refuse, not fatal).
static bool skipNonCondForAnalyzedWalk(const MachineInstr &C,
                                       const HaydnInstrInfo &TII) {
  if (C.isDebugInstr() || C.isCFIInstruction() || C.isImplicitDef() ||
      C.isKill() || C.isMetaInstruction())
    return true;
  if (isReturningJalrCall(C))
    return true;
  unsigned L = logicalOpcOf(C);
  if (L == Haydn::NOP || L == Haydn::JAL || L == Haydn::JAL_W ||
      L == Haydn::JAL_TCO || L == Haydn::JALR_TCO)
    return true;
  if (C.isUnconditionalBranch(MachineInstr::IgnoreBundle) &&
      !jalrLogicalOrZero(C))
    return true;
  if (jalrLogicalOrZero(C))
    return TII.getBranchDestBlock(C) != nullptr;
  // Coissued compute is not a cond and not a CFG barrier.
  if (!C.isBranch(MachineInstr::IgnoreBundle) &&
      !C.isTerminator(MachineInstr::IgnoreBundle) &&
      !C.isCall(MachineInstr::IgnoreBundle) &&
      !C.isIndirectBranch(MachineInstr::IgnoreBundle) &&
      !C.isBarrier(MachineInstr::IgnoreBundle) &&
      !C.isReturn(MachineInstr::IgnoreBundle))
    return true;
  return false;
}

static MachineInstr *findAnalyzedCondMI(MachineBasicBlock &MBB,
                                        const HaydnInstrInfo &TII) {
  for (MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
       I != MBB.end();) {
    MachineInstr &Top = *I;
    auto stepBack = [&]() -> bool {
      if (I == MBB.begin())
        return false;
      --I;
      return true;
    };
    if (Top.isDebugInstr() || Top.isCFIInstruction()) {
      if (!stepBack())
        break;
      continue;
    }
    if (Top.isBundle()) {
      MachineInstr *Cond = nullptr;
      bool SkipOnly = true;
      for (MachineInstr *C : haydn::bundle::members(Top)) {
        if (C->isConditionalBranch(MachineInstr::IgnoreBundle))
          Cond = C;
        else if (!skipNonCondForAnalyzedWalk(*C, TII))
          SkipOnly = false;
      }
      if (Cond)
        return Cond;
      if (SkipOnly) {
        if (!stepBack())
          break;
        continue;
      }
      break;
    }
    if (Top.isConditionalBranch())
      return &Top;
    if (skipNonCondForAnalyzedWalk(Top, TII)) {
      if (!stepBack())
        break;
      continue;
    }
    break;
  }
  return nullptr;
}

static unsigned llvmOpcodeForSymbol(const TargetInstrInfo &TII,
                                    StringRef Symbol) {
  const unsigned N = TII.getNumOpcodes();
  for (unsigned Opc = 1; Opc < N; ++Opc)
    if (TII.getName(Opc) == Symbol)
      return Opc;
  return 0;
}

// Post-stamp invert stays on the committed member's Mode/Entry/Unit/Type
// (pipeline.md: do not change row/unit/alternate). AIE has no BR.
// Pre-stamp logicals take NewOpc unchanged.
static unsigned sameRowInvertedOpcode(const MachineInstr &MI, unsigned NewOpc,
                                      const HaydnInstrInfo &TII) {
  const StringRef CurName = TII.getName(MI.getOpcode());
  if (!isGeneratedFormatEMemberName(CurName))
    return NewOpc;
  const haydn::format_e::FormatEMemberRec *Cur = nullptr;
  for (unsigned I = 0; I < haydn::format_e::FormatEMemberCount; ++I) {
    if (CurName == haydn::format_e::FormatEMembers[I].MemberSymbol) {
      Cur = &haydn::format_e::FormatEMembers[I];
      break;
    }
  }
  if (!Cur)
    return 0;
  const std::string NewLog =
      haydn::format_e::peelLogicalOpcodeName(TII.getName(NewOpc));
  if (NewLog.empty())
    return 0;
  for (unsigned I = 0; I < haydn::format_e::FormatEMemberCount; ++I) {
    const haydn::format_e::FormatEMemberRec &M =
        haydn::format_e::FormatEMembers[I];
    if (M.IsNop || M.Mode != Cur->Mode || M.EntryIdx != Cur->EntryIdx ||
        M.Unit != Cur->Unit || M.TypeCode != Cur->TypeCode)
      continue;
    if (!StringRef(M.Logical).equals_insensitive(NewLog))
      continue;
    if (unsigned Opc = llvmOpcodeForSymbol(TII, M.MemberSymbol))
      return Opc;
  }
  return 0;
}

static bool retargetAnalyzedCond(MachineInstr &MI, unsigned NewOpc,
                                 MachineBasicBlock *NewDest,
                                 const HaydnInstrInfo &TII) {
  if (!NewDest)
    return false;
  const unsigned DestOpc = sameRowInvertedOpcode(MI, NewOpc, TII);
  if (!DestOpc)
    return false;
  MI.setDesc(TII.get(DestOpc));
  for (MachineOperand &MO : MI.operands()) {
    if (MO.isMBB()) {
      MO.setMBB(NewDest);
      return true;
    }
  }
  return false;
}

static bool condDisplacementInRange(MachineBasicBlock &MBB,
                                    const HaydnInstrInfo &TII,
                                    ArrayRef<int64_t> BlockStarts,
                                    MachineInstr &CondMI) {
  MachineInstr *Root = haydn::bundle::bundleRootOf(CondMI);
  MachineInstr &Site = Root ? *Root : CondMI;
  MachineBasicBlock *Dest = TII.getBranchDestBlock(CondMI);
  if (!Dest || Dest->getNumber() < 0 ||
      Dest->getNumber() >= (int)BlockStarts.size() ||
      BlockStarts[Dest->getNumber()] < 0)
    return false;
  MachineBasicBlock::iterator TI = Site.getIterator();
  const int64_t Off =
      BlockStarts[Dest->getNumber()] -
      (BlockStarts[MBB.getNumber()] + estimateLayoutInstrOffset(MBB, TI, TII));
  // D1.142: explicit site MI unwrap, not the opcode-only BUNDLE handshake.
  return TII.isBranchOffsetInRange(Site, Off);
}

// D1.141: remaining short conds after Arm A/B. Arm A only sees analyzed
// TBB/FBB; Arm B only a trailing uncond; generic BR's opcode-only BUNDLE
// hook is always-in-range unless the D1.142 same-MI handshake fired on a
// trailing-JALR dest recovery (BranchRelaxation.cpp:726-740). ZOL metas
// stay FixupHwLoops. Pre-stamp leaves the site for BR split; the caller
// fatals post-stamp (owner wall, not GR1.8).
static bool hasFarShortCondWithoutRangeOwner(MachineBasicBlock &MBB,
                                            const HaydnInstrInfo &TII,
                                            ArrayRef<int64_t> BlockStarts) {
  if (MBB.empty() || MBB.getNumber() < 0 ||
      MBB.getNumber() >= (int)BlockStarts.size() ||
      BlockStarts[MBB.getNumber()] < 0)
    return false;
  auto consider = [&](MachineInstr &C) -> bool {
    if (!C.isConditionalBranch(MachineInstr::IgnoreBundle))
      return false;
    unsigned L = logicalOpcOf(C);
    if (L == Haydn::PseudoLoopEnd || L == Haydn::LoopJNZ)
      return false;
    if (!TII.getBranchDestBlock(C))
      return false;
    return !condDisplacementInRange(MBB, TII, BlockStarts, C);
  };
  for (MachineBasicBlock::iterator TI = MBB.getLastNonDebugInstr();
       TI != MBB.end(); --TI) {
    if (TI->isBundle()) {
      for (MachineInstr *C : haydn::bundle::members(*TI))
        if (consider(*C))
          return true;
    } else if (consider(*TI)) {
      return true;
    }
    if (isReturningJalrCallIn(*TI)) {
      if (TI == MBB.begin())
        break;
      continue;
    }
    if (!TI->isBranch(MachineInstr::IgnoreBundle) && !TI->isTerminator() &&
        !TI->isUnconditionalBranch() && !TI->isConditionalBranch())
      break;
    if (TI == MBB.begin())
      break;
  }
  return false;
}

// In-block both-far overlay: keep the dest-recovered JALR and select TBB vs
// FBB into its rs (SEQ32 of the 1-reg cond against a zeroed temp, then
// MOVT/MOVF). No new MBB. BNEZ taken (reg!=0) → TBB; not-taken → FBB.
static bool rewriteBothFarCondOntoJalr(
    MachineBasicBlock &MBB, const HaydnInstrInfo &TII,
    const haydn::hwloop::FunctionPhysLiveness &FPL,
    ArrayRef<MachineOperand> Cond, MachineBasicBlock *TBB,
    MachineBasicBlock *FBB, MachineInstr &Jalr, const DebugLoc &DL) {
  if (!TBB || !FBB || Cond.size() != 2 || !Cond[0].isImm() || !Cond[1].isReg())
    return false;
  const unsigned Opc = haydn::format_e::logicalOpcodeOrSelf(Cond[0].getImm());
  const bool IsBnez = Opc == Haydn::BNEZ_W || Opc == Haydn::BNEZ;
  const bool IsBeqz = Opc == Haydn::BEQZ_W || Opc == Haydn::BEQZ;
  if (!IsBnez && !IsBeqz)
    return false;
  const Register JumpReg = jalrJumpReg(Jalr);
  const Register CondReg = Cond[1].getReg();
  if (!JumpReg || !CondReg)
    return false;
  // Inserts sit BEFORE the JALR, which is a barrier. D1.98: both scratches
  // go through pickInBlockScratch (pristine-CSR skip + tail-overlap +
  // Exclude). JumpReg/CondReg are tail operands so they are already
  // refused; Exclude keeps AddrT and Tmp disjoint.
  const TargetRegisterInfo &TRI = TII.getRegisterInfo();
  const Register AddrT = pickInBlockScratch(MBB, TRI, FPL);
  if (!AddrT)
    return false;
  const Register Tmp =
      pickInBlockScratch(MBB, TRI, FPL, /*DeadOn=*/nullptr, AddrT);
  if (!Tmp)
    return false;

  MachineInstr *CondMI = findAnalyzedCondMI(MBB, TII);
  if (!CondMI)
    return false;
  // Insert before the JALR cycle. MBB::iterator asserts isBundledWithPred
  // (MachineInstrBundleIterator.h:132-136); trailingCfgJalr may return a
  // BUNDLE child. Resolve the root first (HaydnLatencyStalls.cpp:418-450).
  MachineInstr *Root = haydn::bundle::bundleRootOf(Jalr);
  MachineInstr &Site = Root ? *Root : Jalr;
  MachineBasicBlock::iterator Ins = Site.getIterator();

  MachineInstr *Lui = emitExactLateDef(MBB, Ins, DL, TII, Haydn::LUI, AddrT,
                                       [&](MachineInstrBuilder MIB) {
                                         MIB.addMBB(TBB);
                                       });
  Ins = afterEmittedCycle(*Lui);
  MachineInstr *Addi =
      emitExactLateDef(MBB, Ins, DL, TII, Haydn::ADDI32_W, AddrT,
                       [&](MachineInstrBuilder MIB) {
                         MIB.addReg(AddrT).addMBB(TBB);
                       });
  Ins = afterEmittedCycle(*Addi);
  MachineInstr *Xor = emitExactLateDef(MBB, Ins, DL, TII, Haydn::XOR32, Tmp,
                                       [&](MachineInstrBuilder MIB) {
                                         MIB.addReg(AddrT).addReg(AddrT);
                                       });
  Ins = afterEmittedCycle(*Xor);
  MachineInstr *Seq = emitExactLateDef(MBB, Ins, DL, TII, Haydn::SEQ32, Tmp,
                                       [&](MachineInstrBuilder MIB) {
                                         MIB.addReg(CondReg).addReg(Tmp);
                                       });
  Ins = afterEmittedCycle(*Seq);
  // SEQ32 Tmp=(CondReg==0). BNEZ taken when CondReg!=0 → Tmp bit0==0 → MOVF
  // selects TBB. BEQZ taken when CondReg==0 → Tmp bit0==1 → MOVT selects TBB.
  const unsigned MovOpc = IsBnez ? Haydn::MOVF32 : Haydn::MOVT32;
  emitExactLateDef(MBB, Ins, DL, TII, MovOpc, JumpReg,
                   [&](MachineInstrBuilder MIB) {
                     MIB.addReg(JumpReg).addReg(AddrT).addReg(Tmp);
                   });

  // Vacate the cond member without dissolving its packet (post-stamp
  // same-row NOP / pre-stamp restamp). D1.142 already unwraps the site
  // MI for isBranchOffsetInRange; do not extractBareFromBundle.
  if (TII.eraseSelectedBranch(*CondMI) == 0)
    return false;
  return true;
}

// getFirstTerminator is the start of the TRAILING terminator sequence and
// returns end() when a coissued survivor sits after remaining controls.
// Walk forward for the first actual control, including a BUNDLE whose
// children are branches/returns (BUNDLE roots are not isBranch).
static bool isControlInstr(const MachineInstr &MI) {
  if (MI.isDebugInstr() || MI.isCFIInstruction())
    return false;
  if (MI.isTerminator() || MI.isBranch() || MI.isIndirectBranch() ||
      MI.isReturn())
    return true;
  if (!MI.isBundle())
    return false;
  for (const MachineInstr *C : haydn::bundle::members(MI)) {
    if (C->isTerminator() || C->isBranch(MachineInstr::IgnoreBundle) ||
        C->isIndirectBranch(MachineInstr::IgnoreBundle) ||
        C->isReturn(MachineInstr::IgnoreBundle))
      return true;
  }
  return false;
}

static MachineBasicBlock::iterator firstControlMI(MachineBasicBlock &MBB) {
  for (MachineInstr &MI : MBB) {
    if (isControlInstr(MI))
      return MI.getIterator();
  }
  return MBB.end();
}

// Padding Keep filter matching HaydnInstrInfo::eraseSelectedBranch.
static bool isUncondCyclePadding(const MachineInstr &MI) {
  if (MI.isDebugInstr() || MI.isMetaInstruction() || MI.isImplicitDef() ||
      MI.isKill() || MI.isCFIInstruction() || MI.isPosition())
    return true;
  return haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode()) == Haydn::NOP;
}

static void collectIncomingPhysReads(const MachineInstr &MI,
                                     SmallVectorImpl<Register> &Reads) {
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.getReg().isPhysical() || !MO.readsReg())
      continue;
    // InternalRead is RAW/new-value (MachineInstrBundle.cpp:157-171
    // LocalDefs). Incoming WAR/snapshot is the legal read
    // (HaydnIntraCycleRAW.h:218-219; LivePhysRegs.cpp:68-73 stepBackward
    // removeDefs then addUses).
    if (MO.isInternalRead())
      continue;
    Reads.push_back(MO.getReg());
  }
}

// D1.135: spliceTrailingNonTermsBeforeFirstControl moves eraseSelectedBranch
// Keep MIs before firstControlMI. Pre-rewrite those defs execute one cycle
// AFTER the retained cond reads (legal cross-cycle WAR); post-splice they
// precede the read. Skipping the splice is not legal either: the generic
// verifier forbids non-terminators after the first terminator
// (HaydnHardwareLoops.cpp:2271-2274). Prove Keep defs disjoint from retained
// control incoming reads before any invert/erase; overlap refuses the rewrite
// (pre-stamp defer, post-stamp fatal). AIE/Hexagon/RISCV erase at most the
// trailing uncond and have no VLIW coissue-ALU overlay
// (AIEBaseInstrInfo.cpp:237-266 / HexagonInstrInfo.cpp:605-625 /
// RISCVInstrInfo.cpp:1361-1390).
static bool uncondCycleSurvivorDefsOverlapRetainedControlReads(
    const MachineBasicBlock &MBB, const MachineInstr &Uncond,
    const TargetRegisterInfo &TRI) {
  const MachineInstr *UncondRoot = &Uncond;
  if (const MachineInstr *R = haydn::bundle::bundleRootOf(Uncond))
    UncondRoot = R;

  SmallVector<const MachineInstr *, 4> Selected;
  if (Uncond.isBundle()) {
    for (const MachineInstr *C : haydn::bundle::members(Uncond))
      if (C->isBranch(MachineInstr::IgnoreBundle))
        Selected.push_back(C);
  } else if (Uncond.isBundledWithPred()) {
    if (!Uncond.isBranch(MachineInstr::IgnoreBundle))
      return false;
    Selected.push_back(&Uncond);
  } else {
    return false; // bare uncond: erase drops the cycle, nothing to splice
  }
  if (Selected.empty())
    return false;

  SmallVector<Register, 8> Defs;
  if (UncondRoot->isBundle()) {
    for (const MachineInstr *C : haydn::bundle::members(*UncondRoot)) {
      if (llvm::is_contained(Selected, C) || isUncondCyclePadding(*C))
        continue;
      for (const MachineOperand &MO : C->operands())
        if (MO.isReg() && MO.getReg().isPhysical() && MO.isDef())
          Defs.push_back(MO.getReg());
    }
  }
  if (Defs.empty())
    return false;

  SmallVector<Register, 8> Reads;
  for (const MachineInstr &MI : MBB) {
    if (&MI == UncondRoot) {
      if (!MI.isBundle())
        continue;
      for (const MachineInstr *C : haydn::bundle::members(MI)) {
        if (llvm::is_contained(Selected, C))
          continue;
        if (!C->isBranch(MachineInstr::IgnoreBundle) &&
            !C->isReturn(MachineInstr::IgnoreBundle) &&
            !C->isIndirectBranch(MachineInstr::IgnoreBundle) &&
            !C->isTerminator(MachineInstr::IgnoreBundle))
          continue;
        collectIncomingPhysReads(*C, Reads);
      }
      continue;
    }
    if (!isControlInstr(MI))
      continue;
    collectIncomingPhysReads(MI, Reads);
    if (MI.isBundle())
      for (const MachineInstr *C : haydn::bundle::members(MI))
        collectIncomingPhysReads(*C, Reads);
  }

  for (Register D : Defs)
    for (Register R : Reads)
      if (TRI.regsOverlap(D, R))
        return true;
  return false;
}

// True = caller must continue (pre-stamp) or does not return (post-stamp fatal).
static bool refuseUncondSurvivorWarSplice(const MachineBasicBlock &MBB,
                                          const MachineInstr &Uncond,
                                          const TargetRegisterInfo &TRI,
                                          bool PostCommit) {
  if (!uncondCycleSurvivorDefsOverlapRetainedControlReads(MBB, Uncond, TRI))
    return false;
  LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": " << MBB.getParent()->getName()
                    << " bb." << MBB.getNumber()
                    << " skip: uncond survivor defs overlap retained control "
                       "reads (splice would invert WAR)\n");
  if (PostCommit)
    report_fatal_error(
        DEBUG_TYPE +
            Twine(": uncond survivor defs overlap retained control reads at ") +
            MBB.getParent()->getName() + " bb." + Twine(MBB.getNumber()) +
            " (splice would invert WAR)",
        /*GenCrashDiag=*/false);
  return true;
}

// Coissued survivors of eraseSelectedBranch can sit after remaining
// controls. Splice them before the first control so LUI/ADDI/JALR form a
// verifier-legal tail. MBB::iterator splice of a BUNDLE root moves the
// whole cycle (header + children). D1.135: callers prove Keep defs disjoint
// from retained control reads (refuseUncondSurvivorWarSplice) before erase.
static void spliceTrailingNonTermsBeforeFirstControl(MachineBasicBlock &MBB) {
  MachineBasicBlock::iterator First = firstControlMI(MBB);
  if (First == MBB.end())
    return;
  SmallVector<MachineInstr *, 4> Move;
  for (MachineBasicBlock::iterator I = std::next(First), E = MBB.end(); I != E;
       ++I) {
    if (I->isDebugInstr() || I->isCFIInstruction())
      continue;
    if (isControlInstr(*I))
      continue;
    Move.push_back(&*I);
  }
  for (MachineInstr *MI : Move)
    MBB.splice(First, &MBB, MI->getIterator());
}

// D1.50 ordered-terminator law (GOALS "Ordered branch-pipeline" item 2:
// one ordered-terminator description shared by D1.50/D1.61). A complete
// far-site rewrite may replace ONLY the control child this description
// names, in tail order, with its TBB/FBB/layout relationship proven:
//
//   * every control cycle of the tail is NAMED IN ORDER (root or bare
//     control MI; bundle-interior control children enumerated);
//   * SelectedUncond is the UNIQUE trailing unconditional child of the
//     analyzed vocabulary (B / JAL-to-MBB; never JALR forms)
//     and its cycle is the LAST control cycle — a selected uncond that
//     is not trailing is a corruption description, never a selection;
//   * its dest IS the analyzed FBB (two-way tail) or the analyzed TBB
//     (uncond-only tail) — the analyzeBranch relationship, so a
//     non-analyzed successor can never be the erase/rewrite target
//     (HexagonBranchRelaxation.cpp:153-181 range-tests the analyzed
//     terminators only);
//   * when analyzeBranch produced a Cond, the analyzed cond child (the
//     last conditional before the trailing uncond,
//     AIEBaseInstrInfo.cpp:209-222) exists and owns the analyzed TBB.
//     ZOL metas (PseudoLoopEnd/LoopJNZ) own TBB the same way and are
//     named through the same vocabulary scan.
//
// The description is BUILT and CHECKED BEFORE any mutation; every erase
// arm below (ZOL, Arm A, Arm B) obtains its selected member through this
// law only — the retired findTrailingUncondMI probe matched "an" uncond
// child; this law adds the order/uniqueness/analyzed-dest proof. Empty
// return = the tail satisfies the law (SelectedUncond may still be null:
// nothing erasable). Nonempty = named violation (pre-stamp defer,
// post-stamp fatal).
struct OrderedTerminatorTail {
  // Control cycles (roots/bare control MIs) that hold uncond children,
  // in program order.
  SmallVector<MachineInstr *, 4> ControlRoots;
  // The unique trailing unconditional child (root or bundled member).
  MachineInstr *SelectedUncondChild = nullptr;
  // Cycle owning SelectedUncondChild (== the child when bare).
  MachineInstr *SelectedRoot = nullptr;
};

// Unconditional short-branch vocabulary child (IgnoreBundle). JALR/JALR_W
// / JALR_CALL are the already-long / call form, never an erase selection.
static bool isUncondBranchChild(const MachineInstr &C) {
  if (!C.isUnconditionalBranch(MachineInstr::IgnoreBundle))
    return false;
  if (isReturningJalrCall(C) || jalrLogicalOrZero(C))
    return false;
  return true;
}

// ZOL metas own the analyzed TBB outside the cond vocabulary; the
// ordered-dest proof for them is dest(TBB) presence. getBranchDestBlock
// is reached only through isBranch children (its fallthrough is
// llvm_unreachable on non-branch opcodes).
static bool isHwLoopAnalyzeCondOf(MachineBasicBlock *TBB,
                                  MachineBasicBlock &MBB,
                                  const HaydnInstrInfo &TII) {
  auto isZolMetaTo = [&](const MachineInstr &C) -> bool {
    if (!C.isBranch(MachineInstr::IgnoreBundle))
      return false;
    unsigned L = haydn::format_e::logicalOpcodeOrSelf(C.getOpcode());
    return (L == Haydn::PseudoLoopEnd || L == Haydn::LoopJNZ) &&
           TII.getBranchDestBlock(C) == TBB;
  };
  for (MachineInstr &Root : MBB) {
    if (Root.isBundle()) {
      for (MachineInstr *C : haydn::bundle::members(Root))
        if (isZolMetaTo(*C))
          return true;
    } else if (isZolMetaTo(Root)) {
      return true;
    }
  }
  return false;
}

static std::string describeOrderedTerminatorTail(
    MachineBasicBlock &MBB, const HaydnInstrInfo &TII, MachineBasicBlock *TBB,
    MachineBasicBlock *FBB, OrderedTerminatorTail &Tail) {
  const MachineFunction &MF = *MBB.getParent();
  auto fatal = [&](const Twine &Why) -> std::string {
    return ("ordered terminator tail: " + Why + " at " + MF.getName() +
            " bb." + Twine(MBB.getNumber()))
        .str();
  };
  auto destOfChild = [](const HaydnInstrInfo &II,
                        const MachineInstr &C) -> MachineBasicBlock * {
    if (!C.isBranch(MachineInstr::IgnoreBundle))
      return nullptr;
    return II.getBranchDestBlock(C);
  };

  MachineBasicBlock *UncondDest = nullptr;
  bool SawUncondCycle = false;
  for (MachineInstr &Root : MBB) {
    SmallVector<MachineInstr *, 4> Unconds;
    if (Root.isBundle()) {
      for (MachineInstr *C : haydn::bundle::members(Root)) {
        if (!isUncondBranchChild(*C))
          continue;
        Unconds.push_back(C);
      }
      if (Unconds.empty())
        continue;
      Tail.ControlRoots.push_back(&Root);
      if (Unconds.size() > 1)
        return fatal("multiple unconditional children in one cycle");
      if (SawUncondCycle)
        return fatal("multiple unconditional control cycles in tail");
      SawUncondCycle = true;
      Tail.SelectedUncondChild = Unconds[0];
      Tail.SelectedRoot = &Root;
      UncondDest = destOfChild(TII, *Unconds[0]);
      continue;
    }
    if (!isUncondBranchChild(Root))
      continue;
    Tail.ControlRoots.push_back(&Root);
    if (SawUncondCycle)
      return fatal("multiple unconditional control cycles in tail");
    SawUncondCycle = true;
    Tail.SelectedUncondChild = &Root;
    Tail.SelectedRoot = &Root;
    UncondDest = destOfChild(TII, Root);
  }

  // Order law: the selected uncond cycle holds the block's LAST
  // non-debug instr (a selected uncond that is not trailing leaves a
  // control cycle after it that could hide a guard).
  if (Tail.SelectedUncondChild) {
    MachineBasicBlock::iterator Last = MBB.getLastNonDebugInstr();
    if (Last == MBB.end() || &*Last != Tail.SelectedRoot)
      return fatal("selected uncond is not the trailing control cycle");
  }

  // Analyzed-dest law: selected uncond owns the analyzed FBB (two-way) or
  // the analyzed TBB (uncond-only). analyzeBranch: TBB from the cond, FBB
  // from the trailing uncond; uncond-only → TBB.
  if (Tail.SelectedUncondChild && UncondDest) {
    if (FBB && UncondDest != FBB)
      return fatal("selected uncond dest is not the analyzed FBB");
    if (!FBB && TBB && UncondDest != TBB)
      return fatal("selected uncond dest is not the analyzed TBB");
  }

  // Cond/TBB relationship proof (analyzeBranch produced a Cond): the
  // analyzed cond child must exist and own the analyzed TBB. ZOL metas
  // (PseudoLoopEnd/LoopJNZ) own TBB the same way and are exempt from the
  // invert proof — they are uninvertible and stay as the backedge. An
  // UNCOND-ONLY tail (Cond empty, TBB owned by the trailing uncond
  // itself) has no cond child to prove — the uncond-dest law above
  // already proved UncondDest == TBB, which IS the analyzed
  // relationship for that shape (AIEBaseInstrInfo analyzeBranch:
  // uncond-only → TBB from the uncond, no Cond).
  bool UncondOnlyTail = Tail.SelectedUncondChild && UncondDest &&
                        UncondDest == TBB && !FBB;
  // Dest-recovered trailing CFG JALR owns TBB the same way a short B does
  // (JALR is not isUncondBranchChild). Returning JALR_CALL is not this form.
  if (!UncondOnlyTail && TBB && !Tail.SelectedUncondChild && !FBB) {
    MachineBasicBlock::iterator Last = MBB.getLastNonDebugInstr();
    if (Last != MBB.end()) {
      const MachineInstr *Jalr = nullptr;
      auto takeJalr = [&](const MachineInstr &C) {
        if (jalrLogicalOrZero(C) && !Jalr)
          Jalr = &C;
      };
      takeJalr(*Last);
      if (Last->isBundle())
        for (const MachineInstr *C : haydn::bundle::members(*Last))
          takeJalr(*C);
      if (Jalr && TII.getBranchDestBlock(*Jalr) == TBB)
        UncondOnlyTail = true;
    }
  }
  if (TBB && !UncondOnlyTail &&
      !isHwLoopAnalyzeCondOf(TBB, MBB, TII)) {
    MachineInstr *CondChild = findAnalyzedCondMI(MBB, TII);
    if (!CondChild)
      return fatal("analyzed TBB has no cond child");
    if (destOfChild(TII, *CondChild) != TBB)
      return fatal("analyzed cond child does not own the analyzed TBB");
  }
  return {};
}

// D1.50 selected-member discovery THROUGH the ordered-terminator law:
// returns the unique trailing uncond child the law names (nullptr when
// the tail has none). A violation is pre-stamp deferred (BR trampoline)
// and post-stamp a named fatal — never an unnamed erase. MUST be called
// BEFORE any tail mutation of this site (invert/retarget): the
// description proves the ORIGINAL analyzed relationships, and the
// returned pointer stays valid across the arm's cond-cycle surgery (the
// selected uncond lives in the trailing cycle; invert retargets the
// cond member in place).
static MachineInstr *selectedTrailingUncondThroughLaw(
    MachineBasicBlock &MBB, const HaydnInstrInfo &TII, MachineBasicBlock *TBB,
    MachineBasicBlock *FBB, bool PostCommit) {
  OrderedTerminatorTail Tail;
  std::string Bad = describeOrderedTerminatorTail(MBB, TII, TBB, FBB, Tail);
  if (!Bad.empty()) {
    if (!PostCommit)
      return nullptr; // pre-stamp: leave for BR trampoline (no RestoreBB)
    report_fatal_error(Twine(DEBUG_TYPE) + ": " + Twine(Bad),
                       /*GenCrashDiag=*/false);
  }
  return Tail.SelectedUncondChild;
}

// Erase a law-selected trailing uncond (root or bundled member). Leaves
// PseudoLoopEnd / LoopJNZ and preceding controls in place. Selected-member
// restamp is HaydnInstrInfo::eraseSelectedBranch
// (HaydnHWLoopDemote.cpp:742-773 eraseSetMemberAndRecommitSiblings peer).
// A failed erase of the selected member is fatal so a short B cannot
// remain beside the JALR this rewrite is about to append.
static void eraseSelectedTrailingUncond(MachineBasicBlock &MBB,
                                        const HaydnInstrInfo &TII,
                                        MachineInstr &Uncond) {
  if (TII.eraseSelectedBranch(Uncond) == 0)
    report_fatal_error(
        DEBUG_TYPE +
            Twine(": failed to erase selected trailing uncond at ") +
            MBB.getParent()->getName() + " bb." + Twine(MBB.getNumber()) +
            " (short B would remain beside JALR)",
        /*GenCrashDiag=*/false);
}

static bool rebuildLayoutSites(haydn::LayoutSiteTable &Sites,
                               MachineFunction &MF, const HaydnInstrInfo &TII,
                               bool PostCommit) {
  std::string Err;
  if (Sites.collect(MF, TII, Err))
    return true;
  if (PostCommit)
    report_fatal_error(DEBUG_TYPE + Twine(": ") + Err, /*GenCrashDiag=*/false);
  return false;
}

static void stampLongTemplate(haydn::LayoutSite &Site, bool InvertNear) {
  Site.raiseRank(haydn::LayoutSiteRank::LongTemplate);
  Site.Template = InvertNear ? haydn::LayoutTemplateId::InvertNearCondJalr
                             : haydn::LayoutTemplateId::InBlockJalr;
}

// D1.173: LBN consults Dest + Rank, not a second size oracle. Span math is
// computeLayoutBlockStarts (padLayoutBytesForMBBAlign / alignTo /
// getAlignment — HexagonBranchRelaxation.cpp:100-104 alignTo heuristic;
// ARM ImmBranch is MI+MaxDisp only, ARMConstantIslandPass.cpp:188-197).
// Rank>=LongTemplate is already the in-block template; do not re-promote.
static bool siteDisplacementOutOfRange(const haydn::LayoutSite &S,
                                       const HaydnInstrInfo &TII,
                                       ArrayRef<int64_t> BlockStarts) {
  if (!S.Member || !S.Dest || !S.Member->getParent())
    return false;
  MachineBasicBlock &MBB = *S.Member->getParent();
  if (MBB.getNumber() < 0 || MBB.getNumber() >= (int)BlockStarts.size() ||
      BlockStarts[MBB.getNumber()] < 0)
    return false;
  if (S.Dest->getNumber() < 0 ||
      S.Dest->getNumber() >= (int)BlockStarts.size() ||
      BlockStarts[S.Dest->getNumber()] < 0)
    return false;
  MachineInstr *Root = S.Root ? S.Root : S.Member;
  MachineBasicBlock::iterator TI = Root->getIterator();
  const int64_t Off =
      BlockStarts[S.Dest->getNumber()] -
      (BlockStarts[MBB.getNumber()] + estimateLayoutInstrOffset(MBB, TI, TII));
  return !TII.isBranchOffsetInRange(*S.Member, Off);
}

static bool isLongTemplateMember(const haydn::LayoutSiteTable &Sites,
                                 const MachineInstr *MI) {
  if (!MI)
    return false;
  const haydn::LayoutSite *S = Sites.findByMember(MI);
  return S && S->Rank >= haydn::LayoutSiteRank::LongTemplate;
}

// Iterate the table (ARM ImmBranch vector; Haydn overlay is Rank/Dest).
// HWLoop sites stay the H closer. Branches first, then calls. Skip
// Rank>=LongTemplate (classify re-derives InBlockJalr on dest-recovered
// JALR). Keep in-range FitPatch. Pending MBBs still run Arm A/B.
static void collectFarFitPatchMBBs(const haydn::LayoutSiteTable &Sites,
                                   const HaydnInstrInfo &TII,
                                   ArrayRef<int64_t> BlockStarts,
                                   SmallPtrSetImpl<MachineBasicBlock *> &Out) {
  auto consider = [&](haydn::LayoutSiteKind Kind) {
    for (const haydn::LayoutSite &S : Sites.sites()) {
      if (S.Kind != Kind || !S.Member || !S.Member->getParent())
        continue;
      if (S.Rank >= haydn::LayoutSiteRank::LongTemplate) {
        LLVM_DEBUG(dbgs() << DEBUG_TYPE
                          << ": skip re-promote Rank>=LongTemplate\n");
        continue;
      }
      if (!S.canFitPatch())
        continue;
      if (!siteDisplacementOutOfRange(S, TII, BlockStarts))
        continue;
      Out.insert(S.Member->getParent());
    }
  };
  consider(haydn::LayoutSiteKind::Branch);
  consider(haydn::LayoutSiteKind::Call);
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
  // Pre-stamp, LBN still runs first so every far site uses the in-block
  // template; pre-S1 BR trampoline is leftover (RestoreBB unused). Its
  // fixupConditionalBranch can assert on a multi-cond/unanalyzable tail
  // after earlier same-seat rewrites shift layout (nsichneu: bb.0's
  // inverted-B rewrite made bb.2's tail unanalyzable before BR reached
  // it). The in-block rewrite runs first; BR only verifies.
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();

  const auto &TII =
      *static_cast<const HaydnInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  bool Changed = false;
  // HWLoop Off1/Off2 FitPatch first, branches/calls last (GR1.4 library
  // closeRetainedHwLoops; GR1.5 in-block templates). No-op when unstamped.
  // Hexagon runs FixupHwLoops then its own BR
  // (HexagonTargetMachine.cpp:476-493); AIE has empty PreEmit.
  if (FuncInfo && FuncInfo->hasPostCommitBlockBudget())
    Changed |= haydn::hwloop::closeRetainedHwLoops(MF, TII);

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
    // D1.167: materialize internal alignment packets before span math
    // (HexagonBranchRelaxation.cpp:100-104 alignTo). ClearMetadata=false
    // during closer waves so later range growth can re-pad. AIE padRegions
    // (AIEMachineAlignment.cpp:370-424) overlay, no elongation. Rank is
    // not a second size oracle.
    if (FuncInfo && FuncInfo->hasPostCommitBlockBudget())
      Changed |= haydn::padInternalMBBAlignment(MF, TII,
                                                /*ClearMetadata=*/false);
    SmallVector<int64_t, 32> BlockStarts;
    computeLayoutBlockStarts(MF, TII, BlockStarts);

    // D1.61: rebuild the whole-function physical-liveness fixed point
    // for this iteration. Each in-block rewrite mutates MIR (installs
    // LUI/ADDI/JALR); the rebuild keeps later sites in the same pass
    // honest. The build is read-only over the current MIR.
    haydn::hwloop::FunctionPhysLiveness FPL;
    FPL.build(MF);

    haydn::LayoutSiteTable Sites;
    const bool PostCommitStamp =
        FuncInfo && FuncInfo->hasPostCommitBlockBudget();
    if (!rebuildLayoutSites(Sites, MF, TII, PostCommitStamp))
      break;

    SmallPtrSet<MachineBasicBlock *, 8> FarFitPatchMBBs;
    collectFarFitPatchMBBs(Sites, TII, BlockStarts, FarFitPatchMBBs);
    LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": layout-site Rank consult ("
                      << Sites.sites().size() << " sites, "
                      << FarFitPatchMBBs.size()
                      << " far FitPatch MBB(s))\n");
    if (FarFitPatchMBBs.empty())
      break;

    bool IterChanged = false;

  for (MachineBasicBlock &MBB : MF) {
    if (MBB.empty() || !FarFitPatchMBBs.contains(&MBB))
      continue;

    // Already the in-block long form (LUI+ADDI+[near-cond+]JALR with %bb
    // dest). Do not re-promote: removeBranch stops at JALR and a second
    // LUI+ADDI+JALR would stack forever (matmult-int hang). Skip only
    // that CFG form — never a whole-MBB last-JALR or returning JALR_CALL
    // (opcode-only skip left a far short uncond for LateConvergence
    // insertIndirectBranch). A far cond on this form falls through to
    // Arm A (invert+retarget if the other dest is near, or both-far
    // overlay). Skip does not named-fatal a retained near cond.
    {
      MachineBasicBlock::iterator LastJ = MBB.getLastNonDebugInstr();
      if (LastJ != MBB.end() && isAlreadyLongCfgJalr(*LastJ, TII)) {
        if (MachineInstr *CondMI = findAnalyzedCondMI(MBB, TII)) {
          if (condDisplacementInRange(MBB, TII, BlockStarts, *CondMI))
            continue;
        } else {
          continue;
        }
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
        return shortBranchDestOf(TII, MI);
      };
      MachineBasicBlock *FarDest = nullptr;
      // Walk the TAIL as bundle roots (MBB::iterator yields roots; a
      // BUNDLE root's own descriptor has no isTerminator/isBranch flag,
      // so terminators() skips committed branch cycles — analyzeBranch's
      // unwrap law instead). Stop at the first non-branch tail instr.
      for (MachineBasicBlock::iterator TI = MBB.getLastNonDebugInstr();
           TI != MBB.end(); --TI) {
        MachineBasicBlock *Dest = destOf(*TI);
        // Only analyzed TBB/FBB may become FarDest. A layout successor
        // that is not the cond dest or the trailing uncond dest is not
        // a promotion edge (HexagonBranchRelaxation.cpp:164-180).
        if (Dest && (Dest == TBB || Dest == FBB) && Dest->getNumber() >= 0 &&
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
          // committed packets). Prefer a short sibling over JALR_CALL.
          // D1.142: explicit site MI unwrap (BUNDLE child), not handshake.
          if (!TII.isBranchOffsetInRange(*TI, BrOff)) {
            FarDest = Dest;
            break;
          }
        }
        // Returning JALR_CALL is a mid-block call, not the branch tail.
        if (isReturningJalrCallIn(*TI)) {
          if (TI == MBB.begin())
            break;
          continue;
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

        // Scanned FarDest is the analyzed edge only (TBB from the cond,
        // FBB from the trailing uncond; AIEBaseInstrInfo.cpp:209-222 /
        // RISCVInstrInfo.cpp:1349-1355). A non-analyzed layout successor
        // is not a promotion dest.
        if (FarDest != TBB && FarDest != FBB) {
          if (!PostCommit)
            continue; // pre-stamp: leave for BR trampoline
          report_fatal_error(
              DEBUG_TYPE + Twine(": scanned far dest is not analyzed TBB or "
                                 "FBB at ") +
                  MF.getName() + " bb." + Twine(MBB.getNumber()),
              /*GenCrashDiag=*/false);
        }

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
          // D1.50: name the selected member through the ordered-terminator
          // law BEFORE any mutation (violation = pre-stamp defer via
          // nullptr / post-stamp named fatal).
          MachineInstr *SelUncond =
              selectedTrailingUncondThroughLaw(MBB, TII, TBB, FBB, PostCommit);
          const Register Scratch =
              pickInBlockScratch(MBB, TRI, FPL, FarDest);
          if (!Scratch) {
            if (!PostCommit)
              continue; // pre-stamp: leave for BR trampoline (no RestoreBB)
            report_fatal_error(
                DEBUG_TYPE +
                    Twine(": no dead-on-edge GPR for the in-block long form (") +
                    MF.getName() + " bb." + Twine(MBB.getNumber()) +
                    "); postcommit far site cannot be promoted without CFG "
                    "creation",
                /*GenCrashDiag=*/false);
          }
          if (!SelUncond) {
            if (!PostCommit)
              continue; // pre-stamp: leave for BR trampoline (no RestoreBB)
            report_fatal_error(
                DEBUG_TYPE + Twine(": ZOL latch has no trailing uncond to "
                                   "promote at ") +
                    MF.getName() + " bb." + Twine(MBB.getNumber()),
                /*GenCrashDiag=*/false);
          }
          stampLongTemplate(*Sites.requireMember(SelUncond, DEBUG_TYPE),
                            /*InvertNear=*/false);
          eraseSelectedTrailingUncond(MBB, TII, *SelUncond);
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

        // D1.50: name the selected far-uncond member through the
        // ordered-terminator law BEFORE any mutation of this site — the
        // ORIGINAL analyzed relationships (cond owns TBB, uncond owns
        // FBB/TBB) must be proven on the pre-surgery tail. Invert retargets
        // the cond member in place; the uncond pointer stays valid.
        MachineInstr *SelUncond =
            selectedTrailingUncondThroughLaw(MBB, TII, TBB, FBB, PostCommit);
        if (isLongTemplateMember(Sites, SelUncond)) {
          LLVM_DEBUG(dbgs() << DEBUG_TYPE
                            << ": skip re-promote Rank>=LongTemplate\n");
          continue;
        }

        // Invert the original cond to the analyzed near dest so JALR
        // takes the far edge. Never reverse PseudoLoopEnd (handled
        // above). Generic BranchRelaxation.cpp:477-490 invert-swap uses
        // FBB when present; layout-next only if FBB is null (520-530).
        SmallVector<MachineOperand, 4> NearCond;
        MachineBasicBlock *NearDest = nullptr;
        auto isSucc = [&](MachineBasicBlock *D) {
          return D && llvm::is_contained(MBB.successors(), D);
        };
        // D1.140: measure the retained near-cond from the cond's own
        // cycle, matching the D1.34 far-scan / condDisplacementInRange.
        // Last is the trailing uncond, one parcel later; post-rewrite the
        // cond stays at its original cycle, so a Last-anchored forward
        // measure underestimates by one parcel and silently leaned on
        // BranchRelaxSafetyBufferBytes. Arm B still uses Last because
        // that is the trailing uncond's own cycle.
        MachineBasicBlock::iterator CondSite = MBB.end();
        if (MachineInstr *CondMI = findAnalyzedCondMI(MBB, TII)) {
          if (MachineInstr *Root = haydn::bundle::bundleRootOf(*CondMI))
            CondSite = Root->getIterator();
          else
            CondSite = CondMI->getIterator();
        }
        auto destInRange = [&](MachineBasicBlock *D) {
          if (!D || D->getNumber() < 0 ||
              D->getNumber() >= (int)BlockStarts.size() ||
              BlockStarts[D->getNumber()] < 0 || CondSite == MBB.end())
            return false;
          const int64_t Off =
              BlockStarts[D->getNumber()] -
              (BlockStarts[MBB.getNumber()] +
               estimateLayoutInstrOffset(MBB, CondSite, TII));
          return TII.isBranchOffsetInRange(*CondSite, Off);
        };
        bool InvertCond = false;
        if (!Cond.empty()) {
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
            InvertCond = true;
            // Far TBB: invert to analyzed FBB only when FBB is a CFG
            // successor and in-range. Layout-next only when FBB is null
            // and the layout successor is a proven in-range one-way
            // fallthrough (isLayoutSuccessor && successor && != TBB &&
            // in-range). Never pick layout-next while FBB is non-null,
            // even if FBB is also far.
            if (FBB && isSucc(FBB) && destInRange(FBB)) {
              NearDest = FBB;
            } else if (!FBB) {
              MachineFunction::iterator NextIt =
                  std::next(MachineFunction::iterator(MBB));
              MachineBasicBlock *LayoutNext =
                  NextIt != MF.end() ? &*NextIt : nullptr;
              if (LayoutNext && MBB.isLayoutSuccessor(LayoutNext) &&
                  isSucc(LayoutNext) && LayoutNext != TBB &&
                  destInRange(LayoutNext))
                NearDest = LayoutNext;
            }
          } else if (FarDest == FBB && TBB && isSucc(TBB) &&
                     destInRange(TBB)) {
            // Far FBB: keep original cond to in-range analyzed TBB.
            NearDest = TBB;
          }
        }

        MachineInstr *TrailJalr =
            Last != MBB.end() ? trailingCfgJalr(*Last) : nullptr;
        MachineBasicBlock *JalrDest =
            TrailJalr ? TII.getBranchDestBlock(*TrailJalr) : nullptr;
        const bool HaveLongUncond =
            JalrDest && (JalrDest == FBB || (!FBB && JalrDest == TBB));

        if (!NearDest && !HaveLongUncond && !Cond.empty()) {
          if (!PostCommit)
            continue; // pre-stamp: leave for BR trampoline
          report_fatal_error(
              DEBUG_TYPE + Twine(": no proven near dest at ") +
                  MF.getName() + " bb." + Twine(MBB.getNumber()) +
                  " (postcommit far cond site)",
              /*GenCrashDiag=*/false);
        }

        // Dest-recovered trailing JALR is the already-long uncond (not a
        // short B). Invert to a near dest when one exists; both-far
        // selects TBB vs FBB into the JALR rs. Missing short uncond
        // without that JALR is skip/refuse, not a named fatal.
        if (!SelUncond && HaveLongUncond && !Cond.empty()) {
          MachineInstr *BothFarCond = findAnalyzedCondMI(MBB, TII);
          haydn::LayoutSite *BothFarSite =
              BothFarCond ? Sites.findByMember(BothFarCond) : nullptr;
          if (NearDest) {
            // D1.124: refuse incomplete/incoherent pair before invert so a
            // skipped retarget cannot leave a mutated cond.
            if (FarDest && FarDest != JalrDest) {
              HaydnJalrAddrMaterializeChain Chain =
                  TII.getJalrAddrMaterializeChain(*TrailJalr);
              if (!Chain.Complete || Chain.Incoherent) {
                LLVM_DEBUG(dbgs()
                           << DEBUG_TYPE << ": " << MF.getName() << " bb."
                           << MBB.getNumber()
                           << " skip: incomplete/incoherent LUI+ADDI "
                              "retarget\n");
                continue;
              }
            }
            if (InvertCond) {
              MachineInstr *CondMI = findAnalyzedCondMI(MBB, TII);
              if (!CondMI ||
                  !retargetAnalyzedCond(*CondMI, NearCond[0].getImm(),
                                        NearDest, TII))
                report_fatal_error(
                    DEBUG_TYPE +
                        Twine(": failed to invert/retarget cond at ") +
                        MF.getName() + " bb." + Twine(MBB.getNumber()),
                    /*GenCrashDiag=*/false);
              Sites.requireMember(CondMI, DEBUG_TYPE);
            }
            if (FarDest && FarDest != JalrDest &&
                !retargetJalrAddrDest(*TrailJalr, FarDest, TII)) {
              LLVM_DEBUG(dbgs()
                         << DEBUG_TYPE << ": " << MF.getName() << " bb."
                         << MBB.getNumber()
                         << " skip: incomplete/incoherent LUI+ADDI "
                            "retarget\n");
              continue;
            }
          } else if (!rewriteBothFarCondOntoJalr(MBB, TII, FPL, Cond, TBB, FBB,
                                                 *TrailJalr, DL)) {
            if (!PostCommit)
              continue;
            report_fatal_error(
                DEBUG_TYPE +
                    Twine(": cannot rewrite both-far cond onto JALR at ") +
                    MF.getName() + " bb." + Twine(MBB.getNumber()),
                /*GenCrashDiag=*/false);
          } else if (PostCommit && BothFarSite) {
            // D1.168: vacated coissued cond stays the same MI as a
            // same-row NOP (eraseSelectedBranch does not delete it).
            BothFarSite->raiseRank(haydn::LayoutSiteRank::NeutralizedNop);
          }
          stampLongTemplate(*Sites.requireMember(TrailJalr, DEBUG_TYPE),
                            InvertCond);
          LLVM_DEBUG(dbgs()
                     << DEBUG_TYPE << ": " << MF.getName() << " bb."
                     << MBB.getNumber()
                     << " rewrote far cond onto existing JALR\n");
          Changed = true;
          IterChanged = true;
          continue;
        }
        if (!SelUncond && (FBB || Cond.empty())) {
          LLVM_DEBUG(dbgs()
                     << DEBUG_TYPE << ": " << MF.getName() << " bb."
                     << MBB.getNumber()
                     << " skip: selected trailing uncond missing\n");
        } else {
          // Refusal must precede invert/erase (d149 THROUGH / gr27
          // NOSTAMP): pick on the original two-way tail. Invert-then-refuse
          // left BNEZ_W one-way while CFG still named the far FBB.
          const Register Scratch = pickInBlockScratch(MBB, TRI, FPL);
          if (!Scratch) {
            if (!PostCommit)
              continue;
            report_fatal_error(
                DEBUG_TYPE +
                    Twine(": no dead-on-edge GPR for the in-block long form (") +
                    MF.getName() + " bb." + Twine(MBB.getNumber()) +
                    "); postcommit far site cannot be promoted without CFG "
                    "creation",
                /*GenCrashDiag=*/false);
          }
          // D1.135: prove disjointness before invert/erase. Splice after a
          // WAR Keep would feed the retained cond the new value; skipping
          // splice leaves a non-terminator after the first terminator.
          if (SelUncond &&
              refuseUncondSurvivorWarSplice(MBB, *SelUncond, TRI, PostCommit))
            continue;
          if (InvertCond) {
            MachineInstr *CondMI = findAnalyzedCondMI(MBB, TII);
            if (!CondMI || !retargetAnalyzedCond(*CondMI, NearCond[0].getImm(),
                                                 NearDest, TII))
              report_fatal_error(
                  DEBUG_TYPE + Twine(": failed to invert/retarget cond at ") +
                      MF.getName() + " bb." + Twine(MBB.getNumber()),
                  /*GenCrashDiag=*/false);
            Sites.requireMember(CondMI, DEBUG_TYPE);
          }

          // Erase only the far uncond member. One-way (no uncond): append
          // JALR below. Do not call removeBranch — that walks preceding
          // controls (AIE/Hexagon/RISCV erase at most two trailing terms).
          // Failed erase of a selected uncond is fatal inside the helper.
          // Invert retargets the cond in place; re-name the trailing uncond
          // against the post-invert analyzed pair (NearDest as TBB).
          MachineInstr *EraseMI = SelUncond;
          if (InvertCond)
            // Post-surgery analyzed pair: the RETARGETED cond owns NearDest
            // (the new TBB) and the trailing B still targets the ORIGINAL
            // FBB — the near dest, never FarDest (the surgery retargets the
            // cond only; the uncond keeps its dest until erased here). With
            // FBB null (one-way + layout-next near dest) there is no
            // trailing uncond to re-name at all.
            EraseMI = selectedTrailingUncondThroughLaw(
                MBB, TII, /*TBB=*/NearDest, /*FBB=*/FBB, PostCommit);
          if (EraseMI) {
            stampLongTemplate(*Sites.requireMember(EraseMI, DEBUG_TYPE),
                              InvertCond);
            eraseSelectedTrailingUncond(MBB, TII, *EraseMI);
          } else if (FBB || Cond.empty()) {
            LLVM_DEBUG(dbgs()
                       << DEBUG_TYPE << ": " << MF.getName() << " bb."
                       << MBB.getNumber()
                       << " skip: selected trailing uncond missing\n");
            continue;
          } else if (MachineInstr *CondMI = findAnalyzedCondMI(MBB, TII)) {
            Sites.requireMember(CondMI, DEBUG_TYPE);
          } else {
            report_fatal_error(
                DEBUG_TYPE + Twine(": missing layout site (Arm A one-way) at ") +
                    MF.getName() + " bb." + Twine(MBB.getNumber()),
                /*GenCrashDiag=*/false);
          }
          spliceTrailingNonTermsBeforeFirstControl(MBB);
          emitInBlockLongJump(MBB, firstControlMI(MBB), DL, TII, Scratch,
                              FarDest, /*SplitJalrToEnd=*/true);

          LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": " << MF.getName() << " bb."
                            << MBB.getNumber() << " promoted far site -> bb."
                            << FarDest->getNumber()
                            << " (in-block LUI+ADDI+cond+JALR)\n");
          Changed = true;
          IterChanged = true;
          continue; // tail rewritten; next MBB
        }
      }
    }

    // --- Arm B: unanalyzable tail whose LAST terminator is an
    // out-of-range unconditional B. Replace exactly that terminator.
    // Do NOT match a trailing conditional via isBranch: that would
    // drop the condition and JALR unconditionally (MEMORY_FAULT class).
    MachineBasicBlock::iterator Last = MBB.getLastNonDebugInstr();
    if (Last == MBB.end())
      continue;
    // Prefer the short uncond sibling over a coissued JALR_CALL / baked
    // catalog JALR (unwrapBundleControlFlow keeps the first call/barrier
    // child and can hide B). Do not require Last->isUnconditionalBranch:
    // JALR_CALL isCall poisons AnyInBundle isUnconditionalBranch.
    MachineBasicBlock *Dest = shortUncondDestOf(TII, *Last);
    if (!Dest || Dest->getNumber() < 0 ||
        Dest->getNumber() >= (int)BlockStarts.size())
      continue;
    const int64_t DestStart = BlockStarts[Dest->getNumber()];
    if (DestStart < 0)
      continue;
    const int64_t BrOff =
        DestStart - (BlockStarts[MBB.getNumber()] +
                     estimateLayoutInstrOffset(MBB, Last, TII));
    // D1.142: explicit site MI unwrap, not a BUNDLE-root opcode handshake
    // (BUNDLE is always-in-range at the opcode-only API so a coissued
    // trailing uncond would be left short).
    if (TII.isBranchOffsetInRange(*Last, BrOff))
      continue;

    const bool PostCommitB =
        FuncInfo && FuncInfo->hasPostCommitBlockBudget();
    // D1.49: ALL CFG successors are obligations. Arm B keeps every
    // successor edge (only the selected far uncond is replaced; earlier
    // retained conditions keep their edges), and the unanalyzable tail
    // can hold retained controls whose destinations are not Dest — the
    // retired DeadOn=Dest narrowing could prove "dead on Dest" while a
    // live-through value on another successor was clobbered.
    const Register Scratch = pickInBlockScratch(MBB, TRI, FPL);
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

    // Replace exactly the selected trailing uncond root/member. Do not
    // call removeBranch: that walks preceding analyzable conds
    // (AIEBaseInstrInfo.cpp:237-266 / HexagonInstrInfo.cpp:605-625 /
    // RISCVInstrInfo.cpp:1361-1390 stop at the first non-branch, but
    // Haydn's walk still erases every trailing branch it classifies).
    // Last is an uncond: a failed erase is fatal so short B and JALR
    // cannot both remain (one-way never reaches Arm B). D1.50: the
    // selection is named by the ordered-terminator law; an unanalyzable
    // tail has no analyzed TBB/FBB, so the law enforces order/uniqueness
    // (trailing cycle, one uncond child, no second uncond cycle). A
    // violation is pre-stamp skip/refuse (do not emit JALR beside leftover
    // B) and post-stamp a named fatal.
    MachineInstr *SelUncondB = selectedTrailingUncondThroughLaw(
        MBB, TII, /*TBB=*/nullptr, /*FBB=*/nullptr, PostCommitB);
    if (!SelUncondB) {
      if (!PostCommitB)
        continue;
      report_fatal_error(
          DEBUG_TYPE + Twine(": Arm B selected trailing uncond missing at ") +
              MF.getName() + " bb." + Twine(MBB.getNumber()),
          /*GenCrashDiag=*/false);
    }
    if (refuseUncondSurvivorWarSplice(MBB, *SelUncondB, TRI, PostCommitB))
      continue;
    if (isLongTemplateMember(Sites, SelUncondB)) {
      LLVM_DEBUG(dbgs() << DEBUG_TYPE
                        << ": skip re-promote Rank>=LongTemplate\n");
      continue;
    }
    stampLongTemplate(*Sites.requireMember(SelUncondB, DEBUG_TYPE),
                      /*InvertNear=*/false);
    eraseSelectedTrailingUncond(MBB, TII, *SelUncondB);
    spliceTrailingNonTermsBeforeFirstControl(MBB);
    emitInBlockLongJump(MBB, firstControlMI(MBB), DL, TII, Scratch, Dest,
                        /*SplitJalrToEnd=*/true);
    LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": " << MF.getName() << " bb."
                      << MBB.getNumber() << " promoted far uncond -> bb."
                      << Dest->getNumber() << " (in-block LUI+ADDI+JALR)\n");
    Changed = true;
    IterChanged = true;
  }

    if (!IterChanged)
      break;
  }

  // GR2.9: after closer waves, one EncodedBytes pad + MBB metadata clear
  // so AsmPrinter cannot emit post-freeze emitCodeAlignment. AIE seats
  // MachineAlignment in addPreSched2 only because AIE PreEmit is empty.
  if (FuncInfo && FuncInfo->hasPostCommitBlockBudget())
    Changed |= haydn::padInternalMBBAlignment(MF, TII, /*ClearMetadata=*/true);

  // D1.141: after Arm A/B converge, every remaining far short cond in an
  // unanalyzable / one-way cycle has no range owner (BR will not see a
  // committed BUNDLE cond without the trailing-JALR handshake). Pre-stamp
  // defers to BR splitBlockBeforeInstr. Post-stamp is an owner wall —
  // do not invent GR1.8 here (Wave 8).
  if (FuncInfo && FuncInfo->hasPostCommitBlockBudget()) {
    SmallVector<int64_t, 32> FinalStarts;
    computeLayoutBlockStarts(MF, TII, FinalStarts);
    for (MachineBasicBlock &MBB : MF) {
      if (hasFarShortCondWithoutRangeOwner(MBB, TII, FinalStarts))
        report_fatal_error(
            DEBUG_TYPE +
                Twine(": far cond in unanalyzable/one-way cycle has no range "
                      "owner at ") +
                MF.getName() + " bb." + Twine(MBB.getNumber()),
            /*GenCrashDiag=*/false);
    }
  }

  return Changed;
}

INITIALIZE_PASS(HaydnLongBranchNormalize, DEBUG_TYPE,
                "Haydn Long-Branch Normalize", false, false)

FunctionPass *llvm::createHaydnLongBranchNormalizePass() {
  return new HaydnLongBranchNormalize();
}
