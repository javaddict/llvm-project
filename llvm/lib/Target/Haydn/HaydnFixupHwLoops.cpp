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
// live demote failure is fatal. demote OFF is debug-only (force erase-setup).
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
// c. demoteToSoftwareLoop when hard Off1/Off2 still illegal.
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
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnFrameLowering.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPostRAScratch.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
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
// body (generic HardwareLoops + Role A expand already replaced the software
// back-edge). Recovery ladder:
// a) demoteToSoftwareLoop (final-real SUBI32+BNEZ_W) when free counter exists
// b) Header/Latch dead → erase-setup only is OK (body gone)
// c) live body but demote cannot install soft edge → report_fatal_error
// (never silent erase-only once-through)
// Counter pick: LivePhysRegs at SET site + loop-block mention filter; never
// invent a clobbering free AT.
// Flag OFF = debug-only force erase-setup (once-through risk; not product).
static cl::opt<bool> EnableHaydnHwLoopDemote(
    "haydn-enable-hwloop-demote", cl::Hidden, cl::init(true),
    cl::desc("Demote out-of-range / invalid ZOL to software SUBI32+BNEZ_W "
             "when a free counter GPR exists (LivePhysRegs). Default ON "
             "(product demote-first). OFF = force erase-setup only "
             "(debug only; once-through body risk; not product)."));
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
  // True if \p MI is a residual countdown step of \p Reg (generic
  // HardwareLoops LoopDec or leftover Prefer+=-1).
  static bool isCountdownStepOf(const MachineInstr &MI, Register Reg);
  // Erase residual countdown steps of \p Reg from \p Latch so demote's
  // SUBI32+BNEZ_W is the sole decrement. Walk instrs for BUNDLE interiors.
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
  // HaydnFinalizeBundle wraps SET/LoopStart as singleton BUNDLEs;
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
  // Generic path (PLE / terminators): drop empty BUNDLE shells only. SET
  // setup erase uses eraseSetMemberAndRecommitSiblings so coissued survivors
  // keep a transactionally recommitted product root (rebuilt operands/kills).
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

static unsigned lateMemberOpcode(unsigned LogicalOpc) {
  return haydn::bundle::lateProductMemberOpcode(LogicalOpc);
}

static void finalizeExactLateSingleton(MachineInstr &MI) {
  haydn::bundle::finalizeExactLateSingleton(MI);
}

/// After SET-member erase from a multi-member product cycle, re-exact-commit
/// surviving coissued siblings so the BUNDLE root is rebuilt (consolidated
/// defs/uses, kill flags, FormatID). Bare survivors would leave residual
/// real MIs for the late firewall and stale root operands.
static void recommitSurvivingCycleMembers(ArrayRef<MachineInstr *> Keep,
                                          const HaydnInstrInfo &TII) {
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
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: coissue survivors not one "
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

/// SET/LoopStart-member erase that preserves coissued siblings as an exact
/// product cycle. Dissolves the old root, erases only the setup member, then
/// recommits remaining children so consolidated root operands match the
/// surviving membership (plan hard-root operand recommit peer for Fixup).
static void eraseSetMemberAndRecommitSiblings(MachineInstr &SetMI,
                                              const HaydnInstrInfo &TII) {
  MachineBasicBlock *MBB = SetMI.getParent();
  if (!MBB)
    return;

  SmallVector<MachineInstr *, 3> Keep;
  if (SetMI.isBundledWithPred() || SetMI.isBundledWithSucc()) {
    MachineInstr *Root = &*getBundleStart(SetMI.getIterator());
    for (MachineBasicBlock::instr_iterator I = std::next(Root->getIterator());
         I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
      if (&*I != &SetMI)
        Keep.push_back(&*I);
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
  recommitSurvivingCycleMembers(Keep, TII);
}

/// Build a late singleton with exact-commit member opcode (dest form).
static MachineInstrBuilder
buildExactLateDef(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
                  const DebugLoc &DL, const TargetInstrInfo &TII,
                  unsigned LogicalOpc, Register Dest) {
  return BuildMI(MBB, InsertPt, DL, TII.get(lateMemberOpcode(LogicalOpc)),
                 Dest);
}

/// Build a late singleton with exact-commit member opcode (no dest).
static MachineInstrBuilder
buildExactLate(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
               const DebugLoc &DL, const TargetInstrInfo &TII,
               unsigned LogicalOpc) {
  return BuildMI(MBB, InsertPt, DL, TII.get(lateMemberOpcode(LogicalOpc)));
}

/// Emit one exact-committed singleton (dest form) and stamp Format E commit.
template <typename AddOpsFn>
static MachineInstr *
emitExactLateDef(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
                 const DebugLoc &DL, const TargetInstrInfo &TII,
                 unsigned LogicalOpc, Register Dest, AddOpsFn AddOps) {
  MachineInstrBuilder MIB =
      buildExactLateDef(MBB, InsertPt, DL, TII, LogicalOpc, Dest);
  AddOps(MIB);
  finalizeExactLateSingleton(*MIB);
  return MIB;
}

/// Emit one exact-committed singleton (no dest) and stamp Format E commit.
template <typename AddOpsFn>
static MachineInstr *
emitExactLate(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
              const DebugLoc &DL, const TargetInstrInfo &TII,
              unsigned LogicalOpc, AddOpsFn AddOps) {
  MachineInstrBuilder MIB = buildExactLate(MBB, InsertPt, DL, TII, LogicalOpc);
  AddOps(MIB);
  finalizeExactLateSingleton(*MIB);
  return MIB;
}

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
static MachineInstr &topLevelForLayout(MachineInstr &MI) {
  if (MI.isBundledWithPred() || MI.isBundledWithSucc())
    return *getBundleStart(MI.getIterator());
  return MI;
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
  // Parcel count via product EncodedBytes (generated Full Size).
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
  if (Opc != Haydn::LoopStart &&
      SetMI.getNumOperands() >= 3 && SetMI.getOperand(1).isMBB()) {
    // Any SET form (logical/wide/member) carries Header as op1.
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

  // After the SET *cycle*: next top-level MI past the whole coissued BUNDLE
  // Bundle-preserving: never hand a mid-bundle iterator to
  // MachineInstrBundleIterator (asserts isBundledWithPred).
  MachineBasicBlock *Pre = SetMI.getParent();
  MachineBasicBlock::iterator AfterSet = nextBundleBoundary(SetMI);
  StartOff = estimateMBBDistance(*MF, Pre, AfterSet, StartMBB, TII);
  EndOff = estimateMBBDistance(*MF, Pre, AfterSet, EndMBB, TII);
  // END addresses the last size-bearing body cycle (start), not the byte
  // after the latch. Walk EndMBB and pin EndOff to the last non-zero cycle.
  if (EndMBB && EndOff >= 0) {
    int64_t Cursor = EndOff;
    int64_t LastCycleStart = -1;
    for (const MachineInstr &MI : *EndMBB) {
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
  // Every demote trip-materialize MI is exact-committed before the
  // second BranchRelaxation (shared commitLateProductCycle surface).
  if (!HasImm) {
    if (SrcReg == Dst)
      return;
    // Post-RA: real MOVE32 (not generic COPY — expand-pseudos already ran).
    // Canonical MOVE32 encoding needs rs1=rs2=Src.
    emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::MOVE32, Dst,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(SrcReg).addReg(SrcReg);
                     });
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

// Erase SET/LoopStart and any PseudoLoopEnd in the (live) body. Always safe
// w.r.t. MC: no hwloop fixup remains. Body may fall through once.
// Coissued SET cycles: erase the setup member only, then transactionally
// exact-recommit surviving siblings so the BUNDLE root operands/kills match
// the remaining membership (never leave bare coissued ALUs or a stale root).
bool HaydnFixupHwLoops::eraseHardwareSetup(MachineInstr &SetMI) {
  MachineBasicBlock *Pre = SetMI.getParent();
  if (!Pre)
    return false;
  MachineFunction &MF = *Pre->getParent();
  const auto &TII = *static_cast<const HaydnInstrInfo *>(
      MF.getSubtarget().getInstrInfo());

  SmallVector<MachineInstr *, 8> PLEs;

  // Collect PseudoLoopEnd from live body (LoopStart) or live Header/Latch.
  auto collectPLE = [&](MachineBasicBlock *BB) {
    if (!isLiveMBB(MF, BB))
      return;
    for (MachineInstr &MI : BB->instrs()) {
      if (MI.isBundledWithPred())
        continue;
      if (MI.getOpcode() == Haydn::PseudoLoopEnd)
        PLEs.push_back(&MI);
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

  // SET/LoopStart first: sibling recommit needs the coissue root intact.
  eraseSetMemberAndRecommitSiblings(SetMI, TII);

  SmallPtrSet<MachineInstr *, 8> Seen;
  for (MachineInstr *MI : PLEs) {
    if (!MI || !MI->getParent() || !Seen.insert(MI).second)
      continue;
    eraseInstrSafe(MI);
  }
  return true;
}

// Demote SET_HWLOOP{,_REG} / LoopStart to a countable software loop:
// materialise the trip counter at the former SET site, erase SET (and
// PseudoLoopEnd for ZOL), restore latch Header+Exit edges with final-real
// SUBI32 + BNEZ_W (: no residual LoopDec/LoopJNZ after late commit).
// eraseHardwareSetup is SET-member-only (bundle-preserving): coissued slot
// siblings survive and are exact-recommitted (rebuilt root operands/kills).
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
  if (!TII.isHardwareLoopSetupOpcode(Opc) && !IsLoopStart)
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
    if (TII.isHardwareLoopRegTripOpcode(Opc)) {
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

  // Exit selection (ZOL demote):
  // After BranchRelaxation, a far PseudoLoopEnd back-edge is rewritten as a
  // continue trampoline (empty MBB → LUI+ADDI+JALR Header) that remains a
  // latch successor alongside the true exit. Taking the *first* non-Header
  // successor then installs:
  //   BNEZ Header ; fallthrough/B trampoline→Header
  // so both soft edges return to the header (infinite loop; gcc-c-torture
 // 20021120-1 @ -O1). Prefer:
  //  1) Unconditional branch target on the latch (B after PseudoLoopEnd) —
  //     that is the ZOL fallthrough exit.
  //  2) Non-Header successor that is not a continue-only trampoline chain
  //     that only reaches Header.
  //  3) Layout successor of the latch.
  MachineBasicBlock *Exit = nullptr;

  auto uncondBranchTarget = [&TII](const MachineInstr &TermMI)
      -> MachineBasicBlock * {
    // terminators() yields top-level MIs (BUNDLE roots included). Prefer
    // TII.getBranchDestBlock when analyzable; else shape-match B / JAL*_W R0.
    if (TermMI.isIndirectBranch() || TermMI.isReturn())
      return nullptr;
    unsigned Opc = TermMI.getOpcode();
    if (Opc == TargetOpcode::BUNDLE) {
      // First non-meta child that is a branch.
      for (const MachineInstr &C : make_range(getBundleStart(TermMI.getIterator()),
                                              getBundleEnd(TermMI.getIterator()))) {
        if (&C == &TermMI)
          continue;
        if (C.isMetaInstruction() || C.isCFIInstruction())
          continue;
        if (C.isUnconditionalBranch() && !C.isIndirectBranch()) {
          if (C.getNumOperands() > 0 && C.getOperand(0).isMBB())
            return C.getOperand(0).getMBB();
          if (C.getNumOperands() > 1 && C.getOperand(1).isMBB())
            return C.getOperand(1).getMBB();
        }
      }
      return nullptr;
    }
    if (!TermMI.isUnconditionalBranch() || TermMI.isIndirectBranch())
      return nullptr;
    // Direct uncond: B MBB, or JAL/JAL_W R0, MBB.
    if (TermMI.getNumOperands() > 0 && TermMI.getOperand(0).isMBB())
      return TermMI.getOperand(0).getMBB();
    if (TermMI.getNumOperands() > 1 && TermMI.getOperand(0).isReg() &&
        TermMI.getOperand(0).getReg() == Haydn::R0 &&
        TermMI.getOperand(1).isMBB())
      return TermMI.getOperand(1).getMBB();
    (void)TII;
    return nullptr;
  };

  // (1) Latch unconditional branch target (skip PseudoLoopEnd / cond).
  for (const MachineInstr &Term : Latch->terminators()) {
    if (Term.getOpcode() == Haydn::PseudoLoopEnd)
      continue;
    if (MachineBasicBlock *T = uncondBranchTarget(Term)) {
      if (T != Header && isLiveMBB(MF, T))
        Exit = T;
    }
  }

  // Continue-trampoline: empty / LUI+ADDI+JALR chain whose only reachable
  // latch-side destination is Header (BranchRelaxation artifact).
  auto reachesOnlyHeader = [&](MachineBasicBlock *S) -> bool {
    SmallPtrSet<const MachineBasicBlock *, 8> Visited;
    MachineBasicBlock *Cur = S;
    for (int Depth = 0; Cur && Depth < 8; ++Depth) {
      if (!Visited.insert(Cur).second)
        return Cur == Header;
      if (Cur == Header)
        return true;
      if (Cur->succ_size() != 1)
        return false;
      // Allow empty blocks and pure far-jump materialization (LUI/ADDI/JALR).
      for (const MachineInstr &MI : Cur->instrs()) {
        if (MI.isMetaInstruction() || MI.isCFIInstruction() || MI.isKill() ||
            MI.isImplicitDef())
          continue;
        if (MI.isBundle())
          continue;
        unsigned Opc = MI.getOpcode();
        const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(Opc);
        if (Log == Haydn::NOP || Log == Haydn::LUI || Log == Haydn::ADDI32_W ||
            Log == Haydn::JALR_W || Log == Haydn::JALR || Log == Haydn::B)
          continue;
        // Real work — not a trampoline.
        return false;
      }
      Cur = *Cur->succ_begin();
    }
    return false;
  };

  // (2) Successor scan, skipping Header and continue-trampolines.
  if (!Exit) {
    if (Latch->succ_size() == 1) {
      MachineBasicBlock *S = *Latch->succ_begin();
      if (S != Header && isLiveMBB(MF, S) && !reachesOnlyHeader(S))
        Exit = S;
    } else {
      for (MachineBasicBlock *S : Latch->successors()) {
        if (S != Header && isLiveMBB(MF, S) && !reachesOnlyHeader(S)) {
          Exit = S;
          break;
        }
      }
    }
  }

  // (3) Layout fallthrough of latch (also skip trampolines).
  if (!Exit) {
    MachineFunction::iterator LatchIt = Latch->getIterator();
    MachineFunction::iterator NextIt = std::next(LatchIt);
    if (NextIt != MF.end() && isLiveMBB(MF, &*NextIt) &&
        &*NextIt != Header && !reachesOnlyHeader(&*NextIt))
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

  if ((IsLoopStart || TII.isHardwareLoopRegTripOpcode(Opc)) &&
      Prefer.isPhysical() && Prefer != Haydn::R0) {
    // Trip reg at LoopStart / SET_HWLOOP_REG.
    // Prefer is correct only if the body does not redefine it as a
    // non-countdown (e.g. S_LW_POST dest = trip). Residual Prefer+=-1 is OK
    // we strip it below and install a single SUBI32 dec.
    // Bundle-preserving: materialize before the SET cycle root, not mid-bundle.
    MachineBasicBlock::iterator Ins =
        topLevelForLayout(SetMI).getIterator();
    if (canUsePreferAsCounter()) {
      CountReg = Prefer;
      InstallSoftLoop = true;
    } else {
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
  } else if (!IsLoopStart &&
             (TII.isHardwareLoopImmTripOpcode(Opc) || HasImm)) {
    // Imm form: need a free GPR + materialize.
    MachineBasicBlock::iterator Ins =
        topLevelForLayout(SetMI).getIterator();
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
      MachineBasicBlock::iterator Ins =
          topLevelForLayout(SetMI).getIterator();
      Register FrameReg;
      int64_t Off =
          TFL->getFrameIndexReference(MF, StackCounterFI, FrameReg).getFixed();
      // FI ref is bytes; ST32/LD32 take word element indices (imm<<2).
      assert((Off % 4) == 0 && "stack-counter FI must be word-aligned");
      const int64_t Elem = Off / 4;
      if (HasImm) {
        // Materialize imm into a free/scratch temp, then store to FI.
  // ST/address glue exact-committed (trip mat already is).
        withPostRAScratch(
            *Preheader, Ins, DL, TII, ST, /*PreferNotR12=*/true,
            [&](Register Scr) {
              materializeTripCount(*Preheader, Ins, DL, TII, Scr, Prefer, Imm,
                                   /*HasImm=*/true);
              if (isInt<6>(Elem)) {
                emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                              [&](MachineInstrBuilder MIB) {
                                MIB.addReg(Scr, getKillRegState(true))
                                    .addReg(FrameReg)
                                    .addImm(Elem);
                              });
              } else {
                // Rare large FI: use R0 as address temp (xor-zero after).
                // ADDI takes byte offset; ST32 at [R0+0].
                emitExactLateDef(*Preheader, Ins, DL, TII, Haydn::ADDI32_W,
                                 Haydn::R0, [&](MachineInstrBuilder MIB) {
                                   MIB.addReg(FrameReg).addImm(Off);
                                 });
                emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                              [&](MachineInstrBuilder MIB) {
                                MIB.addReg(Scr, getKillRegState(true))
                                    .addReg(Haydn::R0)
                                    .addImm(0);
                              });
                emitExactLateDef(*Preheader, Ins, DL, TII, Haydn::XOR32,
                                 Haydn::R0, [&](MachineInstrBuilder MIB) {
                                   MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                                 });
              }
            },
            /*Exclude=*/Prefer.isPhysical() ? ArrayRef<Register>{Prefer}
                                            : ArrayRef<Register>{});
      } else {
        // Prefer holds trip at SET; store it to FI before erase.
        if (isInt<6>(Elem)) {
          emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                        [&](MachineInstrBuilder MIB) {
                          MIB.addReg(Prefer).addReg(FrameReg).addImm(Elem);
                        });
        } else {
          emitExactLateDef(*Preheader, Ins, DL, TII, Haydn::ADDI32_W, Haydn::R0,
                           [&](MachineInstrBuilder MIB) {
                             MIB.addReg(FrameReg).addImm(Off);
                           });
          emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                        [&](MachineInstrBuilder MIB) {
                          MIB.addReg(Prefer).addReg(Haydn::R0).addImm(0);
                        });
          emitExactLateDef(*Preheader, Ins, DL, TII, Haydn::XOR32, Haydn::R0,
                           [&](MachineInstrBuilder MIB) {
                             MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                           });
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
  // latch, while Latch is still intact. Generic HardwareLoops may leave
  // LoopDec / Prefer+=-1; Role A expand does not consume them.
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

  // Drop existing top-level terminators, then final-real SUBI32+BNEZ_W +
 // optional B Exit. : no residual LoopDec/LoopJNZ after late commit.
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
    assert((Off % 4) == 0 && "stack-counter FI must be word-aligned");
    const int64_t Elem = Off / 4;
    MachineBasicBlock::iterator LatchEnd = Latch->end();
    // Stack-counter path builds several real MIs; exact-commit each singleton
    // so second BR / Verify see committed FormatID cycles (no residual bare
    // SUBI32/BNEZ_W for the late firewall to invent).
    withPostRAScratch(
        *Latch, LatchEnd, DL, TII, ST, /*PreferNotR12=*/true,
        [&](Register Scr) {
          if (isInt<6>(Elem)) {
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::LD32, Scr,
                             [&](MachineInstrBuilder MIB) {
                               MIB.addReg(FrameReg).addImm(Elem);
                             });
          } else {
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::ADDI32_W,
                             Haydn::R0, [&](MachineInstrBuilder MIB) {
                               MIB.addReg(FrameReg).addImm(Off);
                             });
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::LD32, Scr,
                             [&](MachineInstrBuilder MIB) {
                               MIB.addReg(Haydn::R0).addImm(0);
                             });
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::XOR32, Haydn::R0,
                             [&](MachineInstrBuilder MIB) {
                               MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                             });
          }
          // Final-real countdown: counter -= 1; branch if nonzero.
          emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::SUBI32, Scr,
                           [&](MachineInstrBuilder MIB) {
                             MIB.addReg(Scr).addImm(1);
                           });
          if (isInt<6>(Elem)) {
            emitExactLate(*Latch, LatchEnd, DL, TII, Haydn::ST32,
                          [&](MachineInstrBuilder MIB) {
                            MIB.addReg(Scr).addReg(FrameReg).addImm(Elem);
                          });
          } else {
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::ADDI32_W,
                             Haydn::R0, [&](MachineInstrBuilder MIB) {
                               MIB.addReg(FrameReg).addImm(Off);
                             });
            emitExactLate(*Latch, LatchEnd, DL, TII, Haydn::ST32,
                          [&](MachineInstrBuilder MIB) {
                            MIB.addReg(Scr).addReg(Haydn::R0).addImm(0);
                          });
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::XOR32, Haydn::R0,
                             [&](MachineInstrBuilder MIB) {
                               MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                             });
          }
          emitExactLate(*Latch, LatchEnd, DL, TII, Haydn::BNEZ_W,
                        [&](MachineInstrBuilder MIB) {
                          MIB.addReg(Scr, getKillRegState(true)).addMBB(Header);
                        });
        });
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: demote stack-counter exact-commit "
                         "SUBI32+BNEZ_W FI#"
                      << StackCounterFI << "\n");
  } else {
    // Final-real soft edge: SUBI32 count,count,1 + BNEZ_W count, Header.
    // Residual countdown was stripped above; never double-dec.
  // Exact-commit each edge as a product singleton before second BR.
    emitExactLateDef(*Latch, Latch->end(), DL, TII, Haydn::SUBI32, CountReg,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(CountReg).addImm(1);
                     });
    emitExactLate(*Latch, Latch->end(), DL, TII, Haydn::BNEZ_W,
                  [&](MachineInstrBuilder MIB) {
                    MIB.addReg(CountReg).addMBB(Header);
                  });
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: demote exact-commit SUBI32+BNEZ_W on "
                      << printReg(CountReg) << "\n");
  }

  MachineFunction::iterator LatchIt = Latch->getIterator();
  MachineFunction::iterator NextIt = std::next(LatchIt);
  bool ExitIsLayoutFallthrough =
      (NextIt != MF.end()) && (&*NextIt == Exit);
  if (!ExitIsLayoutFallthrough && Exit != Header) {
  // Do not leave a bare B for late Finalize — exact-commit the
    // unconditional exit edge so second BR charges committed EncodedBytes.
    // B has no PlacementAlternatives (wrap-only Format E singleton commit).
    emitExactLate(*Latch, Latch->end(), DL, TII, Haydn::B,
                  [&](MachineInstrBuilder MIB) { MIB.addMBB(Exit); });
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

  // Bundle-preserving Fixup. Never unconditional unbundle of a legal
  // coissued SET cycle. Layout helpers (nextBundleBoundary, getCountUnitHead,
  // topLevelForLayout) walk around BUNDLE interiors; eraseHardwareSetup
  // removes only the SET member via eraseInstrSafe.

  // Dead body / stale MBB operands (contract §1)
  // SET_HWLOOP with %bb.-1: body was erased after convert. Erase setup only.
  // Product selector domain is {0,1}: out-of-domain sel stays unavailable and
  // demotes/erases fail-closed (never invent extra CSR/selector identities).
  {
    unsigned Opc = SetMI.getOpcode();
    if (TII.isHardwareLoopSetupOpcode(Opc) && Opc != Haydn::LoopStart) {
      if (SetMI.getNumOperands() < 4 || !SetMI.getOperand(0).isImm() ||
          !SetMI.getOperand(1).isMBB() || !SetMI.getOperand(2).isMBB()) {
        // Malformed SET — erase rather than emit.
        return eraseHardwareSetup(SetMI);
      }
      const int64_t Sel = SetMI.getOperand(0).getImm();
      if (!haydn::hwloop::isProductSelector(Sel)) {
        LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: out-of-domain selector "
                          << Sel << " — demote-first / erase fail-closed\n");
        if (EnableHaydnHwLoopDemote) {
          if (demoteToSoftwareLoop(SetMI, TII))
            return true;
          report_fatal_error(
              "HaydnFixupHwLoops: unsupported SET_HWLOOP selector cannot "
              "demote to software loop; refusing erase-only once-through",
              /*gen_crash_diag=*/false);
        }
        return eraseHardwareSetup(SetMI);
      }
      MachineBasicBlock *H = SetMI.getOperand(1).getMBB();
      MachineBasicBlock *L = SetMI.getOperand(2).getMBB();
      if (!isLiveMBB(MF, H) || !isLiveMBB(MF, L)) {
        LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: stale Header/Latch "
                             "(%bb.-1 or foreign) — erase SET only\n");
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
    // Imm trip COUNT must be >= MinCount when statically known.
    if (TII.isHardwareLoopImmTripOpcode(SetMI.getOpcode()) &&
        SetMI.getNumOperands() >= 4 && SetMI.getOperand(3).isImm() &&
        !haydn::hwloop::countMeetsMinLaw(SetMI.getOperand(3).getImm()))
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
    using haydn::hwloop::MaxSingleBranchGrowthBytes;

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
