//===-- HaydnLateConvergence.cpp - monotone late closure driver ----*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// GR2.6 monotone closure driver. See HaydnLateConvergence.h for the
// contract loop, the enforced no-growth law, and the termination argument.
// Implementation notes:
//
// * S2 runs EXACTLY ONCE per driver entry, before the first closure
//   iteration. The W68.3R RunS2/inventoryChanged reschedule-after-mutation
//   arm is deleted (constraints 9/11: no scheduling after a range/HWLoop
//   mutation inside the loop; GR2.6 migration). Every subsequent mutating
//   iteration is only [MDT/MLI refresh -> LatencyStalls -> FixupHwLoops
//   inner-first -> BranchRelaxation LAST].
//
// * Every mutating step instantiates a FRESH pass object — the two common
//   passes (BranchRelaxationLegacy, PostMachineSchedulerLegacy) are
//   anonymous-namespace classes reachable only through the pass registry;
//   Pass::createPass(ID) is the same mechanism TargetPassConfig::addPass
//   uses, so the common implementations are reused without editing them and
//   without any second scheduler. The two Haydn passes are created through
//   their factories. All are deleted at end of iteration: no state
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
//   (llvm/lib/CodeGen/BranchRelaxation.cpp) plus the parcel-rounded
//   worst-case Haydn MBB gap (the same ceilProductParcels arithmetic
//   LayoutBlockInfo::postOffset uses — GR2.6 replaces the raw ARM
//   unknownPadding under-reserve). The census includes those prefix
//   charges; whole-function byte totals are not a proof.
//
// * Enforced no-growth law (GR2.6, was the discarded (void)NoGrowth site;
//   D1.41 exact accounting): growth of a consumed pair's charge across ONE
//   closure iteration is fatal unless accounted by the admitted event
//   vocabulary (promotion / demote / stall / split evidence) ATTRIBUTED to
//   that pair's span — every event names its affected MBBs and its exact
//   net bytes (measured from per-arm MBB size deltas), so an event never
//   grants credit to an unrelated prefix. Entry-vs-final no-growth is
//   deliberately telemetry-only: the single S2 may redistribute bytes with
//   no event at all.
//
// * Monotone law (checked every iteration; violation is a hard diagnostic):
//     - the hardware-loop setup census never grows (demotion erases the
//       SET; no late pass creates one).
//   There is deliberately NO indirect-count monotone check. The
//   gcc_layout t018 story (a BR re-run swaps a JALR_W long form back to
//   PC-relative) predates the TII guards that make JALR promotion
//   irreversible and is stale; IndirectCount is non-decreasing in
//   practice, but it is not law-checked because it seeds the iteration
//   bound and the event ledger carries the fixed point.
//   Short->Relaxed shape growth is also NOT a census dimension: the
//   relaxed two-branch shape is byte-identical in kind to an ordinary
//   two-target conditional lowering (insertBranch emits cond+B), so shape
//   churn is carried by the per-iteration pair charges, not by a census
//   dimension of its own.
//
//===----------------------------------------------------------------------===//

#include "HaydnLateConvergence.h"
#include "Haydn.h"
#include "HaydnBundlePlan.h"
#include "HaydnFixupHwLoops.h"
#include "HaydnFormatERecords.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnInstrInfo.h"
#include "HaydnLatencyStalls.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
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
// pad before an aligned block, BEFORE parcel rounding. Range budgets
// reserve the parcel-rounded form (the same ceilProductParcels arithmetic
// LayoutBlockInfo::postOffset uses): on the product 12-byte grid a
// raw A-1 reserve can under-reserve by up to one parcel minus one byte
// (GR2.6; the pre-migration raw reserve under-counted the pad the
// closure actually emits).
static unsigned unknownPadding(Align A) {
  const unsigned KnownBits = 0;
  if (KnownBits < Log2(A))
    return A.value() - (1u << KnownBits);
  return 0;
}

/// Parcel-rounded worst-case alignment reserve for \p A. This is what the
/// prefix budget charges — never the raw unknownPadding value.
static uint64_t parcelRoundedUnknownPad(Align A) {
  const unsigned Raw = unknownPadding(A);
  if (Raw == 0)
    return 0;
  return static_cast<uint64_t>(
      haydn::bundle::productBundlesToBytes(haydn::bundle::ceilProductParcels(Raw)));
}

using PrefixKey = HaydnPrefixKey;

static unsigned computeBlockSize(const MachineBasicBlock &MBB,
                                 const TargetInstrInfo &TII) {
  unsigned Bytes = 0;
  for (const MachineInstr &MI : MBB)
    Bytes += TII.getInstSizeInBytes(MI);
  return Bytes;
}

/// GR2.6 audit note (no second fatal — documented residual): the closure
/// budget/offset model consumes committed EncodedBytes only — BUNDLE
/// roots (committedEncodedBytes + namedLateLayoutGrowthBytes), zero-size
/// metas, and single-parcel exact reals/pseudos. Bare MIs larger than one
/// product parcel outside the named-growth vocabulary (the N*12 stand-in
/// class: LOADI32/LOAD_ADDR/LOADI64/VASTART/VACOPY/MOV_GPR_TO_DR64...) DO
/// survive to this seat on crafted -start-after MIR corpora
/// (residual-executable-pseudos-matrix.mir), where AsmPrinter's
/// residual-pseudo fatal is the owning fail-closed seat. A second fatal
/// here would preempt that owner (constraint 14: no second mechanism);
/// the surviving stand-ins are the recorded W70.1/W70.4 residual and get
/// their exact layout length through getInstSizeInBytes exactly as
/// BranchRelaxation itself charges them.

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

/// Count still-relaxable short-branch sites and retained HWLoop setups in
/// the span between the Src and Dst endpoints (exclusive of the Src
/// block's own bytes; inclusive of interior MBBs). The ONE shared
/// classifier (haydn::hwloop::isStillRelaxableShortBranch) is the same
/// law FixupHwLoops charges Off1/Off2 margins with — constraint 14.
static void countSpanEvents(const MachineFunction &MF,
                            const HaydnInstrInfo &HII, unsigned Src,
                            unsigned Dst, unsigned &RelaxableSites,
                            unsigned &HwLoopSetups) {
  RelaxableSites = 0;
  HwLoopSetups = 0;
  bool InSpan = false;
  for (const MachineBasicBlock &MBB : MF) {
    const unsigned N = MBB.getNumber();
    const bool IsEndPoint = (N == Src || N == Dst);
    if (!InSpan) {
      if (!IsEndPoint)
        continue;
      InSpan = true;
      if (N == Dst)
        break; // empty span
      continue;
    }
    if (N == Dst)
      break;
    for (const MachineInstr &MI : MBB.instrs()) {
      if (HII.isHardwareLoopSetupInstr(MI))
        ++HwLoopSetups;
      if (haydn::hwloop::isStillRelaxableShortBranch(MI))
        ++RelaxableSites;
    }
  }
}

static HaydnPrefixPairRecord measurePrefix(const MachineFunction &MF,
                                           const HaydnInstrInfo &HII,
                                           ArrayRef<LayoutBlockInfo> Info,
                                           unsigned Src, unsigned Dst) {
  HaydnPrefixPairRecord P;
  P.Src = Src;
  P.Dst = Dst;
  if (Src == Dst) {
    if (Src < Info.size()) {
      P.EncodedBytes = Info[Src].Size;
      P.MaxAlign = Info[Src].Alignment;
      P.AlignPad = parcelRoundedUnknownPad(P.MaxAlign);
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
  // Reserve the parcel-rounded worst-case pad, never the accumulated
  // emitted gap alone (the emitted gap is only this compilation's pad).
  P.AlignPad = std::max<uint64_t>(Pad, parcelRoundedUnknownPad(MaxA));
  countSpanEvents(MF, HII, Src, Dst, P.RelaxableSites, P.HwLoopSetups);
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
  /// Source MBB numbers holding a JALR/JALR_W long form. Diagnostic aid
  /// only — the monotone LAW is the COUNT (IndirectSites can shift MBB
  /// numbers under BranchRelaxation splits, which renumbers blocks while
  /// every site survives; a per-number set difference false-positives as
  /// a regression, gcc_layout t018).
  DenseSet<unsigned> IndirectSites;
  /// JALR/JALR_W long-form site count. Non-decreasing under the TII
  /// guards (JALR promotion is irreversible); counted once at entry to
  /// seed the iteration bound, where its term is slack-safe.
  unsigned IndirectCount = 0;

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
    // IndirectSites (MBB numbers) is deliberately excluded: BR splits
    // renumber blocks while every site survives; the count is the law.
    return IndirectCount == R.IndirectCount &&
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
    HaydnPrefixPairRecord Rec = measurePrefix(MF, HII, Info, K.first, K.second);
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
      //
      // S2 identity-bakes JALR_W onto generated JALR_E2_/JALR_E3_ members
      // (AIEMachineScheduler.cpp:1126-1132 setDesc). Census the logical,
      // not the raw opcode — otherwise a bake looks like the site shrank
      // back to a PC-relative B and the monotone diagnostic is a false
      // positive (core_matrix / NatureDSP compile abort).
      unsigned Log = haydn::format_e::logicalOpcodeOrSelf(Opc);
      StringRef Name = TII.getName(Log);
      if (Name.ends_with("_MSP"))
        Name = Name.drop_back(4);
      if (Name.starts_with("JALR")) {
        S.IndirectSites.insert(MBB.getNumber());
        ++S.IndirectCount;
      }
    }
  }
  return S;
}

/// GR2.6 monotone closure repeat predicate: an iteration repeats only on
/// an UPWARD event — a JALR promotion (IndirectCount up), an HWLoop demote
/// insertion (HwLoopSetups down), MBB growth (BranchRelaxation split
/// evidence), or growth of any consumed pair's charge. An iteration with
/// no upward event terminates the loop (pure shrink / no change closes).
bool upwardEvent(const ConvergenceSnapshot &Before,
                 const ConvergenceSnapshot &After) {
  if (After.IndirectCount > Before.IndirectCount)
    return true; // promotion
  if (After.HwLoopSetups < Before.HwLoopSetups)
    return true; // demote insertion
  if (After.MBBBytes.size() > Before.MBBBytes.size())
    return true; // split
  for (const auto &KV : After.PrefixCharge)
    if (KV.second > Before.PrefixCharge.lookup(KV.first))
      return true; // span grew
  return false;
}

/// Per-MBB encoded sizes via TII->getInstSizeInBytes (the same law
/// scanLayout/computeBlockSize and ConvergenceSnapshot::MBBBytes use) —
/// the D1.41 attribution measurement taken around each mutating arm.
static DenseMap<unsigned, uint64_t> perMBBEncodedSizes(MachineFunction &MF,
                                                       const TargetInstrInfo &TII) {
  DenseMap<unsigned, uint64_t> Sizes;
  for (MachineBasicBlock &MBB : MF)
    Sizes[MBB.getNumber()] = computeBlockSize(MBB, TII);
  return Sizes;
}

/// D1.41 pair-interval attribution for a stable-key window (stalls /
/// Fixup / LongBranchNormalize — arms that never renumber or create
/// blocks): the measured CHARGE delta (encoded + alignment pad) per pair
/// key is exact for the span, alignment-pad ripple included. Only spans
/// that net-grew earn an event (admission vocabulary is growth; a span
/// that net-shrank — stripped regenerable parcels — earns nothing and its
/// shrinkage never subsidizes another span). The event class is chosen
/// PER SPAN from the span's own setup census: a span whose retained-setup
/// count dropped across the window was touched by a demotion (Demote
/// class, capped at MaxHwLoopDemoteGrowthBytes per retained setup by the
/// law); every other growing span is stall-class (exact bytes). No global
/// class switch: nested loops sharing one span and a pure retained-pad
/// iteration in the same window are each accounted by their own span's
/// evidence.
static void addPairDeltaEvents(HaydnClosureEventLedger &Ledger,
                               HaydnClosureGrowthEvent::Kind KindIfSetupLost,
                               HaydnClosureGrowthEvent::Kind KindOtherwise,
                               const HaydnPrefixBudgetRecord &Pre,
                               const HaydnPrefixBudgetRecord &Post) {
  DenseMap<HaydnPrefixKey, const HaydnPrefixPairRecord *> PreByKey;
  for (const HaydnPrefixPairRecord &P : Pre.Pairs)
    PreByKey[{P.Src, P.Dst}] = &P;
  for (const HaydnPrefixPairRecord &P : Post.Pairs) {
    auto It = PreByKey.find({P.Src, P.Dst});
    if (It == PreByKey.end())
      continue; // no stable key (cannot happen in these windows)
    const HaydnPrefixPairRecord &B = *It->second;
    if (P.charge() <= B.charge())
      continue;
    const bool SetupLost = P.HwLoopSetups < B.HwLoopSetups;
    Ledger.Events.push_back(HaydnClosureGrowthEvent::forPair(
        SetupLost ? KindIfSetupLost : KindOtherwise, P.charge() - B.charge(),
        P.Src, P.Dst));
  }
}

/// D1.41 vanished-key attribution for a stable-key window. A key can
/// vanish inside stalls/Fixup/LongBranchNormalize ONLY by the window's
/// one key-consuming rewrite: a far-site promotion to the in-block long
/// form (LUI+ADDI[+cond]+JALR), whose parcels carry no range-pair MBB
/// operand, so collectRangePairs stops emitting the key (cxfir16x16
/// bb.6->bb.2 at the normalize seat). The rewrite's own byte growth is
/// already attributed exactly by the surviving spans that grew (the
/// self-prefix bb.6->bb.6 above); what vanishes is the KEY, so the
/// migration evidence is a Promotion-class event scoped to EXACTLY the
/// vanished key — never a global count, never credit on a surviving
/// span. Fail-closed: this helper is called only when the window's
/// IndirectCount actually rose (census evidence, the same law
/// takeSnapshot counts), so a vanished key with no promotion in the
/// window still reaches the named vanished-pair fatal.
static void addVanishedPairEvents(HaydnClosureEventLedger &Ledger,
                                  const HaydnPrefixBudgetRecord &Pre,
                                  const HaydnPrefixBudgetRecord &Post) {
  DenseMap<HaydnPrefixKey, bool> PostKeys;
  for (const HaydnPrefixPairRecord &P : Post.Pairs)
    PostKeys[{P.Src, P.Dst}] = true;
  for (const HaydnPrefixPairRecord &B : Pre.Pairs)
    if (!PostKeys.count({B.Src, B.Dst}))
      Ledger.Events.push_back(HaydnClosureGrowthEvent::forPair(
          HaydnClosureGrowthEvent::Kind::Promotion, /*Bytes=*/0, B.Src,
          B.Dst));
}

/// D1.41 MBB attribution for the BranchRelaxation window (BR renumbers
/// pair keys at entry, so pair matching is meaningless there): net-grown
/// surviving MBBs earn Promotion-class events; the accounting law caps
/// the span credit at MaxSingleBranchGrowthBytes × the span's
/// pre-iteration still-relaxable sites.
static void addMBBDeltaEvents(HaydnClosureEventLedger &Ledger,
                              HaydnClosureGrowthEvent::Kind K,
                              const DenseMap<unsigned, uint64_t> &Pre,
                              const DenseMap<unsigned, uint64_t> &Post) {
  for (const auto &KV : Post) {
    const uint64_t PreSize = Pre.lookup(KV.first);
    if (KV.second > PreSize)
      Ledger.Events.push_back(
          HaydnClosureGrowthEvent(K, KV.second - PreSize, {KV.first}));
  }
}

/// Verify the monotone law between the snapshots bracketing one mutating
/// iteration. Returns a diagnostic string on violation.
std::string checkMonotonicity(const ConvergenceSnapshot &Before,
                              const ConvergenceSnapshot &After) {
  // Deliberately NO indirect-count monotone check. JALR promotion is
  // irreversible under the TII guards (analyzeBranch unanalyzable at
  // JALR; removeBranch keeps LUI+ADDI32_W+JALR_W intact), so
  // IndirectCount is non-decreasing — but the earlier gcc_layout t018
  // swap-back narrative predates those guards and the count is not
  // law-checked; termination rests on the global iteration bound
  // (#cond + #hwloop + #indirect + 2, the indirect term slack-safe)
  // and the MBBBytes/PrefixCharge fixed point.
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
using PrefixBudgetRecord = HaydnPrefixBudgetRecord;

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
    Budget.Pairs.push_back(measurePrefix(MF, HII, Info, K.first, K.second));
  // Long-form site census (the vanished-key promotion evidence gate; the
  // counting law is takeSnapshot's — logical opcode, MSP suffix stripped).
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB.instrs()) {
      StringRef Name = TII.getName(haydn::format_e::logicalOpcodeOrSelf(
          MI.getOpcode()));
      if (Name.ends_with("_MSP"))
        Name = Name.drop_back(4);
      if (Name.starts_with("JALR"))
        ++Budget.IndirectCount;
    }
  return Budget;
}

static void dumpPrefixBudget(StringRef Tag, const MachineFunction &MF,
                             const PrefixBudgetRecord &Budget) {
  (void)Tag;
  (void)MF;
  (void)Budget;
  LLVM_DEBUG({
    for (const HaydnPrefixPairRecord &P : Budget.Pairs)
      dbgs() << "HaydnLateConvergence: " << Tag << ' ' << MF.getName()
             << " prefix bb." << P.Src << "->bb." << P.Dst
             << " encoded=" << P.EncodedBytes << " pad=" << P.AlignPad
             << " maxalign=" << P.MaxAlign.value()
             << " budget=" << P.charge() << '\n';
  });
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Enforced no-growth law (GR2.6, exact accounting D1.41). Pure accounting
// over the snapshot pair plus the attributed event ledger — unit-testable
// without a MachineFunction.
//===----------------------------------------------------------------------===//

bool llvm::haydnSpanContainsMBB(unsigned Src, unsigned Dst, unsigned MBB) {
  // Same numeric interior the shared span walk (countSpanEvents /
  // measurePrefix) iterates: the span runs from the Src endpoint to the
  // Dst endpoint, visiting every numerically interior MBB (either
  // direction — collectRangePairs emits both forward and backedge pairs).
  // A self-pair span (latch branch, ZOL self-prefix) is exactly the Src
  // block: a growth event in the block itself is inside that span.
  if (Src == Dst)
    return MBB == Src;
  const unsigned Lo = std::min(Src, Dst);
  const unsigned Hi = std::max(Src, Dst);
  return MBB >= Lo && MBB <= Hi;
}

/// Exact admitted budget for one pair span from the attributed ledger
/// (D1.41): every event whose affected MBBs feed the span contributes its
/// exact net bytes; promotion/demote credit is additionally capped by the
/// sites/setups the span held BEFORE the iteration (a promoted site stops
/// being relaxable after; a demoted setup is no longer retained). An
/// event whose MBBs are all outside the span contributes nothing — no
/// event may grant credit to an unrelated prefix.
static uint64_t spanAdmittedBudget(const HaydnClosureEventLedger &Ledger,
                                   const HaydnPrefixPairRecord &Span,
                                   int64_t MaxSingleBranchGrowthBytes,
                                   int64_t MaxHwLoopDemoteGrowthBytes) {
  uint64_t PromotionBudget = 0, DemoteBudget = 0, StallBudget = 0;
  for (const HaydnClosureGrowthEvent &E : Ledger.Events) {
    bool InSpan;
    if (E.HasExactPair)
      InSpan = E.PairSrc == Span.Src && E.PairDst == Span.Dst;
    else if (!E.AffectedMBBs.empty())
      InSpan = llvm::any_of(E.AffectedMBBs, [&](unsigned M) {
        return haydnSpanContainsMBB(Span.Src, Span.Dst, M);
      });
    else
      InSpan = false; // unscoped: admits nothing (fail-closed)
    if (!InSpan)
      continue;
    switch (E.K) {
    case HaydnClosureGrowthEvent::Kind::Promotion:
      PromotionBudget += E.Bytes;
      break;
    case HaydnClosureGrowthEvent::Kind::Demote:
      DemoteBudget += E.Bytes;
      break;
    case HaydnClosureGrowthEvent::Kind::Stall:
      StallBudget += E.Bytes;
      break;
    }
  }
  // Vocabulary caps over the span's pre-iteration census (the same two
  // bounds the coarse law used; now applied to the exact per-event bytes).
  PromotionBudget = std::min(
      PromotionBudget, static_cast<uint64_t>(Span.RelaxableSites) *
                           static_cast<uint64_t>(MaxSingleBranchGrowthBytes));
  DemoteBudget = std::min(
      DemoteBudget, static_cast<uint64_t>(Span.HwLoopSetups) *
                        static_cast<uint64_t>(MaxHwLoopDemoteGrowthBytes));
  return PromotionBudget + DemoteBudget + StallBudget;
}

std::string llvm::haydnClosureGrowthAccount(
    const HaydnPrefixBudgetRecord &Before, const HaydnPrefixBudgetRecord &After,
    const HaydnClosureEventLedger &Ledger, int64_t MaxSingleBranchGrowthBytes,
    int64_t MaxHwLoopDemoteGrowthBytes) {
  DenseMap<HaydnPrefixKey, const HaydnPrefixPairRecord *> BeforeByKey;
  for (const HaydnPrefixPairRecord &P : Before.Pairs)
    BeforeByKey[{P.Src, P.Dst}] = &P;

  const bool SplitEvidence = Ledger.MBBGrowth > 0;
  // Structural migrations that move pair keys WITHOUT an MBB split:
  // (1) a promotion's insertIndirectBranch trampoline/restore blocks
  //     introduce new pair keys (BranchBB retargeting) — the promotion
  //     event itself is key-migration evidence;
  // (2) BranchRelaxation calls RenumberBlocks() unconditionally at entry,
  //     so even a no-change BR iteration rotates EVERY pair key when a
  //     mid-CFG block was removed earlier (trampoline merging compacts
  //     numbering; split blocks appended past the high-water mark keep
  //     vacated numbers alive until then).
  // Renumber migrations are accounted as ONE aggregate span: total
  // appeared charge vs total vanished charge plus the exact event budget
  // attributed to the vanished keys' spans (a pure retarget with an empty
  // ledger is the zero-growth case of the same aggregate); residue beyond
  // that budget falls through to the named fatal below.
  DenseMap<HaydnPrefixKey, bool> AfterKeys;
  for (const HaydnPrefixPairRecord &A : After.Pairs)
    AfterKeys[{A.Src, A.Dst}] = true;
  SmallVector<const HaydnPrefixPairRecord *, 4> Appeared, VanishedKeys;
  for (const HaydnPrefixPairRecord &A : After.Pairs)
    if (!BeforeByKey.count({A.Src, A.Dst}))
      Appeared.push_back(&A);
  for (const HaydnPrefixPairRecord &B : Before.Pairs)
    if (!AfterKeys.count({B.Src, B.Dst}))
      VanishedKeys.push_back(&B);
  uint64_t AppearedCharge = 0, VanishedCharge = 0;
  for (const auto *A : Appeared)
    AppearedCharge += A->charge();
  for (const auto *B : VanishedKeys)
    VanishedCharge += B->charge();
  // D1.41 exact per-span budget: the ONLY admitted growth for one span is
  // the events whose affected MBBs lie inside it, with promotion/demote
  // events additionally capped by the sites/setups the span held BEFORE
  // the iteration (a promoted site stops being relaxable after; a demoted
  // setup is no longer retained). Stall-class events contribute exactly
  // the parcels their arm inserted on the affected MBBs — no fixed
  // allowance, no global grant. Identical law for the common-key and
  // migration-aggregate arms below.
  auto SpanAdmitted = [&](const HaydnPrefixPairRecord &B) {
    return spanAdmittedBudget(Ledger, B, MaxSingleBranchGrowthBytes,
                              MaxHwLoopDemoteGrowthBytes);
  };
  // A renumber migration (BranchRelaxation ALWAYS RenumbersBlocks at
  // entry, even on its no-change iterations) rotates every pair key at
  // once, so the migration is accounted as ONE aggregate span: total
  // appeared charge against total vanished charge plus the event budget
  // the vanished keys were entitled to. A pure retarget (equal charges,
  // empty ledger) is the zero-growth case of the same aggregate. Growth
  // beyond that budget falls through to the named fatal below.
  const bool VanishedPresent = !VanishedKeys.empty();
  uint64_t VanishedAdmitted = 0;
  for (const auto *B : VanishedKeys)
    VanishedAdmitted += SpanAdmitted(*B);
  const bool AccountedMigration =
      VanishedPresent && !Appeared.empty() &&
      AppearedCharge <= VanishedCharge + VanishedAdmitted;
  const bool AnyPromotionOrDemote = llvm::any_of(
      Ledger.Events, [](const HaydnClosureGrowthEvent &E) {
        return E.K == HaydnClosureGrowthEvent::Kind::Promotion ||
               E.K == HaydnClosureGrowthEvent::Kind::Demote;
      });
  const bool KeyMigrationEvidence =
      SplitEvidence || AnyPromotionOrDemote || AccountedMigration;
  for (const HaydnPrefixPairRecord &A : After.Pairs) {
    auto It = BeforeByKey.find({A.Src, A.Dst});
    if (It == BeforeByKey.end()) {
      // Appeared key: legal only as structural evidence (split
      // renumbering, promotion/demote block insertion, or an
      // event-accounted renumber migration); with an empty ledger and no
      // accounted migration the pair is new layout state no admitted
      // event can produce.
      if (!KeyMigrationEvidence)
        return ("appeared pair bb." + Twine(A.Src) + "->bb." + Twine(A.Dst) +
                " with no split/promotion evidence")
                   .str();
      continue;
    }
    const HaydnPrefixPairRecord &B = *It->second;
    const uint64_t Growth =
        A.charge() > B.charge() ? A.charge() - B.charge() : 0;
    if (Growth == 0)
      continue;
    // Split evidence admits block-boundary movement inside the span.
    if (Growth > SpanAdmitted(B) && !SplitEvidence)
      return ("bb." + Twine(A.Src) + "->bb." + Twine(A.Dst) + " grew " +
              Twine(Growth) + " bytes; admitted vocabulary covers " +
              Twine(SpanAdmitted(B)))
                 .str();
  }

  // Vanished keys: legal only as structural evidence (renumbering /
  // promotion retargeting / balanced pure retarget), never as a silent
  // disappearance.
  if (!KeyMigrationEvidence) {
    for (const auto *B : VanishedKeys)
      return ("vanished pair bb." + Twine(B->Src) + "->bb." + Twine(B->Dst) +
              " with no split evidence")
                 .str();
  }
  return {};
}

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
  // confirmation. S2 runs exactly once per driver entry (GR2.6); closure
  // iterations after it are only stalls + HWLoop revalidation +
  // BranchRelaxation LAST. Exhaustion without a fixed point is a hard
  // diagnostic.
  unsigned NumCondBranches = 0, NumHwLoopSetups = 0, NumIndirect = 0;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB.instrs()) {
      if (MI.isConditionalBranch())
        ++NumCondBranches;
      if (HII.isHardwareLoopSetupInstr(MI))
        ++NumHwLoopSetups;
      unsigned Opc = MI.getOpcode();
      StringRef N = TII.getName(
          haydn::format_e::logicalOpcodeOrSelf(Opc));
      if (N.ends_with("_MSP"))
        N = N.drop_back(4);
      if (N.starts_with("JALR"))
        ++NumIndirect;
    }
  // Indirect sites never demote (TII guards make JALR promotion
  // irreversible), so the #indirect term is dead slack; it is retained
  // because the bound is slack-safe either way.
  const unsigned MaxIterations =
      NumCondBranches + NumHwLoopSetups + NumIndirect + 2;

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

  // S2 EXACTLY ONCE per driver entry, before the first closure iteration
  // (GR2.6). PostMachineSchedulerImpl::run returns true unconditionally,
  // so the census (not the return value) decides relevance; conservatively
  // mark changed (an in-MBB reorder is invisible to the byte census). The
  // inner pass honors optnone itself; the driver skipped it first.
  {
    MachineFunctionPass *S2 = createFreshMachinePass(PostMachineSchedulerID);
    if (!S2)
      report_fatal_error(
          "HaydnLateConvergence: PostMachineSchedulerID not registered",
          /*gen_crash_diag=*/false);
    LLVM_DEBUG(dbgs() << "HaydnLateConvergence: S2 once per driver entry\n");
    runInnerPass(*S2, DriverPass, MF);
    delete S2;
    AnyChanged = true;
  }

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

    PrefixBudgetRecord IterBefore = capturePrefixBudget(MF, TII);
    LLVM_DEBUG(dumpPrefixBudget("iter-in", MF, IterBefore));

    // 1. Regenerate stalls/alignment (exposed-pipeline correctness net).
    //    D1.41: this window's keys are stable (no renumber/creation), so
    //    its attribution is the exact per-pair CHARGE delta — pad ripple
    //    included. Baseline is the iteration-top scan (IterBefore holds
    //    the same measurement; rescan keeps the two independent).
    bool StallsChanged = false;
    if (MachineFunctionPass *Stalls =
            asMachinePass(createHaydnLatencyStallsPass())) {
      StallsChanged = runInnerPass(*Stalls, DriverPass, MF);
      AnyChanged |= StallsChanged;
      delete Stalls;
    }
    PrefixBudgetRecord PostStalls = capturePrefixBudget(MF, TII);

    // 2. Validate or demote HWLoops against the post-S2 byte layout.
    //    fixupOne recomputes Off1/Off2 windows from CURRENT layout on every
    //    invocation, so re-invocation IS the post-mutation revalidation of
    //    retained loops (innermost-first wave order). Fixup may also insert
    //    exact-commit deficit NOP pads on a RETAINED loop (setup-gap /
    //    MinBodyBundles floors) — admitted closure vocabulary alongside
    //    demotion (a change signal, not a demotion event). D1.41: stable
    //    keys — exact per-pair charge delta.
    bool FixupChanged = false;
    if (MachineFunctionPass *Fixup =
            asMachinePass(createHaydnFixupHwLoopsPass())) {
      FixupChanged = runInnerPass(*Fixup, DriverPass, MF);
      AnyChanged |= FixupChanged;
      delete Fixup;
    }
    PrefixBudgetRecord PostFixup = capturePrefixBudget(MF, TII);

    // 3. BranchRelaxation LAST in the mutating iteration (contract).
    //    GR2.7: LongBranchNormalize immediately before it — the S2 repack
    //    above (and this loop's stalls/Fixup mutations) can shrink a
    //    fallthrough span so a site the pre-S1 normalization BR accepted
    //    re-overflows. The in-block long-form rewrite (no CFG creation)
    //    keeps BR's trampoline/RestoreBB arms unreachable so the
    //    postcommit block-budget wall holds (bundlesim cb_wua_cbr /
    //    matmult-int failure class). No-op before the first Finalize
    //    stamp (pre-stamp window owns nothing here). D1.41: stable keys —
    //    exact per-pair charge delta.
    bool NormalizeChanged = false;
    if (MachineFunctionPass *Norm =
            asMachinePass(createHaydnLongBranchNormalizePass())) {
      NormalizeChanged = runInnerPass(*Norm, DriverPass, MF);
      AnyChanged |= NormalizeChanged;
      delete Norm;
    }
    PrefixBudgetRecord PostNorm = capturePrefixBudget(MF, TII);
    DenseMap<unsigned, uint64_t> SizesPreBR = perMBBEncodedSizes(MF, TII);
    bool BRChanged = false;
    if (MachineFunctionPass *BR =
            createFreshMachinePass(BranchRelaxationPassID)) {
      BRChanged = runInnerPass(*BR, DriverPass, MF);
      AnyChanged |= BRChanged;
      delete BR;
    } else {
      report_fatal_error(
          "HaydnLateConvergence: BranchRelaxationPassID not registered",
          /*gen_crash_diag=*/false);
    }
    DenseMap<unsigned, uint64_t> SizesPostBR = perMBBEncodedSizes(MF, TII);

    // D1.40 immediate CFG identity check: refusal must not wait for the
    // later Verify seat while further closure mutation proceeds. After
    // each mutating inner pass (stalls, Fixup, Norm, BR — at minimum the
    // Norm/BR pair above), any postcommit CFG mutation is an immediate
    // fatal with the wall text: creation AND shrink AND equal-count MBB
    // replacement AND a numbering-slot trace (create-then-delete /
    // erase+replace) — the identity laws, not just cardinality. This seat
    // runs after BR's LAST invocation, whose entry RenumberBlocks precedes
    // all of BR's own mutations, so a create-then-delete inside BR is still
    // visible here through the numbering-slot slack (L3) or the token
    // sequence (L4). The ledger's MBBGrowth below stays TELEMETRY ONLY
    // (event evidence for the growth accounting); the wall itself is this
    // check plus the Verify seats — no second admission path.
    if (std::string CfgViolation =
            MF.getInfo<HaydnMachineFunctionInfo>()
                ->postCommitCfgCreationViolation(MF);
        !CfgViolation.empty())
      report_fatal_error(
          "HaydnLateConvergence: postcommit CFG creation refused during "
          "closure (" +
              Twine(CfgViolation) + ")",
          /*gen_crash_diag=*/false);

    // 4. Change detection + monotone law + ENFORCED no-growth law (GR2.6).
    ConvergenceSnapshot After = takeSnapshot(MF, TII);
    std::string Violation = checkMonotonicity(Before, After);
    if (!Violation.empty())
      report_fatal_error("HaydnLateConvergence: non-monotone mutation: " +
                             Twine(Violation),
                         /*gen_crash_diag=*/false);

    // Event ledger for the no-growth accounting (D1.41 exact attribution):
    // each stable-key window (stalls / Fixup / LongBranchNormalize)
    // contributes the exact per-pair CHARGE delta it measured — inserted
    // AND removed parcels, pad ripple included — to exactly the pair it
    // affected; the BR window contributes per-MBB promotion events (BR
    // renumbers keys, so pair matching is meaningless there; the
    // vocabulary cap keeps those tight). No global booleans/counts: an
    // event on one span never grants credit to an unrelated prefix.
    HaydnClosureEventLedger Ledger;
    // (a) Stall-class: the stalls pass's net charge growth per span
    //     (regenerable-parcel strips are net-negative: no event, and their
    //     shrinkage never subsidizes another span).
    if (StallsChanged) {
      addPairDeltaEvents(Ledger, HaydnClosureGrowthEvent::Kind::Stall,
                         HaydnClosureGrowthEvent::Kind::Stall, IterBefore,
                         PostStalls);
      // Stalls never rewrite a branch site; a vanished key here would be
      // unaccounted (no census rise -> addVanishedPairEvents stays off).
    }
    // (b) Fixup window: per-span class from the span's own setup census —
    //     a span whose retained-setup count dropped was touched by a
    //     demotion (Demote class, capped at MaxHwLoopDemoteGrowthBytes per
    //     retained setup); pure deficit-pad growth on a RETAINED loop is
    //     stall-class. Both carry the window's exact measured deltas.
    if (FixupChanged)
      addPairDeltaEvents(Ledger, HaydnClosureGrowthEvent::Kind::Demote,
                         HaydnClosureGrowthEvent::Kind::Stall, PostStalls,
                         PostFixup);
    // (c) GR2.7 in-block long form (LongBranchNormalize) inserts
    //     LUI+ADDI+JALR parcels on a far site, including ZOL latches whose
    //     self-prefix is not a RelaxableSites pair (PseudoLoopEnd is
    //     always-in-range): stall-class, exact per-span charge delta.
    //     A promotion CONSUMES the promoted pair key (the long form has
    //     no range-pair MBB operand): the vanished key earns a
    //     Promotion-class event scoped to exactly itself, gated on the
    //     window's IndirectCount census rise (cxfir16x16 bb.6->bb.2 —
    //     the key vanishes while its self-prefix sibling bb.6->bb.6
    //     carries the exact byte growth).
    if (NormalizeChanged) {
      addPairDeltaEvents(Ledger, HaydnClosureGrowthEvent::Kind::Stall,
                         HaydnClosureGrowthEvent::Kind::Stall, PostFixup,
                         PostNorm);
      if (PostNorm.IndirectCount > PostFixup.IndirectCount)
        addVanishedPairEvents(Ledger, PostFixup, PostNorm);
    }
    // (d) BR promotions (JALR long form): the IndirectCount delta is the
    //     census evidence; the exact bytes come from BR's own MBB deltas,
    //     capped per span by MaxSingleBranchGrowthBytes × the span's
    //     pre-iteration still-relaxable sites.
    if (After.IndirectCount > Before.IndirectCount)
      addMBBDeltaEvents(Ledger, HaydnClosureGrowthEvent::Kind::Promotion,
                        SizesPreBR, SizesPostBR);
    Ledger.MBBGrowth = After.MBBBytes.size() > Before.MBBBytes.size()
                           ? After.MBBBytes.size() - Before.MBBBytes.size()
                           : 0;
    PrefixBudgetRecord IterAfter = capturePrefixBudget(MF, TII);
    LLVM_DEBUG(dumpPrefixBudget("iter-out", MF, IterAfter));
    {
      unsigned Promotions = 0, Demotions = 0, StallEvents = 0;
      for (const auto &E : Ledger.Events) {
        switch (E.K) {
        case HaydnClosureGrowthEvent::Kind::Promotion:
          ++Promotions;
          break;
        case HaydnClosureGrowthEvent::Kind::Demote:
          ++Demotions;
          break;
        case HaydnClosureGrowthEvent::Kind::Stall:
          ++StallEvents;
          break;
        }
      }
      LLVM_DEBUG(dbgs()
                 << "HaydnLateConvergence: closure iteration " << Iter
                 << " events: promotions=" << Promotions
                 << " demotions=" << Demotions << " stalls=" << StallEvents
                 << " mbb-growth=" << Ledger.MBBGrowth << " (indirect "
                 << Before.IndirectCount << "->" << After.IndirectCount
                 << ")\n");
    }
    std::string Unaccounted = haydnClosureGrowthAccount(
        IterBefore, IterAfter, Ledger,
        haydn::hwloop::MaxSingleBranchGrowthBytes,
        haydn::hwloop::MaxHwLoopDemoteGrowthBytes);
    if (!Unaccounted.empty())
      report_fatal_error(
          "HaydnLateConvergence: prefix budget grew beyond the admitted "
          "closure vocabulary: " +
              Twine(Unaccounted) + " (function " + MF.getName() + ")",
          /*gen_crash_diag=*/false);

    // Monotone closure termination: repeat only on an upward event; an
    // iteration with no upward event (shrink, no change) closes the loop.
    if (!upwardEvent(Before, After)) {
      LLVM_DEBUG(dbgs() << "HaydnLateConvergence: closed after "
                        << Iter + 1 << " iteration(s) (no upward event)\n");
      Before = std::move(After);
      break;
    }
    Before = std::move(After);
  }

  // Refresh CFG analyses from the terminal CFG so preserved MDT/MLI match
  // what downstream Finalize/Verify observe.
  MDTWrapper.getDomTree().recalculate(MF);
  MLIWrapper.getLI().calculate(MDTWrapper.getDomTree());

  PrefixBudgetRecord FinalBudget = capturePrefixBudget(MF, TII);
  dumpPrefixBudget("final", MF, FinalBudget);
  // Entry-vs-final strict no-growth is deliberately NOT a fatal law (GR2.6
  // documented-legality rationale): the single S2 may redistribute encoded
  // bytes across pairs with no event at all — that is legal repacking, not
  // closure growth. The enforceable law is the per-iteration event
  // accounting above; this comparison stays as QoR telemetry only.
  const bool NoGrowth = EntryBudget.noGrowth(FinalBudget);
  LLVM_DEBUG({
    dbgs() << "HaydnLateConvergence: " << MF.getName()
           << " prefixes=" << FinalBudget.Pairs.size()
           << " no-growth=" << (NoGrowth ? 1 : 0)
           << " jalr-sites=" << Before.IndirectSites.size()
           << " hwloop-setups=" << Before.HwLoopSetups << '\n';
  });

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
  // pre-stamp inner passes (BranchRelaxation trampoline/RestoreBB,
  // RestoreBB-presched normalization) may split or insert blocks. After
  // the first Finalize stamp, post-stamp CFG creation is FATAL (the
  // D1.40 immediate closure check + the Verify seats); every post-stamp
  // mutation is in-block only.
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
