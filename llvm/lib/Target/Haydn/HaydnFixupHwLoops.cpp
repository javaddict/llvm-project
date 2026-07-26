//===-- HaydnFixupHwLoops.cpp - Post-layout HW loop validation ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Late pass (after BranchRelaxation): re-check SET_HWLOOP / LoopStart under
// the final size model, enforce the t-3 setup window, and keep Off1/Off2
// inside the encoder's uimm6/uimm12/4 fields.
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
// Role B already removed the software back-edge when forming ZOL. Erasing
// SET alone on a *live* body yields a once-through fallthrough (wrong-code).
// Product recovery is soft LoopDec+LoopJNZ when a free counter exists;
// live demote failure is fatal. demote OFF is debug-only (force erase-setup).
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
// from the MI after SET. If Header is not after SET in layout, Off is
// unknown → treat as range-bad.
//
// 3. Recoverability ladder (correctness only)
// a. Pad setup gap only (deficit-only t−3 NOPs after SET → BEGIN).
// b. tryShortenStartOffset — free-only lifts before count unit (not SET
//    alone); never across remat→SET; never dangerous peel.
// c. demoteToSoftwareLoop when hard Off1/Off2 still illegal.
// Soft-loop restore is *closed* (total, like AIE expand):
// • Collect loop blocks by CFG (reverse from Latch to Header)
// never by layout range — layout can put Latch before Header.
// • CountReg = trip Prefer if body does not non-countdown-def it;
// else scavenge a reg not mentioned in any loop block.
// • Materialize trip into CountReg at the SET site when needed.
// • Strip residual countdown of CountReg from the latch (Role B
// leftover Prefer+=-1), then always install LoopDec+LoopJNZ
// (AIE JNZD pair). Never LoopJNZ-only, never double-dec.
// • SET_HWLOOP imm: materialize into a free reg via ADDI, then
// LoopDec+LoopJNZ.
// d. If demote cannot install a correct soft edge on a *live* body
// report_fatal_error — never erase-only once-through.
// Dead Header/Latch still allow erase-setup only (body gone).
// Debug only: -haydn-enable-hwloop-demote=false force erase-setup
// (once-through body risk — never a product setting).
// 4. Pipeline
// addPreEmit: BranchRelaxation → FixupHwLoops → BranchRelaxation again
// so Fixup growth cannot leave branches past simm12.
//
// AsmPrinter is the emit-side twin: no START/END temp symbols without a
// real body instruction to flush them.
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"
#include "HaydnFrameLowering.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPostRAScratch.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-fixup-hwloops"

// Product demote policy: default ON — demote-first, not erase-only.
// Out-of-range / unencodable SET must not drop control and leave a single-pass
// body (Role B already removed the software back-edge). Recovery ladder:
// a) demoteToSoftwareLoop (LoopDec+LoopJNZ) when free counter GPR exists
// b) Header/Latch dead → erase-setup only is OK (body gone)
// c) live body but demote cannot install soft edge → report_fatal_error
// (never silent erase-only once-through)
// Counter pick: LivePhysRegs at SET site + loop-block mention filter; never
// invent a clobbering free AT.
// Flag OFF = debug-only force erase-setup (once-through risk; not product).
static cl::opt<bool> EnableHaydnHwLoopDemote(
    "haydn-enable-hwloop-demote", cl::Hidden, cl::init(true),
    cl::desc("Demote out-of-range / invalid ZOL to software LoopDec+LoopJNZ "
             "when a free counter GPR exists (LivePhysRegs). Default ON "
             "(product demote-first). OFF = force erase-setup only "
             "(debug only; once-through body risk; not product)."));
namespace {

// Real wide forms (post ExpandPseudos) + residual logicals + setDesc members.
static bool isHwloopSetup(unsigned Opc) {
  return Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_REG ||
         Opc == Haydn::SET_HWLOOP_W || Opc == Haydn::SET_HWLOOP_F2_W ||
         Opc == Haydn::SET_HWLOOP_W_S0 || Opc == Haydn::SET_HWLOOP_F2_W_S0;
}
static bool isHwloopRegTrip(unsigned Opc) {
  return Opc == Haydn::SET_HWLOOP_REG || Opc == Haydn::SET_HWLOOP_F2_W ||
         Opc == Haydn::SET_HWLOOP_F2_W_S0;
}
static bool isHwloopImmTrip(unsigned Opc) {
  return Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_W ||
         Opc == Haydn::SET_HWLOOP_W_S0;
}

// Aliases from HaydnHWLoopContracts.h / BundlePlan EncodedBytes (B4.4).
// Ceil byte→parcel and setup distances use productParcelBytes /
// ceilProductParcels — not a second hard-coded 16.
static constexpr unsigned MinSetupBundles = haydn::hwloop::MinSetupBundles;
static constexpr int64_t MinSetupBytes = haydn::hwloop::MinSetupBytes;
static constexpr int64_t MaxOff1Bytes = haydn::hwloop::MaxStartOffsetBytes;
static constexpr int64_t MaxOff2Bytes = haydn::hwloop::MaxEndOffsetBytes;
static constexpr int64_t MaxOff1BytesSafe =
    haydn::hwloop::MaxStartOffsetBytesSafe;

class HaydnFixupHwLoops : public MachineFunctionPass {
public:
  static char ID;
  HaydnFixupHwLoops() : MachineFunctionPass(ID) {
    initializeHaydnFixupHwLoopsPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "Haydn Hardware Loop Fixup";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    // Demotion rewrites latch successors / terminators (CFG change).
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  bool fixupOne(MachineInstr &SetMI, const HaydnInstrInfo &TII);
  MachineBasicBlock *resolveBodyMBB(MachineInstr &SetMI) const;
  unsigned countFollowingBundles(MachineInstr &SetMI,
                                 const HaydnInstrInfo &TII) const;
  int64_t estimateMBBDistance(const MachineFunction &MF,
                              const MachineBasicBlock *FromMBB,
                              MachineBasicBlock::const_iterator FromIt,
                              const MachineBasicBlock *ToMBB,
                              const HaydnInstrInfo &TII) const;
  bool computeOffsets(MachineInstr &SetMI, const HaydnInstrInfo &TII,
                      int64_t &StartOff, int64_t &EndOff,
                      MachineBasicBlock *&StartMBB,
                      MachineBasicBlock *&EndMBB) const;
  bool tryShortenStartOffset(MachineInstr &SetMI, const HaydnInstrInfo &TII,
                             int64_t &StartOff, int64_t &EndOff);
  bool demoteToSoftwareLoop(MachineInstr &SetMI, const HaydnInstrInfo &TII);
  bool eraseHardwareSetup(MachineInstr &SetMI);

  // Blocks of the hardware loop body: Header, Latch, and every CFG
  // predecessor of Latch that can reach Latch without leaving the loop
  // (reverse walk, stop at Header / Preheader). Layout order is irrelevant
  // after placement Latch may sit *before* Header in the function.
  using LoopBlockSet = SmallPtrSet<const MachineBasicBlock *, 8>;
  static void collectLoopBlocks(const MachineBasicBlock *Header,
                                const MachineBasicBlock *Latch,
                                const MachineBasicBlock *Preheader,
                                LoopBlockSet &Out);

  // True if any MI in \p Blocks mentions \p Reg (use or def).
  static bool regMentionedInBlocks(Register Reg, const LoopBlockSet &Blocks);
  // True if \p Reg is defined in \p Blocks by a non-countdown op (load dest
  // move, etc.). Pure residual countdown (Reg+=-1 / Reg-=1) is allowed.
  static bool regClobberedNonCountdownIn(Register Reg,
                                         const LoopBlockSet &Blocks);
  // True if \p MI is a residual countdown step of \p Reg (Role B leftover).
  static bool isCountdownStepOf(const MachineInstr &MI, Register Reg);
  // Erase residual countdown steps of \p Reg from \p Latch (so LoopDec is
  // the sole dec). Walk instrs for BUNDLE interiors.
  static void stripResidualCountdown(MachineBasicBlock *Latch, Register Reg);

  // Pick a free GPR for soft-loop countdown at \p InsertPt in \p Preheader.
  // Returns invalid Register if none is free (caller must refuse erase-only
  // on a live body — fatal via recoverRangeOrOrder).
  Register pickCounterReg(const LoopBlockSet &Blocks, Register Prefer,
                          const HaydnSubtarget &ST, MachineBasicBlock &Preheader,
                          MachineBasicBlock::iterator InsertPt) const;
  void materializeTripCount(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator InsertPt, DebugLoc DL,
                            const HaydnInstrInfo &TII, Register Dst,
                            Register SrcReg, int64_t SrcImm, bool HasImm);

  // True iff \p MBB is a live block of \p MF (not erased / renumbered out).
  // SET operands can hold `%bb.-1` after CFG merges; those are not live.
  static bool isLiveMBB(const MachineFunction &MF,
                        const MachineBasicBlock *MBB) {
    return MBB && MBB->getParent() == &MF && MBB->getNumber() >= 0;
  }

  // If \p BundleRoot has no remaining children after an unbundle, erase it.
  // B1.2: HaydnFinalizeBundle wraps SET/LoopStart as singleton BUNDLEs;
  // unbundling the only child must not leave an empty BUNDLE shell that still
  // carries kill flags (verifier: "Using an undefined physical register").
  static void eraseEmptyBundleRoot(MachineInstr *BundleRoot) {
    if (!BundleRoot || !BundleRoot->isBundle() || !BundleRoot->getParent())
      return;
    MachineBasicBlock::instr_iterator Next =
        std::next(BundleRoot->getIterator());
    MachineBasicBlock *MBB = BundleRoot->getParent();
    if (Next != MBB->instr_end() && Next->isBundledWithPred())
      return; // still has children
    BundleRoot->eraseFromParent();
  }

  // Safe erase of a (possibly bundled) MI collected by pointer.
  static void eraseInstrSafe(MachineInstr *MI) {
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
};

char HaydnFixupHwLoops::ID = 0;

} // namespace

INITIALIZE_PASS(HaydnFixupHwLoops, DEBUG_TYPE, "Haydn Hardware Loop Fixup",
                false, false)

FunctionPass *llvm::createHaydnFixupHwLoopsPass() {
  return new HaydnFixupHwLoops();
}

// Next *bundle-boundary* iterator after \p MI. `std::next(MI.getIterator)`
// advances one instruction and can land on a BUNDLE interior
// (`isBundledWithPred`); constructing `MachineBasicBlock::iterator` from
// that asserts (— CoreMark/moddi3 after PostRA co-issue).
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
    // B4.4: parcel count via product EncodedBytes (ProductFormatDesc.Bytes).
    Bundles += haydn::bundle::ceilProductParcels(Bytes);
  }
  return Bundles;
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

MachineBasicBlock *
HaydnFixupHwLoops::resolveBodyMBB(MachineInstr &SetMI) const {
  const MachineFunction *MF = SetMI.getParent() ? SetMI.getParent()->getParent()
                                                : nullptr;
  if (!MF)
    return nullptr;

  unsigned Opc = SetMI.getOpcode();
  if (isHwloopSetup(Opc) &&
      SetMI.getNumOperands() >= 3 && SetMI.getOperand(1).isMBB()) {
    MachineBasicBlock *H = SetMI.getOperand(1).getMBB();
    return isLiveMBB(*MF, H) ? H : nullptr;
  }

  // LoopStart: body is the layout successor that carries PseudoLoopEnd, or
  // the single fallthrough successor of the preheader.
  if (Opc == Haydn::LoopStart) {
    MachineBasicBlock *Pre = SetMI.getParent();
    for (MachineBasicBlock *Succ : Pre->successors()) {
      if (!isLiveMBB(*MF, Succ))
        continue;
      for (const MachineInstr &T : Succ->terminators()) {
        if (T.getOpcode() == Haydn::PseudoLoopEnd)
          return Succ;
      }
    }
    if (Pre->succ_size() == 1) {
      MachineBasicBlock *S = *Pre->succ_begin();
      return isLiveMBB(*MF, S) ? S : nullptr;
    }
    MachineBasicBlock *Next = Pre->getNextNode();
    return isLiveMBB(*MF, Next) ? Next : nullptr;
  }
  return nullptr;
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

  // LoopStart (IR ZOL): body MBB from PseudoLoopEnd / layout successor.
  // Without this, AsmPrinter still emits set_hwloop_f2_w with Off1 that can
  // exceed uimm6 → "relocation offset out of range" / missing END labels.
  if (SetMI.getOpcode() == Haydn::LoopStart) {
    StartMBB = resolveBodyMBB(SetMI);
    EndMBB = StartMBB;
    if (!StartMBB)
      return false;
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

  // After SET: next *top-level* MI. Never hand a mid-bundle iterator to
  // MachineInstrBundleIterator (asserts isBundledWithPred).
  MachineBasicBlock *Pre = SetMI.getParent();
  MachineBasicBlock::iterator AfterSet = SetMI.getIterator();
  ++AfterSet;
  while (AfterSet != Pre->end() && AfterSet->isBundledWithPred())
    ++AfterSet;
  StartOff = estimateMBBDistance(*MF, Pre, AfterSet, StartMBB, TII);
  EndOff = estimateMBBDistance(*MF, Pre, AfterSet, EndMBB, TII);
  if (EndMBB && EndOff >= 0) {
    for (const MachineInstr &MI : *EndMBB)
      EndOff += TII.getInstSizeInBytes(MI);
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

static Register getHwloopCountReg(const MachineInstr &SetMI) {
  unsigned Opc = SetMI.getOpcode();
  if (isHwloopRegTrip(Opc) && SetMI.getNumOperands() >= 4 &&
      SetMI.getOperand(3).isReg())
    return SetMI.getOperand(3).getReg();
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
static MachineInstr &getCountUnitHead(MachineInstr &SetMI) {
  Register Count = getHwloopCountReg(SetMI);
  if (!Count)
    return SetMI;
  MachineBasicBlock *MBB = SetMI.getParent();
  MachineBasicBlock::iterator It = SetMI.getIterator();
  if (It == MBB->begin())
    return SetMI;
  MachineBasicBlock::iterator PrevIt = std::prev(It);
  while (PrevIt != MBB->begin() &&
         (PrevIt->isMetaInstruction() || PrevIt->isDebugInstr() ||
          PrevIt->isKill() || PrevIt->isImplicitDef()))
    --PrevIt;
  // After unbundle, remat may be a BUNDLE containing only ADDI, or bare ADDI.
  if (miOrBundleDefines(*PrevIt, Count))
    return *PrevIt;
  return SetMI;
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

  MachineInstr &Head = getCountUnitHead(SetMI);
  SmallSet<Register, 8> ExtLiveIns;
  SmallSet<Register, 8> HeadDefs;
  collectCountUnitExtLiveIns(Head, SetMI, ExtLiveIns);
  collectCountUnitHeadDefs(Head, HeadDefs);

  while (StartOff > MaxOff1BytesSafe) {
    unsigned Following = countFollowingBundles(SetMI, TII);
    if (Following <= MinSetupBundles)
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
      // B4.4: ceil by committed product EncodedBytes, not a dual magic 16.
      CandBundles = haydn::bundle::ceilProductParcels(Bytes);
      break;
    }
    if (!Cand || CandBundles == 0)
      break;
    if (Following - CandBundles < MinSetupBundles)
      break;

    // Splice before count-unit head (remat), NEVER before SET alone.
    MachineInstr &CurHead = getCountUnitHead(SetMI);
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

void HaydnFixupHwLoops::collectLoopBlocks(const MachineBasicBlock *Header,
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

bool HaydnFixupHwLoops::isCountdownStepOf(const MachineInstr &MI,
                                          Register Reg) {
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

  unsigned Opc = MI.getOpcode();
  if (Opc == Haydn::LoopDec)
    return true;
  if (Opc == Haydn::ADDI32 || Opc == Haydn::ADDI32_W) {
    return MI.getNumOperands() >= 3 && MI.getOperand(1).isReg() &&
           MI.getOperand(1).getReg() == Reg && MI.getOperand(2).isImm() &&
           MI.getOperand(2).getImm() == -1;
  }
  if (Opc == Haydn::SUBI32) {
    return MI.getNumOperands() >= 3 && MI.getOperand(1).isReg() &&
           MI.getOperand(1).getReg() == Reg && MI.getOperand(2).isImm() &&
           MI.getOperand(2).getImm() == 1;
  }
  if (Opc == Haydn::ADD32 || Opc == Haydn::SUB32) {
    // Prefer = Prefer + K / Prefer - K (K often a phys holding -1).
    return MI.getNumOperands() >= 3 && MI.getOperand(1).isReg() &&
           MI.getOperand(1).getReg() == Reg;
  }
  // Flex / slot-suffixed forms (ADD32_S0, …): operand-shape fallback.
  if (MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
      MI.getOperand(0).getReg() == Reg && MI.getOperand(1).isReg() &&
      MI.getOperand(1).getReg() == Reg) {
    if (MI.getOperand(2).isImm()) {
      int64_t Imm = MI.getOperand(2).getImm();
      return Imm == -1 || Imm == 1;
    }
    if (MI.getOperand(2).isReg())
      return true;
  }
  return false;
}

bool HaydnFixupHwLoops::regMentionedInBlocks(Register Reg,
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

bool HaydnFixupHwLoops::regClobberedNonCountdownIn(Register Reg,
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

void HaydnFixupHwLoops::stripResidualCountdown(MachineBasicBlock *Latch,
                                               Register Reg) {
  if (!Latch || !Reg.isPhysical())
    return;
  SmallVector<MachineInstr *, 4> Kill;
  for (MachineInstr &MI : Latch->instrs()) {
    if (isCountdownStepOf(MI, Reg) && MI.getOpcode() != Haydn::LoopDec)
      Kill.push_back(&MI);
  }
  for (MachineInstr *MI : Kill)
    eraseInstrSafe(MI);
}

Register HaydnFixupHwLoops::pickCounterReg(
    const LoopBlockSet &Blocks, Register Prefer, const HaydnSubtarget &ST,
    MachineBasicBlock &Preheader, MachineBasicBlock::iterator InsertPt) const {
  // Free counter must be:
  // 1) not mentioned in any CFG loop block (lc_dp_lis: layout range missed
  // latch earlier in the function — CFG Blocks is required);
  // 2) available at the SET insert point (LivePhysRegs — AIE/RISC-V style
  // post-RA scavenge, not "first preferred even if live").
  // Fail-closed: return invalid Register rather than Prefer/R11 when both
  // are live (old code clobbered live-through temps under demote).
  const TargetRegisterInfo &TRI = *ST.getRegisterInfo();
  const MachineRegisterInfo &MRI = Preheader.getParent()->getRegInfo();

  LivePhysRegs LPR(TRI);
  LPR.addLiveOuts(Preheader);
  for (MachineBasicBlock::iterator II = Preheader.end(); II != InsertPt;) {
    --II;
    LPR.stepBackward(*II);
  }

  auto isUsable = [&](Register R) -> bool {
    if (!R.isPhysical() || R == Haydn::R0 || R == Haydn::R13 || R == Haydn::R15)
      return false;
    if (MRI.isReserved(R))
      return false;
    if (regMentionedInBlocks(R, Blocks))
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
  LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: pickCounterReg — no free GPR "
                       "(LivePhysRegs + loop mention); demote refuses "
                       "erase-only on live body\n");
  return Register();
}
void HaydnFixupHwLoops::materializeTripCount(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt, DebugLoc DL,
    const HaydnInstrInfo &TII, Register Dst, Register SrcReg, int64_t SrcImm,
    bool HasImm) {
  if (!HasImm) {
    if (SrcReg == Dst)
      return;
    // Post-RA: emit real MOVE32 via copyPhysReg. Generic COPY can be dropped
    // or poorly handled this late (expand-pseudos already ran).
    TII.copyPhysReg(MBB, InsertPt, DL, Dst, SrcReg, /*KillSrc=*/false);
    return;
  }

  // uimm16 trip counts: XOR-zero then ADDI32_W (expandPostRA already ran).
  if (SrcImm == 0) {
    BuildMI(MBB, InsertPt, DL, TII.get(Haydn::XOR32), Dst)
        .addReg(Haydn::R0)
        .addReg(Haydn::R0);
    return;
  }
  BuildMI(MBB, InsertPt, DL, TII.get(Haydn::XOR32), Dst)
      .addReg(Haydn::R0)
      .addReg(Haydn::R0);
  BuildMI(MBB, InsertPt, DL, TII.get(Haydn::ADDI32_W), Dst)
      .addReg(Dst)
      .addImm(SrcImm);
}

// Erase SET/LoopStart and any PseudoLoopEnd in the (live) body. Always safe
// w.r.t. MC: no hwloop fixup remains. Body may fall through once.
bool HaydnFixupHwLoops::eraseHardwareSetup(MachineInstr &SetMI) {
  MachineBasicBlock *Pre = SetMI.getParent();
  if (!Pre)
    return false;
  const MachineFunction &MF = *Pre->getParent();

  SmallVector<MachineInstr *, 8> ToErase;
  ToErase.push_back(&SetMI);

  // Collect PseudoLoopEnd from live body (LoopStart) or live Header/Latch.
  auto collectPLE = [&](MachineBasicBlock *BB) {
    if (!isLiveMBB(MF, BB))
      return;
    for (MachineInstr &MI : BB->instrs()) {
      if (MI.isBundledWithPred())
        continue;
      if (MI.getOpcode() == Haydn::PseudoLoopEnd)
        ToErase.push_back(&MI);
    }
  };

  unsigned Opc = SetMI.getOpcode();
  if (Opc == Haydn::LoopStart) {
    collectPLE(resolveBodyMBB(SetMI));
  } else if (SetMI.getNumOperands() >= 3) {
    if (SetMI.getOperand(1).isMBB())
      collectPLE(SetMI.getOperand(1).getMBB());
    if (SetMI.getOperand(2).isMBB()) {
      MachineBasicBlock *L = SetMI.getOperand(2).getMBB();
      if (SetMI.getOperand(1).isMBB() && L != SetMI.getOperand(1).getMBB())
        collectPLE(L);
    }
  }

  // Dedup pointers.
  SmallPtrSet<MachineInstr *, 8> Seen;
  for (MachineInstr *MI : ToErase) {
    if (!MI || !Seen.insert(MI).second)
      continue;
    eraseInstrSafe(MI);
  }
  return true;
}

// Demote SET_HWLOOP{,_REG} / LoopStart to a countable software loop:
// materialise the trip counter at the former SET site, erase SET (and
// PseudoLoopEnd for ZOL), restore latch Header+Exit edges with
// LoopDec+LoopJNZ (AsmPrinter → SUBI32 + BNEZ_W).
// Return value :
// true — handled: soft edge installed, OR L1 erase-only because
// Header/Latch/body is dead (body gone / peeled).
// false — live loop body but soft edge cannot be installed (no free
// counter GPR, no usable exit, or unparseable trip). Caller must
// NOT erase-only: keep SET (pad already applied) or fatal.
// Fail-closed on dead MBB: if Header/Latch are not live (e.g. `%bb.-1`)
// only erase the SET — never walk a dead MBB.
bool HaydnFixupHwLoops::demoteToSoftwareLoop(MachineInstr &SetMI,
                                             const HaydnInstrInfo &TII) {
  unsigned Opc = SetMI.getOpcode();
  const bool IsLoopStart = Opc == Haydn::LoopStart;
  if (!isHwloopSetup(Opc) && !IsLoopStart)
    return false;

  MachineBasicBlock *Preheader = SetMI.getParent();
  if (!Preheader)
    return false;
  MachineFunction &MF = *Preheader->getParent();

  MachineBasicBlock *Header = nullptr;
  MachineBasicBlock *Latch = nullptr;
  Register Prefer;
  int64_t Imm = 0;
  bool HasImm = false;

  if (IsLoopStart) {
    if (!SetMI.getOperand(0).isReg())
      return eraseHardwareSetup(SetMI);
    Prefer = SetMI.getOperand(0).getReg();
    Header = resolveBodyMBB(SetMI);
    Latch = Header;
  } else {
    if (SetMI.getNumOperands() < 4 || !SetMI.getOperand(1).isMBB() ||
        !SetMI.getOperand(2).isMBB())
      return eraseHardwareSetup(SetMI);
    Header = SetMI.getOperand(1).getMBB();
    Latch = SetMI.getOperand(2).getMBB();
    if (isHwloopRegTrip(Opc)) {
      if (!SetMI.getOperand(3).isReg())
        return eraseHardwareSetup(SetMI);
      Prefer = SetMI.getOperand(3).getReg();
    } else {
      if (!SetMI.getOperand(3).isImm())
        return eraseHardwareSetup(SetMI);
      Imm = SetMI.getOperand(3).getImm();
      HasImm = true;
    }
  }

  // Contract §1: dead MBB operands → L1 erase only (body gone).
  if (!isLiveMBB(MF, Header) || !isLiveMBB(MF, Latch)) {
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: demote L1-only — Header/Latch "
                         "not live in MF (stale %bb.-1 or erased body)\n");
    return eraseHardwareSetup(SetMI);
  }

  DebugLoc DL = SetMI.getDebugLoc();

  // Exit: after convert the latch falls through / branches only to Exit.
  MachineBasicBlock *Exit = nullptr;
  if (Latch->succ_size() == 1) {
    Exit = *Latch->succ_begin();
  } else {
    for (MachineBasicBlock *S : Latch->successors()) {
      if (S != Header && isLiveMBB(MF, S)) {
        Exit = S;
        break;
      }
    }
  }
  if (!Exit) {
    // Layout fallthrough of latch.
    MachineFunction::iterator LatchIt = Latch->getIterator();
    MachineFunction::iterator NextIt = std::next(LatchIt);
    if (NextIt != MF.end() && isLiveMBB(MF, &*NextIt))
      Exit = &*NextIt;
  }
  if (!isLiveMBB(MF, Exit)) {
    // Live body but no usable exit — cannot install soft edge. Leave SET
    // intact for the caller (never erase-only once-through).
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: demote refused — no live exit "
                         "(body still live)\n");
    return false;
  }

  // Closed demote model (AIE expand-style: total restore, no half state):
  // Decide CountReg *before* any erase. Only then L1 erase + L2 soft edge.
  // Never erase SET when soft edge cannot be installed on a live body.

  LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: demoting hwloop header="
                    << printMBBReference(*Header) << " latch="
                    << printMBBReference(*Latch) << " exit="
                    << printMBBReference(*Exit)
                    << (IsLoopStart ? " (LoopStart)\n" : "\n"));

  // CFG loop blocks (not layout range — Latch may precede Header).
  LoopBlockSet LoopBlocks;
  collectLoopBlocks(Header, Latch, Preheader, LoopBlocks);

  // Snapshot soft-loop decision *before* erasing SetMI (operands die with it).
  Register CountReg;
  bool InstallSoftLoop = false;
  bool UseStackCounter = false;
  int StackCounterFI = -1;
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnFrameLowering *TFL = ST.getFrameLowering();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();

  auto canUsePreferAsCounter = [&]() -> bool {
    return Prefer.isPhysical() && Prefer != Haydn::R0 && Prefer != Haydn::R13 &&
           Prefer != Haydn::R15 &&
           !regClobberedNonCountdownIn(Prefer, LoopBlocks);
  };

  auto resolveScratchFI = [&]() -> int {
    int FI = FuncInfo->getBranchRelaxationScratchFI();
    if (FI < 0)
      FI = FuncInfo->getPostRAScratchFI();
    return FI;
  };

  if ((IsLoopStart || isHwloopRegTrip(Opc)) && Prefer.isPhysical() &&
      Prefer != Haydn::R0) {
    // Trip reg at LoopStart / SET_HWLOOP_REG.
    // Prefer is correct only if the body does not redefine it as a
    // non-countdown (e.g. S_LW_POST dest = trip). Residual Prefer+=-1 is OK
    // we strip it below and install a single LoopDec.
    if (canUsePreferAsCounter()) {
      CountReg = Prefer;
      InstallSoftLoop = true;
    } else {
      MachineBasicBlock::iterator Ins = SetMI.getIterator();
      CountReg = pickCounterReg(LoopBlocks, Prefer, ST, *Preheader, Ins);
      if (CountReg.isPhysical()) {
        materializeTripCount(*Preheader, Ins, DL, TII, CountReg, Prefer, 0,
                             /*HasImm=*/false);
        InstallSoftLoop = true;
        LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: demote trip "
                          << printReg(Prefer)
                          << " clobbered in body — counter "
                          << printReg(CountReg) << "\n");
      }
    }
  } else if (!IsLoopStart && (isHwloopImmTrip(Opc) || HasImm)) {
    // Imm form: need a free GPR + materialize.
    MachineBasicBlock::iterator Ins = SetMI.getIterator();
    CountReg = pickCounterReg(LoopBlocks, Prefer, ST, *Preheader, Ins);
    if (CountReg.isPhysical()) {
      materializeTripCount(*Preheader, Ins, DL, TII, CountReg, Prefer, Imm,
                           /*HasImm=*/true);
      InstallSoftLoop = true;
    }
  }

  // No free body-wide counter: keep trip on a post-RA scratch FI and reload
  // each latch with a short-lived scratch (does not steal body physregs).
  if (!InstallSoftLoop) {
    StackCounterFI = resolveScratchFI();
    if (StackCounterFI >= 0 &&
        ((Prefer.isPhysical() && Prefer != Haydn::R0) || HasImm)) {
      MachineBasicBlock::iterator Ins = SetMI.getIterator();
      Register FrameReg;
      int64_t Off =
          TFL->getFrameIndexReference(MF, StackCounterFI, FrameReg).getFixed();
      if (HasImm) {
        // Materialize imm into a free/scratch temp, then store to FI.
        withPostRAScratch(
            *Preheader, Ins, DL, TII, ST, /*PreferNotR12=*/true,
            [&](Register Scr) {
              materializeTripCount(*Preheader, Ins, DL, TII, Scr, Prefer, Imm,
                                   /*HasImm=*/true);
              if (isInt<16>(Off))
                BuildMI(*Preheader, Ins, DL, TII.get(Haydn::ST32))
                    .addReg(Scr, getKillRegState(true))
                    .addReg(FrameReg)
                    .addImm(Off);
              else {
                // Rare large FI: use R0 as address temp (xor-zero after).
                BuildMI(*Preheader, Ins, DL, TII.get(Haydn::ADDI32_W), Haydn::R0)
                    .addReg(FrameReg)
                    .addImm(Off);
                BuildMI(*Preheader, Ins, DL, TII.get(Haydn::ST32))
                    .addReg(Scr, getKillRegState(true))
                    .addReg(Haydn::R0)
                    .addImm(0);
                BuildMI(*Preheader, Ins, DL, TII.get(Haydn::XOR32), Haydn::R0)
                    .addReg(Haydn::R0)
                    .addReg(Haydn::R0);
              }
            },
            /*Exclude=*/Prefer.isPhysical() ? ArrayRef<Register>{Prefer}
                                            : ArrayRef<Register>{});
      } else {
        // Prefer holds trip at SET; store it to FI before erase.
        if (isInt<16>(Off))
          BuildMI(*Preheader, Ins, DL, TII.get(Haydn::ST32))
              .addReg(Prefer)
              .addReg(FrameReg)
              .addImm(Off);
        else {
          BuildMI(*Preheader, Ins, DL, TII.get(Haydn::ADDI32_W), Haydn::R0)
              .addReg(FrameReg)
              .addImm(Off);
          BuildMI(*Preheader, Ins, DL, TII.get(Haydn::ST32))
              .addReg(Prefer)
              .addReg(Haydn::R0)
              .addImm(0);
          BuildMI(*Preheader, Ins, DL, TII.get(Haydn::XOR32), Haydn::R0)
              .addReg(Haydn::R0)
              .addReg(Haydn::R0);
        }
      }
      UseStackCounter = true;
      InstallSoftLoop = true;
      LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: demote stack-counter FI#"
                        << StackCounterFI << "\n");
    }
  }

  if (!InstallSoftLoop) {
    // Live body, no free counter / unusable trip — refuse erase-only.
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: demote refused — no free counter "
                         "GPR for soft edge (body still live)\n");
    return false;
  }

  // Strip residual countdown of CountReg *before* erasing SET / rewriting
  // latch, while Latch is still intact. (Role B leaves Prefer+=-1.)
  if (!UseStackCounter)
    stripResidualCountdown(Latch, CountReg);
  else if (Prefer.isPhysical())
    stripResidualCountdown(Latch, Prefer);

  // L1: erase hardware setup (SET + PLE)
  eraseHardwareSetup(SetMI);
  // SetMI is gone; do not touch it again.

  // Re-validate live blocks after erase (should still be live).
  if (!isLiveMBB(MF, Header) || !isLiveMBB(MF, Latch) || !isLiveMBB(MF, Exit))
    return true;

  // L2: rewrite latch to software counted back-edge
  if (!UseStackCounter) {
    auto ensureLiveIn = [](MachineBasicBlock *MBB, Register R) {
      if (!R.isPhysical() || !MBB)
        return;
      if (!MBB->isLiveIn(R))
        MBB->addLiveIn(R);
    };
    for (const MachineBasicBlock *B : LoopBlocks)
      ensureLiveIn(const_cast<MachineBasicBlock *>(B), CountReg);
    ensureLiveIn(Header, CountReg);
    ensureLiveIn(Latch, CountReg);
  }

  // Drop existing top-level terminators, then LoopDec+LoopJNZ + optional B Exit.
  SmallVector<MachineInstr *, 4> Terms;
  for (MachineInstr &MI : Latch->instrs()) {
    if (MI.isBundledWithPred())
      continue;
    if (MI.isTerminator())
      Terms.push_back(&MI);
  }
  for (MachineInstr *MI : Terms)
    eraseInstrSafe(MI);

  while (!Latch->succ_empty())
    Latch->removeSuccessor(Latch->succ_begin());
  Latch->addSuccessor(Header);
  if (Exit != Header)
    Latch->addSuccessor(Exit);

  if (UseStackCounter) {
    Register FrameReg;
    int64_t Off =
        TFL->getFrameIndexReference(MF, StackCounterFI, FrameReg).getFixed();
    MachineBasicBlock::iterator LatchEnd = Latch->end();
    withPostRAScratch(
        *Latch, LatchEnd, DL, TII, ST, /*PreferNotR12=*/true,
        [&](Register Scr) {
          if (isInt<16>(Off))
            BuildMI(*Latch, LatchEnd, DL, TII.get(Haydn::LD32), Scr)
                .addReg(FrameReg)
                .addImm(Off);
          else {
            BuildMI(*Latch, LatchEnd, DL, TII.get(Haydn::ADDI32_W), Haydn::R0)
                .addReg(FrameReg)
                .addImm(Off);
            BuildMI(*Latch, LatchEnd, DL, TII.get(Haydn::LD32), Scr)
                .addReg(Haydn::R0)
                .addImm(0);
            BuildMI(*Latch, LatchEnd, DL, TII.get(Haydn::XOR32), Haydn::R0)
                .addReg(Haydn::R0)
                .addReg(Haydn::R0);
          }
          BuildMI(*Latch, LatchEnd, DL, TII.get(Haydn::LoopDec), Scr)
              .addReg(Scr);
          if (isInt<16>(Off))
            BuildMI(*Latch, LatchEnd, DL, TII.get(Haydn::ST32))
                .addReg(Scr)
                .addReg(FrameReg)
                .addImm(Off);
          else {
            BuildMI(*Latch, LatchEnd, DL, TII.get(Haydn::ADDI32_W), Haydn::R0)
                .addReg(FrameReg)
                .addImm(Off);
            BuildMI(*Latch, LatchEnd, DL, TII.get(Haydn::ST32))
                .addReg(Scr)
                .addReg(Haydn::R0)
                .addImm(0);
            BuildMI(*Latch, LatchEnd, DL, TII.get(Haydn::XOR32), Haydn::R0)
                .addReg(Haydn::R0)
                .addReg(Haydn::R0);
          }
          BuildMI(*Latch, LatchEnd, DL, TII.get(Haydn::LoopJNZ))
              .addReg(Scr, getKillRegState(true))
              .addMBB(Header);
        });
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: demote stack-counter LoopDec+JNZ "
                         "FI#"
                      << StackCounterFI << "\n");
  } else {
    // Always the AIE JNZD pair: one dec, one branch. Residual was stripped.
    BuildMI(*Latch, Latch->end(), DL, TII.get(Haydn::LoopDec), CountReg)
        .addReg(CountReg);
    BuildMI(*Latch, Latch->end(), DL, TII.get(Haydn::LoopJNZ))
        .addReg(CountReg)
        .addMBB(Header);
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: demote LoopDec+LoopJNZ on "
                      << printReg(CountReg) << "\n");
  }

  MachineFunction::iterator LatchIt = Latch->getIterator();
  MachineFunction::iterator NextIt = std::next(LatchIt);
  bool ExitIsLayoutFallthrough =
      (NextIt != MF.end()) && (&*NextIt == Exit);
  if (!ExitIsLayoutFallthrough && Exit != Header) {
    SmallVector<MachineOperand, 0> NoCond;
    TII.insertBranch(*Latch, Exit, /*FBB=*/nullptr, NoCond, DL);
  }

  return true;
}
bool HaydnFixupHwLoops::fixupOne(MachineInstr &SetMI,
                                   const HaydnInstrInfo &TII) {
  bool Changed = false;
  DebugLoc DL = SetMI.getDebugLoc();
  MachineBasicBlock *Pre = SetMI.getParent();
  if (!Pre)
    return false;
  const MachineFunction &MF = *Pre->getParent();

  // SET/LoopStart must be top-level for MBB iterators. Unbundle for safety.
  // Remat ADDI (if unbundled from SET) stays the previous MI — free lifts
  // splice before that head, not between remat and SET.
  // B1.2: singleton BUNDLE from HaydnFinalizeBundle — erase empty root after
  // unbundle so kill flags do not outlive the SET use (verifier).
  if (SetMI.isBundledWithPred() || SetMI.isBundledWithSucc()) {
    MachineInstr *BundleRoot = nullptr;
    if (SetMI.isBundledWithPred()) {
      BundleRoot = &*getBundleStart(SetMI.getIterator());
      SetMI.unbundleFromPred();
    }
    if (SetMI.isBundledWithSucc())
      SetMI.unbundleFromSucc();
    eraseEmptyBundleRoot(BundleRoot);
    Changed = true;
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: unbundled SET/LoopStart before "
                         "range check\n");
  }

  // Dead body / stale MBB operands (contract §1)
  // SET_HWLOOP with %bb.-1: body was erased after convert. Erase setup only.
  {
    unsigned Opc = SetMI.getOpcode();
    if (isHwloopSetup(Opc)) {
      if (SetMI.getNumOperands() >= 3 && SetMI.getOperand(1).isMBB() &&
          SetMI.getOperand(2).isMBB()) {
        MachineBasicBlock *H = SetMI.getOperand(1).getMBB();
        MachineBasicBlock *L = SetMI.getOperand(2).getMBB();
        if (!isLiveMBB(MF, H) || !isLiveMBB(MF, L)) {
          LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: stale Header/Latch "
                               "(%bb.-1 or foreign) — erase SET only\n");
          return eraseHardwareSetup(SetMI);
        }
      } else {
        // Malformed SET — erase rather than emit.
        return eraseHardwareSetup(SetMI);
      }
    } else if (Opc == Haydn::LoopStart) {
      MachineBasicBlock *Body = resolveBodyMBB(SetMI);
      if (!Body) {
        LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: LoopStart with no live body "
                             "— erase setup\n");
        return eraseHardwareSetup(SetMI);
      }
    }
  }

  // Setup gap (t−3): deficit-only NOPs after SET
  unsigned Following = countFollowingBundles(SetMI, TII);
  if (Following < MinSetupBundles) {
    unsigned Deficit = MinSetupBundles - Following;
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: setup gap " << Following
                      << " < " << MinSetupBundles << " — insert deficit "
                      << Deficit << " NOP bundle(s) after " << SetMI);
    MachineBasicBlock *MBB = SetMI.getParent();
    MachineBasicBlock::iterator InsertPt = nextBundleBoundary(SetMI);
    for (unsigned I = 0; I < Deficit; ++I)
      BuildMI(*MBB, InsertPt, DL, TII.get(Haydn::NOP));
    Changed = true;
  }
#ifndef NDEBUG
  assert(countFollowingBundles(SetMI, TII) >= MinSetupBundles &&
         "t-3 setup gap must be satisfied after deficit-only pad");
#endif

  // Range re-check (begin + end) — one path for SET_* and LoopStart.
  // Product (default demote ON): demote-first on unencodable/range-bad SET.
  // Debug only (demote OFF): force erase-setup — once-through body risk;
  // never a product setting.
  auto recoverRangeOrOrder = [&](const char *Why) -> bool {
    if (!EnableHaydnHwLoopDemote) {
      LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: " << Why
                        << " — erase setup only (demote disabled/debug; "
                           "not product)\n");
      return eraseHardwareSetup(SetMI);
    }
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: " << Why
                      << " — demote-first (product)\n");
    if (demoteToSoftwareLoop(SetMI, TII))
      return true;
    // Live body, soft edge not installable. Never silent single-pass body.
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

  auto rangeBad = [&](bool HardOff1Only) {
    if (StartOff < 0 || EndOff < 0)
      return true;
    // Soft: try free lifts under safety margin. Hard: demote only past uimm6.
    int64_t Off1Lim = HardOff1Only ? MaxOff1Bytes : MaxOff1BytesSafe;
    if (StartOff > Off1Lim)
      return true;
    if (EndOff > MaxOff2Bytes)
      return true;
    if (EndOff < StartOff)
      return true;
    // B4.4: MinSetupBytes = MinSetupBundles × ProductFormatDesc.Bytes.
    if (StartOff < MinSetupBytes)
      return true;
    return false;
  };

  if (rangeBad(/*HardOff1Only=*/false)) {
    // Free-only lifts; remat→SET stays glued. No peel across count unit.
    if (StartOff > MaxOff1BytesSafe)
      Changed |= tryShortenStartOffset(SetMI, TII, StartOff, EndOff);

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
  LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: startOff=" << StartOff
                    << " endOff=" << EndOff
                    << " sameMBB=" << (StartMBB == EndMBB)
                    << " followingBundles="
                    << countFollowingBundles(SetMI, TII) << "\n");

  return Changed;
}

bool HaydnFixupHwLoops::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  const auto &TII =
      *static_cast<const HaydnInstrInfo *>(MF.getSubtarget().getInstrInfo());

  bool Changed = false;
  // Collect first — inserting NOPs / demote invalidates iterators.
  // Walk instrs so SETs that PostRASched bundled are still found.
  SmallVector<MachineInstr *, 8> Sets;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB.instrs()) {
      unsigned Opc = MI.getOpcode();
      if (isHwloopSetup(Opc) || Opc == Haydn::LoopStart)
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
