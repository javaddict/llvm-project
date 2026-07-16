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
// Closed contracts (no recover-by-fatal, no monkey-patch special cases):
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
// 3. Recoverability ladder (always prefer MC-safe over "keep HW")
// a. Pad setup gap only (deficit-only t−3 NOPs after SET → BEGIN).
//    Body min-length is not product law; END >= BEGIN (inclusive) is legal.
// b. tryShortenStartOffset — order-preserving (first post-SET MI only).
// c. demoteToSoftwareLoop — AIE expands LoopDec+LoopJNZ to JNZD or
// strips empty ZOL; it never half-demotes. We must demote when
// layout forbids forward Off1/Off2 (latch before header, etc.).
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
// Debug: -haydn-enable-hwloop-demote=false force erase-setup.
// 4. Pipeline
// addPreEmit: BranchRelaxation → FixupHwLoops → BranchRelaxation again
// so Fixup growth cannot leave branches past simm12.
//
// AsmPrinter is the emit-side twin: no START/END temp symbols without a
// real body instruction to flush them.
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
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

// Product demote policy (update): default ON.
// Out-of-range / unencodable SET must not drop control and leave a single-pass
// body (Role B already removed the software back-edge). Recovery ladder:
// a) demoteToSoftwareLoop (LoopDec+LoopJNZ) when free counter GPR exists
// b) Header/Latch dead → erase-setup only is OK (body gone)
// c) live body but demote cannot install soft edge → report_fatal_error
// (never silent erase-only once-through)
// Counter pick: LivePhysRegs at SET site + loop-block mention filter; never
// invent a clobbering free AT. Flag OFF = debug force erase-setup only.
static cl::opt<bool> EnableHaydnHwLoopDemote(
    "haydn-enable-hwloop-demote", cl::Hidden, cl::init(true),
    cl::desc("Demote out-of-range / invalid ZOL to software LoopDec+LoopJNZ "
             "when a free counter GPR exists (LivePhysRegs). Default ON "
             "(product). OFF = force erase-setup only (debug; not "
             "semantics-preserving)."));
namespace {

// Aliases from HaydnHWLoopContracts.h (sole numeric source).
static constexpr unsigned MinSetupBundles = haydn::hwloop::MinSetupBundles;
static constexpr unsigned Bundle128Bytes =
    static_cast<unsigned>(haydn::hwloop::Bundle128Bytes);
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
  // Returns invalid Register if none is free (caller must erase-only).
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

  // Safe erase of a (possibly bundled) MI collected by pointer.
  static void eraseInstrSafe(MachineInstr *MI) {
    if (!MI || !MI->getParent())
      return;
    if (MI->isBundledWithPred())
      MI->unbundleFromPred();
    if (MI->isBundledWithSucc())
      MI->unbundleFromSucc();
    MI->eraseFromParent();
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
    Bundles += (Bytes + Bundle128Bytes - 1) / Bundle128Bytes;
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
  if ((Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_REG) &&
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

// Move post-SET preheader instructions back before SET until StartOff fits
// uimm6, while keeping ≥ MinSetupBundles after SET (t−3).
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

  // Greedily move post-SET preheader work before SET while keeping
  // ≥ MinSetupBundles after SET (t−3).
  //
  // CRITICAL: always move the *first* size-bearing MI after SET, never the
  // last. Repeatedly splicing the last MI in front of SET reverses relative
  // order of the preheader (SET, A, B, C → C, B, A, SET). That breaks
  // reduction chains: the final MAX32 may land first with intermediate
  // operands while later MAX32s update a different physreg; the hwloop body
  // still live-ins the early reg → wrong mx/cnt (BundleSim cb44 residual
  // host=145 sim=138 after PEI fixes).
  while (StartOff > MaxOff1BytesSafe) {
    unsigned Following = countFollowingBundles(SetMI, TII);
    if (Following <= MinSetupBundles)
      break;

    // Bundle-safe start : never construct iterators mid-bundle.
    MachineInstr *First = nullptr;
    unsigned FirstBundles = 0;
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
      First = &*I;
      FirstBundles = (Bytes + Bundle128Bytes - 1) / Bundle128Bytes;
      break;
    }
    if (!First || FirstBundles == 0)
      break;
    if (Following - FirstBundles < MinSetupBundles)
      break;

    // Move First to immediately before SET (preserves order of remaining work).
    MBB->splice(SetMI.getIterator(), MBB, First->getIterator());
    Changed = true;
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: shortened StartOff — moved MI "
                         "before SET: "
                      << *First);

    if (!computeOffsets(SetMI, TII, StartOff, EndOff, StartMBB, EndMBB))
      break;
  }

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
  if (Opc != Haydn::SET_HWLOOP && Opc != Haydn::SET_HWLOOP_REG && !IsLoopStart)
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
    if (Opc == Haydn::SET_HWLOOP_REG) {
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
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();

  auto canUsePreferAsCounter = [&]() -> bool {
    return Prefer.isPhysical() && Prefer != Haydn::R0 && Prefer != Haydn::R13 &&
           Prefer != Haydn::R15 &&
           !regClobberedNonCountdownIn(Prefer, LoopBlocks);
  };

  if ((IsLoopStart || Opc == Haydn::SET_HWLOOP_REG) && Prefer.isPhysical() &&
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
  } else if (!IsLoopStart && (Opc == Haydn::SET_HWLOOP || HasImm)) {
    // Imm form: need a free GPR + materialize.
    MachineBasicBlock::iterator Ins = SetMI.getIterator();
    CountReg = pickCounterReg(LoopBlocks, Prefer, ST, *Preheader, Ins);
    if (CountReg.isPhysical()) {
      materializeTripCount(*Preheader, Ins, DL, TII, CountReg, Prefer, Imm,
                           /*HasImm=*/true);
      InstallSoftLoop = true;
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
  stripResidualCountdown(Latch, CountReg);

  // L1: erase hardware setup (SET + PLE)
  eraseHardwareSetup(SetMI);
  // SetMI is gone; do not touch it again.

  // Re-validate live blocks after erase (should still be live).
  if (!isLiveMBB(MF, Header) || !isLiveMBB(MF, Latch) || !isLiveMBB(MF, Exit))
    return true;

  // L2: rewrite latch to software counted back-edge
  // CountReg is live at SET/preheader and into every loop block.
  {
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

  // Always the AIE JNZD pair: one dec, one branch. Residual was stripped.
  BuildMI(*Latch, Latch->end(), DL, TII.get(Haydn::LoopDec), CountReg)
      .addReg(CountReg);
  BuildMI(*Latch, Latch->end(), DL, TII.get(Haydn::LoopJNZ))
      .addReg(CountReg)
      .addMBB(Header);
  LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: demote LoopDec+LoopJNZ on "
                    << printReg(CountReg) << "\n");

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

  // Closed rule: SET/LoopStart must be a *top-level* MI. PostRASched may have
  // bundled it with neighbors; MBB::iterator / getFirstTerminator / splice
  // all assert if handed a mid-bundle instr_iterator. Unbundle before any
  // distance math or CFG edit (same contract as nextBundleBoundary).
  if (SetMI.isBundledWithPred() || SetMI.isBundledWithSucc()) {
    if (SetMI.isBundledWithPred())
      SetMI.unbundleFromPred();
    if (SetMI.isBundledWithSucc())
      SetMI.unbundleFromSucc();
    Changed = true;
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: unbundled SET/LoopStart before "
                         "range check\n");
  }

  // Dead body / stale MBB operands (contract §1)
  // SET_HWLOOP with %bb.-1: body was erased after convert. Erase setup only.
  {
    unsigned Opc = SetMI.getOpcode();
    if (Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_REG) {
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

  // Range re-check (begin + end) — one path for SET_* and LoopStart
  // Product : always demote-first on unencodable/range-bad SET.
  // Flag OFF is debug force erase-setup only (not semantics-preserving).
  auto recoverRangeOrOrder = [&](const char *Why) -> bool {
    if (!EnableHaydnHwLoopDemote) {
      LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: " << Why
                        << " — erase setup only (demote disabled/debug)\n");
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

  auto rangeBad = [&]() {
    // Unknown distance is unsafe (layout order).
    if (StartOff < 0 || EndOff < 0)
      return true;
    // Use MaxOff1BytesSafe (not hard MaxOff1Bytes) — see Off1SafetyMargin.
    if (StartOff > MaxOff1BytesSafe)
      return true;
    if (EndOff > MaxOff2Bytes)
      return true;
    // Inclusive END (BundleSim): END >= BEGIN is legal; only inverted range
    // is bad. Primary hard rule is t−3 (pad above), not body length.
    if (EndOff < StartOff)
      return true;
    // SET must land at least MinSetupBundles before BEGIN (bytes).
    if (StartOff < static_cast<int64_t>(MinSetupBundles) * Bundle128Bytes)
      return true;
    return false;
  };

  if (rangeBad()) {
    // Prefer shortening StartOff (move post-SET preheader work back before SET).
    // LoopStart usually has no post-SET payload; shorten is a no-op then demote.
    if (StartOff > MaxOff1BytesSafe)
      Changed |= tryShortenStartOffset(SetMI, TII, StartOff, EndOff);

    // Recompute after shorten.
    if (!computeOffsets(SetMI, TII, StartOff, EndOff, StartMBB, EndMBB) ||
        rangeBad()) {
      LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: range still bad after shorten "
                           "(startOff="
                        << StartOff << " endOff=" << EndOff << ")\n");
      return recoverRangeOrOrder("range still bad after shorten");
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
      if (Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_REG ||
          Opc == Haydn::LoopStart)
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
