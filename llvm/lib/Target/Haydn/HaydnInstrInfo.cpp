//===-- HaydnInstrInfo.cpp - Haydn Instruction Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Haydn implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "HaydnInstrInfo.h"
#include "HaydnMspCloneFamily.h"
#include "HaydnPipelinerLoopInfo.h"
#include "Haydn.h"
#include "HaydnFrameLowering.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnMachineScheduler.h"
#include "HaydnPortModel.h"
#include "HaydnPostRAScratch.h"
#include "HaydnResourceCycle.h"
#include "HaydnResourceRestrictionClasses.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMatInt.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnRelocLayout.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachinePipeliner.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

#define DEBUG_TYPE "haydn-instr-info"

using namespace llvm;

namespace llvm {
unsigned haydnLogicalOpcode(unsigned Opc) {
  // D1.130: CFG/analyzeBranch keep gMIR role names. The encode-inverse
  // table maps B→BEQZ for freeze/MC only; peeling B here made uncond look
  // like cond BEQZ and broke 200+ tests.
  if (Opc == Haydn::B || Opc == Haydn::JALR_CALL || Opc == Haydn::JAL_TCO ||
      Opc == Haydn::JALR_TCO)
    return Opc;
  if (unsigned Mapped = haydn::msp::logicalOpcodeForMspClone(Opc))
    return Mapped;
  return haydn::format_e::logicalOpcodeOrSelf(Opc);
}
} // namespace llvm

namespace {

// HaydnFinalizeBundle: every real MI is a BUNDLE root. BranchRelaxation
// and analyzeBranch see MBB::iterator → BUNDLE headers; architectural opcode
// and MBB operands live on the child (AIE-style wrap; Hexagon instr_iterator
// peer). Return the first control-flow child, else \p MI.
const MachineInstr &unwrapBundleControlFlow(const MachineInstr &MI) {
  if (!MI.isBundle())
    return MI;
  // Prefer a cond, then other CFG (uncond B / RET / dest-less JALR), then a
  // returning call. JALR_CALL is isCall only; listed first in a cycle it must
  // not hide a sibling B/cond from analyzeBranch (va-arg-24).
  const MachineInstr *Cfg = nullptr;
  const MachineInstr *Call = nullptr;
  for (const MachineInstr *C : haydn::bundle::members(MI)) {
    if (C->isConditionalBranch(MachineInstr::IgnoreBundle))
      return *C;
    // JAL_W / JALR_CALL: isCall, not terminator/barrier/indirect.
    const bool ReturningCall =
        C->isCall(MachineInstr::IgnoreBundle) &&
        !C->isTerminator(MachineInstr::IgnoreBundle) &&
        !C->isIndirectBranch(MachineInstr::IgnoreBundle) &&
        !C->isBarrier(MachineInstr::IgnoreBundle);
    if (ReturningCall) {
      if (!Call)
        Call = C;
      continue;
    }
    if (!Cfg &&
        (C->isBranch(MachineInstr::IgnoreBundle) ||
         C->isReturn(MachineInstr::IgnoreBundle) ||
         C->isIndirectBranch(MachineInstr::IgnoreBundle) ||
         C->isBarrier(MachineInstr::IgnoreBundle) ||
         C->isCall(MachineInstr::IgnoreBundle)))
      Cfg = C;
  }
  if (Cfg)
    return *Cfg;
  return Call ? *Call : MI;
}

MachineInstr &unwrapBundleControlFlow(MachineInstr &MI) {
  return const_cast<MachineInstr &>(
      unwrapBundleControlFlow(const_cast<const MachineInstr &>(MI)));
}

// Non-issue children that may ride in a terminator cycle without changing
// EncodedBytes. Coissued real work is not padding — removeBranch keeps it.
bool isTerminatorCyclePadding(const MachineInstr &MI) {
  if (MI.isDebugInstr() || MI.isMetaInstruction() || MI.isImplicitDef() ||
      MI.isKill() || MI.isCFIInstruction() || MI.isPosition())
    return true;
  unsigned Opc = MI.getOpcode();
  if (haydn::format_e::logicalOpcodeOrSelf(Opc) == Haydn::NOP)
    return true;
  if (const MachineFunction *MF = MI.getMF()) {
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    return StringRef(haydn::format_e::peelLogicalOpcodeName(TII->getName(Opc)))
        .equals_insensitive("NOP");
  }
  return false;
}

bool isControlFlowChild(const MachineInstr &MI) {
  return MI.isBranch(MachineInstr::IgnoreBundle) ||
         MI.isReturn(MachineInstr::IgnoreBundle) ||
         MI.isIndirectBranch(MachineInstr::IgnoreBundle);
}

} // namespace

// HexagonConstPropagation.cpp:2508-2512 replaceWithNop (setDesc + strip).
// AIE has no BR and no child-NOP overlay. Haydn overlay: generated
// same-row NOP of the committed packet (pipeline.md preserve-or-extend);
// do not unbundle or re-choose the row.
void llvm::neutralizeSameRowNop(MachineInstr &MI, const HaydnInstrInfo &TII) {
  const unsigned NopOpc = haydn::bundle::lateProductMemberOpcode(Haydn::NOP);
  MI.setDesc(TII.get(NopOpc));
  while (MI.getNumOperands() > 0)
    MI.removeOperand(0);
}

namespace {

bool isHaydnCondBranch1Reg(unsigned LogicalOpc) {
  switch (LogicalOpc) {
  case Haydn::BEQZ_W:
  case Haydn::BNEZ_W:
  case Haydn::BGEZ_W:
  case Haydn::BLTZ_W:
  case Haydn::BEQZ:
  case Haydn::BNEZ:
  case Haydn::BGEZ:
  case Haydn::BLTZ:
    return true;
  default:
    return false;
  }
}

bool isHaydnCondBranch2Reg(unsigned LogicalOpc) {
  switch (LogicalOpc) {
  case Haydn::BEQ_W:
  case Haydn::BNE_W:
  case Haydn::BGE_W:
  case Haydn::BGEU_W:
  case Haydn::BLT_W:
  case Haydn::BLTU_W:
  case Haydn::BEQ:
  case Haydn::BNE:
  case Haydn::BGE:
  case Haydn::BGEU:
  case Haydn::BLT:
  case Haydn::BLTU:
    return true;
  default:
    return false;
  }
}

bool isHaydnIndirectJALR(unsigned LogicalOpc) {
  return LogicalOpc == Haydn::JALR || LogicalOpc == Haydn::JALR_W ||
         LogicalOpc == Haydn::JALR_CALL;
}

bool isHaydnAddrMaterializeOpc(unsigned LogicalOpc) {
  return LogicalOpc == Haydn::LUI || LogicalOpc == Haydn::ADDI32_W ||
         LogicalOpc == Haydn::ADDI32;
}

/// Short uncond dest: B through MIR (encoder peels to BEQZ rs=R0). Residual
/// BEQZ_W R0 when R0 is not a live-in i1 (HWLoopDemote latch leftover).
/// Catalog BEQZ_W with a real rs is cond only. JAL_W / JAL_TCO are CallSImm20,
/// not an analyzable uncond (AIEBaseInstrInfo.cpp:186-188 /
/// RISCVInstrInfo.cpp:1325-1327 leave non-branch last unanalyzable).
MachineBasicBlock *uncondBranchDest(const MachineInstr &MI,
                                    const MachineBasicBlock &MBB) {
  const unsigned Opc = haydnLogicalOpcode(MI.getOpcode());
  if (Opc == Haydn::B && MI.getNumOperands() > 0 && MI.getOperand(0).isMBB())
    return MI.getOperand(0).getMBB();
  if (Opc == Haydn::BEQZ_W && MI.getNumOperands() > 1 &&
      MI.getOperand(0).isReg() && MI.getOperand(0).getReg() == Haydn::R0 &&
      MI.getOperand(1).isMBB() && MBB.getParent() &&
      !MBB.getParent()->getRegInfo().isLiveIn(Haydn::R0))
    return MI.getOperand(1).getMBB();
  return nullptr;
}

/// JALR_CALL is the returning fnptr call (isCall, not
/// terminator/barrier/indirect). Opcode identity, not peel: catalog JALR /
/// JALR_W keep isTerminator+isBarrier (RET / computed-goto / long-form).
/// musttail jalr is JALR_TCO (isReturn), not this predicate. Do not classify
/// `$rd = JALR rd` as a call — that operand shape is also insertIndirect
/// long-form uncond (link discarded into scratch).
bool isHaydnReturningJalrCall(const MachineInstr &MI) {
  if (MI.getOpcode() == Haydn::JALR_CALL)
    return true;
  if (!isHaydnIndirectJALR(haydnLogicalOpcode(MI.getOpcode())))
    return false;
  return MI.isCall(MachineInstr::IgnoreBundle) &&
         !MI.isTerminator(MachineInstr::IgnoreBundle) &&
         !MI.isIndirectBranch(MachineInstr::IgnoreBundle) &&
         !MI.isBarrier(MachineInstr::IgnoreBundle);
}



// Opcode-only isBranchOffsetInRange cannot unwrap a BUNDLE child. BR calls
// getBranchDestBlock(MI) immediately before isBlockInRange(MI)
// (BranchRelaxation.cpp:739-740), so that pairing is the only MI context
// the virtual opcode hook sees. D1.142: record the same MI rather than a
// dangling bool — isBranchOffsetInRange(BUNDLE) consumes the arm only when
// it is that BUNDLE. Invert-swap must range-test the cond child window.
// One-way BUNDLE (no trailing CFG JALR) stays always-in-range so BR cannot
// trampoline committed packets; LBN owns those sites via the MI overload.
static thread_local const MachineInstr *HaydnBundleCondRangeMI = nullptr;

static unsigned haydnBranchRangeOpcode(const MachineInstr &MI) {
  if (!MI.isBundle())
    return MI.getOpcode();
  const MachineInstr &Br = unwrapBundleControlFlow(MI);
  if (&Br != &MI)
    return Br.getOpcode();
  return MI.getOpcode();
}

// CFG JALR in this MBB: unwrap + bundle members, walking back past short
// cond/uncond and addr-materialize (RED: last is B, pair+JALR sit above).
// Returning JALR_CALL is not a CFG jump.
const MachineInstr *trailingCfgJalr(const MachineBasicBlock &MBB) {
  auto take = [](const MachineInstr &MI) -> const MachineInstr * {
    if (isHaydnIndirectJALR(haydnLogicalOpcode(MI.getOpcode())) &&
        !isHaydnReturningJalrCall(MI))
      return &MI;
    return nullptr;
  };
  for (const MachineInstr &Top : llvm::reverse(MBB)) {
    if (Top.isDebugInstr() || Top.isCFIInstruction() || Top.isKill() ||
        Top.isImplicitDef())
      continue;
    if (const MachineInstr *J = take(unwrapBundleControlFlow(Top)))
      return J;
    if (Top.isBundle()) {
      SmallVector<const MachineInstr *, 4> Mems;
      for (const MachineInstr *C : haydn::bundle::members(Top))
        Mems.push_back(C);
      for (const MachineInstr *C : llvm::reverse(Mems))
        if (const MachineInstr *J = take(*C))
          return J;
    }
    unsigned L = haydnLogicalOpcode(unwrapBundleControlFlow(Top).getOpcode());
    if (isHaydnAddrMaterializeOpc(L) || isHaydnCondBranch1Reg(L) ||
        isHaydnCondBranch2Reg(L) || L == Haydn::B || L == Haydn::NOP)
      continue;
    if (Top.isBranch(MachineInstr::AnyInBundle))
      continue;
    break;
  }
  return nullptr;
}

// D1.117/D1.153: one reaching-def walk from JALR rs. Peers leave every
// indirect unanalyzable (AIEBaseInstrInfo.cpp:186-188,
// RISCVInstrInfo.cpp:1325-1327); Hexagon walks instr_iterator with no
// LUI+ADDI pair (HexagonInstrInfo.cpp:435-508). Haydn overlay matches
// insertIndirectBranch emission (LUI then ADDI32_W then JALR_W) and
// RISC-V PseudoJump expand (RISCVExpandPseudoInsts.cpp:705-714 AUIPC
// then consumer). Complete is nearest ADDI on JALR rs, then that ADDI's
// exact LUI producer (COPY/MOVE32 explicit), unique coherent %bb dest.
// Reverse-collecting LUI then ADDI is not complete: forward ADDI->LUI->
// JALR must named-refuse. Dest still names the first recovered %bb so
// cond+JALR stays analyzable when the pair is incomplete. Returning
// JALR_CALL / dest-less JALR: empty/incomplete, not UNREACHABLE.
// Incoherent same-register dest pair: Incoherent, not Complete.
HaydnJalrAddrMaterializeChain
scanJalrAddrMaterializeChain(const MachineInstr &JalrIn) {
  HaydnJalrAddrMaterializeChain Out;
  const MachineInstr *J = &JalrIn;
  if (JalrIn.isBundle()) {
    J = nullptr;
    for (const MachineInstr *C : haydn::bundle::members(JalrIn)) {
      if (!isHaydnIndirectJALR(haydnLogicalOpcode(C->getOpcode())))
        continue;
      if (isHaydnReturningJalrCall(*C))
        continue;
      J = C;
      break;
    }
    if (!J)
      return Out;
  }

  unsigned Opc = haydnLogicalOpcode(J->getOpcode());
  if (!isHaydnIndirectJALR(Opc))
    return Out;
  Out.Control = const_cast<MachineInstr *>(J);

  Register JumpReg;
  if (J->getNumOperands() >= 2 && J->getOperand(1).isReg())
    JumpReg = J->getOperand(1).getReg();
  else if (J->getNumOperands() && J->getOperand(0).isReg())
    JumpReg = J->getOperand(0).getReg();
  else
    return Out;
  Out.JumpReg = JumpReg;

  if (isHaydnReturningJalrCall(*J))
    return Out;

  Register Cur = JumpReg;
  MachineBasicBlock *LuiDest = nullptr;
  MachineBasicBlock *AddiDest = nullptr;
  enum class Stage { NeedAddi, NeedLui };
  Stage St = Stage::NeedAddi;

  auto destFromAddrMI = [&](const MachineInstr &AMI) -> MachineBasicBlock * {
    if (!isHaydnAddrMaterializeOpc(haydnLogicalOpcode(AMI.getOpcode())))
      return nullptr;
    if (!AMI.getNumOperands() || !AMI.getOperand(0).isReg() ||
        AMI.getOperand(0).getReg() != Cur)
      return nullptr;
    for (const MachineOperand &MO : AMI.operands())
      if (MO.isMBB())
        return MO.getMBB();
    return nullptr;
  };
  auto skipNearCond = [](const MachineInstr &AMI) {
    unsigned L = haydnLogicalOpcode(AMI.getOpcode());
    return isHaydnCondBranch1Reg(L) || isHaydnCondBranch2Reg(L) ||
           L == Haydn::B;
  };
  auto followGprMove = [&](const MachineInstr &AMI) -> bool {
    unsigned L = haydnLogicalOpcode(AMI.getOpcode());
    if (L != Haydn::MOVE32 && !AMI.isCopy())
      return false;
    if (AMI.getNumOperands() < 2 || !AMI.getOperand(0).isReg() ||
        !AMI.getOperand(1).isReg() || AMI.getOperand(0).getReg() != Cur)
      return false;
    Cur = AMI.getOperand(1).getReg();
    return true;
  };
  auto clobbersCur = [&](const MachineInstr &AMI) {
    for (const MachineOperand &MO : AMI.operands()) {
      if (MO.isReg() && MO.isDef() && !MO.isImplicit() && MO.getReg() == Cur)
        return true;
      if (MO.isRegMask() && MO.clobbersPhysReg(Cur))
        return true;
    }
    return false;
  };

  enum class Walk { Continue, Stop };
  auto considerOne = [&](const MachineInstr &AMI) -> Walk {
    if (&AMI == J)
      return Walk::Continue;
    if (AMI.isDebugInstr() || AMI.isCFIInstruction() ||
        AMI.isImplicitDef() || AMI.isKill() || AMI.isMetaInstruction())
      return Walk::Continue;
    if (MachineBasicBlock *D = destFromAddrMI(AMI)) {
      unsigned L = haydnLogicalOpcode(AMI.getOpcode());
      const bool IsLui = L == Haydn::LUI;
      if (St == Stage::NeedAddi) {
        if (IsLui) {
          // Reaching def of JALR rs is LUI, not ADDI (swapped / lui-only).
          if (!Out.Lui) {
            Out.Lui = const_cast<MachineInstr *>(&AMI);
            LuiDest = D;
          }
          if (!Out.Dest)
            Out.Dest = D;
          return Walk::Stop;
        }
        if (!Out.Addi) {
          Out.Addi = const_cast<MachineInstr *>(&AMI);
          AddiDest = D;
        }
        if (!Out.Dest)
          Out.Dest = D;
        if (AMI.getNumOperands() < 2 || !AMI.getOperand(1).isReg())
          return Walk::Stop;
        Cur = AMI.getOperand(1).getReg();
        St = Stage::NeedLui;
        return Walk::Continue;
      }
      // NeedLui: exact LUI producer of ADDI rs. Another ADDI is a clobber.
      if (!IsLui)
        return Walk::Stop;
      if (!Out.Lui) {
        Out.Lui = const_cast<MachineInstr *>(&AMI);
        LuiDest = D;
      }
      if (!Out.Dest)
        Out.Dest = D;
      return Walk::Stop;
    }
    if (followGprMove(AMI))
      return Walk::Continue;
    unsigned L = haydnLogicalOpcode(AMI.getOpcode());
    if (skipNearCond(AMI) || L == Haydn::NOP)
      return Walk::Continue;
    if (isHaydnReturningJalrCall(AMI))
      return Walk::Continue;
    if (clobbersCur(AMI))
      return Walk::Stop;
    return Walk::Continue;
  };
  auto considerAddr = [&](const MachineInstr &AMI) -> Walk {
    if (AMI.isBundle()) {
      SmallVector<const MachineInstr *, 4> Mems;
      for (const MachineInstr *C : haydn::bundle::members(AMI))
        Mems.push_back(C);
      for (const MachineInstr *C : llvm::reverse(Mems)) {
        if (considerOne(*C) == Walk::Stop)
          return Walk::Stop;
      }
      return Walk::Continue;
    }
    if (AMI.isBundledWithPred())
      return Walk::Continue;
    return considerOne(AMI);
  };

  const MachineBasicBlock *Parent = J->getParent();
  if (!Parent)
    return Out;
  bool Done = false;
  for (MachineBasicBlock::const_instr_iterator It = J->getIterator();
       It != Parent->instr_begin();) {
    --It;
    if (considerAddr(*It) == Walk::Stop) {
      Done = true;
      break;
    }
  }
  if (!Done && !Out.Dest) {
    const MachineInstr *Root = haydn::bundle::bundleRootOf(*J);
    MachineBasicBlock::const_iterator Start =
        (Root ? Root : J)->getIterator();
    while (Start != Parent->begin()) {
      --Start;
      if (considerAddr(*Start) == Walk::Stop)
        break;
    }
  }

  if (Out.Lui && Out.Addi && LuiDest && AddiDest && LuiDest != AddiDest)
    Out.Incoherent = true;

  Out.Complete = Out.Lui && Out.Addi && Out.Dest && !Out.Incoherent;
  return Out;
}

// insertBranch is const; MachineBasicBlock::findBranchDebugLoc is not.
// Walk back past debug/CFI (AIE/RISCV BuildMI(DL) overlay: never invent a
// source location for a synthetic trampoline when the caller passed none).
DebugLoc haydnInheritBranchDebugLoc(const MachineBasicBlock &MBB,
                                    const DebugLoc &DL) {
  if (DL)
    return DL;
  for (const MachineInstr &MI : llvm::reverse(MBB)) {
    if (MI.isDebugInstr() || MI.isCFIInstruction())
      continue;
    if (MI.getDebugLoc())
      return MI.getDebugLoc();
    break;
  }
  return DL;
}

void haydnStampDebugLocOnNewInstrs(MachineBasicBlock &MBB, MachineInstr *LastOld,
                                   const DebugLoc &DL) {
  if (!DL)
    return;
  MachineBasicBlock::iterator I =
      LastOld ? std::next(LastOld->getIterator()) : MBB.begin();
  for (; I != MBB.end(); ++I) {
    if (!I->getDebugLoc())
      I->setDebugLoc(DL);
  }
}

// Long-form LUI+ADDI32_W+JALR_W is inserted into a trampoline MBB that
// BranchRelaxation already wired as Dest's predecessor (RISCV/AArch64
// insertIndirectBranch: TII does not addSuccessor). GR2.7: the owning
// normalization seat is LBN in-block templates; once the first Finalize
// run stamps the postcommit block budget this callback refuses
// (CFG-creation wall). Dest PHIs may still name the original pred after
// replaceSuccessor — retarget them onto the trampoline. RestoreBB is
// unused (generic BR erases the empty block at BranchRelaxation.cpp:687).
void haydnPreserveLongFormJumpState(MachineBasicBlock &Trampoline,
                                    MachineBasicBlock &NewDestBB) {
  if (Trampoline.pred_size() != 1)
    return;
  MachineBasicBlock *OrigPred = *Trampoline.pred_begin();
  if (OrigPred != &Trampoline && !OrigPred->isSuccessor(&NewDestBB))
    NewDestBB.replacePhiUsesWith(OrigPred, &Trampoline);
}

} // namespace

// SMS pipelining of ZOL-form loops. DEFAULT ON per / ("classic SMS
// running PRE-RA on ZOL form"). The IR-level HardwareLoops pass runs before
// IRTranslator, so every countable single-BB loop arrives at the pre-RA
// pipeliner already in LoopStart (preheader) + PseudoLoopEnd (latch) ZOL form;
// the naive icmp+br countable-loop path never sees them. With the default off
// SMS rejected 100% of real loops with "Unable to analyzeLoop".
//
// The "experimental / careful validation" gate is retired: the ZOL
// PipelinerLoopInfo (shouldIgnoreForPipelining, shouldUseSchedule rejecting
// StageCount<=1, adjustTripCount editing LoopStart's $adj, the no-guard
// createTripCountGreaterCondition) is complete and AIE-faithful. The prior
// single-stage +358-bloat regression feared is now gated inside
// shouldUseSchedule. Declared non-static so
// HaydnSubtarget::enableWindowScheduler can read it (that override is the
// sole layer keeping the WindowScheduler off ZOL loops — see
// HaydnSubtarget.cpp for the D1.29-verified mechanism; SMS is the sole
// pipeliner while ZOL pipelining is on).
// SMS of ZOL-form loops (default ON). When off, SMS skips ZOL loops — they
// still form hwloops via IR HardwareLoops, just without software pipelining.
//
// D1.29 CLOSE (2026-08-30) — the former NOTE here (admitted 2026-07-27 in
// 7ef07decdc32, pre-W68, with no reproducer and no tracker) claimed
// multi-stage SMS "has been seen to misplace prolog/kernel/epilog vs HWLOOP
// BEGIN/END" on some ZOL byte-mem loops (e.g. libc memcpy @ -O3). CLOSED by
// mechanism + corpus, no reproducer exists on the current artifact:
//
//  - Mechanism: the W68.1 edn fatal root-caused the one real misplacement
//    class of this shape — the pipelined ZOL body (preheader LoopStart ->
//    prologue -> kernel(self-latch PLE)) becoming invisible to body
//    resolution. It is fixed by the pure-CFG guarded-chain proof
//    (HaydnHWLoopDemote.cpp resolveBodyMBBCore / prologueChainReachesKernel)
//    with fail-closed rejection in HaydnFixupHwLoops on any unproven body;
//    CB-166 dynamic-guard chains (033372428db8) and the D1.6 pipelined
//    LoopStart demote Prefer+Adj law (1288c452f1a2) cover the runtime-trip
//    and demote siblings. Placement is proven by CFG, never layout; any
//    unprovable shape demotes or fatals — it cannot silently misbind.
//  - Corpus (D1.29 sweep, artifact of record cf28e436c7c8): byte-mem i8
//    copy loops — constant trip 8/16/64, runtime trip, and store-only fill —
//    compiled -O2/-O3 all accept multi-stage (stages=2, II=2,
//    "Schedule Found? 1"); the committed HWLOOP windows bracket exactly the
//    self-latched kernel (prologue peel before START, epilog drain after
//    inclusive END) and the same artifacts execute clean in BundleSim ISS
//    (GUEST_EXIT 0, value/memory equal).
//  - Pin: llvm/test/CodeGen/Haydn/d129-zol-bytemem-multistage-memcpy.ll
//    checks the accept + the prologue/kernel/epilog-vs-BEGIN/END structure
//    so the class stays testably closed.
// If a byte-mem misplacement ever reappears, fix it at shouldUseSchedule
// (fail-closed shape reject) or in the CFG proof — never by layout, and
// never by disabling ZOL SMS; this flag stays emergency-disable only.
cl::opt<bool> EnableZOLPipelining(
    "haydn-zol-pipelining", cl::Hidden, cl::init(true),
    cl::desc("Enable SMS pipelining of ZOL-form loops (default on)"));

bool llvm::haydnZOLPipeliningEnabled() { return EnableZOLPipelining; }

// AIE aie-loop-min-tripcount peer: force a floor MinTripCount for all SMS
// candidates. -1 = disabled (default). Used for soak / when MD is missing.
// Non-static since F39: the post-RA multi-stage host honors the same floor
// in its static-trip proof (extern in HaydnInstrInfo.h).
namespace llvm {
cl::opt<int> HaydnLoopMinTripCount(
    "haydn-loop-min-tripcount", cl::Hidden, cl::init(-1),
    cl::desc("AIE aie-loop-min-tripcount peer: floor MinTripCount for ZOL SMS "
             "(-1 = disabled). Warning: applies to all ZOL SMS candidates."));
} // namespace llvm

// Pre-RA SMS containment: StageCount > 1 is rejected in shouldUseSchedule.
// Product multi-stage SWP and exact E96 commit live only in the post-RA engine.
// No pre-RA SMS BUNDLE / clone-cycle group materialize remains.

// GR2.1: the pre-RA SMS ResourceCycle boundary is the Kind-A
// HaydnIssueWidthCycle (generated IssueWidth entry cap + the shared
// same-cycle RAW/WAW dependency laws). The former exact HaydnResourceCycle
// arm, its -haydn-hr-resource-cycle switch, and the createDFAPacketizer
// fallback are deleted from this seat: exact capacity/unit/port/hazard
// matching is post-RA HR business, and pre-RA is proposal-only (pipeline
// contract Kind A). HaydnResourceCycle remains the post-RA HR peer depth +
// shared statics + unit-test surface.

// SMS-HOOK positive / bisect: product itineraries are InstrStage cycles==1, so
// the multi-cycle scan has no live reject corpus. Force fail-closed so lits can
// pin analyzeLoopForPipelining → nullptr and "SMS-HOOK: reject" without
// inventing a multi-cycle product FU. Default OFF — never product policy.
static cl::opt<bool> ForceSMSHookReject(
    "haydn-sms-hook-force-reject", cl::Hidden, cl::init(false),
    cl::desc("SMS-HOOK test/bisect: reject every loop in "
             "analyzeLoopForPipelining (fail-closed). Default OFF."));

// SMS-HOOK II-wrap false-accept positive / bisect: product has no multi-cycle
// InstrStage rows, so the live multi-cycle scan cannot demonstrate the
// issue-time-only II-wrap hazard (independent ResourceCycle per modulo phase
// would accept concurrent use that multi-cycle occupancy wrapping under II
// forbids). Force the same analyzeLoop fail-closed path with an II-wrap
// specific log line. Default OFF — never product policy.
static cl::opt<bool> ForceSMSHookIIWrapReject(
    "haydn-sms-hook-force-iiwrap-reject", cl::Hidden, cl::init(false),
    cl::desc("SMS-HOOK test/bisect: reject every loop as II-wrap "
             "issue-time-only false-accept (fail-closed). Default OFF."));

// Hexagon manner (HexagonBranchRelaxation.cpp:37-38, 95-114): BR has no
// exact final layout, so Hexagon keeps a residual byte buffer (cl::init 200
// there). Haydn's generic BranchRelaxation only sums getInstSizeInBytes, so
// named late-layout growth (same-slot serial parcels, JT R0 re-zero, hwloop
// setup pads) is charged there. The distance buffer is then residual layout
// uncertainty — one insertIndirectBranch sequence. AIE has empty
// addPreEmitPass (no BR). Init is HaydnHWLoopContracts.h
// BranchRelaxSafetyBufferBytes (equals MaxSingleBranchGrowthBytes), never a
// free-standing 200/1024.
static cl::opt<uint32_t> BranchRelaxSafetyBuffer(
    "haydn-branch-relax-safety-buffer", cl::Hidden,
    cl::init(static_cast<uint32_t>(
        haydn::hwloop::BranchRelaxSafetyBufferBytes)),
    cl::desc("Extra bytes added to branch distance when deciding if a "
             "conditional is in WIDE_BranchSImm12 range (signed 12-bit "
             "byte PC+imm). Default is BranchRelaxSafetyBufferBytes after "
             "named growth is charged in getInstSizeInBytes."));

// Product MemoryEdges latency is architectural: First/LastMemoryCycle tables
// (Last-First+1, floored at 1) for Slot0_LS / Slot1_LD / Slot01_LD / Slot2_LS.
// Class-agnostic latency-1 was a historical densify soften that made II/density
// fiction; -haydn-accurate-memory-latency=false remains a soak-off only.
// Densify invents stay FATED. Unit/lit pin product Latency=2 vs soft Latency=1.
static cl::opt<bool> AccurateMemoryLatency(
    "haydn-accurate-memory-latency", cl::Hidden, cl::init(true),
    cl::desc("Compute getMemoryLatency from First/LastMemoryCycle tables "
             "(default ON = product architectural latency). "
             "false = class-agnostic latency 1 soak-off only."));

#define GET_INSTRINFO_CTOR_DTOR
#include "HaydnGenInstrInfo.inc"
#include "HaydnGenDFAPacketizer.inc"

// Sched-class enum for generated MemoryCycle tables (AIE MemInstrItinData /
// AIEMemoryCyclesEmitter.cpp:123-157 peer). HaydnGenMemoryCycles.inc is the
// single home for first/last literals.
#define GET_INSTRINFO_SCHED_ENUM
#include "HaydnGenInstrInfo.inc"
#include "HaydnGenMemoryCycles.inc"

// Generated member → logical inverse (AIE getAlternateInstsOpcode inverted).
#define GET_FORMAT_E_MEMBER_TO_LOGICAL
#include "HaydnGenFormatEMemberOpcodes.inc"

HaydnInstrInfo::HaydnInstrInfo(const HaydnSubtarget &STI)
    : HaydnGenInstrInfo(STI, RegInfo, Haydn::ADJCALLSTACKDOWN,
                        Haydn::ADJCALLSTACKUP),
      RegInfo(/* HwMode*/ 0),
      STI(STI) {}

void HaydnInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                const DebugLoc &DL, Register DestReg,
                                Register SrcReg, bool KillSrc, bool RenamableDest,
                                bool RenamableSrc) const {
  if (Haydn::DR64RegClass.contains(DestReg, SrcReg)) {
    // DR64 → DR64: OR64 rd, rs, rs (rd = rs | rs = rs).
    // Cannot use ADD64 with R0 because ADD64 requires DR64 operands.
    BuildMI(MBB, MI, DL, get(Haydn::OR64), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  // A physical COPY is only defined within one register bank (Hard #2: DR64
  // is a separate bank, NOT aliased to GPR32 pairs). Cross-bank data motion
  // MUST go through an explicit lane-extract (MOVE32_DR_L / MOVE32_DR_H) or
  // lane-insert (MOV_GPR_TO_DR64) pseudo before regalloc, never copyPhysReg.
  // Failing closed here converts a silent MC wrong-code (MOVE32_E3_E0_ALU2_R
  // filled with one GPR + two DR sources — fillFormatEMemberInst aborts) into
  // a pointed error naming the owning selector that produced the cross-bank
  // COPY. The v8i8 lane-extract selector path materialises every DR64→GPR32
  // lane extract as MOVE32_DR_L/_H (see HaydnInstructionSelector.cpp
  // G_UNMERGE_VALUES), so no generic cross-bank COPY should survive to here.
  if (Haydn::GPR32RegClass.contains(DestReg, SrcReg)) {
    // GPR32 → GPR32: MOVE32 rd, rs. Peer: AIE2InstrInfo.cpp MOVScl dest,
    // src. Logical and Format E members are dest+src.
    BuildMI(MBB, MI, DL, get(Haydn::MOVE32), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  // Cross-bank COPY (DR64 <-> GPR32). Not a physical copy — Hard #2. The
  // upstream selector must materialise this via MOVE32_DR_L/H (DR64→GPR32
  // lane extract) or MOV_GPR_TO_DR64 (GPR32 pair → DR64 pack). Fail closed
  // with a pointed message rather than emitting a malformed MOVE32.
  report_fatal_error(
      "Haydn: cross-bank COPY in copyPhysReg (DestReg=" +
      RegInfo.getRegAsmName(DestReg) + ", SrcReg=" +
      RegInfo.getRegAsmName(SrcReg) +
      "). DR64 and GPR32 are separate register banks (Hard #2); cross-bank "
      "data motion must use MOVE32_DR_L/_H (DR64->GPR32 lane extract) or "
      "MOV_GPR_TO_DR64 (GPR32 pair -> DR64 pack). Fix the selector that "
      "produced this COPY (likely a v8i8/v4i16 lane-extract that bypassed "
      "MOVE32_DR_L/H materialisation in HaydnInstructionSelector.cpp "
      "G_UNMERGE_VALUES).");
}

std::optional<DestSourcePair>
HaydnInstrInfo::isCopyInstrImpl(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default:
    return std::nullopt;
  case Haydn::MOVE32:
    // Canonical GPR move: MOVE32 rd, rs.
    if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
        !MI.getOperand(1).isReg())
      return std::nullopt;
    return DestSourcePair{MI.getOperand(0), MI.getOperand(1)};
  case Haydn::OR32:
  case Haydn::OR64:
    // Bank copy: OR rd, rs, rs ⇒ rd = rs | rs = rs.
    // Do NOT rewrite these to bare COPY post-RA (bundler/AsmPrinter drop).
    if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
        !MI.getOperand(1).isReg() || !MI.getOperand(2).isReg())
      return std::nullopt;
    if (MI.getOperand(1).getReg() != MI.getOperand(2).getReg())
      return std::nullopt;
    if (!MI.getOperand(1).getReg())
      return std::nullopt;
    return DestSourcePair{MI.getOperand(0), MI.getOperand(1)};
  }
}

void HaydnInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  unsigned Opc;
  unsigned Size;
  // Check if this is a 32-bit GPR class (including subclasses)
  // Use contains check since GPR32NoSPNoLR is a subclass
  if (RC == &Haydn::GPR32RegClass || RC == &Haydn::GPR32NoSPNoLRRegClass ||
      RC->hasSubClassEq(&Haydn::GPR32RegClass)) {
    Opc = Haydn::ST32;
    Size = 4;
  } else if (RC == &Haydn::DR64RegClass) {
    Opc = Haydn::ST64; // Use 64-bit store for DR64 registers
    Size = 8;
  } else {
    llvm_unreachable("Unknown register class for store");
  }

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOStore, Size, MFI.getObjectAlign(FrameIndex));

  auto MIB = BuildMI(MBB, MI, DL, get(Opc))
                 .addReg(SrcReg, getKillRegState(IsKill))
                 .addFrameIndex(FrameIndex)
                 .addImm(0)
                 .addMemOperand(MMO);
  if (Flags)
    MIB.setMIFlag(Flags);
}

void HaydnInstrInfo::loadRegFromStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
    int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    unsigned SubReg, MachineInstr::MIFlag Flags) const {
  assert(SubReg == 0 && "Haydn stack slots are whole-reg");
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  unsigned Opc;
  unsigned Size;
  // Check if this is a 32-bit GPR class (including subclasses)
  // Use contains check since GPR32NoSPNoLR is a subclass
  if (RC == &Haydn::GPR32RegClass || RC == &Haydn::GPR32NoSPNoLRRegClass ||
      RC->hasSubClassEq(&Haydn::GPR32RegClass)) {
    Opc = Haydn::LD32;
    Size = 4;
  } else if (RC == &Haydn::DR64RegClass) {
    Opc = Haydn::LD64; // logical; slot from placement / setDesc materialize
    Size = 8;
  } else {
    llvm_unreachable("Unknown register class for load");
  }

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOLoad, Size, MFI.getObjectAlign(FrameIndex));

  auto MIB = BuildMI(MBB, MI, DL, get(Opc), DestReg)
                 .addFrameIndex(FrameIndex)
                 .addImm(0)
                 .addMemOperand(MMO);
  if (Flags)
    MIB.setMIFlag(Flags);
}

/// Common implementation for isLoadFromStackSlot and isStoreToStackSlot.
/// Port of AIEBaseInstrInfo.cpp:1965-2018 (isStackSlotMemoryAccess).
/// \param IsLoad true for load detection, false for store detection.
/// \returns The register being loaded/stored, or 0 if not a stack access.
static Register isStackSlotMemoryAccess(const MachineInstr &MI, int &FrameIndex,
                                        bool IsLoad) {
  // Quick reject: check memory access type
  // (AIEBaseInstrInfo.cpp:1970-1978).
  if (IsLoad) {
    if (!MI.mayLoad() || MI.mayStore())
      return 0;
  } else {
    if (!MI.mayStore() || MI.mayLoad())
      return 0;
  }

  if (MI.getNumOperands() < 2)
    return 0;

  // AIEBaseInstrInfo.cpp:1980-1985: AIE requires SP use (loads often have
  // Uses=[SP]). Haydn architectural SP is R13. storeRegToStackSlot /
  // loadRegFromStackSlot emit FI base without an explicit R13 use until
  // eliminateFrameIndex — treat FI as sufficient for the pre-FE path;
  // otherwise require R13 (SP-relative post-shape still seen pre-PostFE).
  const TargetRegisterInfo *TRI = MI.getMF()->getSubtarget().getRegisterInfo();
  const bool HasFI = MI.getOperand(1).isFI();
  if (!HasFI && !MI.readsRegister(Haydn::R13, TRI))
    return 0;

  const MachineOperand &RegOp = MI.getOperand(0);
  if (!RegOp.isReg())
    return 0;
  if (IsLoad ? !RegOp.isDef() : !RegOp.isUse())
    return 0;

  // Pre-FE contract: FI base (AIEBaseInstrInfo.cpp:1997-1998).
  if (!HasFI)
    return 0;

  unsigned MatchingStackMMOs = 0;
  for (const auto *MMO : MI.memoperands()) {
    const bool IsMatching =
        (IsLoad ? MMO->isLoad() : MMO->isStore()) &&
        isa_and_nonnull<FixedStackPseudoSourceValue>(MMO->getPseudoValue());
    if (!IsMatching)
      continue;
    ++MatchingStackMMOs;
  }

  if (!MatchingStackMMOs)
    return 0;

  assert(MatchingStackMMOs == 1 &&
         "Expected exactly one fixed-stack MachineMemOperand");
  assert(MI.hasOneMemOperand() &&
         "Expected stack spill/reload to have exactly one MachineMemOperand");

  FrameIndex = MI.getOperand(1).getIndex();
  return RegOp.getReg();
}

Register HaydnInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                             int &FrameIndex) const {
  // AIEBaseInstrInfo.cpp:2021-2024.
  return isStackSlotMemoryAccess(MI, FrameIndex, /*IsLoad=*/true);
}

Register HaydnInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                            int &FrameIndex) const {
  // AIEBaseInstrInfo.cpp:2026-2028.
  return isStackSlotMemoryAccess(MI, FrameIndex, /*IsLoad=*/false);
}

Register HaydnInstrInfo::isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                                   int &FrameIndex) const {
  // After eliminateFrameIndex, FI becomes R13/R14 + imm; keep FixedStack MMO
  // recognition so MachineInstr::getRestoreSize works at AsmPrinter time.
  // Shape: AArch64InstrInfo.cpp:2719-2737 / X86InstrInfo.cpp:691-707, using
  // the same FixedStack MMO predicate as AIE isStackSlotMemoryAccess:1999-2007.
  if (Register Reg = isLoadFromStackSlot(MI, FrameIndex))
    return Reg;
  if (!MI.mayLoad() || MI.mayStore())
    return 0;
  if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(0).isDef())
    return 0;
  SmallVector<const MachineMemOperand *, 1> Accesses;
  if (!hasLoadFromStackSlot(MI, Accesses) || Accesses.size() != 1)
    return 0;
  FrameIndex =
      cast<FixedStackPseudoSourceValue>(Accesses.front()->getPseudoValue())
          ->getFrameIndex();
  return MI.getOperand(0).getReg();
}

Register HaydnInstrInfo::isStoreToStackSlotPostFE(const MachineInstr &MI,
                                                  int &FrameIndex) const {
  // Peer of isLoadFromStackSlotPostFE (AArch64InstrInfo.cpp:2698-2716).
  if (Register Reg = isStoreToStackSlot(MI, FrameIndex))
    return Reg;
  if (!MI.mayStore() || MI.mayLoad())
    return 0;
  if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(0).isUse())
    return 0;
  SmallVector<const MachineMemOperand *, 1> Accesses;
  if (!hasStoreToStackSlot(MI, Accesses) || Accesses.size() != 1)
    return 0;
  FrameIndex =
      cast<FixedStackPseudoSourceValue>(Accesses.front()->getPseudoValue())
          ->getFrameIndex();
  return MI.getOperand(0).getReg();
}

bool HaydnInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  TBB = nullptr;
  FBB = nullptr;
  Cond.clear();

  if (MBB.empty())
    return false;

  // Walk backwards through the block, skipping debug/CFI instructions.
  // LLVM analyzeBranch convention:
  // [cond-br TBB] [uncond-br FBB] → TBB + Cond + FBB
  // [cond-br TBB] → TBB + Cond (fallthrough)
  // [uncond-br TBB] → TBB only (unconditional)
  //
  // When walking backwards, we may see the unconditional branch BEFORE the
  // conditional one. We must remember the unconditional target (as FBB) and
  // continue scanning for a preceding conditional branch.
  MachineBasicBlock *UncondTarget = nullptr;

  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr() || I->isCFIInstruction() || I->isKill() ||
        I->isImplicitDef())
      continue;

    // BUNDLE roots wrap real terminators — analyze the child opcode
    // operands (unwrapBundleControlFlow). MBB::iterator never yields children.
    MachineInstr &Top = *I;
    MachineInstr *CFMI = &unwrapBundleControlFlow(Top);

    // Coissued cycle: unwrap prefers the near cond / uncond B over a
    // returning call. Recover a sibling short uncond (B, or residual BEQZ_W
    // R0) so the pair stays two-way (va-arg-22: hiding that sibling reports
    // one-way cond + layout fall-through → MEMORY_FAULT). Recover a sibling
    // CFG JALR dest from bundle members / LUI+ADDI the same way. Dest-less
    // bundled CFG JALR (RET / computed-goto) is a barrier — never analyzable
    // fall-through, even if a sibling B exists. JALR_CALL is a returning
    // call, not a CFG jump (va-arg-24): skip it before dest recovery, or a
    // coincidental LUI/ADDI %bb of rs becomes UncondTarget and drops the
    // following far cond. Dest-less `$rd = JALR rd` is long-form uncond when
    // dest recovers and a barrier when it does not.
    if (Top.isBundle()) {
      bool DestlessCfgJalr = false;
      MachineBasicBlock *SiblingUncond = nullptr;
      MachineInstr *RealCond = nullptr;
      unsigned CondCount = 0;
      for (MachineInstr *C : haydn::bundle::members(Top)) {
        if (isHaydnIndirectJALR(haydnLogicalOpcode(C->getOpcode()))) {
          if (isHaydnReturningJalrCall(*C))
            continue;
          if (MachineBasicBlock *JDest = getBranchDestBlock(*C)) {
            if (!SiblingUncond)
              SiblingUncond = JDest;
            continue;
          }
          DestlessCfgJalr = true;
          break;
        }
        if (MachineBasicBlock *UDest = uncondBranchDest(*C, MBB)) {
          if (!SiblingUncond)
            SiblingUncond = UDest;
          continue;
        }
        unsigned LO = haydnLogicalOpcode(C->getOpcode());
        if (isHaydnCondBranch1Reg(LO) || isHaydnCondBranch2Reg(LO)) {
          ++CondCount;
          if (!RealCond)
            RealCond = C;
        }
      }
      if (DestlessCfgJalr) {
        // Same as a dest-less last JALR: unanalyzable as the terminator,
        // but do not drop an already-parsed trailing cond/uncond from a
        // later cycle. Sibling uncond in THIS cycle does not license a
        // dest-less CFG JALR (barrier).
        if (!Cond.empty() || UncondTarget)
          break;
        LLVM_DEBUG(dbgs() << "analyzeBranch destless-bundled-jalr "
                          << MBB.getParent()->getName() << " bb."
                          << MBB.getNumber() << '\n');
        return true;
      }
      // D1.104: two cond children in one cycle cannot be TBB+FBB. Taking
      // the first dropped the second edge (same class as dest-less JALR).
      if (CondCount > 1) {
        if (!Cond.empty() || UncondTarget)
          break;
        LLVM_DEBUG(dbgs() << "analyzeBranch multi-cond-bundle "
                          << MBB.getParent()->getName() << " bb."
                          << MBB.getNumber() << '\n');
        return true;
      }
      if (SiblingUncond && !UncondTarget)
        UncondTarget = SiblingUncond;
      // unwrap returns the first isConditionalBranch, including a BEQZ_W R0
      // uncond clone listed first. Keep the real cond as CF so the sibling
      // uncond stays FBB (va-arg-22).
      if (RealCond)
        CFMI = RealCond;
    }

    MachineInstr &CF = *CFMI;
    if (haydnLogicalOpcode(CF.getOpcode()) == Haydn::NOP)
      continue;

    // RET / IRET / JAL_TCO / JALR_TCO: unanalyzable. Catalog JALR is not
    // isReturn; dest recovery below owns those. JALR_TCO stays off
    // isHaydnIndirectJALR so dest recovery cannot steal a coincidental
    // LUI/ADDI of the fnptr. Do not abort a dest-recovered CFG JALR
    // (cond+long two-way) or drop an already-parsed trailing branch.
    if (CF.isReturn(MachineInstr::IgnoreBundle) &&
        !isHaydnIndirectJALR(haydnLogicalOpcode(CF.getOpcode()))) {
      if (!Cond.empty() || UncondTarget)
        break;
      return true;
    }

    // Generic opcodes (pre-selection) are not analyzable.
    if (isPreISelGenericOpcode(CF.getOpcode()))
      return true;

    unsigned Opc = haydnLogicalOpcode(CF.getOpcode());

    // Unconditional branches. JAL_W is not in this set.
    if (MachineBasicBlock *Target = uncondBranchDest(CF, MBB)) {
      // Remember this unconditional branch target. If we later find a
      // conditional branch, this becomes FBB. Otherwise it's TBB.
      UncondTarget = Target;
      continue; // Keep scanning backwards for a conditional branch
    }

    // JALR / JALR_W — GR2.7 in-block long form (LUI+ADDI+near-cond+JALR).
    // Treat a trailing JALR as the unconditional dest so a PRECEDING near
    // cond stays analyzable (BranchRelaxation::fixupConditionalBranch
    // asserts "branches to be relaxed must be analyzable"). Returning
    // unanalyzable here made every cond+JALR tail opaque; a leftover far
    // cond in the same function then aborted (picojpeg / bkfir32x16).
    // insertBranch still cannot shrink the JALR: removeBranch stops at
    // isHaydnIndirectJALR and never erases it.
    //
    // RISC-V (RISCVInstrInfo.cpp:1325-1327) and AIE
    // (AIEBaseInstrInfo.cpp:186-188) treat every indirect as unanalyzable.
    // Haydn overlay: recover LUI/ADDI %bb dest (getBranchDestBlock) so
    // cond+JALR stays two-way and LateConvergence BR retargets the pair
    // instead of insertIndirectBranch (post-stamp CFG wall). Dest-less
    // CFG JALR/RET/computed-goto is a barrier: never continue as
    // analyzable fall-through (MBP updateTerminator PreviousLayoutSuccessor;
    // MachineVerifier: MBB exits via conditional branch/fall-through but
    // ends with a barrier). JALR_CALL is a returning call (same as JAL
    // libcall): do not abort the walk when dest recovery misses — va-arg-24
    // @verify has printf JALR_CALL then a far cond in the same MBB.
    if (isHaydnIndirectJALR(Opc)) {
      // JALR_CALL before dest recovery: a printf fnptr whose rs was
      // materialized with LUI/ADDI %bb is still a returning call.
      if (isHaydnReturningJalrCall(CF)) {
        if (!Cond.empty() || UncondTarget)
          break;
        continue;
      }
      if (MachineBasicBlock *JDest = getBranchDestBlock(CF)) {
        // Keep scanning for a preceding cond. Breaking because
        // UncondTarget is already set (this JALR, coissued scan) drops
        // the cond and leaves its successor unexplained.
        if (!UncondTarget)
          UncondTarget = JDest;
        continue;
      }
      // Dest-less CFG JALR (RET `$r0 = JALR $r15` / computed-goto
      // `$r0 = JALR rs` / long-form whose LUI/ADDI %bb was not found).
      // As the terminator: unanalyzable. After an already-parsed trailing
      // cond: stop and keep that analysis — same as JAL libcall.
      if (!Cond.empty() || UncondTarget)
        break;
      LLVM_DEBUG(dbgs() << "analyzeBranch destless-cfg-jalr "
                        << MBB.getParent()->getName() << " bb."
                        << MBB.getNumber() << '\n');
      return true;
    }

    // BR_JT — indirect jump table branch, not analyzable as a terminator.
    if (Opc == Haydn::BR_JT) {
      if (!Cond.empty() || UncondTarget)
        break;
      return true;
    }

    // JAL / JAL_W is CallSImm20 / libcall, not an analyzable uncond
    // terminator (treating JAL_W + %bb as uncond does not stop post-stamp
    // CFG splits; RISC-V :1325-1327 / AIE :186-188 leave non-branch last
    // unanalyzable). Mid-block after a parsed branch → stop and keep
    // analysis (yarpgen `JAL_W &__divsi3; BNE_W`). As sole non-terminator
    // call: continue so empty Cond is analyzable fallthrough (20000815-1).
    // True terminators stay unanalyzable.
    if (Opc == Haydn::JAL || Opc == Haydn::JAL_W || Opc == Haydn::JAL_TCO ||
        Opc == Haydn::JALR_TCO) {
      if (!Cond.empty() || UncondTarget)
        break;
      if (!CF.isTerminator(MachineInstr::IgnoreBundle))
        continue;
      return true;
    }

    // Conditional branches before the isBarrier early-out: generated
    // Format E members may carry isBarrier on a cond opcode, and BR still
    // has to rewrite the site. Logical opcode after member→logical inverse.
    if (isHaydnCondBranch1Reg(Opc)) {
      if (Cond.empty()) {
        if (CF.getNumOperands() < 2 || !CF.getOperand(1).isMBB())
          return true;
        MachineBasicBlock *TargetBB = CF.getOperand(1).getMBB();
        Cond.push_back(MachineOperand::CreateImm(Opc));
        Cond.push_back(CF.getOperand(0));
        TBB = TargetBB;
        // If there was a preceding unconditional branch, it's FBB
        if (UncondTarget) {
          FBB = UncondTarget;
          return false;
        }
        // No unconditional — may fall through
      } else {
        return true; // Second conditional — can't analyze
      }
    }
    // Conditional branches (2 registers)
    else if (isHaydnCondBranch2Reg(Opc)) {
      if (Cond.empty()) {
        if (CF.getNumOperands() < 3 || !CF.getOperand(2).isMBB())
          return true;
        MachineBasicBlock *TargetBB = CF.getOperand(2).getMBB();
        Cond.push_back(MachineOperand::CreateImm(Opc));
        Cond.push_back(CF.getOperand(0));
        Cond.push_back(CF.getOperand(1));
        TBB = TargetBB;
        if (UncondTarget) {
          FBB = UncondTarget;
          return false;
        }
      } else {
        return true; // Second conditional — can't analyze
      }
    }
    // Hardware-loop terminators (ZOL + JNZD)
    // PseudoLoopEnd: single MBB operand (the loop body / self-back-edge).
    // Cond = [Imm(PseudoLoopEnd)]. No register operand needed — the hardware
    // loop counter is implicit. Mirrors AIE's parseCondBranch
    // (AIEBaseInstrInfo.cpp:112-130).
    else if (Opc == Haydn::PseudoLoopEnd) {
      if (Cond.empty()) {
        if (CF.getNumOperands() < 1 || !CF.getOperand(0).isMBB())
          return true;
        Cond.push_back(MachineOperand::CreateImm(Opc));
        TBB = CF.getOperand(0).getMBB();
        if (UncondTarget) {
          FBB = UncondTarget;
          return false;
        }
      } else {
        return true;
      }
    }
    // LoopJNZ: register counter + MBB target (JNZD model).
    // Cond = [Imm(LoopJNZ), <counter reg>].
    else if (Opc == Haydn::LoopJNZ) {
      if (Cond.empty()) {
        if (CF.getNumOperands() < 2 || !CF.getOperand(1).isMBB())
          return true;
        Cond.push_back(MachineOperand::CreateImm(Opc));
        Cond.push_back(CF.getOperand(0)); // counter reg
        TBB = CF.getOperand(1).getMBB();
        if (UncondTarget) {
          FBB = UncondTarget;
          return false;
        }
      } else {
        return true;
      }
    } else {
      // Other barriers that are not analyzable as terminators. Mid-block
      // after an already-parsed branch sequence: stop scanning, keep the
      // analysis. Evaluated after cond/hwloop so a generated member that
      // carries isBarrier is still rewritten by BranchRelaxation.
      if (CF.isBarrier(MachineInstr::IgnoreBundle)) {
        if (!Cond.empty() || UncondTarget)
          break;
        LLVM_DEBUG(dbgs() << "analyzeBranch barrier " << MBB.getParent()->getName()
                          << " bb." << MBB.getNumber() << " opc="
                          << CF.getOpcode() << '\n');
        return true;
      }
      break;
    }
  }

  // If we only found an unconditional branch (no conditional), set TBB.
  if (Cond.empty() && UncondTarget)
    TBB = UncondTarget;

  return false;
}

//===----------------------------------------------------------------------===//
// SSA EarlyIfConversion: canInsertSelect / insertSelect
//===----------------------------------------------------------------------===//
//
// Generic EarlyIfConverter (llvm/lib/CodeGen/EarlyIfConversion.cpp) rewrites
// SSA triangles/diamonds by speculating side blocks and inserting a select for
// each PHI at the join. Cond comes from analyzeBranch:
// Cond = [ Imm(BEQZ_W|BNEZ_W|...), CondReg ]
//
// Haydn MOVT32/MOVF32 test only bit0 of the condition GPR (same as G_SELECT
// isel). We therefore accept only single-register zero-tests (BEQZ/BNEZ)
// which are the form produced after SEQ32/icmp. Two-register BEQ/BNE are
// refused — EarlyIfConv leaves those as branches (or they can be booleanized
// earlier).
//
// Semantics (identical to HaydnInstructionSelector G_SELECT s32):
// Dst = COPY False
// Dst = MOVT32 Dst(tied), True, CondReg / if branch-taken means Cond!=0
// Dst = MOVF32 Dst(tied), True, CondReg / if branch-taken means Cond==0
//
// CFG is owned by EarlyIfConverter (splice, transferSuccessorsAndUpdatePHIs
// updateTerminator) — never by hand-rolled post-RA successor edits.

bool HaydnInstrInfo::canInsertSelect(const MachineBasicBlock &MBB,
                                     ArrayRef<MachineOperand> Cond,
                                     Register DstReg, Register TrueReg,
                                     Register FalseReg, int &CondCycles,
                                     int &TrueCycles,
                                     int &FalseCycles) const {
  // Cond from analyzeBranch: [Imm(opc), Reg] for BEQZ_W / BNEZ_W.
  if (Cond.size() != 2 || !Cond[0].isImm() || !Cond[1].isReg())
    return false;

  unsigned Opc = haydnLogicalOpcode(Cond[0].getImm());
  if (Opc != Haydn::BEQZ_W && Opc != Haydn::BNEZ_W &&
      Opc != Haydn::BEQZ && Opc != Haydn::BNEZ)
    return false;

  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC = RegInfo.getCommonSubClass(
      MRI.getRegClass(TrueReg), MRI.getRegClass(FalseReg));
  if (!RC || !RegInfo.getCommonSubClass(RC, MRI.getRegClass(DstReg)))
    return false;
  // Scalar GPR32 only — MOVT32/MOVF32 live in GPR32. DR64 / vector selects
  // stay as branches or use other isel paths.
  if (!Haydn::GPR32RegClass.hasSubClassEq(RC))
    return false;

  // Rough latencies for EarlyIfConv heuristics (ALU cmov = 1 cycle).
  CondCycles = 1;
  TrueCycles = FalseCycles = 1;
  return true;
}

void HaydnInstrInfo::insertSelect(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I,
                                  const DebugLoc &DL, Register DstReg,
                                  ArrayRef<MachineOperand> Cond,
                                  Register TrueReg,
                                  Register FalseReg) const {
  assert(Cond.size() == 2 && Cond[0].isImm() && Cond[1].isReg() &&
         "insertSelect: Cond must be [Imm(BEQZ/BNEZ), Reg]");

  unsigned Opc = haydnLogicalOpcode(Cond[0].getImm());
  Register CondReg = Cond[1].getReg();

  // Branch-taken means "Cond is true" for EarlyIfConv's TrueReg/FalseReg.
  // BNEZ: taken when CondReg != 0 → MOVT (bit0==1 takes True)
  // BEQZ: taken when CondReg == 0 → MOVF (bit0==0 takes True)
  unsigned MovOpc;
  switch (Opc) {
  case Haydn::BNEZ_W:
  case Haydn::BNEZ:
    MovOpc = Haydn::MOVT32;
    break;
  case Haydn::BEQZ_W:
  case Haydn::BEQZ:
    MovOpc = Haydn::MOVF32;
    break;
  default:
    llvm_unreachable("canInsertSelect should have rejected this Cond");
  }

  // Single SSA def: %Dst = MOVT/MOVF %False(tied), %True, %Cond
  // Same shape as GISel G_SELECT s32. Do NOT emit
  // %Dst = COPY False; %Dst = MOVT %Dst,...
  // that is two defs of %Dst and trips getVRegDef in MachineCSE.
  // The $rd = $rd_src constraint forces RA to coalesce Dst with False.
  BuildMI(MBB, I, DL, get(MovOpc), DstReg)
      .addReg(FalseReg) // tied $rd_src (False fallthrough)
      .addReg(TrueReg)  // $rs1
      .addReg(CondReg)  // $rs2 bit0
      ;
}

unsigned HaydnInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *TBB,
                                     MachineBasicBlock *FBB,
                                     ArrayRef<MachineOperand> Cond,
                                     const DebugLoc &DL,
                                     int *BytesAdded) const {
  assert(TBB && "insertBranch must not be called with a null TBB");

  if (BytesAdded)
    *BytesAdded = 0;

  // Emit bare final one-cycle logical/real MIs (AIE peer; no callback-local
  // pack). Size is one product Full parcel via getInstSizeInBytes. Called
  // throughout the pipeline (MBP, BranchFolder, PreEmit BR) — must not form
  // BUNDLE roots before post-RA pack / late Finalize. Late Finalize wraps
  // residual bare reals after the fixed BR→Fixup→BR multipass.
  //
  // Short forms only (B / cond / hwloop latch). Long-form LUI+ADDI32_W+JALR_W
  // is insertIndirectBranch; insertBranch must not emit JALR (branches only
  // promote). CFG successors/probabilities stay with the caller
  // (TargetInstrInfo.h:780-781; AIEBaseInstrInfo.cpp:271-307).
  //
  // GR2.7 phase law: pre-stamp (before the first Finalize run — MBP,
  // BranchFolder, the pre-S1 normalization BR, -run-pass probes) bare
  // emission is legal, S1 commits later. Post-stamp, a REAL conditional
  // branch emission self-commits as a committed singleton packet at
  // emission time (bake + finalizeExactLateSingleton — the same authority
  // removeBranch's survivor re-stamp uses), so no bare real-encode MI is
  // reachable between a postcommit BR/Fixup invocation and the next
  // Finalize. Representation shells (B stays B through MIR; encoder peels
  // to BEQZ rs=R0) and zero-size hwloop metas (PseudoLoopEnd/LoopJNZ) are
  // not encode cycles and stay bare by the isUncommittedBareEncodeEscape law.
  const DebugLoc UseDL = haydnInheritBranchDebugLoc(MBB, DL);

  MachineFunction *MF = MBB.getParent();
  HaydnMachineFunctionInfo *FuncInfo =
      MF ? MF->getInfo<HaydnMachineFunctionInfo>() : nullptr;
  const bool PostCommit = FuncInfo && FuncInfo->hasPostCommitBlockBudget();

  // GR2.7 S2: if a LUI+ADDI+JALR tail remains (removeBranch skips it
  // post-stamp; pre-stamp it is erased), short cond/B must be inserted
  // BEFORE that tail. Inserting at end() placed cond+B after JALR
  // (dead), left the original far BEQZ in place, and BranchRelaxation
  // invert-swapped forever (matmult-int).
  auto longFormInsertPt = [&]() -> MachineBasicBlock::iterator {
    // Insert the new cond immediately before the retained CFG JALR so the
    // addr pair stays non-terminators (LUI/ADDI, cond, JALR). Walking back
    // through the pair placed the cond first and left non-terminators after
    // the first terminator.
    MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
    if (I == MBB.end() || !trailingCfgJalr(MBB))
      return MBB.end();
    while (I != MBB.begin()) {
      MachineBasicBlock::iterator P = std::prev(I);
      if (P->isDebugInstr() || P->isCFIInstruction()) {
        I = P;
        continue;
      }
      break;
    }
    return I;
  };
  const MachineBasicBlock::iterator Ins = longFormInsertPt();
  auto retargetLongFormUncond = [&](MachineBasicBlock *Dest) -> bool {
    // Post-stamp invert-swap: keep the committed LUI+ADDI+JALR and rewrite
    // the complete typed pair only (D1.117/D1.153 ordered reaching-def).
    // Require Dest equal a selected analyzed edge (TBB or FBB): invert-swap
    // has OldDest==TBB; identity has OldDest==FBB. Incomplete/incoherent/
    // swapped-order pair is a named refuse — never rewrite one half
    // (D1.124). Named fatal on stale dest — do not emit a dead short B
    // beside the retained JALR (D1.76).
    if (!PostCommit || !Dest)
      return false;
    const MachineInstr *Jalr = trailingCfgJalr(MBB);
    if (!Jalr)
      return false;

    HaydnJalrAddrMaterializeChain Chain = getJalrAddrMaterializeChain(*Jalr);
    if (!Chain.Complete || Chain.Incoherent)
      report_fatal_error(
          Twine("Haydn insertBranch: incomplete-or-incoherent LUI+ADDI "
                "chain in ") +
              MBB.getParent()->getName() + " BB#" + Twine(MBB.getNumber()),
          /*GenCrashDiag=*/false);
    if (!Chain.Dest || (Chain.Dest != TBB && Chain.Dest != Dest))
      report_fatal_error(
          Twine("Haydn insertBranch: stale-old-destination: retained "
                "long-form JALR dest is not a selected analyzed edge in ") +
              MBB.getParent()->getName() + " BB#" + Twine(MBB.getNumber()),
          /*GenCrashDiag=*/false);
    auto rewriteDest = [&](MachineInstr *AMI) {
      if (!AMI)
        return;
      for (MachineOperand &MO : AMI->operands())
        if (MO.isMBB())
          MO.setMBB(Dest);
    };
    rewriteDest(Chain.Lui);
    rewriteDest(Chain.Addi);
    return true;
  };

  // Self-commit one real conditional-branch emission. Same
  // bake+finalizeExactLateSingleton authority as removeBranch survivors.
  // Both callers pass a freshly BuildMI-inserted MI (parent always set),
  // so the only gate is the post-stamp state.
  auto selfCommitCondBranch = [&](MachineInstr &MI) {
    if (!PostCommit)
      return;
    unsigned Member = haydn::bundle::lateProductMemberOpcode(MI.getOpcode());
    if (Member != MI.getOpcode())
      bakeFormatEMemberDesc(MI, Member, *this);
    haydn::bundle::finalizeExactLateSingleton(MI);
  };

  auto insertUncond = [&](MachineBasicBlock *Dest) -> MachineInstr & {
    // Unconditional B is isBarrier=1 (HaydnPseudos.td). Catalog BEQZ_W is
    // isConditionalBranch. B stays B through MIR; the encoder peels to
    // BEQZ rs=R0.
    return *BuildMI(MBB, Ins, UseDL, get(Haydn::B)).addMBB(Dest);
  };

  if (Cond.empty()) {
    MachineInstr &MI = insertUncond(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    return 1;
  }

  // Conditional branch
  unsigned Opc = haydnLogicalOpcode(Cond[0].getImm());
  assert(!isHaydnIndirectJALR(Opc) &&
         "insertBranch emits short B/cond only; JALR is insertIndirectBranch");

  // Hardware-loop terminators. PseudoLoopEnd has no register operand
  // (Cond = [Imm] only); LoopJNZ has one register (Cond = [Imm, reg]).
  // Both use addMBB(TBB) for the target. When FBB is non-null (two-way)
  // append an unconditional B to FBB after the conditional.
  // Meta zero-byte markers stay bare (getInstSizeInBytes → 0).
  if (Opc == Haydn::PseudoLoopEnd) {
    MachineInstr &MI =
        *BuildMI(MBB, Ins, UseDL, get(Opc)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    if (FBB) {
      MachineInstr &BMI = insertUncond(FBB);
      if (BytesAdded)
        *BytesAdded += getInstSizeInBytes(BMI);
      return 2;
    }
    return 1;
  }
  if (Opc == Haydn::LoopJNZ) {
    MachineInstr &MI = *BuildMI(MBB, Ins, UseDL, get(Opc))
                            .addReg(Cond[1].getReg())
                            .addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    if (FBB) {
      MachineInstr &BMI = insertUncond(FBB);
      if (BytesAdded)
        *BytesAdded += getInstSizeInBytes(BMI);
      return 2;
    }
    return 1;
  }

  if (FBB == nullptr) {
    // One-way conditional: if Cond, goto TBB; else fall through.
    MachineInstrBuilder MIB = BuildMI(MBB, Ins, UseDL, get(Opc));
    if (isHaydnCondBranch1Reg(Opc)) {
      MIB.addReg(Cond[1].getReg());
    } else {
      MIB.addReg(Cond[1].getReg()).addReg(Cond[2].getReg());
    }
    MIB.addMBB(TBB);
    // Charge BEFORE self-commit: the singleton wrap turns the MI into a
    // bundle child and getInstSizeInBytes returns 0 for children (the root
    // owns the charge). Both forms are exactly one product parcel, so the
    // pre-wrap bare charge (D1.66) restores the insertBranch BytesAdded =
    // removeBranch lateLayoutBytes parity this function's erase side relies
    // on; generic BranchRelaxation was reading a -12-per-cycle drift.
    const unsigned Bytes = getInstSizeInBytes(*MIB);
    // Post-stamp real-encode emission self-commits (committed singleton
    // BUNDLE root at emission time); pre-stamp stays bare.
    selfCommitCondBranch(*MIB);
    if (BytesAdded)
      *BytesAdded += Bytes;
    return 1;
  }

  // Two-way conditional: if Cond, goto TBB; else goto FBB.
  MachineInstrBuilder MIB = BuildMI(MBB, Ins, UseDL, get(Opc));
  if (isHaydnCondBranch1Reg(Opc)) {
    MIB.addReg(Cond[1].getReg());
  } else {
    MIB.addReg(Cond[1].getReg()).addReg(Cond[2].getReg());
  }
  MIB.addMBB(TBB);
  // Charge before self-commit for the same D1.66 parity reason.
  const unsigned Bytes = getInstSizeInBytes(*MIB);
  // Post-stamp real-encode emission self-commits.
  selfCommitCondBranch(*MIB);
  if (BytesAdded)
    *BytesAdded += Bytes;
  if (retargetLongFormUncond(FBB))
    return 1;
  MachineInstr &BMI = insertUncond(FBB);
  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(BMI);
  return 2;
}

unsigned HaydnInstrInfo::eraseSelectedBranch(MachineInstr &MI,
                                             int *BytesRemoved) const {
  MachineBasicBlock *MBB = MI.getParent();
  if (!MBB)
    return 0;

  MachineFunction *MF = MBB->getParent();
  HaydnMachineFunctionInfo *FuncInfo =
      MF ? MF->getInfo<HaydnMachineFunctionInfo>() : nullptr;
  const bool PostCommit = FuncInfo && FuncInfo->hasPostCommitBlockBudget();

  auto unbundleMI = [](MachineInstr *M) {
    if (!M)
      return;
    if (M->isBundledWithPred())
      M->unbundleFromPred();
    if (M->isBundledWithSucc())
      M->unbundleFromSucc();
  };

  auto restampKeep = [&](ArrayRef<MachineInstr *> Keep) {
    // Pre-stamp restamp: one singleton wrap, or one product-cycle commit.
    // Unbundle first (HaydnHWLoopDemote.cpp:860-890
    // recommitSurvivingCycleMembers). No singleton-split: a failed
    // multi-MI commit is fatal. Post-stamp never reaches here (same-row
    // NOP path above). D1.168 pins the neutralize path through
    // verifyCommittedBundle; this lambda is not that coverage.
    assert(!PostCommit &&
           "post-stamp eraseSelectedBranch uses same-row NOP, not restampKeep");
    SmallVector<MachineInstr *, 4> Live;
    Live.reserve(Keep.size());
    for (MachineInstr *S : Keep) {
      if (!S || !S->getParent())
        continue;
      unbundleMI(S);
      Live.push_back(S);
    }
    if (Live.size() == 1) {
      MachineInstr *S = Live[0];
      unsigned Member = haydn::bundle::lateProductMemberOpcode(S->getOpcode());
      if (Member != S->getOpcode())
        bakeFormatEMemberDesc(*S, Member, *this);
      haydn::bundle::finalizeExactLateSingleton(*S);
    } else if (Live.size() > 1) {
      if (!haydn::bundle::commitOneProductCycle(Live))
        report_fatal_error(
            Twine("Haydn eraseSelectedBranch: singleton-split fallback "
                  "refused in ") +
                MF->getName() + " BB#" + Twine(MBB->getNumber()),
            /*GenCrashDiag=*/false);
    }
  };

  MachineInstr *Root = nullptr;
  SmallVector<MachineInstr *, 4> Selected;
  if (MI.isBundle()) {
    Root = &MI;
    for (MachineInstr *Child : haydn::bundle::members(MI)) {
      if (isControlFlowChild(*Child) &&
          Child->isBranch(MachineInstr::IgnoreBundle))
        Selected.push_back(Child);
    }
  } else if (MI.isBundledWithPred()) {
    // Bundled member: erase only this branch, keep coissued siblings and
    // any other control children in the same cycle.
    if (!isControlFlowChild(MI) ||
        !MI.isBranch(MachineInstr::IgnoreBundle))
      return 0;
    Root = haydn::bundle::bundleRootOf(MI);
    if (!Root)
      return 0;
    Selected.push_back(&MI);
  } else {
    // Bare branch (pre-finalize / pre-pack): one product parcel.
    if (!MI.isBranch(MachineInstr::IgnoreBundle))
      return 0;
    if (BytesRemoved)
      *BytesRemoved += static_cast<int>(haydn::bundle::lateLayoutBytes(MI));
    MI.eraseFromParent();
    return 1;
  }

  if (!Root || Selected.empty())
    return 0;

  SmallVector<MachineInstr *, 4> Keep;
  for (MachineInstr *Child : haydn::bundle::members(*Root)) {
    if (llvm::is_contained(Selected, Child))
      continue;
    if (isTerminatorCyclePadding(*Child))
      continue;
    Keep.push_back(Child);
  }

  if (Keep.empty()) {
    // Solo branch cycle (branch ± idle padding): charge committed
    // EncodedBytes on the root and erase the whole cycle atomically.
    // MBB::erase(iterator) deletes header + children via the bundle range.
    // lateLayoutBytes matches insertBranch BytesAdded oracle.
    if (BytesRemoved)
      *BytesRemoved += static_cast<int>(haydn::bundle::lateLayoutBytes(*Root));
    unsigned N = Selected.size();
    MBB->erase(Root);
    return N;
  }

  // Coissued BUNDLE{branch, real…}. Post-stamp: neutralize the vacated
  // child with the generated same-row NOP and keep this root (pipeline.md
  // preserve-or-extend; HexagonConstPropagation.cpp:2508-2512). Pre-stamp:
  // strip the selected children and restamp survivors as one product
  // cycle or one singleton wrap (no singleton-split).
  if (PostCommit) {
    for (MachineInstr *Br : Selected) {
      if (!Br || !Br->getParent())
        continue;
      neutralizeSameRowNop(*Br, *this);
    }
    haydn::bundle::dropBundleImplicitRegsAbsentFromMembers(*Root);
    return Selected.size();
  }

  for (MachineInstr *K : Keep)
    unbundleMI(K);
  for (MachineInstr *Br : Selected) {
    unbundleMI(Br);
    Br->eraseFromParent();
  }
  if (Root->getParent() && Root->isBundle()) {
    MachineBasicBlock::instr_iterator Next = std::next(Root->getIterator());
    if (Next == MBB->instr_end() || !Next->isBundledWithPred())
      Root->eraseFromParent();
  }
  // D1.135: LongBranchNormalize must prove Keep defs disjoint from retained
  // control incoming reads before splicing these restamped MIs before
  // firstControlMI. Overlap refuses the rewrite (skipping splice is not a
  // verifier-legal tail).
  restampKeep(Keep);
  // Surviving cycle still occupies one product parcel — no BytesRemoved.
  return Selected.size();
}

unsigned HaydnInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  if (BytesRemoved)
    *BytesRemoved = 0;

  MachineFunction *MF = MBB.getParent();
  HaydnMachineFunctionInfo *FuncInfo =
      MF ? MF->getInfo<HaydnMachineFunctionInfo>() : nullptr;
  const bool PostCommit = FuncInfo && FuncInfo->hasPostCommitBlockBudget();

  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;
  bool SawJALR = false;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr() || I->isCFIInstruction())
      continue;

    // Post-pack / late-commit: branch terminators are often BUNDLE roots.
    // isBranch() defaults to AnyInBundle so a committed branch cycle matches
    // on the root. MBB::iterator never yields interior children — use
    // standard bundle iterators (same as analyzeBranch unwrap).
    MachineInstr &Top = *I;
    MachineInstr &CF = unwrapBundleControlFlow(Top);
    unsigned CFOpc = haydnLogicalOpcode(CF.getOpcode());
    bool HasJALR = isHaydnIndirectJALR(CFOpc) && !isHaydnReturningJalrCall(CF);
    if (!HasJALR && Top.isBundle()) {
      for (const MachineInstr *Child : haydn::bundle::members(Top)) {
        if (isHaydnIndirectJALR(haydnLogicalOpcode(Child->getOpcode())) &&
            !isHaydnReturningJalrCall(*Child)) {
          HasJALR = true;
          break;
        }
      }
    }
    // GR2.7 S2: LUI+ADDI+JALR is the in-block long uncond. Pre-stamp, BR
    // invert-swap must be able to erase the whole tail (insertIndirect
    // re-emits long form if the new B is far). Post-stamp, keep the
    // committed JALR/addr pair and skip over them so the preceding near
    // cond can still be stripped. Breaking here left the far BEQZ in
    // place and insertBranch appended dead cond+B after JALR (matmult-int
    // invert-swap ping-pong).
    if (HasJALR) {
      SawJALR = true;
      if (PostCommit)
        continue;
      if (BytesRemoved)
        *BytesRemoved += static_cast<int>(haydn::bundle::lateLayoutBytes(Top));
      Count++;
      I = MBB.erase(I);
      continue;
    }
    if (SawJALR && isHaydnAddrMaterializeOpc(CFOpc)) {
      if (PostCommit)
        continue;
      if (BytesRemoved)
        *BytesRemoved += static_cast<int>(haydn::bundle::lateLayoutBytes(Top));
      Count++;
      I = MBB.erase(I);
      continue;
    }
    if (!CF.isBranch(MachineInstr::IgnoreBundle))
      break;

    // Per-cycle selected erase. The reverse walk still owns which trailing
    // branch cycles to visit (AIEBaseInstrInfo.cpp:237-266;
    // HexagonInstrInfo.cpp:605-625; RISCVInstrInfo.cpp:1361-1390) and still
    // stops at the first non-branch; JALR / LUI+ADDI handling above is
    // unchanged. Narrowing that walk would break BR invert-swap.
    unsigned N = eraseSelectedBranch(Top, BytesRemoved);
    if (!N)
      break;
    Count += N;
    I = MBB.end();
  }

  return Count;
}

bool HaydnInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  // Empty Cond is an unconditional site (Haydn::B / BEQZ_W R0). reverse
  // returns true = cannot reverse (TargetInstrInfo.h). Returning false
  // here told BranchRelaxation the invert succeeded, so
  // fixupConditionalBranch did std::next(MBB) and crashed on the function
  // sentinel when MBB was last.
  if (Cond.empty())
    return true;

  unsigned Opc = haydnLogicalOpcode(Cond[0].getImm());

  // Invert conditional branches. Emit inverted opcode as the matching logical.
  unsigned Inv = 0;
  switch (Opc) {
  case Haydn::BEQZ_W:  Inv = Haydn::BNEZ_W; break;
  case Haydn::BNEZ_W:  Inv = Haydn::BEQZ_W; break;
  case Haydn::BGEZ_W:  Inv = Haydn::BLTZ_W; break;
  case Haydn::BLTZ_W:  Inv = Haydn::BGEZ_W; break;
  case Haydn::BEQ_W:   Inv = Haydn::BNE_W;  break;
  case Haydn::BNE_W:   Inv = Haydn::BEQ_W;  break;
  case Haydn::BGE_W:   Inv = Haydn::BLT_W;  break;
  case Haydn::BLT_W:   Inv = Haydn::BGE_W;  break;
  case Haydn::BGEU_W:  Inv = Haydn::BLTU_W; break;
  case Haydn::BLTU_W:  Inv = Haydn::BGEU_W; break;
  case Haydn::BEQZ:    Inv = Haydn::BNEZ;   break;
  case Haydn::BNEZ:    Inv = Haydn::BEQZ;   break;
  case Haydn::BGEZ:    Inv = Haydn::BLTZ;   break;
  case Haydn::BLTZ:    Inv = Haydn::BGEZ;   break;
  case Haydn::BEQ:     Inv = Haydn::BNE;    break;
  case Haydn::BNE:     Inv = Haydn::BEQ;    break;
  case Haydn::BGE:     Inv = Haydn::BLT;    break;
  case Haydn::BLT:     Inv = Haydn::BGE;    break;
  case Haydn::BGEU:    Inv = Haydn::BLTU;   break;
  case Haydn::BLTU:    Inv = Haydn::BGEU;   break;
  default:
    return true; // Cannot reverse
  }

  Cond[0].setImm(Inv);
  return false; // Successfully reversed
}

/// True when expandPostRAPseudo packs LOADI64 / MOV_GPR_TO_DR64 through
/// DR64PackFI. determineCalleeSaves uses this same predicate so a
/// reservation-scan miss cannot silently CreateStackObject after PEI
/// (generic ExpandPostRAPseudos at TargetPassConfig.cpp:1192 is after PEI
/// and before the freeze snapshot). Probe gate is CSI-valid, not a named
/// probe function. RISC-V getMoveF64FrameIndex is ISel-time
/// (RISCVMachineFunctionInfo.h) and is declined as a post-RA pattern.
namespace llvm {
bool haydnInstrNeedsDR64PackSlot(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case Haydn::LOADI64:
    // Register-only: Hi==0 zero-extend, Hi==-1&&Lo<0 sign-extend.
    // Non-immediate (relocatable) LOADI64 is rare but conservatively packed.
    if (MI.getNumOperands() < 2 || !MI.getOperand(1).isImm())
      return true;
    {
      uint64_t V = static_cast<uint64_t>(MI.getOperand(1).getImm());
      int32_t Lo = static_cast<int32_t>(V & 0xFFFFFFFFu);
      int32_t Hi = static_cast<int32_t>((V >> 32) & 0xFFFFFFFFu);
      return Hi != 0 && !(Hi == -1 && Lo < 0);
    }
  case Haydn::MOV_GPR_TO_DR64:
    // R0-half packs take the stackless shift path; only two live GPRs pack.
    if (MI.getNumOperands() <= 2 || !MI.getOperand(1).isReg() ||
        !MI.getOperand(2).isReg())
      return false;
    return MI.getOperand(1).getReg() != Haydn::R0 &&
           MI.getOperand(2).getReg() != Haydn::R0;
  default:
    return false;
  }
}
} // namespace llvm

namespace {

/// DR64PackSlotRef — resolved addressing for the per-function DR64 pack slot
/// (DR64PackFI). PEI's eliminateFrameIndex has ALREADY run by the time
/// expandPostRAPseudo executes (PEI precedes ExpandPostRAPseudos in the
/// pipeline), so the slot is resolved here via getFrameIndexReferenceAt to a
/// stable FP/SP base plus any live call-frame SP delta. No dynamic SP adjust
/// is permitted for pack temporaries.
struct DR64PackSlotRef {
  Register FrameReg;
  int64_t Off = 0;      ///< Raw byte offset of the slot base from FrameReg.
  int64_t ElemLo = 0;   ///< ST32 word-element for the low half  (EA = base + elem<<2)
  int64_t ElemHi = 0;   ///< ST32 word-element for the high half
  int64_t ElemLd = 0;   ///< LD64 dword-element                 (EA = base + elem<<3)
  int FI = -1;
  /// True when the slot is addressed short-form (FrameReg + scaled simm6
  /// element). False when ANY of ElemLo/Hi/Ld would overflow isInt<6> — the
  /// caller must scavenge a base GPR via withDR64PackBase (FrameReg+Off,
  /// addressed at element 0/1/0). One closed rule, never moves SP.
  bool UseShortForm = true;
};

/// Unset FI after PEI (CSI valid) is reservation-scan drift. Generic
/// ExpandPostRAPseudos runs after PEI but before the earliest freeze snapshot,
/// so CreateStackObject here would grow the frame invisibly. Probe MIR that
/// bypasses PEI (`-run-pass=postrapseudos`) keeps CSI invalid and may still
/// CreateStackObject. RISC-V getMoveF64FrameIndex lazy create is ISel-time
/// (RISCVMachineFunctionInfo.h); declined as a post-RA pattern.
static int ensureDR64PackSlotFI(MachineFunction &MF, int FI, unsigned Size,
                                Align Alignment, const char *SlotName) {
  if (FI >= 0)
    return FI;
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (MFI.isCalleeSavedInfoValid())
    report_fatal_error(Twine("Haydn: lazy ") + SlotName +
                           " CreateStackObject after PEI",
                       /*GenCrashDiag=*/false);
  return MFI.CreateStackObject(Size, Alignment, /*SpillSlot=*/true);
}

/// Resolve the DR64 pack slot to a stable (FrameReg, element-index) triple
/// when the offset fits scaled simm6; otherwise mark for the scavenged-base
/// fallback. The canonical reservation is in determineCalleeSaves (it calls
/// haydnInstrNeedsDR64PackSlot — the same predicate as the expansion branch —
/// so every pack that reaches here is already reserved in the real pipeline).
/// If FI is still unset after PEI (CSI valid), fail closed. Probe MIR never
/// ran PEI, so CSI is invalid and the lazy CreateStackObject remains (default
/// offset 0). The slot is
/// Align(8) → Off and Off+4 are width-aligned. Never emits a dynamic SP
/// adjust — the fallback materialises a plain GPR base (FrameReg + Off) and
/// addresses the slot at element 0/1/0, which is always short-form-legal.
DR64PackSlotRef resolveDR64PackSlot(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    const HaydnFrameLowering &TFL) {
  MachineFunction &MF = *MBB.getParent();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  int FI = FuncInfo->getDR64PackFI();
  if (FI < 0) {
    FI = ensureDR64PackSlotFI(MF, FI, /*Size=*/8, Align(8), "DR64PackFI");
    FuncInfo->setDR64PackFI(FI);
  }
  DR64PackSlotRef R;
  R.FI = FI;
  R.Off = TFL.getFrameIndexReferenceAt(MF, FI, R.FrameReg, MBB, I).getFixed();
  // Closed rule: short-form iff every scaled element fits isInt<6>. Else the
  // caller opens a scavenged-base window (withDR64PackBase) — never a fatal,
  // never an SP motion.
  auto tryElem = [](int64_t ByteOff, unsigned Scale,
                    int64_t &ElemOut) -> bool {
    if ((ByteOff % static_cast<int64_t>(Scale)) != 0)
      report_fatal_error("Haydn: DR64 pack slot offset not width-aligned");
    int64_t Elem = ByteOff / static_cast<int64_t>(Scale);
    if (!isInt<6>(Elem))
      return false;
    ElemOut = Elem;
    return true;
  };
  bool Short =
      tryElem(R.Off, 4, R.ElemLo) && tryElem(R.Off + 4, 4, R.ElemHi) &&
      tryElem(R.Off, 8, R.ElemLd);
  R.UseShortForm = Short;
  if (!Short) {
    // Fallback addressing: base = FrameReg + Off; ST32 low at element 0
    // (byte 0), ST32 high at element 1 (byte +4 = one word), LD64 at element
    // 0 (byte 0). All three are trivially short-form-legal from the base.
    R.ElemLo = 0;
    R.ElemHi = 1;
    R.ElemLd = 0;
  }
  return R;
}

/// Spill/restore a physreg to/from the dedicated DR64PackBaseSpillFI. Used by
/// withDR64PackBase so the fallback base spill stays disjoint from any nested
/// withPostRAScratch spill (which uses PostRAScratchFI). Delegates to the
/// shared emitFrameRelativeMemOp so the same three-tier closed rule covers
/// every in-frame spill slot (tier 1 short-form element, tier 2 simm20
/// ADDI32_W+elem0, tier 3 MatInt real ops + ADD32 + elem0 for huge frames —
/// never the LOADI32 pseudo, which would not be re-expanded inside
/// expandPostRAPseudo). Never moves SP.
static void emitDR64PackBaseSpill(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I,
                                  const DebugLoc &DL,
                                  const TargetInstrInfo &TII,
                                  const HaydnSubtarget &ST, Register Reg,
                                  bool IsStore) {
  MachineFunction &MF = *MBB.getParent();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  int FI = FuncInfo->getDR64PackBaseSpillFI();
  if (FI < 0) {
    // Same CSI-valid gate as resolveDR64PackSlot. Probe MIR that bypasses
    // PEI keeps CSI invalid and may still lazily reserve (default offset 0).
    FI = ensureDR64PackSlotFI(MF, FI, /*Size=*/4, Align(4),
                              "DR64PackBaseSpillFI");
    FuncInfo->setDR64PackBaseSpillFI(FI);
  }
  const HaydnFrameLowering *TFL = ST.getFrameLowering();
  Register SpillFrameReg;
  int64_t Off =
      TFL->getFrameIndexReferenceAt(MF, FI, SpillFrameReg, MBB, I).getFixed();
  // D1.54: FrameIdx>=0 stamps FixedStack store MMO (beginSpill twin).
  emitFrameRelativeMemOp(MBB, I, DL, TII, Reg, SpillFrameReg, Off, IsStore,
                         /*StoreFlags=*/0, FI);
}

/// Acquire a stable base for the DR64 pack slot, then run Fn with it. The base
/// is `R.FrameReg` for the short-form path (byte-identical to the original
/// code), or a scavenged GPR = R.FrameReg + R.Off for the large-frame fallback
/// (element 0/1/0 on the base). NEVER moves SP — the scavenged base is a plain
/// GPR. The scavenger uses findPostRAScratchGPR and spills to the dedicated
/// DR64PackBaseSpillFI when needed (so a nested withPostRAScratch inside Fn
/// can still spill to PostRAScratchFI without conflict). R0 is never chosen
/// (PostRASoftZero::NeedsZeroBase semantics: a base borrow would clobber
/// soft-zero mid-Fn, and Fn may itself run a MatInt that reads R0 as zero).
template <typename FnT>
static void withDR64PackBase(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator I, const DebugLoc &DL,
                             const TargetInstrInfo &TII,
                             const HaydnSubtarget &ST, const DR64PackSlotRef &R,
                             FnT Fn, ArrayRef<Register> Exclude = {}) {
  if (R.UseShortForm) {
    Fn(R.FrameReg);
    return;
  }
  bool NeedsSpill = false;
  Register Base = findPostRAScratchGPR(MBB, I, /*PreferNotR12=*/true,
                                       NeedsSpill, Exclude);
  assert(Base != Haydn::R0 && "scavenger must not return soft-zero R0");
  // Soft-zero cleanliness at the insertion point: the base MatInt below
  // seeds Cur=R0, the spill-bracket tiers 2/3 borrow R0, and Fn itself
  // (emitConst32) chains MatInt from R0. The prologue zero is NOT
  // sufficient — any mid-function write can leave R0 dirty here (JALR
  // link-discard, an explicit ADDI32_W into R0). One local restore
  // emitter plus assert; never a function-wide epilogue xor; never
  // silently read a dirty seed.
  ensureSoftZeroR0Clean(MBB, I, DL, TII);
  assert(isSoftZeroR0Clean(MBB, I) &&
         "withDR64PackBase: soft-zero R0 must be clean before pack-base MatInt");
  if (NeedsSpill)
    emitDR64PackBaseSpill(MBB, I, DL, TII, ST, Base, /*IsStore=*/true);
  // Materialise Base = R.FrameReg + R.Off (full byte offset). Mirrors the
  // large-offset rebase in HaydnRegisterInfo::eliminateFrameIndex.
  if (isInt<20>(R.Off)) {
    BuildMI(MBB, I, DL, TII.get(Haydn::ADDI32_W), Base)
        .addReg(R.FrameReg)
        .addImm(R.Off);
  } else {
    // Large offset: emit real MatInt ops (never the LOADI32 pseudo — this
    // runs inside expandPostRAPseudo; the pseudo would survive and fatal
    // AsmPrinter's residual cycle-forming pseudo check). One mechanism —
    // HaydnMatInt::generate — same as expandPostRAPseudo's LOADI32 case and
    // the emitConst32 lambda below.
    HaydnMatInt::InstSeq Seq = HaydnMatInt::generate(R.Off);
    Register Cur = Haydn::R0;
    for (const HaydnMatInt::Inst &MatInst : Seq) {
      // LUI is dest+imm (logical matches Format E members).
      MachineInstrBuilder MIB =
          BuildMI(MBB, I, DL, TII.get(MatInst.Opc), Base);
      if (MatInst.Opc != Haydn::LUI)
        MIB.addReg(Cur);
      MIB.addImm(MatInst.Imm);
      Cur = Base;
    }
    BuildMI(MBB, I, DL, TII.get(Haydn::ADD32), Base)
        .addReg(R.FrameReg)
        .addReg(Base);
  }
  Fn(Base);
  if (NeedsSpill)
    emitDR64PackBaseSpill(MBB, I, DL, TII, ST, Base, /*IsStore=*/false);
}

/// Store one GPR32 half (low or high) of the DR64 pack slot.
void storeDR64PackHalf(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                       const DebugLoc &DL, const TargetInstrInfo &TII,
                       const DR64PackSlotRef &R, Register SrcReg, bool Kill,
                       bool IsHi) {
  MachineFunction &MF = *MBB.getParent();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, R.FI, IsHi ? 4 : 0),
      MachineMemOperand::MOStore, 4, Align(8));
  BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
      .addReg(SrcReg, getKillRegState(Kill))
      .addReg(R.FrameReg)
      .addImm(IsHi ? R.ElemHi : R.ElemLo)
      .addMemOperand(MMO);
}

/// Load the full DR64 pack from the pack slot.
void loadDR64Pack(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                  const DebugLoc &DL, const TargetInstrInfo &TII,
                  const DR64PackSlotRef &R, Register DstReg) {
  MachineFunction &MF = *MBB.getParent();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, R.FI), MachineMemOperand::MOLoad,
      8, Align(8));
  BuildMI(MBB, I, DL, TII.get(Haydn::LD64), DstReg)
      .addReg(R.FrameReg)
      .addImm(R.ElemLd)
      .addMemOperand(MMO);
}

} // namespace

void HaydnInstrInfo::preserveCircularBufferWritebackDefs(
    MachineFunction &MF) const {
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      switch (MI.getOpcode()) {
      case Haydn::D_LDW_CB_IMM:
      case Haydn::D_LDW_CB_REG:
      case Haydn::D_SDW_CB_IMM:
      case Haydn::D_SDW_CB_REG:
        break;
      default:
        continue;
      }
      Register Wb;
      for (const MachineOperand &MO : MI.explicit_operands()) {
        if (MO.isReg() && MO.isDef() && MO.getReg().isPhysical())
          Wb = MO.getReg();
      }
      if (!Wb)
        continue;
      bool HasImpDef = false;
      for (const MachineOperand &MO : MI.implicit_operands()) {
        if (MO.isReg() && MO.isDef() && MO.getReg() == Wb) {
          HasImpDef = true;
          break;
        }
      }
      if (HasImpDef)
        continue;
      MI.addOperand(MF, MachineOperand::CreateReg(Wb, /*isDef=*/true,
                                                  /*isImp=*/true));
    }
  }
}

static void rebuildAsJALR_W(MachineInstr &MI, const HaydnInstrInfo &TII,
                            Register Rd, Register Rs, int64_t Imm) {
  MachineFunction &MF = *MI.getParent()->getParent();
  SmallVector<MachineOperand, 4> LiveOuts;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isUse())
      continue;
    Register R = MO.getReg();
    if (!R || R == Rd || R == Rs)
      continue;
    LiveOuts.push_back(MO);
  }
  while (MI.getNumOperands())
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.setDesc(TII.get(Haydn::JALR_W));
  MI.addOperand(MF, MachineOperand::CreateReg(Rd, /*isDef=*/true));
  MI.addOperand(MF, MachineOperand::CreateReg(Rs, /*isDef=*/false));
  MI.addOperand(MF, MachineOperand::CreateImm(Imm));
  for (const MachineOperand &MO : LiveOuts)
    MI.addOperand(MF, MachineOperand::CreateReg(
                          MO.getReg(), /*isDef=*/false, /*isImp=*/true,
                          /*isKill=*/MO.isKill()));
}

MachineInstr *
HaydnInstrInfo::expandRepresentationPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  switch (MI.getOpcode()) {
  default:
    return nullptr;

  case Haydn::RET:
    rebuildAsJALR_W(MI, *this, Haydn::R0, Haydn::R15, 0);
    return &MI;

  case Haydn::BR_JT: {
    if (!(MI.getNumOperands() >= 1 && MI.getOperand(0).isReg()))
      report_fatal_error("Haydn: BR_JT has no address register",
                         /*GenCrashDiag=*/false);
    rebuildAsJALR_W(MI, *this, Haydn::R0, MI.getOperand(0).getReg(), 0);
    return &MI;
  }

  case Haydn::PseudoCALLIndirect: {
    Register Rd = MI.getOperand(0).getReg();
    Register Rs = MI.getOperand(1).getReg();
    MachineInstrBuilder MIB =
        BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(), get(Haydn::JALR_CALL))
            .addDef(Rd)
            .addReg(Rs)
            .addImm(0);
    MI.eraseFromParent();
    return MIB.getInstr();
  }
  }
  llvm_unreachable("expandRepresentationPseudo switch");
}

bool HaydnInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineBasicBlock::iterator MBBI = MI.getIterator();
  DebugLoc DL = MI.getDebugLoc();

  switch (MI.getOpcode()) {
  default:
    return false;

  case Haydn::RET: {
    // Rebuild JALR_W r0, r15, 0 so Finalize can setDesc the Format E
    // member (prints jalr). setDesc+append left RET implicits in
    // explicit slots. Do not BuildMI: JALR_W Defs include D0 and would
    // clobber the i64 return. RISCV expandPseudo_RET rebuilds JALR
    // x0, x1, 0 (RISCVInstrInfo.cpp).
    rebuildAsJALR_W(MI, *this, Haydn::R0, Haydn::R15, 0);
    return true;
  }

  case Haydn::B:
    // B must survive MachineBlockPlacement (ExpandPostRAPseudos runs
    // before addPreSched2 MBP). Encoder peels B to BEQZ rs=R0.
    return false;

  case Haydn::BR_JT:
    // JALR_W is isCall (caller-saved clobbers). Expanding a computed goto
    // to a call before pack would treat the jump as a call. Generic
    // ExpandPostRAPseudos runs before MBP (TargetPassConfig.cpp:1192);
    // HaydnExpandPseudos is after MBP but still before pack and rebuilds
    // JALR_W r0, addr, 0. ExpandPseudos still re-zeros successors of the
    // BR_JT pseudo.
    return false;

  case Haydn::LOADI32: {
    // LOADI32 $rd, $imm — post-RA expansion of rematerialisable constants.
    //
    // ISA LUI loads imm12 into bits[31:20] (<< 20), NOT a 16-bit
    // "upper half" (<< 16). The pre-ISA-43 split (Upper = Val>>16, LUI Upper
    // ADDI Lower) mis-materialised every value outside simm16 — e.g. 0xFFFF
    // became LUI 1; ADDI 0xFFFF → 0x0010FFFF on real hardware/ISS, which then
    // poisoned AND masks and (when remat/spilled) load bases → unaligned
    // MEMORY_FAULT. Always use HaydnMatInt (same as AsmPrinter / G_CONSTANT
    // isel). MBB operands: BranchRelaxation insertIndirectBranch now emits
 // exact-commit LUI+ADDI32_W; any residual non-imm LOADI32 is a
    // contract violation (VerifyBundles / AsmPrinter fail closed).
    //
    // slice Z: ZERO_GPR retired — MatInt(0) is ADDI32_W rd, R0, 0 (or
    // equivalent); soft-zero R0 remains available as the sequence source.
    if (!MI.getOperand(1).isImm())
      return false;

    Register DstReg = MI.getOperand(0).getReg();
    int64_t Imm = MI.getOperand(1).getImm();
    HaydnMatInt::InstSeq Seq = HaydnMatInt::generate(Imm);

    // MatInt seeds Cur=R0. Restore only on a proven dirty def. Unknown
    // fallthrough is not a second XOR (withDR64PackBase stays conservative).
    ensureSoftZeroR0IfKnownDirty(MBB, MBBI, DL, *this);

    // Post-RA: chain every MatInt step through the same phys dst (R0 → Dst → …).
    Register CurrentReg = Haydn::R0;
    for (const HaydnMatInt::Inst &MatInst : Seq) {
      switch (MatInst.Opc) {
      default: {
        // LUI is dest+imm (logical matches Format E members); the rest of
        // the sequence is (rd, rs, imm).
        MachineInstrBuilder MIB =
            BuildMI(MBB, MBBI, DL, get(MatInst.Opc), DstReg);
        if (MatInst.Opc != Haydn::LUI)
          MIB.addReg(CurrentReg);
        MIB.addImm(MatInst.Imm);
        break;
      }
      case Haydn::SLLI32:
        BuildMI(MBB, MBBI, DL, get(Haydn::SLLI32), DstReg)
            .addReg(CurrentReg)
            .addImm(MatInst.Imm);
        break;
      case Haydn::ORI32:
        BuildMI(MBB, MBBI, DL, get(Haydn::ORI32), DstReg)
            .addReg(CurrentReg)
            .addImm(MatInst.Imm);
        break;
      }
      CurrentReg = DstReg;
    }
    MI.eraseFromParent();
    return true;
  }

  case Haydn::LOADI64: {
    // LOADI64 $rd, $imm — materialise an i64 constant into a DR64 register.
    // Rematerialisable single-imm pseudo; G_CONSTANT (s64) selects this.
    //
    // Classification (one closed rule, two cases — NO dynamic SP adjust):
    //   * sign/zero-extendable from one GPR32 half  → REGISTER-ONLY
    //       - Hi == 0          : zero-extend Lo  (SEXT_GPR32_TO_DR64; <<32; >>32)
    //       - Hi == -1, Lo < 0 : sign-extend Lo  (SEXT_GPR32_TO_DR64)
    //     Covers every small i64 compare-constant (e.g. mac_mula64_all
    //     930/950/979/985). Same shift sequence as the MOV_GPR_TO_DR64
    //     R0-half path. No memory, no SP motion.
    //   * both halves nonzero (e.g. 0x5_0000_0036) → fixed DR64PackFI pack
    //     MatInt Lo/Hi into Scr, ST32 each half, LD64. Addressed through
    //     getFrameIndexReference (stable FP/SP base). No SP motion.
    //
    // The previous implementation ALWAYS spilled through a dynamic
    // SUBI32 $r13,8 / ST32 / ST32 / LD64 / ADDI32_W $r13,8 transient. That
    // shifted SP mid-function and desynchronised sibling SP-relative fixed
    // objects: the hoisted compare-constant 979 in mac_mula64_all was stored
    // at sp=BASE-16 but loaded at sp=BASE-8 → wrong value → guest exit 11.
    // Invariant: no pass may emit a dynamic SP adjustment for a temporary.
    //
    // MatInt chains from soft-zero R0 as the *source* of the first instr
    // (ADDI/LUI/ORI rd, R0, imm). NeedsZeroBase: never Scr=R0 (would destroy
    // the zero between lo and hi materialization). Scavenger if R0 dirty or
    // for the dest temp itself.
    Register DstReg = MI.getOperand(0).getReg(); // DR64
    int64_t Imm = MI.getOperand(1).getImm();
    uint64_t Val = static_cast<uint64_t>(Imm);
    int32_t Lo = static_cast<int32_t>(Val & 0xFFFFFFFFu);
    int32_t Hi = static_cast<int32_t>((Val >> 32) & 0xFFFFFFFFu);
    const bool HiIsZero = (Hi == 0);
    const HaydnSubtarget &ST =
        MBB.getParent()->getSubtarget<HaydnSubtarget>();
    // Pack vs register-only is haydnInstrNeedsDR64PackSlot — the same
    // predicate determineCalleeSaves uses to reserve DR64PackFI.
    assert(haydnInstrNeedsDR64PackSlot(MI) ==
               !(HiIsZero || (Hi == -1 && Lo < 0)) &&
           "LOADI64 pack predicate drifted from determineCalleeSaves");

    // emitConst32 seeds Cur=R0. Restore only on a proven dirty def so a
    // fallthrough from a clean predecessor does not grow a second XOR.
    // withDR64PackBase still uses the conservative borrow check.
    ensureSoftZeroR0IfKnownDirty(MBB, MBBI, DL, *this);

    auto emitConst32 = [&](int32_t V, Register Target) {
      assert(Target != Haydn::R0 &&
             "LOADI64 MatInt dest must not be soft-zero R0");
      HaydnMatInt::InstSeq Seq = HaydnMatInt::generate(V);
      Register Cur = Haydn::R0;
      for (size_t I = 0; I < Seq.size(); ++I) {
        // LUI is dest+imm (logical matches Format E members); the rest of
        // the sequence is (rd, rs, imm).
        MachineInstrBuilder MIB =
            BuildMI(MBB, MBBI, DL, get(Seq[I].Opc), Target);
        if (Seq[I].Opc != Haydn::LUI)
          MIB.addReg(Cur);
        MIB.addImm(Seq[I].Imm);
        Cur = Target;
      }
    };

    if (!haydnInstrNeedsDR64PackSlot(MI)) {
      // REGISTER-ONLY: no memory, no SP motion.
      withPostRAScratch(
          MBB, MBBI, DL, *this, ST, /*PreferNotR12=*/true,
          [&](Register Scr) {
            emitConst32(Lo, Scr);
            // SEXT_GPR32_TO_DR64 sign-extends Scr into Dst. For Hi==0 the
            // follow-up logical <<32; >>32 clears [63:32] → zero-extend.
            // For HiIsSignExtOfLo the sext alone is the full value.
            BuildMI(MBB, MBBI, DL, get(Haydn::SEXT_GPR32_TO_DR64), DstReg)
                .addReg(Scr, RegState::Kill);
            if (HiIsZero) {
              BuildMI(MBB, MBBI, DL, get(Haydn::SLLI64), DstReg)
                  .addReg(DstReg)
                  .addImm(32);
              BuildMI(MBB, MBBI, DL, get(Haydn::SRLI64), DstReg)
                  .addReg(DstReg)
                  .addImm(32);
            }
          },
          /*Exclude=*/{}, PostRASoftZero::NeedsZeroBase);
      MI.eraseFromParent();
      return true;
    }

    // GENERAL (both halves nonzero): pack Lo/Hi via the fixed DR64PackFI.
    // Materialise Lo into Scr, store; overwrite Scr with Hi (Lo dead after
    // the store), store; LD64. No SP motion. withDR64PackBase keeps the
    // short-form fast path byte-identical and only opens a scavenged-base
    // window (FrameReg+Off at element 0/1/0) when the slot offset overflows
    // scaled simm6 — never a fatal, never an SP motion.
    {
      DR64PackSlotRef R =
          resolveDR64PackSlot(MBB, MBBI, *ST.getFrameLowering());
      withDR64PackBase(
          MBB, MBBI, DL, *this, ST, R,
          [&](Register Base) {
            DR64PackSlotRef Bounded = R;
            Bounded.FrameReg = Base;
            withPostRAScratch(
                MBB, MBBI, DL, *this, ST, /*PreferNotR12=*/true,
                [&](Register Scr) {
                  emitConst32(Lo, Scr);
                  storeDR64PackHalf(MBB, MBBI, DL, *this, Bounded, Scr,
                                    /*Kill=*/false, /*IsHi=*/false);
                  emitConst32(Hi, Scr);
                  storeDR64PackHalf(MBB, MBBI, DL, *this, Bounded, Scr,
                                    /*Kill=*/true, /*IsHi=*/true);
                  loadDR64Pack(MBB, MBBI, DL, *this, Bounded, DstReg);
                },
                /*Exclude=*/{Base}, PostRASoftZero::NeedsZeroBase);
          });
    }

    MI.eraseFromParent();
    return true;
  }

  case Haydn::MOV_GPR_TO_DR64: {
    // MOV_GPR_TO_DR64 $rd, $rs_lo, $rs_hi
    // Pack two GPR32 values into one DR64: rd = (rs_hi << 32) | rs_lo.
    //
    // Why a special case when a half is R0 — not "hardwired zero":
    // Haydn R0 is soft-zero (HaydnRegisterInfo): prologue sets it to 0
    // reserved so regalloc never assigns it. Selectors emit R0 as the
    // zero half of a pack (e.g. MOV_GPR_TO_DR64 R0, srai for Q15 coef).
    // Silicon does NOT force R0==0; do not read R0 here for the zero half.
    //
    // Why only that special case (not general two-live-half):
    // Stackless pack of two nonzero GPRs needs a second DR temp to OR the
    // shifted halves; expandPostRAPseudo has no scavenger. With one half
    // known zero (operand is the soft-zero reg), shifts alone produce the
    // zero half and Dst is the only DR needed.
    //
    // fir_xcorr / firinterp: MOV_GPR_TO_DR64 R0, (srai hi,16) — the old SP
    // path (subi32/st32/ld64 bloat) is gone.
    //
    // General (both halves live GPRs) packs via the per-function fixed
    // DR64PackFI (stable FP/SP base, NO SP motion): ST32 lo; ST32 hi; LD64 rd.
    // The previous dynamic SUBI32 SP,8 / ... / ADDI32 SP,8 transient shifted
    // SP mid-function and corrupted sibling SP-relative fixed objects.
    Register DstReg = MI.getOperand(0).getReg();  // DR64
    Register SrcLo = MI.getOperand(1).getReg();   // GPR32 low half
    Register SrcHi = MI.getOperand(2).getReg();   // GPR32 high half
    bool LoKill = MI.getOperand(1).isKill();
    bool HiKill = MI.getOperand(2).isKill();

    // Stackless when a half is the soft-zero *operand* (no R0 read).
    // haydnInstrNeedsDR64PackSlot is the same predicate determineCalleeSaves
    // uses; a drift here is a reservation-scan miss after PEI.
    if (!haydnInstrNeedsDR64PackSlot(MI)) {
      if (SrcLo == Haydn::R0 && SrcHi == Haydn::R0) {
        // Zero DR64 without reading R0 (soft-zero may be stale after JALR→R0).
        BuildMI(MBB, MBBI, DL, get(Haydn::XOR64), DstReg)
            .addReg(DstReg, RegState::Undef)
            .addReg(DstReg, RegState::Undef);
        MI.eraseFromParent();
        return true;
      }
      if (SrcLo == Haydn::R0) {
        // rd = (uint64_t)rs_hi << 32 — hi in [63:32], zero in [31:0].
        // Zero low half comes from the shift, not from reading R0.
        BuildMI(MBB, MBBI, DL, get(Haydn::SEXT_GPR32_TO_DR64), DstReg)
            .addReg(SrcHi, getKillRegState(HiKill));
        BuildMI(MBB, MBBI, DL, get(Haydn::SLLI64), DstReg)
            .addReg(DstReg)
            .addImm(32);
        MI.eraseFromParent();
        return true;
      }
      if (SrcHi == Haydn::R0) {
        // rd = zero_extend(rs_lo) — lo in [31:0], zero in [63:32].
        // Zero high half from (<<32)>>32; do not read R0.
        BuildMI(MBB, MBBI, DL, get(Haydn::SEXT_GPR32_TO_DR64), DstReg)
            .addReg(SrcLo, getKillRegState(LoKill));
        BuildMI(MBB, MBBI, DL, get(Haydn::SLLI64), DstReg)
            .addReg(DstReg)
            .addImm(32);
        BuildMI(MBB, MBBI, DL, get(Haydn::SRLI64), DstReg)
            .addReg(DstReg)
            .addImm(32);
        MI.eraseFromParent();
        return true;
      }
      llvm_unreachable(
          "MOV_GPR_TO_DR64: pack predicate drifted from R0-half paths");
    }

    // General (both halves live GPRs): pack via the per-function fixed
    // DR64PackFI. No SP motion: the slot is addressed through
    // getFrameIndexReference (stable FP/SP base), or — when that offset
    // overflows scaled simm6 — through a scavenged-GPR base = FrameReg+Off
    // at element 0/1/0 (withDR64PackBase fallback; never a fatal, never an
    // SP motion). The previous SUBI32 $r13,8 / ST32 / ST32 / LD64 /
    // ADDI32_W $r13,8 transient shifted SP mid-function and corrupted
    // sibling SP-relative fixed objects (same invariant as LOADI64). When
    // SrcLo == SrcHi, only apply kill on the last use to avoid killing the
    // same physical register twice.
    const HaydnSubtarget &STGen =
        MBB.getParent()->getSubtarget<HaydnSubtarget>();
    DR64PackSlotRef R =
        resolveDR64PackSlot(MBB, MBBI, *STGen.getFrameLowering());
    withDR64PackBase(
        MBB, MBBI, DL, *this, STGen, R,
        [&](Register Base) {
          DR64PackSlotRef Bounded = R;
          Bounded.FrameReg = Base;
          if (SrcLo == SrcHi) {
            storeDR64PackHalf(MBB, MBBI, DL, *this, Bounded, SrcLo,
                              /*Kill=*/false, /*IsHi=*/false);
            storeDR64PackHalf(MBB, MBBI, DL, *this, Bounded, SrcHi,
                              /*Kill=*/(LoKill || HiKill), /*IsHi=*/true);
          } else {
            storeDR64PackHalf(MBB, MBBI, DL, *this, Bounded, SrcLo, LoKill,
                              /*IsHi=*/false);
            storeDR64PackHalf(MBB, MBBI, DL, *this, Bounded, SrcHi, HiKill,
                              /*IsHi=*/true);
          }
          loadDR64Pack(MBB, MBBI, DL, *this, Bounded, DstReg);
        },
        /*Exclude=*/{SrcLo, SrcHi});

    MI.eraseFromParent();
    return true;
  }

  case Haydn::MOV_DR64_TO_GPR: {
    // MOV_DR64_TO_GPR $rd_lo, $rd_hi, $rs
    // use native MOVE32_DR_L + MOVE32_DR_H (2 ops, no stack) instead
    // of the 5-op stack spill (SUBI/ST64/LD32/LD32/ADDI). Both are slot-0
    // ALU ops that can bundle with neighbors.
    Register DstLo = MI.getOperand(0).getReg();   // GPR32 low half
    Register DstHi = MI.getOperand(1).getReg();   // GPR32 high half
    Register SrcReg = MI.getOperand(2).getReg();   // DR64
    bool SrcKill = MI.getOperand(2).isKill();

    BuildMI(MBB, MBBI, DL, get(Haydn::MOVE32_DR_L), DstLo)
        .addReg(SrcReg, getKillRegState(false));
    BuildMI(MBB, MBBI, DL, get(Haydn::MOVE32_DR_H), DstHi)
        .addReg(SrcReg, getKillRegState(SrcKill));

    MI.eraseFromParent();
    return true;
  }
  }
}

bool HaydnInstrInfo::isHardwareLoopSetupOpcode(unsigned Opc) const {
  // Logical / wide catalog forms. Keep this the sole opcode list for
  // mutations + Fixup (HWLOOP-SU). Hexagon matches architectural LOOP
  // opcodes directly (HexagonFixupHwLoops.cpp isHardwareLoop). Generated
  // members resolve through the inverse (inside the classifier).
  // Tii family membership — the old peel-name fallback's only live member
  // (bare golden SET_HWLOOP_F2) is Expanded in the family enum, so the
  // fallback is subsumed and deleted (W64 QW2).
  return haydnClassifyHwloopSetupOpcode(Opc) !=
         HaydnHwloopSetupFamily::None;
}

bool HaydnInstrInfo::isHardwareLoopSetupInstr(const MachineInstr &MI) const {
  return isHardwareLoopSetupOpcode(MI.getOpcode());
}

bool HaydnInstrInfo::isHardwareLoopRegTripOpcode(unsigned Opc) const {
  // Reg-trip law — NOT family-identical (see
  // haydnClassifyHwloopSetupOpcode): it is Residual∩{SET_HWLOOP_REG} ∪
  // (Expanded∖{SET_HWLOOP_W}) ∪ {LoopStart}; no single family level
  // expresses it. Keep explicit. The peel-name fallback's only live member
  // (bare golden SET_HWLOOP_F2) is now an Expanded switch case (W64 QW2).
  switch (haydnLogicalOpcode(Opc)) {
  case Haydn::SET_HWLOOP_REG:
  case Haydn::SET_HWLOOP_F2:
  case Haydn::SET_HWLOOP_F2_W:
  case Haydn::SET_HWLOOP_REG_W:
  case Haydn::LoopStart:
    return true;
  default:
    // Dead fallback deleted (W64 QW2): SET_HWLOOP_REG / SET_HWLOOP_REG_*
    // members invert through haydnLogicalOpcode first, and `*_S*` peel is
    // refused, so a name-based second chance is unreachable.
    return false;
  }
}

bool HaydnInstrInfo::isHardwareLoopImmTripOpcode(unsigned Opc) const {
  // Imm-trip law — NOT family-identical (see
  // haydnClassifyHwloopSetupOpcode): exactly Residual∩{SET_HWLOOP} ∪
  // Expanded∩{SET_HWLOOP_W}; every other family member is reg-trip or F2.
  // Keep explicit.
  switch (haydnLogicalOpcode(Opc)) {
  case Haydn::SET_HWLOOP:
  case Haydn::SET_HWLOOP_W:
    return true;
  default:
    return false;
  }
}

bool HaydnInstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                         const MachineBasicBlock *MBB,
                                         const MachineFunction &MF) const {
  // Instructions that should not be packetized across:
  if (MI.isCall() || MI.isInlineAsm() || MI.isReturn() || MI.isBranch())
    return true;

  // SET / LoopStart and their remat producers are ordinary DAG SUs. Setup
  // distance is enforced by ZOLSetupExitLatency (ExitSU artificial edge =
  // SetupIssueDistance) + leaveRegion handleRegionConflicts (ExitReady +
  // inter-zone pads) + residual Fixup deficit pads — not by splitting the
  // scheduling region around SET. Format HR decides solo vs coissue. Data
  // edges keep remat Dest→SET ordered. Product coissue still refuses a
  // same-cycle producer of SET's trip/Off GPRs (snapshot no-forwarding —
  // the one shared body is haydnCycleMembersHaveHwloopTripConflict in
  // HaydnPortModel.h, numeric classifier over generated
  // logicalOpcodeOrSelf).

  // Every standard TargetOpcode::BUNDLE root is an atomic scheduling
  // boundary. HaydnHazardRecognizer returns NoHazard for isBundle() roots
  // without aggregating children (opcode-level isNoHazardMetaInstruction
  // deliberately excludes BUNDLE; MI.isBundle is a separate HR skip). Without
  // this fence MachineScheduler can place dependent / resource-conflicting
  // neighbors across membership — including mixed GPR32/DR64 Full rematch
  // groups (ADD32 + 2xADD64). Soft compact TRI hints remain metrics-gated
  // default OFF. Pre-RA SMS does not freeze multi-member BUNDLE roots;
  // post-RA packing owns product cycles. Integration:
  // format-bundle-through-ra.mir; unit pin HaydnHazardRecognizerTest bundle
  // fence.
  if (MI.isBundle())
    return true;

  // Frame-setup / frame-destroy instructions modify the stack pointer (R13):
  // the prologue SUBI32 $r13 and the epilogue ADDI32 $r13. They MUST be
  // scheduling barriers. Without this, the post-RA scheduler treats them as
  // ordinary ALU ops — dependency-independent of the surrounding callee-save
  // stores — and co-issues the epilogue SP-restore with prologue stores
  // restoring SP mid-function. Every subsequent sp-relative address then
  // resolves against the caller's SP, not the callee frame (e.g. VASTART
  // va_list field materializations in variadic functions read garbage past
  // the frame, so va_arg returns 0). The base TargetInstrInfo default enforces
  // this via modifiesRegister(getStackPointerRegisterToSaveRestore); this
  // override shadows the base, so re-assert it via the explicit frame flags.
  if (MI.getFlag(MachineInstr::FrameSetup) ||
      MI.getFlag(MachineInstr::FrameDestroy))
    return true;

  // Do not packetize SP adjusts with later SP-relative spills.
  // After PEI, ADJCALLSTACK is SUBI32/ADDI32_W of SP; the pseudo names
  // alone would miss the expanded form.
  if (MI.getOpcode() == Haydn::ADJCALLSTACKDOWN ||
      MI.getOpcode() == Haydn::ADJCALLSTACKUP)
    return true;
  if ((MI.getOpcode() == Haydn::SUBI32 || MI.getOpcode() == Haydn::ADDI32_W) &&
      MI.getNumOperands() >= 1 && MI.getOperand(0).isReg() &&
      MI.getOperand(0).getReg() == Haydn::R13)
    return true;

  // Debug values and labels
  if (MI.isDebugValue() || MI.isDebugLabel() || MI.isDebugInstr())
    return true;

  // CFI instructions
  if (MI.isCFIInstruction())
    return true;

  // Labels and position markers
  if (MI.isLabel())
    return true;

  // Implicitdefs/Uses are not real instructions.
  if (MI.isImplicitDef())
    return false;

  // Instructions with side effects that should remain isolated.
  // Haydn ALU instructions set hasSideEffects=1 because they access SFR (status
  // flag register). This is a modeled side effect — SFR appears as an explicit
  // implicit def/use in MIR. Only treat as a boundary if there are unmodeled
  // side effects BEYOND just accessing SFR.
  if (MI.hasUnmodeledSideEffects()) {
    bool HasNonSFRImplicit = false;
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.isImplicit()) {
        if (MO.getReg() != Haydn::SFR)
          HasNonSFRImplicit = true;
      }
    }
    if (HasNonSFRImplicit)
      return true;
    // All implicit operands are SFR — modeled side effect, not a boundary.
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Branch relaxation hooks
//===----------------------------------------------------------------------===//

// GR2.7: the range estimate below is the SAME authority at the pre-S1
// normalization BranchRelaxation seat (HaydnTargetMachine.cpp addPreSched2,
// C1–C6 catalog comment) and at the interim addPreEmitPass BR seats; a far
// site it selects at the pre-S1 seat is terminal long form before the
// first packet commit.

// D1.33 one-buffer accessor: every far-deciding seat reads the cl::opt
// through THIS method (no second capture, no compile-time constant fork).
uint32_t HaydnInstrInfo::getBranchRelaxSafetyBuffer() const {
  return BranchRelaxSafetyBuffer;
}

bool HaydnInstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                            int64_t BrOffset) const {
  // Opcode-only API cannot unwrap a BUNDLE child (JALR long form vs near
  // cond). Conservative simm12 made BR trampoline committed packets in a
  // loop (matmult-int hang). GR2.7: LongBranchNormalize range-tests the
  // child opcode; BR must not create CFG on a BUNDLE root.
  // Exception: getBranchDestBlock just observed this same bundled cond
  // whose MBB has a recoverable CFG JALR (BR calls getBranchDestBlock then
  // isBlockInRange on that same MI). Unwrap that MI explicitly
  // (D1.142); one-way / unarmed BUNDLE stays always-in-range.
  if (BranchOpc == TargetOpcode::BUNDLE) {
    const MachineInstr *Armed = HaydnBundleCondRangeMI;
    HaydnBundleCondRangeMI = nullptr;
    if (!Armed || !Armed->isBundle() ||
        Armed->getOpcode() != TargetOpcode::BUNDLE)
      return true;
    BranchOpc = haydnBranchRangeOpcode(*Armed);
    if (BranchOpc == TargetOpcode::BUNDLE)
      return true;
  }

  BranchOpc = haydnLogicalOpcode(BranchOpc);
  // JAL / JAL_W / JAL_TCO have a 20-bit signed target field (SImm20):
  // ±512KB range. JALR / JALR_W / JALR_CALL / JALR_TCO have no offset
  // limitation (register-indirect). ISel emits JAL_IND (JALR after
  // expand); JAL_W is the short encoding after cycle-neutral relax.
  // Legacy opcodes stay for asm/parser. Catalog BEQZ stays cond-only
  // (WIDE_BranchSImm12 below).
  if (BranchOpc == Haydn::JAL || BranchOpc == Haydn::JAL_W ||
      BranchOpc == Haydn::JAL_TCO || BranchOpc == Haydn::JALR ||
      BranchOpc == Haydn::JALR_W || BranchOpc == Haydn::JALR_CALL ||
      BranchOpc == Haydn::JALR_TCO)
    return true;

  // Pseudo-call and jump-table pseudo reach ±512KB (JAL_W) / unlimited
  // (JALR_W via BR_JT) — always in range for any single fn.
  // NOTE : B is deliberately NOT here. B encodes as BEQZ rs=R0 and shares
  // the conditional WIDE_BranchSImm12 byte-simm12 reach — it is NOT a
  // long-reach unconditional jump. Modeling B as always-in-range hid
  // out-of-range unconditional branches from BranchRelaxation: when
  // fixupConditionalBranch relaxes a far conditional into (inverted cond to
  // near + B to far), the B leg still overflows simm12. Letting B fall
  // through to the RelocFieldInfo check below makes BranchRelaxation detect
  // the far B leg and relax it via fixupUnconditionalBranch →
  // insertIndirectBranch (LUI+ADDI+JALR, unlimited reach) on the next
  // fixed-point iteration.
  if (BranchOpc == Haydn::PseudoCALL || BranchOpc == Haydn::BR_JT)
    return true;

  // ZOL / JNZD latch metas are NOT PC-relative cond branches. analyzeBranch
  // exposes them so SMS/HardwareLoops can see the back-edge, but their
  // "offset" is the whole body span. Treating them as WIDE_BranchSImm12
  // makes BranchRelaxation retarget PseudoLoopEnd through a LUI+ADDI+JALR
  // trampoline (gcc-c-torture 20021120-1: ~10KB soft-float body). That
  // poisons the hwloop end label, and a later software-loop rewrite leaves
  // the trampoline on the exit fallthrough → infinite loop / torture
  // TIMEOUT. Hardware end-of-loop has no PC-relative field; FixupHwLoops
  // owns lowering. Never relax these opcodes.
  if (BranchOpc == Haydn::PseudoLoopEnd || BranchOpc == Haydn::LoopJNZ)
    return true;

  // Logical BEQ_W..BLTU_W / BEQZ_W..BLTZ_W, catalog BEQZ/BNEZ, and B.
  // One window with computeRelocValue: WIDE_BranchSImm12 FieldSize=12,
  // ValueShift=0 (byte PC+imm). isInt<13> was the leftover ÷2-era ±4 KiB
  // window and left a 2–4 KiB dead zone that integrated-as then rejected.
  //
  // Hexagon-style residual buffer (HexagonBranchRelaxation.cpp:164-166):
  // Distance = |offset| + BranchRelaxSafetyBuffer after getInstSizeInBytes
  // has charged named late-layout growth. Inflate BrOffset away from zero,
  // then apply the reloc row. Default is BranchRelaxSafetyBufferBytes.
  //
  // Forward vs backward are named separately so the two directions cannot
  // drift: a later promotion between src and dest grows a forward offset;
  // an earlier promotion grows a backward |offset|. Each direction charges
  // one insertIndirectBranch sequence (MaxSingleBranchGrowthBytes).
  const HaydnReloc::RelocFieldInfo &I = HaydnReloc::getRelocFieldInfo(
      HaydnReloc::RelocKind::WIDE_BranchSImm12);
  // D1.33: consume the one-buffer accessor (not the raw cl::opt) so this
  // window test and every other far-deciding seat cannot drift.
  const int64_t ForwardGrowth =
      (int64_t)getBranchRelaxSafetyBuffer();
  const int64_t BackwardGrowth =
      (int64_t)getBranchRelaxSafetyBuffer();
  static_assert(haydn::hwloop::BranchRelaxSafetyBufferBytes ==
                    haydn::hwloop::MaxSingleBranchGrowthBytes,
                "TII range buffer is one long-form sequence");
  int64_t Inflated =
      BrOffset >= 0 ? BrOffset + ForwardGrowth : BrOffset - BackwardGrowth;
  int64_t Shifted = Inflated >> I.ValueShift;
  return I.IsSigned ? isIntN(I.FieldSize, Shifted)
                    : isUIntN(I.FieldSize, static_cast<uint64_t>(Shifted));
}

bool HaydnInstrInfo::isBranchOffsetInRange(const MachineInstr &MI,
                                           int64_t BrOffset) const {
  return isBranchOffsetInRange(haydnBranchRangeOpcode(MI), BrOffset);
}

HaydnJalrAddrMaterializeChain
HaydnInstrInfo::getJalrAddrMaterializeChain(const MachineInstr &Jalr) const {
  return scanJalrAddrMaterializeChain(Jalr);
}

MachineBasicBlock *
HaydnInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  // BranchRelaxation may pass a BUNDLE root (AnyInBundle isBranch).
  // Destination MBB is on the child branch (unwrapBundleControlFlow).
  HaydnBundleCondRangeMI = nullptr;
  if (MI.isBundle() && MI.getParent()) {
    const MachineInstr &Br0 = unwrapBundleControlFlow(MI);
    unsigned LO0 = haydnLogicalOpcode(Br0.getOpcode());
    if (isHaydnCondBranch1Reg(LO0) || isHaydnCondBranch2Reg(LO0)) {
      if (const MachineInstr *Jalr = trailingCfgJalr(*MI.getParent())) {
        if (Jalr != &Br0 && Jalr != &MI &&
            getJalrAddrMaterializeChain(*Jalr).Dest)
          HaydnBundleCondRangeMI = &MI;
      }
    }
  }
  const MachineInstr &Br = unwrapBundleControlFlow(MI);
  unsigned Opc = haydnLogicalOpcode(Br.getOpcode());

  auto mbbOp = [&](unsigned Idx) -> MachineBasicBlock * {
    if (Br.getNumOperands() <= Idx || !Br.getOperand(Idx).isMBB())
      return nullptr;
    return Br.getOperand(Idx).getMBB();
  };

  // Two-register conditional branches. Operands: rs1, rs2, brtarget
  if (isHaydnCondBranch2Reg(Opc))
    return mbbOp(2);

  // Single-register conditional branches. Operands: rs, brtarget
  if (isHaydnCondBranch1Reg(Opc))
    return mbbOp(1);

  // Unconditional branch pseudo: B
  // Operand 0: brtarget
  if (Opc == Haydn::B)
    return mbbOp(0);

  // JAL / JAL_W / JAL_TCO with MBB operand. Operands: rd,
  // calltarget/brtarget_wide_i20 (both shapes are (rd, target)). ISel emits
  // JAL_IND; a later cycle-neutral relax may rewrite to JAL_W. Legacy JAL
  // stays for asm/parser and hand MIR. Non-MBB (symbol / musttail) is
  // dest-less: null, not UNREACHABLE.
  if (Opc == Haydn::JAL || Opc == Haydn::JAL_W || Opc == Haydn::JAL_TCO)
    return mbbOp(1);

  // Hardware-loop terminators.
  // PseudoLoopEnd: single MBB operand (the loop body).
  // LoopJNZ: reg + MBB operand (counter + loop body).
  if (Opc == Haydn::PseudoLoopEnd)
    return mbbOp(0);
  if (Opc == Haydn::LoopJNZ)
    return mbbOp(1);

  // Long-form JALR has no MBB operand; dest is on the LUI/ADDI32_W pair
  // that defines JALR rs (possibly each a singleton BUNDLE root).
  // Hexagon analyzeBranch peer: instr_iterator, not getPrevNode
  // (HexagonInstrInfo.cpp:435-508). getPrevNode from a bundled JALR
  // child lands on the BUNDLE header.
  // RISC-V (RISCVInstrInfo.cpp:1325-1327) and AIE
  // (AIEBaseInstrInfo.cpp:186-188) leave every indirect unanalyzable.
  // Haydn overlay: recover LUI/ADDI %bb dest so a preceding near cond
  // stays two-way (va-arg-24). Match rs (follow MOVE32/COPY). Skip
  // unrelated instrs (coissued ADD32, latch SUBI of another GPR). Stop
  // at the first def/clobber of rs that is not that ordered pair — walking
  // past an unrelated LUI of a different reg is fine; assigning that LUI
  // to RET/computed-goto (rs mismatch) is not. Forward ADDI->LUI->JALR is
  // Dest-from-LUI incomplete, not a complete pair (D1.153).
  if (isHaydnIndirectJALR(Opc)) {
    // One D1.117/D1.153 walk. Returning JALR_CALL / dest-less JALR: Dest
    // is null, never UNREACHABLE. Incomplete pair may still name the first
    // %bb so cond+JALR stays analyzable.
    return getJalrAddrMaterializeChain(Br).Dest;
  }

  // Dest-less RET / BR_JT / JALR_TCO / computed-goto / unhandled opcode: null,
  // not UNREACHABLE. Long-Branch Normalize may call this on isBranch BUNDLE
  // children that unwrap to a return. RISC-V asserts isBranch then last-operand MBB
  // (RISCVInstrInfo.cpp:1744-1748); AIE leaves every indirect unanalyzable
  // (AIEBaseInstrInfo.cpp:186-188). Haydn overlay: no dest → nullptr.
  return nullptr;
}

void HaydnInstrInfo::insertIndirectBranch(
    MachineBasicBlock &MBB, MachineBasicBlock &NewDestBB,
    MachineBasicBlock &RestoreBB, const DebugLoc &DL, int64_t BrOffset,
    RegScavenger *RS) const {
  // Pre-S1 generic BR trampoline: LUI+ADDI32_W+JALR_W into MBB.
  // RestoreBB stays empty so BranchRelaxation.cpp:687-688 erases it.
  // RISC-V RestoreBB (RISCVInstrInfo.cpp:1433-1498) and AArch64
  // spill-to-RestoreBB (AArch64InstrInfo.cpp:390-404) are the CFG form
  // Haydn does not grow. AIE has no insertIndirectBranch
  // (AIE2TargetMachine.cpp:92 empty PreEmit). Hexagon regenerates in
  // place (HexagonBranchRelaxation.cpp:185). ARM ImmBranch is
  // MI+MaxDisp only (ARMConstantIslandPass.cpp:184-197).
  //
  // Sequence (exact-commit real MIs — no residual LOADI32 pseudo):
  //   LUI      scratch, <dest_mbb>
  //   ADDI32_W scratch, scratch, <dest_mbb>
  //   JALR_W   scratch, scratch, 0
  //
  // Never use R0 as the JALR link dest (soft-zero, not hardwired).
  // Scratch: RegScavenger AllowSpill=false. No free GPR is fail-closed
  // (no R11 spill, no RestoreBB reload). AllowSpill=true used to reload
  // after JALR_W in the trampoline (dead reload; dest saw a clobbered
  // live-in).
  assert(RS && "RegScavenger required for long branching");
  assert(MBB.pred_size() == 1);
  assert(RestoreBB.empty() &&
         "RestoreBB stays empty; generic BR erases the unused block");

  // GR2.7 postcommit CFG-creation wall: once the first Finalize run
  // stamped the per-function block budget, creating a trampoline here is
  // refused — long-form promotion is LBN in-block templates. Never silent
  // CFG surgery.
  if (auto *FuncInfoStamp = MBB.getParent()
                                ->getInfo<HaydnMachineFunctionInfo>();
      FuncInfoStamp->hasPostCommitBlockBudget()) {
    std::string Msg;
    raw_string_ostream OS(Msg);
    OS << "Haydn insertIndirectBranch: postcommit CFG creation refused "
          "(GR2.7: long-form promotion must be selected pre-scheduler) in "
       << MBB.getParent()->getName() << " BB#" << MBB.getNumber()
       << " -> BB#" << NewDestBB.getNumber();
    report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
  }

  if (!isInt<32>(BrOffset))
    report_fatal_error(
        "Branch offsets outside of the signed 32-bit range not supported");

  MachineFunction *MF = MBB.getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  auto II = MBB.end();
  const DebugLoc UseDL = haydnInheritBranchDebugLoc(MBB, DL);
  MachineInstr *LastOld =
      MBB.empty() ? nullptr : &*std::prev(MBB.end());

  auto scavengeScratch = [&](MachineBasicBlock::iterator From) -> Register {
    // Scavenge only (AIE model: R12 is allocatable, never a free AT).
    Register S = RS->scavengeRegisterBackwards(
        Haydn::GPR32RegClass, From, /*RestoreAfter=*/false, /*SpAdj=*/0,
        /*AllowSpill=*/false);
    if (!S)
      S = RS->FindUnusedReg(&Haydn::GPR32RegClass);
    return S;
  };

  auto refuseNoScratch = [&]() {
    report_fatal_error(
        Twine("Haydn insertIndirectBranch: no free GPR for "
              "LUI+ADDI32_W+JALR_W (RestoreBB/spill-R11 deleted) in ") +
            MF->getName() + " BB#" + Twine(MBB.getNumber()),
        /*GenCrashDiag=*/false);
  };

  // Bare final real MIs (one product parcel each). Late Finalize wraps after
  // PreEmit — do not callback-pack. Returns the LUI MI for scavenger walk-from.
  auto emitMBBAddr = [&](MachineBasicBlock &InsMBB,
                         MachineBasicBlock::iterator InsertPt, Register Dst,
                         MachineBasicBlock *JumpDest) -> MachineInstr * {
    MachineInstr *Lui =
        BuildMI(InsMBB, InsertPt, UseDL, get(Haydn::LUI), Dst).addMBB(JumpDest);
    BuildMI(InsMBB, InsertPt, UseDL, get(Haydn::ADDI32_W), Dst)
        .addReg(Dst)
        .addMBB(JumpDest);
    return Lui;
  };

  auto emitIndirectJump =
      [&](MachineBasicBlock &InsMBB, MachineBasicBlock::iterator InsertPt,
          Register Scratch, MachineBasicBlock *JumpDest) -> MachineInstr * {
    MachineInstr *First = emitMBBAddr(InsMBB, InsertPt, Scratch, JumpDest);
    BuildMI(InsMBB, InsertPt, UseDL, get(Haydn::JALR_W))
        .addReg(Scratch, RegState::Define)
        .addReg(Scratch)
        .addImm(0);
    return First;
  };

  Register ScratchPhys;
  if (MBB.empty()) {
    // Scavenger needs at least one instr to walk from; use a vreg then
    // substitute (same workaround as RISCV/SIInstrInfo).
    Register ScratchV = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    MachineInstr *LoadMI = emitIndirectJump(MBB, II, ScratchV, &NewDestBB);

    RS->enterBasicBlockEnd(MBB);
    ScratchPhys = scavengeScratch(LoadMI->getIterator());
    if (!ScratchPhys.isValid())
      refuseNoScratch();
    RS->setRegUsed(ScratchPhys);
    MRI.replaceRegWith(ScratchV, ScratchPhys);
    MRI.clearVirtRegs();
    haydnStampDebugLocOnNewInstrs(MBB, LastOld, UseDL);
    haydnPreserveLongFormJumpState(MBB, NewDestBB);
    return;
  }

  RS->enterBasicBlockEnd(MBB);
  ScratchPhys = scavengeScratch(II);
  if (!ScratchPhys.isValid())
    refuseNoScratch();
  RS->setRegUsed(ScratchPhys);
  emitIndirectJump(MBB, II, ScratchPhys, &NewDestBB);
  haydnStampDebugLocOnNewInstrs(MBB, LastOld, UseDL);
  haydnPreserveLongFormJumpState(MBB, NewDestBB);
}

/// Named late-layout growth that generic BranchRelaxation cannot see as
/// separate MIR cycles at the first PreEmit scan (Hexagon computeOffset
/// charges extender + MBB align in HexagonBranchRelaxation.cpp:95-114).
/// Extra is ON TOP of the instruction's own EncodedBytes:
///   * BR_JT: one parcel for successor XOR32 R0 re-zero
///     (HaydnExpandPseudos.cpp:88-116; historically AsmPrinter-injected)
///   * SET_HWLOOP*: InterveningCycles parcels for setup/align pads
///     (HaydnHWLoopContracts.h; Fixup inserts them after the first BR)
///   * same-slot overflow: extra parcels when membership cannot injectively
///     occupy one Format E cycle (historical serial-split undercount)
static unsigned namedLateLayoutGrowthBytes(const MachineInstr &MI,
                                           const HaydnInstrInfo &TII) {
  const unsigned B = haydn::bundle::productParcelBytes().Value;
  unsigned Extra = 0;

  auto chargeOpcode = [&](unsigned Opc) {
    if (Opc == Haydn::BR_JT)
      Extra += B;
    if (TII.isHardwareLoopSetupOpcode(Opc))
      Extra += haydn::hwloop::InterveningCycles * B;
  };

  if (MI.isBundle()) {
    if (!MI.getParent())
      return 0;
    SmallVector<unsigned, 3> Members;
    for (const MachineInstr *C : haydn::bundle::members(MI)) {
      if (C->isMetaInstruction() || C->isDebugInstr() || C->isPosition())
        continue;
      Members.push_back(C->getOpcode());
      chargeOpcode(C->getOpcode());
    }
    if (Members.size() > 1 &&
        !haydn::bundle::opcodesHaveFormatEUnitCover(Members, TII))
      Extra += B * (Members.size() - 1);
    return Extra;
  }

  chargeOpcode(MI.getOpcode());
  return Extra;
}

namespace {

bool haydnAsmStartsWith(const char *Str, StringRef Needle) {
  return Needle.size() && strncmp(Str, Needle.data(), Needle.size()) == 0;
}

bool isHaydnAsmIdentStart(unsigned char C) {
  return isAlpha(C) || C == '_' || C == '.';
}

bool isHaydnAsmIdentBody(unsigned char C) {
  return isAlnum(C) || C == '_' || C == '.';
}

bool isPublicHaydnAsmMnemonic(const MCInstrInfo &MII, StringRef Tok) {
  if (Tok.empty())
    return false;
  if (haydnIsGeneratedMemberName(Tok) || haydnIsResidualFieldSlotName(Tok))
    return false;
  for (unsigned Opc = 0, N = MII.getNumOpcodes(); Opc != N; ++Opc) {
    StringRef Name = MII.getName(Opc);
    if (Name.empty() || haydnIsGeneratedMemberName(Name) ||
        haydnIsResidualFieldSlotName(Name))
      continue;
    if (Name.equals_insensitive(Tok))
      return true;
  }
  return false;
}

// Exact Format E layout bytes for INLINEASM text, or nullopt if the text is
// not an admitted typed sequence. Empty / comment-only is 0 (metadata).
// Unbraced public mnemonic → one product parcel (MC wraps a bare logical as
// a singleton row, HaydnMCCodeEmitter.cpp:445-496). Braced `{ ... }` → one
// parcel (AsmParser one-packet emit, HaydnAsmParser.cpp:761-986). `.space N`
// is the generic TII exact-byte directive (TargetInstrInfo.cpp:107-141).
// Comment law mirrors AsmLexer::LexToken exactly: the CommentString ('//')
// is a line comment anywhere a token starts; '#' is an additional line
// comment ONLY at start-of-statement and only while the MCAsmInfo admits
// additional comments (AllowAdditionalComments, MCAsmInfo.h:143). '#'
// mid-statement is an operand error in the real assembler, so it stays
// operand text here and the real parse rejects it (fail-closed parity;
// llvm-libc setjmp/longjmp naked bodies use '# ...' result comments).
std::optional<unsigned> tryExactHaydnInlineAsmLayoutBytes(
    const MCInstrInfo &MII, const char *Str, const MCAsmInfo &MAI) {
  using haydn::bundle::productParcelBytes;
  const unsigned Parcel = productParcelBytes().Value;
  const char *const Sep = MAI.getSeparatorString();
  const size_t SepLen = std::strlen(Sep);
  unsigned Length = 0;

  auto atSeparator = [&](const char *P) {
    return SepLen && strncmp(P, Sep, SepLen) == 0;
  };
  auto atComment = [&](const char *P) {
    return haydnAsmStartsWith(P, MAI.getCommentString());
  };
  auto atStmtStartHash = [&](const char *P) {
    return MAI.shouldAllowAdditionalComments() && *P == '#';
  };
  auto skipSpaces = [](const char *&P) {
    while (*P && *P != '\n' && isSpace(static_cast<unsigned char>(*P)))
      ++P;
  };
  auto skipComment = [&](const char *&P) {
    while (*P && *P != '\n' && !atSeparator(P))
      ++P;
  };
  auto skipOperands = [&](const char *&P, int Depth) {
    while (*P && *P != '\n') {
      if (Depth == 0 && atSeparator(P))
        return;
      if (Depth > 0 && *P == '}')
        return;
      if (atComment(P)) {
        skipComment(P);
        return;
      }
      ++P;
    }
  };

  const char *P = Str;
  bool AtStmt = true;
  int Depth = 0;
  bool BraceHasMnemonic = false;
  bool BraceOpen = false;
  while (*P) {
    if (*P == '\n' || (Depth == 0 && atSeparator(P))) {
      AtStmt = true;
      P += (*P == '\n') ? 1 : static_cast<int>(SepLen);
      continue;
    }
    if (Depth > 0 && atSeparator(P)) {
      P += SepLen;
      AtStmt = true;
      continue;
    }
    if (atComment(P)) {
      skipComment(P);
      AtStmt = false;
      continue;
    }
    // '#' line comment only at start-of-statement outside braces (the
    // AsmLexer LexToken gate; the braced-packet parse rejects comments, so
    // Depth>0 keeps fail-closed). Mid-statement '#' stays operand text and
    // the real parse rejects it.
    if (Depth == 0 && AtStmt && atStmtStartHash(P)) {
      skipComment(P);
      AtStmt = false;
      continue;
    }
    if (isSpace(static_cast<unsigned char>(*P))) {
      ++P;
      continue;
    }

    if (Depth > 0) {
      if (*P == '{') {
        ++Depth;
        ++P;
        AtStmt = false;
        continue;
      }
      if (*P == '}') {
        --Depth;
        ++P;
        if (Depth == 0) {
          if (!BraceHasMnemonic)
            return std::nullopt;
          Length += Parcel;
          BraceOpen = false;
          BraceHasMnemonic = false;
          AtStmt = true;
        }
        continue;
      }
      if (!AtStmt) {
        ++P;
        continue;
      }
      if (strncmp(P, ".space", 6) == 0)
        return std::nullopt;
      if (*P == '.') {
        const char *Q = P;
        while (*Q && isHaydnAsmIdentBody(static_cast<unsigned char>(*Q)))
          ++Q;
        const char *R = Q;
        skipSpaces(R);
        if (*R != ':')
          return std::nullopt;
        P = R + 1;
        AtStmt = true;
        continue;
      }
      if (!isHaydnAsmIdentStart(static_cast<unsigned char>(*P)))
        return std::nullopt;
      const char *TokStart = P;
      ++P;
      while (*P && isHaydnAsmIdentBody(static_cast<unsigned char>(*P)))
        ++P;
      StringRef Tok(TokStart, P - TokStart);
      const char *R = P;
      skipSpaces(R);
      if (*R == ':') {
        P = R + 1;
        AtStmt = true;
        continue;
      }
      if (!isPublicHaydnAsmMnemonic(MII, Tok))
        return std::nullopt;
      BraceHasMnemonic = true;
      skipOperands(P, Depth);
      AtStmt = false;
      continue;
    }

    if (*P == '{') {
      BraceOpen = true;
      BraceHasMnemonic = false;
      Depth = 1;
      AtStmt = true;
      ++P;
      continue;
    }
    if (*P == '}')
      return std::nullopt;

    if (strncmp(P, ".space", 6) == 0) {
      char *End = nullptr;
      long SpaceSize = std::strtol(P + 6, &End, 10);
      if (End == P + 6)
        return std::nullopt;
      SpaceSize = SpaceSize < 0 ? 0 : SpaceSize;
      P = End;
      skipSpaces(P);
      if (*P && *P != '\n' && !atSeparator(P) && !atComment(P))
        return std::nullopt;
      Length += static_cast<unsigned>(SpaceSize);
      AtStmt = false;
      continue;
    }

    if (!isHaydnAsmIdentStart(static_cast<unsigned char>(*P)))
      return std::nullopt;
    const char *TokStart = P;
    ++P;
    while (*P && isHaydnAsmIdentBody(static_cast<unsigned char>(*P)))
      ++P;
    StringRef Tok(TokStart, P - TokStart);
    const char *R = P;
    skipSpaces(R);
    if (*R == ':') {
      P = R + 1;
      AtStmt = true;
      continue;
    }
    if (Tok.front() == '.' && !Tok.equals_insensitive(".space"))
      return std::nullopt;
    if (!isPublicHaydnAsmMnemonic(MII, Tok))
      return std::nullopt;
    Length += Parcel;
    skipOperands(P, /*Depth=*/0);
    AtStmt = false;
  }
  if (BraceOpen || Depth != 0)
    return std::nullopt;
  return Length;
}

} // namespace

unsigned HaydnInstrInfo::getInlineAsmLength(
    const char *Str, const MCAsmInfo &MAI,
    const TargetSubtargetInfo *STI) const {
  (void)STI;
  if (!Str)
    return 0;
  std::optional<unsigned> Exact =
      tryExactHaydnInlineAsmLayoutBytes(*this, Str, MAI);
  if (!Exact) {
    report_fatal_error(
        Twine("Haydn: inline asm is not an exact typed Format E layout "
              "sequence (public mnemonic / braced packet / .space N, or "
              "empty metadata); opaque executable asm is rejected in "
              "layout positions: ") +
            Str,
        /*GenCrashDiag=*/false);
  }
  return *Exact;
}

unsigned HaydnInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  // AIE-shaped size authority (AIE1InstrInfo.cpp:646-651 getSize; AIE
  // AIEBaseInstrInfo.cpp:570-577 Format->getSize on the composite):
  //   * BUNDLE root → encodedBytesFor(committed Format E row) + named
  //     late-layout growth (Hexagon computeOffset peer)
  //   * bare real / catalog logical with a FormatEAltSpans row /
  //     multi-parcel pseudo → productParcelBytes() * N
  //   * child inside a BUNDLE → 0 (composite size is on the root)
  //   * pure meta / zero-size compiler pseudos → 0
  //   * INLINEASM / INLINEASM_BR → exact typed getInlineAsmLength
  using haydn::bundle::committedEncodedBytes;
  using haydn::bundle::productParcelBytes;
  const unsigned B = productParcelBytes();
  static_assert(haydn::bundle::productParcelBytes().Value ==
                    haydn::bundle::ProductEncodedBytesValue,
                "productParcelBytes is the product EncodedBytes oracle");

  // Formed VLIW packet: committed EncodedBytes plus named late growth.
  if (MI.isBundle())
    return committedEncodedBytes(MI).Value +
           namedLateLayoutGrowthBytes(MI, *this);

  // Children are accounted on the BUNDLE root (AIE bundle size is format size
  // on the composite, not sum of slot sub-instruction Sizes).
  if (MI.isInsideBundle())
    return 0;

  // Exact typed layout length. Empty side-effect barriers charge 0. Must
  // run before the isPseudo early-out (INLINEASM is a
  // StandardPseudoInstruction). Opaque / untyped text is fatal — never
  // MaxInstLength × statement count.
  if (MI.isInlineAsm()) {
    const MachineFunction *MF = MI.getMF();
    if (!MF || !MI.getNumOperands() || !MI.getOperand(0).isSymbol())
      return 0;
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              *MF->getTarget().getMCAsmInfo(),
                              &MF->getSubtarget());
  }

  // Pseudos that expand to one or more real parcels before/at emit.
  // Size unit is always productParcelBytes() (= generated Full EncodedBytes).
  // Resolve Format E / `_S*` members first so SET_HWLOOP* and logical NOP
  // (isPseudo so MC does not emit all-zero Inst) still charge layout bytes.
  // Hexagon getSize uses desc size for architectural NOP
  // (HexagonInstrInfo.cpp:4601-4609); AIE has no ZOL analog.
  switch (haydnLogicalOpcode(MI.getOpcode())) {
  default:
    break;
  case Haydn::NOP:
  case Haydn::WFI:
    return B;
  case Haydn::B:
  case Haydn::RET:
  case Haydn::BR_JT:
  case Haydn::PseudoCALL:
  case Haydn::PseudoCALLIndirect:
  case Haydn::SET_HWLOOP:
  case Haydn::SET_HWLOOP_REG:
  case Haydn::SET_HWLOOP_W:
  case Haydn::SET_HWLOOP_F2_W:
  case Haydn::SET_HWLOOP_REG_W:
    // One product parcel plus named late-layout growth (JT re-zero /
    // hwloop setup pads). B/RET/PseudoCALL growth is zero.
    return B + namedLateLayoutGrowthBytes(MI, *this);
  case Haydn::LOADI32: {
    // expandPostRAPseudo: Imm==0 → XOR32 (1); else XOR+ADDI or LUI+ADDI (2).
    if (MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 0)
      return B;
    return B * 2;
  }
  case Haydn::LOAD_ADDR:
    // LUI + ADDI32_W → two parcels.
    return B * 2;
  case Haydn::LOADI64:
    // SP-transient materialize: optional R12 spill/restore (when AT not
    // reserved) + SUBI32 + up to 2× MatInt(~4) + 2×ST + LD64 + ADDI32.
    // Worst case ~17 real ops. Size conservatively high so BR relaxes early.
    return B * 17;
  case Haydn::MOV_GPR_TO_DR64:
  case Haydn::MOV_DR64_TO_GPR:
    // SUBI32 + 2×ST + LD + ADDI32 = 5 parcels.
    return B * 5;
  case Haydn::ADJCALLSTACKDOWN:
  case Haydn::ADJCALLSTACKUP:
  case Haydn::VAEND:
  case Haydn::LIBCALL_SDIV:
  case Haydn::LIBCALL_UDIV:
  case Haydn::LIBCALL_SREM:
  case Haydn::LIBCALL_UREM:
  case Haydn::LIBCALL_MUL64:
    return 0;
  case Haydn::VASTART:
    // Was 0 while AsmPrinter emitted many product parcels → BR undercount.
    // Expanded pre-pack via free-reg scavenge (often no spill). Upper bound:
    // optional spill/restore + 3×(addr+st) + 2×(neg+st); far FI ≤ ~16 parcels.
    return B * 16;
  case Haydn::VACOPY:
    // Free-reg scavenge; worst-case spill + 5×(ld+st) ≤ ~14 parcels.
    return B * 14;
  }

  // AIE getSize reads MCInstrDesc::getSize() (AIE1InstrInfo.cpp:646-651)
  // and Format->getSize() on the composite (AIEBaseInstrInfo.cpp:570-577).
  // AIE MultiSlot logicals carry format size on the desc, so
  // getAlternateInstsOpcode → setDesc (AIEMachineScheduler.cpp:1126-1132)
  // is size-neutral. Haydn HaydnInst<0> catalog shells have desc Size 0
  // and isPseudo=1 so MC does not emit an all-zero Inst — isPseudo is an
  // emit flag, not a layout-zero flag. The generated FormatEAltSpans row
  // (AIE AlternateInsts analog) is the uncommitted singleton stand-in:
  // identity-bake (applyFinalDirectCompatibleMembers, the AIE
  // unconditional alternate bake overlay) is layout-identity only. Query
  // the generated span after occupancy peel (CSRW_W → CSRW) — never an
  // opcode list. D1.54 claimed this law; the charge never landed, and
  // LateConvergence D1.41 then saw +24 unaccounted bytes on two bare
  // CSRW shells (fleet-4 / fleet-5 cxfir16x16 bb.0->bb.9).
  {
    const std::string Catalog = haydn::format_e::peelLogicalOpcodeName(
        getName(MI.getOpcode()), /*StripWide=*/true);
    if (!Catalog.empty() &&
        haydn::format_e::findAltSpan(Catalog.c_str()) != nullptr)
      return B + namedLateLayoutGrowthBytes(MI, *this);
  }

  if (MI.isPseudo() || MI.isMetaInstruction() || MI.isDebugInstr() ||
      MI.isImplicitDef() || MI.isKill())
    return 0;

  // Bare real opcode: one product Format E parcel plus named late growth
  // (SET_HWLOOP_W / BR_JT already charged above; other setup forms land here).
  return B + namedLateLayoutGrowthBytes(MI, *this);
}

ResourceCycle *HaydnInstrInfo::CreateTargetScheduleState(
    const TargetSubtargetInfo &STI) const {
  // Pre-RA SMS contract boundary (GR2.1): Kind-A only — the generated
  // IssueWidth entry cap (bound once from the sched model; IssueWidth ==
  // FormatEE3EntryCapacity == Haydn::ISSUE_SLOT_COUNT, pinned in
  // HaydnMachineScheduler.cpp) plus the shared same-cycle RAW/WAW dependency
  // laws via the D999 MI overload. No exact format/unit/port consultation at
  // this seat; exact legality, cycle-slip, and commit are post-RA. The seat
  // never materializes multi-member BUNDLE roots (StageCount>1 is rejected;
  // product multi-stage is post-RA).
  return new HaydnIssueWidthCycle(STI.getSchedModel().IssueWidth);
}

ScheduleHazardRecognizer *HaydnInstrInfo::CreateTargetMIHazardRecognizer(
    const InstrItineraryData *ItinData, const ScheduleDAGMI *DAG) const {
  // Install HaydnHazardRecognizer for both pre-RA (vreg liveness) and
  // post-RA. Peer: AIE2InstrInfo creates AIEHazardRecognizer with
  // IsPreRA=DAG->hasVRegLiveness(). Always return a live target HR — never
  // a null factory / bare ScoreboardHazardRecognizer product path. Dual-run
  // generic residual (-haydn-premisched-matching-frontier=false) only
  // changes tryCandidate ranking; it does not uninstall this HR. ILP /
  // critical ranking residual attribution is the same contract: residual
  // drops matching-frontier ResourceDemand after pressure/critical stay
  // primary — CreateTargetMIHazardRecognizer still installs IsPreRA HR.
  //
  // Pre-RA law (phase identity): feasibility only — no
  // setAlternateDescriptor / setDesc / member opcodes / FormatID freeze.
  // Port demand is MRI-correct via HaydnPortModel (vreg regclass → GPR/DR/AR
  // bank), so three independent GPR writes cannot share one cycle under 2W
  // even when Full has three slots. MOVE32-class MI path charges each
  // explicit field (rd,rs → 1R1W), matching descriptor-only estimates
  // used by SMS MID placement. Pre-RA list-sched uses the MI path only.
  // Matching-frontier / packability oracles on PreRASchedStrategy are
  // metrics-only: they never materialize durable BUNDLE roots. Post-RA alone
  // stamps AltDescs for leaveRegion materializeMultiOpcodeInstrs. Lits:
  // prera-format-generic-baseline.ll, scheduler-ilp.ll, and
  // scheduler-critical-path.ll freeze pre-greedy CHECK-NOT BUNDLE / _S*
  // under product and generic residual arms.
  //
  // SMS-HOOK II-wrap: product InstrStage cycles==1; class-3 inventory empty.
  // Pre-RA HR books stages linearly (DeltaCycles+StageCycle), not modulo-II
  // ResourceCycle phases. Multi-cycle / II-wrap product enable stays blocked
  // (PreRASchedStrategy::productIIWrapFalseAcceptFailsClosed catalog pin).
  // No setDesc/member opcodes from II-wrap metrics on this path.
  //
  // Format-acceptance differential (plan §8.4 #7): CurrentCycleCandidates /
  // commitPlacement use pure exactTryAddProduct depth — same descriptor-
  // derived legality as ResourceCycle canReserve and post-RA HR. Format is
  // opcode-keyed (MI ≡ desc); port MI-vs-desc is orthogonal (MOVE32 above).
  // PreRASchedStrategy::productExactCanPackSequence pins the polarity surface.
  const bool IsPreRA = DAG && DAG->hasVRegLiveness();
  HaydnAlternateDescriptors *AltDescs = nullptr;
  AAResults *AA = nullptr;
  if (DAG && !IsPreRA) {
    AltDescs = &DAG->MF.getInfo<HaydnMachineFunctionInfo>()->getAltDescs();
    // AIE2InstrInfo.cpp:1153-1158 installs HR from the DAG.
    // AIEMachineScheduler.cpp:1792 passes Context->AA into buildEdges.
    // HaydnScheduleDAGMI::getAliasAnalysis is that overlay; post-RA factory
    // is always HaydnScheduleDAGMI (createHaydnPostRAScheduler).
    AA = static_cast<const HaydnScheduleDAGMI *>(DAG)->getAliasAnalysis();
  }
  return new HaydnHazardRecognizer(this, ItinData, IsPreRA, AltDescs, AA);
}

//===----------------------------------------------------------------------===//
// AIE dual-sched mutation helpers ()
//===----------------------------------------------------------------------===//

// getFirstMemoryCycle / getLastMemoryCycle / min/max are generated
// (HaydnGenMemoryCycles.inc, included above). AIE peer:
// AIEMemoryCyclesEmitter.cpp:123-157 and AIE2InstrInfo.cpp:53.

bool HaydnInstrInfo::isPublishedMemoryItinerary(unsigned SchedClass) {
  // Exact published MEMORY_ITIN_NAMES set (generate_sched_records.py).
  // One mechanism: the generator publishes a MemoryCycle row for every
  // class in this set; a memory-class miss against getFirst/LastMemoryCycle
  // is therefore a generator hole, not an unmodeled op (W21 / AIE
  // ExactLatencies fatality).
  // 2026-08-21 itinerary re-map: Slot2_LS retired (golden assigns every
  // former S2 memory row to LOADSTORE0/LOAD1); the published set is three.
  // 2026-08-21 latency P3: Slot0_LS_WbLat joins — store-writeback stores
  // keep the conservative memory pair (0, Data_Latency-1) while the
  // writeback REGISTER is golden Data_Latency=1.
  switch (SchedClass) {
  case Haydn::Sched::Slot0_LS:
  case Haydn::Sched::Slot0_LS_WbLat:
  case Haydn::Sched::Slot1_LD:
  case Haydn::Sched::Slot01_LD:
    return true;
  default:
    return false;
  }
}

std::optional<int>
HaydnInstrInfo::getMemoryLatency(unsigned SrcSchedClass,
                                 unsigned DstSchedClass) const {
  // Soft soak-off only: class-agnostic latency 1 for every src/dst pair.
  // Product path is architectural tables (AccurateMemoryLatency default ON).
  if (!AccurateMemoryLatency)
    return 1;

  std::optional<int> LastSrc = getLastMemoryCycle(SrcSchedClass);
  std::optional<int> FirstDst = getFirstMemoryCycle(DstSchedClass);
  // Unknown cycle → nullopt. MemoryEdges separates the two miss causes:
  // a published Slot*_LS / Slot*_LD class with no row is a generator hole
  // (fatal there, W21); any other class is simply not table-driven and
  // keeps the local default latency 1.
  if (!LastSrc || !FirstDst)
    return std::nullopt;
  // Last cycle of producer memory → first cycle of consumer memory.
  // Floor at 1 so a degenerate table cannot collapse an Ord Memory edge to 0.
  return std::max(1, static_cast<int>(*LastSrc) - static_cast<int>(*FirstDst) +
                         1);
}

unsigned HaydnInstrInfo::getMaxResultLatency(const MachineInstr &MI) const {
  const MachineFunction *MF = MI.getMF();
  if (!MF)
    return 1;
  const auto &ST = MF->getSubtarget<HaydnSubtarget>();
  const InstrItineraryData *Itin = ST.getInstrItineraryData();
  unsigned Lat = 1;
  if (Itin && !Itin->isEmpty()) {
    unsigned SC = MI.getDesc().getSchedClass();
    int FirstOp = Itin->Itineraries[SC].FirstOperandCycle;
    int LastOp = Itin->Itineraries[SC].LastOperandCycle;
    if (FirstOp >= 0 && LastOp > FirstOp) {
      for (int OpIdx = FirstOp; OpIdx < LastOp; ++OpIdx) {
        unsigned C = Itin->OperandCycles[OpIdx];
        if (C > Lat)
          Lat = C;
      }
    }
  }
  if (MI.mayLoad()) {
    const HaydnAvailabilityAwareResourceRecord Rec =
        haydnMakeAvailabilityAwareResourceRecord(MI.getOpcode());
    Lat = std::max(Lat, Rec.Aggregate.LoadLatencyScaffold);
  }
  return Lat;
}

unsigned HaydnInstrInfo::getNumDelaySlots(const MachineInstr & /*MI*/) const {
  // Haydn has no architectural delayed-branch slots; post-RA inserts NOPs via
  // leaveMBB. RegionEndEdges uses getMaxResultLatency instead.
  return 0;
}

namespace {

// True if \p Opc is a Haydn ADD/SUB that can serve as a loop-carried induction
// step (the bump of an IV update). ADD32/SUB32 have two register sources;
// ADDI32 has one register source and an immediate step. The IV candidate is
// the step's non-immediate register source. Accepts both the legacy base
// opcodes and their `_S<k>` Selector-emitted variants.
static bool isInductionStep(unsigned Opc, const MCInstrInfo &) {
  unsigned Base = haydnLogicalOpcode(Opc);
  return Base == Haydn::ADD32 || Base == Haydn::ADDI32 ||
         Base == Haydn::ADDI32_W || Base == Haydn::SUB32;
}

// Return the latch-incoming value of a PHI in \p LoopBB, i.e. the incoming
// register whose predecessor block is the loop back-edge. For a single-BB
// loop analyzed by the MachinePipeliner, the back-edge predecessor is LoopBB
// itself (the loop block branches to itself). Returns Register if \p PHIMI
// is not a PHI in LoopBB or has no such incoming.
static Register getPHILatchIncoming(const MachineInstr &PHIMI,
                                    MachineBasicBlock *LoopBB) {
  if (!PHIMI.isPHI() || PHIMI.getParent() != LoopBB)
    return Register();
  // PHI operand layout: %dst = PHI %v0, %bb0, %v1, %bb1,...
  for (unsigned I = 1, E = PHIMI.getNumOperands(); I + 1 < E; I += 2) {
    if (PHIMI.getOperand(I + 1).isMBB() &&
        PHIMI.getOperand(I + 1).getMBB() == LoopBB)
      return PHIMI.getOperand(I).getReg();
  }
  return Register();
}

// Return the preheader-incoming value of a PHI in \p LoopBB, i.e. the incoming
// register whose predecessor block is NOT the loop back-edge (the value the
// IV is initialized to before the first iteration). For a single-BB loop this
// is the other incoming of the 2-input PHI. Returns Register if \p PHIMI is
// not a PHI in LoopBB or has no such incoming. This is the induction
// variable's *init* value -- for a decrementing loop it is the trip count
// (the IV counts N, N-1,..., 0).
static Register getPHIPreheaderIncoming(const MachineInstr &PHIMI,
                                        MachineBasicBlock *LoopBB) {
  if (!PHIMI.isPHI() || PHIMI.getParent() != LoopBB)
    return Register();
  for (unsigned I = 1, E = PHIMI.getNumOperands(); I + 1 < E; I += 2) {
    if (PHIMI.getOperand(I + 1).isMBB() &&
        PHIMI.getOperand(I + 1).getMBB() != LoopBB)
      return PHIMI.getOperand(I).getReg();
  }
  return Register();
}

// Resolve the constant integer value of an induction step (\p BumpMI) into
// \p Step. The step is signed: positive for an incrementing IV, negative for
// a decrementing IV. ADDI32 carries the step in an immediate operand;
// ADD32/SUB32 carry it in a register source, which LSR materializes in one of
// two constant-load forms -- ADDI32 $r0, imm (the legacy materializer) or
// LOADI32 imm (the rematerializable single-immediate pseudo, emitted for
// countdown steps such as matrix_sum's -1). Chase one hop to the materializing
// Def and accept either form. Returns true and sets \p Step on success; false
// if the step is not a recoverable constant (the caller then conservatively
// rejects the loop, mirroring AIE's isConstStep requirement).
static bool getInductionStep(const MachineRegisterInfo &MRI,
                             const MCInstrInfo &,
                             const MachineInstr &BumpMI, int64_t &Step) {
  unsigned Opc = BumpMI.getOpcode();
  unsigned Base = haydnLogicalOpcode(Opc);
  if (Base == Haydn::ADDI32 || Base == Haydn::ADDI32_W) {
    if (!BumpMI.getOperand(2).isImm())
      return false;
    Step = BumpMI.getOperand(2).getImm();
    return true;
  }
  // ADD32/SUB32: the step is a register source. Recover the constant from the
  // materializing Def (the non-IV operand). ADDI32 $r0, imm carries the step
  // in operand 2 (with an R0 source); LOADI32 imm carries it as a single
  // immediate in operand 1 (no source register). LSR may emit either form, so
  // both must be recognized -- missing LOADI32 left countdown IVs (e.g.
  // matrix_sum `%step = LOADI32 -1; %bump = ADD32 %iv, %step`) with an
  // unresolved step, sending analyzeSimpleLoop down the incrementing branch
  // and deriving the trip count from the compare's zero operand. The
  // materialized step may be a generated member, so resolve through
  // haydnLogicalOpcode; LOADI32 has no member variants today but the call
  // is uniform and harmless.
  for (const MachineOperand &MO : BumpMI.explicit_uses()) {
    if (!MO.isReg() || !MO.getReg().isVirtual())
      continue;
    const MachineInstr *Def = MRI.getVRegDef(MO.getReg());
    if (!Def)
      continue;
    unsigned DefBase = haydnLogicalOpcode(Def->getOpcode());
    int64_t V = 0;
    if (DefBase == Haydn::LOADI32) {
      if (!Def->getOperand(1).isImm())
        continue;
      V = Def->getOperand(1).getImm();
    } else if (DefBase == Haydn::ADDI32 || DefBase == Haydn::ADDI32_W) {
      if (Def->getOperand(1).getReg() != Haydn::R0 ||
          !Def->getOperand(2).isImm())
        continue;
      V = Def->getOperand(2).getImm();
    } else {
      continue;
    }
    Step = (Base == Haydn::SUB32) ? -V : V;
    return true;
  }
  return false;
}

// Determine whether \p Reg is the update (bump) of a loop-carried induction
// variable in \p LoopBB, and return the IV register (the PHI that the bump
// feeds back into). The MachinePipeliner runs PRE-PHIElimination, so it sees
// genuine PHI-form MIR (-REWORK):
// bb.loop:
// %iv = PHI %init, %preheader, %bump, %bb.loop; the IV
// %bump = ADD32 %iv, %step; or ADDI32 / SUB32
// %cmp = SEQ32 %bump, %limit
// Given the comparison source \p Reg:
// 1. If \p Reg is the bump (ADD/ADDI/SUB defined in LoopBB), the IV candidate
// is its non-immediate register source; that candidate must be a PHI whose
// latch-incoming value == the bump's def (closing the back-edge cycle).
// 2. Symmetric shape -- \p Reg is itself the IV PHI and the bump is the PHI's
// latch-incoming value. This occurs when the compare reads the IV directly
// rather than the bumped value.
// On success, also returns the IV's defining PHI (\p IVPhi, for trip-count
// derivation) and the signed step (\p Step, to distinguish incrementing vs
// decrementing IVs). Returns the IV register on success, or Register if
// \p Reg is not an induction variable. See (rework), and
static Register findInductionVar(MachineRegisterInfo &MRI,
                                 const MCInstrInfo &MII,
                                 MachineBasicBlock *LoopBB, Register Reg,
                                 MachineInstr *&IVPhi, int64_t &Step) {
  IVPhi = nullptr;
  Step = 0;
  if (!Reg.isVirtual())
    return Register();

  MachineInstr *DefMI = MRI.getVRegDef(Reg);
  if (!DefMI || DefMI->getParent() != LoopBB)
    return Register();

  // Shape 1: the compare source is the BUMP. The IV is the bump's
  // non-immediate register source, which must be a PHI whose latch-incoming
  // value is exactly the bump's definition.
  if (isInductionStep(DefMI->getOpcode(), MII)) {
    Register BumpDef = DefMI->getOperand(0).getReg();
    for (const MachineOperand &MO : DefMI->explicit_uses()) {
      if (!MO.isReg() || !MO.getReg().isVirtual())
        continue;
      Register IVCandidate = MO.getReg();
      MachineInstr *PhiMI = MRI.getVRegDef(IVCandidate);
      if (!PhiMI || !PhiMI->isPHI() || PhiMI->getParent() != LoopBB)
        continue;
      Register LatchIncoming = getPHILatchIncoming(*PhiMI, LoopBB);
      if (LatchIncoming.isValid() && LatchIncoming == BumpDef) {
        IVPhi = PhiMI;
        // Recover the step; if it is not a constant, still recognize the IV
        // (the caller will conservatively reject if it needs the step).
        int64_t S = 0;
        if (getInductionStep(MRI, MII, *DefMI, S))
          Step = S;
        return IVCandidate;
      }
    }
    return Register();
  }

  // Shape 2 (symmetric): the compare source is itself the IV PHI. The bump is
  // the PHI's latch-incoming value and must be an ADD/ADDI/SUB defined in the
  // loop whose IV-source operand is this PHI.
  if (DefMI->isPHI()) {
    Register BumpReg = getPHILatchIncoming(*DefMI, LoopBB);
    if (!BumpReg.isVirtual())
      return Register();
    MachineInstr *BumpMI = MRI.getVRegDef(BumpReg);
    if (!BumpMI || BumpMI->getParent() != LoopBB ||
        !isInductionStep(BumpMI->getOpcode(), MII))
      return Register();
    for (const MachineOperand &MO : BumpMI->explicit_uses()) {
      if (MO.isReg() && MO.getReg() == Reg) {
        IVPhi = DefMI;
        int64_t S = 0;
        if (getInductionStep(MRI, MII, *BumpMI, S))
          Step = S;
        return Reg;
      }
    }
    return Register();
  }

  return Register();
}

// True if \p Opc is a Haydn conditional branch (single or two-register).
// Resolves generated members to the logical via haydnLogicalOpcode.
static bool isHaydnCondBranch(unsigned Opc) {
  Opc = haydnLogicalOpcode(Opc);
  return isHaydnCondBranch1Reg(Opc) || isHaydnCondBranch2Reg(Opc);
}

// True if \p LoopBB has a PHI chain the classic ModuloScheduleExpander
// cannot prove expandable: a latch-incoming defined by another PHI in the
// same block whose chain never grounds in a def inside the loop body
// (an ungrounded / cyclic shift register).
//
// CB-166 law (2026-08-27): the init-era blanket reject of EVERY
// phi-of-phi latch edge also rejected every sliding-window FIR loop
// (J1=J2, J2=J3 delay lines) — the exact shape SMS exists to pipeline —
// so loads never rode with dual-MAC bodies. The classic expander in this
// tree DOES walk phi-referencing-another-phi in updateInstruction
// (ModuloSchedule.cpp "If the Phi references another Phi" Indirects loop,
// grounded by getInitPhiReg defaults), and true PHI cycles are already
// rejected upstream by MachinePipeliner::canPipelineLoop -> hasPHICycle.
// Fail closed only on the ungrounded remainder: a latch chain that
// cycles or whose definition leaves the loop block.
static bool hasUngroundedPhiChain(MachineBasicBlock *LoopBB) {
  MachineRegisterInfo &MRI = LoopBB->getParent()->getRegInfo();
  for (const MachineInstr &MI : LoopBB->phis()) {
    Register LatchIn = getPHILatchIncoming(MI, LoopBB);
    if (!LatchIn.isVirtual())
      continue;
    // Walk the latch chain. Every hop must stay a PHI of this block; the
    // walk grounds when the next value is defined by a non-PHI inside the
    // loop (the body def the expander clones per stage). A revisit is a
    // cycle (hasPHICycle also rejects it upstream; the local guard keeps
    // this law self-contained), and a def outside the block is not
    // expandable from here.
    SmallPtrSet<const MachineInstr *, 8> Seen;
    const MachineInstr *Def = MRI.getVRegDef(LatchIn);
    while (Def && Def->isPHI() && Def->getParent() == LoopBB) {
      if (!Seen.insert(Def).second)
        return true; // phi cycle through the latch chain
      Register Next = getPHILatchIncoming(*Def, LoopBB);
      if (!Next.isVirtual())
        break; // physical/unknown latch operand — leave to the expander
      Def = MRI.getVRegDef(Next);
      if (Def && Def->getParent() != LoopBB)
        return true; // chain definition leaves the loop block
    }
  }
  return false;
}

// Analyze a countable single-BB loop for the MachinePipeliner.
// This mirrors the ARM structure (ARMBaseInstrInfo.cpp:6770): identify the
// conditional terminator (EndLoop), the comparison that defines its
// condition register (CmpMI), and a runtime trip-count register (TripCountReg).
// The conditional branch may target either the loop (back-edge) or the exit
// (fall-through / B-self) -- both shapes are accepted, matching the loops
// produced by LSR on Haydn. Trip-count computation is delegated entirely to
// the generic ModuloScheduleExpander via createTripCountGreaterCondition
// adjustTripCount, which ALWAYS emit a runtime comparison and return nullopt.
// There is NO static-trip-count shortcut: a hand-rolled `(limit-init)/step`
// derivation was the Blocker-1 silent-wrong-code root cause. Loops
// where no usable runtime trip-count register can be found are REJECTED
// (return false), exactly like ARM returns nullptr when it cannot recognize
// the comparison.
// IV detection runs at the MachinePipeliner, which executes PRE-PHIElimination
// the loop block STILL contains its PHIs. The loop-carried IV is a PHI
// whose latch-incoming value is the IV's own bump:
// bb.loop:
// %iv = PHI %init, %preheader, %bump, %bb.loop; the IV
// %bump = ADD32 %iv, %step; or ADDI32 / SUB32
// %cmp = SEQ32 %bump, %limit
// We identify the IV from the comparison's own operands via findInductionVar:
// the compare source that is an induction-step bump feeding a PHI's
// latch-incoming (or, symmetrically, a PHI whose latch-incoming is such a
// bump) is the IV. The earlier "copy-chain" diagnosis was WRONG: it
// assumed the pipeliner sees post-PHIElimination COPY cycles, but the
// pipeliner never runs that late -- the copy-chain code was dead and every
// real loop was over-rejected. This PHI-form rework is the first
// recognizer to actually match the MIR the pipeliner sees.
// Trip-count derivation : the source depends on IV direction.
// For a DECREMENTING IV (step < 0, e.g. LSR's `iv += -1` until `SEQ32 iv, 0`
// the shape every real DSP loop lowers to), TripCountReg is the IV's
// preheader *init* (the IV counts N, N-1,..., 0). For an INCREMENTING IV
// (`SLT32 iv, limit`), TripCountReg is the compare's non-IV operand. Taking
// the compare's non-IV operand UNCONDITIONALLY was the matrix_test crash +
// silent-wrong-code root cause: for decrementing loops that operand is a
// function-wide materialized zero (`ADDI32 $r0, 0`), and adjustTripCount's
// replaceRegWith on it corrupted every compare/PHI-init in the function
// while createTripCountGreaterCondition emitted `SLT32 0, TC+1` (always-true
// so the guard never disabled the kernel). AIE's DownCountLoop resolves the
// trip count from the IV init the same way.
// Returns true and fills outs on success; false if the loop is not
// analyzable for SMS. See (rework)/ and lessons
static bool analyzeSimpleLoop(MachineBasicBlock *LoopBB,
                              const MCInstrInfo &MII, MachineInstr *&EndLoop,
                              MachineInstr *&CmpMI, MachineInstr *&InvertMI,
                              Register &TripCountReg) {
  MachineFunction *MF = LoopBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  EndLoop = nullptr;
  CmpMI = nullptr;
  InvertMI = nullptr;
  TripCountReg = Register();

  // Decline SMS on ungrounded PHI chains — see hasUngroundedPhiChain.
  if (hasUngroundedPhiChain(LoopBB)) {
    LLVM_DEBUG(dbgs() << "SMS: reject loop with ungrounded PHI chain "
                         "(ModuloScheduleExpander PHI rewrite unsafe)\n");
    return false;
  }

  // Reject loops containing a CALL: control leaves the loop, so the call
  // cannot be safely moved across SMS stages (-REWORK). This
  // mirrors AIE's hasLockInstruction early bail (AIEBaseInstrInfo.cpp:1413)
  // AIE rejects ONLY lock instructions here, not generic side effects.
  //
  // We deliberately do NOT reject on hasUnmodeledSideEffects: every Haydn
  // `_S<k>` opcode carries MCID::UnmodeledSideEffects as a TableGen
  // ARTIFACT (HaydnFormatInst in HaydnSlots.td does not set
  // `hasSideEffects = 0`, so pattern-less FLEX defs default to side-effecting
  // unlike the legacy FmtALU32/FmtI bases). The FLEX ALU32/compare/MAC ops
  // the Selector emits are genuinely pure (no real side effect), so treating
  // the flag as real rejected EVERY subsequent loop at this scan before the
  // recognizer even reached its opcode-key checks -- the real reason 's
  // opcode-strip helper did not unblock the naive path. isCall is the one
  // reliably-unsafe signal for the body. hasOrderedMemoryRef is also NOT
  // used: it returns true whenever memoperands are empty (common pre-RA)
  // spuriously rejecting every ordinary load loop. (The latent.td gap
  // HaydnFormatInst should set `hasSideEffects = 0` for pure-compute FLEX
  // formats -- is recorded as a follow-up; it benefits other passes too.)
  for (const MachineInstr &MI : *LoopBB) {
    if (MI.isTerminator() || MI.isPHI() || MI.isDebugInstr())
      continue;
    if (MI.isCall())
      return false;
  }

  // Find the conditional terminator. Single-BB loops on Haydn have either:
  // (a) cond_br Reg, LoopBB (conditional back-edge), or
  // (b) cond_br Reg, ExitBB; B LoopBB (conditional exit + B-self).
  // getFirstTerminator returns the first terminator in either layout;
  // in case (b) the conditional comes first, in (a) it may be the only one.
  for (MachineInstr &MI : LoopBB->terminators()) {
    if (isHaydnCondBranch(MI.getOpcode())) {
      EndLoop = &MI;
      break;
    }
  }
  if (!EndLoop)
    return false;

  // The branch's condition register is operand 0. It must be a virtual
  // register for us to trace it back to a defining comparison.
  if (EndLoop->getNumOperands() == 0 || !EndLoop->getOperand(0).isReg())
    return false;
  Register CondReg = EndLoop->getOperand(0).getReg();
  if (!CondReg.isVirtual())
    return false;

  // Find the SEQ32/SLT32/SLTU32 comparison that sets the branch condition
  // register, defined within the loop. Without it we cannot identify the
  // runtime trip-count (limit) register, so reject -- like ARM returning
  // nullptr when it cannot recognize the comparison.
  MachineInstr *CondDef = MRI.getVRegDef(CondReg);
  if (!CondDef || CondDef->getParent() != LoopBB)
    return false;
  // Accept both the logical compare opcodes and their generated members:
  // GISel InstructionSelect may emit Format E members.
  unsigned CondOpc = haydnLogicalOpcode(CondDef->getOpcode());
  // GISel emitInvert01 / CondOpt Pattern B (logical-not of a 0/1 cmp):
  //   SEQ/SLT/SLTU rd, a, b
  //   XORI32       re, rd, 1
  //   BNEZ/BEQZ    re, ...
  // After branch-polarity canonicalization almost every countable latch
  // looks like this. Peel the invert so SMS keys on the real compare;
  // without the peel, analyzeLoop rejects 100% of real naive-path loops.
  // Keep InvertMI so shouldIgnoreForPipelining pins the whole control chain
  // at stage 0 (peeling alone left XORI schedulable → stage-1 exit skew).
  if (CondOpc == Haydn::XORI32 && CondDef->getNumOperands() >= 3 &&
      CondDef->getOperand(1).isReg() && CondDef->getOperand(2).isImm() &&
      CondDef->getOperand(2).getImm() == 1) {
    Register CmpSrc = CondDef->getOperand(1).getReg();
    if (!CmpSrc.isVirtual())
      return false;
    MachineInstr *MaybeCmp = MRI.getVRegDef(CmpSrc);
    if (!MaybeCmp || MaybeCmp->getParent() != LoopBB)
      return false;
    InvertMI = CondDef;
    CondDef = MaybeCmp;
    CondOpc = haydnLogicalOpcode(CondDef->getOpcode());
  }
  if (CondOpc != Haydn::SEQ32 && CondOpc != Haydn::SLT32 &&
      CondOpc != Haydn::SLTU32)
    return false;
  CmpMI = CondDef;

  // Identify the IV from the comparison's two source operands: the IV is the
  // operand that is an induction-step bump feeding a PHI's latch-incoming
  // (PHI-form -- the pipeliner runs PRE-PHIElimination; see). We also
  // recover the IV's defining PHI and the signed step (to distinguish
  // incrementing vs decrementing IVs -- critical for trip-count derivation).
  Register SrcA = (CmpMI->getOperand(1).isReg())
                      ? CmpMI->getOperand(1).getReg()
                      : Register();
  Register SrcB = (CmpMI->getOperand(2).isReg())
                      ? CmpMI->getOperand(2).getReg()
                      : Register();

  Register IVReg;
  Register LimitReg;
  MachineInstr *IVPhi = nullptr;
  int64_t Step = 0;
  Register IVA, IVB;
  MachineInstr *PhiA = nullptr, *PhiB = nullptr;
  int64_t StepA = 0, StepB = 0;
  if (SrcA.isValid())
    IVA = findInductionVar(MRI, MII, LoopBB, SrcA, PhiA, StepA);
  if (SrcB.isValid())
    IVB = findInductionVar(MRI, MII, LoopBB, SrcB, PhiB, StepB);

  // Reject ambiguous loops where BOTH compare operands look like induction
  // variables -- the trip-count operand would be ill-defined.
  if (IVA.isValid() && IVB.isValid())
    return false;

  if (IVA.isValid()) {
    IVReg = IVA;
    IVPhi = PhiA;
    Step = StepA;
    LimitReg = SrcB;
  } else if (IVB.isValid()) {
    IVReg = IVB;
    IVPhi = PhiB;
    Step = StepB;
    LimitReg = SrcA;
  } else {
    return false;
  }

  // Proven trip-count residual only (soft branch is not itself disqualifying —
  // AIE DownCountLoop is one). Unproven invents are rejected:
  // * non-unit step (|Step| != 1): TC is not init/limit without division;
  // * decrementing non-zero limit: TC is not IV init;
  // * incrementing non-zero init: LimitReg is not TC (pointer scan / i=k..n).
  // DECREMENTING unit step to zero: TC = IV preheader init (counts N..1).
  // INCREMENTING unit step with init==0: TC = LimitReg (common LSR shape).
  // Product multi-stage remains post-RA only; this path only feeds StageCount
  // ==1 bare logical residual or StageCount>1 containment reject.

  // Compile-time zero? Walk COPY / ADDI r0,0 / LOADI32 0 / phys R0.
  // (getHaydnConstantImm lives in a later anon namespace — keep local walker.)
  auto isProvenZeroReg = [&](Register R) -> bool {
    if (!R.isValid())
      return false;
    if (R.isPhysical())
      return R == Haydn::R0;
    if (!R.isVirtual())
      return false;
    const MachineInstr *Def = MRI.getVRegDef(R);
    for (unsigned I = 0; I < 8 && Def; ++I) {
      if (Def->isCopy() && Def->getOperand(1).isReg()) {
        Register Src = Def->getOperand(1).getReg();
        if (Src.isPhysical())
          return Src == Haydn::R0;
        if (!Src.isVirtual())
          return false;
        Def = MRI.getVRegDef(Src);
        continue;
      }
      unsigned Opc = Def->getOpcode();
      if (Opc == Haydn::LOADI32 && Def->getOperand(1).isImm() &&
          Def->getOperand(1).getImm() == 0)
        return true;
      if ((Opc == Haydn::ADDI32 || Opc == Haydn::ADDI32_W) &&
          Def->getNumOperands() >= 3 && Def->getOperand(1).isReg() &&
          Def->getOperand(2).isImm() && Def->getOperand(2).getImm() == 0) {
        Register Base = Def->getOperand(1).getReg();
        if (Base.isPhysical() && Base == Haydn::R0)
          return true;
      }
      return false;
    }
    return false;
  };

  // Non-unit step: trip count is not init or LimitReg without inventing
  // (limit-init)/step. Fail closed — AIE requires a recoverable const step
  // and Haydn only treats |step|==1 as a proven TC source here.
  if (Step != 1 && Step != -1) {
    LLVM_DEBUG(dbgs() << "SMS: reject non-unit step=" << Step
                      << " (unproven trip count; |step|==1 only)\n");
    return false;
  }

  Register InitReg =
      IVPhi ? getPHIPreheaderIncoming(*IVPhi, LoopBB) : Register();
  if (Step == -1) {
    // Decrementing unit step: TC = IV init only when compare limit is zero.
    if (!InitReg.isValid() || !InitReg.isVirtual())
      return false;
    if (!isProvenZeroReg(LimitReg)) {
      LLVM_DEBUG(dbgs() << "SMS: reject decrementing IV with non-zero limit "
                           "(IV init is not a trip count)\n");
      return false;
    }
    TripCountReg = InitReg;
  } else {
    // Incrementing unit step: LimitReg is TC only when IV init is 0.
    if (!LimitReg.isValid() || !LimitReg.isVirtual())
      return false;
    if (!InitReg.isValid())
      return false;
    if (!isProvenZeroReg(InitReg)) {
      LLVM_DEBUG(dbgs() << "SMS: reject incrementing IV with non-zero init "
                           "(LimitReg is not a trip count; multi-stage peel "
                           "would mis-iterate)\n");
      return false;
    }
    TripCountReg = LimitReg;
  }

  return true;
}

} // end anonymous namespace

bool HaydnInstrInfo::analyzeCountableLoop(MachineBasicBlock *LoopBB,
                                          HaydnCountableLoop &Out) const {
  MachineInstr *EndLoop = nullptr;
  MachineInstr *CmpMI = nullptr;
  MachineInstr *InvertMI = nullptr;
  Register TripCountReg;
  if (!analyzeSimpleLoop(LoopBB, *this, EndLoop, CmpMI, InvertMI,
                         TripCountReg) ||
      !EndLoop)
    return false;
  Out.EndLoop = EndLoop;
  Out.CmpMI = CmpMI;
  Out.InvertMI = InvertMI;
  Out.TripCountReg = TripCountReg;
  // The IV register is identified inside analyzeSimpleLoop via findInductionVar
  // but not surfaced today; the SMS path does not need it (it only needs the
  // trip count). Hardware-loop conversion does not need it either -- the IV PHI
  // and its bump are left intact (mirrors Hexagon: only the compare + branch are
  // removed; the IV survives because its body uses, e.g. array indices, keep it
  // live). Leave IVReg invalid; callers that need it can re-derive it.
  Out.IVReg = Register();
  return true;
}

namespace {

// Resolve a compile-time constant from a vreg, walking COPY chains.
// Peer: AIE getDefInstr + isIConst on the LoopStart trip operand.
std::optional<int64_t> getHaydnConstantImm(Register R,
                                           const MachineRegisterInfo &MRI) {
  if (!R.isVirtual())
    return std::nullopt;
  const MachineInstr *Def = MRI.getVRegDef(R);
  // Limit COPY walk.
  for (unsigned I = 0; I < 8 && Def; ++I) {
    if (Def->isCopy() && Def->getOperand(1).isReg()) {
      Register Src = Def->getOperand(1).getReg();
      if (!Src.isVirtual())
        return std::nullopt;
      Def = MRI.getVRegDef(Src);
      continue;
    }
    unsigned Opc = Def->getOpcode();
    // LOADI32 rd, imm
    if (Opc == Haydn::LOADI32 && Def->getOperand(1).isImm())
      return Def->getOperand(1).getImm();
    // ADDI32 / ADDI32_W rd, r0, imm  (materialize small constants)
    if ((Opc == Haydn::ADDI32 || Opc == Haydn::ADDI32_W) &&
        Def->getNumOperands() >= 3 && Def->getOperand(1).isReg() &&
        Def->getOperand(2).isImm()) {
      Register Base = Def->getOperand(1).getReg();
      if (Base.isPhysical() && Base == Haydn::R0)
        return Def->getOperand(2).getImm();
    }
    break;
  }
  return std::nullopt;
}

// AIE ZeroOverheadLoop::accept MinTripCount derivation:
// pragma/CL min trip, else constant feeding LoopStart.
// Without a known MinTripCount > 1, ZOL SMS is refused (cannot guard) —
// UNLESS the LoopStart count operand is a runtime value (CB-166): then the
// expander guard contract can emit a dynamic per-prologue condition on that
// register (Hexagon J2_loop0r law, HexagonInstrInfo.cpp
// HexagonPipelinerLoopInfo::createTripCountGreaterCondition), and
// OutTripReg below carries it. The register is the value SET_HWLOOP_F2_W
// will consume (Role A expansion reads LoopStart operand 0), so the guard
// tests exactly the count the hardware will decrement.
int64_t computeZOLMinTripCount(MachineInstr *LoopStartMI,
                               MachineBasicBlock *LoopBB,
                               Register *OutTripReg = nullptr) {
  int64_t MinTC = 0;
  if (OutTripReg)
    *OutTripReg = Register();

  // Optional floor from -haydn-loop-min-tripcount (AIE aie-loop-min-tripcount).
  if (HaydnLoopMinTripCount > 0)
    MinTC = HaydnLoopMinTripCount;

  // Constant trip from LoopStart's source register.
  if (LoopStartMI && LoopStartMI->getNumOperands() >= 1 &&
      LoopStartMI->getOperand(0).isReg()) {
    const MachineRegisterInfo &MRI =
        LoopStartMI->getParent()->getParent()->getRegInfo();
    Register Src = LoopStartMI->getOperand(0).getReg();
    if (auto C = getHaydnConstantImm(Src, MRI)) {
      // LoopStart adj is the SMS delta (starts 0); InitVal is the HW trip.
      if (*C > MinTC)
        MinTC = *C;
    } else if (OutTripReg && Src.isVirtual()) {
      // Runtime trip: the count register is guard-able (dynamic law above).
      // Fail closed when its def is not reachable in this function.
      *OutTripReg = MRI.getVRegDef(Src) ? Src : Register();
    }
  }

  (void)LoopBB; // reserved for future llvm.loop MD min-trip (AIE MD path)
  return MinTC;
}

} // namespace

namespace {

//===----------------------------------------------------------------------===//
// SMS-HOOK fail-closed + SMS-RESMII oracle
//===----------------------------------------------------------------------===//
// Restriction catalog: HaydnResourceRestrictionClasses.h
//   class 1 — same-issue-cycle format/capacity (ResourceCycle / PackLegality)
//   class 2 — DDG / operand latency / SMS mutations
//   class 3 — anonymous cross-cycle capacity (product empty)
// HaydnResourceCycle proves class-1 format + descriptor-derived ports +
// ARCTAN/SIN_COS alone. It cannot see:
//   * class-3 anonymous cross-cycle capacity (multi-cycle itinerary stages
//     without a corresponding DDG edge / approved shared hook);
//   * operand-dependent format predicates not encoded in MCInstrDesc.
// SMS-RESMII (exact ResMII oracle vs greedy/DFA) fail-closes on positive
// overestimate in analyzeLoopForPipelining after this HOOK scan.
// Qual-kernel packability is metrics/fail-closed here; pre-RA same-cycle
// BUNDLE materialize is permanently off (StageCount>1 containment). Neither
// RESMII overestimate nor missing groups is repaired inside ResourceCycle.
// Fail closed here via the existing analyzeLoopForPipelining rejection (AIE
// precedent) so SMS never product-enables unsupported resource classes or
// format-oracle overestimates.
//
// Product pin ProductCrossCycleCapacityEnabled=false: all product InstrStage
// rows use cycles==1; the multi-cycle scan is the hard gate when a multi-cycle
// stage lands. Draft ARCTAN/SIN_COS multi-cycle (uimm4+2) is not product-
// enabled (issue-alone only — class 1).
static bool haydnHasOperandDependentFormatPredicate(const MachineInstr &MI) {
  // Catalog pin ProductOperandDependentFormatPredicateEnabled=false: no product
  // opcode requires an MI-only format predicate beyond what MID placement can
  // see. Keep this hook so SMS-HOOK stays fail-closed when such ops land —
  // do not invent predicates here. When the catalog pin is raised, register
  // real checks before returning true.
  using namespace haydn::restriction;
  (void)MI;
  if (!ProductOperandDependentFormatPredicateEnabled)
    return false;
  return false;
}

static bool haydnLoopHasUnsupportedSMSResources(const MachineBasicBlock &LoopBB,
                                                const HaydnInstrInfo &TII) {
  using namespace haydn::restriction;

  // Positive / bisect path for SMS-HOOK fail-closed (see ForceSMSHookReject).
  // DEBUG_WITH_TYPE("pipeliner") so sms-* lits with -debug-only=pipeliner see
  // the gate line (file DEBUG_TYPE is haydn-instr-info).
  if (ForceSMSHookReject) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "SMS-HOOK: reject — forced by -haydn-sms-hook-force-reject\n";
    });
    return true;
  }

  // II-wrap false-accept positive / bisect (see ForceSMSHookIIWrapReject).
  // Logs the issue-time-only hazard so lits pin plan §8.4 #8 without inventing
  // a multi-cycle product FU. Same analyzeLoop → nullptr gate as multi-cycle.
  if (ForceSMSHookIIWrapReject) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "SMS-HOOK: reject — II-wrap issue-time-only false-accept "
                "(forced by -haydn-sms-hook-force-iiwrap-reject; multi-cycle "
                "occupancy unsupported without approved cross-cycle hook; "
                "ProductCrossCycleCapacityEnabled=false)\n";
    });
    return true;
  }

  const MachineFunction *MF = LoopBB.getParent();
  const InstrItineraryData *Itin =
      MF ? MF->getSubtarget().getInstrItineraryData() : nullptr;

  for (const MachineInstr &MI : LoopBB) {
    if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isImplicitDef() ||
        MI.isKill() || MI.isCFIInstruction() || MI.isPHI())
      continue;

    if (haydnHasOperandDependentFormatPredicate(MI)) {
      DEBUG_WITH_TYPE("pipeliner", {
        dbgs() << "SMS-HOOK: reject — operand-dependent format predicate: "
               << MI;
      });
      return true;
    }

    // Class-3: multi-cycle FU occupancy (Required/Reserved stages spanning
    // more than one cycle) is not representable in per-modulo-cycle
    // ResourceCycle without an approved shared representation. Product pin
    // ProductCrossCycleCapacityEnabled=false → reject any stage with
    // getCycles() > ProductMaxInstrStageCycles (smsHookRejectsMultiCycleStage).
    // II-wrap: independent ResourceCycle per phase would false-accept concurrent
    // use of phases covered by (Issue+k)%II — fail closed here so product SMS
    // never relies on that gap (smsHookRejectsIIWrapFalseAccept).
    if (Itin && !Itin->isEmpty()) {
      const unsigned SchedClass = TII.get(MI.getOpcode()).getSchedClass();
      for (const InstrStage *IS = Itin->beginStage(SchedClass),
                            *E = Itin->endStage(SchedClass);
           IS != E; ++IS) {
        const unsigned Cycles = IS->getCycles();
        if (smsHookRejectsMultiCycleStage(Cycles)) {
          DEBUG_WITH_TYPE("pipeliner", {
            dbgs() << "SMS-HOOK: reject — multi-cycle itinerary stage ("
                   << Cycles
                   << " cycles) II-wrap issue-time-only false-accept "
                      "unsupported without approved cross-cycle hook "
                      "(ProductCrossCycleCapacityEnabled=false): "
                   << MI;
          });
          return true;
        }
      }
    }
  }
  return false;
}

/// SMS-PORT metrics (MOVE32-class MI-versus-descriptor):
/// Log when the body contains MOVE32 (or slot members) so qualification can
/// pin that MI and descriptor paths are both 2R1W per-field. Does **not**
/// fail-close — the retired overcount pin stays as a zero metric, not an
/// operand-dependent format predicate / SMS-HOOK reject class.
static void haydnLogSMSPortMiVsDescDifferential(const MachineBasicBlock &LoopBB) {
  unsigned Move32Ops = 0;
  for (const MachineInstr &MI : LoopBB) {
    if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isImplicitDef() ||
        MI.isKill() || MI.isCFIInstruction() || MI.isPHI() || MI.isTerminator())
      continue;
    const unsigned Opc = MI.getOpcode();
    if (haydnLogicalOpcode(Opc) == Haydn::MOVE32)
      ++Move32Ops;
  }
  if (Move32Ops == 0)
    return;

  DEBUG_WITH_TYPE("pipeliner", {
    dbgs() << "SMS-PORT: move32-class ops=" << Move32Ops
           << " desc_shape=" << HaydnMove32ClassDescShapeGprReads << "R"
           << HaydnMove32ClassDescShapeGprWrites << "W"
           << " mi_repeated_src=" << HaydnMove32ClassMiRepeatedSrcGprReads
           << "R" << HaydnMove32ClassMiRepeatedSrcGprWrites
           << "W desc_overcounts_mi="
           << (haydnMove32ClassDescOvercountsMiPorts() ? 1 : 0) << "\n";
  });
}

/// GR2.1 generated-coverage fail-close (replaces the deleted exact-pack
/// SMS-HANDOFF gate). The ONLY format fact the pre-RA seat may consult is
/// coverage: every packable body logical must be encodable in at least one
/// non-NOP golden-admitted Format E row (findAltSpan over the normalized
/// catalog name — the same generated coverage oracle
/// HaydnPipelinerLoopInfo::estimateCyclesAcrossAvailableFormats uses). A miss
/// is an RA-legal tuple with no alternate: reject analyzeLoopForPipelining
/// fail-closed. This is coverage data, not exact packability — whether covered
/// bodies co-issue is post-RA business (cycle-slip + universal singleton
/// fallback). Witness-free; no rows/units/ports are matched here.
/// \returns false when the loop must be rejected (uncovered body logical).
static bool haydnCheckSMSCoverage(const TargetInstrInfo &TII,
                                  const MachineBasicBlock &LoopBB) {
  for (const MachineInstr &MI : LoopBB) {
    if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isImplicitDef() ||
        MI.isKill() || MI.isCFIInstruction() || MI.isPHI() || MI.isTerminator())
      continue;
    // Same catalog-name normalization as the Kind C estimate (post-inc /
    // load-store families use catalog occupancy names).
    const std::string CatalogName = haydn::format_e::peelLogicalOpcodeName(
        TII.getName(MI.getOpcode()));
    if (!haydn::format_e::findAltSpan(CatalogName.c_str())) {
      DEBUG_WITH_TYPE("pipeliner", {
        // peelLogicalOpcodeName fails closed on unknown spellings (returns
        // ""; e.g. the LOADI32 pseudo is not a FormatEAltSpans key), so the
        // reject names the raw MI opcode — an empty catalog name would hide
        // which body logical rejected.
        dbgs() << "SMS-HANDOFF: reject — body logical '"
               << (CatalogName.empty() ? TII.getName(MI.getOpcode())
                                       : StringRef(CatalogName))
               << "' has no generated non-NOP alternate (coverage fail-close; "
                  "post-RA packability is not a pre-RA matching input)\n";
      });
      return false;
    }
  }
  DEBUG_WITH_TYPE("pipeliner", {
    dbgs() << "SMS-HANDOFF: coverage ok (every packable body logical has a "
              "generated non-NOP alternate; Kind C advisory in "
              "shouldUseSchedule)\n";
  });
  return true;
}

} // namespace

std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo>
HaydnInstrInfo::analyzeLoopForPipelining(MachineBasicBlock *LoopBB) const {
 // SMS-HOOK: fail closed before any ZOL/naive form recognition when the
  // body needs resources the approved ResourceCycle adapter cannot prove.
  if (haydnLoopHasUnsupportedSMSResources(*LoopBB, *this)) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "SMS: analyzeLoopForPipelining fail-closed "
                "(unsupported SMS-HOOK resource class)\n";
    });
    return nullptr;
  }

  // GR2.1: the exact pre-RA oracle seats are deleted (SMS-RESMII greedy-vs-
  // exhaustive differential, SMS-FORMAT RC↔pure-exact differential, SMS-QOR/
  // SMS-IPC exact format/port floors, SMS-HANDOFF exact-pack gate). The pre-RA
  // seat reasons on generated IssueWidth (Kind A ResourceCycle) plus the
  // witness-free Kind C floor only; ResMII itself is the generic pipeliner's
  // calculateResMIIDFA over the Kind-A cycle. The one surviving fail-close is
  // generated COVERAGE (Kind C findAltSpan) — exact packability is post-RA
  // business (cycle-slip + universal singleton fallback).
  if (!haydnCheckSMSCoverage(*this, *LoopBB)) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "SMS: analyzeLoopForPipelining fail-closed "
                "(SMS-HANDOFF uncovered body logical)\n";
    });
    return nullptr;
  }

  // SMS-PORT: metrics-only MOVE32-class MI-versus-descriptor differential.
  // Placement (MID) overcounts repeated sources; ResMII (MI) is exact. Does
  // not fail-close — intentional conservative placement.
  haydnLogSMSPortMiVsDescDifferential(*LoopBB);

  // Check for ZOL (Zero-Overhead Loop) form. The IR-level HardwareLoops pass
  // runs before IRTranslator, so it has ALREADY converted every countable
  // single-BB loop to LoopStart (preheader) + PseudoLoopEnd (latch) by the time
  // SMS runs. SMS pipelines it via the ZOL PipelinerLoopInfo
  // (adjustTripCount edits LoopStart's $adj operand).
  // haydnZOLPipeliningEnabled() defaults ON ("SMS runs PRE-RA on ZOL form");
  // the cl::opt remains for emergency disable. When off, SMS skips ZOL loops —
  // they still form hwloops via the IR-level pass, just without software
  // pipelining.
  MachineBasicBlock::iterator TermIt = LoopBB->getFirstTerminator();
  if (TermIt != LoopBB->end() &&
      TermIt->getOpcode() == Haydn::PseudoLoopEnd) {
    if (!haydnZOLPipeliningEnabled())
      return nullptr; // ZOL loop — don't pipeline (keep hwloop, skip SMS).
    // Same call rejection as the naive path below: soft-float libcalls
    // (e.g. JAL_W __addsf3 in pr28982a/b) must not SMS across ZOL stages —
    // expander/FixupHwLoops then walk bundled children as bundle iterators
    // and assert. Keep hwloop form; skip software pipeline only.
    for (const MachineInstr &MI : *LoopBB) {
      if (MI.isTerminator() || MI.isPHI() || MI.isDebugInstr())
        continue;
      if (MI.isCall()) {
        LLVM_DEBUG(dbgs() << "SMS: reject ZOL loop with call "
                             "(cannot pipeline across call stages)\n");
        return nullptr;
      }
    }
    // Same PHI-chain hazard as the naive path (hasUngroundedPhiChain).
    if (hasUngroundedPhiChain(LoopBB)) {
      LLVM_DEBUG(dbgs() << "SMS: reject ZOL loop with ungrounded PHI chain "
                           "(ModuloScheduleExpander PHI rewrite unsafe)\n");
      return nullptr;
    }
    MachineInstr *Term = &*TermIt;
    // Find LoopStart in the preheader (or dominating block).
    MachineBasicBlock *Pred = LoopBB->getSinglePredecessor();
    if (!Pred) {
      for (MachineBasicBlock *P : LoopBB->predecessors()) {
        if (P != LoopBB) { Pred = P; break; }
      }
    }
    if (Pred) {
      for (MachineInstr &MI : *Pred) {
        if (MI.getOpcode() == Haydn::LoopStart) {
          // Analyze every well-formed ZOL loop (LoopStart + PseudoLoopEnd).
          // MinTripCount may be 0 (variable trip / unknown) or small: that is
          // NOT an analyze failure. shouldUseSchedule refuses multi-stage SMS
          // when MinTC is unknown or too small to cover peeled prologues AND
          // no runtime trip register is available for a dynamic guard
          // (CB-166: a runtime count register makes the per-prologue guard
          // emittable — the Hexagon J2_loop0r law — so the static MinTC gate
          // no longer refuses). This preserves the analyzability contract
          // (swpipeline-zol-countable-analyzable.ll) while keeping SMS
          // product-safe for memcpy-class variable-trip loops.
          Register ZOLTripReg;
          int64_t MinTC = computeZOLMinTripCount(&MI, LoopBB, &ZOLTripReg);
          if (MinTC <= 1) {
            LLVM_DEBUG(dbgs() << "ZOL: analyze OK but MinTripCount=" << MinTC
                              << (ZOLTripReg.isValid()
                                      ? " (runtime trip reg; dynamic guard "
                                        "available in shouldUseSchedule)\n"
                                      : " (SMS schedules gated in "
                                        "shouldUseSchedule)\n"));
          }
          MachineFunction *MF = LoopBB->getParent();
          return std::make_unique<HaydnPipelinerLoopInfo>(
              MF, this, Term, &MI, MinTC, ZOLTripReg);
        }
      }
    }
    // ZOL loop without findable LoopStart — don't pipeline.
    return nullptr;
  }

  // Counted soft residual path (SEQ32/SLT32 + BNEZ/BEQZ). Peer law: soft
  // branch is not itself disqualifying (AIE DownCountLoop is one); Haydn
  // requires a proven trip count: unit step only, decrementing-to-zero (TC =
  // IV init) or incrementing with init==0 (TC = LimitReg). Non-unit step,
  // non-zero countdown limit, and non-zero init are rejected as unproven
  // invents in analyzeSimpleLoop. Product multi-stage remains post-RA only.
  HaydnCountableLoop L;
  if (!analyzeCountableLoop(LoopBB, L))
    return nullptr;

  MachineFunction *MF = LoopBB->getParent();
  return std::make_unique<HaydnPipelinerLoopInfo>(
      MF, this, L.EndLoop, L.CmpMI, L.TripCountReg, L.InvertMI);
}

void HaydnInstrInfo::insertNoop(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const {
  // Mirrors HexagonInstrInfo::insertNoop (HexagonInstrInfo.cpp:1667-1671).
  // Used by the post-RA scheduler's leaveMBB to pad idle cycles (Phase B2).
  DebugLoc DL;
  BuildMI(MBB, MI, DL, get(Haydn::NOP));
}

//===----------------------------------------------------------------------===//
// Pre/post-inc/dec load/store addressing-mode hooks (SMS + mem clustering)
//===----------------------------------------------------------------------===//
//
// Operand layouts (explicit defs only; no implicit):
//
//   Fused LOAD POST/PRE IMM/REG:
//     (outs Data:$rt, GPR32:$rs_wb), (ins GPR32:$rs, ImmOrReg:$delta)
//     indices: rt=0, rs_wb=1, rs=2, delta=3
//
//   Fused STORE POST/PRE IMM/REG (incl. ST32_POST / ST64_POST):
//     (outs GPR32:$rs_wb), (ins Data:$rt, GPR32:$rs, ImmOrReg:$delta)
//     indices: rs_wb=0, rt=1, rs=2, delta=3
//
//   Pseudo LD*_POST_INC:
//     (outs Data:$rt), (ins GPR32:$base, i32imm:$stride_bytes, i32imm:$offset)
//     indices: rt=0, base=1, stride=2, offset=3
//
//   Pseudo ST*_POST_INC:
//     (outs), (ins Data:$rt, GPR32:$base, i32imm:$stride_bytes, i32imm:$offset)
//     indices: rt=0, base=1, stride=2, offset=3
//
//   Plain LD32/LD64: (outs rt), (ins base, imm_offset) → base=1, off=2
//   Plain ST32/ST64: (outs), (ins rt, base, imm_offset) → base=1, off=2
//
// Scaled IMM is element index; byte delta = imm << ScaleShift.
// Negative imm = pre/post-decrement. REG forms have no compile-time delta.
//===----------------------------------------------------------------------===//

namespace {

enum class HaydnUpdateAM {
  None,
  PostImm,       // access then base += imm<<scale
  PreImm,        // base += imm<<scale then access
  PostReg,       // access then base += reg
  PreReg,        // base += reg then access
  PostIncPseudo, // LD/ST*_POST_INC (byte stride + access offset)
};

// Classify by opcode name so every logical + Flex/slot private variant is
// covered without enumerating hundreds of enum values.
HaydnUpdateAM classifyUpdateAM(const TargetInstrInfo &TII, unsigned Opc) {
  switch (Opc) {
  case Haydn::LD32_POST_INC:
  case Haydn::LD64_POST_INC:
  case Haydn::ST32_POST_INC:
  case Haydn::ST64_POST_INC:
    return HaydnUpdateAM::PostIncPseudo;
  default:
    break;
  }

  StringRef N = TII.getName(Opc);
  // Order matters: POST_INC already handled; POST_IMM before bare POST.
  if (N.contains("POST_IMM"))
    return HaydnUpdateAM::PostImm;
  if (N.contains("PRE_IMM"))
    return HaydnUpdateAM::PreImm;
  if (N.contains("POST_REG"))
    return HaydnUpdateAM::PostReg;
  if (N.contains("PRE_REG"))
    return HaydnUpdateAM::PreReg;
  // Short codegen aliases: LD32_POST / LD64_POST / ST32_POST / ST64_POST
  // (and slot forms that keep the bare _POST suffix without _IMM).
  if (N.contains("POST") && !N.contains("PRE")) {
    // Exclude non-addr-mode POST names if any appear later.
    if (N.contains("LD") || N.contains("ST") || N.contains("LW") ||
        N.contains("SW") || N.contains("SDW") || N.contains("SHW") ||
        N.contains("SB") || N.contains("LHW") || N.contains("LBS") ||
        N.contains("LBU"))
      return HaydnUpdateAM::PostImm;
  }
  return HaydnUpdateAM::None;
}

/// Byte width of a logical LS opcode. 0 = unknown (fail closed).
///
/// One oracle for SMS, mem clustering, and areMemAccessesTriviallyDisjoint:
/// scaled immediates are element indices (EA = base + imm * width) except
/// FrameIndex extras and *_POST_INC pseudos, which are already bytes.
/// D_LW is a 32-bit DR access and D_LHW is 16-bit (HaydnISelLowering
/// mem-intrinsic sizes); they are not 64-bit. Generated Format E members
/// peel to the catalog logical via logicalOpcodeOrSelf (AIE inverse of
/// AIEMCFormats::getAlternateInstsOpcode). Residual `_S*` names stay
/// fail-closed.
unsigned haydnMemAccessWidthBytes(unsigned Opc) {
  Opc = haydn::format_e::logicalOpcodeOrSelf(Opc);
  switch (Opc) {
  case Haydn::LD8:
  case Haydn::LDU8:
  case Haydn::ST8:
  case Haydn::S_LBS_POST_IMM:
  case Haydn::S_LBS_POST_REG:
  case Haydn::S_LBS_PRE_IMM:
  case Haydn::S_LBS_PRE_REG:
  case Haydn::S_LBS_WITH_IMM:
  case Haydn::S_LBS_WITH_REG:
  case Haydn::S_LBU_POST_IMM:
  case Haydn::S_LBU_POST_REG:
  case Haydn::S_LBU_PRE_IMM:
  case Haydn::S_LBU_PRE_REG:
  case Haydn::S_LBU_WITH_IMM:
  case Haydn::S_LBU_WITH_REG:
  case Haydn::S_SB_POST_IMM:
  case Haydn::S_SB_POST_REG:
  case Haydn::S_SB_PRE_IMM:
  case Haydn::S_SB_PRE_REG:
  case Haydn::S_SB_WITH_IMM:
  case Haydn::S_SB_WITH_REG:
    return 1;

  case Haydn::LD16:
  case Haydn::LDU16:
  case Haydn::ST16:
  case Haydn::S_LHWS_POST_IMM:
  case Haydn::S_LHWS_POST_REG:
  case Haydn::S_LHWS_PRE_IMM:
  case Haydn::S_LHWS_PRE_REG:
  case Haydn::S_LHWS_WITH_IMM:
  case Haydn::S_LHWS_WITH_REG:
  case Haydn::S_LHWU_POST_IMM:
  case Haydn::S_LHWU_POST_REG:
  case Haydn::S_LHWU_PRE_IMM:
  case Haydn::S_LHWU_PRE_REG:
  case Haydn::S_LHWU_WITH_IMM:
  case Haydn::S_LHWU_WITH_REG:
  case Haydn::S_SHW_POST_IMM:
  case Haydn::S_SHW_POST_REG:
  case Haydn::S_SHW_PRE_IMM:
  case Haydn::S_SHW_PRE_REG:
  case Haydn::S_SHW_WITH_IMM:
  case Haydn::S_SHW_WITH_REG:
  case Haydn::D_LHW_POST_IMM:
  case Haydn::D_LHW_POST_REG:
  case Haydn::D_LHW_PRE_IMM:
  case Haydn::D_LHW_PRE_REG:
  case Haydn::D_LHW_WITH_IMM:
  case Haydn::D_LHW_WITH_REG:
  case Haydn::D_SHW_POST_IMM:
  case Haydn::D_SHW_POST_REG:
  case Haydn::D_SHW_PRE_IMM:
  case Haydn::D_SHW_PRE_REG:
  case Haydn::D_SHW_WITH_IMM:
  case Haydn::D_SHW_WITH_REG:
    return 2;

  case Haydn::LD32:
  case Haydn::ST32:
  case Haydn::LD32_POST:
  case Haydn::ST32_POST:
  case Haydn::LD32_POST_INC:
  case Haydn::ST32_POST_INC:
  case Haydn::LD32_REG_M0S0LS:
  case Haydn::ST32_REG_M0S0LS:
  case Haydn::S_LW_POST_IMM:
  case Haydn::S_LW_POST_REG:
  case Haydn::S_LW_PRE_IMM:
  case Haydn::S_LW_PRE_REG:
  case Haydn::S_LW_WITH_IMM:
  case Haydn::S_LW_WITH_REG:
  case Haydn::S_LW_BREV_IMM:
  case Haydn::S_LW_BREV_REG:
  case Haydn::S_SW_POST_IMM:
  case Haydn::S_SW_POST_REG:
  case Haydn::S_SW_PRE_IMM:
  case Haydn::S_SW_PRE_REG:
  case Haydn::S_SW_WITH_IMM:
  case Haydn::S_SW_WITH_REG:
  case Haydn::S_SW_BREV_IMM:
  case Haydn::S_SW_BREV_REG:
  case Haydn::D_LW_POST_IMM:
  case Haydn::D_LW_POST_REG:
  case Haydn::D_LW_PRE_IMM:
  case Haydn::D_LW_PRE_REG:
  case Haydn::D_LW_WITH_IMM:
  case Haydn::D_LW_WITH_REG:
  case Haydn::D_SW_L_POST_IMM:
  case Haydn::D_SW_L_POST_REG:
  case Haydn::D_SW_L_PRE_IMM:
  case Haydn::D_SW_L_PRE_REG:
  case Haydn::D_SW_L_WITH_IMM:
  case Haydn::D_SW_L_WITH_REG:
  case Haydn::D_SW_H_POST_IMM:
  case Haydn::D_SW_H_POST_REG:
  case Haydn::D_SW_H_PRE_IMM:
  case Haydn::D_SW_H_PRE_REG:
  case Haydn::D_SW_H_WITH_IMM:
  case Haydn::D_SW_H_WITH_REG:
    return 4;

  case Haydn::LD64:
  case Haydn::ST64:
  case Haydn::LD64_POST:
  case Haydn::ST64_POST:
  case Haydn::LD64_POST_INC:
  case Haydn::ST64_POST_INC:
  case Haydn::LD64_REG_M0S0LS:
  case Haydn::ST64_REG_M0S0LS:
  case Haydn::D_LDW_POST_IMM:
  case Haydn::D_LDW_POST_REG:
  case Haydn::D_LDW_PRE_IMM:
  case Haydn::D_LDW_PRE_REG:
  case Haydn::D_LDW_WITH_IMM:
  case Haydn::D_LDW_WITH_REG:
  case Haydn::D_LDW_BREV_IMM:
  case Haydn::D_LDW_BREV_REG:
  case Haydn::D_SDW_POST_IMM:
  case Haydn::D_SDW_POST_REG:
  case Haydn::D_SDW_PRE_IMM:
  case Haydn::D_SDW_PRE_REG:
  case Haydn::D_SDW_WITH_IMM:
  case Haydn::D_SDW_WITH_REG:
  case Haydn::D_SDW_BREV_IMM:
  case Haydn::D_SDW_BREV_REG:
    return 8;

  default:
    return 0;
  }
}

/// Convert a scaled LS immediate to a byte offset.
/// FrameIndex extras are LLVM byte offsets (PEI has not rewritten them).
/// Register-base fields are element indices (ISel and post-PEI).
int64_t haydnScaledImmToBytes(int64_t Imm, unsigned WidthBytes,
                              bool BaseIsFrameIndex) {
  if (BaseIsFrameIndex || WidthBytes <= 1)
    return Imm;
  return Imm * static_cast<int64_t>(WidthBytes);
}

} // namespace

bool HaydnInstrInfo::isPostIncrement(const MachineInstr &MI) const {
  HaydnUpdateAM AM = classifyUpdateAM(*this, MI.getOpcode());
  return AM == HaydnUpdateAM::PostImm || AM == HaydnUpdateAM::PostReg ||
         AM == HaydnUpdateAM::PostIncPseudo;
}

bool HaydnInstrInfo::isPreIncrement(const MachineInstr &MI) const {
  HaydnUpdateAM AM = classifyUpdateAM(*this, MI.getOpcode());
  return AM == HaydnUpdateAM::PreImm || AM == HaydnUpdateAM::PreReg;
}

bool HaydnInstrInfo::getBaseAndOffsetPosition(const MachineInstr &MI,
                                              unsigned &BasePos,
                                              unsigned &OffsetPos) const {
  unsigned Opc = MI.getOpcode();
  HaydnUpdateAM AM = classifyUpdateAM(*this, Opc);

  if (AM == HaydnUpdateAM::PostIncPseudo) {
    // LDs: [rt, base, stride, offset]; STs: [rt, base, stride, offset]
    // For SMS post-inc rewrite, OffsetPos is the *increment* field (stride).
    BasePos = 1;
    OffsetPos = 2;
    if (MI.getNumOperands() <= OffsetPos)
      return false;
    if (!MI.getOperand(BasePos).isReg() || !MI.getOperand(OffsetPos).isImm())
      return false;
    return true;
  }

  if (AM != HaydnUpdateAM::None) {
    // Fused load/store: base at 2, delta at 3.
    BasePos = 2;
    OffsetPos = 3;
    if (MI.getNumOperands() <= OffsetPos)
      return false;
    // Generic SMS (canUseLastOffsetValue / fixupRegisterOverlaps /
    // applyInstrChange) calls getImm() on OffsetPos with no isImm guard.
    // REG forms have no compile-time delta — refuse here (Hexagon peer).
    // getMemOperandsWithOffsetWidth still reports POST_REG / PRE_REG with
    // Offset=0 for clustering.
    if (!MI.getOperand(BasePos).isReg() || !MI.getOperand(OffsetPos).isImm())
      return false;
    return true;
  }

  // Plain base+imm loads/stores used by canUseLastOffsetValue rewrite.
  switch (Opc) {
  case Haydn::LD32:
  case Haydn::LD64:
    BasePos = 1;
    OffsetPos = 2;
    break;
  case Haydn::ST32:
  case Haydn::ST64:
    BasePos = 1;
    OffsetPos = 2;
    break;
  default:
    return false;
  }
  if (MI.getNumOperands() <= OffsetPos)
    return false;
  return MI.getOperand(BasePos).isReg() && MI.getOperand(OffsetPos).isImm();
}

bool HaydnInstrInfo::getIncrementValue(const MachineInstr &MI,
                                       int &Value) const {
  unsigned Opc = MI.getOpcode();
  HaydnUpdateAM AM = classifyUpdateAM(*this, Opc);

  if (AM == HaydnUpdateAM::PostImm || AM == HaydnUpdateAM::PreImm ||
      AM == HaydnUpdateAM::PostIncPseudo) {
    unsigned BasePos = 0, OffsetPos = 0;
    if (!getBaseAndOffsetPosition(MI, BasePos, OffsetPos))
      return false;
    const MachineOperand &OffOp = MI.getOperand(OffsetPos);
    if (!OffOp.isImm())
      return false;
    int64_t Imm = OffOp.getImm();
    if (AM == HaydnUpdateAM::PostIncPseudo) {
      // Pseudo stride is already in bytes (may be negative = post-dec).
      if (!isInt<32>(Imm))
        return false;
      Value = static_cast<int>(Imm);
      return true;
    }
    // Scaled element index → signed byte delta (negative = decrement).
    unsigned WidthBytes = haydnMemAccessWidthBytes(Opc);
    if (WidthBytes == 0)
      return false;
    int64_t Bytes = Imm * static_cast<int64_t>(WidthBytes);
    if (!isInt<32>(Bytes))
      return false;
    Value = static_cast<int>(Bytes);
    return true;
  }

  // Plain ADDI is the split post-inc fallback (and common IV step).
  if (Opc == Haydn::ADDI32 || Opc == Haydn::ADDI32_W) {
    const MachineOperand &ImmOp = MI.getOperand(2);
    if (!ImmOp.isImm() || !isInt<32>(ImmOp.getImm()))
      return false;
    Value = static_cast<int>(ImmOp.getImm());
    return true;
  }

  return false;
}

bool HaydnInstrInfo::getMemOperandsWithOffsetWidth(
    const MachineInstr &MI, SmallVectorImpl<const MachineOperand *> &BaseOps,
    int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
    const TargetRegisterInfo * /*TRI*/) const {
  BaseOps.clear();
  OffsetIsScalable = false;
  // Classify AM on the raw opcode so generated *_PRE_IMM_* / *_POST_* keep
  // writeback AM (peeled PRE must not look like AM=None ST32). Peel is width
  // + AM=None WITH_IMM catalog only (leaveMBB setDesc). Disjoint is AIE
  // SameValue MMO, not this oracle (RISCVInstrInfo.cpp:3522-3552 clustering).
  unsigned RawOpc = MI.getOpcode();
  unsigned Opc = haydn::format_e::logicalOpcodeOrSelf(RawOpc);
  unsigned WidthBytes = haydnMemAccessWidthBytes(Opc);
  if (WidthBytes == 0)
    return false;
  Width = LocationSize::precise(WidthBytes);
  HaydnUpdateAM AM = classifyUpdateAM(*this, RawOpc);

  auto dumpOracle = [&]() {
    LLVM_DEBUG(dbgs() << "Haydn mem oracle: " << getName(Opc)
                      << " byte-offset=" << Offset << " width=" << Width
                      << '\n');
  };

  if (AM == HaydnUpdateAM::PostIncPseudo) {
    if (!MI.mayLoad() && !MI.mayStore())
      return false;
    // Access at base+offset (op3); post-update is op2 stride (not EA offset).
    // Pseudo offset is already in bytes.
    if (MI.getNumOperands() < 4 || !MI.getOperand(1).isReg() ||
        !MI.getOperand(3).isImm())
      return false;
    BaseOps.push_back(&MI.getOperand(1));
    Offset = MI.getOperand(3).getImm();
    dumpOracle();
    return true;
  }

  if (AM == HaydnUpdateAM::PostImm || AM == HaydnUpdateAM::PostReg) {
    // Post-*: EA is [rs] (offset 0); delta is writeback only.
    if (MI.getNumOperands() < 4 || !MI.getOperand(2).isReg())
      return false;
    BaseOps.push_back(&MI.getOperand(2));
    Offset = 0;
    dumpOracle();
    return true;
  }

  if (AM == HaydnUpdateAM::PreImm) {
    // Pre-imm: EA is [rs + imm*width] relative to the pre-update base that
    // SMS tracks (the rs use before writeback).
    if (MI.getNumOperands() < 4 || !MI.getOperand(2).isReg() ||
        !MI.getOperand(3).isImm())
      return false;
    BaseOps.push_back(&MI.getOperand(2));
    Offset = haydnScaledImmToBytes(MI.getOperand(3).getImm(), WidthBytes,
                                   /*BaseIsFrameIndex=*/false);
    dumpOracle();
    return true;
  }

  if (AM == HaydnUpdateAM::PreReg) {
    // Pre-reg: EA depends on a register delta — not a fixed offset.
    if (MI.getNumOperands() < 4 || !MI.getOperand(2).isReg())
      return false;
    BaseOps.push_back(&MI.getOperand(2));
    Offset = 0;
    dumpOracle();
    return true;
  }

  // Plain logical LS and post-setDesc WITH_IMM catalog members (AM=None):
  // (ins … base, imm). Base may be FI (byte extra) or a register (element
  // index, ISel and post-PEI). Generated S_LW/S_SW members peel to these
  // cases for SMS clustering. BREV is not linear EA; WITH_REG offset is
  // not Imm — keep both fail-closed.
  switch (Opc) {
  case Haydn::LD8:
  case Haydn::LDU8:
  case Haydn::LD16:
  case Haydn::LDU16:
  case Haydn::LD32:
  case Haydn::LD64:
  case Haydn::ST8:
  case Haydn::ST16:
  case Haydn::ST32:
  case Haydn::ST64:
  case Haydn::S_LBS_WITH_IMM:
  case Haydn::S_LBU_WITH_IMM:
  case Haydn::S_LHWS_WITH_IMM:
  case Haydn::S_LHWU_WITH_IMM:
  case Haydn::S_LW_WITH_IMM:
  case Haydn::D_LHW_WITH_IMM:
  case Haydn::D_LW_WITH_IMM:
  case Haydn::D_LDW_WITH_IMM:
  case Haydn::S_SB_WITH_IMM:
  case Haydn::S_SHW_WITH_IMM:
  case Haydn::S_SW_WITH_IMM:
  case Haydn::D_SHW_WITH_IMM:
  case Haydn::D_SW_L_WITH_IMM:
  case Haydn::D_SW_H_WITH_IMM:
  case Haydn::D_SDW_WITH_IMM:
    if (MI.getNumOperands() < 3)
      return false;
    {
      const MachineOperand &Base = MI.getOperand(1);
      if ((!Base.isReg() && !Base.isFI()) || !MI.getOperand(2).isImm())
        return false;
      BaseOps.push_back(&Base);
      Offset = haydnScaledImmToBytes(MI.getOperand(2).getImm(), WidthBytes,
                                     Base.isFI());
      dumpOracle();
      return true;
    }
  default:
    return false;
  }
}

bool HaydnInstrInfo::areMemAccessesTriviallyDisjoint(
    const MachineInstr &MIa, const MachineInstr &MIb) const {
  assert(MIa.mayLoadOrStore() && "MIa must be a load or store.");
  assert(MIb.mayLoadOrStore() && "MIb must be a load or store.");

  if (MIa.hasUnmodeledSideEffects() || MIb.hasUnmodeledSideEffects() ||
      MIa.hasOrderedMemoryRef() || MIb.hasOrderedMemoryRef())
    return false;

  // Update-AM writeback: EA is [rs] after pre-update. Operand base identity
  // is the pre-update register, so a later [rs],0 looks disjoint from
  // [rs+imm] while they are the same object.
  if (classifyUpdateAM(*this, MIa.getOpcode()) != HaydnUpdateAM::None ||
      classifyUpdateAM(*this, MIb.getOpcode()) != HaydnUpdateAM::None)
    return false;

  // AIE AIEBaseInstrInfo.cpp:2085-2109 SameValue / same-PSV + MMO
  // offset+width. Distinct GEP Values that share a GPR (%p vs %q) must not
  // use operand identity: that dropped DAG store-store/store-load edges
  // MemoryEdges restamps. Unknown-address MMOs (va_list fields) have no
  // IR Value; fall through to the RISCV operand oracle.
  if (MIa.hasOneMemOperand() && MIb.hasOneMemOperand()) {
    const MachineMemOperand *MMOa = *MIa.memoperands_begin();
    const MachineMemOperand *MMOb = *MIb.memoperands_begin();
    auto CheckOverlapping = [=](int64_t OffsetA, int64_t OffsetB) {
      const LocationSize WidthA = MMOa->getSize(), WidthB = MMOb->getSize();
      const int64_t LowOffset = OffsetA < OffsetB ? OffsetA : OffsetB;
      const int64_t HighOffset = OffsetA < OffsetB ? OffsetB : OffsetA;
      const LocationSize LowWidth = (LowOffset == OffsetA) ? WidthA : WidthB;
      return LowWidth.hasValue() &&
             LowOffset + static_cast<int64_t>(LowWidth.getValue()) <=
                 HighOffset;
    };
    const int64_t MMOOffsetA = MMOa->getOffset();
    const int64_t MMOOffsetB = MMOb->getOffset();
    const Value *VALa = MMOa->getValue();
    const Value *VALb = MMOb->getValue();
    if (VALa && VALb && VALa == VALb)
      return CheckOverlapping(MMOOffsetA, MMOOffsetB);
    // Distinct GEP Values of one object (%p vs gep %p, 1). AIE SameValue is
    // pointer equality (AIEBaseInstrInfo.cpp:2105-2109); Haydn GISel keeps
    // the GEP as the MMO Value with extra offset 0. ISel LD32/ST32 accumulate
    // constant GEP offsets (RISCVInstrInfo.cpp:3455-3461) so p[0] vs p[1] is
    // SameValue and RA does not coalesce the later load dest into the
    // pointer. Catalog S_*_WITH_IMM / generated members keep pointer
    // equality — post-setDesc MemoryEdges still lengthens the chain.
    auto isGISelLogicalMemOp = [](unsigned Opc) {
      switch (Opc) {
      case Haydn::LD8:
      case Haydn::LDU8:
      case Haydn::LD16:
      case Haydn::LDU16:
      case Haydn::LD32:
      case Haydn::LD64:
      case Haydn::ST8:
      case Haydn::ST16:
      case Haydn::ST32:
      case Haydn::ST64:
        return true;
      default:
        return false;
      }
    };
    if (VALa && VALb && isGISelLogicalMemOp(MIa.getOpcode()) &&
        isGISelLogicalMemOp(MIb.getOpcode())) {
      const DataLayout &DL = MIa.getMF()->getDataLayout();
      int64_t GepOffA = 0, GepOffB = 0;
      const Value *BaseA =
          GetPointerBaseWithConstantOffset(VALa, GepOffA, DL);
      const Value *BaseB =
          GetPointerBaseWithConstantOffset(VALb, GepOffB, DL);
      if (BaseA && BaseB && BaseA == BaseB)
        return CheckOverlapping(MMOOffsetA + GepOffA, MMOOffsetB + GepOffB);
    }
    const PseudoSourceValue *PSVa = MMOa->getPseudoValue();
    const PseudoSourceValue *PSVb = MMOb->getPseudoValue();
    if (PSVa && PSVb && PSVa == PSVb)
      return CheckOverlapping(MMOOffsetA, MMOOffsetB);
    // Distinct IR/PSV objects: operand same-base is not SameValue.
    if (VALa || VALb || PSVa || PSVb)
      return false;
  } else {
    return false;
  }

  // Both MMOs are unknown-address (no Value, no PSV). RISCVInstrInfo.cpp:3522-3552
  // same-base offset+width for va_list field spills.
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  SmallVector<const MachineOperand *, 2> BaseOpsA, BaseOpsB;
  int64_t OffsetA = 0, OffsetB = 0;
  bool OffsetAIsScalable = false, OffsetBIsScalable = false;
  LocationSize WidthA = LocationSize::precise(0),
               WidthB = LocationSize::precise(0);
  if (!getMemOperandsWithOffsetWidth(MIa, BaseOpsA, OffsetA, OffsetAIsScalable,
                                     WidthA, TRI) ||
      !getMemOperandsWithOffsetWidth(MIb, BaseOpsB, OffsetB, OffsetBIsScalable,
                                     WidthB, TRI))
    return false;
  if (BaseOpsA.size() != 1 || BaseOpsB.size() != 1)
    return false;
  if (!BaseOpsA.front()->isIdenticalTo(*BaseOpsB.front()))
    return false;
  if (OffsetAIsScalable || OffsetBIsScalable)
    return false;
  if (!WidthA.hasValue() || !WidthB.hasValue())
    return false;

  int64_t LowOffset = std::min(OffsetA, OffsetB);
  int64_t HighOffset = std::max(OffsetA, OffsetB);
  LocationSize LowWidth = (LowOffset == OffsetA) ? WidthA : WidthB;
  return LowOffset + static_cast<int64_t>(LowWidth.getValue()) <= HighOffset;
}
