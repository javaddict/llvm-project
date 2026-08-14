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
#include "HaydnHWLoopContracts.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPostRAScratch.h"
#include "HaydnResourceCycle.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMatInt.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/Support/CommandLine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachinePipeliner.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCContext.h"
#include <algorithm>
#include <optional>
#include <vector>

#define DEBUG_TYPE "haydn-instr-info"

using namespace llvm;

namespace {

// B1.2 / HaydnFinalizeBundle: every real MI is a BUNDLE root. BranchRelaxation
// and analyzeBranch see MBB::iterator → BUNDLE headers; architectural opcode
// and MBB operands live on the child (AIE-style wrap; Hexagon instr_iterator
// peer). Return the first control-flow child, else \p MI.
const MachineInstr &unwrapBundleControlFlow(const MachineInstr &MI) {
  if (!MI.isBundle())
    return MI;
  const MachineBasicBlock *MBB = MI.getParent();
  if (!MBB)
    return MI;
  for (MachineBasicBlock::const_instr_iterator I =
           std::next(MI.getIterator()),
                                              E = MBB->instr_end();
       I != E && I->isInsideBundle(); ++I) {
    // IgnoreBundle: query the child itself, not nested AnyInBundle.
    if (I->isBranch(MachineInstr::IgnoreBundle) ||
        I->isReturn(MachineInstr::IgnoreBundle) ||
        I->isIndirectBranch(MachineInstr::IgnoreBundle) ||
        I->isCall(MachineInstr::IgnoreBundle) ||
        I->isBarrier(MachineInstr::IgnoreBundle))
      return *I;
  }
  return MI;
}

MachineInstr &unwrapBundleControlFlow(MachineInstr &MI) {
  return const_cast<MachineInstr &>(
      unwrapBundleControlFlow(const_cast<const MachineInstr &>(MI)));
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
// HaydnSubtarget::enableWindowScheduler can read it (the WindowScheduler
// crashes on PseudoLoopEnd, so SMS must be the sole pipeliner when ZOL
// pipelining is on).
// SMS of ZOL-form loops (default ON). When off, SMS skips ZOL loops — they
// still form hwloops via IR HardwareLoops, just without software pipelining.
//
// NOTE: multi-stage SMS on some ZOL byte-mem loops (e.g. libc memcpy @ -O3)
// has been seen to misplace prolog/kernel/epilog vs HWLOOP BEGIN/END. That is
// an expander/schedule bug for those kernels — not a reason to disable ZOL
// SMS globally. Prefer fixing shouldUseSchedule / ModuloScheduleExpander for
// the bad case; keep this flag for emergency disable only.
cl::opt<bool> EnableZOLPipelining(
    "haydn-zol-pipelining", cl::Hidden, cl::init(true),
    cl::desc("Enable SMS pipelining of ZOL-form loops (default on)"));

// Stage-0 PostPipeliner deleted — prefer-PP path retired (always false).
static constexpr bool EnableZOLPreferPostPipeliner = false;

// PPS-3: AIE-style stage-count gate for SMS (into shouldUseSchedule).
static cl::opt<unsigned> HaydnSMSMaxStageCount(
    "haydn-sms-max-stagecount", cl::Hidden, cl::init(3),
    cl::desc("PPS-3: reject SMS schedules with more than this many stages "
             "(prologue stages + 1). Default 3 (AIE LoopMaxStageCount)."));

// PPS-3: AIE-style reg-pressure gate for SMS (into shouldUseSchedule — this
// LLVM has no PipelinerLoopInfo::canAcceptII virtual). Mirrors AIE's
// TrackRegPressure + canAllocate (AIEBasePipelinerLoopInfo.cpp:31-34, 460-518
// 865-869). Default ON matches AIE aie-pipeliner-track-regpressure.
static cl::opt<bool> HaydnSMSTrackRegPressure(
    "haydn-pipeliner-track-regpressure", cl::Hidden, cl::init(true),
    cl::desc("PPS-3: refuse SMS schedules likely to force register spills "
             "(AIE canAllocate peer). Default ON matching AIE."));

// AIE aie-loop-min-tripcount peer: force a floor MinTripCount for all SMS
// candidates. -1 = disabled (default). Used for soak / when MD is missing.
static cl::opt<int> HaydnLoopMinTripCount(
    "haydn-loop-min-tripcount", cl::Hidden, cl::init(-1),
    cl::desc("AIE aie-loop-min-tripcount peer: floor MinTripCount for ZOL SMS "
             "(-1 = disabled). Warning: applies to all ZOL SMS candidates."));

cl::opt<bool> EnableHaydnHRResourceCycle(
    "haydn-hr-resource-cycle", cl::Hidden, cl::init(true),
    cl::desc("/: return a HaydnResourceCycle (Bundle-backed"
             "alternative-aware) from CreateTargetScheduleState so SMS reasons "
             "about real slot pressure. Default ON: the DFA packetizer is "
             "choice-set-naive (DFAPacketizerEmitter ORs all units in a stage), "
             "so a Slot01_LD LD64 reserves BOTH slot0+slot1 bits and two LD64 "
             "always conflict — inflating ResMII past the schedule span and "
             "rejecting every dual-load streaming loop (Subagent A). The"
             "Bundle model picks ONE slot from the alt-set, so two LD64 pack as "
             "slot0+slot1. Mirrors AIE's AIEResourceCycle (AIE-faithful)."));

// Hexagon manner (HexagonBranchRelaxation.cpp): BR / layout has no exact final
// text size knowledge — post-BR AsmPrinter growth (same-slot overflow
// serial Bundle128s, JT R0 re-zero, hwloop align pads) can push a branch that
// computeBlockSize believes is in-range past WIDE_BranchSImm12 (±4 KB).
// Hexagon adds `BranchRelaxSafetyBuffer` (default 200) to the distance before
// isJumpWithinBranchRange; we do the same for isBranchOffsetInRange.
//
// Default history:
// 256 (> measured ~224 B printf undercount; Bundle128-aligned).
// (yarpgen seed 3434, ~6.4k LOC): buffer=256 still left a beqz_w at
// ~4096 B un-relaxed; buffer=512 clears the seed. Default 1024 keeps
// Hexagon-style headroom for yarpgen-scale TUs without forcing every
// mid-range branch to long form.
// Product floor: default MUST remain >= 1024 (do not lower without
// re-proving yarpgen-scale BR undercount). Flag override is debug-only.
// AIE has empty addPreEmitPass (no BR) — N/A there.
static cl::opt<uint32_t> BranchRelaxSafetyBuffer(
    "haydn-branch-relax-safety-buffer", cl::Hidden, cl::init(1024),
    cl::desc("Extra bytes added to branch distance when deciding if a "
             "conditional is in WIDE_BranchSImm12 range (Hexagon-style "
             "branch-relax-safety-buffer). /."));

// W2.1 / G-MEMCYCLE: product path keeps class-agnostic latency 1 (AIE
// AccurateMemEdges=false peer). Opt-in accurate path for soak / A/B only —
// do not default-ON without NatureDSP + densify lit green.
static cl::opt<bool> AccurateMemoryLatency(
    "haydn-accurate-memory-latency", cl::Hidden, cl::init(false),
    cl::desc("W2.1: compute getMemoryLatency from First/LastMemoryCycle "
             "(default OFF = product latency 1). Soak-only; not densify enable."));

#define GET_INSTRINFO_CTOR_DTOR
#include "HaydnGenInstrInfo.inc"
#include "HaydnGenDFAPacketizer.inc"

// Map a format-member opcode to the logical it was expanded from.
//
// Delegates to getHaydnLogicalBaseOpcode, which resolves the base by NAME
// SEARCH rather than a hardcoded table, so it works for both spellings:
// Bundle128's `<logical>_S<k>` and format E's `<logical>_P<form><pos>_<UNIT>`.
//
// This used to strip only `_S0/_S1/_S2` and then look the stripped name up in
// a hand-maintained KnownBases table. Under format E neither half worked — the
// suffix never matched, so the member opcode was returned unchanged and every
// caller's `Opc == Haydn::BEQ`-style compare silently failed. In
// getBranchDestBlock that reached llvm_unreachable and took out 112 of the 430
// CodeGen tests in branch relaxation. Same correction as c290615e3cb0 made for
// the hwloop predicates: fold through the logical, never the spelling.
static unsigned getHaydnFlexBaseOpcode(unsigned Opc, const MCInstrInfo &MII) {
  return getHaydnLogicalBaseOpcode(Opc, MII);
}

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

  // GPR32 → GPR32: MOVE32 rd, rs, rs (register move).
  // MOVE32 reads one register (1R/1W). It used to be modelled with two source
  // operands so that FmtALU32's rs2 bit field had something to encode, and
  // copyPhysReg passed SrcReg twice to fill it; the field is bound to zero in
  // the.td now, which is what the database and the format E members say. An
  // operand the logical has and the member does not is a place the encoder
  // reads the wrong one — FORMAT-E-SWITCH-PLAN.md § 5.11.
  BuildMI(MBB, MI, DL, get(Haydn::MOVE32), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

std::optional<DestSourcePair>
HaydnInstrInfo::isCopyInstrImpl(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default:
    return std::nullopt;
  case Haydn::MOVE32:
    // Canonical GPR move: MOVE32 rd, rs, rs (rs2 mirrors rs1 for encoding).
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
    Opc = Haydn::S_SW_WITH_IMM;
    Size = 4;
  } else if (RC == &Haydn::DR64RegClass) {
    Opc = Haydn::D_SDW_WITH_IMM; // Use 64-bit store for DR64 registers
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
    Opc = Haydn::S_LW_WITH_IMM;
    Size = 4;
  } else if (RC == &Haydn::DR64RegClass) {
    Opc = Haydn::D_LDW_WITH_IMM; // logical; slot from placement / setDesc materialize
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
    if (I->isDebugInstr() || I->isCFIInstruction())
      continue;

    // B1.2: BUNDLE roots wrap real terminators — analyze the child opcode /
    // operands (unwrapBundleControlFlow). MBB::iterator never yields children.
    MachineInstr &CF = unwrapBundleControlFlow(*I);

    // Returns are not analyzable as branches.
    if (CF.isReturn(MachineInstr::IgnoreBundle))
      return true;

    // Generic opcodes (pre-selection) are not analyzable.
    if (isPreISelGenericOpcode(CF.getOpcode()))
      return true;

    unsigned Opc = CF.getOpcode();
    // BranchRelaxation-era branch-analysis must recognize the
    // `_W_S0` far-branch variants. Resolve to the legacy `_W` base so the
    // enum compares below match (the FLEX variants have identical operand
    // shapes — only the opcode encoding/reloc differ).
    Opc = getHaydnFlexBaseOpcode(Opc, *this);

    // Unconditional branches
    bool IsUnconditional = false;
    MachineBasicBlock *Target = nullptr;

    if (Opc == Haydn::B && CF.getNumOperands() > 0 &&
        CF.getOperand(0).isMBB()) {
      IsUnconditional = true;
      Target = CF.getOperand(0).getMBB();
    } else if ((Opc == Haydn::JAL || Opc == Haydn::JAL) &&
               CF.getNumOperands() > 1 && CF.getOperand(0).isReg() &&
               CF.getOperand(0).getReg() == Haydn::R0 &&
               CF.getOperand(1).isMBB()) {
      // Phase 1a: CodeGen now selects JAL_W; legacy JAL kept for the
      // asm parser / decoder. Both have the same (rd, target) operand shape.
      IsUnconditional = true;
      Target = CF.getOperand(1).getMBB();
    } else if (Opc == Haydn::BEQZ && CF.getNumOperands() > 1 &&
               CF.getOperand(0).isReg() &&
               CF.getOperand(0).getReg() == Haydn::R0 &&
               CF.getOperand(1).isMBB() &&
               !MBB.getParent()->getRegInfo().isLiveIn(Haydn::R0)) {
      // BEQZ R0 is only unconditional when R0 is not a function argument
      // (i1 values passed in R0 make this a genuine conditional branch).
      IsUnconditional = true;
      Target = CF.getOperand(1).getMBB();
    }

    if (IsUnconditional) {
      // Remember this unconditional branch target. If we later find a
      // conditional branch, this becomes FBB. Otherwise it's TBB.
      UncondTarget = Target;
      continue; // Keep scanning backwards for a conditional branch
    }

    // JALR / JALR_W — indirect branch, not analyzable as a terminator.
    // Phase 1a: CodeGen selects JALR_W; legacy JALR kept for asm.
    // If we already parsed a trailing branch sequence, this is mid-block
    // material (should not happen for JALR) — stop and keep the analysis.
    if (Opc == Haydn::JALR || Opc == Haydn::JALR) {
      if (!Cond.empty() || UncondTarget)
        break;
      return true;
    }

    // BR_JT — indirect jump table branch, not analyzable as a terminator.
    if (Opc == Haydn::BR_JT) {
      if (!Cond.empty() || UncondTarget)
        break;
      return true;
    }

    // JAL / JAL_W with non-MBB target (external symbol / libcall):
    // As the terminator → unanalyzable (tail-call / bare call end).
    // After a trailing conditional/unconditional branch → mid-block call
    // (yarpgen soft-div pattern: `JAL_W &__divsi3; BNE...`). Stop
    // scanning and keep the branch analysis. Without this, BranchRelaxation
    // asserts "branches to be relaxed must be analyzable" whenever
    // a far cond-branch sits after a call in the same MBB.
    // JAL/JAL_W libcall (non-MBB target): mid-block after a parsed branch →
    // stop and keep analysis. As sole terminator (noreturn abort): continue
    // so empty Cond means analyzable fallthrough for MBP (20000815-1).
    // Only for non-terminator calls — true terminators stay unanalyzable.
    if (Opc == Haydn::JAL || Opc == Haydn::JAL) {
      if (!Cond.empty() || UncondTarget)
        break;
      if (!CF.isTerminator(MachineInstr::IgnoreBundle))
        continue;
      return true;
    }

    // Other barriers that are not analyzable as terminators. Mid-block after
    // an already-parsed branch sequence: stop scanning, keep the analysis.
    if (CF.isBarrier(MachineInstr::IgnoreBundle)) {
      if (!Cond.empty() || UncondTarget)
        break;
      return true;
    }

    // Conditional branches (1 register)
    if (Opc == Haydn::BNEZ || Opc == Haydn::BEQZ ||
        Opc == Haydn::BGEZ || Opc == Haydn::BLTZ) {
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
    else if (Opc == Haydn::BEQ || Opc == Haydn::BNE || Opc == Haydn::BGE ||
             Opc == Haydn::BGEU || Opc == Haydn::BLT || Opc == Haydn::BLTU) {
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
      // Not a branch — stop scanning.
      break;
    }
  }

  // If we only found an unconditional branch (no conditional), set TBB.
  if (Cond.empty() && UncondTarget) {
    TBB = UncondTarget;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// SSA EarlyIfConversion: canInsertSelect / insertSelect
//===----------------------------------------------------------------------===//
//
// Generic EarlyIfConverter (llvm/lib/CodeGen/EarlyIfConversion.cpp) rewrites
// SSA triangles/diamonds by speculating side blocks and inserting a select for
// each PHI at the join. Cond comes from analyzeBranch:
// Cond = [ Imm(BEQZ|BNEZ|...), CondReg ]
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
  // Cond from analyzeBranch: [Imm(opc), Reg] for BEQZ / BNEZ.
  if (Cond.size() != 2 || !Cond[0].isImm() || !Cond[1].isReg())
    return false;

  unsigned Opc = getHaydnFlexBaseOpcode(Cond[0].getImm(), *this);
  if (Opc != Haydn::BEQZ && Opc != Haydn::BNEZ)
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

  unsigned Opc = getHaydnFlexBaseOpcode(Cond[0].getImm(), *this);
  Register CondReg = Cond[1].getReg();

  // Branch-taken means "Cond is true" for EarlyIfConv's TrueReg/FalseReg.
  // BNEZ: taken when CondReg != 0 → MOVT (bit0==1 takes True)
  // BEQZ: taken when CondReg == 0 → MOVF (bit0==0 takes True)
  unsigned MovOpc;
  switch (Opc) {
  case Haydn::BNEZ:
    MovOpc = Haydn::MOVT32;
    break;
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

  if (Cond.empty()) {
    // Unconditional branch — emit the B pseudo (has isBarrier=1).
    // The B pseudo survives through BranchRelaxation (where it is properly
    // recognized by analyzeBranch). It is expanded to BEQZ R0 either by
    // expandPostRAPseudo (for pre-existing B pseudos) or by AsmPrinter
    // (for B pseudos inserted by BranchRelaxation via insertBranch).
    MachineInstr &MI = *BuildMI(MBB, MBB.end(), DL, get(Haydn::B)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    return 1;
  }

  // Conditional branch
  unsigned Opc = Cond[0].getImm();
  // resolve `_W_S0` far-branch variants to their legacy `_W`
  // base. analyzeBranch stores the resolved base in Cond[0], but
  // BranchRelaxation may mutate Cond; resolving here keeps the operand-shape
  // compares below consistent and builds the legacy `_W` form (whose encoder
  // pipeline handling is identical to the FLEX variant for branch purposes).
  Opc = getHaydnFlexBaseOpcode(Opc, *this);

  // Hardware-loop terminators. PseudoLoopEnd has no register operand
  // (Cond = [Imm] only); LoopJNZ has one register (Cond = [Imm, reg]).
  // Both use addMBB(TBB) for the target. When FBB is non-null (two-way)
  // append an unconditional B to FBB after the conditional.
  if (Opc == Haydn::PseudoLoopEnd) {
    MachineInstr &MI = *BuildMI(MBB, MBB.end(), DL, get(Opc)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    if (FBB) {
      MachineInstr &BMI = *BuildMI(MBB, MBB.end(), DL, get(Haydn::B)).addMBB(FBB);
      if (BytesAdded)
        *BytesAdded += getInstSizeInBytes(BMI);
      return 2;
    }
    return 1;
  }
  if (Opc == Haydn::LoopJNZ) {
    MachineInstr &MI = *BuildMI(MBB, MBB.end(), DL, get(Opc))
                            .addReg(Cond[1].getReg())
                            .addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    if (FBB) {
      MachineInstr &BMI = *BuildMI(MBB, MBB.end(), DL, get(Haydn::B)).addMBB(FBB);
      if (BytesAdded)
        *BytesAdded += getInstSizeInBytes(BMI);
      return 2;
    }
    return 1;
  }

  if (FBB == nullptr) {
    // One-way conditional branch: if Cond, goto TBB; else fall through
    MachineInstrBuilder MIB = BuildMI(MBB, MBB.end(), DL, get(Opc));
    if (Opc == Haydn::BNEZ || Opc == Haydn::BEQZ ||
        Opc == Haydn::BGEZ || Opc == Haydn::BLTZ) {
      MIB.addReg(Cond[1].getReg());
    } else {
      MIB.addReg(Cond[1].getReg()).addReg(Cond[2].getReg());
    }
    MIB.addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(*MIB);
    return 1;
  }

  // Two-way conditional branch: if Cond, goto TBB; else goto FBB
  MachineInstrBuilder MIB = BuildMI(MBB, MBB.end(), DL, get(Opc));
  if (Opc == Haydn::BNEZ || Opc == Haydn::BEQZ ||
      Opc == Haydn::BGEZ || Opc == Haydn::BLTZ) {
    MIB.addReg(Cond[1].getReg());
  } else {
    MIB.addReg(Cond[1].getReg()).addReg(Cond[2].getReg());
  }
  MIB.addMBB(TBB);
  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(*MIB);
  // Unconditional branch to FBB via B pseudo (has isBarrier=1).
  // Expanded by expandPostRAPseudo or AsmPrinter.
  MachineInstr &BMI = *BuildMI(MBB, MBB.end(), DL, get(Haydn::B)).addMBB(FBB);
  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(BMI);
  return 2;
}

unsigned HaydnInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  if (BytesRemoved)
    *BytesRemoved = 0;

  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr() || I->isCFIInstruction())
      continue;

    if (!I->isBranch())
      break;

    // Report removed branch size via getInstSizeInBytes (not hand constants).
    // BranchRelaxation increments BlockInfo from BytesRemoved; verify
    // recomputes via computeBlockSize — both must agree. : B/RET/etc.
    // size as one Bundle128 parcel (16), not the retired WIDE 6-byte model.
    if (BytesRemoved)
      *BytesRemoved += getInstSizeInBytes(*I);
    I = MBB.erase(I);
    Count++;
  }

  return Count;
}

bool HaydnInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  // Unconditional branches have an empty condition — there is nothing to
  // reverse. Returning false tells BranchRelaxation that we cannot produce
  // an inverted branch, so it will fall back to its own long-branch handling
  // (e.g. inserting an indirect jump).
  if (Cond.empty())
    return false;

  unsigned Opc = Cond[0].getImm();
  // resolve `_W_S0` far-branch variants to their legacy `_W`
  // base so the inversion switch below recognizes them (the inverted opcode
  // is written back as the legacy `_W` form, which the existing pipeline
  // encoder handle identically to the FLEX variant for branch purposes).
  Opc = getHaydnFlexBaseOpcode(Opc, *this);

 // Invert conditional branches. Emit inverted opcode as s0 FLEX.
  unsigned Inv = 0;
  switch (Opc) {
  case Haydn::BEQZ:  Inv = Haydn::BNEZ; break;
  case Haydn::BNEZ:  Inv = Haydn::BEQZ; break;
  case Haydn::BGEZ:  Inv = Haydn::BLTZ; break;
  case Haydn::BLTZ:  Inv = Haydn::BGEZ; break;
  case Haydn::BEQ:   Inv = Haydn::BNE;  break;
  case Haydn::BNE:   Inv = Haydn::BEQ;  break;
  case Haydn::BGE:   Inv = Haydn::BLT;  break;
  case Haydn::BLT:   Inv = Haydn::BGE;  break;
  case Haydn::BGEU:  Inv = Haydn::BLTU; break;
  case Haydn::BLTU:  Inv = Haydn::BGEU; break;
  default:
    return true; // Cannot reverse
  }

  Cond[0].setImm(Inv);
  return false; // Successfully reversed
}

bool HaydnInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineBasicBlock::iterator MBBI = MI.getIterator();
  DebugLoc DL = MI.getDebugLoc();

  switch (MI.getOpcode()) {
  default:
    return false;

  case Haydn::RET:
    // RET → JALR_W R0, R15, 0 (jump to LR, discard link address).
    // Phase 1a: route to the 48-bit WIDE form (legacy JALR kept in
    // the.td for asm parser / decoder until Phase 3).
    MI.setDesc(get(Haydn::JALR));
    MI.addOperand(MachineOperand::CreateReg(Haydn::R0, /*isDef*/ true));
    MI.addOperand(MachineOperand::CreateReg(Haydn::R15, /*isDef*/ false));
    MI.addOperand(MachineOperand::CreateImm(0));
    return true;

  case Haydn::B:
    // B pseudo must survive through all post-RA passes including
    // MachineBlockPlacement so analyzeBranch correctly identifies it.
    // Expanded to BEQZ R0 in the AsmPrinter (HaydnAsmPrinter.cpp).
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
    // isel). MBB operands (branch-relax destinations) are left for AsmPrinter
    // ExpandPostRA runs before BranchRelaxation inserts those.
    //
    // slice Z: ZERO_GPR retired — MatInt(0) is ADDI32_W rd, R0, 0 (or
    // equivalent); soft-zero R0 remains available as the sequence source.
    if (!MI.getOperand(1).isImm())
      return false;

    Register DstReg = MI.getOperand(0).getReg();
    int64_t Imm = MI.getOperand(1).getImm();
    HaydnMatInt::InstSeq Seq = HaydnMatInt::generate(Imm);

    // Post-RA: chain every MatInt step through the same phys dst (R0 → Dst → …).
    Register CurrentReg = Haydn::R0;
    for (const HaydnMatInt::Inst &MatInst : Seq) {
      switch (MatInst.Opc) {
      default: {
        // ADDI32 / ADDI32_W / ORI32_W: (rd, rs, imm). LUI is (rd, imm) —
        // its source went with the Bundle128 shape in afc345108f57 and this
        // loop kept passing one, which MachineVerifier rejects as an extra
        // explicit operand. FORMAT-E-SWITCH-PLAN.md 5.11.
        auto B = BuildMI(MBB, MBBI, DL, get(MatInst.Opc), DstReg);
        if (MatInst.Opc != Haydn::LUI)
          B.addReg(CurrentReg);
        B.addImm(MatInst.Imm);
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
    // lo32/hi32 via HaydnMatInt into a scavenged post-RA GPR scratch, ST32
    // both halves to a transient SP slot, LD64. Rematerialisable single-imm
    // pseudo.
    //
    // OPT-7 (sign-extend hi): when hi32 == 0xFFFFFFFF and lo32 < 0, the
    // high half is arithmetic sign-extension of the low half. Emit
    // SRAI32 scr, scr, 31
    // after storing lo, instead of a second MatInt(-1). Same shape as
    // HaydnMatInt's 64-bit early-out; applied here because G_CONSTANT
    // selects LOADI64 and expansion owns the final halves.
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
    const bool HiIsSignExtOfLo = (Hi == -1 && Lo < 0);
    const HaydnSubtarget &ST =
        MBB.getParent()->getSubtarget<HaydnSubtarget>();

    auto emitConst32 = [&](int32_t V, Register Target) {
      assert(Target != Haydn::R0 &&
             "LOADI64 MatInt dest must not be soft-zero R0");
      HaydnMatInt::InstSeq Seq = HaydnMatInt::generate(V);
      Register Cur = Haydn::R0;
      for (size_t I = 0; I < Seq.size(); ++I) {
        auto B = BuildMI(MBB, MBBI, DL, get(Seq[I].Opc), Target);
        if (Seq[I].Opc != Haydn::LUI)  // (rd, imm), see above
          B.addReg(Cur);
        B.addImm(Seq[I].Imm);
        Cur = Target;
      }
    };

    withPostRAScratch(
        MBB, MBBI, DL, *this, ST, /*PreferNotR12=*/true,
        [&](Register Scr) {
          BuildMI(MBB, MBBI, DL, get(Haydn::SUBI32), Haydn::R13)
              .addReg(Haydn::R13)
              .addImm(8);
          emitConst32(Lo, Scr);
          BuildMI(MBB, MBBI, DL, get(Haydn::S_SW_WITH_IMM))
              .addReg(Scr)
              .addReg(Haydn::R13)
              .addImm(0);
          if (HiIsSignExtOfLo) {
            // OPT-7: hi = ashr(lo, 31). Scr still holds lo.
            BuildMI(MBB, MBBI, DL, get(Haydn::SRAI32), Scr)
                .addReg(Scr)
                .addImm(31);
          } else {
            emitConst32(Hi, Scr);
          }
          BuildMI(MBB, MBBI, DL, get(Haydn::S_SW_WITH_IMM))
              .addReg(Scr)
              .addReg(Haydn::R13)
              .addImm(haydnScaledLSImm(4, 4));
          BuildMI(MBB, MBBI, DL, get(Haydn::D_LDW_WITH_IMM), DstReg)
              .addReg(Haydn::R13)
              .addImm(0);
          BuildMI(MBB, MBBI, DL, get(Haydn::ADDI32), Haydn::R13)
              .addReg(Haydn::R13)
              .addImm(8);
        },
        /*Exclude=*/{}, PostRASoftZero::NeedsZeroBase);

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
    // fir_xcorr / firinterp: MOV_GPR_TO_DR64 R0, (srai hi,16) — SP path was
    // the hot-loop subi32/st32/ld64 bloat (P7 / ISA-42 interim).
    //
    // General (both halves live GPRs) still uses SP-relative memory after PEI:
    // SUBI32 SP, SP, 8; ST32 lo; ST32 hi; LD64 rd; ADDI32 SP, 8
    // CFI: balanced transient — net CFA zero (same rationale as before).
    Register DstReg = MI.getOperand(0).getReg();  // DR64
    Register SrcLo = MI.getOperand(1).getReg();   // GPR32 low half
    Register SrcHi = MI.getOperand(2).getReg();   // GPR32 high half
    bool LoKill = MI.getOperand(1).isKill();
    bool HiKill = MI.getOperand(2).isKill();

    // Stackless when a half is the soft-zero *operand* (no R0 read)
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
      BuildMI(MBB, MBBI, DL, get(Haydn::SEXT32T64), DstReg)
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
      BuildMI(MBB, MBBI, DL, get(Haydn::SEXT32T64), DstReg)
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

    BuildMI(MBB, MBBI, DL, get(Haydn::SUBI32), Haydn::R13)
        .addReg(Haydn::R13)
        .addImm(8);
    // When SrcLo == SrcHi, only apply kill on the last use to avoid
    // killing the same physical register twice.
    if (SrcLo == SrcHi) {
      BuildMI(MBB, MBBI, DL, get(Haydn::S_SW_WITH_IMM))
          .addReg(SrcLo, getKillRegState(false))
          .addReg(Haydn::R13)
          .addImm(0);
      BuildMI(MBB, MBBI, DL, get(Haydn::S_SW_WITH_IMM))
          .addReg(SrcHi, getKillRegState(LoKill || HiKill))
          .addReg(Haydn::R13)
          .addImm(haydnScaledLSImm(4, 4));
    } else {
      BuildMI(MBB, MBBI, DL, get(Haydn::S_SW_WITH_IMM))
          .addReg(SrcLo, getKillRegState(LoKill))
          .addReg(Haydn::R13)
          .addImm(0);
      BuildMI(MBB, MBBI, DL, get(Haydn::S_SW_WITH_IMM))
          .addReg(SrcHi, getKillRegState(HiKill))
          .addReg(Haydn::R13)
          .addImm(haydnScaledLSImm(4, 4));
    }
    BuildMI(MBB, MBBI, DL, get(Haydn::D_LDW_WITH_IMM), DstReg)
        .addReg(Haydn::R13)
        .addImm(0);
    BuildMI(MBB, MBBI, DL, get(Haydn::ADDI32), Haydn::R13)
        .addReg(Haydn::R13)
        .addImm(8);

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

bool HaydnInstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                         const MachineBasicBlock *MBB,
                                         const MachineFunction &MF) const {
  // Instructions that should not be packetized across:
  if (MI.isCall() || MI.isInlineAsm() || MI.isReturn() || MI.isBranch())
    return true;

  // Hardware-loop setup is a hard region boundary (AIE/Hexagon-aligned).
  // Role A expands LoopStart → SET_HWLOOP_F2 before postmisched; without
  // this fence the scheduler can reorder body peels / address setup across
  // SET (lc_dp_merge: s_lw_post with unscaled index after SET → ALIGNMENT).
  // Treat remaining LoopStart the same until fully expanded.
  // Compare Flex base opcodes — post-RA setDesc may leave *_S0 member Desc.
  unsigned Opc = MI.getOpcode();
  unsigned HwBase = getHaydnFlexBaseOpcode(Opc, *this);
  if (HwBase == Haydn::SET_HWLOOP_PSEUDO || HwBase == Haydn::SET_HWLOOP_F2_PSEUDO ||
      HwBase == Haydn::SET_HWLOOP || HwBase == Haydn::SET_HWLOOP_F2 ||
      HwBase == Haydn::SET_HWLOOP_REG || HwBase == Haydn::LoopStart ||
      Opc == Haydn::LoopStart)
    return true;

  // Remat ADDI* that feeds SET_HWLOOP* use of Dest (rematerializeAddImmForUse
  // also bundles them). Region-split as a second fence when unbundled.
  // Look through debug/NOP. Post-RA setDesc may leave ADDI32_W_S0 member Desc.
  {
    unsigned BaseOpc = getHaydnFlexBaseOpcode(Opc, *this);
    if (BaseOpc == Haydn::ADDI32) {
      if (MI.getNumExplicitOperands() >= 1 && MI.getOperand(0).isReg()) {
        Register Dest = MI.getOperand(0).getReg();
        // Bundled remat def: whole BUNDLE is a boundary via SET inside, but
        // also fence the def itself when it is the bundle start interior.
        if (MI.isBundledWithSucc())
          return true;
        MachineBasicBlock::const_iterator I =
            std::next(MachineBasicBlock::const_iterator(MI.getIterator()));
        // Skip debug, kill, implicit-def, and pure NOPs (t−3 layout pads are
        // *after* SET; any NOP between remat and SET is still a fence gap).
        while (I != MBB->end() &&
               (I->isDebugInstr() || I->isKill() || I->isImplicitDef() ||
                I->getOpcode() == Haydn::NOP))
          ++I;
        if (I != MBB->end()) {
          unsigned NBase = getHaydnFlexBaseOpcode(I->getOpcode(), *this);
          if (NBase == Haydn::SET_HWLOOP_F2_PSEUDO || NBase == Haydn::SET_HWLOOP_F2 ||
              NBase == Haydn::SET_HWLOOP_REG || NBase == Haydn::SET_HWLOOP_PSEUDO ||
              NBase == Haydn::SET_HWLOOP || NBase == Haydn::LoopStart) {
            for (const MachineOperand &MO : I->operands()) {
              if (MO.isReg() && MO.isUse() && !MO.isImplicit() &&
                  MO.getReg() == Dest)
                return true;
            }
          }
        }
      }
    }
  }

  // BUNDLE that contains SET_HWLOOP / remat pair — hard region edge.
  // MBB::iterator only visits the BUNDLE header; interior SET is invisible
  // unless we inspect the bundle here.
  if (MI.isBundle()) {
    for (const MachineInstr *I = MI.getNextNode();
         I && I->isBundledWithPred(); I = I->getNextNode()) {
      unsigned B = getHaydnFlexBaseOpcode(I->getOpcode(), *this);
      if (B == Haydn::SET_HWLOOP_PSEUDO || B == Haydn::SET_HWLOOP_F2_PSEUDO ||
          B == Haydn::SET_HWLOOP || B == Haydn::SET_HWLOOP_F2 ||
          B == Haydn::SET_HWLOOP_REG || B == Haydn::LoopStart)
        return true;
    }
  }

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

bool HaydnInstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                            int64_t BrOffset) const {
  // B1.2: BranchRelaxation may pass TargetOpcode::BUNDLE (header of a wrapped
  // branch). Cond/B field width is simm12 → fall through to isInt<13> below.
  // JAL long-reach is not modeled via BUNDLE opc (insertBranch emits bare MI).
  if (BranchOpc == TargetOpcode::BUNDLE)
    BranchOpc = Haydn::B; // conservative short-range (Bundle128 cond/B)

  // resolve `_W_S0` far-branch variants to their legacy `_W`
  // base so the opcode-range compares below recognize them (the FLEX variants
  // share the legacy `_W` offset field width — only the encoding/reloc differ).
  BranchOpc = getHaydnFlexBaseOpcode(BranchOpc, *this);
  // JAL / JAL_W have a 20-bit signed target field (SImm20): ±512KB range.
  // JALR / JALR_W have no offset limitation (register-indirect).
  // Phase 1a: CodeGen selects the _W forms; legacy opcodes kept for
  // the asm parser / decoder.
  if (BranchOpc == Haydn::JAL || BranchOpc == Haydn::JAL ||
      BranchOpc == Haydn::JALR || BranchOpc == Haydn::JALR)
    return true;

  // Pseudo-call and jump-table pseudo reach ±512KB (JAL_W) / unlimited
  // (JALR_W via BR_JT) — always in range for any single fn.
  // NOTE : B is deliberately NOT here. B lowers to BEQZ R0, which
  // shares the conditional branch's ±4 KB WIDE_BranchSImm12 reach — it is
  // NOT a long-reach unconditional jump. Modeling B as always-in-range hid
  // out-of-range unconditional branches from BranchRelaxation: when
  // fixupConditionalBranch relaxes a far conditional into (inverted cond to
  // near + B to far), the B leg still overflows ±4 KB. Letting B fall through
  // to the isInt<13> check below makes BranchRelaxation detect the far B leg
  // and relax it via fixupUnconditionalBranch → insertIndirectBranch
  // (LOADI32 + JALR, unlimited reach) on the next fixed-point iteration.
  if (BranchOpc == Haydn::PseudoCALL || BranchOpc == Haydn::BR_JT)
    return true;

  // All conditional branches (BEQ..BLTU, BEQZ..BLTZ) carry a 12-bit signed
  // offset field stored in 2-byte units (encoding_manual.md §5.5, §5.14 D1):
  // range = sext(off12) << 1 = ±(2^11) << 1 = ±4096 bytes (±4KB).
  // The offset is measured in bytes.
  //
  // Hexagon-style safety buffer (see HexagonBranchRelaxation::isJumpOutOfRange):
  // Distance = |offset| + BranchRelaxSafetyBuffer
  // out-of-range if !isJumpWithinBranchRange(..., Distance)
  // Inflate BrOffset away from zero by the buffer, then apply isInt<13>.
  // without this, BR accepted printf BNEZ at 3904 B; AsmPrinter growth
  // made final distance 4128 B and MC-fixup failed.
  // residual: large yarpgen TUs still under-estimate (seed 3434
  // beqz_w @ ~4 KB with buffer=256). Default buffer is 1024 — see cl::opt.
  int64_t Inflated = BrOffset >= 0
                         ? BrOffset + (int64_t)BranchRelaxSafetyBuffer
                         : BrOffset - (int64_t)BranchRelaxSafetyBuffer;
  return isInt<13>(Inflated);
}

MachineBasicBlock *
HaydnInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  // B1.2: BranchRelaxation may pass a BUNDLE root (AnyInBundle isBranch).
  // Destination MBB is on the child branch (unwrapBundleControlFlow).
  const MachineInstr &Br = unwrapBundleControlFlow(MI);
  unsigned Opc = Br.getOpcode();
  // resolve `_W_S0` far-branch variants to their legacy `_W`
  // base so the operand-index compares below recognize them (the FLEX variants
  // share the legacy `_W` operand layout — only the encoding/reloc differ).
  Opc = getHaydnFlexBaseOpcode(Opc, *this);

  // Two-register conditional branches: BEQ..BLTU.
  // Operands: rs1, rs2, brtarget
  if (Opc == Haydn::BEQ || Opc == Haydn::BNE || Opc == Haydn::BGE ||
      Opc == Haydn::BGEU || Opc == Haydn::BLT || Opc == Haydn::BLTU) {
    return Br.getOperand(2).getMBB();
  }

  // Single-register conditional branches: BEQZ..BLTZ.
  // Operands: rs, brtarget
  if (Opc == Haydn::BEQZ || Opc == Haydn::BNEZ || Opc == Haydn::BGEZ ||
      Opc == Haydn::BLTZ) {
    return Br.getOperand(1).getMBB();
  }

  // Unconditional branch pseudo: B
  // Operand 0: brtarget
  if (Opc == Haydn::B) {
    return Br.getOperand(0).getMBB();
  }

  // JAL / JAL_W with MBB operand (call or far jump).
  // Operands: rd, calltarget/brtarget_wide_i20 (both shapes are (rd, target)).
  // Phase 1a: CodeGen selects JAL_W; legacy JAL kept for asm/parser.
  if ((Opc == Haydn::JAL || Opc == Haydn::JAL) &&
      Br.getOperand(1).isMBB()) {
    return Br.getOperand(1).getMBB();
  }

  // Hardware-loop terminators.
  // PseudoLoopEnd: single MBB operand (the loop body).
  // LoopJNZ: reg + MBB operand (counter + loop body).
  if (Opc == Haydn::PseudoLoopEnd)
    return Br.getOperand(0).getMBB();
  if (Opc == Haydn::LoopJNZ)
    return Br.getOperand(1).getMBB();

  llvm_unreachable("unhandled branch in getBranchDestBlock");
}

void HaydnInstrInfo::insertIndirectBranch(
    MachineBasicBlock &MBB, MachineBasicBlock &NewDestBB,
    MachineBasicBlock &RestoreBB, const DebugLoc &DL, int64_t BrOffset,
    RegScavenger *RS) const {
  // Insert an indirect branch from MBB to NewDestBB using JALR.
  //
  // Sequence:
  // LOADI32 scratch, <dest_addr> (expands to LUI+ADDI32_W / MatInt)
  // JALR_W scratch, scratch, 0 (jump; link discarded into scratch)
  //
  // never use R0 as the JALR link dest (soft-zero, not hardwired).
  // Scratch policy (RISC-V-aligned; AIE model: no free AT):
  // 1. RegScavenger with AllowSpill=false after re-attaching the dedicated
  // BranchRelaxationScratchFI (BranchRelaxation constructs a *fresh* RS
  // that does not inherit PEI scavenger FIs).
  // 2. If still no free reg: spill R11 to that FI, jump via RestoreBB
  // restore R11 there (same as RISCVInstrInfo::insertIndirectBranch).
  //
  // Product contract: AllowSpill must stay false for branch-relax scavenging.
  // Do not flip to true "to help pressure". AllowSpill=true was wrong here.
  // Under greedy RA (high live pressure across a far branch),
  // scavengeRegisterBackwards spilled a live-out GPR and reinserted the
  // reload *after* the JALR_W terminator in the trampoline MBB. The reload
  // is dead (never executed); the dest block saw a clobbered live-in → wrong
  // oracle_u64 (seed2 @ -O1/-O2; seed7 @ -O2 same class). RISC-V uses
  // AllowSpill=false and the RestoreBB path for the no-free-reg case; match
  // that. No-free-reg → manual R11 spill only.
  assert(RS && "RegScavenger required for long branching");
  assert(MBB.pred_size() == 1);

  if (!isInt<32>(BrOffset))
    report_fatal_error(
        "Branch offsets outside of the signed 32-bit range not supported");

  MachineFunction *MF = MBB.getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const HaydnSubtarget &ST = MF->getSubtarget<HaydnSubtarget>();
  const TargetRegisterInfo *TRI = ST.getRegisterInfo();
  auto *FuncInfo = MF->getInfo<HaydnMachineFunctionInfo>();
  auto II = MBB.end();

  // Re-attach PEI-allocated emergency FI onto BranchRelaxation's fresh RS.
  int ScratchFI = FuncInfo->getBranchRelaxationScratchFI();
  if (ScratchFI >= 0 && !RS->isScavengingFrameIndex(ScratchFI))
    RS->addScavengingFrameIndex(ScratchFI);

  auto scavengeScratch = [&](MachineBasicBlock::iterator From) -> Register {
    // Scavenge only (AIE model: R12 is allocatable, never a free AT).
    // Product contract: AllowSpill must stay false (see product-contract block).
    Register S = RS->scavengeRegisterBackwards(
        Haydn::GPR32RegClass, From, /*RestoreAfter=*/false, /*SpAdj=*/0,
        /*AllowSpill=*/false);
    if (!S)
      S = RS->FindUnusedReg(&Haydn::GPR32RegClass);
    return S;
  };

  // Manual spill of R11 when scavenger still fails (RISC-V s11 pattern).
  // Jump lands on RestoreBB (restore R11) which falls through to NewDestBB.
  auto emitWithManualSpill = [&](Register ScratchPhys,
                                 MachineBasicBlock::iterator InsertPt,
                                 bool JumpToRestore) {
    if (ScratchFI < 0)
      report_fatal_error(
          "Haydn: insertIndirectBranch needs BranchRelaxationScratchFI "
          "(underestimated function size / no emergency spill)");

    storeRegToStackSlot(MBB, InsertPt, ScratchPhys, /*IsKill=*/true, ScratchFI,
                        &Haydn::GPR32RegClass, Register());
    // Post-PEI: fold FI now.
    TRI->eliminateFrameIndex(std::prev(InsertPt), /*SpAdj=*/0,
                             /*FIOperandNum=*/1);

    MachineBasicBlock *JumpDest = JumpToRestore ? &RestoreBB : &NewDestBB;
    BuildMI(MBB, InsertPt, DL, get(Haydn::LOADI32), ScratchPhys)
        .addMBB(JumpDest);
    BuildMI(MBB, InsertPt, DL, get(Haydn::JALR))
        .addReg(ScratchPhys, RegState::Define)
        .addReg(ScratchPhys)
        .addImm(0);

    if (JumpToRestore) {
      loadRegFromStackSlot(RestoreBB, RestoreBB.end(), ScratchPhys, ScratchFI,
                           &Haydn::GPR32RegClass, Register());
      TRI->eliminateFrameIndex(RestoreBB.back(), /*SpAdj=*/0,
                               /*FIOperandNum=*/1);
    }
  };

  Register ScratchPhys;
  if (MBB.empty()) {
    // Scavenger needs at least one instr to walk from; use a vreg then
    // substitute (same workaround as RISCV/SIInstrInfo).
    Register ScratchV = MRI.createVirtualRegister(&Haydn::GPR32RegClass);
    MachineInstr *LoadMI =
        BuildMI(MBB, II, DL, get(Haydn::LOADI32), ScratchV).addMBB(&NewDestBB);
    BuildMI(MBB, II, DL, get(Haydn::JALR))
        .addReg(ScratchV, RegState::Define)
        .addReg(ScratchV)
        .addImm(0);

    RS->enterBasicBlockEnd(MBB);
    ScratchPhys = scavengeScratch(LoadMI->getIterator());
    if (ScratchPhys.isValid()) {
      RS->setRegUsed(ScratchPhys);
      MRI.replaceRegWith(ScratchV, ScratchPhys);
      MRI.clearVirtRegs();
      return;
    }

    // No free reg: tear down vreg sequence and manual-spill R11 via RestoreBB.
    MBB.erase(MBB.begin(), MBB.end());
    MRI.clearVirtRegs();
    ScratchPhys = Haydn::R11;
    emitWithManualSpill(ScratchPhys, MBB.end(), /*JumpToRestore=*/true);
    return;
  }

  // Non-empty MBB: scavenge at the insertion point before emitting.
  RS->enterBasicBlockEnd(MBB);
  ScratchPhys = scavengeScratch(II);
  if (ScratchPhys.isValid()) {
    RS->setRegUsed(ScratchPhys);
    BuildMI(MBB, II, DL, get(Haydn::LOADI32), ScratchPhys).addMBB(&NewDestBB);
    BuildMI(MBB, II, DL, get(Haydn::JALR))
        .addReg(ScratchPhys, RegState::Define)
        .addReg(ScratchPhys)
        .addImm(0);
    return;
  }

  ScratchPhys = Haydn::R11;
  emitWithManualSpill(ScratchPhys, II, /*JumpToRestore=*/true);
}

unsigned HaydnInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  // B4.4 / AIE-shaped size authority (shared with Fixup/HardwareLoops/BR):
  //   * BUNDLE root → encodedBytesFor(committed FormatID)
  //     (AIE getAIEMachineBundleSize → Format->getSize(),
  //      AIEBaseInstrInfo.cpp:546-555)
  //   * bare real / multi-parcel pseudo → ProductFormatDesc.Bytes * N
  //     (AIE getInstSizeInBytes → get(Opcode).getSize(),
  //      AIE1InstrInfo.cpp:646-651; Haydn product is one 12-byte format E
  //      parcel per architectural cycle)
  //   * child inside a BUNDLE → 0 (composite size is on the root)
  //   * pure meta / zero-size pseudos → 0
  // No parallel "always 12" oracle independent of FormatID/EncodedBytes.
  using haydn::bundle::committedEncodedBytes;
  using haydn::bundle::productParcelBytes;
  const unsigned B = productParcelBytes();
  static_assert(haydn::bundle::ProductEncodedBytesValue == 12u,
                "format E product parcel is 12 bytes");
  static_assert(
      haydn::bundle::ProductFormatDesc.Bytes.Value ==
          haydn::bundle::ProductEncodedBytesValue,
      "product FormatDesc.Bytes is the EncodedBytes oracle");

  // Formed VLIW packet: committed FormatID → EncodedBytes (idle slots = zeros).
  if (MI.isBundle())
    return committedEncodedBytes(MI);

  // Children are accounted on the BUNDLE root (AIE bundle size is format size
  // on the composite, not sum of slot sub-instruction Sizes).
  if (MI.isInsideBundle())
    return 0;

  // Pseudos that expand to one or more real parcels before/at emit.
  // Size unit is always productParcelBytes() (FormatDesc EncodedBytes).
  switch (MI.getOpcode()) {
  default:
    break;
  case Haydn::B:
  case Haydn::RET:
  case Haydn::BR_JT:
  case Haydn::PseudoCALL:
  case Haydn::SET_HWLOOP_PSEUDO:
  case Haydn::SET_HWLOOP_F2_PSEUDO:
    // Each expands to a single real instruction → one product parcel.
    return B;
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
  case Haydn::WFI:
    return 0;
  case Haydn::VASTART:
    // W1.4: was 0 while AsmPrinter emitted many product parcels → BR undercount.
    // Expanded pre-pack via free-reg scavenge (often no spill). Upper bound:
    // optional spill/restore + 3×(addr+st) + 2×(neg+st); far FI ≤ ~16 parcels.
    return B * 16;
  case Haydn::VACOPY:
    // Free-reg scavenge; worst-case spill + 5×(ld+st) ≤ ~14 parcels.
    return B * 14;
  }

  if (MI.isPseudo() || MI.isMetaInstruction() || MI.isDebugInstr() ||
      MI.isImplicitDef() || MI.isKill())
    return 0;

  // Bare real opcode: one product parcel (ProductFormatDesc.Bytes). Encode
  // reports fatal if there is no legal placement; BR must never undercount.
  return B;
}

ResourceCycle *HaydnInstrInfo::CreateTargetScheduleState(
    const TargetSubtargetInfo &STI) const {
  // Bundle-backed resource model for SWPS (alternative-aware slot
  // pressure). Default ON — see EnableHaydnHRResourceCycle. The DFA fallback is
  // choice-set-naive (reserves all alt bits in a stage), which inflates ResMII
  // on dual-load streaming loops.
  if (EnableHaydnHRResourceCycle)
    return new HaydnResourceCycle();
  const InstrItineraryData *II = STI.getInstrItineraryData();
  return static_cast<const HaydnSubtarget &>(STI).createDFAPacketizer(II);
}

ScheduleHazardRecognizer *HaydnInstrInfo::CreateTargetMIHazardRecognizer(
    const InstrItineraryData *ItinData, const ScheduleDAGMI *DAG) const {
  // Phase B1 (Stream B): install the Haydn scoreboard hazard recognizer
  // only for the POST-RA scheduler (where DAG has no vreg liveness). The pre-RA
  // path keeps the existing VLIWMachineScheduler strategy with its default
  // ScoreboardHazardRecognizer — we MUST return a non-null recognizer here for
  // pre-RA because VLIWMachineScheduler::schedule unconditionally assigns
  // the result to Top/Bot.HazardRec and later dereferences it (returning
  // nullptr would SIGSEGV). Falling back to the base implementation gives the
  // pre-RA path exactly what it had before this override existed.
  if (DAG && DAG->hasVRegLiveness())
    return TargetInstrInfo::CreateTargetMIHazardRecognizer(ItinData, DAG);
  // Thread the function's AltDescs into the HR so commitPlacementForEmit can
  // stamp setAlternateDescriptor(MemberOpcode) for leaveRegion setDesc
  // (AIEHazardRecognizer.cpp:389; AIEAlternateDescriptors.h:39-44).
  HaydnAlternateDescriptors *AltDescs = nullptr;
  if (DAG)
    AltDescs = &DAG->MF.getInfo<HaydnMachineFunctionInfo>()->getAltDescs();
  return new HaydnHazardRecognizer(this, ItinData, /*IsPreRA=*/false, AltDescs);
}

//===----------------------------------------------------------------------===//
// AIE dual-sched mutation helpers
//===----------------------------------------------------------------------===//

std::optional<int>
HaydnInstrInfo::getFirstMemoryCycle(unsigned SchedClass) const {
  // Memory ops issue on cycle 0 relative to the MI start (Bundle128 pack
  // unit). Non-memory sched classes have no memory cycle.
  const InstrItineraryData *Itin = STI.getInstrItineraryData();
  if (!Itin || Itin->isEmpty())
    return std::nullopt;
  // Itinerary present for class: treat as memory-capable if it has stages.
  const InstrStage *IS = Itin->beginStage(SchedClass);
  const InstrStage *E = Itin->endStage(SchedClass);
  if (IS == E)
    return std::nullopt;
  return 0;
}

std::optional<int>
HaydnInstrInfo::getLastMemoryCycle(unsigned SchedClass) const {
  // Loads: data returns at LoadLatency (2) → last memory cycle = 1.
  // Stores: complete on issue cycle 0.
  // Without per-opcode mayLoad, use itinerary operand span when available.
  std::optional<int> First = getFirstMemoryCycle(SchedClass);
  if (!First)
    return std::nullopt;
  const InstrItineraryData *Itin = STI.getInstrItineraryData();
  if (!Itin || Itin->isEmpty())
    return 0;
  unsigned Lat = 1;
  int FirstOp = Itin->Itineraries[SchedClass].FirstOperandCycle;
  int LastOp = Itin->Itineraries[SchedClass].LastOperandCycle;
  if (FirstOp >= 0 && LastOp > FirstOp) {
    for (int OpIdx = FirstOp; OpIdx < LastOp; ++OpIdx) {
      unsigned C = Itin->OperandCycles[OpIdx];
      if (C > Lat)
        Lat = C;
    }
  }
  // Cap last memory cycle at LoadLatency-1 (=1 for model LoadLatency=2).
  int Last = static_cast<int>(std::min(Lat, 2u)) - 1;
  return std::max(0, Last);
}

std::optional<int>
HaydnInstrInfo::getMemoryLatency(unsigned SrcSchedClass,
                                 unsigned DstSchedClass) const {
  // Product default: AIE AccurateMemEdges=false peer — class-agnostic latency 1.
  // Unconditional Last-First+1 regressed packing / densify lit (MemoryEdges ON).
  // W2.1: accurate path is opt-in only (-haydn-accurate-memory-latency).
  if (!AccurateMemoryLatency)
    return 1;

  std::optional<int> LastSrc = getLastMemoryCycle(SrcSchedClass);
  std::optional<int> FirstDst = getFirstMemoryCycle(DstSchedClass);
  if (!LastSrc || !FirstDst)
    return 1;
  // AIE-style: last cycle of producer memory → first cycle of consumer memory.
  int Lat = *LastSrc - *FirstDst + 1;
  return std::max(1, Lat);
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
    // ISA §55 / HaydnSchedModel.LoadLatency = 2
    Lat = std::max(Lat, 2u);
  }
  return Lat;
}

unsigned HaydnInstrInfo::getNumDelaySlots(const MachineInstr & /*MI*/) const {
  // Haydn has no architectural delayed-branch slots; post-RA inserts NOPs via
  // leaveMBB. RegionEndEdges uses getMaxResultLatency instead.
  return 0;
}

//===----------------------------------------------------------------------===//
// Software Pipelining (MachinePipeliner) support
//===----------------------------------------------------------------------===//

namespace {

// Get an instruction sequence from an SMS schedule estimated to have similar
// register pressure to premisched output. Reverse stage extraction: stages
// laid down in reverse order without interleaving.
// Peer: AIEBasePipelinerLoopInfo.cpp:getInstrSequence (408-429).
std::vector<MachineInstr *> getSMSInstrSequence(SMSchedule &Sched) {
  std::vector<MachineInstr *> Seq;

  for (int Stage = static_cast<int>(Sched.getMaxStageCount()); Stage >= 0;
       --Stage) {
    int FirstSeqCycle =
        Sched.getFirstCycle() + Stage * Sched.getInitiationInterval();
    int LastSeqCycle = FirstSeqCycle + Sched.getInitiationInterval() - 1;
    for (int SeqCycle = FirstSeqCycle; SeqCycle <= LastSeqCycle; ++SeqCycle) {
      for (SUnit *SU : Sched.getInstructions(SeqCycle))
        Seq.push_back(SU->getInstr());
    }
  }

  return Seq;
}

// Replay instructions in \p Seq and collect live-in registers.
// Peer: AIEBasePipelinerLoopInfo.cpp:collectLiveInRegs (431-456).
std::vector<VRegMaskOrUnit>
collectSMSLiveInRegs(const std::vector<MachineInstr *> &Seq,
                     const MachineFunction &MF) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  LiveRegSet LiveRegs;
  LiveRegs.init(MF.getRegInfo());

  for (const MachineInstr *MI : reverse(Seq)) {
    // Ignore PHI nodes: they make two values appear live-in without actually
    // increasing pressure when correctly placed/allocated.
    if (MI->isPHI())
      continue;

    RegisterOperands RegOpers;
    RegOpers.collect(*MI, *TRI, MF.getRegInfo(), true, true);
    for (const VRegMaskOrUnit &Def : RegOpers.Defs)
      LiveRegs.erase(Def);
    for (const VRegMaskOrUnit &Use : RegOpers.Uses)
      LiveRegs.insert(Use);
  }

  SmallVector<VRegMaskOrUnit> LiveInRegs;
  LiveRegs.appendTo(LiveInRegs);
  return {LiveInRegs.begin(), LiveInRegs.end()};
}

// Estimate whether RA can allocate the schedule without spilling by checking
// incoming register pressure against pressure-set limits.
// Peer: AIEBasePipelinerLoopInfo.cpp:canAllocate (458-518).
bool canAllocateSMS(SMSchedule &Sched) {
  std::vector<MachineInstr *> Seq = getSMSInstrSequence(Sched);
  if (Seq.empty())
    return true;

  for (const MachineInstr *MI : Seq)
    LLVM_DEBUG(dbgs() << "PPS-3 predicted order: " << *MI);

  MachineBasicBlock &MBB = *Seq.front()->getParent();
  MachineFunction &MF = *MBB.getParent();

  RegionPressure RegPressure;
  RegPressureTracker RPTracker(RegPressure);
  RegisterClassInfo RegClassInfo;
  RegClassInfo.runOnMachineFunction(MF);
  RPTracker.init(&MF, &RegClassInfo, nullptr, &MBB,
                 MachineBasicBlock::iterator(Seq.back()), false, false);
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  auto CheckPressureExcess = [&](const RegisterPressure &Pressure) {
    bool PressureExcess = false;
    for (unsigned I = 0, E = Pressure.MaxSetPressure.size(); I < E; ++I) {
      unsigned Limit = RegClassInfo.getRegPressureSetLimit(I);
      if (Pressure.MaxSetPressure[I] > Limit) {
        LLVM_DEBUG(dbgs() << TRI->getRegPressureSetName(I) << " Limit " << Limit
                          << " Actual " << Pressure.MaxSetPressure[I] << "\n");
        PressureExcess = true;
      }
    }
    return PressureExcess;
  };

  for (const VRegMaskOrUnit &LiveInReg : collectSMSLiveInRegs(Seq, MF)) {
    Register PrintReg =
        LiveInReg.VRegOrUnit.isVirtualReg()
            ? LiveInReg.VRegOrUnit.asVirtualReg()
            : Register(static_cast<unsigned>(LiveInReg.VRegOrUnit.asMCRegUnit()));
    LLVM_DEBUG(dbgs() << "PPS-3 add livein pressure: "
                      << printReg(PrintReg, TRI, 0, &MF.getRegInfo()) << ":"
                      << printRegClassOrBank(PrintReg, MF.getRegInfo(), TRI)
                      << "\n");

    // Ignore partially live regs — RPTracker overcounts pressure units.
    if (LiveInReg.VRegOrUnit.isVirtualReg() &&
        LiveInReg.LaneMask !=
            MF.getRegInfo().getMaxLaneMaskForVReg(
                LiveInReg.VRegOrUnit.asVirtualReg())) {
      LLVM_DEBUG(dbgs() << "PPS-3 skipped partially live reg\n");
      continue;
    }
    RPTracker.increaseRegPressure(LiveInReg.VRegOrUnit, LaneBitmask::getNone(),
                                  LiveInReg.LaneMask);
  }

  // true when no pressure set is overbooked (AIE ExcessIncomingPressure).
  bool CanAllocate = !CheckPressureExcess(RPTracker.getPressure());
  return CanAllocate;
}

} // namespace

bool HaydnPipelinerLoopInfo::shouldIgnoreForPipelining(
    const MachineInstr *MI) const {
  // Ignore the loop-control instructions -- the conditional-branch latch
  // (EndLoop) and the comparison that sets the branch condition (CmpMI). These
  // must remain in stage 0 and cannot be pipelined across stages.
  // in ZOL mode, also ignore LoopStart (preheader setup) and
  // PseudoLoopEnd (the meta latch terminator).
  if (MI == EndLoop || MI == CmpMI)
    return true;
  if (IsZOL && (MI == LoopStart ||
                MI->getOpcode() == Haydn::PseudoLoopEnd ||
                MI->getOpcode() == Haydn::LoopStart))
    return true;
  return false;
}

void HaydnPipelinerLoopInfo::recordSuccessfulSMS(
    MachineFunction &MFIn, MachineBasicBlock *KernelBB, unsigned ResMII,
    unsigned RecMII, unsigned MII, unsigned StageCount, unsigned NumOps,
    unsigned ScheduledII) {
  if (!KernelBB)
    return;
  auto &HMFI = *MFIn.getInfo<HaydnMachineFunctionInfo>();
  HaydnMachineFunctionInfo::SMSSWPSInfo Info;
  Info.ResMII = ResMII;
  Info.RecMII = RecMII;
  Info.MII = MII;
  Info.StageCount = StageCount;
  Info.NumOps = NumOps;
  Info.ScheduledII = ScheduledII;
  HMFI.recordSMSLoop(KernelBB, Info);
}

bool HaydnPipelinerLoopInfo::shouldUseSchedule(SwingSchedulerDAG &SSD,
                                               SMSchedule &SMS) {
  const unsigned PrologueCount = SMS.getMaxStageCount();
  const unsigned StageCount = PrologueCount + 1;

  // PR8 / AIE preferPostPipeliner seed: when the flag is set, defer ZOL to the
  // post-pipeliner path by rejecting every SMS schedule for ZOL form. Default
  // off so classic pre-RA SMS behavior is unchanged.
  if (IsZOL && EnableZOLPreferPostPipeliner) {
    LLVM_DEBUG(dbgs() << "ZOL: preferring post-pipeliner over SMS "
                         "(-haydn-zol-prefer-post-pipeliner)\n");
    return false;
  }

  // For ZOL loops, reject single-stage schedules (StageCount <= 1).
  // A single-stage schedule has no pipeline overlap -- it just adds
  // prologue/epilogue overhead (register copies, trip-count adjustments)
  // without any benefit. This caused +358 bundles on the corpus when ZOL
  // pipelining was first enabled with unconditional acceptance.
  // Multi-stage schedules (StageCount >= 2) have real overlap and are worth
  // the overhead. Mirrors AIE's ZeroOverheadLoop::shouldUseSchedule
  // (AIEBasePipelinerLoopInfo.cpp:826-835) which delegates to the base class
  // that rejects StageCount <= 1.
  if (IsZOL && StageCount <= 1) {
    LLVM_DEBUG(dbgs() << "ZOL: rejecting single-stage schedule (no overlap)\n");
    return false;
  }

  // AIE ZeroOverheadLoop::canAcceptII: MaxStageCount >= MinTripCount.
  // Prologue stages peel iterations; without a static min trip high enough
  // to cover them, ZOL cannot emit a dynamic guard and would mis-iterate
  // (memcpy-class variable trip, small sizes). MinTripCount==0 means
  // unknown/unbounded — refuse every multi-stage schedule (analysis may
  // still succeed so the ZOL form is recognized; SMS is not applied).
  // Peer AIEBasePipelinerLoopInfo.cpp:807-814.
  if (IsZOL &&
      (MinTripCount == 0 ||
       static_cast<int64_t>(PrologueCount) >= MinTripCount)) {
    LLVM_DEBUG(dbgs() << "ZOL: reject SMS (MaxStageCount=" << PrologueCount
                      << " MinTripCount=" << MinTripCount << ")\n");
    return false;
  }

  // PPS-3: AIE canAcceptII stage-count gate (into shouldUseSchedule — this
  // LLVM has no PipelinerLoopInfo::canAcceptII virtual). Reject schedules
  // with too many stages; high stage count forces many prologue/epilogue
  // copies and often loses to Stage-0 PostPipeliner or no-SWP.
  // Mirrors AIEBasePipelinerLoopInfo::canAcceptII MaxStageCount check
  // (AIEBasePipelinerLoopInfo.cpp:839-847).
  if (StageCount > HaydnSMSMaxStageCount) {
    LLVM_DEBUG(dbgs() << "PPS-3: reject SMS (stages=" << StageCount
                      << " > max=" << HaydnSMSMaxStageCount
                      << " II=" << SMS.getInitiationInterval() << ")\n");
    return false;
  }

  // PPS-3: AIE canAcceptII TrackRegPressure/canAllocate gate. Reject schedules
  // whose estimated kernel live-ins exceed register pressure-set limits (spill
  // risk). Peer AIEBasePipelinerLoopInfo.cpp:865-869.
  if (HaydnSMSTrackRegPressure && !canAllocateSMS(SMS)) {
    LLVM_DEBUG(dbgs() << "PPS-3: reject SMS (too much block pressure, stages="
                      << StageCount
                      << " II=" << SMS.getInitiationInterval() << ")\n");
    return false;
  }

  // For naive loops (non-ZOL), accept remaining schedules (Hexagon-like).
  return true;
}

std::optional<bool> HaydnPipelinerLoopInfo::createTripCountGreaterCondition(
    int TC, MachineBasicBlock &MBB,
    SmallVectorImpl<MachineOperand> &Cond) {
  // ZOL mode — the hardware loop counter handles the iteration count.
  // We cannot emit a dynamic guard (the ZOL terminator cannot be reversed).
  // AIE only returns true when MinTripCount > TC (static no-guard); otherwise
  // llvm_unreachable. We mirror that contract: only claim "no guard needed"
  // when MinTripCount statically exceeds the requested TC. Schedules that
  // would need a dynamic guard must already have been rejected in
  // shouldUseSchedule / analyzeLoopForPipelining.
  // Peer AIEBasePipelinerLoopInfo.cpp:750-761.
  if (IsZOL) {
    if (MinTripCount > TC)
      return true;
    LLVM_DEBUG(dbgs() << "ZOL: createTripCountGreaterCondition TC=" << TC
                      << " MinTripCount=" << MinTripCount
                      << " — cannot reverse ZOL; refuse static true\n");
    // Returning false = static "trip not greater" → expander skips/disposes.
    // Prefer this over asserting: analyze may have accepted via override.
    return false;
  }

  // Always emit a runtime "branch if TripCountReg > TC" and return nullopt
  // NEVER a compile-time static bool. This mirrors the ARM reference
  // implementation (ARMBaseInstrInfo.cpp::ARMPipelinerLoopInfo), which has no
  // static-trip-count path whatsoever. Returning a static bool here was the
  // Blocker-1 silent-wrong-code root cause: a hand-rolled `(limit-init)/step`
  // value drove `PeelingModuloScheduleExpander::fixupBranches`
  // (ModuloSchedule.cpp:1980-1999) into the static-false (`KernelDisposed`)
  // branch, collapsing countable loops (e.g. dot_product_16, trip 16) to ~1
  // iteration with `-verify-machineinstrs` still green. See.
  //
  // `analyzeLoopForPipelining` rejects any loop without a usable runtime
  // trip-count register, so TripCountReg must be valid here.
  assert(TripCountReg.isValid() && "pipelined loop must have a runtime TC reg");

  // TripCountReg > TC <=> NOT (TripCountReg < TC + 1)
  // <=> BEQZ (SLT32 TripCountReg, TC+1)
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetRegisterClass *RC = &Haydn::GPR32RegClass;
  DebugLoc BranchDL = MBB.findBranchDebugLoc();

  // Materialize (TC + 1) into a register.
  Register CmpReg = MRI.createVirtualRegister(RC);
  if (isInt<16>(TC + 1)) {
    BuildMI(&MBB, BranchDL, HII->get(Haydn::LOADI32), CmpReg).addImm(TC + 1);
  } else {
    BuildMI(&MBB, BranchDL, HII->get(Haydn::LUI), CmpReg)
        .addImm(((static_cast<uint32_t>(TC + 1) + 0x8000) >> 16) & 0xFFFF);
    BuildMI(&MBB, BranchDL, HII->get(Haydn::ADDI32), CmpReg)
        .addReg(CmpReg)
        .addImm((TC + 1) & 0xFFFF);
  }

  // CmpResult = (TripCountReg < TC + 1)
  Register CmpResult = MRI.createVirtualRegister(RC);
  BuildMI(&MBB, BranchDL, HII->get(Haydn::SLT32), CmpResult)
      .addReg(TripCountReg)
      .addReg(CmpReg);

  // fix: upstream contract (see Hexagon's J2_jumpf reference and
  // PeelingModuloScheduleExpander::fixupBranches / placeRematerializersCall
  // call sites in ModuloSchedule.cpp:886,1975) requires the Cond to be TRUE
  // (branch-taken) when the trip count is NOT greater than TC, i.e. when the
  // prologue should be SKIPPED. CmpResult = (TripCountReg < TC+1) is true when
  // trip <= TC. To branch on that "skip" condition we must fire when CmpResult
  // != 0, hence BNEZ. The previous BEQZ fired when trip > TC (CmpResult ==
  // 0), reversing the guard and dead-stripping every pipelined loop with trip
  // > stage count (counting-sort, vec-max). Phase 1b: emit the
  // WIDE 48-bit form so insertBranch / AsmPrinter produce a WIDE parcel.
  Cond.push_back(MachineOperand::CreateImm(Haydn::BNEZ));
  Cond.push_back(MachineOperand::CreateReg(CmpResult, false));
  return {};
}

void HaydnPipelinerLoopInfo::adjustTripCount(int TripCountAdjust) {
  // ZOL mode — edit LoopStart's simm6:$adj operand directly.
  // Mirrors AIE's ZeroOverheadLoop::adjustTripCount
  // (AIEBasePipelinerLoopInfo.cpp:763-768).
  if (IsZOL) {
    assert(LoopStart && "ZOL pipelined loop must have a LoopStart");
    // LoopStart has operands: $src (reg), $adj (simm6). The adj field is
    // the pipeliner's trip-count adjustment — add the delta to it.
    int64_t CurAdj = LoopStart->getOperand(1).getImm();
    LoopStart->getOperand(1).setImm(CurAdj + TripCountAdjust);
    return;
  }

  // Runtime trip count: subtract the prolog stage count from TripCountReg.
  // TripCountAdjust is the (negative) delta the expander wants applied.
  // There is no static path -- see createTripCountGreaterCondition.
  assert(TripCountReg.isValid() && "pipelined loop must have a runtime TC reg");

  MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetRegisterClass *RC = &Haydn::GPR32RegClass;
  Register NewTC = MRI.createVirtualRegister(RC);

  int64_t Adj = -TripCountAdjust;
  // Use the cached LoopBB (the original loop body).
  MachineBasicBlock *LoopBB = this->LoopBB;
  if (isInt<16>(Adj)) {
    BuildMI(*LoopBB, LoopBB->getFirstNonPHI(), DL, HII->get(Haydn::ADDI32),
            NewTC)
        .addReg(TripCountReg)
        .addImm(Adj);
  } else {
    Register AdjReg = MRI.createVirtualRegister(RC);
    BuildMI(*LoopBB, LoopBB->getFirstNonPHI(), DL, HII->get(Haydn::LOADI32),
            AdjReg)
        .addImm(Adj);
    BuildMI(*LoopBB, LoopBB->getFirstNonPHI(), DL, HII->get(Haydn::ADD32),
            NewTC)
        .addReg(TripCountReg)
        .addReg(AdjReg);
  }

  // Rewrite all subsequent uses of the trip-count register.
  MRI.replaceRegWith(TripCountReg, NewTC);
}

void HaydnPipelinerLoopInfo::setPreheader(MachineBasicBlock *NewPreheader) {
  // No-op. SMS runs PRE-RA on ZOL form : the IR-level
  // HardwareLoops pass has already emitted LoopStart (preheader) +
  // PseudoLoopEnd (latch) before the pipeliner runs. The expander clones the
  // ZOL terminator into its new preheader/prologue/epilogue blocks directly;
  // adjustTripCount edits the LoopStart $adj operand already present in the
  // expander's new preheader, so no additional target splice is needed here.
}

namespace {

// True if \p Opc is a Haydn ADD/SUB that can serve as a loop-carried induction
// step (the bump of an IV update). ADD32/SUB32 have two register sources;
// ADDI32 has one register source and an immediate step. The IV candidate is
// the step's non-immediate register source. Accepts both the legacy base
// opcodes and their `_S<k>` Selector-emitted variants.
static bool isInductionStep(unsigned Opc, const MCInstrInfo &MII) {
  unsigned Base = getHaydnFlexBaseOpcode(Opc, MII);
  return Base == Haydn::ADD32 || Base == Haydn::ADDI32 || Base == Haydn::SUB32;
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
// ADD32/SUB32 carry it in a register source, which LSR materializes as
// ADDI32 $r0, imm -- chase that one hop. Returns true and sets \p Step on
// success; false if the step is not a recoverable constant (the caller then
// conservatively rejects the loop, mirroring AIE's isConstStep requirement).
static bool getInductionStep(const MachineRegisterInfo &MRI,
                             const MCInstrInfo &MII,
                             const MachineInstr &BumpMI, int64_t &Step) {
  unsigned Opc = BumpMI.getOpcode();
  unsigned Base = getHaydnFlexBaseOpcode(Opc, MII);
  if (Base == Haydn::ADDI32) {
    if (!BumpMI.getOperand(2).isImm())
      return false;
    Step = BumpMI.getOperand(2).getImm();
    return true;
  }
  // ADD32/SUB32: the step is a register source. LSR materializes constant
  // steps as ADDI32 $r0, imm; recover it (the non-IV operand -- whichever
  // source is defined by such an ADDI32). The materialized step itself may be
  // a `_S<k>` variant, so resolve it through getHaydnFlexBaseOpcode too.
  for (const MachineOperand &MO : BumpMI.explicit_uses()) {
    if (!MO.isReg() || !MO.getReg().isVirtual())
      continue;
    const MachineInstr *Def = MRI.getVRegDef(MO.getReg());
    if (!Def)
      continue;
    unsigned DefBase = getHaydnFlexBaseOpcode(Def->getOpcode(), MII);
    if (DefBase != Haydn::ADDI32)
      continue;
    if (Def->getOperand(1).getReg() != Haydn::R0 || !Def->getOperand(2).isImm())
      continue;
    int64_t V = Def->getOperand(2).getImm();
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
static bool isHaydnCondBranch(unsigned Opc) {
  switch (Opc) {
  case Haydn::BEQZ:
  case Haydn::BNEZ:
  case Haydn::BGEZ:
  case Haydn::BLTZ:
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

// True if \p LoopBB has a PHI whose latch-incoming is defined by another PHI
// in the same block (shift-register / delay-line chain).
// Classic ModuloScheduleExpander mis-rewrites such chains into epilog PHIs
// that use a same-block PHI result as a predecessor live-in, e.g.:
// bb.epilog:
// %A = PHI %x, %pred,...
// %B = PHI %A, %pred,...; %A is not live-out of %pred
// That breaks SSA; LiveVariables later asserts
// "Can't find reaching def for virtreg". Prefer declining SMS over incorrect
// code (correctness > SWPS). Observed on NatureDSP 32x16 FIR
// (bkfir32x16 / bkfira32x16 / fir_xcorr32x16 / firdec32x16 / firinterp32x16)
// where sliding-window delay PHIs form PHI→PHI latch edges. Bisect:
// enable-pipeliner=0 avoids the crash; -haydn-enable-hwloops=0 does not.
static bool hasShiftRegisterPhiChain(MachineBasicBlock *LoopBB) {
  MachineRegisterInfo &MRI = LoopBB->getParent()->getRegInfo();
  for (const MachineInstr &MI : LoopBB->phis()) {
    Register LatchIn = getPHILatchIncoming(MI, LoopBB);
    if (!LatchIn.isVirtual())
      continue;
    const MachineInstr *Def = MRI.getVRegDef(LatchIn);
    if (Def && Def->isPHI() && Def->getParent() == LoopBB)
      return true;
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
                              MachineInstr *&CmpMI, Register &TripCountReg) {
  MachineFunction *MF = LoopBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  EndLoop = nullptr;
  CmpMI = nullptr;
  TripCountReg = Register();

  // Decline SMS on shift-register PHI chains — see hasShiftRegisterPhiChain.
  if (hasShiftRegisterPhiChain(LoopBB)) {
    LLVM_DEBUG(dbgs() << "SMS: reject loop with shift-register PHI chain "
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
  // Accept both the legacy base compare opcodes and their `_S<k>`
  // Selector-emitted variants : the GISel InstructionSelect pass emits
  // `SLT32_S0` etc. directly, so the naive recognizer must key on the
  // base semantic opcode via getHaydnFlexBaseOpcode.
  unsigned CondOpc = getHaydnFlexBaseOpcode(CondDef->getOpcode(), MII);
  // GISel emitInvert01 / CondOpt Pattern B (logical-not of a 0/1 cmp):
  //   SEQ/SLT/SLTU rd, a, b
  //   XORI32       re, rd, 1
  //   BNEZ/BEQZ    re, ...
  // After branch-polarity canonicalization almost every countable latch
  // looks like this. Peel the invert so SMS keys on the real compare;
  // without the peel, analyzeLoop rejects 100% of real naive-path loops.
  if (CondOpc == Haydn::XORI32 && CondDef->getNumOperands() >= 3 &&
      CondDef->getOperand(1).isReg() && CondDef->getOperand(2).isImm() &&
      CondDef->getOperand(2).getImm() == 1) {
    Register CmpSrc = CondDef->getOperand(1).getReg();
    if (!CmpSrc.isVirtual())
      return false;
    MachineInstr *MaybeCmp = MRI.getVRegDef(CmpSrc);
    if (!MaybeCmp || MaybeCmp->getParent() != LoopBB)
      return false;
    CondDef = MaybeCmp;
    CondOpc = getHaydnFlexBaseOpcode(CondDef->getOpcode(), MII);
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

  // Derive the runtime trip-count register. The correct source depends on the
  // IV direction :
  // DECREMENTING IV (step < 0, e.g. `iv -= 1` until `iv == 0`): the IV
  // starts at the trip count N and counts down to 0, so the trip count is
  // the IV's preheader *init* value. The compare's other operand is 0
  // (often a function-wide materialized zero like `ADDI32 $r0, 0`), NOT
  // the trip count -- using it was the matrix_test crash + silent
  // wrong-code root cause: adjustTripCount did replaceRegWith on that
  // shared zero register, corrupting every compare/PHI-init in the
  // function, and createTripCountGreaterCondition emitted `SLT32 0, TC+1`
  // which is always-true (the guard never disabled the kernel).
  // INCREMENTING IV (step > 0, e.g. `iv += 1` until `iv < limit`): the
  // compare's non-IV operand is the upper bound; when the init is 0 (the
  // common LSR shape) it equals the trip count, so fall back to it.
  // AIE's DownCountLoop resolves the trip count from the IV init the same way
  // (sms-packetizer-deep-dive.md §2c); we additionally keep the incrementing
  // fallback so existing synthetic tests (init==0, limit==N) keep working.
  Register InitReg =
      IVPhi ? getPHIPreheaderIncoming(*IVPhi, LoopBB) : Register();
  if (Step < 0) {
    // Decrementing: trip count = IV init. Must be a usable register.
    if (!InitReg.isValid() || !InitReg.isVirtual())
      return false;
    TripCountReg = InitReg;
  } else {
    // Incrementing (or unknown step): use the compare's non-IV operand. This
    // is correct when the IV init is 0; for a non-zero init it is an
    // approximation, but SMS's runtime guard still bounds the kernel safely
    // as long as LimitReg is a real register.
    if (!LimitReg.isValid() || !LimitReg.isVirtual())
      return false;
    TripCountReg = LimitReg;
  }

  return true;
}

} // end anonymous namespace

bool HaydnInstrInfo::analyzeCountableLoop(MachineBasicBlock *LoopBB,
                                          HaydnCountableLoop &Out) const {
  MachineInstr *EndLoop = nullptr;
  MachineInstr *CmpMI = nullptr;
  Register TripCountReg;
  if (!analyzeSimpleLoop(LoopBB, *this, EndLoop, CmpMI, TripCountReg) ||
      !EndLoop)
    return false;
  Out.EndLoop = EndLoop;
  Out.CmpMI = CmpMI;
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
    if ((Opc == Haydn::ADDI32) &&
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
// Without a known MinTripCount > 1, ZOL SMS is refused (cannot guard).
int64_t computeZOLMinTripCount(MachineInstr *LoopStartMI,
                               MachineBasicBlock *LoopBB) {
  int64_t MinTC = 0;

  // Optional floor from -haydn-loop-min-tripcount (AIE aie-loop-min-tripcount).
  if (HaydnLoopMinTripCount > 0)
    MinTC = HaydnLoopMinTripCount;

  // Constant trip from LoopStart's source register.
  if (LoopStartMI && LoopStartMI->getNumOperands() >= 1 &&
      LoopStartMI->getOperand(0).isReg()) {
    const MachineRegisterInfo &MRI =
        LoopStartMI->getParent()->getParent()->getRegInfo();
    if (auto C = getHaydnConstantImm(LoopStartMI->getOperand(0).getReg(), MRI)) {
      // LoopStart adj is the SMS delta (starts 0); InitVal is the HW trip.
      if (*C > MinTC)
        MinTC = *C;
    }
  }

  (void)LoopBB; // reserved for future llvm.loop MD min-trip (AIE MD path)
  return MinTC;
}

} // namespace

std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo>
HaydnInstrInfo::analyzeLoopForPipelining(MachineBasicBlock *LoopBB) const {
  // Check for ZOL (Zero-Overhead Loop) form. The IR-level HardwareLoops pass
  // runs before IRTranslator, so it has ALREADY converted every countable
  // single-BB loop to LoopStart (preheader) + PseudoLoopEnd (latch) by the time
  // SMS runs. SMS pipelines it via the ZOL PipelinerLoopInfo
  // (adjustTripCount edits LoopStart's $adj operand). EnableZOLPipelining
  // defaults ON ("SMS runs PRE-RA on ZOL form"); the cl::opt remains for
  // emergency disable. When off, SMS skips ZOL loops — they still form
  // hwloops via the IR-level pass, just without software pipelining.
  MachineBasicBlock::iterator TermIt = LoopBB->getFirstTerminator();
  if (TermIt != LoopBB->end() &&
      TermIt->getOpcode() == Haydn::PseudoLoopEnd) {
    if (!EnableZOLPipelining)
      return nullptr; // ZOL loop — don't pipeline (keep hwloop, skip SMS).
    // Same PHI-chain hazard as the naive path (hasShiftRegisterPhiChain).
    if (hasShiftRegisterPhiChain(LoopBB)) {
      LLVM_DEBUG(dbgs() << "SMS: reject ZOL loop with shift-register PHI chain "
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
          // when MinTC is unknown or too small to cover peeled prologues
          // (ZOL cannot emit a dynamic guard). This preserves the analyzability
          // contract (swpipeline-zol-countable-analyzable.ll) while keeping
          // SMS product-safe for memcpy-class variable-trip loops.
          int64_t MinTC = computeZOLMinTripCount(&MI, LoopBB);
          if (MinTC <= 1) {
            LLVM_DEBUG(dbgs() << "ZOL: analyze OK but MinTripCount=" << MinTC
                              << " (SMS schedules gated in shouldUseSchedule)\n");
          }
          MachineFunction *MF = LoopBB->getParent();
          return std::make_unique<HaydnPipelinerLoopInfo>(MF, this, Term, &MI,
                                                           MinTC);
        }
      }
    }
    // ZOL loop without findable LoopStart — don't pipeline.
    return nullptr;
  }

  // Naive countable loop path (SEQ32/SLT32 + BNEZ/BEQZ).
  HaydnCountableLoop L;
  if (!analyzeCountableLoop(LoopBB, L))
    return nullptr;

  MachineFunction *MF = LoopBB->getParent();
  return std::make_unique<HaydnPipelinerLoopInfo>(MF, this, L.EndLoop, L.CmpMI,
                                                   L.TripCountReg);
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

// Element → byte scale for scaled-imm post/pre forms.
unsigned updateAMScaleShift(const TargetInstrInfo &TII, unsigned Opc) {
  StringRef N = TII.getName(Opc);
  // 64-bit double-word streams (D_LDW / D_SDW / LD64 / ST64).
  if (N.contains("LDW") || N.contains("SDW") || N.contains("LD64") ||
      N.contains("ST64"))
    return 3;
  // Halfword.
  if (N.contains("LHW") || N.contains("SHW") || N.contains("LHWS") ||
      N.contains("LHWU"))
    return 1;
  // Byte.
  if (N.contains("LBS") || N.contains("LBU") || N.contains("SB"))
    return 0;
  // Default word (S_LW / S_SW / LD32 / ST32 / D_LW / D_SW_*).
  return 2;
}

LocationSize updateAMAccessWidth(const TargetInstrInfo &TII, unsigned Opc) {
  StringRef N = TII.getName(Opc);
  // 64-bit data (DR64 load/store family).
  if (N.contains("LD64") || N.contains("ST64") || N.contains("LDW") ||
      N.contains("SDW") || N.contains("D_LW") || N.contains("D_LHW"))
    return LocationSize::precise(8);
  // Halfword.
  if (N.contains("LHW") || N.contains("SHW") || N.contains("LHWS") ||
      N.contains("LHWU"))
    return LocationSize::precise(2);
  // Byte.
  if (N.contains("LBS") || N.contains("LBU") ||
      (N.contains("SB") && !N.contains("SBE")))
    return LocationSize::precise(1);
  // Default word.
  return LocationSize::precise(4);
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
    if (!MI.getOperand(BasePos).isReg())
      return false;
    // REG forms: offset is a register — still report positions; callers that
    // require Imm (getIncrementValue) check isImm themselves.
    return true;
  }

  // Plain base+imm loads/stores used by canUseLastOffsetValue rewrite.
  switch (Opc) {
  case Haydn::S_LW_WITH_IMM:
  case Haydn::D_LDW_WITH_IMM:
    BasePos = 1;
    OffsetPos = 2;
    break;
  case Haydn::S_SW_WITH_IMM:
  case Haydn::D_SDW_WITH_IMM:
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
    unsigned Shift = updateAMScaleShift(*this, Opc);
    int64_t Bytes = Imm << Shift;
    if (!isInt<32>(Bytes))
      return false;
    Value = static_cast<int>(Bytes);
    return true;
  }

  // Plain ADDI is the split post-inc fallback (and common IV step).
  if (Opc == Haydn::ADDI32) {
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
  unsigned Opc = MI.getOpcode();
  HaydnUpdateAM AM = classifyUpdateAM(*this, Opc);

  if (AM == HaydnUpdateAM::PostIncPseudo) {
    if (!MI.mayLoad() && !MI.mayStore())
      return false;
    // Access at base+offset (op3); post-update is op2 stride (not EA offset).
    if (MI.getNumOperands() < 4 || !MI.getOperand(1).isReg() ||
        !MI.getOperand(3).isImm())
      return false;
    BaseOps.push_back(&MI.getOperand(1));
    Offset = MI.getOperand(3).getImm();
    Width = MI.mayLoad() && Opc == Haydn::LD64_POST_INC
                ? LocationSize::precise(8)
                : (MI.mayStore() && Opc == Haydn::ST64_POST_INC
                       ? LocationSize::precise(8)
                       : LocationSize::precise(4));
    return true;
  }

  if (AM == HaydnUpdateAM::PostImm || AM == HaydnUpdateAM::PostReg) {
    // Post-*: EA is [rs] (offset 0); delta is writeback only.
    if (MI.getNumOperands() < 4 || !MI.getOperand(2).isReg())
      return false;
    BaseOps.push_back(&MI.getOperand(2));
    Offset = 0;
    Width = updateAMAccessWidth(*this, Opc);
    return true;
  }

  if (AM == HaydnUpdateAM::PreImm) {
    // Pre-imm: EA is [rs + imm<<scale] relative to the pre-update base that
    // SMS tracks (the rs use before writeback).
    if (MI.getNumOperands() < 4 || !MI.getOperand(2).isReg() ||
        !MI.getOperand(3).isImm())
      return false;
    BaseOps.push_back(&MI.getOperand(2));
    Offset = MI.getOperand(3).getImm() << updateAMScaleShift(*this, Opc);
    Width = updateAMAccessWidth(*this, Opc);
    return true;
  }

  if (AM == HaydnUpdateAM::PreReg) {
    // Pre-reg: EA depends on a register delta — not a fixed offset.
    if (MI.getNumOperands() < 4 || !MI.getOperand(2).isReg())
      return false;
    BaseOps.push_back(&MI.getOperand(2));
    Offset = 0;
    Width = updateAMAccessWidth(*this, Opc);
    return true;
  }

  // Plain LD/ST base+imm.
  // Plain `<base> + imm` forms. Offset is returned in BYTES, and the
  // immediate is an ELEMENT INDEX -- `simm6:$scaled_imm`, EA = rs + (imm <<
  // log2(width)) -- so it has to be scaled on the way out. It was not, which
  // shrank every distance by the access width: two words at elements 0 and 1
  // are 4 bytes apart and looked 1 apart, i.e. overlapping. The PreImm path
  // above already had the shift; this one never did.
  //
  // The byte and halfword forms were missing entirely, so they reported "no
  // information" and every consumer had to assume the worst about them.
  {
    unsigned W = 0;
    switch (Opc) {
    case Haydn::S_LBS_WITH_IMM:
    case Haydn::S_LBU_WITH_IMM:
    case Haydn::S_SB_WITH_IMM:
      W = 1;
      break;
    case Haydn::S_LHWS_WITH_IMM:
    case Haydn::S_LHWU_WITH_IMM:
    case Haydn::S_SHW_WITH_IMM:
      W = 2;
      break;
    case Haydn::S_LW_WITH_IMM:
    case Haydn::S_SW_WITH_IMM:
      W = 4;
      break;
    case Haydn::D_LDW_WITH_IMM:
    case Haydn::D_SDW_WITH_IMM:
      W = 8;
      break;
    default:
      break;
    }
    if (W) {
      if (MI.getNumOperands() < 3 || !MI.getOperand(1).isReg() ||
          !MI.getOperand(2).isImm())
        return false;
      BaseOps.push_back(&MI.getOperand(1));
      Offset = MI.getOperand(2).getImm() * (int64_t)W;
      Width = LocationSize::precise(W);
      return true;
    }
  }
  switch (Opc) {
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

  // The interface's own contract: assume any register used to compute an
  // address holds the same value in both instructions. That is what makes a
  // bare base comparison sound here, and it is why this is a post-RA question.
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  SmallVector<const MachineOperand *, 4> BaseOpsA, BaseOpsB;
  int64_t OffsetA = 0, OffsetB = 0;
  bool ScalableA = false, ScalableB = false;
  LocationSize WidthA = LocationSize::precise(0),
               WidthB = LocationSize::precise(0);

  if (!getMemOperandsWithOffsetWidth(MIa, BaseOpsA, OffsetA, ScalableA, WidthA,
                                     TRI) ||
      !getMemOperandsWithOffsetWidth(MIb, BaseOpsB, OffsetB, ScalableB, WidthB,
                                     TRI))
    return false;

  // Haydn never reports a scalable offset, but a false here is the safe answer
  // if that ever changes rather than a comparison of incomparable units.
  if (ScalableA || ScalableB)
    return false;
  if (BaseOpsA.size() != 1 || BaseOpsB.size() != 1)
    return false;
  if (!BaseOpsA[0]->isIdenticalTo(*BaseOpsB[0]))
    return false;
  if (!WidthA.hasValue() || !WidthB.hasValue())
    return false;

  // Offsets come back in BYTES (§ 5.18 — the immediate is an element index and
  // the accessor scales it), so this compares like with like.
  const int64_t LowOffset = std::min(OffsetA, OffsetB);
  const int64_t HighOffset = std::max(OffsetA, OffsetB);
  const LocationSize LowWidth = (LowOffset == OffsetA) ? WidthA : WidthB;
  return LowOffset + static_cast<int64_t>(LowWidth.getValue()) <= HighOffset;
}

