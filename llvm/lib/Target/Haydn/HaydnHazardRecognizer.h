//===-- HaydnHazardRecognizer.h - Haydn scoreboard hazard recognizer ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Haydn scoreboard hazard recognizer — the AIE-style
// resource model for the MachineScheduler (post-RA commit + pre-RA 
// feasibility with MRI-correct vreg port demand).
//
// Design
//======//
// Format E resources are seven execution units (LOADSTORE0, LOAD1, ALU0–2,
// MAC0–1), plus pooled GPR/DR/AR/SFR register-file ports. Up to three
// entries may issue per cycle, subject to:
// * unit injectivity (no two entries map to the same unit)
// * the GPR 4R2W, DR 8R3W, AR 2R2W port budgets (live SFR 2R1W vocabulary)
// * at most one SFR writer per cycle (dead implicit-def $sfr still counts)
// * latency-bound data dependencies (DAG SDep edges plus the dest-read
//   window below so a no-interlock machine cannot issue a reader inside
//   a producer's Data_Latency)
//
// Encoded entry indices are not processor-resource bits; entry matching is
// format legality. ALU0 and LOADSTORE0 are independent units and may
// co-issue when assigned to different entries.
//
// This recognizer drives a ResourceScoreboard<HaydnFuncUnitWrapper> that
// records per-cycle unit occupancy and pooled port demand. The
// MachineScheduler consults it via SchedBoundary::checkHazard
// (MachineScheduler.cpp:2700), which calls getHazardType on every ready SUnit.
//
// Register Data_Latency is primarily a DAG SDep. Haydn has no interlock, so
// the post-RA scoreboard also serializes dest readers (and SIN_COS/ARCTAN
// dest writers / unit occupancy) that would land inside a published window.
// Pre-RA is OnlyBottomUp and must not book those windows — RecedeCycle
// growing remaining is a compile hang (ready set recedes forever).
//
// Why this is safe vs the / UAF
//=====================================//
// documented a use-after-free in upstream
// ConvergingVLIWScheduler::SchedulingCost (VLIWMachineScheduler.cpp) that was
// triggered by modeling GPR ports as DFA FuncUnit choice-sets (multi-stage
// InstrStages with NextCycles_=0). This recognizer takes a completely
// different path: it is consumed by PostGenericScheduler via
// ScheduleDAGMI/ScheduleDAGMILive, NEVER by VLIWMachineScheduler. The buggy
// ConvergingVLIWScheduler class is never instantiated on this path. So
// rich resource modeling (slots + 4R2W ports) is reintroduced without
// re-triggering the UAF. See.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNHAZARDRECOGNIZER_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNHAZARDRECOGNIZER_H

#include "HaydnBundle.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnPortModel.h"
#include "HaydnResourceScoreboard.h"
#include "HaydnStaticBitSet.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <optional>
#include <unordered_map>

namespace llvm {

class MachineInstr;
class MachineFunction;
class SUnit;
class TargetInstrInfo;
class HaydnAlternateDescriptors;
class TargetRegisterInfo;
class TargetSubtargetInfo;
class Value;

// AIE AIEHazardRecognizer.h:34-42 MemoryObjectsBits / MemoryObjectPair.
// Peer struct only. Haydn golden lets two loads of one object share a
// cycle (LOADSTORE0 + LOAD1, p[0]/p[1]). Wait-cycle same-object reject
// needs an admitted ISA row and is not booked.
using MemoryObjectsBits = uint64_t;

struct MemoryObjectPair {
  MemoryObjectsBits Load = 0;
  MemoryObjectsBits Store = 0;
};

struct MemoryObjectEnumerator {
private:
  std::unordered_map<const Value *, unsigned> ObjectNumberingMap;
  unsigned ObjectCounter = 0;
  bool isFull() const;

public:
  std::optional<unsigned> getObjectNumber(const Value *Object);
};

// AIE peer: AIEHazardRecognizer.cpp:278-314 applyFormatOrdering —
// walk Format.getSlots(), Bundle.at(Slot), removeFromBundle+insert+
// bundleWithPred, then finalizeBundle. : free function in the same
// namespace as AIE (llvm::), names match for the AIE clone path.
void applyFormatOrdering(Haydn::MachineBundle &Bundle, const VLIWFormat &Format,
                         MachineBasicBlock::iterator InsertPoint);

// Field-ordered (emit-order) member list that applyFormatOrdering produces.
// Single source of truth for the member order the encoder / AsmPrinter /
// hardware observe; the no-forwarding intra-bundle RAW law is validated on
// THIS order (see HaydnBundleMaterialize.h commitExactMultiMIProductCycle).
// Defined in HaydnHazardRecognizer.cpp; forward-declared in HaydnBundleMaterialize.h.
SmallVector<MachineInstr *, 3>
getFieldOrderedMembers(const Haydn::MachineBundle &Bundle,
                       const VLIWFormat &Format);

// Named Format E execution-unit indices. Must match HaydnSchedule.td
// ProcessorItineraries FuncUnit order (bit N of InstrStage Units_).
enum HaydnExecUnit : unsigned {
  EU_LOADSTORE0 = 0,
  EU_LOAD1 = 1,
  EU_ALU0 = 2,
  EU_ALU1 = 3,
  EU_ALU2 = 4,
  EU_MAC0 = 5,
  EU_MAC1 = 6,
  EU_COUNT = 7
};

// Distinct functional-unit resource bits tracked per cycle. Port demand is
// scalar counts (pooled budgets), not exclusive FU bits. Must equal the
// number of FuncUnits in HaydnSchedule.td (seven execution units).
inline constexpr unsigned HAYDN_NUM_FU_BITS = EU_COUNT;
static_assert(HAYDN_NUM_FU_BITS == 7, "seven Format E execution units");

// Per-cycle resource container — the RC type parameter of
// ResourceScoreboard<HaydnFuncUnitWrapper>.
// Each instance records the resource occupancy of a single cycle:
// * Required — itinerary execution units this cycle that are Required.
//   Exclusive single-unit Required bits conflict (unit injectivity);
//   multi-unit choice-sets (logical possible-unit menus) do not.
// * Reserved — itinerary units this cycle that are Reserved (AIE
//   FuncUnitWrapper Reserved). Req↔Res overlap conflicts; Res↔Res is legal.
// * IssueCount — number of instructions issued this cycle (≤ 3 entries).
// * GPR/DR/AR/SFR read/write running demand for pooled port budgets.
// * Slots — AIE FuncUnitWrapper::Slots peer. PacketFormats occupancy
//   booked from getSlotKind / getSlotInfo()->getSlotSet(). conflict()
//   ends with isFormatAvailable(Slots|Other) plus a fail-closed
//   getFormatOrNull / productCovers PacketFormats cover.
class HaydnFuncUnitWrapper {
public:
  using ResourceSet = StaticBitSet<HAYDN_NUM_FU_BITS>;

private:
  /// AIE FuncUnitWrapper::FormatInterface peer
  /// (AIEHazardRecognizer.h:74). Process-wide default formats.
  static const HaydnBaseMCFormats *FormatInterface;

  ResourceSet Required;
  ResourceSet Reserved;
  /// AIE FuncUnitWrapper::Slots peer (AIEHazardRecognizer.h:82).
  SlotBits Slots = 0;
  unsigned IssueCount = 0;
  unsigned GPRReads = 0;
  unsigned GPRWrites = 0;
  unsigned DRReads = 0;
  unsigned DRWrites = 0;
  unsigned ARReads = 0;
  unsigned ARWrites = 0;
  unsigned SFRReads = 0;
  unsigned SFRWrites = 0;
  MemoryObjectsBits LoadMemObjectsBits = 0;
  MemoryObjectsBits StoreMemObjectsBits = 0;

public:
  HaydnFuncUnitWrapper() = default;

  // Build from an InstrStage — Units land in Required or Reserved according
  // to InstrStage::getReservationKind (AIE FuncUnitWrapper ctor peer).
  HaydnFuncUnitWrapper(const InstrStage &IS);

  // Required-only constructor (unit tests / explicit unit set synthesis).
  explicit HaydnFuncUnitWrapper(const ResourceSet &RequiredSet)
      : Required(RequiredSet) {}

  // Required + Reserved constructor (unit tests / explicit stage synthesis).
  HaydnFuncUnitWrapper(const ResourceSet &RequiredSet,
                       const ResourceSet &ReservedSet)
      : Required(RequiredSet), Reserved(ReservedSet) {}

  static void setFormatInterface(const HaydnBaseMCFormats *Formats) {
    FormatInterface = Formats;
  }
  static const HaydnBaseMCFormats *getFormatInterface() {
    return FormatInterface;
  }

  bool isEmpty() const {
    return Required.empty() && Reserved.empty() && Slots == 0 &&
           IssueCount == 0 && GPRReads == 0 && GPRWrites == 0 &&
           DRReads == 0 && DRWrites == 0 && ARReads == 0 && ARWrites == 0 &&
           SFRReads == 0 && SFRWrites == 0 && LoadMemObjectsBits == 0 &&
           StoreMemObjectsBits == 0;
  }

  void clearResources() {
    Required.clear();
    Reserved.clear();
    Slots = 0;
    IssueCount = 0;
    GPRReads = 0;
    GPRWrites = 0;
    DRReads = 0;
    DRWrites = 0;
    ARReads = 0;
    ARWrites = 0;
    SFRReads = 0;
    SFRWrites = 0;
    LoadMemObjectsBits = 0;
    StoreMemObjectsBits = 0;
  }

  // Block all resources (used to mark a cycle as fully occupied so nothing
  // can issue). Mirrors AIE FuncUnitWrapper::blockResources.
  void blockResources() {
    Required = ~ResourceSet();
    Reserved = ~ResourceSet();
    Slots = ~SlotBits(0);
    IssueCount = ~0u;
    GPRReads = ~0u;
    GPRWrites = ~0u;
    DRReads = ~0u;
    DRWrites = ~0u;
    ARReads = ~0u;
    ARWrites = ~0u;
    SFRReads = ~0u;
    SFRWrites = ~0u;
  }

  SlotBits getSlots() const { return Slots; }
  void setSlots(SlotBits S) { Slots = S; }

  unsigned getIssueCount() const { return IssueCount; }
  unsigned getGPRReads() const { return GPRReads; }
  unsigned getGPRWrites() const { return GPRWrites; }
  unsigned getDRReads() const { return DRReads; }
  unsigned getDRWrites() const { return DRWrites; }
  unsigned getARReads() const { return ARReads; }
  unsigned getARWrites() const { return ARWrites; }
  unsigned getSFRReads() const { return SFRReads; }
  unsigned getSFRWrites() const { return SFRWrites; }
  const ResourceSet &getRequired() const { return Required; }
  const ResourceSet &getReserved() const { return Reserved; }
  void setGPRPorts(unsigned Reads, unsigned Writes) {
    GPRReads = Reads;
    GPRWrites = Writes;
  }
  void setDRPorts(unsigned Reads, unsigned Writes) {
    DRReads = Reads;
    DRWrites = Writes;
  }
  void setARPorts(unsigned Reads, unsigned Writes) {
    ARReads = Reads;
    ARWrites = Writes;
  }
  void setSFRPorts(unsigned Reads, unsigned Writes) {
    SFRReads = Reads;
    SFRWrites = Writes;
  }
  void setMemoryObjectBits(MemoryObjectsBits LoadBits,
                           MemoryObjectsBits StoreBits) {
    LoadMemObjectsBits = LoadBits;
    StoreMemObjectsBits = StoreBits;
  }
  MemoryObjectsBits getLoadMemObjectsBits() const { return LoadMemObjectsBits; }
  MemoryObjectsBits getStoreMemObjectsBits() const {
    return StoreMemObjectsBits;
  }
  // Mark this cycle as issuing one more instruction.
  void setIssueCountOne() { IssueCount = 1; }
  // Union another Required / Reserved unit set into this one (used when
  // accumulating an instruction's itinerary stages).
  void mergeRequired(const ResourceSet &Units) { Required |= Units; }
  void mergeReserved(const ResourceSet &Units) { Reserved |= Units; }

  bool operator==(const HaydnFuncUnitWrapper &Other) const {
    return Required == Other.Required && Reserved == Other.Reserved &&
           Slots == Other.Slots && IssueCount == Other.IssueCount &&
           GPRReads == Other.GPRReads && GPRWrites == Other.GPRWrites &&
           DRReads == Other.DRReads && DRWrites == Other.DRWrites &&
           ARReads == Other.ARReads && ARWrites == Other.ARWrites &&
           SFRReads == Other.SFRReads && SFRWrites == Other.SFRWrites &&
           LoadMemObjectsBits == Other.LoadMemObjectsBits &&
           StoreMemObjectsBits == Other.StoreMemObjectsBits;
  }

  // Union (accumulate another cycle's resources into this one). Used by the
  // recognizer's enterResources to record an issued instruction's footprint.
  HaydnFuncUnitWrapper &operator|=(const HaydnFuncUnitWrapper &Other) {
    Required |= Other.Required;
    Reserved |= Other.Reserved;
    Slots |= Other.Slots;
    IssueCount += Other.IssueCount;
    GPRReads += Other.GPRReads;
    GPRWrites += Other.GPRWrites;
    DRReads += Other.DRReads;
    DRWrites += Other.DRWrites;
    ARReads += Other.ARReads;
    ARWrites += Other.ARWrites;
    SFRReads += Other.SFRReads;
    SFRWrites += Other.SFRWrites;
    LoadMemObjectsBits |= Other.LoadMemObjectsBits;
    StoreMemObjectsBits |= Other.StoreMemObjectsBits;
    return *this;
  }

  // True iff issuing Other's resources on top of this cycle would violate a
  // constraint. Rules:
  // * exclusive single-unit Required: both |Required|==1 and same bit
  //   (unit injectivity — multi-unit choice-sets never Required-conflict alone)
  // * AIE Req/Res law: Required overlaps Other.Reserved OR Reserved overlaps
  //   Other.Required; Res/Res is legal
  // * issue cap: combined IssueCount exceeds 3
  // * GPR 4R2W / DR 8R3W / AR 2R2W / SFR 2R1W port budgets
  // * Format E occupancy: both Slots nonempty and the combined SlotSet is
  //   not admitted by isFormatAvailable / getFormatOrNull / productCovers
  //   (AIE FuncUnitWrapper::conflict :147-150).
  // * Memory-object bits are not a same-cycle reject (golden dual-load).
  bool conflict(const HaydnFuncUnitWrapper &Other) const;

  void dump() const;
};

// Scoreboard hazard recognizer for the Haydn MachineScheduler.
// It maintains a ResourceScoreboard of per-cycle resource occupancy and
// answers getHazardType by checking whether the candidate instruction's
// itinerary-stage units and GPR/DR/AR/SFR port demand conflict with the
// current cycle. EmitInstruction records the candidate's footprint;
// AdvanceCycle shifts the scoreboard window.
//
// IsPreRA=true: feasibility-only — MRI-correct vreg port demand, format
// tryAdd occupancy, same-cycle WAW/RAW including vregs; never stamps
// AltDescs/member opcodes (phase identity: logical only through RA).
// Format ResMII oracle (exhaustive ≤3 vs greedy) and SMS-HANDOFF packability
// helpers are pure vocabulary on HaydnPreRASchedStrategy / BundleFormatSolver
// — HR does not freeze FormatID and never materializes durable BUNDLE roots
// from matching-frontier scores (metrics-only; positive handoff is sibling).
// Matching-frontier probe (scoreMatchingFrontier) exposes nondominated
// cardinality / free-slot scarcity for pre-RA tryCandidate only; pure copies,
// no freeze. CurrentCycleCandidates / commitPlacementForEmit use the same
// exactTryAddProduct depth as SMS ResourceCycle canReserve/reserve for
// descriptor-derived format legality (plan §8.4 #7); PreRASchedStrategy pure
// helpers pin the accept/reject polarity surface without freezing FormatID.
// Multi-cycle itinerary stages book stage-relative scoreboard cycles
// (DeltaCycles+StageCycle, linear window) — not modulo-II SMS ResourceCycle
// phases. Product InstrStage cycles==1 and class-3 inventory is empty;
// SMS-HOOK II-wrap false-accept fail-close polarity is pinned on
// HaydnPreRASchedStrategy (catalog re-export) so list-sched never claims
// multi-cycle product support. FE5B WP4 whole-kernel periodic certificate
// (retain original until final accept / recoverable rollback / same-phase
// WAW via hasSameBundleWAW) is the ResourceCycle pack-oracle surface;
// HR same-bundle WAW is the list-sched dual of SMS same-phase simultaneous
// def fail-close. IsPreRA=false: post-RA path stamps setAlternateDescriptor
// for leaveRegion setDesc materialize.
// The recognizer is constructed once per scheduling region (the framework
// resets it via Reset at each region boundary).
class HaydnHazardRecognizer : public ScheduleHazardRecognizer {
public:
  // \p AltDescs is the function-lifetime alt-descriptor side map (owned by
  // HaydnMachineFunctionInfo). Null for pre-RA / tests — post-RA only stamps
  // MemberOpcode for leaveRegion materialize.
  HaydnHazardRecognizer(const TargetInstrInfo *TII,
                        const InstrItineraryData *ItinData, bool IsPreRA,
                        HaydnAlternateDescriptors *AltDescs = nullptr);

  // ScheduleHazardRecognizer interface.

  // Reset the scoreboard to an empty window for a new region.
  void Reset() override;

  // Return the hazard type of issuing SU DeltaCycles from the current cycle.
  // NoHazard if the candidate's slots and GPR ports fit in the current cycle;
  // Hazard if they would exceed a limit. Post-RA also consults the shared
  // canCoissueProductCycle emission probe so leaveMBB sequentialize cannot
  // become a second packing authority (AIE applyBundles size()>1 peer).
  HazardType getHazardType(SUnit *SU, int DeltaCycles = 0) override;

  // Record SU's resource footprint in the scoreboard (DeltaCycles defaults
  // to 0 = current cycle). The SUnit overload is what the MachineScheduler
  // calls; the MachineInstr overload is for non-scheduling pass use.
  void EmitInstruction(SUnit *SU) override;
  void EmitInstruction(MachineInstr *MI) override;
  void emitInstruction(SUnit *SU, int DeltaCycles);

  void AdvanceCycle() override;
  void RecedeCycle() override;

  // Issue limit for the current cycle (max three Format E entries).
  bool atIssueLimit() const override;

  // Accessors used by HaydnPostRASchedStrategy and tests.
  int getMaxLatency() const { return MaxLatency; }
  int getPipelineDepth() const { return PipelineDepth; }
  /// AIEHazardRecognizer.cpp:718-720. Distance at which two issued
  /// instructions can still share occupancy. Floor 1 so an empty itinerary
  /// cannot divide-by-zero the SF10 kernel replay.
  int getConflictHorizon() const {
    return std::max(std::max(PipelineDepth, MaxLatency), 1);
  }
  bool isPreRA() const { return IsPreRA; }

  /// Product pin: same-phase / same-bundle destination WAW is fail-closed
  /// (hasSameBundleWAW). Peer of ResourceCycle certificate same-reg keys.
  static constexpr bool productSamePhaseWAWFailsClosed() { return true; }

  /// Product pin: original loop may not be discarded from the HR surface;
  /// FE5B retain-until-accept is the ResourceCycle certificate lifecycle.
  /// HR never rewrites loops — polarity only so post-RA/list-sched ownership
  /// cannot claim half-enabled multi-stage SMS.
  static constexpr bool productSMSCertHalfEnabledMultiStageForbidden() {
    return productSamePhaseWAWFailsClosed();
  }

  /// Pure matching-frontier score for pre-RA tryCandidate (plan §5.1).
  /// Probe never mutates MI / AltDescs / FormatID; Full-only product keeps
  /// FeasibleFormatMask at 0 or ProductFormatMask (compact bytes are a no-op).
  struct MatchingFrontierScore {
    /// True iff \p LogicalOpc can join the candidate set (no-alt → true when
    /// the base set is non-empty; alts → canExactTryAddProduct).
    bool Feasible = false;
    /// Nondominated successor cardinality after a probe expand (base size for
    /// no-alt ops; 0 when infeasible).
    unsigned SuccessorMatchings = 0;
    /// Free issue slots in the preferred successor (higher = less scarcity).
    unsigned FreeSlotsPreferred = 0;
    /// Preferred successor's FormatID mask (product: Full bit or 0).
    uint64_t FeasibleFormatMask = 0;
  };

  /// Score adding \p LogicalOpc onto a *copy* of \p Base. Stateless; safe for
  /// unit tests and for tryCandidate without touching live HR state.
  static MatchingFrontierScore
  scoreMatchingFrontier(ArrayRef<haydn::bundle::CycleState> Base,
                        unsigned LogicalOpc);

  /// Live current-cycle frontier probe (copies CurrentCycleCandidates).
  MatchingFrontierScore probeMatchingFrontier(unsigned LogicalOpc) const {
    return scoreMatchingFrontier(CurrentCycleCandidates, LogicalOpc);
  }

  /// Live nondominated set for the current issue cycle (tests / debug).
  const haydn::bundle::CycleCandidateSet &getCurrentCycleCandidates() const {
    return CurrentCycleCandidates;
  }

  /// SF1 (Band 2S): format-aware placement oracle for the post-RA
  /// multi-stage pipeliner. One CycleCandidateSet per modulo cycle —
  /// ModuloCycleCandidates[II] — probed by canExactTryAddProduct and
  /// mutated only by exactTryAddProduct on the SMS accept path, the same
  /// mutate/probe pair this HR uses for the current cycle at
  /// commitPlacementForEmit / getHazardType(DeltaCycles==0).
  /// checkConflict also books Slots and asks isFormatAvailable /
  /// getFormatOrNull / productCovers (AIE FuncUnitWrapper::conflict).
  /// Re-seeded wholesale per tryII attempt (init is O(1) per cycle).
  haydn::bundle::ModuloCyclePlacementOracle ModuloOracle;

  /// SF1 probe: can \p MI's opcode join modulo cycle \p Cycle of an
  /// initialized ModuloOracle? Non-mutating; ports/itinerary/RAW/WAW gates
  /// remain in their owning predicates. True for untracked (no-alts) ops.
  bool canPlaceModulo(const MachineInstr &MI, int Cycle) const {
    return ModuloOracle.canPlace(resolveBookingOpcode(MI), Cycle);
  }

  /// SF1 accept path: commit \p MI's opcode into modulo cycle \p Cycle.
  /// Fail-closed — on false the placement must be rejected (retry / raise
  /// II), never forced or silently re-selected.
  bool placeModulo(const MachineInstr &MI, int Cycle) {
    return ModuloOracle.place(resolveBookingOpcode(MI), Cycle);
  }

  /// True when \p Opcode is SIN_COS/ARCTAN or a placement / Format E member of
  /// those logicals. Identity is AIE canAdd AlternateInsts membership
  /// (AIEHazardRecognizer.cpp:188-202) plus generated member Logical — never
  /// `_S*` suffix parse. Class-1 issue-alone is this predicate; the
  /// uimm4+2 unit occupancy / dest-writer window is booked on emit.
  static bool opcodeIssuesAloneInCycle(unsigned Opcode);

  /// Named SF1 same-cycle laws (alone / CSRW↔SET / e0-alone). Same
  /// predicate as haydn::pack::cycleViolatesNamedSameCycleLaws.
  static bool cycleViolatesNamedSameCycleLaws(
      const MachineInstr &Cand, ArrayRef<const MachineInstr *> Occupied);

  /// Remark / ResourceCycle tag for the three named same-cycle laws.
  static constexpr const char *namedSameCycleLawsTag() {
    return HAYDN_NAMED_SAME_CYCLE_LAWS_TAG;
  }

  /// Golden §Special occupancy for SIN_COS/ARCTAN: uimm4+2 (2..17).
  /// Returns 0 when \p MI is not a SIN_COS/ARCTAN logical or member.
  /// Missing or out-of-range imm fail-closes to the published max (15+2).
  static unsigned sinCosWindowOccupancy(const MachineInstr &MI);

  /// Architectural dest Data_Latency of \p MI's def at \p DefOpIdx.
  /// SIN_COS/ARCTAN use the exact uimm4+2 window; every other class uses
  /// the itinerary (clamped so the conservative OperandCycles 17 scaffold
  /// does not size ordinary dest-read windows).
  static unsigned architecturalDefLatency(const InstrItineraryData *Itin,
                                          const MachineInstr &MI,
                                          unsigned DefOpIdx);

  // Late stall-net reuse of dest-window maps (AIE scoreboard emit/advance
  // overlay; Haydn has no interlock). Does not rematch or setDesc. Tick
  // after book so a Data_Latency=2 def still blocks the next issue cycle.
  void emitForDestWindow(const MachineInstr &MI);
  void advanceDestWindows();
  unsigned destWindowStallNeed(const MachineInstr &MI) const;
  unsigned destWindowExitLeak() const;

  // PostPipeliner / external scoreboard helpers. Issue-cycle footprint is
  // ports + issue + stage-0 FUs; multi-cycle stages are booked via
  // checkConflict/enterResources (AIE anyStage peer).
  HaydnFuncUnitWrapper getInstrFootprint(const MachineInstr &MI) const {
    return buildCandidate(MI);
  }

  // Cross-zone scoreboard overlap (AIEHazardRecognizer::conflict peer,
  // AIEHazardRecognizer.cpp:410-413). DeltaCycles is the displacement of
  // Other relative to this; leaveRegion checkInterZoneConflicts uses -1 so
  // Bot scoreboard[0] (empty receded cycle) lines up with Top scoreboard[-1].
  bool conflict(const HaydnHazardRecognizer &Other, int DeltaCycles) const {
    return Scoreboard.conflict(Other.Scoreboard, DeltaCycles);
  }
  // Stage-relative conflict: issue ports + Format E Slots at \p Cycle
  // plus each itinerary stage at Cycle+StageCycle
  // (AIEHazardRecognizer::checkConflict peer :554-616). Same-cycle
  // unplaced alts skip leftover StageCycle==0 FieldSlot unit bits
  // (exactTryAdd owns issue-cycle injectivity) but still check
  // StageCycle>0 occupancy. Format coverage rides Slots via
  // isFormatAvailable / getFormatOrNull / productCovers — not a second
  // solver. MultiSlot_Pseudo is not exempt — only isNoHazardMeta / debug
  // BUNDLE / LLVM meta skip hazard booking (never blanket isPseudo).
  bool checkConflict(const ResourceScoreboard<HaydnFuncUnitWrapper> &SB,
                     const MachineInstr &MI, int Cycle) const;

  /// PacketFormats occupancy of \p MI for the AIE Slots overlay.
  /// Selected / pinned member getSlotKind only. Unplaced alts return 0
  /// so preferred-member slot OR cannot serialize a rematchable pair
  /// (exactTryAdd owns issue-cycle rematch).
  SlotBits occupancySlots(const MachineInstr &MI) const;

  /// AIE getMemoryObjectsBits peer (AIEHazardRecognizer.cpp:829-862).
  /// IR Value* identity from MMOs — not a golden bank invent. Empty
  /// MMOs are optimistic (no bits). Pre-RA returns empty.
  MemoryObjectPair getMemoryObjectsBits(const MachineInstr *MI) const;

  /// Swap the AltDesc side-map (SMS search uses the transient pin map).
  void setAlternateDescriptors(HaydnAlternateDescriptors *A) { AltDescs = A; }
  HaydnAlternateDescriptors *getAlternateDescriptors() const {
    return AltDescs;
  }
  /// Off-side replay MRI context. Port counters (countGPRPorts & friends)
  /// resolve an instruction's MachineFunction through MI.getParent() to get
  /// the MRI; ClonedMachineInstrs checked BEFORE insertion into an MBB have
  /// no parent and MachineInstr::getMF() derefs null. Multi-stage epilogue
  /// preseed probes such clones — set the owning MF's MRI for that replay.
  /// Null (default) preserves the parent-derived path.
  void setPortMRIContext(const MachineRegisterInfo *MRI) { PortMRI = MRI; }
  const MachineRegisterInfo *getPortMRIContext() const { return PortMRI; }
  /// Itinerary view for placement-time latency queries (G004 residue
  /// dest-window law reads architectural def latencies off the same
  /// ItinData the scoreboard uses).
  const InstrItineraryData *getItineraryData() const { return ItinData; }
  // Stage-relative enter: book issue ports + each stage at relative ring
  // cycle. Stages use selected AltDesc member schedclass when stamped
  // (post-rematch), else the logical opcode schedclass.
  void emitInScoreboard(ResourceScoreboard<HaydnFuncUnitWrapper> &SB,
                        const MachineInstr &MI, int Cycle) const;
  void enterResources(ResourceScoreboard<HaydnFuncUnitWrapper> &SB,
                      const MachineInstr &MI, int DeltaCycles) const;

private:
  const TargetInstrInfo *TII;
  const InstrItineraryData *ItinData;
  bool IsPreRA;
  // Off-side replay MRI (parentless clones; see setPortMRIContext).
  const MachineRegisterInfo *PortMRI = nullptr;
  // slice 2a: function alt-descriptor side-map (non-owning). Null in
  // tests / when no MF context.
  HaydnAlternateDescriptors *AltDescs = nullptr;
  mutable MemoryObjectEnumerator ObjectEnumerator;

  ResourceScoreboard<HaydnFuncUnitWrapper> Scoreboard;
  // Snapshot of Scoreboard at the start of the current issue cycle (after
  // Reset/Advance/Recede). After each rematch at DeltaCycles==0, Scoreboard
  // is restored from this snapshot and all CurrentCyclePlacedMIs re-enter
  // with their selected member schedclasses so single-slot FU bits track the
 // preferred matching.
  ResourceScoreboard<HaydnFuncUnitWrapper> ScoreboardAtCycleStart;
  int PipelineDepth = -1;
  int MaxLatency = -1;
  unsigned IssueLimit = 3;

  // same-bundle destination-register WAW. Constraints forbid two instructions
  // in one bundle from writing the same register. This set holds destination
  // registers of instructions already issued in the CURRENT cycle (cleared on
  // Advance/Recede/Reset); a candidate whose defs overlap it is a Hazard.
  // SFR is included: product law is one SFR writer per cycle (dead flag
  // side-effects count). R0 is included: soft-zero restores and R0-borrow
  // loads are real write-port consumers.
  SmallSet<Register, 8> CurrentCycleDefs;
  // LIVE destination registers written this cycle (defs whose result is
  // consumed, i.e. NOT dead). Used by hasSameBundleRAW. A same-bundle read+write
  // of one register is only a real RAW hazard when the write has a consumer
  // (the reader consumes its value); if the write is DEAD the reader correctly
  // observes the OLD value (WAR-style) and the bundle is legal. The dead flag
  // is precisely the scheduling-DAG/liveness verdict on "does this def have a
  // consumer" — so checking it here is equivalent to consulting the SDag for a
  // true RAW edge, without storing SUnit pointers (which the / UAF
  // taught us to avoid). WAW (CurrentCycleDefs above) intentionally does NOT
  // skip dead: the spec forbids two writes to one register regardless of
  // liveness (write-port/undefined), matching.
  SmallSet<Register, 8> CurrentCycleLiveDefs;
  // ARCTAN/SIN_COS issue-alone flag for the current cycle. Multi-cycle
  // unit occupancy (uimm4+2 Reserved) lives on the scoreboard ring.
  bool CurrentCycleHasLockedSlotOp = false;
  // Dest-read remaining cycles (Data_Latency-1). Serializes readers so the
  // late stall net inserts nothing at O1+ when the scheduler honors this.
  DenseMap<Register, unsigned> DestReadPending;
  // SIN_COS/ARCTAN dest-writer lock remaining cycles (occupancy-1).
  DenseMap<Register, unsigned> DestWritePending;
  // Same availability-aware pin pre-RA / post-RA / LatencyStalls consume.
  // Instance member, not a Reset-local static (one check per recognizer).
  bool ResourceAdmissionPinned = false;
  // Per-issue-cycle flags, cleared on Reset/AdvanceCycle/RecedeCycle.
  // CSRW↔SET_HWLOOP same-bundle (spec §5.10) and LUI/ADDI32_W e0-alone.
  bool CurrentCycleHasHwloopSetup = false;
  bool CurrentCycleHasHwloopCsrw = false;
  bool CurrentCycleHasAbsMaterialize = false;
  bool CurrentCycleHasNonAbsReal = false;
  const TargetRegisterInfo *TRI = nullptr;

 // : product CycleCandidateSet for the CURRENT cycle — placement
  // authority via exactTryAddProduct (AIEHazardRecognizer.cpp:174-214 alt try
  // + AIEBundle.h canAdd/add occupancy, strengthened with nondominated
  // rematching). Replaces first-fit freeze of a single CycleState. Cleared on
  // Advance/Recede/Reset. Preferred OccupiedSlots is
  // selectPreferredCandidate(CurrentCycleCandidates).OccupiedSlots.
  haydn::bundle::CycleCandidateSet CurrentCycleCandidates =
      haydn::bundle::makeProductCandidateSet();
  /// MIs issued this cycle (alts-bearing), parallel to preferred Members for
  /// AltDesc re-stamp after each exact expand (rematch earlier fields).
  SmallVector<MachineInstr *, 3> CurrentCyclePlacedMIs;

 // HaydnMCFormats for PlacementAlternative / exact tryAdd ( alts-only).
  // Stateless table lookup.
  HaydnMCFormats Fmts;

  // Walk all scheduling classes to compute the scoreboard depth and the
  // maximum result latency (used to size the scoreboard window).
  void computeMaxLatency();

  // Build the issue-cycle resource footprint of one instruction (ports +
  // IssueCount=1 + stage-0 FU bits from selected/logical schedclass or
  // PlacementAlternative FieldSlots). Multi-cycle stages are NOT folded in
  // here — use checkConflict/enterResources for stage-relative booking.
  HaydnFuncUnitWrapper buildCandidate(const MachineInstr &MI) const;

  // SchedClass for scoreboard stages: selected AltDesc member after rematch,
  // else the logical opcode. No setDesc — descriptor side-map only.
  unsigned resolveSchedClass(const MachineInstr &MI) const;

  // Opcode whose itinerary/FieldSlots drive FU booking (selected member or
  // logical).
  unsigned resolveBookingOpcode(const MachineInstr &MI) const;

  // Capture Scoreboard → ScoreboardAtCycleStart (Reset/Advance/Recede).
  void captureCycleStartScoreboard();
  // Restore Scoreboard from ScoreboardAtCycleStart and re-enter every MI
  // issued this cycle with post-rematch selected member schedclasses.
  void reenterCurrentCycleScoreboard();

  // Lazily cache the TargetRegisterInfo (the recognizer has no MachineFunction
  // at construction time; it is fetched from the first MI seen).
  const TargetRegisterInfo *getTRI(const MachineInstr &MI);

  // true iff MI defines a register that overlaps a register already
  // written by an instruction issued in the current cycle. FE5B same-phase
  // simultaneous def fail-close dual of HaydnResourceCycle WAW / DefRegKey
  // certificate pin (WP4); product never accepts dual same-reg writers in
  // one issue bundle/modulo phase. W39: delegates to the ONE shared
  // no-dual-write law (HaydnIntraCycleWAW.h), the same predicate the
  // ResourceCycle and materialize commit paths use.
  bool hasSameBundleWAW(const MachineInstr &MI) const;

  // true iff MI reads a register that an instruction already issued in
  // the current cycle writes (CurrentCycleDefs). Haydn spec §Constraints:
  // "All instructions within the same bundle read their source registers
  // simultaneously" — there is NO intra-bundle forwarding, so a reader placed
  // in the same cycle as its writer observes the OLD value. The scheduler
  // issues writers before their RAW readers (dataflow/topological order), so
  // when the reader is the candidate the writer is already in
  // CurrentCycleDefs and this trips. WAR (reader-issued-before-writer) does
  // NOT trip: the reader correctly observes the old value, which is what WAR
  // semantics require — so this check never false-positives on a legal bundle.
  bool hasSameBundleRAW(const MachineInstr &MI) const;

  // record MI's destination registers into CurrentCycleDefs.
  void appendDefs(const MachineInstr &MI);

  // true iff MI's opcode or selected AltDesc member is a SIN_COS/ARCTAN
  // logical, residual placement member, or generated Format E member.
  // Survives post-setDesc recommit (SM-H2). Class-1 issue-alone only.
  bool isLockedSlotDspOp(const MachineInstr &MI) const;

  // Book uimm4+2 Reserved occupancy on the selected unit and dest-writer lock.
  // No-op on the pre-RA bottom-up HR (DAG SDep carries latency).
  void bookSinCosWindow(const MachineInstr &MI);
  // Publish dest-read remaining cycles for every def (post-RA only).
  void bookDestReadWindow(const MachineInstr &MI);
  // Expire dest-window remaining counts. Both Advance and Recede expire
  // (never grow) so a bottom-up zone cannot recede forever.
  void tickDestWindows(int Delta);
  // True when MI would read a dest still inside Data_Latency, or write a
  // SIN_COS/ARCTAN dest still inside its occupancy window. Always false
  // on the pre-RA HR.
  bool hasDestWindowHazard(const MachineInstr &MI, int DeltaCycles) const;

 // –B3.exit.3 / : exact-expand CurrentCycleCandidates via
  // exactTryAddProduct (alts-only). Post-RA re-stamps setAlternateDescriptor
  // for every MI issued this cycle from the preferred surviving matching
  // (rematch-safe) for leaveRegion setDesc materialize. Pre-RA tracks tryAdd
 // occupancy only — no AltDesc/member stamp ( phase identity).
  // No setDesc here — that is materializeMultiOpcodeInstrs.
  // No new MCFlags writers.
  void commitPlacementForEmit(MachineInstr *MI);

  /// Preferred CycleState view of CurrentCycleCandidates (debug / tests).
  const haydn::bundle::CycleState &currentCyclePreferred() const {
    return haydn::bundle::selectPreferredCandidate(CurrentCycleCandidates);
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNHAZARDRECOGNIZER_H
