//===-- HaydnHWLoopContracts.h - Shared HWLOOP layout contracts -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Single source of truth for Haydn hardware-loop geometry and recovery policy.
// Included by formation (HaydnHardwareLoops) and pre-emit fixup (HaydnFixupHwLoops).
//
// ISA (Database SET_HWLOOP):
//   SET_HWLOOP sel, uimm6_off1, uimm12_off2, uimm16_cnt
//   HWLR_BEGIN[sel] = PC + (off1 << 2)   // max 252 B forward
//   HWLR_END[sel]   = PC + (off2 << 2)   // max 16380 B forward
//   HWLR_COUNT[sel] = cnt
//
// Geometry (golden Format E product law; cycle-primary, byte-derived):
//   1. Strict END: HWLR_END > HWLR_BEGIN (END is the last body cycle start).
//      Body parcels from BEGIN through END inclusive >= MinBodyBundles (3).
//      COUNT >= 1 when the selector is activated. A statically known
//      COUNT must also fit uimm16 (countMeetsFieldLaw); over-field
//      trips demote or fatal — they must not reach MC.
//   2. Primary hard rule — setup arithmetic (issue cycles):
//
//        cycle C:       SET issues
//        C+1 .. C+2:    InterveningCycles (= 2) complete following cycles
//        cycle C+3:     earliest legal BEGIN
//
//      SetupIssueDistance = Cycle(BEGIN) - Cycle(SET) >= 3
//      InterveningCycles  = SetupIssueDistance - 1     (= 2)
//
//      Formation / Fixup / lit count size-bearing parcels *after* SET:
//        Following >= InterveningCycles
//      Following counts any size-bearing parcel (real work or pad) — see
//      hwloop-following-non-nop-work.mir (proof seal).
//
//      Byte coincidence of the cycle floor (not the timing definition):
//        MinSetupBytes = InterveningCycles × productParcelBytes()
//        → PC_after_SET + MinSetupBytes <= PC_BEGIN
//      MinSetupIssueBytes = SetupIssueDistance × productParcelBytes()
//        → PC_SET_start + MinSetupIssueBytes <= PC_BEGIN (distance-3 PC delta)
//
//      Do not re-hide this pair behind the phrase "t-3" alone: that name is
//      ambiguous between distance-3 and three intervening cycles. Product law
//      is distance 3 with two intervening cycles.
//      Dedicated unit: unittests/Target/Haydn/HaydnHWLoopContractsTest.cpp.
//   3. Immediate Off1/Off2 scale is <<2 (displacement % 4). Absolute target
//      alignment is 2-byte bundle min, not absolute Align(4) label pads.
//
// AIE peer (AIEBaseInstrInfo::ZOLSupport + PostRA ExitSU latency + alignment):
//   LoopSetupDistance is enforced first by the post-RA scheduler (edge latency
//   from setup to region exit), then residual gaps are padded. Haydn:
//   SetupIssueDistance ExitSU edge + leaveRegion handleRegionConflicts +
//   Fixup deficit NOPs after SET (Following floor still InterveningCycles).
//
//   - SET is a real post-RA DAG SU (not a TII scheduling boundary).
//   - TII::isHardwareLoopSetupInstr covers logical/wide/member forms.
//   - ZOLSetupExitLatency raises the forward Artificial Exit edge by
//     SetupIssueDistance (Succs; drives ExitSU.TopReadyCycle top-down) and
//     the reverse Preds edge to SetupIssueDistance-1 (bot-up same-cycle-as-
//     Exit convention; BotCurr still covers the Following floor).
//   - leaveRegion handleRegionConflicts (AIE peer):
//       TopFinal = TopCurr + BotCurr
//       if ExitReady > TopFinal: Top.bumpCycle(ExitReady - BotCurr)
//       then bump Top while inter-zone scoreboard / Bot TopReadyCycle deps
//       still conflict; reflect growth as empty Top cycle-list pads.
//     Dual-zone BotCurr and inter-zone pads only lengthen the region end —
//     they never shrink Following after SET.
//   - Single-MI regions skipped by the list scheduler never run the flush;
//     Fixup residual deficit NOPs remain the safety net for those and for
//     short useful-window fill.
//   - Fixup is bundle-preserving (never unconditional SET unbundle).
//   - Every Fixup-created real MI (deficit pads, demote trip materialize,
//     stack-counter LD/ST glue, SUBI32+BNEZ_W, exit B) exact-commits via
//     commitLateProductCycle (shared with late Finalize / pack).
//   - Formation residual pads remain until useful-window fill + trailing-cycle
//     materialization prove redundant together (pad-drop deferred).
//   - Late-layout stable row (fixed BR → Fixup → BR → late Finalize/Verify):
//     Fixup sums MaxSingleBranchGrowthBytes over still-relaxable short
//     PC-relative sites in each SET→BEGIN and SET→END window and accepts
//     hardware form only when residual Off1/Off2 margins cover both sums.
//     The second BranchRelaxation must not invalidate that acceptance.
//
// Pipeline (no free AT invent; no ad hoc multi-BB conversion):
//   SCEV-proven IR + retained LoopStart→SET expansion before post-RA pack only;
//   incomplete retained seats reject fail-closed before mutation (fatal);
//   post-RA semantic rediscovery helpers stay deleted; multi-BB is a measured
//   SCEV/CFG extension (innermost single-latch/single-exit) after single-BB
//   qualify — never late physical rediscovery. Multi-stage SMS qualifies first
//   with hardware loops OFF; their combined interaction qualifies afterward.
//   Demote latch windows never emit scratch restore after BNEZ_W (SP leak);
//   stack-counter FI that is not a word-aligned simm6 element refuses
//   (no R0 address-temp XOR after the terminator); unverifiable countdown
//   addends are clobbers, not strip candidates.
//   Formation owns software-loop demotion (encodability or soft edge before
//   layout lock-in). Fixup: intervening pad → order-preserving shorten →
//   range recheck; residual generic setup fatals; late range may still call
//   the formation demote helper for already-committed wide forms.
//   Debug -haydn-enable-hwloop-demote=false refuses the soft-edge install
//   on a live body (Hexagon FixupHwLoops skip overlay) — callers fatal,
//   never erase-only once-through. Unpublished HWLR CSR addresses stay
//   unavailable (product programs HWLR only through SET_HWLOOP).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNHWLOOPCONTRACTS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNHWLOOPCONTRACTS_H

#include "HaydnBundlePlan.h"
#include <cstdint>

namespace llvm {
namespace haydn {
namespace hwloop {

//===----------------------------------------------------------------------===//
// Parcel size — generated product EncodedBytes only (no parallel magic)
//===----------------------------------------------------------------------===//
//
// All byte helpers go through bundle::productParcelBytes() /
// productBundlesToBytes(). Do not re-introduce free-standing 12/16 literals
// or a second size oracle in this header.
//===----------------------------------------------------------------------===//

/// Typed product parcel size (bytes). Alias of productParcelBytes().Value
/// (Format E registry/row EncodedBytes).
inline constexpr int64_t ProductParcelBytes =
    static_cast<int64_t>(bundle::productParcelBytes().Value);

static_assert(ProductParcelBytes ==
                  static_cast<int64_t>(bundle::productParcelBytes().Value),
              "hwloop ProductParcelBytes must equal productParcelBytes()");
static_assert(ProductParcelBytes > 0,
              "product parcel EncodedBytes must be positive");

// SET_HWLOOP offset / COUNT field widths (ISA DB).
inline constexpr unsigned Offset1Bits = 6;  // uimm6 → START
inline constexpr unsigned Offset2Bits = 12; // uimm12 → END
inline constexpr unsigned CountBits = 16;   // uimm16_cnt → HWLR_COUNT

// Displacement scale for Off1/Off2 immediates: field encodes (byte_delta >> 2).
// Byte distance must be divisible by DisplacementScale. Absolute target
// alignment remains the 2-byte bundle minimum — not absolute Align(4).
inline constexpr int64_t DisplacementScale = 4; // <<2

// Max forward PC-relative distances in bytes (field × DisplacementScale).
inline constexpr int64_t MaxStartOffsetBytes =
    ((static_cast<int64_t>(1) << Offset1Bits) - 1) * DisplacementScale; // 252
inline constexpr int64_t MaxEndOffsetBytes =
    ((static_cast<int64_t>(1) << Offset2Bits) - 1) * DisplacementScale; // 16380

static_assert(DisplacementScale == 4, "SET_HWLOOP off scale is <<2");
static_assert(MaxStartOffsetBytes % DisplacementScale == 0 &&
                  MaxEndOffsetBytes % DisplacementScale == 0,
              "offset ceilings must be scale-aligned");

// Safety margin so Fixup's size estimate does not pass a value that AsmPrinter
// later rejects (label placement, late bundles). Prefer demote over MC fail.
// Margin is a parcel count × product EncodedBytes (no absolute byte freeze).
inline constexpr int64_t Off1SafetyMarginBundles = 3;
inline constexpr int64_t Off1SafetyMarginBytes =
    bundle::productBundlesToBytes(
        static_cast<unsigned>(Off1SafetyMarginBundles));
inline constexpr int64_t MaxStartOffsetBytesSafe =
    MaxStartOffsetBytes - Off1SafetyMarginBytes;

static_assert(Off1SafetyMarginBytes ==
                  static_cast<int64_t>(Off1SafetyMarginBundles) *
                      ProductParcelBytes,
              "Off1 margin bytes must be parcels × product EncodedBytes");

//===----------------------------------------------------------------------===//
// Late-layout second-BR growth budget (stable row)
//===----------------------------------------------------------------------===//
//
// Pass order: each HaydnFixupHwLoops invocation (standalone PreEmit row, or
// every HaydnLateConvergence iteration after S2/stalls) rewrites Off1/Off2
// from the CURRENT layout inventory. There is no cached "layout-stable"
// hardware-form acceptance. During one invocation Fixup still charges a
// conservative absolute expansion budget for every still-relaxable short
// PC-relative branch in SET→BEGIN and SET→END (including nested windows).
// Hardware form is accepted only when residual Off1/Off2 margins cover both
// sums. Nested inner demote/resize re-checks the outer (inner-first).
//
// Worst case for one short B / cond → indirect path (TII
// insertIndirectBranch): LUI + ADDI32_W + JALR_W + optional emergency
// spill/pad parcel = 4 product parcels. Growth *bytes* follow product
// EncodedBytes; do not hard-code a Full-only absolute.
//
// Charge absolute expanded size (not net delta). Current branch size is
// already in Off; over-charging is fail-closed demote, never silent accept.
// Not still-relaxable (zero further layout growth under second BR):
// already-indirect JALR*, long-reach JAL*/calls, pure RET/BR_JT, ZOL metas.
//===----------------------------------------------------------------------===//

/// Product parcels for one still-relaxable short branch expanded to the
/// indirect materialization path (see insertIndirectBranch).
inline constexpr unsigned MaxSingleBranchGrowthParcels = 4;

/// Byte budget charged per still-relaxable site against Off1/Off2 residual
/// margin. Equals MaxSingleBranchGrowthParcels × productParcelBytes.
inline constexpr int64_t MaxSingleBranchGrowthBytes =
    bundle::productBundlesToBytes(MaxSingleBranchGrowthParcels);

static_assert(MaxSingleBranchGrowthParcels == 4,
              "second-BR growth: 4 parcels (LUI+ADDI+JALR+pad)");
static_assert(MaxSingleBranchGrowthBytes ==
                  static_cast<int64_t>(MaxSingleBranchGrowthParcels) *
                      ProductParcelBytes,
              "growth bytes must be parcels × product EncodedBytes");

/// Residual BranchRelaxation distance buffer after named growth is charged
/// in getInstSizeInBytes. Equals one insertIndirectBranch sequence — never
/// a free-standing 200/1024. TII `haydn-branch-relax-safety-buffer` must
/// initialize from this value (HaydnInstrInfo.cpp cl::init).
inline constexpr int64_t BranchRelaxSafetyBufferBytes =
    MaxSingleBranchGrowthBytes;
static_assert(BranchRelaxSafetyBufferBytes == MaxSingleBranchGrowthBytes,
              "BR safety buffer is the contracts growth budget");
static_assert(BranchRelaxSafetyBufferBytes ==
                  bundle::productBundlesToBytes(MaxSingleBranchGrowthParcels),
              "BR safety buffer is 4 product parcels, not 200/1024");
static_assert(BranchRelaxSafetyBufferBytes != 200 &&
                  BranchRelaxSafetyBufferBytes != 1024,
              "BR safety buffer is not a free-standing 200/1024");

//===----------------------------------------------------------------------===//
// Setup arithmetic (width-independent issue-cycle inequality)
//===----------------------------------------------------------------------===//
//
// Spec / golden: earliest BEGIN is three issue cycles after SET (t-3).
// AIE peer of ZOLSupport::LoopSetupDistance (AIE2 uses 7 bundles to LEND).
//
// Named pair — do not collapse back into a single ambiguous "3":
//   SetupIssueDistance : Cycle(BEGIN) - Cycle(SET) lower bound (= 3)
//   InterveningCycles  : complete following cycles between SET and BEGIN (= 2)
//
// Formation, Fixup, and lit enforce Following >= InterveningCycles.
// MinSetupBytes is the EncodedBytes coincidence of that floor only.
//===----------------------------------------------------------------------===//

/// Minimum Cycle(BEGIN) - Cycle(SET). Earliest legal BEGIN is at SET+3.
inline constexpr unsigned SetupIssueDistance = 3;

/// Complete size-bearing cycles after SET before BEGIN.
/// Equals SetupIssueDistance - 1. This is what Following/countFollowingBundles
/// compares against (not SetupIssueDistance itself).
inline constexpr unsigned InterveningCycles = SetupIssueDistance - 1; // 2

static_assert(SetupIssueDistance == 3,
              "Setup arithmetic: SetupIssueDistance is 3 issue cycles");
static_assert(InterveningCycles == 2,
              "Setup arithmetic: InterveningCycles is 2 following cycles");
static_assert(InterveningCycles + 1 == SetupIssueDistance,
              "Setup arithmetic: InterveningCycles = SetupIssueDistance - 1");

/// Historical name for the Following floor. Alias of InterveningCycles — not
/// SetupIssueDistance. New code should prefer InterveningCycles by name.
inline constexpr unsigned MinSetupBundles = InterveningCycles;

/// W61 scheduler tail credit (AIE RegionEndEdges
/// LoopSetupDistance - ZOLBundlesCount analog): the in-region SET→ExitSU
/// edge latency owed after crediting the SET-MBB tail parcels — the
/// size-bearing parcels between the scheduling region's end and the first
/// terminator (the unconditional B to the header, a guarding conditional
/// branch, call-boundary parcels). Those parcels lie between the SET cycle
/// and HWLR_BEGIN on every activation path, so the region only owes the
/// remainder of SetupIssueDistance. AIE credits ZOL body bundles because
/// its law runs to LEND; the golden Haydn law runs to BEGIN
/// (VLIW_Engine_Compiler_Constraints "Setup Timing"), so the credit is the
/// tail, never body parcels. Following/StartOff walks count the same first
/// size-bearing terminator so the credit is not re-padded post-sched.
inline constexpr unsigned setupGapAfterTailCredit(unsigned TailParcels) {
  return TailParcels >= SetupIssueDistance ? 0
                                           : SetupIssueDistance - TailParcels;
}

static_assert(setupGapAfterTailCredit(0) == SetupIssueDistance,
              "no tail: region owes the full distance");
static_assert(setupGapAfterTailCredit(1) == SetupIssueDistance - 1,
              "one tail parcel (unconditional B preheader) credits one cycle");
static_assert(setupGapAfterTailCredit(InterveningCycles) == 1,
              "two tail parcels leave the SET cycle itself to the region");
static_assert(setupGapAfterTailCredit(SetupIssueDistance) == 0,
              "tail covers the whole distance: region owes nothing");
static_assert(setupGapAfterTailCredit(10) == 0,
              "over-credit clamps at zero, never under-reserves");

// Min setup distance in bytes (InterveningCycles × product EncodedBytes).
// Single EncodedBytes path; AIE sums Format->getSize(). Timing law is the
// cycle pair above, not this byte product under a mixed-width fantasy.
inline constexpr int64_t MinSetupBytes =
    bundle::productBundlesToBytes(InterveningCycles);
static_assert(MinSetupBytes ==
                  static_cast<int64_t>(InterveningCycles) * ProductParcelBytes,
              "MinSetupBytes must be InterveningCycles × product parcel");
static_assert(MinSetupBytes ==
                  bundle::productBundlesToBytes(MinSetupBundles),
              "MinSetupBytes must match MinSetupBundles × product parcel");

/// PC delta from SET start to BEGIN start when distance == SetupIssueDistance.
/// Equals SetupIssueDistance × productParcelBytes (distinct from MinSetupBytes,
/// which measures the intervening span after SET's end).
inline constexpr int64_t MinSetupIssueBytes =
    bundle::productBundlesToBytes(SetupIssueDistance);
static_assert(MinSetupIssueBytes ==
                  static_cast<int64_t>(SetupIssueDistance) * ProductParcelBytes,
              "MinSetupIssueBytes must be SetupIssueDistance × product parcel");
static_assert(MinSetupIssueBytes == MinSetupBytes + ProductParcelBytes,
              "issue PC delta = intervening span + one parcel (the SET cycle)");

/// CB-164 anchor law: MC anchors HWLoopOff1/Off2 at the SET parcel base.
/// HaydnAsmBackend::evaluateFixup seeds Value = Abs % Parcel so
/// MCAssembler's PC-rel subtract lands on align_down(fixup_loc, Parcel) —
/// the parcel the SET member encodes in. A layout walk that measures
/// after the SET cycle under-charges one parcel: convert such an
/// after-SET span into the encoded (SET-anchored) displacement by adding
/// the SET cycle's committed EncodedBytes. Inverse of the subtract above;
/// the sole arithmetic bridge between MI-level walks and MC fixups.
inline constexpr int64_t anchoredFromAfterSet(int64_t AfterSetOff,
                                              int64_t SetParcelBytes) {
  return AfterSetOff < 0 ? AfterSetOff : AfterSetOff + SetParcelBytes;
}
static_assert(anchoredFromAfterSet(-1, ProductParcelBytes) == -1,
              "unknown (-1) stays unknown, never a silent +parcel");
static_assert(anchoredFromAfterSet(0, ProductParcelBytes) ==
                  ProductParcelBytes,
              "target at the next parcel start is one full SET parcel away");

//===----------------------------------------------------------------------===//
// Body / END / COUNT product law
//===----------------------------------------------------------------------===//
//
// Body parcels from BEGIN through END inclusive >= MinBodyBundles.
// END addresses the last body cycle (start), not the byte after the body.
// Strict: EndOff > StartOff. For body=N parcels, EndOff - StartOff =
// (N-1) × productParcelBytes. Min body span (N=3) = 2 × parcel.
// COUNT >= MinCount when activated.
//===----------------------------------------------------------------------===//

/// Minimum size-bearing body parcels (BEGIN through END inclusive).
inline constexpr unsigned MinBodyBundles = 3;

/// Byte distance from BEGIN start to END start for a min-length body.
/// (MinBodyBundles - 1) × productParcelBytes.
inline constexpr int64_t MinBodySpanBytes =
    bundle::productBundlesToBytes(MinBodyBundles - 1);

/// Minimum HWLR_COUNT when a hardware loop is activated.
inline constexpr unsigned MinCount = 1;

/// Maximum statically known COUNT that fits the uimm16 field.
/// Over-field trips demote (or fatal when demote is disabled) — they must
/// not reach MC as an unencodable immediate. Register-trip SET_HWLOOP_F2_W
/// is not bound by this field (trip lives in a GPR).
inline constexpr int64_t MaxCountImm =
    (static_cast<int64_t>(1) << CountBits) - 1; // 65535

static_assert(MinBodyBundles == 3, "body floor is 3 parcels");
static_assert(MinBodySpanBytes ==
                  static_cast<int64_t>(MinBodyBundles - 1) * ProductParcelBytes,
              "MinBodySpanBytes = (MinBodyBundles-1) × product parcel");
static_assert(MinCount == 1, "activated COUNT must be >= 1");
static_assert(CountBits == 16, "SET_HWLOOP cnt field is uimm16");
static_assert(MaxCountImm == 65535, "uimm16 COUNT ceiling is 65535");

//===----------------------------------------------------------------------===//
// Product selector domain (retained-state path)
//===----------------------------------------------------------------------===//
//
// SET_HWLOOP carries a selector that names which HWLR slot is programmed.
// Only sel in {0,1} is available on the product retained-state path:
//   * InnermostProductSelector (= 0): golden manual §6.4/§6.5 — nested
//     loops use HWLR_*[0] for the inner loop, HWLR_*[1] for the outer;
//     single-BB ZOL expand arms the inner seat.
//   * OuterProductSelector     (= 1): reserved-no-consumer product seat
// Values outside this domain stay unavailable and fail-closed at expand/fixup
// (never invent extra CSR/selector identities). Multi-block formation is a
// separate SCEV/CFG extension and does not widen the selector domain.
//===----------------------------------------------------------------------===//

/// Inclusive product selector range. Only these values may arm SET_HWLOOP.
inline constexpr int64_t ProductSelectorMin = 0;
inline constexpr int64_t ProductSelectorMax = 1;

/// Preferred selector for innermost single-BB Role-A expand. Golden manual
/// (§"two independent hardware loop registers"): HWLR_*[0] is the INNER
/// loop seat in a nesting; single-BB Role-A expand is innermost by
/// construction, so it arms sel 0.
inline constexpr int64_t InnermostProductSelector = 0;

/// Reserved-no-consumer product selector 1 (golden outer-loop seat). Not a
/// nesting free-list companion; no live consumer. Innermost expand arms
/// selector 0 only.
inline constexpr int64_t OuterProductSelector = 1;

/// True iff Sel is in the product selector domain {0,1}.
inline constexpr bool isProductSelector(int64_t Sel) {
  return Sel >= ProductSelectorMin && Sel <= ProductSelectorMax;
}

static_assert(isProductSelector(InnermostProductSelector),
              "innermost product selector must be in domain");
static_assert(isProductSelector(OuterProductSelector),
              "outer product selector must be in domain");
static_assert(InnermostProductSelector == 0 && OuterProductSelector == 1,
              "golden manual: HWLR_*[0] = inner loop seat, HWLR_*[1] = outer");
static_assert(InnermostProductSelector != OuterProductSelector,
              "inner/outer seats are distinct");
static_assert(!isProductSelector(2) && !isProductSelector(3) &&
                  !isProductSelector(-1),
              "out-of-domain selectors stay unavailable");

//===----------------------------------------------------------------------===//
// Product CSR / relocation / COUNT fail-closed helpers
//===----------------------------------------------------------------------===//
//
// Product programs HWLR state only through SET_HWLOOP with a product selector.
// Free CSR-address invent for HWLR_BEGIN/END/COUNT is unavailable and must
// never be emitted by the retained-state path. Displacement bytes that are not
// divisible by DisplacementScale, or that exceed field ceilings, demote /
// reject fail-closed rather than inventing an alternate encoding.
//===----------------------------------------------------------------------===//

/// True iff \p OffBytes is a non-negative, scale-aligned displacement that
/// fits in a uimm\p FieldBits field after >>2.
inline constexpr bool isEncodableDisplacement(int64_t OffBytes,
                                              unsigned FieldBits) {
  if (OffBytes < 0 || DisplacementScale <= 0)
    return false;
  if ((OffBytes % DisplacementScale) != 0)
    return false;
  const int64_t Max =
      ((static_cast<int64_t>(1) << FieldBits) - 1) * DisplacementScale;
  return OffBytes <= Max;
}

/// True iff START/END displacements are both encodable under Off1/Off2 law.
inline constexpr bool offsetsMeetImmRelocLaw(int64_t StartOff, int64_t EndOff) {
  return isEncodableDisplacement(StartOff, Offset1Bits) &&
         isEncodableDisplacement(EndOff, Offset2Bits);
}

/// True iff a statically known COUNT may activate a hardware loop.
inline constexpr bool countMeetsMinLaw(int64_t Count) {
  return Count >= static_cast<int64_t>(MinCount);
}

/// True iff a statically known COUNT is in [MinCount, MaxCountImm].
/// Imm-trip SET_HWLOOP_W must satisfy this or demote / fatal. Over-field
/// values used to fall through to MC ("relocation / imm out of range")
/// instead of the recovery ladder.
inline constexpr bool countMeetsFieldLaw(int64_t Count) {
  return countMeetsMinLaw(Count) && Count <= MaxCountImm;
}

static_assert(isEncodableDisplacement(0, Offset1Bits),
              "zero START displacement is encodable");
static_assert(isEncodableDisplacement(MaxStartOffsetBytes, Offset1Bits),
              "max START displacement is encodable");
static_assert(!isEncodableDisplacement(1, Offset1Bits),
              "non-scale START displacement stays unavailable");
static_assert(!isEncodableDisplacement(MaxStartOffsetBytes + DisplacementScale,
                                        Offset1Bits),
              "over-max START displacement stays unavailable");
static_assert(offsetsMeetImmRelocLaw(MinSetupBytes,
                                     MinSetupBytes + MinBodySpanBytes),
              "min legal geometry must meet reloc law");
// CB-164 instance, frozen: after-SET 252 (the uimm6 ceiling) encodes as
// 252 + 12 = 264 -> 264 >> 2 = 66 > 63. The accepted-after-SET walk must
// not survive; the anchored walk demotes at this geometry. One parcel
// short is exactly the ceiling and must stay encodable.
static_assert(!isEncodableDisplacement(
                  anchoredFromAfterSet(MaxStartOffsetBytes, ProductParcelBytes),
                  Offset1Bits),
              "after-SET uimm6-ceiling span is NOT encodable once anchored");
static_assert(isEncodableDisplacement(
                  anchoredFromAfterSet(MaxStartOffsetBytes - ProductParcelBytes,
                                       ProductParcelBytes),
                  Offset1Bits),
              "one-parcel-short after-SET span is exactly the ceiling");
static_assert(countMeetsMinLaw(MinCount) && !countMeetsMinLaw(0),
              "COUNT floor is MinCount");
static_assert(countMeetsFieldLaw(MinCount) &&
                  countMeetsFieldLaw(MaxCountImm) &&
                  !countMeetsFieldLaw(0) &&
                  !countMeetsFieldLaw(MaxCountImm + 1),
              "COUNT field law is [MinCount, uimm16 max]");

// Unpublished HWLR CSR window already named by haydnHwloopCsrAddr
// (HaydnPortModel.h:288-297). Product programs HWLR only through
// SET_HWLOOP with a product selector. CSRW to this window is unavailable
// — do not invent a catalog address or emit CSR writes here.
inline constexpr int64_t UnpublishedHwlrCsrAddrMin = 0x20;
inline constexpr int64_t UnpublishedHwlrCsrAddrMax = 0x25;

inline constexpr bool isUnpublishedHwlrCsrAddress(int64_t Addr) {
  return Addr >= UnpublishedHwlrCsrAddrMin && Addr <= UnpublishedHwlrCsrAddrMax;
}

static_assert(!isUnpublishedHwlrCsrAddress(InnermostProductSelector) &&
                  !isUnpublishedHwlrCsrAddress(OuterProductSelector),
              "product selectors are not CSR addresses");
static_assert(isUnpublishedHwlrCsrAddress(0x20) &&
                  isUnpublishedHwlrCsrAddress(0x25) &&
                  !isUnpublishedHwlrCsrAddress(0x1f) &&
                  !isUnpublishedHwlrCsrAddress(0x26),
              "unpublished HWLR CSR window stays 0x20-0x25 fail-closed");

/// Inclusive body parcel count from StartOff/EndOff (END = last cycle start).
/// Returns 0 if offsets are unordered or unaligned to the product parcel.
inline constexpr unsigned bodyParcelsFromOffsets(int64_t StartOff,
                                                 int64_t EndOff) {
  if (EndOff < StartOff || ProductParcelBytes <= 0)
    return 0;
  int64_t Delta = EndOff - StartOff;
  if (Delta % ProductParcelBytes != 0)
    return 0;
  return static_cast<unsigned>(Delta / ProductParcelBytes) + 1u;
}

/// True iff END > BEGIN and body parcels meet MinBodyBundles.
inline constexpr bool bodyMeetsMinLaw(int64_t StartOff, int64_t EndOff) {
  if (EndOff <= StartOff)
    return false;
  return bodyParcelsFromOffsets(StartOff, EndOff) >= MinBodyBundles;
}

//===----------------------------------------------------------------------===//
// Late revalidation / nested cascade (post-S2 inventory)
//===----------------------------------------------------------------------===//
//
// Peer: AIEBaseHardwareLoops.cpp:304-306 processLoop walks inner MachineLoops
// first, then the outer. HexagonFixupHwLoops.cpp:97-148 is a two-pass offset
// census then LOOP→LOOPext swap that does not change inner size, so a later
// outer re-walk is unnecessary. Haydn demote/pad mutates EncodedBytes, so the
// Hexagon census cannot be the acceptance; Haydn overlays AIE inner-first on
// the SET→END layout window (no MLI — Haydn loop membership is CFG
// successor/pred, not MachineLoopInfo).
//
// Laws:
//  * Every retained SET is revalidated from the live post-S2 inventory.
//  * Inner demote or resize (deficit pads) re-checks every outer whose
//    SET→END window contains the inner setup.
//  * Demotion is monotone: the number of hardware setups never increases.
//  * Demote spill/reload homes are the pre-PEI reserved FIs
//    (HwLoopDemoteSaveFI / PostRAScratchFI / BranchRelaxationScratchFI);
//    CreateStackObject after frame finalization is a contract break.
//  * Live trip-value, exact FixedStack MMOs on those homes, and latch
//    Header membership are preservation gates on a successful demote —
//    not cached Off1/Off2 from a prior invocation.
//
// Bound: one inner-first wave plus one re-check of remaining setups per
// original setup. Exhaustion without a no-mutation wave is a hard
// diagnostic (finite; selectors {0,1} cap product nesting at 2).
//===----------------------------------------------------------------------===//

/// Inclusive wave budget for inner-first revalidation of \p NumSetups
/// retained hardware-loop setups. Empty inventory still runs one no-op
/// collect so a post-S2 function with zero SETs is not a special case.
inline constexpr unsigned nestedCascadeBound(unsigned NumSetups) {
  return NumSetups + 1u;
}

/// True iff a later inventory may keep a hardware setup. Demotion is
/// monotone: retained setups only shrink (or stay), never re-form.
inline constexpr bool setupsMonotone(unsigned Before, unsigned After) {
  return After <= Before;
}

static_assert(nestedCascadeBound(0) == 1,
              "empty inventory still one no-op collect wave");
static_assert(nestedCascadeBound(1) == 2, "one setup: fixup + recheck");
static_assert(nestedCascadeBound(2) == 3,
              "inner+outer: inner-first wave + recheck remaining");
static_assert(setupsMonotone(2, 1) && setupsMonotone(2, 2) &&
                  !setupsMonotone(1, 2),
              "HWLoop demote is monotone (setups only shrink)");
static_assert(ProductSelectorMax - ProductSelectorMin + 1 == 2,
              "product nesting depth follows the {0,1} selector domain");

} // namespace hwloop
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNHWLOOPCONTRACTS_H
