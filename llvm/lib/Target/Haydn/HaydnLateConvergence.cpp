//===-- HaydnLateConvergence.cpp - bounded late repair loop ------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// W68.3R bounded convergence driver. See HaydnLateConvergence.h for the
// contract loop and the termination argument. Implementation notes:
//
// * Every mutating step instantiates a FRESH pass object — the two common
//   passes (BranchRelaxationLegacy, PostMachineSchedulerLegacy) are
//   anonymous-namespace classes reachable only through the pass registry;
//   Pass::createPass(ID) is the same mechanism TargetPassConfig::addPass
//   uses, so the common implementations are reused without editing them and
//   without any second scheduler. The two Haydn passes are created through
//   their factories. All four are deleted at end of iteration: no state
//   crosses iterations (padding is regenerated, never accumulated).
//
// * The inner PostMachineSchedulerLegacy resolves MLI/MDT/AA/
//   TargetPassConfig via getAnalysis<>. The driver lends it its own
//   resolver while on the stack (AnalysisResolver is a Pass member with a
//   public setter/getter), and re-derives fresh MDT/MLI each iteration
//   because BranchRelaxation may have split blocks since the last one.
//   BranchRelaxationLegacy and the Haydn passes need no analyses when
//   invoked through runOnMachineFunction directly.
//
// * PostMachineSchedulerImpl::run returns true unconditionally, so its
//   return value is not a change signal; the driver's census is.
//
// * Per-prefix budgets: source/target pairs (branch dests, HWLoop START/END)
//   are measured with BranchRelaxation BasicBlockInfo Offset/Size/postOffset
//   (llvm/lib/CodeGen/BranchRelaxation.cpp) plus ARM UnknownPadding
//   (ARMBasicBlockInfo.h) and Haydn parcel-rounded MBB gaps
//   (HaydnFixupHwLoops padLayoutBytesForMBBAlign). The census includes
//   those prefix charges; whole-function byte totals are not a proof.
//
// * Monotone law (checked every iteration; violation is a hard diagnostic):
//     - a branch site that reached the Indirect level (JALR_W long form)
//       never returns to a PC-relative form, and the Indirect-site census
//       never shrinks;
//     - the hardware-loop setup census never grows (demotion erases the
//       SET; no late pass creates one).
//   Short->Relaxed shape growth is deliberately NOT a census dimension:
//   the relaxed two-branch shape is byte-identical in kind to an ordinary
//   two-target conditional lowering (insertBranch emits cond+B), so shape
//   churn under S2 repacking cannot be distinguished from promotion — the
//   per-MBB byte census carries those iterations instead. The JALR form is
//   the only irreversible promotion the relaxer can produce
//   (fixupUnconditionalBranch -> insertIndirectBranch) and nothing in this
//   loop ever shortens one.
//
//===----------------------------------------------------------------------===//

#include "HaydnLateConvergence.h"
#include "Haydn.h"
#include "HaydnBundlePlan.h"
#include "HaydnFixupHwLoops.h"
#include "HaydnInstrInfo.h"
#include "HaydnLatencyStalls.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "haydn-late-convergence"

namespace {

//===--------------------------------------------------------------------===//
// Inventory census — the change-detection state of one loop iteration.
//===--------------------------------------------------------------------===//

// Peer: BranchRelaxation::BasicBlockInfo
// (llvm/lib/CodeGen/BranchRelaxation.cpp Offset/Size/postOffset).
// Overlay: HexagonBranchRelaxation.cpp computeOffset alignTo when entering
// an aligned MBB; HaydnFixupHwLoops.cpp padLayoutBytesForMBBAlign
// parcel-rounds the gap so HWLoop Off1/Off2 consume the same pad.
struct LayoutBlockInfo {
  unsigned Offset = 0;
  unsigned Size = 0;
  Align Alignment = Align(1);

  unsigned postOffset(const MachineBasicBlock &Next) const {
    const unsigned PO = Offset + Size;
    const Align NextAlign = Next.getAlignment();
    const Align ParentAlign = Next.getParent()->getAlignment();
    unsigned Aligned = alignTo(PO, NextAlign);
    if (NextAlign > ParentAlign)
      Aligned += NextAlign.value() - ParentAlign.value();
    if (Aligned <= PO)
      return PO;
    const unsigned Gap = Aligned - PO;
    return PO + static_cast<unsigned>(haydn::bundle::productBundlesToBytes(
                    haydn::bundle::ceilProductParcels(Gap)));
  }
};

// Peer: ARMBasicBlockInfo.h UnknownPadding with KnownBits=0 — worst-case
// pad before an aligned block. Range budgets reserve this, not the pad
// this particular compilation happened to emit.
static unsigned unknownPadding(Align A) {
  const unsigned KnownBits = 0;
  if (KnownBits < Log2(A))
    return A.value() - (1u << KnownBits);
  return 0;
}

using PrefixKey = std::pair<unsigned, unsigned>;

struct PrefixPairRecord {
  unsigned Src = 0;
  unsigned Dst = 0;
  uint64_t EncodedBytes = 0;
  uint64_t AlignPad = 0;
  Align MaxAlign = Align(1);

  uint64_t charge() const { return EncodedBytes + AlignPad; }
};

static unsigned computeBlockSize(const MachineBasicBlock &MBB,
                                 const TargetInstrInfo &TII) {
  unsigned Bytes = 0;
  for (const MachineInstr &MI : MBB)
    Bytes += TII.getInstSizeInBytes(MI);
  return Bytes;
}

static void scanLayout(const MachineFunction &MF, const TargetInstrInfo &TII,
                       SmallVectorImpl<LayoutBlockInfo> &Info) {
  Info.clear();
  Info.resize(MF.getNumBlockIDs());
  for (const MachineBasicBlock &MBB : MF) {
    LayoutBlockInfo &BBI = Info[MBB.getNumber()];
    BBI.Size = computeBlockSize(MBB, TII);
    BBI.Alignment = MBB.getAlignment();
  }
  bool First = true;
  unsigned PrevNum = 0;
  for (const MachineBasicBlock &MBB : MF) {
    const unsigned Num = MBB.getNumber();
    if (First) {
      Info[Num].Offset = 0;
      First = false;
    } else {
      Info[Num].Offset = Info[PrevNum].postOffset(MBB);
    }
    PrevNum = Num;
  }
}

static PrefixPairRecord measurePrefix(const MachineFunction &MF,
                                      ArrayRef<LayoutBlockInfo> Info,
                                      unsigned Src, unsigned Dst) {
  PrefixPairRecord P;
  P.Src = Src;
  P.Dst = Dst;
  if (Src == Dst) {
    if (Src < Info.size()) {
      P.EncodedBytes = Info[Src].Size;
      P.MaxAlign = Info[Src].Alignment;
      P.AlignPad = unknownPadding(P.MaxAlign);
    }
    return P;
  }

  bool InSpan = false;
  uint64_t Encoded = 0;
  uint64_t Pad = 0;
  Align MaxA = Align(1);
  unsigned PrevNum = ~0u;
  for (const MachineBasicBlock &MBB : MF) {
    const unsigned N = MBB.getNumber();
    const bool IsEndPoint = (N == Src || N == Dst);
    if (!InSpan) {
      if (!IsEndPoint)
        continue;
      InSpan = true;
      PrevNum = N;
      continue;
    }
    const unsigned Expected = Info[PrevNum].Offset + Info[PrevNum].Size;
    const unsigned Actual = Info[N].Offset;
    if (Actual > Expected)
      Pad += Actual - Expected;
    if (MBB.getAlignment() > MaxA)
      MaxA = MBB.getAlignment();
    if (IsEndPoint)
      break;
    Encoded += Info[N].Size;
    PrevNum = N;
  }
  P.EncodedBytes = Encoded;
  P.MaxAlign = MaxA;
  P.AlignPad = std::max<uint64_t>(Pad, unknownPadding(MaxA));
  return P;
}

static void collectRangePairs(const MachineFunction &MF,
                              const HaydnInstrInfo &HII,
                              SmallVectorImpl<PrefixKey> &Pairs) {
  DenseSet<PrefixKey> Seen;
  auto Add = [&](unsigned S, unsigned D) {
    PrefixKey K{S, D};
    if (Seen.insert(K).second)
      Pairs.push_back(K);
  };
  for (const MachineBasicBlock &MBB : MF) {
    const unsigned SN = MBB.getNumber();
    for (const MachineInstr &MI : MBB.instrs()) {
      if (MI.isDebugInstr() || MI.isMetaInstruction() || MI.isImplicitDef() ||
          MI.isKill())
        continue;
      const bool RangeSite = HII.isHardwareLoopSetupInstr(MI) || MI.isBranch() ||
                             MI.isCall();
      if (!RangeSite)
        continue;
      for (const MachineOperand &MO : MI.operands())
        if (MO.isMBB() && MO.getMBB())
          Add(SN, MO.getMBB()->getNumber());
    }
  }
}

struct ConvergenceSnapshot {
  /// Source MBB numbers holding a JALR/JALR_W long form. Monotone law:
  /// this set only grows (irreversible promotion).
  DenseSet<unsigned> IndirectSites;

  /// Hardware-loop setup instructions (SET_HWLOOP_* forms / LoopStart).
  /// Monotone law: only decreases (demotion erases the SET).
  unsigned HwLoopSetups = 0;

  /// Per-MBB encoded size via TII->getInstSizeInBytes.
  DenseMap<unsigned, uint64_t> MBBBytes;

  /// Source/target-pair prefix charge (encoded + alignment pad) for every
  /// pair range decisions consume. A local prefix change can invalidate a
  /// branch or retained HWLoop while the function total shrinks.
  DenseMap<PrefixKey, uint64_t> PrefixCharge;

  bool operator==(const ConvergenceSnapshot &R) const {
    return IndirectSites == R.IndirectSites &&
           HwLoopSetups == R.HwLoopSetups && MBBBytes == R.MBBBytes &&
           PrefixCharge == R.PrefixCharge;
  }
};

ConvergenceSnapshot takeSnapshot(MachineFunction &MF,
                                 const TargetInstrInfo &TII) {
  ConvergenceSnapshot S;
  const auto &HII = *static_cast<const HaydnInstrInfo *>(&TII);

  SmallVector<LayoutBlockInfo, 16> Info;
  scanLayout(MF, TII, Info);

  SmallVector<PrefixKey, 8> Pairs;
  collectRangePairs(MF, HII, Pairs);
  for (const PrefixKey &K : Pairs) {
    PrefixPairRecord Rec = measurePrefix(MF, Info, K.first, K.second);
    S.PrefixCharge[K] = Rec.charge();
  }

  for (MachineBasicBlock &MBB : MF) {
    const unsigned N = MBB.getNumber();
    S.MBBBytes[N] = N < Info.size() ? Info[N].Size : 0;

    for (MachineInstr &MI : MBB.instrs()) {
      if (HII.isHardwareLoopSetupInstr(MI))
        ++S.HwLoopSetups;
      unsigned Opc = MI.getOpcode();
      // The irreversible promotion is exactly the register-indirect long
      // form: fixupUnconditionalBranch -> insertIndirectBranch emits
      // LUI+ADDI32_W+JALR_W and nothing in this loop ever shortens one.
      // PC-relative sites (cond/B) may freely change shape between short
      // and relaxed two-branch forms — that churn is layout-visible in
      // MBBBytes/PrefixCharge, not a promotion regression.
      if (Opc == Haydn::JALR || Opc == Haydn::JALR_W)
        S.IndirectSites.insert(MBB.getNumber());
    }
  }
  return S;
}

/// Promotion, demotion, or MBB identity change. Pure per-MBB / prefix-byte
/// churn is S2 packing, not new inventory — the next iteration still runs
/// stalls + HWLoop revalidation + BranchRelaxation LAST, but does not
/// reschedule. Rerunning S2 on already-committed roots is not a fixed
/// point (spill/call and far-pad regions repack to a different cycle
/// count), which exhausted the +2 slack. Contract: rerun S2 after a
/// promotion/demotion that changes inventory; BR stays last.
bool inventoryChanged(const ConvergenceSnapshot &Before,
                      const ConvergenceSnapshot &After) {
  if (Before.HwLoopSetups != After.HwLoopSetups)
    return true;
  if (Before.IndirectSites != After.IndirectSites)
    return true;
  if (Before.MBBBytes.size() != After.MBBBytes.size())
    return true;
  for (const auto &KV : Before.MBBBytes)
    if (!After.MBBBytes.count(KV.first))
      return true;
  return false;
}

/// Verify the monotone law between the snapshots bracketing one mutating
/// iteration. Returns a diagnostic string on violation.
std::string checkMonotonicity(const ConvergenceSnapshot &Before,
                              const ConvergenceSnapshot &After) {
  for (const unsigned Site : Before.IndirectSites)
    if (!After.IndirectSites.count(Site))
      return ("indirect long-form branch site (bb." + Twine(Site) +
              ") regressed to a PC-relative form")
          .str();
  if (After.HwLoopSetups > Before.HwLoopSetups)
    return ("hardware-loop setup count grew from " +
            Twine(Before.HwLoopSetups) + " to " + Twine(After.HwLoopSetups))
        .str();
  return {};
}

/// Instantiate a registered legacy pass by ID (the same mechanism
/// TargetPassConfig::addPass(AnalysisID) uses). Every ID used here
/// resolves to a MachineFunctionPass; the downcast mirrors what the pass
/// manager itself does when scheduling it. Caller owns the result.
MachineFunctionPass *createFreshMachinePass(char &PassID) {
  Pass *P = Pass::createPass(&PassID);
  return P ? static_cast<MachineFunctionPass *>(P) : nullptr;
}

/// Same for the Haydn factory passes (declared FunctionPass*; all are
/// MachineFunctionPasses).
MachineFunctionPass *asMachinePass(FunctionPass *P) {
  return static_cast<MachineFunctionPass *>(P);
}

/// Invoke a fresh inner pass through its public FunctionPass entry.
///
/// Resolver protocol: Pass::~Pass DELETES its resolver, and setResolver
/// refuses a second set — so the driver's own resolver must never be
/// loaned out directly. Each inner pass receives a FRESH AnalysisResolver
/// referencing the driver's PMDataManager, seeded with pairs for the
/// closed set of analyses the inner passes resolve via the no-arg
/// getAnalysis<> path (which searches only the local pairs vector):
/// MMI (runOnFunction entry), MLI/MDT/AA/TargetPassConfig (scheduler).
/// Each Impl is looked up through the driver's resolver, whose pairs the
/// PM populated when scheduling the driver for exactly its declared
/// required set (the same five). The implementing passes are resident in
/// the same PM for the whole call, and the inner pass's destructor
/// deletes only its own resolver.
bool runInnerPass(FunctionPass &P, MachineFunctionPass &DriverPass,
                  MachineFunction &MF) {
  AnalysisResolver *DriverAR = DriverPass.getResolver();
  assert(DriverAR && "driver pass is not resident in a PassManager");
  auto *InnerAR = new AnalysisResolver(DriverAR->getPMDataManager());
  const AnalysisID Needed[] = {
      &MachineModuleInfoWrapperPass::ID, &MachineLoopInfoWrapperPass::ID,
      &MachineDominatorTreeWrapperPass::ID, &AAResultsWrapperPass::ID,
      &TargetPassConfig::ID};
  for (const AnalysisID ID : Needed)
    if (Pass *Impl = DriverAR->findImplPass(ID))
      InnerAR->addAnalysisImplsPair(ID, Impl);
  P.setResolver(InnerAR);
  return P.runOnFunction(MF.getFunction());
}

// Per-prefix budget record (pipeline.md "S1 issue depths and byte/
// alignment budgets" lifetime row). Dies with the driver.
struct PrefixBudgetRecord {
  SmallVector<PrefixPairRecord, 8> Pairs;

  bool noGrowth(const PrefixBudgetRecord &Final) const {
    DenseMap<PrefixKey, uint64_t> FinalCharge;
    for (const PrefixPairRecord &P : Final.Pairs)
      FinalCharge[{P.Src, P.Dst}] = P.charge();
    for (const PrefixPairRecord &P : Pairs) {
      auto It = FinalCharge.find({P.Src, P.Dst});
      if (It == FinalCharge.end())
        continue;
      if (It->second > P.charge())
        return false;
    }
    return true;
  }
};

static PrefixBudgetRecord capturePrefixBudget(MachineFunction &MF,
                                              const TargetInstrInfo &TII) {
  PrefixBudgetRecord Budget;
  const auto &HII = *static_cast<const HaydnInstrInfo *>(&TII);
  SmallVector<LayoutBlockInfo, 16> Info;
  scanLayout(MF, TII, Info);
  SmallVector<PrefixKey, 8> Pairs;
  collectRangePairs(MF, HII, Pairs);
  Budget.Pairs.reserve(Pairs.size());
  for (const PrefixKey &K : Pairs)
    Budget.Pairs.push_back(measurePrefix(MF, Info, K.first, K.second));
  return Budget;
}

static void dumpPrefixBudget(StringRef Tag, const MachineFunction &MF,
                             const PrefixBudgetRecord &Budget) {
  (void)Tag;
  (void)MF;
  (void)Budget;
  LLVM_DEBUG({
    for (const PrefixPairRecord &P : Budget.Pairs)
      dbgs() << "HaydnLateConvergence: " << Tag << ' ' << MF.getName()
             << " prefix bb." << P.Src << "->bb." << P.Dst
             << " encoded=" << P.EncodedBytes << " pad=" << P.AlignPad
             << " maxalign=" << P.MaxAlign.value()
             << " budget=" << P.charge() << '\n';
  });
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// The bounded loop.
//===----------------------------------------------------------------------===//

bool llvm::runHaydnLateConvergence(MachineFunction &MF,
                                   MachineFunctionPass &DriverPass) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const auto &HII = *static_cast<const HaydnInstrInfo *>(&TII);

  // Iteration bound (contract): every conditional branch site accounts for
  // its finite promotion sequence and every hardware-loop setup for its one
  // demotion; +2 covers the initial pass and the final no-change
  // confirmation. S2 reruns only after promotion/demotion/MBB-identity
  // change; pure packing/prefix-byte churn is closed by stalls + HWLoop
  // revalidation + BranchRelaxation LAST without another S2 (a second S2
  // on committed roots is not a fixed point). Exhaustion without a census
  // fixed point is a hard diagnostic.
  unsigned NumCondBranches = 0, NumHwLoopSetups = 0;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB.instrs()) {
      if (MI.isConditionalBranch())
        ++NumCondBranches;
      if (HII.isHardwareLoopSetupInstr(MI))
        ++NumHwLoopSetups;
    }
  const unsigned MaxIterations = NumCondBranches + NumHwLoopSetups + 2;

  LLVM_DEBUG(dbgs() << "HaydnLateConvergence: " << MF.getName()
                    << " bound=" << MaxIterations << " (cond="
                    << NumCondBranches << " hwloop=" << NumHwLoopSetups
                    << ")\n");

  // In-place refresh handles for the pipeline analysis wrapper passes the
  // inner scheduler consumes through the borrowed resolver. BranchRelaxation
  // may split blocks mid-loop; the wrappers are recalculated in place each
  // iteration so neither the inner pass nor any downstream consumer sees a
  // pre-split CFG (the W68.2 stale-state law: never consume analyses keyed
  // to MBBs that no longer exist). AA is IR-level (function-locked), not
  // MBB-keyed, so it needs no refresh.
  auto &MDTWrapper =
      DriverPass.getAnalysis<MachineDominatorTreeWrapperPass>();
  auto &MLIWrapper = DriverPass.getAnalysis<MachineLoopInfoWrapperPass>();

  // Per-prefix budget record: entry snapshot of every source/target pair
  // the preceding range decisions consumed. Dies with the driver.
  PrefixBudgetRecord EntryBudget = capturePrefixBudget(MF, TII);
  dumpPrefixBudget("entry", MF, EntryBudget);

  ConvergenceSnapshot Before = takeSnapshot(MF, TII);
  bool AnyChanged = false;
  bool RunS2 = true;

  for (unsigned Iter = 0;; ++Iter) {
    if (Iter >= MaxIterations)
      report_fatal_error(
          "HaydnLateConvergence: bounded repair loop exhausted " +
              Twine(MaxIterations) +
              " iterations without a fixed point (function " + MF.getName() +
              "); non-monotone or oscillating late layout mutation",
          /*gen_crash_diag=*/false);

    // Refresh the CFG analyses from the current (possibly split) CFG.
    MDTWrapper.getDomTree().recalculate(MF);
    MLIWrapper.getLI().calculate(MDTWrapper.getDomTree());

    // 1. S2 on current inventory: a fresh PostMachineSchedulerLegacy.
    //    PostMachineSchedulerImpl::run returns true unconditionally, so the
    //    census (not the return value) decides relevance; conservatively
    //    mark changed (an in-MBB reorder is invisible to the byte census).
    //    The inner pass honors optnone itself; the driver skipped it first.
    //    Rerun S2 only after promotion/demotion/MBB-identity change;
    //    otherwise BR last closes the packing that S2 already chose.
    if (RunS2) {
      MachineFunctionPass *S2 = createFreshMachinePass(PostMachineSchedulerID);
      if (!S2)
        report_fatal_error(
            "HaydnLateConvergence: PostMachineSchedulerID not registered",
            /*gen_crash_diag=*/false);
      runInnerPass(*S2, DriverPass, MF);
      delete S2;
      AnyChanged = true;
    } else {
      LLVM_DEBUG(dbgs() << "HaydnLateConvergence: skip S2 (no inventory "
                           "change since last S2)\n");
    }

    // 2. Regenerate stalls/alignment (exposed-pipeline correctness net).
    if (MachineFunctionPass *Stalls =
            asMachinePass(createHaydnLatencyStallsPass())) {
      AnyChanged |= runInnerPass(*Stalls, DriverPass, MF);
      delete Stalls;
    }

    // 3. Validate or demote HWLoops against the post-S2 byte layout.
    //    fixupOne recomputes Off1/Off2 windows from CURRENT layout on every
    //    invocation, so re-invocation IS the post-S2 revalidation of
    //    retained loops (W68.3R exit item).
    if (MachineFunctionPass *Fixup =
            asMachinePass(createHaydnFixupHwLoopsPass())) {
      AnyChanged |= runInnerPass(*Fixup, DriverPass, MF);
      delete Fixup;
    }

    // 4. BranchRelaxation LAST in the mutating iteration (contract).
    if (MachineFunctionPass *BR =
            createFreshMachinePass(BranchRelaxationPassID)) {
      AnyChanged |= runInnerPass(*BR, DriverPass, MF);
      delete BR;
    } else {
      report_fatal_error(
          "HaydnLateConvergence: BranchRelaxationPassID not registered",
          /*gen_crash_diag=*/false);
    }

    // 5. Change detection + monotone law.
    ConvergenceSnapshot After = takeSnapshot(MF, TII);
    std::string Violation = checkMonotonicity(Before, After);
    if (!Violation.empty())
      report_fatal_error("HaydnLateConvergence: non-monotone mutation: " +
                             Twine(Violation),
                         /*gen_crash_diag=*/false);

    if (After == Before) {
      LLVM_DEBUG(dbgs() << "HaydnLateConvergence: fixed point after "
                        << Iter + 1 << " iteration(s)\n");
      break;
    }
    RunS2 = inventoryChanged(Before, After);
    Before = std::move(After);
  }

  // Refresh CFG analyses from the terminal CFG so preserved MDT/MLI match
  // what downstream Finalize/Verify observe.
  MDTWrapper.getDomTree().recalculate(MF);
  MLIWrapper.getLI().calculate(MDTWrapper.getDomTree());

  PrefixBudgetRecord FinalBudget = capturePrefixBudget(MF, TII);
  dumpPrefixBudget("final", MF, FinalBudget);
  const bool NoGrowth = EntryBudget.noGrowth(FinalBudget);
  LLVM_DEBUG({
    ConvergenceSnapshot Final = takeSnapshot(MF, TII);
    dbgs() << "HaydnLateConvergence: " << MF.getName()
           << " prefixes=" << FinalBudget.Pairs.size()
           << " no-growth=" << (NoGrowth ? 1 : 0)
           << " jalr-sites=" << Final.IndirectSites.size()
           << " hwloop-setups=" << Final.HwLoopSetups << '\n';
  });
  (void)NoGrowth;

  return AnyChanged;
}

//===----------------------------------------------------------------------===//
// Driver pass.
//===----------------------------------------------------------------------===//

char HaydnLateConvergencePass::ID = 0;

HaydnLateConvergencePass::HaydnLateConvergencePass()
    : MachineFunctionPass(ID) {
  initializeHaydnLateConvergencePassPass(*PassRegistry::getPassRegistry());
}

void HaydnLateConvergencePass::getAnalysisUsage(AnalysisUsage &AU) const {
  // Inner S2 resolves MMI/MLI/MDT/AA/TargetPassConfig through the borrowed
  // resolver. AA is IR-level (function-locked). TargetPassConfig is
  // immutable. MDT/MLI are recalculated in-place each iteration and after
  // the loop, so the wrapper objects remain valid. CFG is not preserved:
  // inner BranchRelaxation and HWLoop demote may split or insert blocks.
  AU.addRequired<MachineLoopInfoWrapperPass>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  AU.addRequired<AAResultsWrapperPass>();
  AU.addRequired<TargetPassConfig>();
  AU.addPreserved<AAResultsWrapperPass>();
  AU.addPreserved<TargetPassConfig>();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  AU.addPreserved<MachineLoopInfoWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU); // adds MMI requirement
}

bool HaydnLateConvergencePass::runOnMachineFunction(MachineFunction &MF) {
  // Same product skip law as the scheduler seat this pass replaces: the
  // quality transform honors optnone. Finalize/Verify (downstream, not this
  // pass) own the no-reorder commit for optnone functions.
  if (skipFunction(MF.getFunction()))
    return false;
  return runHaydnLateConvergence(MF, *this);
}

FunctionPass *llvm::createHaydnLateConvergencePass() {
  return new HaydnLateConvergencePass();
}

INITIALIZE_PASS_BEGIN(HaydnLateConvergencePass, DEBUG_TYPE,
                      "Haydn Late Layout Convergence Loop", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(HaydnLateConvergencePass, DEBUG_TYPE,
                    "Haydn Late Layout Convergence Loop", false, false)
