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
#include "HaydnPipelinerLoopInfo.h"
#include "Haydn.h"
#include "HaydnFrameLowering.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnFormatERecords.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnMachineFunctionInfo.h"
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
#include "llvm/MC/MCContext.h"
#include <algorithm>
#include <optional>
#include <vector>

#define DEBUG_TYPE "haydn-instr-info"

using namespace llvm;

namespace {

// HaydnFinalizeBundle: every real MI is a BUNDLE root. BranchRelaxation
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

/// AIE getAlternateInstsOpcode inverse (member → logical). Already-logical
/// opcodes are identity. Missing generated members return the opcode itself
/// only through logicalOpcodeOrSelf; lookupGeneratedMemberToLogical is 0.
unsigned haydnLogicalOpcode(unsigned Opc) {
  return haydn::format_e::logicalOpcodeOrSelf(Opc);
}

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

// Hexagon manner (HexagonBranchRelaxation.cpp:37, 95-114, 109-111): BR has no
// exact final layout, so computeOffset charges alignment + extender growth
// and a small BranchRelaxSafetyBuffer (default 200) remains. Haydn's generic
// BranchRelaxation only sums getInstSizeInBytes, so named late-layout growth
// (same-slot serial parcels, JT R0 re-zero, hwloop setup pads) is charged
// there. The distance buffer is then only residual layout uncertainty —
// one insertIndirectBranch sequence (MaxSingleBranchGrowthBytes), not 1024.
// AIE has empty addPreEmitPass (no BR) — N/A there.
static cl::opt<uint32_t> BranchRelaxSafetyBuffer(
    "haydn-branch-relax-safety-buffer", cl::Hidden,
    cl::init(static_cast<uint32_t>(haydn::hwloop::MaxSingleBranchGrowthBytes)),
    cl::desc("Extra bytes added to branch distance when deciding if a "
             "conditional is in WIDE_BranchSImm12 range (GE96-03 signed "
             "12-bit byte PC+imm; Hexagon-style safety buffer). Default is "
             "MaxSingleBranchGrowthBytes after named growth is charged in "
             "getInstSizeInBytes."));

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
    // GPR32 → GPR32: MOVE32 rd, rs, rs (register move).
    // The .td models MOVE32 with two source operands ($rs1, $rs2) because the
    // R-type encoding (FmtALU32) has separate rs1/rs2 bit fields, and both
    // must be populated for a deterministic encoding. copyPhysReg therefore
    // passes SrcReg twice. Each explicit field reserves one GPR read port
    // (2R1W). OR32 rd, rs, rs is a true two-source op; MOVE32 remains the
    // canonical copy form.
    BuildMI(MBB, MI, DL, get(Haydn::MOVE32), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc))
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
    if (I->isDebugInstr() || I->isCFIInstruction())
      continue;

 // : BUNDLE roots wrap real terminators — analyze the child opcode
    // operands (unwrapBundleControlFlow). MBB::iterator never yields children.
    MachineInstr &CF = unwrapBundleControlFlow(*I);

    // Returns are not analyzable as branches.
    if (CF.isReturn(MachineInstr::IgnoreBundle))
      return true;

    // Generic opcodes (pre-selection) are not analyzable.
    if (isPreISelGenericOpcode(CF.getOpcode()))
      return true;

    unsigned Opc = haydnLogicalOpcode(CF.getOpcode());

    // Unconditional branches
    bool IsUnconditional = false;
    MachineBasicBlock *Target = nullptr;

    if (Opc == Haydn::B && CF.getNumOperands() > 0 &&
        CF.getOperand(0).isMBB()) {
      IsUnconditional = true;
      Target = CF.getOperand(0).getMBB();
    } else if ((Opc == Haydn::JAL || Opc == Haydn::JAL_W) &&
               CF.getNumOperands() > 1 && CF.getOperand(0).isReg() &&
               CF.getOperand(0).getReg() == Haydn::R0 &&
               CF.getOperand(1).isMBB()) {
      // Phase 1a: CodeGen now selects JAL_W; legacy JAL kept for the
      // asm parser / decoder. Both have the same (rd, target) operand shape.
      IsUnconditional = true;
      Target = CF.getOperand(1).getMBB();
    } else if (Opc == Haydn::BEQZ_W && CF.getNumOperands() > 1 &&
               CF.getOperand(0).isReg() &&
               CF.getOperand(0).getReg() == Haydn::R0 &&
               CF.getOperand(1).isMBB() &&
               !MBB.getParent()->getRegInfo().isLiveIn(Haydn::R0)) {
      // BEQZ_W R0 is only unconditional when R0 is not a function argument
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
    if (Opc == Haydn::JALR || Opc == Haydn::JALR_W) {
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
    // (yarpgen soft-div pattern: `JAL_W &__divsi3; BNE_W...`). Stop
    // scanning and keep the branch analysis. Without this, BranchRelaxation
    // asserts "branches to be relaxed must be analyzable" whenever
    // a far cond-branch sits after a call in the same MBB.
    // JAL/JAL_W libcall (non-MBB target): mid-block after a parsed branch →
    // stop and keep analysis. As sole terminator (noreturn abort): continue
    // so empty Cond means analyzable fallthrough for MBP (20000815-1).
    // Only for non-terminator calls — true terminators stay unanalyzable.
    if (Opc == Haydn::JAL || Opc == Haydn::JAL_W) {
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

    // Conditional branches (1 register). Logical opcode after generated
    // member→logical inverse (covers residual `_S*` and Format E members).
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

  if (Cond.empty()) {
    // Unconditional branch — B pseudo (isBarrier=1). Survives analyzeBranch;
    // expandPostRAPseudo / AsmPrinter lower to BEQZ_W R0 when still bare.
    MachineInstr &MI = *BuildMI(MBB, MBB.end(), DL, get(Haydn::B)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    return 1;
  }

  // Conditional branch
  unsigned Opc = haydnLogicalOpcode(Cond[0].getImm());

  // Hardware-loop terminators. PseudoLoopEnd has no register operand
  // (Cond = [Imm] only); LoopJNZ has one register (Cond = [Imm, reg]).
  // Both use addMBB(TBB) for the target. When FBB is non-null (two-way)
  // append an unconditional B to FBB after the conditional.
  // Meta zero-byte markers stay bare (getInstSizeInBytes → 0).
  if (Opc == Haydn::PseudoLoopEnd) {
    MachineInstr &MI = *BuildMI(MBB, MBB.end(), DL, get(Opc)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    if (FBB) {
      MachineInstr &BMI =
          *BuildMI(MBB, MBB.end(), DL, get(Haydn::B)).addMBB(FBB);
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
      MachineInstr &BMI =
          *BuildMI(MBB, MBB.end(), DL, get(Haydn::B)).addMBB(FBB);
      if (BytesAdded)
        *BytesAdded += getInstSizeInBytes(BMI);
      return 2;
    }
    return 1;
  }

  if (FBB == nullptr) {
    // One-way conditional: if Cond, goto TBB; else fall through.
    MachineInstrBuilder MIB = BuildMI(MBB, MBB.end(), DL, get(Opc));
    if (isHaydnCondBranch1Reg(Opc)) {
      MIB.addReg(Cond[1].getReg());
    } else {
      MIB.addReg(Cond[1].getReg()).addReg(Cond[2].getReg());
    }
    MIB.addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(*MIB);
    return 1;
  }

  // Two-way conditional: if Cond, goto TBB; else goto FBB.
  MachineInstrBuilder MIB = BuildMI(MBB, MBB.end(), DL, get(Opc));
  if (isHaydnCondBranch1Reg(Opc)) {
    MIB.addReg(Cond[1].getReg());
  } else {
    MIB.addReg(Cond[1].getReg()).addReg(Cond[2].getReg());
  }
  MIB.addMBB(TBB);
  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(*MIB);
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

    // Post-pack / late-commit: branch terminators are often BUNDLE roots.
    // isBranch() defaults to AnyInBundle so a committed branch cycle matches
    // on the root. MBB::iterator never yields interior children — use
    // standard bundle iterators (same as analyzeBranch unwrap).
    MachineInstr &Top = *I;
    MachineInstr &CF = unwrapBundleControlFlow(Top);
    if (!CF.isBranch(MachineInstr::IgnoreBundle))
      break;

    if (Top.isBundle()) {
      // Classify children: branch vs real coissue vs padding (meta/NOP/debug).
      SmallVector<MachineInstr *, 4> BranchKids;
      unsigned RealNonBranch = 0;
      MachineBasicBlock::instr_iterator Child =
          std::next(Top.getIterator());
      MachineBasicBlock::instr_iterator End = MBB.instr_end();
      for (; Child != End && Child->isInsideBundle(); ++Child) {
        if (isControlFlowChild(*Child) &&
            Child->isBranch(MachineInstr::IgnoreBundle)) {
          BranchKids.push_back(&*Child);
          continue;
        }
        if (isTerminatorCyclePadding(*Child))
          continue;
        ++RealNonBranch;
      }

      if (BranchKids.empty())
        break;

      if (RealNonBranch == 0) {
        // Solo branch cycle (branch ± idle padding): charge committed
        // EncodedBytes on the root and erase the whole cycle atomically.
        // MBB::erase(iterator) deletes header + children via the bundle range.
        // lateLayoutBytes matches insertBranch BytesAdded oracle.
        if (BytesRemoved)
          *BytesRemoved +=
              static_cast<int>(haydn::bundle::lateLayoutBytes(Top));
        Count += BranchKids.size();
        I = MBB.erase(I);
        continue;
      }

      // Coissued BUNDLE{branch, real…}: strip only branch children, then
      // re-solve the surviving membership (FixupHwLoops.cpp:338-388
      // recommitSurvivingCycleMembers peer). Finalize skips already-bundled
      // roots (HaydnFinalizeBundle.cpp:54), so leaving a stale row/completion
      // stamp would let MC serialize the pre-strip Format E identity.
      Count += BranchKids.size();
      SmallVector<MachineInstr *, 4> Keep;
      MachineBasicBlock::instr_iterator ChildIt =
          std::next(Top.getIterator());
      MachineBasicBlock::instr_iterator ChildEnd = MBB.instr_end();
      for (; ChildIt != ChildEnd && ChildIt->isInsideBundle(); ++ChildIt) {
        if (isControlFlowChild(*ChildIt) &&
            ChildIt->isBranch(MachineInstr::IgnoreBundle))
          continue;
        if (isTerminatorCyclePadding(*ChildIt))
          continue;
        Keep.push_back(&*ChildIt);
      }

      auto unbundleMI = [](MachineInstr *MI) {
        if (!MI)
          return;
        if (MI->isBundledWithPred())
          MI->unbundleFromPred();
        if (MI->isBundledWithSucc())
          MI->unbundleFromSucc();
      };
      for (MachineInstr *K : Keep)
        unbundleMI(K);
      for (MachineInstr *Br : BranchKids) {
        unbundleMI(Br);
        Br->eraseFromParent();
      }
      if (Top.getParent() && Top.isBundle()) {
        MachineBasicBlock::instr_iterator Next = std::next(Top.getIterator());
        if (Next == MBB.instr_end() || !Next->isBundledWithPred())
          Top.eraseFromParent();
      }

      // Re-stamp survivors: singleton wrap, or exact multi-MI commit.
      // Fall back to per-member singletons if the remainder is no longer
      // one legal product cycle (same as Fixup after SET-member erase).
      if (Keep.size() == 1) {
        MachineInstr *MI = Keep[0];
        unsigned Member = haydn::bundle::lateProductMemberOpcode(MI->getOpcode());
        if (Member != MI->getOpcode())
          bakeFormatEMemberDesc(*MI, Member, *this);
        haydn::bundle::finalizeExactLateSingleton(*MI);
      } else if (Keep.size() > 1) {
        if (!haydn::bundle::commitOneProductCycle(Keep)) {
          for (MachineInstr *MI : Keep) {
            if (!MI || !MI->getParent())
              continue;
            unsigned Member =
                haydn::bundle::lateProductMemberOpcode(MI->getOpcode());
            if (Member != MI->getOpcode())
              bakeFormatEMemberDesc(*MI, Member, *this);
            haydn::bundle::finalizeExactLateSingleton(*MI);
          }
        }
      }
      // Surviving cycle still occupies one product parcel — no BytesRemoved.
      I = MBB.end();
      continue;
    }

    // Bare branch (pre-finalize / pre-pack): one product parcel.
    if (BytesRemoved)
      *BytesRemoved += static_cast<int>(haydn::bundle::lateLayoutBytes(Top));
    I = MBB.erase(I);
    ++Count;
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

/// Resolve the DR64 pack slot to a stable (FrameReg, element-index) triple
/// when the offset fits scaled simm6; otherwise mark for the scavenged-base
/// fallback. The canonical reservation is in determineCalleeSaves (its scan
/// predicate is identical to the expansion branch, so every pack that reaches
/// here is already reserved in the real pipeline). The lazy CreateStackObject
/// below only fires for `-run-pass=postrapseudos` MIR tests that bypass PEI
/// (the fresh object has default offset 0 → a deterministic FrameReg+0 slot);
/// it is dead in the real pipeline. The slot is Align(8) → Off and Off+4 are
/// width-aligned. Never emits a dynamic SP adjust — the fallback materialises
/// a plain GPR base (FrameReg + Off) and addresses the slot at element 0/1/0,
/// which is always short-form-legal.
DR64PackSlotRef resolveDR64PackSlot(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    const HaydnFrameLowering &TFL) {
  MachineFunction &MF = *MBB.getParent();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  int FI = FuncInfo->getDR64PackFI();
  if (FI < 0) {
    FI = MF.getFrameInfo().CreateStackObject(/*Size=*/8, /*Alignment=*/Align(8),
                                             /*SpillSlot=*/true);
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
    // MIR tests bypassing determineCalleeSaves: lazily reserve. The fresh FI
    // has default offset 0 → deterministic FrameReg+0 slot. Dead in the real
    // pipeline (DR64PackBaseSpillFI is reserved alongside DR64PackFI).
    FI = MF.getFrameInfo().CreateStackObject(/*Size=*/4, /*Alignment=*/Align(4),
                                             /*SpillSlot=*/true);
    FuncInfo->setDR64PackBaseSpillFI(FI);
  }
  const HaydnFrameLowering *TFL = ST.getFrameLowering();
  Register SpillFrameReg;
  int64_t Off =
      TFL->getFrameIndexReferenceAt(MF, FI, SpillFrameReg, MBB, I).getFixed();
  emitFrameRelativeMemOp(MBB, I, DL, TII, Reg, SpillFrameReg, Off, IsStore,
                         /*StoreFlags=*/0);
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
      BuildMI(MBB, I, DL, TII.get(MatInst.Opc), Base)
          .addReg(Cur)
          .addImm(MatInst.Imm);
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
    MachineFunction &MF = *MBB.getParent();
    SmallVector<MachineOperand, 4> LiveOuts;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.isUse())
        continue;
      Register R = MO.getReg();
      if (!R || R == Haydn::R0 || R == Haydn::R15)
        continue;
      LiveOuts.push_back(MO);
    }
    while (MI.getNumOperands())
      MI.removeOperand(MI.getNumOperands() - 1);
    MI.setDesc(get(Haydn::JALR_W));
    MI.addOperand(MF, MachineOperand::CreateReg(Haydn::R0, /*isDef=*/true));
    MI.addOperand(MF, MachineOperand::CreateReg(Haydn::R15, /*isDef=*/false));
    MI.addOperand(MF, MachineOperand::CreateImm(0));
    for (const MachineOperand &MO : LiveOuts)
      MI.addOperand(MF, MachineOperand::CreateReg(
                            MO.getReg(), /*isDef=*/false, /*isImp=*/true,
                            /*isKill=*/MO.isKill()));
    return true;
  }

  case Haydn::B:
    // B must survive MachineBlockPlacement (ExpandPostRAPseudos runs
    // before addPreSched2 MBP). Printer expands B to BEQZ_W R0.
    return false;

  case Haydn::BR_JT:
    // JALR_W is isCall. Expanding a computed goto to a call before
    // MBP/pack would treat the jump as a call (same reason
    // PseudoCALLIndirect stays a printer expand). Printer emits
    // JALR_W r0, addr, 0. ExpandPseudos still re-zeros successors.
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
      default:
        // ADDI32 / LUI / ADDI32_W / ORI32_W: (rd, rs, imm)
        BuildMI(MBB, MBBI, DL, get(MatInst.Opc), DstReg)
            .addReg(CurrentReg)
            .addImm(MatInst.Imm);
        break;
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
    const bool HiIsSignExtOfLo = (Hi == -1 && Lo < 0);
    const HaydnSubtarget &ST =
        MBB.getParent()->getSubtarget<HaydnSubtarget>();

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
        BuildMI(MBB, MBBI, DL, get(Seq[I].Opc), Target)
            .addReg(Cur)
            .addImm(Seq[I].Imm);
        Cur = Target;
      }
    };

    if (HiIsZero || HiIsSignExtOfLo) {
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
    // path (subi32/st32/ld64 bloat, P7 / ISA-42 interim) is gone.
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
  // members resolve through the inverse; residual FieldSlots through peel.
  switch (haydnLogicalOpcode(Opc)) {
  case Haydn::SET_HWLOOP:
  case Haydn::SET_HWLOOP_REG:
  case Haydn::SET_HWLOOP_W:
  case Haydn::SET_HWLOOP_F2_W:
  case Haydn::SET_HWLOOP_REG_W:
  case Haydn::LoopStart:
    return true;
  default:
    break;
  }
  const std::string Log =
      haydn::format_e::peelLogicalOpcodeName(getName(Opc));
  return StringRef(Log).equals_insensitive("SET_HWLOOP") ||
         StringRef(Log).equals_insensitive("SET_HWLOOP_F2") ||
         StringRef(Log).equals_insensitive("SET_HWLOOP_REG");
}

bool HaydnInstrInfo::isHardwareLoopSetupInstr(const MachineInstr &MI) const {
  return isHardwareLoopSetupOpcode(MI.getOpcode());
}

bool HaydnInstrInfo::isHardwareLoopRegTripOpcode(unsigned Opc) const {
  switch (haydnLogicalOpcode(Opc)) {
  case Haydn::SET_HWLOOP_REG:
  case Haydn::SET_HWLOOP_F2_W:
  case Haydn::SET_HWLOOP_REG_W:
  case Haydn::LoopStart:
    return true;
  default:
    break;
  }
  const std::string Log =
      haydn::format_e::peelLogicalOpcodeName(getName(Opc));
  return StringRef(Log).equals_insensitive("SET_HWLOOP_REG") ||
         StringRef(Log).equals_insensitive("SET_HWLOOP_F2");
}

bool HaydnInstrInfo::isHardwareLoopImmTripOpcode(unsigned Opc) const {
  switch (haydnLogicalOpcode(Opc)) {
  case Haydn::SET_HWLOOP:
  case Haydn::SET_HWLOOP_W:
    return true;
  default:
    break;
  }
  return StringRef(haydn::format_e::peelLogicalOpcodeName(getName(Opc)))
      .equals_insensitive("SET_HWLOOP");
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
  // see cycleMembersHaveHwloopTripConflict).

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

bool HaydnInstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                            int64_t BrOffset) const {
  // BranchRelaxation may pass TargetOpcode::BUNDLE (header of a wrapped
  // branch). Cond/B field is GE96-03 signed 12-bit byte PC+imm — same
  // RelocFieldInfo as applyFixup. JAL long-reach is not modeled via BUNDLE
  // opc (insertBranch emits bare MI).
  if (BranchOpc == TargetOpcode::BUNDLE)
    BranchOpc = Haydn::B; // conservative short-range (cond/B)

  BranchOpc = haydnLogicalOpcode(BranchOpc);
  // JAL / JAL_W have a 20-bit signed target field (SImm20): ±512KB range.
  // JALR / JALR_W have no offset limitation (register-indirect).
  // Phase 1a: CodeGen selects the _W forms; legacy opcodes kept for
  // the asm parser / decoder.
  if (BranchOpc == Haydn::JAL || BranchOpc == Haydn::JAL_W ||
      BranchOpc == Haydn::JALR || BranchOpc == Haydn::JALR_W)
    return true;

  // Pseudo-call and jump-table pseudo reach ±512KB (JAL_W) / unlimited
  // (JALR_W via BR_JT) — always in range for any single fn.
  // NOTE : B is deliberately NOT here. B lowers to BEQZ_W R0, which
  // shares the conditional WIDE_BranchSImm12 byte-simm12 reach — it is
  // NOT a long-reach unconditional jump. Modeling B as always-in-range hid
  // out-of-range unconditional branches from BranchRelaxation: when
  // fixupConditionalBranch relaxes a far conditional into (inverted cond to
  // near + B to far), the B leg still overflows simm12. Letting B fall
  // through to the RelocFieldInfo check below makes BranchRelaxation detect
  // the far B leg and relax it via fixupUnconditionalBranch →
  // insertIndirectBranch (LOADI32 + JALR, unlimited reach) on the next
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

  // Logical BEQ_W..BLTU_W / BEQZ_W..BLTZ_W. One window with
  // computeRelocValue: WIDE_BranchSImm12 FieldSize=12, ValueShift=0
  // (GE96-03 byte PC+imm). isInt<13> was the leftover ÷2-era ±4 KiB
  // window and left a 2–4 KiB dead zone that integrated-as then rejected.
  //
  // Hexagon-style residual buffer (HexagonBranchRelaxation.cpp:164-166):
  // Distance = |offset| + BranchRelaxSafetyBuffer after getInstSizeInBytes
  // has charged named late-layout growth. Inflate BrOffset away from zero,
  // then apply the reloc row. Default is MaxSingleBranchGrowthBytes.
  const HaydnReloc::RelocFieldInfo &I = HaydnReloc::getRelocFieldInfo(
      HaydnReloc::RelocKind::WIDE_BranchSImm12);
  int64_t Inflated = BrOffset >= 0
                         ? BrOffset + (int64_t)BranchRelaxSafetyBuffer
                         : BrOffset - (int64_t)BranchRelaxSafetyBuffer;
  int64_t Shifted = Inflated >> I.ValueShift;
  return I.IsSigned ? isIntN(I.FieldSize, Shifted)
                    : isUIntN(I.FieldSize, static_cast<uint64_t>(Shifted));
}

MachineBasicBlock *
HaydnInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
 // : BranchRelaxation may pass a BUNDLE root (AnyInBundle isBranch).
  // Destination MBB is on the child branch (unwrapBundleControlFlow).
  const MachineInstr &Br = unwrapBundleControlFlow(MI);
  unsigned Opc = haydnLogicalOpcode(Br.getOpcode());

  // Two-register conditional branches. Operands: rs1, rs2, brtarget
  if (isHaydnCondBranch2Reg(Opc)) {
    return Br.getOperand(2).getMBB();
  }

  // Single-register conditional branches. Operands: rs, brtarget
  if (isHaydnCondBranch1Reg(Opc)) {
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
  if ((Opc == Haydn::JAL || Opc == Haydn::JAL_W) &&
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
 // Sequence ( exact-commit real MIs — no residual LOADI32 pseudo):
  //   LUI      scratch, <dest_mbb>   ; HI12 fixup on block symbol
  //   ADDI32_W scratch, scratch, <dest_mbb>  ; LO20 fixup
  //   JALR_W   scratch, scratch, 0  ; jump; link discarded into scratch
  //
  // BranchRelaxation runs after ExpandPostRA / ExpandPseudos, so a LOADI32
  // MBB pseudo inserted here would survive as a singleton BUNDLE child and
 // fail VerifyBundles / AsmPrinter residual bans (BAD_PC
  // cluster: far-branch jalr with stale scratch). Emit the real LUI+ADDI32_W
  // pair here; size model still counts 2 parcels (see getInstSizeInBytes).
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

  // Bare final real MIs (one product parcel each). Late Finalize wraps after
  // PreEmit multipass — do not callback-pack (insertBranch is also used
  // pre-postmisched; same bare contract for indirect materialization).
  // Returns the LUI MI (first of the pair) for scavenger walk-from.
  auto emitMBBAddr = [&](MachineBasicBlock &InsMBB,
                         MachineBasicBlock::iterator InsertPt, Register Dst,
                         MachineBasicBlock *JumpDest) -> MachineInstr * {
    MachineInstr *Lui =
        BuildMI(InsMBB, InsertPt, DL, get(Haydn::LUI), Dst)
            .addReg(Haydn::R0)
            .addMBB(JumpDest);
    BuildMI(InsMBB, InsertPt, DL, get(Haydn::ADDI32_W), Dst)
        .addReg(Dst)
        .addMBB(JumpDest);
    return Lui;
  };

  auto emitIndirectJump =
      [&](MachineBasicBlock &InsMBB, MachineBasicBlock::iterator InsertPt,
          Register Scratch, MachineBasicBlock *JumpDest) -> MachineInstr * {
    MachineInstr *First = emitMBBAddr(InsMBB, InsertPt, Scratch, JumpDest);
    BuildMI(InsMBB, InsertPt, DL, get(Haydn::JALR_W))
        .addReg(Scratch, RegState::Define)
        .addReg(Scratch)
        .addImm(0);
    return First;
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
    emitIndirectJump(MBB, InsertPt, ScratchPhys, JumpDest);

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
        emitIndirectJump(MBB, II, ScratchV, &NewDestBB);

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
    emitIndirectJump(MBB, II, ScratchPhys, &NewDestBB);
    return;
  }

  ScratchPhys = Haydn::R11;
  emitWithManualSpill(ScratchPhys, II, /*JumpToRestore=*/true);
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
    const MachineBasicBlock *MBB = MI.getParent();
    if (!MBB)
      return 0;
    SmallVector<unsigned, 3> Members;
    for (MachineBasicBlock::const_instr_iterator I =
             std::next(MI.getIterator());
         I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
      if (I->isMetaInstruction() || I->isDebugInstr() || I->isPosition())
        continue;
      Members.push_back(I->getOpcode());
      chargeOpcode(I->getOpcode());
    }
    if (Members.size() > 1 &&
        !haydn::bundle::opcodesHaveFormatEUnitCover(Members, TII))
      Extra += B * (Members.size() - 1);
    return Extra;
  }

  chargeOpcode(MI.getOpcode());
  return Extra;
}

unsigned HaydnInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  // AIE-shaped size authority (AIE1InstrInfo.cpp:646-651 getSize; AIE
  // AIEBaseInstrInfo.cpp:549-557 Format->getSize on the composite):
  //   * BUNDLE root → encodedBytesFor(committed Format E row) + named
  //     late-layout growth (Hexagon computeOffset peer)
  //   * bare real / multi-parcel pseudo → productParcelBytes() * N
  //   * child inside a BUNDLE → 0 (composite size is on the root)
  //   * pure meta / zero-size pseudos → 0
  //   * INLINEASM / INLINEASM_BR → conservative getInlineAsmLength
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

  // Opaque normal-LLVM exception: layout length only. Counts textual
  // instructions × MaxInstLength (HaydnMCAsmInfo = product Full parcel).
  // Empty side-effect barriers correctly charge 0. Must run before the
  // isPseudo early-out (INLINEASM is a StandardPseudoInstruction).
  if (MI.isInlineAsm()) {
    const MachineFunction *MF = MI.getMF();
    if (!MF || !MI.getNumOperands() || !MI.getOperand(0).isSymbol())
      return 0;
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              *MF->getTarget().getMCAsmInfo());
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

  if (MI.isPseudo() || MI.isMetaInstruction() || MI.isDebugInstr() ||
      MI.isImplicitDef() || MI.isKill())
    return 0;

  // Bare real opcode: one product Format E parcel plus named late growth
  // (SET_HWLOOP_W / BR_JT already charged above; other setup forms land here).
  return B + namedLateLayoutGrowthBytes(MI, *this);
}

ResourceCycle *HaydnInstrInfo::CreateTargetScheduleState(
    const TargetSubtargetInfo &STI) const {
  // Bundle-backed resource model for SWPS (alternative-aware slot
  // pressure). Default ON — see EnableHaydnHRResourceCycle. The DFA fallback is
  // choice-set-naive (reserves all alt bits in a stage), which inflates ResMII
  // on dual-load streaming loops. Pre-RA SMS uses this adapter for modulo
  // resource accounting only; it never materializes multi-member BUNDLE
  // roots (StageCount>1 is rejected; product multi-stage is post-RA).
  if (EnableHaydnHRResourceCycle)
    return new HaydnResourceCycle();
  const InstrItineraryData *II = STI.getInstrItineraryData();
  return static_cast<const HaydnSubtarget &>(STI).createDFAPacketizer(II);
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
  // explicit field (rd,rs,rs → 2R1W), matching descriptor-only estimates
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
  if (DAG && !IsPreRA)
    AltDescs = &DAG->MF.getInfo<HaydnMachineFunctionInfo>()->getAltDescs();
  return new HaydnHazardRecognizer(this, ItinData, IsPreRA, AltDescs);
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
  switch (SchedClass) {
  case Haydn::Sched::Slot0_LS:
  case Haydn::Sched::Slot1_LD:
  case Haydn::Sched::Slot01_LD:
  case Haydn::Sched::Slot2_LS:
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
                              MachineInstr *&CmpMI, MachineInstr *&InvertMI,
                              Register &TripCountReg) {
  MachineFunction *MF = LoopBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  EndLoop = nullptr;
  CmpMI = nullptr;
  InvertMI = nullptr;
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

/// Collect placeable body opcodes for SMS-RESMII (format packing multiset).
/// Skips PHI / meta / debug / terminators — same filter spirit as the HOOK scan.
static void haydnCollectSMSBodyOpcodes(const MachineBasicBlock &LoopBB,
                                       SmallVectorImpl<unsigned> &Out) {
  Out.clear();
  for (const MachineInstr &MI : LoopBB) {
    if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isImplicitDef() ||
        MI.isKill() || MI.isCFIInstruction() || MI.isPHI() || MI.isTerminator())
      continue;
    Out.push_back(MI.getOpcode());
  }
}

/// SMS-RESMII gate: compare left-to-right greedy (exactTryAddProduct — same
/// depth as ResourceCycle / calculateResMIIDFA packing) with the exhaustive ≤3
/// format set oracle. Logs MBB-order metrics for dual-run KPI; does not rewrite
/// shared MachinePipeliner or replace ResourceManager::calculateResMIIDFA.
///
/// Pure productResMIIFailsQualification remains the unit/pre-RA surface pin
/// for positive overestimate (order-trap multisets). analyzeLoop no longer
/// nulls the loop on that signal: residual Format E entry-capacity / E2-only
/// packs make ordinary IR body order overestimate while exhaustive still finds
/// a finite cover, and Option A product containment already rejects every
/// StageCount>1 at shouldUseSchedule. Inflated greedy ResMII is conservative
/// (safe II floor), not a miscompile. Preferred-collapse overestimate stays
/// unit-only. Exact-pack fail-close remains in haydnCheckSMSHandoffPackability.
///
/// Uses DEBUG_WITH_TYPE("pipeliner") so sms-* lits with -debug-only=pipeliner
/// pin the gate line (file DEBUG_TYPE is haydn-instr-info).
/// \returns true always (metrics-only; overestimate is not analyze reject).
static bool haydnCheckSMSResMIIOracle(const MachineBasicBlock &LoopBB) {
  SmallVector<unsigned, 16> Body;
  haydnCollectSMSBodyOpcodes(LoopBB, Body);
  if (Body.empty())
    return true;

  using namespace haydn::bundle;
  const unsigned Greedy = computeProductResMII(Body);
  const unsigned Exact = computeExhaustiveProductResMII(Body);
  const int Over = productResMIIOverestimate(Body);
  const bool OverEstimates = productResMIIFailsQualification(Body);

  DEBUG_WITH_TYPE("pipeliner", {
    dbgs() << "SMS-RESMII: body_ops=" << Body.size() << " greedy=" << Greedy
           << " exhaustive=" << Exact << " overestimate=" << Over;
    if (Body.size() > MaxExhaustiveProductResMIIOps)
      dbgs() << " (exhaustive fallback=greedy; N>"
             << MaxExhaustiveProductResMIIOps << ")";
    dbgs() << "\n";
    if (OverEstimates)
      dbgs() << "SMS-RESMII: reject — greedy overestimates exhaustive oracle by "
             << Over
             << " (metrics-only; StageCount>1 containment is the product gate; "
                "format-dependent multi-stage is post-RA only)\n";
  });
  // Always continue analyzeLoop: StageCount>1 / HOOK / exact-pack own reject.
  return true;
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

/// Format-acceptance differential metrics (plan §8.4 #7 SMS surface):
/// Log live ResourceCycle ≡ pure exactTryAddProduct polarity for the body
/// opcode multiset, plus the product pin. Metrics-only — never fail-closes
/// (format reject is already covered by RESMII / HANDOFF packability; this
/// line freezes RC↔HR peer agreement for qualification). Ports (MOVE32-class)
/// are orthogonal SMS-PORT metrics.
static void haydnLogSMSFormatAcceptanceDiff(const MachineBasicBlock &LoopBB) {
  SmallVector<unsigned, 16> Body;
  haydnCollectSMSBodyOpcodes(LoopBB, Body);

  const bool Match =
      HaydnResourceCycle::formatAcceptanceMatchesPureExact(Body);
  const bool Pins =
      HaydnResourceCycle::formatAcceptanceDifferentialPins();
  const unsigned LiveCycles =
      HaydnResourceCycle::formatSequentialCycleCount(Body);
  const bool PureSeq =
      HaydnResourceCycle::formatPureExactCanPackSequence(Body);
  const bool LiveSeq = HaydnResourceCycle::formatCanPackSequence(Body);

  DEBUG_WITH_TYPE("pipeliner", {
    dbgs() << "SMS-FORMAT: rc_hr_diff match=" << (Match ? 1 : 0)
           << " live_pack=" << (LiveSeq ? 1 : 0)
           << " pure_pack=" << (PureSeq ? 1 : 0)
           << " live_cycles=" << LiveCycles
           << " pins=" << (Pins ? 1 : 0)
           << " body_ops=" << Body.size()
           << " (ResourceCycle≡pure exact≡post-RA HR format; ports orthogonal)\n";
  });
}

/// Soft-exit QoR metrics (format-SMS corpus): log softExitIIFloor =
/// max(exhaustive format ResMII, MI port lower bound) plus exact-pack flag.
/// Metrics-only — never invents RecMII or durable BUNDLE groups. RecMII floors
/// remain DDG/itinerary (pipeliner "rec=" line / macc-acc-feedback).
/// VF3-G2 Generic-pass freeze (§8.4 #11): these lines are the SMS KPI surface
/// dual-run pinned under product vs matching-frontier=false vs finer-rp=false
/// in sms-format-generic-baseline.ll (unexplained ResMII/II delta blocks wave).
/// VF3-G3 ILP/critical residual attribution: same PROD/GEN/RP KPI parity is
/// frozen on dedicated ILP multi-load / dual-acc and critical-path chain
/// kernels in sms-format-ilp-crit-dual-run.ll (ranking residual must not invent
/// soft-exit / ResMII / II deltas; ResourceDemand pre-RA attribution is the
/// residual signal — multi-MI finalize parity holds on residual arms).
static void haydnLogSMSSoftExitQoR(const MachineBasicBlock &LoopBB) {
  SmallVector<unsigned, 16> Body;
  haydnCollectSMSBodyOpcodes(LoopBB, Body);

  HaydnCyclePortDemand Ports;
  for (const MachineInstr &MI : LoopBB) {
    if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isImplicitDef() ||
        MI.isKill() || MI.isCFIInstruction() || MI.isPHI() || MI.isTerminator())
      continue;
    Ports += countHaydnPortsFromMI(MI);
  }

  const unsigned FormatII =
      Body.empty() ? 0u
                   : haydn::bundle::computeExhaustiveProductResMII(Body);
  const unsigned PortII = HaydnResourceCycle::portLowerBoundResMII(
      Ports.GPRReads, Ports.GPRWrites, Ports.DRReads, Ports.DRWrites,
      Ports.ARReads, Ports.ARWrites);
  const unsigned Floor = HaydnResourceCycle::softExitIIFloor(
      Body, Ports.GPRReads, Ports.GPRWrites, Ports.DRReads, Ports.DRWrites,
      Ports.ARReads, Ports.ARWrites);
  const bool Exact = HaydnResourceCycle::qualKernelExactlyPackable(Body);

  DEBUG_WITH_TYPE("pipeliner", {
    dbgs() << "SMS-QOR: soft_exit_ii_floor=" << Floor
           << " format_resmii=" << FormatII << " port_resmii=" << PortII
           << " exact_packable=" << (Exact ? 1 : 0) << " body_ops=" << Body.size()
           << " (metrics-only; no HANDOFF invent; RecMII is DDG)\n";
  });
}

/// Qualification-kernel post-RA packability metrics (analyzeLoop). Proves
/// accepted bodies remain exact-packable under the shared product oracle.
/// No pre-RA clone-cycle BUNDLE materialize. Does **not** fail-close on
/// multi-cycle covers (those remain legal; only RESMII overestimate rejects).
/// Exact_packable=0 on an exact-bound body after RESMII pass would mean the
/// exhaustive cover is missing — treat as fail-close so qualification never
/// claims a kernel that post-RA cannot pack under the same product model.
/// Bodies with N > MaxExhaustiveProductResMIIOps use the greedy fallback
/// oracle (same as SMS-RESMII): a finite cover still counts as packable —
/// never false-reject large streaming kernels on the inexact bound.
/// \returns false when the loop must be rejected (not exactly packable).
static bool haydnCheckSMSHandoffPackability(const MachineBasicBlock &LoopBB) {
  SmallVector<unsigned, 16> Body;
  haydnCollectSMSBodyOpcodes(LoopBB, Body);

  // Metrics-only contract restated for lit pins (no pre-RA cycle groups;
  // no generic post-expand virtual). Keep the historical SMS-HANDOFF prefix.
  DEBUG_WITH_TYPE("pipeliner", {
    dbgs() << "SMS-HANDOFF: metrics-only freeze "
              "(scalar metrics on success remark; no pre-RA cycle groups)\n";
  });

  if (Body.empty()) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "SMS-HANDOFF: qual-kernel body_ops=0 coissue_packable=1 "
                "exact_packable=1 exhaustive=0\n";
    });
    return true;
  }

  const bool Coissue = HaydnResourceCycle::qualKernelCoissuePackable(Body);
  const bool Exact = HaydnResourceCycle::qualKernelExactlyPackable(Body);
  const unsigned Exhaustive =
      haydn::bundle::computeExhaustiveProductResMII(Body);

  DEBUG_WITH_TYPE("pipeliner", {
    dbgs() << "SMS-HANDOFF: qual-kernel body_ops=" << Body.size()
           << " coissue_packable=" << (Coissue ? 1 : 0)
           << " exact_packable=" << (Exact ? 1 : 0)
           << " exhaustive=" << Exhaustive << "\n";
    if (!Exact)
      dbgs() << "SMS-HANDOFF: reject — qualification kernel not exactly "
                "packable under product oracle (post-RA no-split contract)\n";
  });
  return Exact;
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

  // SMS-RESMII: greedy vs exhaustive ≤3 format oracle metrics. Positive
  // overestimate is logged only (Option A: StageCount>1 containment is the
  // product gate; inflated greedy II is conservative). Exact-pack reject stays
  // in haydnCheckSMSHandoffPackability. Does not replace calculateResMIIDFA.
  if (!haydnCheckSMSResMIIOracle(*LoopBB)) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "SMS: analyzeLoopForPipelining fail-closed "
                "(SMS-RESMII greedy overestimate)\n";
    });
    return nullptr;
  }

  // SMS-FORMAT: metrics-only ResourceCycle ≡ pure exact ≡ post-RA HR
  // format-acceptance differential (plan §8.4 #7). Does not fail-close —
  // polarity pin + body match log; ports are SMS-PORT orthogonal metrics.
  haydnLogSMSFormatAcceptanceDiff(*LoopBB);

  // SMS-PORT: metrics-only MOVE32-class MI-versus-descriptor differential.
  // Placement (MID) overcounts repeated sources; ResMII (MI) is exact. Does
  // not fail-close — intentional conservative placement.
  haydnLogSMSPortMiVsDescDifferential(*LoopBB);

  // Qualification-kernel post-RA packability metrics. Logs evidence under the
  // historical SMS-HANDOFF debug prefix; fail-closes only for in-bound bodies
  // that are not exactly packable (rare after RESMII). N>bound is not rejected
  // here. Pre-RA never freezes multi-member BUNDLE roots.
  if (!haydnCheckSMSHandoffPackability(*LoopBB)) {
    DEBUG_WITH_TYPE("pipeliner", {
      dbgs() << "SMS: analyzeLoopForPipelining fail-closed "
                "(SMS-HANDOFF qual-kernel not exactly packable)\n";
    });
    return nullptr;
  }

  // Soft-exit QoR: metrics-only II floor (max format ResMII, MI port floor)
  // + exact-pack restate. Never invents RecMII or BUNDLE groups.
  haydnLogSMSSoftExitQoR(*LoopBB);

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
/// mem-intrinsic sizes); they are not 64-bit. Member `_S*` opcodes are not
/// listed — use the logical root, or fail closed.
unsigned haydnMemAccessWidthBytes(unsigned Opc) {
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
    if (!MI.getOperand(BasePos).isReg())
      return false;
    // REG forms: offset is a register — still report positions; callers that
    // require Imm (getIncrementValue) check isImm themselves.
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
  unsigned Opc = MI.getOpcode();
  unsigned WidthBytes = haydnMemAccessWidthBytes(Opc);
  if (WidthBytes == 0)
    return false;
  Width = LocationSize::precise(WidthBytes);
  HaydnUpdateAM AM = classifyUpdateAM(*this, Opc);

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

  // Plain logical LS: (ins … base, imm). Base may be FI (byte extra) or a
  // register (element index, ISel and post-PEI).
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
