//===-- HaydnLateConvergence.h - monotone late closure driver -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// GR2.6 monotone closure driver (contracts/pipeline.md "Exact range, HWLoop,
// and alignment closure"). When -haydn-sms2 is on, this pass owns the
// W68.2R S2 seat at addPostBBSections: after every common executable writer
// (outliner/split/BB sections) and before the closure Finalize+Verify. It
// chooses current physical MIs and runs the closure shape
//
//   S2 EXACTLY ONCE per driver entry (fresh PostMachineScheduler invocation)
//   then repeat until no upward event:
//     regenerate stalls/alignment (fresh HaydnLatencyStalls)
//     -> validate or monotonically demote HWLoops (fresh HaydnFixupHwLoops,
//        innermost-first; fixupOne recomputes Off1/Off2 from CURRENT layout,
//        so each invocation IS the post-mutation revalidation)
//     -> BranchRelaxation LAST in the mutating iteration
//
// There is NO reschedule-after-mutation arm (constraints 9/11): once S2 has
// committed, no range/HWLoop mutation inside the loop is answered with
// another scheduling pass. An iteration repeats only on an upward event —
// a JALR promotion (IndirectCount up), an HWLoop demote insertion
// (HwLoopSetups down; per-pair encoded bytes up), or MBB evidence of a
// BranchRelaxation split (MBB count up / key movement). An iteration with
// no upward event terminates the loop. The bound
// MaxIterations = (#cond + #hwloop + #indirect + 2) exhaustion stays the
// backstop hard diagnostic.
//
// Enforced no-growth law (GR2.6, exact accounting D1.41): growth of any
// consumed source/target pair's charge across ONE closure iteration is
// legal only when accounted for by the admitted vocabulary, and the
// vocabulary grants credit ONLY on the pair spans an event's parcels
// actually touch:
//   * per-site short-branch promotion <= haydn::hwloop::MaxSingleBranch
//     GrowthBytes counted over the pair span via the ONE shared
//     isStillRelaxableShortBranch classifier;
//   * demote insertion <= haydn::hwloop::MaxHwLoopDemoteGrowthBytes per
//     lost setup;
//   * stall-class parcel insertion (regenerated stall parcels, the GR2.7
//     in-block long-branch rewrite, retained-loop Fixup deficit pads)
//     admitted up to the exact parcels inserted on the affected MBBs;
//   * BranchRelaxation splits evidenced by MBB-count growth for
//     appeared/vanished keys.
// An event on one span never legitimizes growth of an unrelated prefix:
// unrelated growth with a non-empty ledger is the fatal. Unaccounted
// growth is a named report_fatal_error (GenCrashDiag=false).
// The parcel-rounded worst-case alignment pad is reserved (not the raw
// ARM unknownPadding under-reserve). Driver-entry vs final no-growth is
// deliberately NOT a fatal law: the single S2 may redistribute bytes with
// no event at all; that comparison stays as LLVM_DEBUG QoR telemetry.
//
// Every mutator is an existing pass instantiated fresh per iteration — there
// is no second scheduler, no fingerprint, no accumulated pad state, and no
// persistent frontier here.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNLATECONVERGENCE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNLATECONVERGENCE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/Alignment.h"
#include <cstdint>
#include <utility>

namespace llvm {

class MachineInstr;
class TargetInstrInfo;

/// Monotone closure loop for one function. Returns true if any iteration
/// changed the function. Exposed for the driver pass below; the loop itself
/// is reusable by the pass only (single seat).
bool runHaydnLateConvergence(MachineFunction &MF,
                             MachineFunctionPass &DriverPass);

//===----------------------------------------------------------------------===//
// Prefix budget law (internal, unit-testable without a .cpp)
//===----------------------------------------------------------------------===//
//
// Per source/target pair (branch dest or HWLoop START/END) that range
// decisions consume: encoded bytes via TII->getInstSizeInBytes plus the
// parcel-rounded worst-case alignment pad. GR2.6/D1.41 exact-accounted
// no-growth: charge growth across one closure iteration is legal only
// within the admitted vocabulary (promotion / demote / stall / split
// evidence), and only on the pair spans the event's parcels actually
// touch — an event never grants credit to an unrelated prefix.
//===----------------------------------------------------------------------===//

/// Identity of one consumed source/target pair: (SrcMBB, DstMBB).
using HaydnPrefixKey = std::pair<unsigned, unsigned>;

/// Measured charge of one source/target pair span.
struct HaydnPrefixPairRecord {
  unsigned Src = 0;
  unsigned Dst = 0;
  uint64_t EncodedBytes = 0;
  uint64_t AlignPad = 0;
  Align MaxAlign = Align(1);
  /// Still-relaxable short-branch sites in the span (shared classifier).
  unsigned RelaxableSites = 0;
  /// Retained HWLoop setups in the span (demote insertion evidence).
  unsigned HwLoopSetups = 0;

  uint64_t charge() const { return EncodedBytes + AlignPad; }
};

/// Snapshot of every consumed pair's charge for one point in the closure.
struct HaydnPrefixBudgetRecord {
  SmallVector<HaydnPrefixPairRecord, 8> Pairs;
  /// JALR/JALR_W long-form site count at this point (the same census law
  /// ConvergenceSnapshot counts; one count per snapshot, not a global
  /// boolean). A rise across a stable-key window is the census evidence
  /// that the window promoted a far site — which CONSUMES the promoted
  /// pair key (the long form carries no range-pair MBB operand).
  unsigned IndirectCount = 0;

  /// Entry-vs-final strict no-growth over surviving keys (telemetry only —
  /// the single S2 may redistribute bytes with no event).
  bool noGrowth(const HaydnPrefixBudgetRecord &Final) const {
    DenseMap<HaydnPrefixKey, uint64_t> FinalCharge;
    for (const HaydnPrefixPairRecord &P : Final.Pairs)
      FinalCharge[{P.Src, P.Dst}] = P.charge();
    for (const HaydnPrefixPairRecord &P : Pairs) {
      auto It = FinalCharge.find({P.Src, P.Dst});
      if (It == FinalCharge.end())
        continue;
      if (It->second > P.charge())
        return false;
    }
    return true;
  }
};

/// One attributed growth event (D1.41): \a Bytes net-new charge admitted
/// on exactly one scope — either ONE pair interval (the stalls/Fixup/
/// LongBranchNormalize windows, whose pair keys are stable because those
/// arms never renumber or create blocks, so the measured charge delta per
/// span is exact, alignment-pad ripple included), or a set of MBBs (the
/// BranchRelaxation window, which RenumbersBlocks at entry and so cannot
/// be pair-matched; its promotions stay capped by the vocabulary bounds).
/// An event with NO scope admits nothing (fail-closed: there is no
/// whole-iteration grant of any class).
struct HaydnClosureGrowthEvent {
  enum class Kind : uint8_t {
    /// Short-branch promotion (JALR long form): net byte growth of the
    /// promoted site's MBB in the BR window, capped at
    /// MaxSingleBranchGrowthBytes per span relaxable site by the law.
    Promotion,
    /// HWLoop demote insertion (or retained-loop Fixup deficit pads): the
    /// Fixup window's exact charge growth of the affected span, capped at
    /// MaxHwLoopDemoteGrowthBytes per span retained setup by the law.
    Demote,
    /// Stall-class insertion: regenerated stall parcels and the GR2.7
    /// in-block long-branch rewrite — the stalls/normalize windows' exact
    /// charge growth of the affected span.
    Stall,
  };
  Kind K = Kind::Stall;
  uint64_t Bytes = 0;
  /// MBB scope: credits every pair span containing one of these MBBs.
  /// Empty AND no exact pair = unscoped = admits nothing.
  SmallVector<unsigned, 2> AffectedMBBs;
  /// Pair scope: credits exactly this pair key. Set only by windows whose
  /// keys are stable across the arm (no renumbering inside the window).
  bool HasExactPair = false;
  unsigned PairSrc = 0;
  unsigned PairDst = 0;

  HaydnClosureGrowthEvent() = default;
  HaydnClosureGrowthEvent(Kind K, uint64_t Bytes,
                          SmallVector<unsigned, 2> AffectedMBBs)
      : K(K), Bytes(Bytes), AffectedMBBs(std::move(AffectedMBBs)) {}
  static HaydnClosureGrowthEvent forPair(Kind K, uint64_t Bytes, unsigned Src,
                                         unsigned Dst) {
    HaydnClosureGrowthEvent E(K, Bytes, {});
    E.HasExactPair = true;
    E.PairSrc = Src;
    E.PairDst = Dst;
    return E;
  }
};

/// Admitted closure-event vocabulary for one iteration (GR2.6 exact
/// accounting, D1.41). The driver fills this ledger from the mutating
/// passes' observable per-window deltas; the accounting predicate below
/// is the enforceable no-growth law. No global booleans/counts: each event
/// names its exact scope (one pair interval or its MBBs) and its exact
/// net bytes, and credit lands only there.
struct HaydnClosureEventLedger {
  /// Attributed events this iteration (promotion / demote / stall class).
  SmallVector<HaydnClosureGrowthEvent, 4> Events;
  /// MBB-count growth this iteration (BranchRelaxation split evidence).
  unsigned MBBGrowth = 0;

  bool anyEvent() const { return !Events.empty() || MBBGrowth; }
};

/// Enforceable per-iteration no-growth law (GR2.6 exact accounting,
/// D1.41). Returns an empty string when the growth of every common key is
/// accounted for by the admitted vocabulary attributed to that span;
/// otherwise a named diagnostic for report_fatal_error. Growth budget for
/// one common key comes ONLY from events whose affected MBBs lie inside
/// that key's span (promotions/demotes additionally capped by the span's
/// pre-iteration sites/setups). An unrelated prefix that grows while an
/// event fired elsewhere gets a zero budget and fatals.
/// vanished/appeared keys are evidence-admitted: a key may vanish or
/// appear only when the ledger has a structural event (MBB growth from a
/// BranchRelaxation split renumbering blocks, a promotion whose
/// insertIndirectBranch trampoline/restore blocks introduce new keys, or a
/// renumber rotation — BranchRelaxation RenumbersBlocks unconditionally,
/// even on no-change iterations). A renumber rotation is accounted as ONE
/// aggregate span: the total appeared charge must stay within the total
/// vanished charge plus the admitted budgets of the vanished keys (a pure
/// retarget with an empty ledger is its zero-growth case). A key change
/// with an empty ledger and no accounted migration is unaccounted.
std::string haydnClosureGrowthAccount(
    const HaydnPrefixBudgetRecord &Before, const HaydnPrefixBudgetRecord &After,
    const HaydnClosureEventLedger &Ledger, int64_t MaxSingleBranchGrowthBytes,
    int64_t MaxHwLoopDemoteGrowthBytes);

/// Span-membership law for exact attribution (D1.41): an MBB is inside the
/// pair span [Src..Dst] exactly when the shared span walk (countSpanEvents
/// / measurePrefix) visits it — the numeric interior of the two endpoints
/// (Src's own bytes are the span start, Dst terminates it; a self-pair
/// span is exactly the Src block). Exposed for the driver's event
/// attribution and the unit tests.
bool haydnSpanContainsMBB(unsigned Src, unsigned Dst, unsigned MBB);

/// The pipeline-registered driver pass. Seated in addPostBBSections under
/// -haydn-sms2 (after the common executable tail; before closure Finalize).
/// Replaces the one-shot S2+BR pair.
class HaydnLateConvergencePass : public MachineFunctionPass {
public:
  static char ID;
  HaydnLateConvergencePass();

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "Haydn Late Layout Convergence Loop";
  }

  /// Required: MLI/MDT/AA/TargetPassConfig for inner S2. Preserves AA and
  /// TargetPassConfig (IR-level / immutable). Preserves MDT/MLI after
  /// in-place recalculate. Does not preserve CFG: inner BranchRelaxation
  /// and HWLoop demote may split or insert blocks.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

FunctionPass *createHaydnLateConvergencePass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNLATECONVERGENCE_H
