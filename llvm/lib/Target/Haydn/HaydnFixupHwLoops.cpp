//===-- HaydnFixupHwLoops.cpp - Post-layout HW loop validation ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Late pass (after BranchRelaxation): re-check SET_HWLOOP / LoopStart under
// the final size model, enforce Following >= InterveningCycles
// (SetupIssueDistance=3), body >= MinBodyBundles with strict END>BEGIN
// (END = last body cycle), and keep Off1/Off2 inside the encoder's
// uimm6/uimm12/4 fields.
//
// Numeric limits: HaydnHWLoopContracts.h (shared with formation).
//
// Opt before RA; post-RA Fixup is correctness only.
//
// Count-unit law (reg trip SET_HWLOOP_REG): late remat owns
//   Dest = Src + adj  →  SET …, Dest
// Fixup must NEVER splice between remat and SET (splits the def→use).
// Free lifts land *before the count-unit head* (remat if present).
// Never lift a dangerous MI (defs remat Src / uses remat Dest). Prefer
// demote / fatal over wrong trip at SET (CoreMark MEMORY_FAULT @ post-inc).
//
// Product narrative: demote-first, NOT erase-only.
// Generic HardwareLoops + Role A expand (HaydnHardwareLoops) already replaced
// the software back-edge with LoopStart/PseudoLoopEnd. Erasing SET alone on a
// *live* body yields a once-through fallthrough (wrong-code).
// Product recovery is final-real SUBI32+BNEZ_W when a free counter exists;
// live demote failure is fatal. demote OFF is debug-only and is also
// fatal on a live body (never erase-only once-through).
//
// Scheduling-unit / layout contracts:
// • Bundle-preserving: never unconditional SET unbundle; erase SET member only.
// • Final-real demotion (no residual LoopDec/LoopJNZ after late commit).
// • Residual generic SET_HWLOOP{,_REG} is fatal. ExpandPseudos owns the
//   rewrite to SET_HWLOOP_{W,F2_W}; Fixup is Hexagon-style range recheck /
//   pad / fatal only (HexagonFixupHwLoops.cpp:75-81, 136-148).
// • Sum still-relaxable branch growth in SET→BEGIN/SET→END vs Off margins.
// • Every Fixup-created real MI (deficit NOP pads, demote trip materialize,
//   stack-counter LD/ST glue, SUBI32+BNEZ_W soft edge, exit B) uses the shared
//   exact-commit surface (commitLateProductCycle → setDesc member →
//   finalizeBundle + FormatID) so the second BranchRelaxation charges
//   committed EncodedBytes, not bare MIs.
//
// Closed contracts:
//
// 1. Live MBB operands
// SET_HWLOOP{,_REG} carries Header/Latch as MBB operands. Later CFG
// edits can erase those blocks while leaving the SET behind
// (MIR shows `%bb.-1`). Touching a dead MBB is undefined. Rule: if
// Header or Latch is not a live member of this MachineFunction, erase
// the SET only (loop body is gone / peeled). Never demote soft-loop
// against a dead pointer.
//
// 2. Off1/Off2 range
// Off1 = uimm6×4 ≤ 252 B (safety margin → MaxOff1BytesSafe).
// Off2 = uimm12×4 ≤ 16380 B. Distance is measured forward in layout
// from the MI after the SET cycle. If Header is not after SET in layout,
// Off is unknown → treat as range-bad.
//
// 3. Recoverability ladder (correctness only)
// a. Pad setup gap only (deficit-only InterveningCycles NOPs after SET → BEGIN).
// b. tryShortenStartOffset — free-only lifts before count unit (not SET
//    alone); never across remat→SET; never dangerous peel.
// c. demote (llvm::demoteHardwareLoopToSoftware) when hard Off1/Off2 illegal.
// Soft-loop restore is *closed* (total, like AIE expand):
// • Collect loop blocks by CFG (reverse from Latch to Header)
// never by layout range — layout can put Latch before Header.
// • CountReg = trip Prefer if body does not non-countdown-def it;
// else scavenge a reg not mentioned in any loop block.
// • Materialize trip into CountReg at the SET site when needed.
// • Strip residual countdown of CountReg from the latch (generic
// HardwareLoops LoopDec / leftover Prefer+=-1 that Role A expand does
// not consume), then always install SUBI32+BNEZ_W.
// Never BNEZ-only, never double-dec.
// • SET_HWLOOP imm: materialize into a free reg via ADDI, then
// SUBI32+BNEZ_W.
// d. If demote cannot install a correct soft edge on a *live* body
// report_fatal_error — never erase-only once-through.
// Dead Header/Latch still allow erase-setup only (body gone).
// Debug only: -haydn-enable-hwloop-demote=false is still fatal on a
// live body (never erase-only once-through).
// 4. Pipeline
// addPreEmit: BranchRelaxation → FixupHwLoops → BranchRelaxation again
// so Fixup growth cannot leave branches past simm12.
//
// AsmPrinter is the emit-side twin: no START/END temp symbols without a
// real body instruction to flush them.
//
//===----------------------------------------------------------------------===//

#include "HaydnFixupHwLoops.h"
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnFrameLowering.h"
#include "HaydnHardwareLoops.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnHWLoopDemote.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPostRAScratch.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

// MI-level demote/erase helpers shared with formation live in
// HaydnHWLoopDemote.{h,cpp} (single owner; previously duplicated verbatim
// in this TU). The demote/erase entry points themselves are the exported
// llvm::demoteHardwareLoopToSoftware / llvm::eraseHardwareLoopSetup
// (HaydnHardwareLoops.cpp); fixupOne calls those directly.
using haydn::hwloop::buildExactLate;
using haydn::hwloop::finalizeExactLateSingleton;
using haydn::hwloop::isLiveMBB;
using haydn::hwloop::lateMemberOpcode;
using haydn::hwloop::topLevelForLayout;

#define DEBUG_TYPE "haydn-fixup-hwloops"

// Product demote policy lives in HaydnHWLoopDemote.cpp
// (isHwLoopDemoteEnabled). Default ON — demote-first, not erase-only.
// demoteHardwareLoopToSoftware refuses a live body when the flag is OFF
// so recoverRangeOrOrder fatals (never silent erase-only once-through).
namespace {

// Opcode lists live on TII (isHardwareLoopSetupOpcode / Reg/Imm).
// Thin locals keep call sites short; no second divergent table.

// Aliases from HaydnHWLoopContracts.h / BundlePlan EncodedBytes.
// Ceil byte→parcel and setup distances use productParcelBytes /
// ceilProductParcels — not a second hard-coded parcel size.
// Following floor is InterveningCycles (2), not SetupIssueDistance (3).
// MinSetupBundles remains the compatibility alias of InterveningCycles.
static constexpr unsigned InterveningCycles =
    haydn::hwloop::InterveningCycles;
static constexpr unsigned MinSetupBundles = haydn::hwloop::MinSetupBundles;
static constexpr int64_t MinSetupBytes = haydn::hwloop::MinSetupBytes;
static_assert(MinSetupBundles == InterveningCycles,
              "Fixup Following floor must be InterveningCycles");
static_assert(MinSetupBytes ==
                  haydn::bundle::productBundlesToBytes(InterveningCycles),
              "MinSetupBytes must be InterveningCycles × product parcel");
static constexpr int64_t MaxOff1Bytes = haydn::hwloop::MaxStartOffsetBytes;
static constexpr int64_t MaxOff2Bytes = haydn::hwloop::MaxEndOffsetBytes;
static constexpr int64_t MaxOff1BytesSafe =
    haydn::hwloop::MaxStartOffsetBytesSafe;

} // namespace

char HaydnFixupHwLoops::ID = 0;

INITIALIZE_PASS(HaydnFixupHwLoops, DEBUG_TYPE, "Haydn Hardware Loop Fixup",
                false, false)

HaydnFixupHwLoops::HaydnFixupHwLoops() : MachineFunctionPass(ID) {
  initializeHaydnFixupHwLoopsPass(*PassRegistry::getPassRegistry());
}

FunctionPass *llvm::createHaydnFixupHwLoopsPass() {
  return new HaydnFixupHwLoops();
}

//===----------------------------------------------------------------------===//
// shared exact-commit for late Fixup-created singletons
//===----------------------------------------------------------------------===//
//
// Declared late creators must call the shared exact no-split surface before
// the next layout consumer (second BranchRelaxation). Shape matches
// HaydnFinalizeBundle / PostRAScratch remat glue:
//   commitLateProductCycle(Logical) → member setDesc target
//   BuildMI(member)
//   finalizeBundle + stampBundleCommit (Format E row + completion)
// Idempotent with the late finalize firewall (already-bundled roots skipped).
//
// Formation pads (HaydnHardwareLoops, pre-pack) stay bare logical NOPs so the
// post-RA pack owns the first commit. Fixup is post-pack / PreEmit only and
// must not leave residual bare real MIs for the firewall to invent.

// Shared exact-late surface lives in HaydnBundleMaterialize.h
// (lateProductMemberOpcode / finalizeExactLateSingleton). Fixup pads and
// demotion use it so the second BR charges committed EncodedBytes for every
// late product cycle. insertBranch stays bare (one product parcel).


// Next *bundle-boundary* iterator after \p MI. `std::next(MI.getIterator)`
// advances one instruction and can land on a BUNDLE interior
// (`isBundledWithPred`); constructing `MachineBasicBlock::iterator` from
// that asserts (— CoreMark/moddi3 after PostRA co-issue).
// When SET is mid-bundle, skip to after the whole coissued cycle
// so Following counts subsequent issue cycles, not co-members.
static MachineBasicBlock::iterator
nextBundleBoundary(MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  assert(MBB && "MI must be inserted");
  MachineBasicBlock::instr_iterator II = std::next(MI.getIterator());
  while (II != MBB->instr_end() && II->isBundledWithPred())
    ++II;
  if (II == MBB->instr_end())
    return MBB->end();
  return MachineBasicBlock::iterator(II);
}

// Top-level MI for layout edits when \p MI may be a BUNDLE interior.
// Bundle-preserving Fixup splices/pads relative to this root, never unbundles
// a legal coissued SET cycle just to walk iterators.

unsigned HaydnFixupHwLoops::countFollowingBundles(
    MachineInstr &SetMI, const HaydnInstrInfo &TII) const {
  unsigned Bundles = 0;
  MachineBasicBlock *MBB = SetMI.getParent();
  for (MachineBasicBlock::iterator I = nextBundleBoundary(SetMI),
                                   E = MBB->end();
       I != E; ++I) {
    if (I->isMetaInstruction() || I->isDebugInstr() || I->isImplicitDef())
      continue;
    if (I->isKill())
      continue;
    // Terminator ends the preheader fallthrough region for counting.
    if (I->isTerminator() && !I->isCall())
      break;
    unsigned Bytes = TII.getInstSizeInBytes(*I);
    if (Bytes == 0)
      continue;
  // Parcel count via product EncodedBytes (generated Full Size).
    Bundles += haydn::bundle::ceilProductParcels(Bytes);
  }
  return Bundles;
}

// Conservative layout pad when entering a later MBB. Hexagon aligns the
// running offset to MBB.getAlignment(); Haydn can emit only whole product
// EncodedBytes parcels, so the gap is charged as ceilProductParcels ×
// generated EncodedBytes. No magic 12/16 quantum.
static int64_t padLayoutBytesForMBBAlign(int64_t Bytes,
                                         const MachineBasicBlock &MBB) {
  const Align A = MBB.getAlignment();
  if (A == Align(1) || Bytes < 0)
    return Bytes;
  const uint64_t Need = alignTo(static_cast<uint64_t>(Bytes), A);
  if (Need <= static_cast<uint64_t>(Bytes))
    return Bytes;
  const unsigned Gap =
      static_cast<unsigned>(Need - static_cast<uint64_t>(Bytes));
  return Bytes + haydn::bundle::productBundlesToBytes(
                     haydn::bundle::ceilProductParcels(Gap));
}

// Conservative layout distance From→To in layout order (only forward).
int64_t HaydnFixupHwLoops::estimateMBBDistance(
    const MachineFunction &MF, const MachineBasicBlock *FromMBB,
    MachineBasicBlock::const_iterator FromIt, const MachineBasicBlock *ToMBB,
    const HaydnInstrInfo &TII) const {
  if (!isLiveMBB(MF, FromMBB) || !isLiveMBB(MF, ToMBB))
    return -1;
  int64_t Bytes = 0;
  bool Started = false;
  for (const MachineBasicBlock &MBB : MF) {
    if (&MBB == FromMBB)
      Started = true;
    if (!Started)
      continue;
    // Already inside FromMBB (AfterSet). Charge alignment only when
    // entering a subsequent MBB in layout order.
    if (&MBB != FromMBB)
      Bytes = padLayoutBytesForMBBAlign(Bytes, MBB);
    auto Begin = (&MBB == FromMBB) ? FromIt : MBB.begin();
    for (auto I = Begin, E = MBB.end(); I != E; ++I) {
      if (&MBB == ToMBB && I == ToMBB->begin())
        return Bytes;
      Bytes += TII.getInstSizeInBytes(*I);
    }
    if (&MBB == ToMBB)
      return Bytes;
  }
  return -1; // To not after From in layout.
}

bool HaydnFixupHwLoops::computeOffsets(MachineInstr &SetMI,
                                       const HaydnInstrInfo &TII,
                                       int64_t &StartOff, int64_t &EndOff,
                                       MachineBasicBlock *&StartMBB,
                                       MachineBasicBlock *&EndMBB) const {
  StartOff = EndOff = -1;
  StartMBB = EndMBB = nullptr;

  const MachineFunction *MF = SetMI.getParent() ? SetMI.getParent()->getParent()
                                                : nullptr;
  if (!MF)
    return false;

  // LoopStart (IR ZOL): Header from CFG-or-layout-tail; Latch is END
  // (single-BB Header==Latch). Multi-BB residual LoopStart must not
  // treat Header as END — that under-counts Off2. Expand normally
  // rewrites to SET_HWLOOP_F2_W with Header/Latch operands first.
  if (SetMI.getOpcode() == Haydn::LoopStart) {
    StartMBB = haydn::hwloop::resolveBodyMBBFixup(SetMI);
    if (!StartMBB)
      return false;
    EndMBB = haydn::hwloop::resolveLoopStartLatch(StartMBB, SetMI.getParent());
    if (!EndMBB)
      EndMBB = StartMBB;
  } else if (SetMI.getNumOperands() >= 3 && SetMI.getOperand(1).isMBB() &&
             SetMI.getOperand(2).isMBB()) {
    StartMBB = SetMI.getOperand(1).getMBB();
    EndMBB = SetMI.getOperand(2).getMBB();
    if (!isLiveMBB(*MF, StartMBB) || !isLiveMBB(*MF, EndMBB)) {
      StartMBB = EndMBB = nullptr;
      return false;
    }
  } else {
    return false;
  }

  // After the SET *cycle*: next top-level MI past the whole coissued BUNDLE
  // Bundle-preserving: never hand a mid-bundle iterator to
  // MachineInstrBundleIterator (asserts isBundledWithPred).
  MachineBasicBlock *Pre = SetMI.getParent();
  MachineBasicBlock::iterator AfterSet = nextBundleBoundary(SetMI);
  StartOff = estimateMBBDistance(*MF, Pre, AfterSet, StartMBB, TII);
  EndOff = estimateMBBDistance(*MF, Pre, AfterSet, EndMBB, TII);
  // HWLR_END is the start of the last size-bearing non-terminator cycle
  // in EndMBB (golden: last body bundle). One closed walk: skip
  // terminators — PseudoLoopEnd and post-PLE soft-exit B/cond sit past
  // END (AsmPrinter getLastRealInstr). No second fallback path.
  if (EndMBB && EndOff >= 0) {
    int64_t Cursor = EndOff;
    int64_t LastCycleStart = -1;
    for (const MachineInstr &MI : *EndMBB) {
      if (MI.isTerminator())
        continue;
      unsigned Bytes = TII.getInstSizeInBytes(MI);
      if (Bytes == 0)
        continue;
      LastCycleStart = Cursor;
      Cursor += static_cast<int64_t>(Bytes);
    }
    if (LastCycleStart < 0)
      return false; // empty body — no END cycle
    EndOff = LastCycleStart;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// Count unit + free-only Off1 shorten (correctness)
//
// Reg trip: Dest = Src+adj → SET …, Dest. Never insert between remat and SET.
// HEAD bug: splice before SET after unbundle left peel between remat and SET
// so SET latched data in Dest (MEMORY_FAULT). Splice before count-unit head;
// refuse dangerous peel.
//===----------------------------------------------------------------------===//

static Register getHwloopCountReg(const MachineInstr &SetMI,
                                  const HaydnInstrInfo &TII) {
  unsigned Opc = SetMI.getOpcode();
  if (TII.isHardwareLoopRegTripOpcode(Opc) && SetMI.getNumOperands() >= 4 &&
      SetMI.getOperand(3).isReg())
    return SetMI.getOperand(3).getReg();
  // LoopStart residual: count in op0 (reg-trip shape).
  if (Opc == Haydn::LoopStart && SetMI.getNumOperands() >= 1 &&
      SetMI.getOperand(0).isReg())
    return SetMI.getOperand(0).getReg();
  return Register();
}

static bool miOrBundleDefines(const MachineInstr &MI, Register Reg) {
  if (!Reg)
    return false;
  auto defs = [&](const MachineInstr &I) {
    for (const MachineOperand &MO : I.operands())
      if (MO.isReg() && MO.isDef() && MO.getReg() == Reg)
        return true;
    return false;
  };
  if (MI.isBundle()) {
    for (const MachineInstr *I = MI.getNextNode();
         I && I->isBundledWithPred(); I = I->getNextNode())
      if (defs(*I))
        return true;
    return false;
  }
  return defs(MI);
}

// Remat ADDI that defs Count immediately before SET, else SET itself.
// Elevate mid-bundle heads to the BUNDLE root so free lifts and
// pads stay top-level (never unbundle a legal coissued SET cycle).
static MachineInstr &getCountUnitHead(MachineInstr &SetMI,
                                      const HaydnInstrInfo &TII) {
  Register Count = getHwloopCountReg(SetMI, TII);
  MachineInstr *Head = &SetMI;
  if (Count) {
    MachineBasicBlock *MBB = SetMI.getParent();
  // SET may be mid-bundle after PostRA co-issue.
    // Never construct MachineBasicBlock::iterator from a BUNDLE child
    // (asserts isBundledWithPred). Elevate to the cycle root first.
    MachineInstr &Top = topLevelForLayout(SetMI);
    MachineBasicBlock::iterator It = Top.getIterator();
    if (It != MBB->begin()) {
      MachineBasicBlock::iterator PrevIt = std::prev(It);
      while (PrevIt != MBB->begin() &&
             (PrevIt->isMetaInstruction() || PrevIt->isDebugInstr() ||
              PrevIt->isKill() || PrevIt->isImplicitDef()))
        --PrevIt;
      // Remat may be a BUNDLE containing only ADDI, bare ADDI, or coissued
      // with SET inside the same BUNDLE (then Head stays the BUNDLE root).
      if (miOrBundleDefines(*PrevIt, Count))
        Head = &*PrevIt;
      else if (miOrBundleDefines(Top, Count) && &Top != &SetMI)
        Head = &Top; // remat coissued with SET in same cycle
    }
  }
  return topLevelForLayout(*Head);
}

static void collectCountUnitExtLiveIns(const MachineInstr &Head,
                                       const MachineInstr &SetMI,
                                       SmallSet<Register, 8> &LiveIns) {
  SmallSet<Register, 8> HeadDefs;
  auto oneDef = [&](const MachineInstr &MI) {
    for (const MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isDef() && MO.getReg().isPhysical())
        HeadDefs.insert(MO.getReg());
  };
  auto oneUse = [&](const MachineInstr &MI, SmallSet<Register, 8> &Uses) {
    for (const MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isUse() && !MO.isImplicit() &&
          MO.getReg().isPhysical())
        Uses.insert(MO.getReg());
  };
  auto walk = [&](const MachineInstr &I, bool CollectDefs,
                  SmallSet<Register, 8> *Uses) {
    if (I.isBundle()) {
      for (const MachineInstr *J = I.getNextNode();
           J && J->isBundledWithPred(); J = J->getNextNode()) {
        if (CollectDefs)
          oneDef(*J);
        else if (Uses)
          oneUse(*J, *Uses);
      }
    } else {
      if (CollectDefs)
        oneDef(I);
      else if (Uses)
        oneUse(I, *Uses);
    }
  };
  walk(Head, /*CollectDefs=*/true, nullptr);
  SmallSet<Register, 8> Uses;
  walk(Head, /*CollectDefs=*/false, &Uses);
  if (&Head != &SetMI)
    walk(SetMI, /*CollectDefs=*/false, &Uses);
  LiveIns.clear();
  for (Register U : Uses)
    if (!HeadDefs.contains(U))
      LiveIns.insert(U);
}

static void collectCountUnitHeadDefs(const MachineInstr &Head,
                                     SmallSet<Register, 8> &Defs) {
  Defs.clear();
  auto one = [&](const MachineInstr &MI) {
    for (const MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isDef() && MO.getReg().isPhysical())
        Defs.insert(MO.getReg());
  };
  if (Head.isBundle()) {
    for (const MachineInstr *J = Head.getNextNode();
         J && J->isBundledWithPred(); J = J->getNextNode())
      one(*J);
  } else {
    one(Head);
  }
}

template <unsigned N>
static bool miOrBundleDefsAny(const MachineInstr &MI,
                              const SmallSet<Register, N> &Regs) {
  if (Regs.empty())
    return false;
  auto defs = [&](const MachineInstr &I) {
    for (const MachineOperand &MO : I.operands())
      if (MO.isReg() && MO.isDef() && Regs.contains(MO.getReg()))
        return true;
    return false;
  };
  if (MI.isBundle()) {
    for (const MachineInstr *I = MI.getNextNode();
         I && I->isBundledWithPred(); I = I->getNextNode())
      if (defs(*I))
        return true;
    return false;
  }
  return defs(MI);
}

template <unsigned N>
static bool miOrBundleUsesAny(const MachineInstr &MI,
                              const SmallSet<Register, N> &Regs) {
  if (Regs.empty())
    return false;
  auto uses = [&](const MachineInstr &I) {
    for (const MachineOperand &MO : I.operands())
      if (MO.isReg() && MO.isUse() && !MO.isImplicit() &&
          Regs.contains(MO.getReg()))
        return true;
    return false;
  };
  if (MI.isBundle()) {
    for (const MachineInstr *I = MI.getNextNode();
         I && I->isBundledWithPred(); I = I->getNextNode())
      if (uses(*I))
        return true;
    return false;
  }
  return uses(MI);
}

template <unsigned N, unsigned M>
static bool isDangerousToLiftBeforeCountUnit(
    const MachineInstr &MI, const SmallSet<Register, N> &ExtLiveIns,
    const SmallSet<Register, M> &HeadDefs) {
  // Def Src before remat → wrong trip. Use Dest before remat → use-before-def.
  if (miOrBundleDefsAny(MI, ExtLiveIns))
    return true;
  if (miOrBundleUsesAny(MI, HeadDefs))
    return true;
  return false;
}

static void collectMiPhysDefUse(const MachineInstr &MI,
                                SmallSet<Register, 16> &Defs,
                                SmallSet<Register, 16> &Uses) {
  auto one = [&](const MachineInstr &I) {
    for (const MachineOperand &MO : I.operands()) {
      if (!MO.isReg() || !MO.getReg().isPhysical())
        continue;
      if (MO.isDef())
        Defs.insert(MO.getReg());
      else if (MO.isUse() && !MO.isImplicit())
        Uses.insert(MO.getReg());
    }
  };
  if (MI.isBundle()) {
    for (const MachineInstr *J = MI.getNextNode();
         J && J->isBundledWithPred(); J = J->getNextNode())
      one(*J);
  } else {
    one(MI);
  }
}

// Free-only Off1 legality. Never split remat→SET. Never lift dangerous peel.
// Order-preserving among free candidates: always take first free post-SET MI
// (not last — reversing free MIs still breaks chains).
bool HaydnFixupHwLoops::tryShortenStartOffset(MachineInstr &SetMI,
                                              const HaydnInstrInfo &TII,
                                              int64_t &StartOff,
                                              int64_t &EndOff) {
  if (StartOff < 0 || StartOff <= MaxOff1BytesSafe)
    return false;

  bool Changed = false;
  MachineBasicBlock *MBB = SetMI.getParent();
  MachineBasicBlock *StartMBB = nullptr;
  MachineBasicBlock *EndMBB = nullptr;

  MachineInstr &Head = getCountUnitHead(SetMI, TII);
  SmallSet<Register, 8> ExtLiveIns;
  SmallSet<Register, 8> HeadDefs;
  collectCountUnitExtLiveIns(Head, SetMI, ExtLiveIns);
  collectCountUnitHeadDefs(Head, HeadDefs);

  while (StartOff > MaxOff1BytesSafe) {
    unsigned Following = countFollowingBundles(SetMI, TII);
    // Keep at least InterveningCycles after SET (Following > floor to lift one).
    if (Following <= InterveningCycles)
      break;

    SmallSet<Register, 16> BarrierDefs;
    SmallSet<Register, 16> BarrierUses;

    MachineInstr *Cand = nullptr;
    unsigned CandBundles = 0;
    for (MachineBasicBlock::iterator I = nextBundleBoundary(SetMI),
                                     E = MBB->end();
         I != E; ++I) {
      if (I->isMetaInstruction() || I->isDebugInstr() || I->isImplicitDef() ||
          I->isKill())
        continue;
      if (I->isTerminator() && !I->isCall())
        break;
      unsigned Bytes = TII.getInstSizeInBytes(*I);
      if (Bytes == 0)
        continue;

      const bool IsNop = I->getOpcode() == Haydn::NOP;
      const bool UnitDanger =
          !IsNop && isDangerousToLiftBeforeCountUnit(*I, ExtLiveIns, HeadDefs);
      const bool CrossBarrier =
          !IsNop && (miOrBundleUsesAny(*I, BarrierDefs) ||
                     miOrBundleDefsAny(*I, BarrierUses));

      if (UnitDanger || CrossBarrier) {
        LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: skip dangerous/dependent "
                             "(leave remat→SET intact): "
                          << *I);
        collectMiPhysDefUse(*I, BarrierDefs, BarrierUses);
        continue;
      }

      Cand = &*I;
  // Ceil by committed product EncodedBytes, not a dual magic 16.
      CandBundles = haydn::bundle::ceilProductParcels(Bytes);
      break;
    }
    if (!Cand || CandBundles == 0)
      break;
    if (Following - CandBundles < InterveningCycles)
      break;

    // Splice before count-unit head (remat), NEVER before SET alone.
    MachineInstr &CurHead = getCountUnitHead(SetMI, TII);
    MBB->splice(CurHead.getIterator(), MBB, Cand->getIterator());
    Changed = true;
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: free lift before count unit: "
                      << *Cand);

    if (!computeOffsets(SetMI, TII, StartOff, EndOff, StartMBB, EndMBB))
      break;
  }

  // Do NOT sink trip-load+remat+SET past peel: peel leaves body live-ins in
  // those physregs (e.g. r1 pointer). Moving LD32 trip after peel clobbers
  // them → MEMORY_FAULT. If free lifts cannot fix Off1, demote (possibly
  // stack-counter) instead.

  return Changed;
}

bool HaydnFixupHwLoops::fixupOne(MachineInstr &SetMI,
                                   const HaydnInstrInfo &TII) {
  bool Changed = false;
  DebugLoc DL = SetMI.getDebugLoc();
  MachineBasicBlock *Pre = SetMI.getParent();
  if (!Pre)
    return false;
  const MachineFunction &MF = *Pre->getParent();

  // Bundle-preserving Fixup. Never unconditional unbundle of a legal
  // coissued SET cycle. Layout helpers (nextBundleBoundary, getCountUnitHead,
  // topLevelForLayout) walk around BUNDLE interiors; eraseHardwareLoopSetup
  // removes only the SET member via the shared recommit path.

  // Dead body / stale MBB operands (contract §1)
  // SET_HWLOOP with %bb.-1: body was erased after convert. Erase setup only.
  // Product selector domain is {0,1}: out-of-domain sel stays unavailable and
  // demotes/erases fail-closed (never invent extra CSR/selector identities).
  {
    unsigned Opc = SetMI.getOpcode();
    if (TII.isHardwareLoopSetupOpcode(Opc) && Opc != Haydn::LoopStart) {
      if (SetMI.getNumOperands() < 4 || !SetMI.getOperand(0).isImm() ||
          !SetMI.getOperand(1).isMBB() || !SetMI.getOperand(2).isMBB()) {
        report_fatal_error(
            "HaydnFixupHwLoops: malformed SET_HWLOOP; refusing erase-only "
            "once-through",
            /*gen_crash_diag=*/false);
      }
      const int64_t Sel = SetMI.getOperand(0).getImm();
      if (!haydn::hwloop::isProductSelector(Sel)) {
        LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: out-of-domain selector "
                          << Sel << " — demote-first / fatal fail-closed\n");
        if (demoteHardwareLoopToSoftware(SetMI, TII, "HaydnFixupHwLoops",
                                         haydn::hwloop::resolveBodyMBBFixup))
          return true;
        report_fatal_error(
            "HaydnFixupHwLoops: unsupported SET_HWLOOP selector cannot "
            "demote to software loop; refusing erase-only once-through",
            /*gen_crash_diag=*/false);
      }
      MachineBasicBlock *H = SetMI.getOperand(1).getMBB();
      MachineBasicBlock *L = SetMI.getOperand(2).getMBB();
      if (!isLiveMBB(MF, H) || !isLiveMBB(MF, L)) {
        LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: stale Header/Latch "
                             "(%bb.-1 or foreign) — erase SET only\n");
        return eraseHardwareLoopSetup(SetMI, "HaydnFixupHwLoops",
                                  haydn::hwloop::resolveBodyMBBFixup);
      }
      // Product programs HWLR only through SET. A body CSRW to the
      // unpublished 0x20-0x25 window is fail-closed: demote would leave
      // the write, so fatal (same law as formation scanRoleABlock).
      // Walk BUNDLE interiors — post-RA pack can hide CSRW as a child.
      haydn::hwloop::LoopBlockSet Blocks;
      haydn::hwloop::collectLoopBlocks(H, L, Pre, Blocks);
      if (haydn::hwloop::loopBlocksContainUnpublishedHwlrCsr(Blocks))
        report_fatal_error(
            "HaydnFixupHwLoops: unpublished HWLR CSR write in "
            "hardware-loop body; product programs HWLR only through "
            "SET_HWLOOP",
            /*gen_crash_diag=*/false);
    } else if (Opc == Haydn::LoopStart) {
      MachineBasicBlock *Body = haydn::hwloop::resolveBodyMBBFixup(SetMI);
      if (!Body) {
        // Unresolved LoopStart: cannot prove the body is dead. Hexagon
        // FixupHwLoops.cpp:137-148 converts or leaves LOOP; it never
        // erases to a once-through fallthrough. Overlay: debug demote
        // OFF fatals; product demote ON may L1-erase only when no latch
        // was found (no back-edge remains).
        if (!haydn::hwloop::isHwLoopDemoteEnabled())
          report_fatal_error(
              "HaydnFixupHwLoops: LoopStart with unresolved body; "
              "demote disabled; refusing erase-only once-through",
              /*gen_crash_diag=*/false);
        LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: LoopStart with no live body "
                             "— erase setup\n");
        return eraseHardwareLoopSetup(SetMI, "HaydnFixupHwLoops",
                                  haydn::hwloop::resolveBodyMBBFixup);
      }
    }
  }

  // Setup gap: Following >= InterveningCycles (SetupIssueDistance=3).
  // Deficit-only NOPs after the SET cycle (bundle root if coissued).
  // leaveRegion handleRegionConflicts owns ExitReady + inter-zone trailing
  // pads for multi-MI scheduled regions; this residual path covers single-MI
  // skip regions and short useful-window fill. Pad-drop of formation sprays
  // stays deferred until both owners prove redundant together.
  // Each pad is exact-committed (shared commitLateProductCycle surface) so the
  // second BranchRelaxation charges committed EncodedBytes.
  unsigned Following = countFollowingBundles(SetMI, TII);
  if (Following < InterveningCycles) {
    unsigned Deficit = InterveningCycles - Following;
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: setup gap Following=" << Following
                      << " < InterveningCycles=" << InterveningCycles
                      << " (SetupIssueDistance="
                      << haydn::hwloop::SetupIssueDistance
                      << ") — insert deficit " << Deficit
                      << " exact-commit NOP bundle(s) after " << SetMI);
    MachineBasicBlock *MBB = SetMI.getParent();
    MachineBasicBlock::iterator InsertPt = nextBundleBoundary(SetMI);
    for (unsigned I = 0; I < Deficit; ++I) {
      MachineInstr *Pad =
          buildExactLate(*MBB, InsertPt, DL, TII, Haydn::NOP);
      finalizeExactLateSingleton(*Pad);
    }
    Changed = true;
  }
#ifndef NDEBUG
  assert(countFollowingBundles(SetMI, TII) >= InterveningCycles &&
         "Following >= InterveningCycles after deficit-only pad");
#endif

  // Range re-check (begin + end) — one path for SET_* and LoopStart.
  // Product (default demote ON): demote-first on unencodable/range-bad SET.
  // Debug only (demote OFF): still fatal on a live body (never erase-only).
  auto recoverRangeOrOrder = [&](const char *Why) -> bool {
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: " << Why
                      << (haydn::hwloop::isHwLoopDemoteEnabled()
                              ? " — demote-first (product)\n"
                              : " — demote disabled; refuse erase-only\n"));
    if (demoteHardwareLoopToSoftware(SetMI, TII, "HaydnFixupHwLoops",
                                     haydn::hwloop::resolveBodyMBBFixup))
      return true;
    // Live body, soft edge not installable (or demote off). Never
    // silent single-pass body.
    report_fatal_error(
        "HaydnFixupHwLoops: out-of-range/invalid SET_HWLOOP cannot demote to "
        "software loop (no free counter GPR or usable exit); refusing "
        "erase-only once-through",
        /*gen_crash_diag=*/false);
  };

  int64_t StartOff = -1, EndOff = -1;
  MachineBasicBlock *StartMBB = nullptr, *EndMBB = nullptr;
  if (!computeOffsets(SetMI, TII, StartOff, EndOff, StartMBB, EndMBB)) {
    return recoverRangeOrOrder("computeOffsets failed");
  }

  // Body floor residual: size-bearing parcels BEGIN..END inclusive must meet
  // MinBodyBundles. Formation may leave short bodies after post-RA pack;
  // pad with exact-commit NOP cycles before demoting (product keep path).
  auto padBodyToMinLaw = [&]() -> bool {
    if (!EndMBB || StartOff < 0 || EndOff < 0)
      return false;
    unsigned Parcels = 0;
    if (EndOff > StartOff)
      Parcels = haydn::hwloop::bodyParcelsFromOffsets(StartOff, EndOff);
    else if (EndOff == StartOff)
      Parcels = 1; // single size-bearing body cycle (END == BEGIN start)
    else
      return false;
    if (Parcels >= haydn::hwloop::MinBodyBundles)
      return false;
    unsigned Deficit = haydn::hwloop::MinBodyBundles - Parcels;
    // Insert before the first terminator (PseudoLoopEnd / RET / soft edge).
    // Never append after a terminator — that fails the machine verifier.
    MachineBasicBlock::iterator InsertPt = EndMBB->getFirstTerminator();
    if (InsertPt == EndMBB->end()) {
      // No terminator yet: still prefer ZOL latch metas if present bare.
      for (MachineInstr &MI : *EndMBB) {
        if (MI.isBundledWithPred())
          continue;
        unsigned Opc = MI.getOpcode();
        if (Opc == Haydn::PseudoLoopEnd || Opc == Haydn::LoopJNZ ||
            Opc == Haydn::LoopDec) {
          InsertPt = MI.getIterator();
          break;
        }
      }
    }
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: body parcels=" << Parcels
                      << " < MinBodyBundles=" << haydn::hwloop::MinBodyBundles
                      << " — insert deficit " << Deficit
                      << " exact-commit NOP bundle(s) in body\n");
    for (unsigned I = 0; I < Deficit; ++I) {
      MachineInstr *Pad =
          buildExactLate(*EndMBB, InsertPt, DL, TII, Haydn::NOP);
      finalizeExactLateSingleton(*Pad);
    }
    return true;
  };

  if (padBodyToMinLaw()) {
    Changed = true;
    if (!computeOffsets(SetMI, TII, StartOff, EndOff, StartMBB, EndMBB))
      return recoverRangeOrOrder("computeOffsets failed after body pad");
  }

  auto rangeBad = [&](bool HardOff1Only) {
    if (StartOff < 0 || EndOff < 0)
      return true;
    // Soft: try free lifts under safety margin. Hard: demote only past uimm6.
    int64_t Off1Lim = HardOff1Only ? MaxOff1Bytes : MaxOff1BytesSafe;
    if (StartOff > Off1Lim)
      return true;
    if (EndOff > MaxOff2Bytes)
      return true;
    // Reloc law: Off1/Off2 encode byte_delta >> 2. Non-scale or over-field
    // distances stay unavailable and demote fail-closed (no invent).
    if (!haydn::hwloop::offsetsMeetImmRelocLaw(StartOff, EndOff))
      return true;
    // Strict END > BEGIN; body parcels BEGIN..END inclusive >= MinBodyBundles.
    if (!haydn::hwloop::bodyMeetsMinLaw(StartOff, EndOff))
      return true;
    // MinSetupBytes = InterveningCycles × productParcelBytes.
    // Timing law is the cycle pair, not this product under mixed widths.
    if (StartOff < MinSetupBytes)
      return true;
    // Imm trip COUNT must fit the uimm16 field and meet MinCount.
    // Over-field values demote (or fatal when demote is off) rather
    // than reaching MC as an unencodable immediate.
    if (TII.isHardwareLoopImmTripOpcode(SetMI.getOpcode()) &&
        SetMI.getNumOperands() >= 4 && SetMI.getOperand(3).isImm() &&
        !haydn::hwloop::countMeetsFieldLaw(SetMI.getOperand(3).getImm()))
      return true;
    return false;
  };

  if (rangeBad(/*HardOff1Only=*/false)) {
    // Free-only lifts; remat→SET stays glued. No peel across count unit.
    if (StartOff > MaxOff1BytesSafe)
      Changed |= tryShortenStartOffset(SetMI, TII, StartOff, EndOff);

    // Body may still be short after free lifts (or pad was blocked).
    if (padBodyToMinLaw())
      Changed = true;

    if (!computeOffsets(SetMI, TII, StartOff, EndOff, StartMBB, EndMBB))
      return recoverRangeOrOrder("computeOffsets failed after free lifts");

    // Soft miss OK if hard uimm6 holds. Else demote (never break remat).
    if (rangeBad(/*HardOff1Only=*/true)) {
      LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: hard Off1 still bad startOff="
                        << StartOff << " endOff=" << EndOff
                        << " (remat→SET intact; demote)\n");
      return recoverRangeOrOrder("range still bad after free lifts only");
    }
  }
  // Second BranchRelaxation may still grow short PC-relative branches in
  // SET→BEGIN and SET→END. Sum the contracts per-site expansion budget over
  // still-relaxable sites only; demote if residual Off1/Off2 margin cannot
  // absorb it. Constant: haydn::hwloop::MaxSingleBranchGrowthBytes
  // (LUI+ADDI32_W+JALR_W+pad parcels × product EncodedBytes). Second BR must
  // not invalidate acceptance.
  //
  // Not still-relaxable (zero further layout growth under second BR):
  //   already-indirect JALR*, long-reach JAL*, pure calls, ZOL latch metas.
  // Charging those would false-demote dense but already-final control flow.
  {
    using haydn::hwloop::BranchRelaxSafetyBufferBytes;
    using haydn::hwloop::MaxSingleBranchGrowthBytes;
    static_assert(BranchRelaxSafetyBufferBytes == MaxSingleBranchGrowthBytes,
                  "BR safety buffer is the contracts growth budget");

    auto isStillRelaxableShortBranch = [](const MachineInstr &Br) -> bool {
      if (!Br.isBranch())
        return false;
      // Already-indirect forms cannot grow further under BranchRelaxation.
      if (Br.isIndirectBranch())
        return false;
      // Calls (including JAL_W) are long-reach / not the short simm12 path.
      if (Br.isCall())
        return false;
      switch (Br.getOpcode()) {
      // ZOL / software-latch metas: not PC-relative BR subjects (see
      // HaydnInstrInfo::isBranchOffsetInRange). Fixup owns their lowering.
      case Haydn::PseudoLoopEnd:
      case Haydn::LoopJNZ:
      case Haydn::LoopDec:
      case Haydn::LoopStart:
      // Long-reach / already-final control (member forms included by
      // isIndirectBranch / isCall above; list logical/wide bases for clarity).
      case Haydn::JAL:
      case Haydn::JAL_W:
      case Haydn::JALR:
      case Haydn::JALR_W:
      case Haydn::PseudoCALL:
      case Haydn::BR_JT:
      case Haydn::RET:
        return false;
      default:
        // Bare/member short cond + B (simm12). Second BR may expand each to
        // an inverted near + trampoline or LUI+ADDI+JALR sequence.
        return true;
      }
    };

    auto countBranchGrowthIn = [&](const MachineInstr &Probe) -> int64_t {
      int64_t G = 0;
      if (Probe.isBundle()) {
        for (const MachineInstr *C = Probe.getNextNode();
             C && C->isBundledWithPred(); C = C->getNextNode()) {
          if (isStillRelaxableShortBranch(*C))
            G += MaxSingleBranchGrowthBytes;
        }
      } else if (isStillRelaxableShortBranch(Probe)) {
        G += MaxSingleBranchGrowthBytes;
      }
      return G;
    };
    // Inclusive=false: stop at ToMBB begin (BEGIN label = first real).
    // Inclusive=true: charge through the last body cycle only. HWLR_END is the
    // last size-bearing body parcel; soft-exit B/cond after PseudoLoopEnd sit
    // past END and cannot grow Off2. Charging them false-demotes dense Role-A
    // single-BB forms that keep an explicit B to a non-layout soft exit.
    auto sumStillRelaxableGrowth =
        [&](MachineBasicBlock::iterator FromIt, const MachineBasicBlock *ToMBB,
            bool InclusiveTo) -> int64_t {
      if (!ToMBB || !isLiveMBB(MF, ToMBB))
        return 0;
      int64_t Growth = 0;
      bool Started = false;
      for (const MachineBasicBlock &MBB : MF) {
        if (&MBB == Pre)
          Started = true;
        if (!Started)
          continue;
        auto Begin = (&MBB == Pre) ? FromIt : MBB.begin();
        for (auto I = Begin, E = MBB.end(); I != E; ++I) {
          if (&MBB == ToMBB && !InclusiveTo && I == ToMBB->begin())
            return Growth;
          // Inclusive END window ends at the ZOL latch meta: do not charge
          // PseudoLoopEnd or any terminator after it (post-END soft exit).
          if (&MBB == ToMBB && InclusiveTo &&
              I->getOpcode() == Haydn::PseudoLoopEnd)
            return Growth;
          Growth += countBranchGrowthIn(*I);
        }
        if (&MBB == ToMBB)
          return Growth;
      }
      return Growth;
    };

    MachineBasicBlock::iterator AfterSet = nextBundleBoundary(SetMI);
    int64_t BeginGrowth =
        sumStillRelaxableGrowth(AfterSet, StartMBB, /*InclusiveTo=*/false);
    int64_t EndGrowth =
        sumStillRelaxableGrowth(AfterSet, EndMBB, /*InclusiveTo=*/true);
    int64_t BeginMargin = MaxOff1Bytes - StartOff;
    int64_t EndMargin = MaxOff2Bytes - EndOff;
    if (BeginGrowth > BeginMargin || EndGrowth > EndMargin) {
      LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: post-Fixup BR growth budget "
                           "exceeds Off margin (beginGrowth="
                        << BeginGrowth << " beginMargin=" << BeginMargin
                        << " endGrowth=" << EndGrowth
                        << " endMargin=" << EndMargin << ")\n");
      return recoverRangeOrOrder("second-BR growth exceeds SET Off margin");
    }
  }

  LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: startOff=" << StartOff
                    << " endOff=" << EndOff
                    << " sameMBB=" << (StartMBB == EndMBB)
                    << " followingBundles="
                    << countFollowingBundles(SetMI, TII) << "\n");

  return Changed;
}

/// Sequentialize multi-member shells that coissue SET_HWLOOP with a producer
/// of its trip/Off GPRs (snapshot no-forwarding). Remat glue and post-pipeliner
/// trip adjust can stamp ADDI+SET after a preheader leaveMBB has already run;
/// this is the late owned correctness net before Off recompute / demote.
static bool sequentializeIllegalHwloopTripCoissue(MachineFunction &MF,
                                                  const HaydnInstrInfo &TII) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  bool Changed = false;

  SmallVector<MachineInstr *, 8> Roots;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!MI.isBundle() || MI.isBundledWithPred())
        continue;
      unsigned Kids = 0;
      for (MachineBasicBlock::instr_iterator I = std::next(MI.getIterator());
           I != MBB.instr_end() && I->isBundledWithPred(); ++I)
        ++Kids;
      if (Kids >= 2)
        Roots.push_back(&MI);
    }
  }

  for (MachineInstr *Root : Roots) {
    if (!Root || !Root->getParent())
      continue;
    MachineBasicBlock &MBB = *Root->getParent();
    SmallVector<MachineInstr *, 3> Kids;
    for (MachineBasicBlock::instr_iterator I = std::next(Root->getIterator());
         I != MBB.instr_end() && I->isBundledWithPred(); ++I)
      Kids.push_back(&*I);
    if (Kids.size() < 2)
      continue;
    if (!haydn::bundle::cycleMembersHaveHwloopTripConflict(Kids, TII, TRI))
      continue;

    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: sequentialize SET trip/Off "
                         "coissue in bb."
                      << MBB.getNumber() << "\n");
    for (MachineInstr *K : Kids) {
      if (!K)
        continue;
      for (MachineOperand &MO : K->operands()) {
        if (MO.isReg() && MO.isInternalRead())
          MO.setIsInternalRead(false);
      }
      if (K->isBundledWithPred())
        K->unbundleFromPred();
      if (K->isBundledWithSucc())
        K->unbundleFromSucc();
    }
    Root->eraseFromParent();
    // Keep schedule/def-before-use order; re-commit each as a late singleton
    // so EncodedBytes stay Format E product parcels for Off measurement.
    for (MachineInstr *K : Kids) {
      if (!K || !K->getParent())
        continue;
      unsigned Member = lateMemberOpcode(K->getOpcode());
      if (Member != K->getOpcode())
        K->setDesc(TII.get(Member));
      finalizeExactLateSingleton(*K);
    }
    Changed = true;
  }
  return Changed;
}

bool HaydnFixupHwLoops::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  const auto &TII =
      *static_cast<const HaydnInstrInfo *>(MF.getSubtarget().getInstrInfo());

  bool Changed = false;
  // Residual generic SET_HWLOOP{,_REG} is ExpandPseudos' rewrite. Hexagon
  // Fixup matches architectural LOOP only (HexagonFixupHwLoops.cpp:75-81)
  // then range-rechecks (136-148). Haydn Fixup fatals on leftovers; MIR that
  // injects generics must run ExpandPseudos first.
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB.instrs()) {
      unsigned Opc = MI.getOpcode();
      if (Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_REG)
        report_fatal_error(
            "HaydnFixupHwLoops: residual SET_HWLOOP{,_REG} — ExpandPseudos "
            "must rewrite to SET_HWLOOP_{W,F2_W} before Fixup",
            /*gen_crash_diag=*/false);
    }
  }

  // Layout ownership only: sequentialize illegal SET trip-reg coissue before
  // Off/Following walks. Semantic multi-stage peel repair is forbidden —
  // post-pipeliner either refuses before mutation or commits a valid loop.
  Changed |= sequentializeIllegalHwloopTripCoissue(MF, TII);

  // Collect first — inserting NOPs / demote invalidates iterators.
  // Walk instrs so SETs that PostRASched coissued (mid-bundle) are found.
  SmallVector<MachineInstr *, 8> Sets;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB.instrs()) {
      if (TII.isHardwareLoopSetupInstr(MI))
        Sets.push_back(&MI);
    }
  }
  for (MachineInstr *MI : Sets) {
    // Skip if a prior demote already erased this MI (should not share
    // pointers, but a later SET is never erased by an earlier one).
    if (!MI->getParent())
      continue;
    Changed |= fixupOne(*MI, TII);
  }

  return Changed;
}
