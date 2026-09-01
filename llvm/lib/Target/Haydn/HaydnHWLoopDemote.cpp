//===-- HaydnHWLoopDemote.cpp - Shared HWLOOP demote/erase helpers ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definitions for the MI-level helpers shared by hardware-loop setup erase
// and software-loop demotion. Declared in HaydnHWLoopDemote.h; the exported
// entry points (llvm::eraseHardwareLoopSetup /
// llvm::demoteHardwareLoopToSoftware) live in HaydnHardwareLoops.cpp and
// call through to these with a per-pass LLVM_DEBUG prefix.
//
//===----------------------------------------------------------------------===//

#include "HaydnHWLoopDemote.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnInstrInfo.h"
#include "HaydnPortModel.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace llvm;
using namespace llvm::haydn::hwloop;

#define DEBUG_TYPE "haydn-hwloop-demote"

// Product default ON: demote-first, not erase-only. Debug OFF skips the
// soft-edge install on a live body (caller fatals). Hexagon FixupHwLoops
// skips conversion when the loop cannot stay legal
// (HexagonFixupHwLoops.cpp:97-148).
static cl::opt<bool> EnableHaydnHwLoopDemote(
    "haydn-enable-hwloop-demote", cl::Hidden, cl::init(true),
    cl::desc("Demote out-of-range / invalid ZOL to software SUBI32+BNEZ_W "
             "when a free counter GPR exists (LivePhysRegs). Default ON "
             "(product demote-first). OFF = do not demote; live body is "
             "fatal rather than erase-only once-through."));

bool haydn::hwloop::isHwLoopDemoteEnabled() {
  return EnableHaydnHwLoopDemote;
}

bool haydn::hwloop::blockContainsUnpublishedHwlrCsr(
    const MachineBasicBlock &BB) {
  // instrs() includes BUNDLE children. Top-level range-for would miss a
  // packed CSRW after post-RA (P6 Fixup). Formation (P3) is typically
  // unbundled; the same walk stays correct. Peer: AIE expand
  // (AIEBaseHardwareLoops.cpp:348-370) walks every MI in the loop
  // blocks; Haydn overlay is this unpublished-window predicate only.
  for (const MachineInstr &MI : BB.instrs()) {
    if (MI.isBundle() || MI.isMetaInstruction())
      continue;
    if (haydnHwloopCsrAddr(MI) >= 0)
      return true;
  }
  return false;
}

bool haydn::hwloop::loopBlocksContainUnpublishedHwlrCsr(
    const LoopBlockSet &Blocks) {
  for (const MachineBasicBlock *BB : Blocks) {
    if (BB && blockContainsUnpublishedHwlrCsr(*BB))
      return true;
  }
  return false;
}

// Resolve the LoopStart/SET body from CFG state only. Any SET form
// (logical/wide/member) carries Header as op1. LoopStart: PLE-carrying
// preheader successor (single-BB) or unique successor with a latch PLE
// targeting it (multi-BB). Formation uses this directly — layout-order
// resolution is incomplete retained state and must reject fail-closed.
static MachineInstr *findPseudoLoopEnd(MachineBasicBlock *BB) {
  if (!BB)
    return nullptr;
  for (MachineInstr &MI : *BB)
    if (MI.getOpcode() == Haydn::PseudoLoopEnd)
      return &MI;
  return nullptr;
}

MachineBasicBlock *haydn::hwloop::resolveBodyMBBCore(MachineInstr &SetMI) {
  const MachineFunction *MF =
      SetMI.getParent() ? SetMI.getParent()->getParent() : nullptr;
  if (!MF)
    return nullptr;

  unsigned Opc = SetMI.getOpcode();
  if (Opc != Haydn::LoopStart &&
      SetMI.getNumOperands() >= 3 && SetMI.getOperand(1).isMBB()) {
    // Any SET form (logical/wide/member) carries Header as op1.
    MachineBasicBlock *H = SetMI.getOperand(1).getMBB();
    return isLiveMBB(*MF, H) ? H : nullptr;
  }

  // LoopStart: prefer a preheader successor that carries PseudoLoopEnd
  // (single-BB Header==Latch). Else the unique successor whose latch PLE
  // targets it (multi-BB Header!=Latch). Never invent a body from layout
  // order when several successors exist and none prove a latch.
  if (Opc == Haydn::LoopStart) {
    MachineBasicBlock *Pre = SetMI.getParent();
    MachineBasicBlock *Single = nullptr;
    MachineBasicBlock *FoundHeader = nullptr;
    for (MachineBasicBlock *Succ : Pre->successors()) {
      if (!isLiveMBB(*MF, Succ))
        continue;
      if (findPseudoLoopEnd(Succ)) {
        if (Single)
          return nullptr;
        Single = Succ;
      }
      if (resolveLoopStartLatch(Succ, Pre)) {
        if (FoundHeader && FoundHeader != Succ)
          return nullptr;
        FoundHeader = Succ;
      }
    }
    if (Single)
      return Single;
    if (FoundHeader)
      return FoundHeader;
    // No unique-successor-without-proof fallback: a lone preheader
    // successor may be the exit (layout-only body is incomplete).
  }
  return nullptr;
}

MachineBasicBlock *haydn::hwloop::resolveBodyMBBFixup(MachineInstr &SetMI) {
  if (MachineBasicBlock *B = resolveBodyMBBCore(SetMI))
    return B;
  if (SetMI.getOpcode() != Haydn::LoopStart)
    return nullptr;
  MachineBasicBlock *Pre = SetMI.getParent();
  if (!Pre)
    return nullptr;
  const MachineFunction *MF = Pre->getParent();
  MachineBasicBlock *Cand = Pre->getNextNode();
  for (unsigned Depth = 0; isLiveMBB(*MF, Cand) && Depth < 8;
       Cand = Cand->getNextNode(), ++Depth) {
    if (resolveLoopStartLatch(Cand, Pre))
      return Cand;
    if (!isContinueTrampolineBlock(Cand) || Cand->succ_size() != 1)
      return nullptr;
  }
  return nullptr;
}

static bool isPLETargetingHeader(const MachineInstr *MI,
                                 const MachineBasicBlock *Header) {
  if (!MI || !Header || MI->getOpcode() != Haydn::PseudoLoopEnd)
    return false;
  return MI->getNumOperands() > 0 && MI->getOperand(0).isMBB() &&
         MI->getOperand(0).getMBB() == Header;
}

MachineBasicBlock *
haydn::hwloop::resolveLoopStartLatch(MachineBasicBlock *Header,
                                     MachineBasicBlock *Preheader) {
  if (!Header)
    return nullptr;
  if (isPLETargetingHeader(findPseudoLoopEnd(Header), Header))
    return Header;
  MachineBasicBlock *Latch = nullptr;
  for (MachineBasicBlock *Pred : Header->predecessors()) {
    if (Pred == Preheader)
      continue;
    if (isPLETargetingHeader(findPseudoLoopEnd(Pred), Header)) {
      if (Latch)
        return nullptr;
      Latch = Pred;
    }
  }
  return Latch;
}

bool haydn::hwloop::isSoftLatchBnezOpcode(unsigned Opc) {
  const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(Opc);
  return Log == Haydn::BNEZ_W || Log == Haydn::BNEZ;
}

bool haydn::hwloop::isContinueTrampolineBlock(const MachineBasicBlock *BB) {
  if (!BB)
    return false;
  // Hexagon FixupHwLoops.cpp:97-148 converts or leaves LOOP; it does not
  // invent a body. Haydn overlay: BR may splice an empty / LUI+ADDI+JALR
  // continue trampoline between preheader and body. Those blocks are not
  // a ZOL header — callers walk past them, then require latch proof.
  for (const MachineInstr &MI : BB->instrs()) {
    if (MI.isMetaInstruction() || MI.isCFIInstruction() || MI.isKill() ||
        MI.isImplicitDef() || MI.isBundle() || MI.isDebugInstr() ||
        MI.isPosition())
      continue;
    const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
    if (Log == Haydn::NOP || Log == Haydn::LUI || Log == Haydn::ADDI32_W ||
        Log == Haydn::JALR_W || Log == Haydn::JALR || Log == Haydn::B)
      continue;
    return false;
  }
  return true;
}

bool haydn::hwloop::isLiveMBB(const MachineFunction &MF,
                              const MachineBasicBlock *MBB) {
  return MBB && MBB->getParent() == &MF && MBB->getNumber() >= 0;
}

void haydn::hwloop::eraseEmptyBundleRoot(MachineInstr *BundleRoot) {
  if (!BundleRoot || !BundleRoot->isBundle() || !BundleRoot->getParent())
    return;
  MachineBasicBlock::instr_iterator Next =
      std::next(BundleRoot->getIterator());
  MachineBasicBlock *MBB = BundleRoot->getParent();
  if (Next != MBB->instr_end() && Next->isBundledWithPred())
    return; // still has children
  BundleRoot->eraseFromParent();
}

void haydn::hwloop::eraseInstrSafe(MachineInstr *MI) {
  if (!MI || !MI->getParent())
    return;
  MachineInstr *BundleRoot = nullptr;
  if (MI->isBundledWithPred()) {
    BundleRoot = &*getBundleStart(MI->getIterator());
    MI->unbundleFromPred();
  }
  if (MI->isBundledWithSucc())
    MI->unbundleFromSucc();
  MI->eraseFromParent();
  eraseEmptyBundleRoot(BundleRoot);
}

MachineInstr &haydn::hwloop::topLevelForLayout(MachineInstr &MI) {
  if (MI.isBundledWithPred() || MI.isBundledWithSucc())
    return *getBundleStart(MI.getIterator());
  return MI;
}

unsigned haydn::hwloop::lateMemberOpcode(unsigned LogicalOpc) {
  return haydn::bundle::lateProductMemberOpcode(LogicalOpc);
}

void haydn::hwloop::finalizeExactLateSingleton(MachineInstr &MI) {
  haydn::bundle::finalizeExactLateSingleton(MI);
}

void haydn::hwloop::recommitSurvivingCycleMembers(
    ArrayRef<MachineInstr *> Keep, const HaydnInstrInfo &TII,
    const char *DebugPrefix) {
  if (Keep.empty())
    return;

  for (MachineInstr *K : Keep) {
    if (!K || !K->getParent())
      continue;
    if (K->isBundledWithPred())
      K->unbundleFromPred();
    if (K->isBundledWithSucc())
      K->unbundleFromSucc();
  }

  SmallVector<MachineInstr *, 3> Live;
  Live.reserve(Keep.size());
  for (MachineInstr *K : Keep)
    if (K && K->getParent())
      Live.push_back(K);
  if (Live.empty())
    return;

  if (Live.size() == 1) {
    MachineInstr *MI = Live[0];
    unsigned Member = lateMemberOpcode(MI->getOpcode());
    if (Member != MI->getOpcode())
      MI->setDesc(TII.get(Member));
    finalizeExactLateSingleton(*MI);
    return;
  }

  // Multi-survivor coissue: transactional multi-MI exact commit rebuilds
  // root operands/kills/internal-reads + Format E row/completion. Fall back to
  // per-member singletons rather than leave bare reals if membership is
  // no longer one legal product cycle after SET removal.
  if (!haydn::bundle::commitExactMultiMIProductCycle(Live)) {
    LLVM_DEBUG(dbgs() << DebugPrefix << ": coissue survivors not one "
                         "legal multi-MI cycle after SET erase — "
                         "singleton exact-commit each\n");
    for (MachineInstr *MI : Live) {
      unsigned Member = lateMemberOpcode(MI->getOpcode());
      if (Member != MI->getOpcode())
        MI->setDesc(TII.get(Member));
      finalizeExactLateSingleton(*MI);
    }
  }
}

void haydn::hwloop::eraseSetMemberAndRecommitSiblings(
    MachineInstr &SetMI, const HaydnInstrInfo &TII, const char *DebugPrefix) {
  MachineBasicBlock *MBB = SetMI.getParent();
  if (!MBB)
    return;

  SmallVector<MachineInstr *, 3> Keep;
  if (SetMI.isBundledWithPred() || SetMI.isBundledWithSucc()) {
    MachineInstr *Root = &*getBundleStart(SetMI.getIterator());
    for (MachineInstr *K : haydn::bundle::members(*Root)) {
      if (K != &SetMI)
        Keep.push_back(K);
    }
    // Dissolve every child from the old root before erasing the header so no
    // pass observes a half-unbundled multi-member shell.
    for (MachineInstr *K : Keep) {
      if (K->isBundledWithPred())
        K->unbundleFromPred();
      if (K->isBundledWithSucc())
        K->unbundleFromSucc();
    }
    if (SetMI.isBundledWithPred())
      SetMI.unbundleFromPred();
    if (SetMI.isBundledWithSucc())
      SetMI.unbundleFromSucc();
    Root->eraseFromParent();
  }

  SetMI.eraseFromParent();
  recommitSurvivingCycleMembers(Keep, TII, DebugPrefix);
}

MachineInstrBuilder haydn::hwloop::buildExactLateDef(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
    const DebugLoc &DL, const TargetInstrInfo &TII, unsigned LogicalOpc,
    Register Dest) {
  return BuildMI(MBB, InsertPt, DL, TII.get(lateMemberOpcode(LogicalOpc)),
                 Dest);
}

MachineInstrBuilder haydn::hwloop::buildExactLate(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
    const DebugLoc &DL, const TargetInstrInfo &TII, unsigned LogicalOpc) {
  return BuildMI(MBB, InsertPt, DL, TII.get(lateMemberOpcode(LogicalOpc)));
}

void haydn::hwloop::collectLoopBlocks(const MachineBasicBlock *Header,
                                      const MachineBasicBlock *Latch,
                                      const MachineBasicBlock *Preheader,
                                      LoopBlockSet &Out) {
  Out.clear();
  if (!Header || !Latch)
    return;
  Out.insert(Header);
  Out.insert(Latch);
  if (Header == Latch)
    return;

  // Reverse CFG from Latch: every predecessor that is not Preheader and not
  // outside the loop. Cap work to avoid runaway on malformed CFG.
  SmallVector<const MachineBasicBlock *, 8> Work(Latch->pred_begin(),
                                                 Latch->pred_end());
  unsigned Guard = 0;
  while (!Work.empty() && Guard++ < 256) {
    const MachineBasicBlock *B = Work.pop_back_val();
    if (!B || B == Preheader)
      continue;
    if (!Out.insert(B).second)
      continue;
    if (B == Header)
      continue;
    for (const MachineBasicBlock *P : B->predecessors())
      Work.push_back(P);
  }
}

bool haydn::hwloop::isCountdownStepOf(const MachineInstr &MI, Register Reg) {
  if (!Reg.isPhysical() || MI.isMetaInstruction() || MI.isDebugInstr() ||
      MI.isBundle())
    return false;
  // Must def Reg.
  bool Defs = false;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isDef() && MO.getReg() == Reg) {
      Defs = true;
      break;
    }
  }
  if (!Defs)
    return false;

  // Closed predicate: a countdown step is
  // LoopDec, or a same-reg immediate step whose GENERATED LOGICAL opcode is
  // an add/sub with proven ±1 immediate. Mapping through
  // logicalOpcodeOrSelf admits the Format E member forms of exactly those
  // ops and nothing else: any other (Reg, Reg, ±1)-shaped instruction
  // (e.g. SLLI32 Reg, Reg, 1) is real compute, and same-reg ADD32/SUB32
  // Prefer, Prefer, <reg> is an unproven step — post-RA may reuse the dead
  // trip physreg as a pointer bump; classifying either as a countdown lets
  // stripResidualCountdown erase live compute (silent wrong trip under
  // hwloops-ON + demote). Unproven steps are clobbers: demote picks a free
  // counter and materializes. Do not walk a reaching-def chain.
  if (MI.getOpcode() == Haydn::LoopDec)
    return true;

  // Register-rhs ADD32/SUB32 is an unverifiable addend even if a later
  // encoding shape grows an immediate-looking operand. Treat as clobber.
  const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
  if (Log == Haydn::ADD32 || Log == Haydn::SUB32)
    return false;

  auto logicalStepIsProvenCountdown = [&](int64_t Imm) -> bool {
    if (Log == Haydn::ADDI32 || Log == Haydn::ADDI32_W)
      return Imm == -1;
    if (Log == Haydn::SUBI32)
      return Imm == 1;
    return false;
  };
  if (MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
      MI.getOperand(0).getReg() == Reg && MI.getOperand(1).isReg() &&
      MI.getOperand(1).getReg() == Reg && MI.getOperand(2).isImm())
    return logicalStepIsProvenCountdown(MI.getOperand(2).getImm());
  return false;
}

bool haydn::hwloop::regMentionedInBlocks(Register Reg,
                                         const LoopBlockSet &Blocks) {
  if (!Reg.isPhysical())
    return false;
  for (const MachineBasicBlock *MBB : Blocks) {
    if (!MBB)
      continue;
    for (const MachineInstr &MI : MBB->instrs()) {
      if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isBundle())
        continue;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.getReg() == Reg)
          return true;
      }
    }
  }
  return false;
}

bool haydn::hwloop::regClobberedNonCountdownIn(Register Reg,
                                               const LoopBlockSet &Blocks) {
  if (!Reg.isPhysical())
    return false;
  for (const MachineBasicBlock *MBB : Blocks) {
    if (!MBB)
      continue;
    for (const MachineInstr &MI : MBB->instrs()) {
      if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isBundle())
        continue;
      bool Defs = false;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.isDef() && MO.getReg() == Reg) {
          Defs = true;
          break;
        }
      }
      if (!Defs)
        continue;
      if (isCountdownStepOf(MI, Reg))
        continue;
      return true;
    }
  }
  return false;
}

// A demote counter's live range: the loop blocks (every one lies on a
// def->latch-BNEZ path — collectLoopBlocks is reverse-reachable from the
// latch) plus the preheader tail from the materialize point.
// Law — this function must OWN the register across that range:
//  * ABI callee-saved (R8-R11/R14/D8-D15): owned iff this function's
//    prologue actually saves it (CalleeSavedInfo; demote runs post-PEI at
//    both seats — formation addPreSched2 and fixup addPreEmit — so an
//    unsaved CSR write is never later repaired: silent breakage of OUR
//    caller's live value). Saved ⇒ also call-safe: callees restore it and
//    our epilogue restores the caller's value.
//  * caller-saved: freely owned by the callee, but any call on the range
//    clobbers it mid-loop (corrupted trip). A call's clobbers are regmask
//    operands — invisible to the explicit-def walk above (verifier:
//    "Using an undefined physical register"; runtime: wrong loop bound).
// No qualifying register ⇒ the stack-counter demote path (PostRAScratchFI
// home; latch scratch defined entirely after the last call) is the sink.
bool haydn::hwloop::isSoundDemoteCounter(
    MCPhysReg Reg, const LoopBlockSet &Blocks, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom, const MachineFunction &MF,
    const TargetRegisterInfo &TRI) {
  // Preheader tail after SET/LoopStart still executes with the software
  // counter live. Following work is legal ZOL setup distance (SET is not a
  // scheduling boundary), so a GPR scavenged as free at the SET site may
  // still be defined as address scratch before the header. Using it as the
  // countdown leaves the trip clobbered (va-arg-22 -O2: r3 = sp+off then
  // SUBI/BNEZ r3 → MEMORY_FAULT). Skip the setup MI and its bundle.
  if (Preheader && PreheaderFrom != Preheader->end()) {
    bool PastSetup = false;
    bool InSetupBundle = false;
    for (const MachineInstr &MI : Preheader->instrs()) {
      if (!PastSetup) {
        if (&MI == &*PreheaderFrom) {
          PastSetup = true;
          InSetupBundle = MI.isBundle() || MI.isBundledWithSucc();
        }
        continue;
      }
      if (InSetupBundle) {
        if (MI.isBundledWithPred())
          continue;
        InSetupBundle = false;
      }
      if (MI.isMetaInstruction() || MI.isDebugInstr() ||
          MI.isCFIInstruction() || MI.isImplicitDef() || MI.isKill() ||
          MI.isBundle())
        continue;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.getReg().isPhysical() &&
            TRI.regsOverlap(MO.getReg(), Reg))
          return false;
      }
    }
  }

  // ABI CSR membership: the save list is the inter-procedural contract.
  bool IsABICalleeSaved = false;
  for (const MCPhysReg *CSR = TRI.getCalleeSavedRegs(&MF); CSR && *CSR; ++CSR)
    if (*CSR == Reg)
      IsABICalleeSaved = true;

  if (IsABICalleeSaved) {
    for (const CalleeSavedInfo &CI : MF.getFrameInfo().getCalleeSavedInfo())
      if (CI.getReg() == Reg)
        return true;
    return false;
  }

  // Caller-saved: refuse when any call on the counter's live range fails to
  // preserve it. A call with no regmask at all is underdescribed: refuse.
  auto callOnRangeClobbers = [&]() -> bool {
    for (const MachineBasicBlock *MBB : Blocks) {
      if (!MBB)
        continue;
      for (const MachineInstr &MI : MBB->instrs()) {
        if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isBundle() ||
            !MI.isCall())
          continue;
        bool HasRegMask = false;
        bool Preserved = false;
        for (const MachineOperand &MO : MI.operands()) {
          if (!MO.isRegMask())
            continue;
          HasRegMask = true;
          if (!MO.clobbersPhysReg(Reg))
            Preserved = true;
        }
        if (!HasRegMask || !Preserved)
          return true;
      }
    }
    if (!Preheader)
      return false;
    for (auto It = PreheaderFrom; It != Preheader->end(); ++It) {
      const MachineInstr &MI = *It;
      if (MI.isMetaInstruction() || MI.isDebugInstr() || !MI.isCall())
        continue;
      bool HasRegMask = false;
      bool Preserved = false;
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isRegMask())
          continue;
        HasRegMask = true;
        if (!MO.clobbersPhysReg(Reg))
          Preserved = true;
      }
      if (!HasRegMask || !Preserved)
        return true;
    }
    return false;
  };
  return !callOnRangeClobbers();
}

void haydn::hwloop::stripResidualCountdown(const LoopBlockSet &Blocks,
                                           Register Reg) {
  if (!Reg.isPhysical())
    return;
  SmallVector<MachineInstr *, 4> Kill;
  for (const MachineBasicBlock *BB : Blocks) {
    if (!BB)
      continue;
    for (MachineInstr &MI : const_cast<MachineBasicBlock *>(BB)->instrs()) {
      if (isCountdownStepOf(MI, Reg) && MI.getOpcode() != Haydn::LoopDec)
        Kill.push_back(&MI);
    }
  }
  for (MachineInstr *MI : Kill)
    eraseInstrSafe(MI);
}

Register haydn::hwloop::pickCounterReg(
    const LoopBlockSet &Blocks, Register Prefer, const HaydnSubtarget &ST,
    MachineBasicBlock &Preheader, MachineBasicBlock::iterator InsertPt,
    const char *DebugPrefix) {
  // Free counter must be:
  // 1) not mentioned in any CFG loop block (lc_dp_lis: layout range missed
  //    latch earlier in the function — CFG Blocks is required);
  // 2) available at the SET insert point (LivePhysRegs — AIE/RISC-V style
  //    post-RA scavenge, not "first preferred even if live");
  // 3) owned by this function across the counter's live range (preheader
  //    tail defs/uses after SET, call regmask clobbers / unsaved-CSR):
  //    isSoundDemoteCounter, same law as Prefer;
  // 4) dead on every loop exit edge (CB-162): the software countdown
  //    destroys the register; a value live for later users (the bkfir16x16
  //    descriptor NBLK/M fields) must never be the countdown.
  // Fail-closed: return invalid Register rather than Prefer/R11 when both
  // are live (old code clobbered live-through temps under demote).
  const TargetRegisterInfo &TRI = *ST.getRegisterInfo();
  const MachineRegisterInfo &MRI = Preheader.getParent()->getRegInfo();
  const MachineFunction &MF = *Preheader.getParent();

  LivePhysRegs LPR(TRI);
  LPR.addLiveOuts(Preheader);
  for (MachineBasicBlock::iterator II = Preheader.end(); II != InsertPt;) {
    --II;
    LPR.stepBackward(*II);
  }

  // Live-in of a loop-exit successor, derived from its live-out stepped
  // backward over its instructions (MBB live-in lists alone are stale this
  // late; the backward walk is the same authority LivePhysRegs uses).
  auto liveInContains = [&](const MachineBasicBlock &S, MCPhysReg R) -> bool {
    LivePhysRegs SuccLPR(TRI);
    SuccLPR.addLiveOuts(S);
    for (const MachineInstr &MI : llvm::reverse(S))
      SuccLPR.stepBackward(MI);
    return SuccLPR.contains(R);
  };

  auto isUsable = [&](Register R) -> bool {
    if (!R.isPhysical() || R == Haydn::R0 || R == Haydn::R13 || R == Haydn::R15)
      return false;
    if (MRI.isReserved(R))
      return false;
    if (regMentionedInBlocks(R, Blocks))
      return false;
    for (const MachineBasicBlock *B : Blocks) {
      if (!B)
        continue;
      for (const MachineBasicBlock *S : B->successors()) {
        if (Blocks.contains(S))
          continue;
        if (liveInContains(*S, R.asMCReg())) {
          LLVM_DEBUG(dbgs()
                     << DebugPrefix << ": pickCounterReg reject "
                     << printReg(R, &TRI) << " live after loop exit ("
                     << printMBBReference(*S) << ")\n");
          return false;
        }
      }
    }
    // Counter ownership law (calls / callee-saved): the materialize point is
    // InsertPt — the range is the preheader tail from there plus the blocks.
    if (!isSoundDemoteCounter(R.asMCReg(), Blocks, &Preheader,
                              MachineBasicBlock::const_iterator(InsertPt), MF,
                              TRI))
      return false;
    if (!LPR.available(MRI, R))
      return false;
    return true;
  };

  if (isUsable(Prefer))
    return Prefer;

  // Call-clobbered temps first. R12 last (normal GPR; AIE: no free AT).
  static const MCPhysReg CandsGPR[] = {
      Haydn::R11, Haydn::R10, Haydn::R9, Haydn::R8, Haydn::R7,
      Haydn::R4,  Haydn::R3,  Haydn::R2, Haydn::R1, Haydn::R12};
  for (MCPhysReg R : CandsGPR) {
    if (Prefer.isPhysical() && R == Prefer)
      continue;
    if (isUsable(R))
      return R;
  }
  LLVM_DEBUG(dbgs() << DebugPrefix << ": pickCounterReg — no free GPR "
                       "(LivePhysRegs + loop mention); demote refuses "
                       "erase-only on live body\n");
  return Register();
}

void haydn::hwloop::materializeTripCount(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt, DebugLoc DL,
    const HaydnInstrInfo &TII, Register Dst, Register SrcReg, int64_t SrcImm,
    bool HasImm) {
  // Every demote trip-materialize MI is exact-committed before the
  // second BranchRelaxation (shared commitLateProductCycle surface).
  if (!HasImm) {
    if (SrcReg == Dst)
      return;
    // Post-RA: real MOVE32 (not generic COPY — expand-pseudos already ran).
    // emitExactLateDef commits the Format E member first; members are
    // dest+src only. Logical MOVE32's vestigial rs2=rs1 must not be added
    // (verifier: extra explicit operand on non-variadic member).
    emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::MOVE32, Dst,
                     [&](MachineInstrBuilder MIB) { MIB.addReg(SrcReg); });
    return;
  }

  // uimm16 trip counts: XOR-zero then ADDI32_W (expandPostRA already ran).
  if (SrcImm == 0) {
    emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::XOR32, Dst,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                     });
    return;
  }
  emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::XOR32, Dst,
                   [&](MachineInstrBuilder MIB) {
                     MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                   });
  emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::ADDI32_W, Dst,
                   [&](MachineInstrBuilder MIB) {
                     MIB.addReg(Dst).addImm(SrcImm);
                   });
}
