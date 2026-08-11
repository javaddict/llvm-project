//===-- HaydnHazardRecognizer.h - Haydn scoreboard hazard recognizer ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Haydn scoreboard hazard recognizer — the AIE-style
// resource model for the post-RA MachineScheduler (Stream B, ).
//
// Design
//======//
// The Haydn VLIW datapath has 3 slots (SLOT0/1/2), a 4R2W GPR register file
// and a 7R3W DR64 register file, each shared across all slots (CLAUDE.md
// "Slot architecture"; spec port-budget table). Up to 3 instructions may
// issue per cycle, subject to:
// * slot exclusivity (one instr per slot per cycle)
// * the GPR 4R2W port budget
// * the DR64 7R3W port budget (— forward-compat / spec-alignment:
// under the current slot model max legal DR demand is 6R2W < 7R3W, but
// the cap is correct-by-construction once fused-MAC / dual-write DR ops
// land, and it matches the RTL SVA contract regardless)
// * latency-bound data dependencies (carried by the scheduler DAG's SDep
// edges, NOT by this recognizer — see note below).
//
// This recognizer drives a ResourceScoreboard<HaydnFuncUnitWrapper> that
// records, per cycle, which slots and how many GPR read/write ports are
// occupied. The MachineScheduler consults it via SchedBoundary::checkHazard
// (MachineScheduler.cpp:2700), which calls getHazardType on every ready SUnit.
//
// Data dependencies (RAW/WAW/WAR with their latencies) are NOT modeled here.
// They are carried by the scheduler DAG's SDep edges, which set each SUnit's
// TopReadyCycle/BotReadyCycle. This matches the AIE design (see
// ~/haydn-plans/AIE/sms-packetizer-deep-dive.md §hazard recognizer): the
// scoreboard carries only resource/slot/port conflicts; the DAG carries data.
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
#include "HaydnResourceScoreboard.h"
#include "HaydnStaticBitSet.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class MachineInstr;
class MachineFunction;
class SUnit;
class TargetInstrInfo;
class HaydnAlternateDescriptors;
class TargetRegisterInfo;
class TargetSubtargetInfo;

// AIE peer: AIEHazardRecognizer.cpp:278-314 applyFormatOrdering —
// walk Format.getSlots(), Bundle.at(Slot), removeFromBundle+insert+
// bundleWithPred, then finalizeBundle. B3.2: free function in the same
// namespace as AIE (llvm::), names match for the AIE clone path.
void applyFormatOrdering(Haydn::MachineBundle &Bundle, const VLIWFormat &Format,
                         MachineBasicBlock::iterator InsertPoint);

// Total number of distinct functional-unit resource bits tracked per cycle.
// This covers the 3 slots (SLOT0/1/2 — the itinerary FuncUnits). GPR port
// demand is tracked as scalar counts rather than FU bits because the 4R2W
// budget is a counting constraint, not an exclusivity constraint. The value
// must match the number of FuncUnits HaydnItineraries declares, which is
// HaydnFormatEUnits: LOADSTORE0, LOAD1, ALU0, ALU1, ALU2, MAC0, MAC1.
//
// It was 3, for the retired [SLOT0, SLOT1, SLOT2], and those three sat AHEAD
// of the units in the list — so a Unit_* itinerary set bits 3..9 and the loop
// over bits 0..2 in HaydnFuncUnitWrapper recorded NOTHING. From the day the
// members were retargeted onto the unit model the post-RA scheduler had an
// empty resource set for every instruction and its exclusivity check was
// inert. It did not show: bundle legality comes from the placement
// (FORMAT-E-SWITCH-PLAN.md section 7), never from here.
//
inline constexpr unsigned HAYDN_NUM_FU_BITS = 7;

// Per-cycle resource container — the RC type parameter of
// ResourceScoreboard<HaydnFuncUnitWrapper>.
// Each instance records the resource occupancy of a single cycle:
// * Required — the itinerary FuncUnits (slots) reserved this cycle. Two
// instructions needing the same single-slot itinerary (e.g. two
// Slot1-only loads) conflict.
// * IssueCount — number of instructions issued this cycle. The 3-issue
// cap is enforced on this count.
// * GPRReads / GPRWrites — running GPR32 port demand this cycle, filled
// by the recognizer from countGPRPorts. The 4R2W budget is enforced on
// these sums.
// * DRReads / DRWrites — running DR64 port demand this cycle, filled from
// countDRPorts. The 7R3W budget is enforced on these sums.
class HaydnFuncUnitWrapper {
  using ResourceSet = StaticBitSet<HAYDN_NUM_FU_BITS>;

  ResourceSet Required;
  unsigned IssueCount = 0;
  unsigned GPRReads = 0;
  unsigned GPRWrites = 0;
  unsigned DRReads = 0;
  unsigned DRWrites = 0;
  unsigned ARReads = 0;
  unsigned ARWrites = 0;

public:
  HaydnFuncUnitWrapper() = default;

  // Build from an InstrStage — the stage's Units become Required bits
  // (matching how AIE's FuncUnitWrapper consumes a stage). The reservation
  // kind (Required vs Reserved) is honored: only Required units are
  // conflict-causing, matching InstrStage::getReservationKind semantics.
  // On Haydn all current stages are Required (single-stage slot-only
  // itineraries), but the Reserved path is implemented for forward
  // compatibility with Stream C multi-stage enrichments.
  HaydnFuncUnitWrapper(const InstrStage &IS);

  // Required-only constructor (used by the recognizer to build a candidate
  // cycle from a pre-resolved slot set, without going through InstrStage).
  explicit HaydnFuncUnitWrapper(const ResourceSet &RequiredSet)
      : Required(RequiredSet) {}

  bool isEmpty() const {
    return Required.empty() && IssueCount == 0 && GPRReads == 0 &&
           GPRWrites == 0 && DRReads == 0 && DRWrites == 0 &&
           ARReads == 0 && ARWrites == 0;
  }

  void clearResources() {
    Required.clear();
    IssueCount = 0;
    GPRReads = 0;
    GPRWrites = 0;
    DRReads = 0;
    DRWrites = 0;
    ARReads = 0;
    ARWrites = 0;
  }

  // Block all resources (used to mark a cycle as fully occupied so nothing
  // can issue). Mirrors AIE FuncUnitWrapper::blockResources.
  void blockResources() {
    Required = ~ResourceSet();
    IssueCount = ~0u;
    GPRReads = ~0u;
    GPRWrites = ~0u;
    DRReads = ~0u;
    DRWrites = ~0u;
    ARReads = ~0u;
    ARWrites = ~0u;
  }

  unsigned getIssueCount() const { return IssueCount; }
  unsigned getGPRReads() const { return GPRReads; }
  unsigned getGPRWrites() const { return GPRWrites; }
  unsigned getDRReads() const { return DRReads; }
  unsigned getDRWrites() const { return DRWrites; }
  unsigned getARReads() const { return ARReads; }
  unsigned getARWrites() const { return ARWrites; }
  const ResourceSet &getRequired() const { return Required; }
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
  // Mark this cycle as issuing one more instruction.
  void setIssueCountOne() { IssueCount = 1; }
  // Union another Required slot set into this one (used when accumulating
  // an instruction's itinerary stages).
  void mergeRequired(const ResourceSet &Slots) { Required |= Slots; }

  bool operator==(const HaydnFuncUnitWrapper &Other) const {
    return Required == Other.Required && IssueCount == Other.IssueCount &&
           GPRReads == Other.GPRReads && GPRWrites == Other.GPRWrites &&
           DRReads == Other.DRReads && DRWrites == Other.DRWrites &&
           ARReads == Other.ARReads && ARWrites == Other.ARWrites;
  }

  // Union (accumulate another cycle's resources into this one). Used by the
  // recognizer's enterResources to record an issued instruction's footprint.
  HaydnFuncUnitWrapper &operator|=(const HaydnFuncUnitWrapper &Other) {
    Required |= Other.Required;
    IssueCount += Other.IssueCount;
    GPRReads += Other.GPRReads;
    GPRWrites += Other.GPRWrites;
    DRReads += Other.DRReads;
    DRWrites += Other.DRWrites;
    ARReads += Other.ARReads;
    ARWrites += Other.ARWrites;
    return *this;
  }

  // True iff issuing Other's resources on top of this cycle would violate a
  // constraint. Rules:
  // * slot exclusivity: any Required bit shared (two instrs need the same
  // single-slot itinerary)
  // * issue cap: combined IssueCount exceeds 3
  // * GPR 4R2W: combined reads exceed 4 OR combined writes exceed 2
  // * DR64 7R3W: combined reads exceed 7 OR combined writes exceed 3
  // * AR 2R2W : combined reads exceed 2 OR combined writes exceed 2.
  bool conflict(const HaydnFuncUnitWrapper &Other) const;

  void dump() const;
};

// Scoreboard hazard recognizer for the Haydn post-RA MachineScheduler.
// It maintains a ResourceScoreboard of per-cycle resource occupancy and
// answers getHazardType by checking whether the candidate instruction's
// itinerary-stage slots and GPR port demand conflict with the current cycle.
// EmitInstruction records the candidate's footprint; AdvanceCycle shifts the
// scoreboard window.
// The recognizer is constructed once per scheduling region (the framework
// resets it via Reset at each region boundary).
class HaydnHazardRecognizer : public ScheduleHazardRecognizer {
public:
  // slice 2a: \p AltDescs is the function-lifetime alt-descriptor side
  // map (owned by HaydnMachineFunctionInfo). May be null when constructed
  // outside a MachineFunction context (tests). When non-null + the slot-select
  // flag is on, the HR records each MI's chosen (slot, variant) here during
  // scheduling for the finalizer to bake (E-4).
  HaydnHazardRecognizer(const TargetInstrInfo *TII,
                        const InstrItineraryData *ItinData, bool IsPreRA,
                        HaydnAlternateDescriptors *AltDescs = nullptr);

  // ScheduleHazardRecognizer interface.

  // Reset the scoreboard to an empty window for a new region.
  void Reset() override;

  // Return the hazard type of issuing SU DeltaCycles from the current cycle.
  // NoHazard if the candidate's slots and GPR ports fit in the current cycle;
  // Hazard if they would exceed a limit.
  HazardType getHazardType(SUnit *SU, int DeltaCycles = 0) override;

  // Record SU's resource footprint in the scoreboard (DeltaCycles defaults
  // to 0 = current cycle). The SUnit overload is what the MachineScheduler
  // calls; the MachineInstr overload is for non-scheduling pass use.
  void EmitInstruction(SUnit *SU) override;
  void EmitInstruction(MachineInstr *MI) override;
  void emitInstruction(SUnit *SU, int DeltaCycles);

  void AdvanceCycle() override;
  void RecedeCycle() override;

  // Issue limit for the current cycle (3 slots).
  bool atIssueLimit() const override;

  // Accessors used by HaydnPostRASchedStrategy and tests.
  int getMaxLatency() const { return MaxLatency; }
  int getPipelineDepth() const { return PipelineDepth; }

  // PostPipeliner / external scoreboard helpers. Build the same per-cycle
  // footprint used by getHazardType/EmitInstruction so modulo search and the
  // list scheduler agree on slot + port pressure.
  HaydnFuncUnitWrapper getInstrFootprint(const MachineInstr &MI) const {
    return buildCandidate(MI);
  }
  bool checkConflict(const ResourceScoreboard<HaydnFuncUnitWrapper> &SB,
                     const MachineInstr &MI, int Cycle) const {
    if (MI.isPseudo() || MI.isDebugInstr() || MI.isBundle() ||
        MI.isMetaInstruction())
      return false;
    if (!SB.isInRange(Cycle))
      return false;
    return SB[Cycle].conflict(buildCandidate(MI));
  }
  void emitInScoreboard(ResourceScoreboard<HaydnFuncUnitWrapper> &SB,
                        const MachineInstr &MI, int Cycle) const {
    if (MI.isPseudo() || MI.isDebugInstr() || MI.isBundle() ||
        MI.isMetaInstruction())
      return;
    if (!SB.isInRange(Cycle))
      return;
    SB[Cycle] |= buildCandidate(MI);
  }

private:
  const TargetInstrInfo *TII;
  const InstrItineraryData *ItinData;
  bool IsPreRA;
  // slice 2a: function alt-descriptor side-map (non-owning). Null in
  // tests / when no MF context.
  HaydnAlternateDescriptors *AltDescs = nullptr;

  ResourceScoreboard<HaydnFuncUnitWrapper> Scoreboard;
  int PipelineDepth = -1;
  int MaxLatency = -1;
  unsigned IssueLimit = 3;

  // same-bundle destination-register WAW. The spec (VLIW_Engine_
  // Compiler_Constraints.md §Constraints) forbids two instructions in one
  // bundle from writing the same register (per register file). The scheduler
  // DAG's output-dependence edges serialize most same-physreg defs, but 's
  // hasWAWHazard lived in the Phase-1 packetizer that retired — so the
  // explicit gate was lost on the live scoreboard path. This set holds the
  // destination registers of instructions already issued in the CURRENT cycle
  // (cleared on AdvanceCycle/RecedeCycle/Reset); a candidate whose defs
  // overlap it is reported as a Hazard so the scheduler delays it to the next
  // cycle and places its consumers with correct latencies (a post-hoc un-bundle
  // would instead violate those latencies). SFR (slot-ordered safe parallel
  // writes — two ALU ops both implicit-def dead $sfr in one bundle is normal)
  // is excluded. R0 is *not*: soft-zero restores (XOR R0,R0,R0) and R0-borrow
  // loads are real write-port consumers; dual R0 defs in one bundle are
  // WRITE_CONFLICT on silicon/BundleSim.
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
  // ARCTAN/SIN_COS (G-PACK-LEGAL, current design): force alone in the issue
  // bundle only. No multi-cycle slot lock / (uimm4+2) scoreboard reservation.
  bool CurrentCycleHasLockedSlotOp = false;
  const TargetRegisterInfo *TRI = nullptr;

  // B2.4: product CycleState for the CURRENT cycle — placement authority
  // via tryAddProduct (AIEHazardRecognizer.cpp:174-214 alt try +
  // AIEBundle.h canAdd/add occupancy). Replaces the former getLegalSlots +
  // S0-first CurrentCycleSlots hand auction. Cleared on Advance/Recede/Reset.
  // OccupiedSlots bitset is CurrentCycleState.OccupiedSlots (SLOT* bits).
  haydn::bundle::CycleState CurrentCycleState =
      haydn::bundle::makeProductCycleState();

  // HaydnMCFormats for PlacementAlternative / tryAdd (B2.5 alts-only).
  // Stateless table lookup.
  // WithMII: this is the placement authority — enumeratePlacementAlternatives
  // and tryAddProduct below choose which MEMBER an instruction becomes, and
  // the unit is a property of the member. A plain HaydnMCFormats here makes
  // that choice slot-only (§ 5.7).
  HaydnMCFormatsWithMII Fmts;

  // Walk all scheduling classes to compute the scoreboard depth and the
  // maximum result latency (used to size the scoreboard window).
  void computeMaxLatency();

  // Build the per-cycle resource footprint of one instruction (slots from
  // its itinerary stages, GPR ports from countGPRPorts, IssueCount=1).
  HaydnFuncUnitWrapper buildCandidate(const MachineInstr &MI) const;

  // Lazily cache the TargetRegisterInfo (the recognizer has no MachineFunction
  // at construction time; it is fetched from the first MI seen).
  const TargetRegisterInfo *getTRI(const MachineInstr &MI);

  // true iff MI defines a register that overlaps a register already
  // written by an instruction issued in the current cycle.
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

  // true iff MI is ARCTAN or SIN_COS (DSP math ops that lock their
  // issue alone this cycle only — no multi-cycle slot lock). Used by
  // the minimal single-instruction-bundle guard.
  bool isLockedSlotDspOp(const MachineInstr &MI) const;

  // B2.4–B3.exit.3: commit MI's field into CurrentCycleState via tryAddProduct
  // (alts-only). Stamps setAlternateDescriptor(MemberOpcode) for leaveRegion
  // setDesc materialize (AIEHazardRecognizer.cpp:389;
  // AIEAlternateDescriptors.h:39-44). Placement after materialize is
  // getSlotKind. No setDesc here — that is materializeMultiOpcodeInstrs.
  // No new MCFlags writers.
  void commitPlacementForEmit(MachineInstr *MI);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNHAZARDRECOGNIZER_H
