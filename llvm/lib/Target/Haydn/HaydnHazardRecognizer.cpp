//===-- HaydnHazardRecognizer.cpp - Haydn scoreboard hazard recognizer ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the Haydn scoreboard hazard recognizer. See the header
// (HaydnHazardRecognizer.h) for the design and the rationale for why this
// path is safe vs the / UAF in VLIWMachineScheduler.
//
//===----------------------------------------------------------------------===//

#include "HaydnHazardRecognizer.h"
#include "HaydnAlternateDescriptors.h"
#include "HaydnInstrInfo.h"
#include "HaydnPortModel.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-hazard-rec"

static cl::opt<bool> EnableHaydnHRSlotSelect(
    "haydn-hr-slot-select", cl::init(true), cl::Hidden,
    cl::desc(" slice 2b: record the HR-chosen slot per MachineInstr into"
             "HaydnAlternateDescriptors. The finalizer (slice 2c) reads it to "
             "resolve the slot-dependent variant opcode (_M0S1/_M0S2/_S1_M0/"
             "_S2_M0) so the MC slot-OR encoder trusts the baked variant. "
             "Default ON (cutover). Implies -haydn-hr-format-aware."));

namespace {
// CSRW↔SET_HWLOOP same-bundle hazard (spec §5.10, line 365 of
// Database/hypo_encoding/encoding_manual.md):
// "Also must not target CSR addresses 0x20-0x25 (HWLR_BEGIN/END/COUNT) in
// the same bundle as SET_HWLOOP, SET_HWLOOP_F2, or SET_HWLOOP_REG."
// CSRs 0x20-0x25 are the hardware-loop registers (HWLR_BEGIN/END/COUNT for
// the two loop slots — golden source: VLIW_Engine_Compiler_Constraints.md).
// Writing them via CSRW in the same cycle as a SET_HWLOOP variant would race
// the implicit HWLR update; the spec forbids it and the packetizer must put
// the two in separate bundles (cycles).
// These helpers identify both sides of the hazard. They cover ALL six
// SET_HWLOOP opcode variants the spec names (narrow pseudos SET_HWLOOP
// SET_HWLOOP_REG today lowered by AsmPrinter, plus the wide real forms
// SET_HWLOOP_W / SET_HWLOOP_F2_W / SET_HWLOOP_REG_W that the encoding
// migration is wiring onto the post-RA path). The pseudo variants are
// normally skipped by getHazardType's isPseudo early-out, but the guard
// is correct-by-construction regardless: if a future pass lowers a hwloop
// setup to a real (non-pseudo) SET_HWLOOP opcode before postmisched, the
// hazard fires automatically.

// True iff MI is any SET_HWLOOP variant (the writer side of the hazard).
bool isHwloopSetupOp(unsigned Opcode) {
  return Opcode == Haydn::SET_HWLOOP || Opcode == Haydn::SET_HWLOOP_REG ||
         Opcode == Haydn::SET_HWLOOP_W || Opcode == Haydn::SET_HWLOOP_F2_W ||
         Opcode == Haydn::SET_HWLOOP_REG_W;
}

// If MI is a CSRW / CSRW_W whose CSR operand is in the HWLR range
// 0x20-0x25, return that CSR address; otherwise return -1.
// Operand layout (HaydnInstrInfo.td):
// CSRW (3 operands, 1 def): op0=$rd (dead def for MC parity), op1=$csr_addr
// CSRW_W (2 operands, 0 defs): op0=$uimm8 (the CSR address)
// The CSR operand is a uimm8 immediate; we read getImm directly. The.td
// marks it uimm8 so a well-formed MI always carries an immediate here; we
// still guard isImm against malformed/MIR test input.
int getHwloopCsrAddr(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  unsigned CsrOpIdx;
  if (Opc == Haydn::CSRW)
    CsrOpIdx = 1; // $csr_addr after the dead $rd def
  else if (Opc == Haydn::CSRW_W)
    CsrOpIdx = 0; // $uimm8 is the first operand
  else
    return -1;
  if (CsrOpIdx >= MI.getNumOperands())
    return -1;
  const MachineOperand &Csr = MI.getOperand(CsrOpIdx);
  if (!Csr.isImm())
    return -1;
  int64_t Addr = Csr.getImm();
  if (Addr >= 0x20 && Addr <= 0x25)
    return static_cast<int>(Addr);
  return -1;
}

// Per-cycle hazard state for the check. The recognizer is constructed
// once per scheduling region and regions are scheduled sequentially on a
// single thread, so file-scope state keyed to the current cycle is safe; it
// is cleared on every Reset/AdvanceCycle/RecedeCycle (all defined in this
//cpp) and on each emit that crosses a cycle. This mirrors the
// CurrentCycleHasLockedSlotOp member pattern without requiring a
// header change (this.cpp is the sole owned file for the change).
// @{
bool CurrentCycleHasHwloopSetup = false;
bool CurrentCycleHasHwloopCsrw = false;
// @}
} // namespace

//===----------------------------------------------------------------------===//
// HaydnFuncUnitWrapper
//===----------------------------------------------------------------------===//

HaydnFuncUnitWrapper::HaydnFuncUnitWrapper(const InstrStage &IS) {
  // The InstrStage Units_ bitmask bit N is set iff FU index N is in the
  // choice set. The FU indices line up 1:1 with our StaticBitSet bit
  // positions because HaydnSchedule.td declares exactly HAYDN_NUM_FU_BITS
  // FuncUnits (SLOT0=0, SLOT1=1, SLOT2=2). For a Required stage the whole
  // choice set is recorded as Required (the conflict rule then ensures two
  // single-slot instrs needing the same exclusive slot clash, while a
  // Slot012_ALU instr contributes a 3-bit set that never exclusively clashes
  // it only conflicts with another instr if the intersection is exactly one
  // bit, i.e. the other instr locked a single slot that this one also needs).
  if (IS.getReservationKind() == InstrStage::Required) {
    const uint64_t Units = IS.getUnits();
    for (unsigned Bit = 0; Bit < HAYDN_NUM_FU_BITS; ++Bit)
      if ((Units >> Bit) & 1u)
        Required.set(Bit);
  }
  // Reserved stages are not modeled on Haydn's current single-stage
  // itineraries. When Stream C introduces multi-stage itineraries with
  // Reserved units (e.g. shared load-store-unit lockout), this branch will
  // record them on a separate bitset to enforce the Reserved↔Required
  // exclusion rule (see AIE FuncUnitWrapper::conflict). For now there are no
  // such stages so this is intentionally empty.
}

bool HaydnFuncUnitWrapper::conflict(const HaydnFuncUnitWrapper &Other) const {
  // Slot exclusivity. On a 3-slot VLIW, the ONLY real slot conflict between
  // two instructions is: BOTH require the SAME single exclusive slot (both
  // have |Required| == 1 and it's the same bit). In that case the DFA cannot
  // assign them to different slots.
  //
  // A multi-slot instruction (e.g. Slot012_ALU, Required={S0,S1,S2}) NEVER
  // slot-conflicts with any other single instruction — it can always find a
  // free slot (the issue-count cap below limits how many can coexist). This
  // was the root cause of the B2 IPC regression : the old logic rejected
  // a Slot012_ALU candidate whenever the cycle already had a single-slot
  // occupant, because InterSize < ThisSize was true (1 < 3). That serialized
  // independent ALU ops that the packetizer packed — e.g. ZERO_GPR alongside
  // a DR64 load, or an ADDI32 alongside a D_LDW_POST_IMM.
  //
  // The issue-count cap (max 3 instrs/cycle) is what prevents over-subscribing
  // slots when multiple multi-slot ops compete; it is checked below.
  if (!Required.empty() && !Other.Required.empty()) {
    if (Required.count() == 1 && Other.Required.count() == 1 &&
        Required == Other.Required)
      return true;
  }

  // 3-issue VLIW cap.
  if (IssueCount + Other.IssueCount > 3)
    return true;

  // GPR 4R2W register-file port budget.
  if (GPRReads + Other.GPRReads > HAYDN_GPR_READ_PORTS)
    return true;
  if (GPRWrites + Other.GPRWrites > HAYDN_GPR_WRITE_PORTS)
    return true;

  // DR64 7R3W register-file port budget. Under the current slot model
  // max legal DR demand is 6R2W < 7R3W, so this never fires today — but it is
  // correct-by-construction once a fused-MAC op (4+ DR reads) or a dual-write
  // DR op lands, and it matches the RTL SVA contract regardless. The 3W
  // ceiling is the binding constraint in that future.
  if (DRReads + Other.DRReads > HAYDN_DR_READ_PORTS)
    return true;
  if (DRWrites + Other.DRWrites > HAYDN_DR_WRITE_PORTS)
    return true;

  // AR 2R2W register-file port budget (spec). Forward-compat: AR demand is
  // always 0 today (no AR-operand instruction), so this never fires — but it
  // is correct-by-construction once one lands and matches the RTL contract.
  if (ARReads + Other.ARReads > HAYDN_AR_READ_PORTS)
    return true;
  if (ARWrites + Other.ARWrites > HAYDN_AR_WRITE_PORTS)
    return true;

  return false;
}

void HaydnFuncUnitWrapper::dump() const {
  dbgs() << "{slots:";
  bool First = true;
  for (unsigned Bit = 0; Bit < HAYDN_NUM_FU_BITS; ++Bit) {
    if (Required.test(Bit)) {
      dbgs() << (First ? "" : "|") << Bit;
      First = false;
    }
  }
  if (First)
    dbgs() << "-";
  dbgs() << " issue:" << IssueCount << " gpr:" << GPRReads << "R/" << GPRWrites
         << "W dr:" << DRReads << "R/" << DRWrites << "W ar:" << ARReads << "R/"
         << ARWrites << "W}";
}

//===----------------------------------------------------------------------===//
// HaydnHazardRecognizer
//===----------------------------------------------------------------------===//

HaydnHazardRecognizer::HaydnHazardRecognizer(const TargetInstrInfo *TII,
                                             const InstrItineraryData *ItinData,
                                             bool IsPreRA,
                                             HaydnAlternateDescriptors *AltDescs)
    : TII(TII), ItinData(ItinData), IsPreRA(IsPreRA), AltDescs(AltDescs) {
  // Compute the scoreboard depth from the itineraries so the window covers
  // the deepest pipeline + max result latency. For Haydn's current
  // single-stage, latency-1 itineraries this is small (1-2), but Stream C
  // enrichments will make it larger and this computation adapts automatically.
  computeMaxLatency();
  const int Depth = std::max(getPipelineDepth(), 1);
  Scoreboard.reset(Depth);
  MaxLookAhead = static_cast<unsigned>(Depth);
}

void HaydnHazardRecognizer::computeMaxLatency() {
  if (!ItinData || ItinData->isEmpty()) {
    MaxLatency = 1;
    PipelineDepth = 1;
    return;
  }
  int MaxPipelineDepth = 1;
  int MaxOpLatency = 1;
  // Walk every scheduling class.
  for (unsigned SchedClass = 0; !ItinData->isEndMarker(SchedClass); ++SchedClass) {
    // Pipeline depth from the stage chain.
    unsigned StartCycle = 0;
    unsigned StageDepth = 0;
    for (const InstrStage *IS = ItinData->beginStage(SchedClass),
                          *E = ItinData->endStage(SchedClass);
         IS != E; ++IS) {
      StageDepth = std::max(StageDepth, StartCycle + IS->getCycles());
      StartCycle += IS->getNextCycles();
    }
    MaxPipelineDepth = std::max(MaxPipelineDepth, static_cast<int>(StageDepth));

    // Operand/result latencies.
    int FirstOp = ItinData->Itineraries[SchedClass].FirstOperandCycle;
    int LastOp = ItinData->Itineraries[SchedClass].LastOperandCycle;
    if (FirstOp < LastOp) {
      for (int OpIdx = FirstOp; OpIdx < LastOp; ++OpIdx) {
        const unsigned Lat = ItinData->OperandCycles[OpIdx];
        if (Lat > 0)
          MaxOpLatency = std::max(MaxOpLatency, static_cast<int>(Lat));
      }
    }
  }
  PipelineDepth = MaxPipelineDepth;
  MaxLatency = MaxOpLatency;
}

void HaydnHazardRecognizer::Reset() {
  Scoreboard.clear();
  CurrentCycleDefs.clear();
  CurrentCycleLiveDefs.clear();
  CurrentCycleHasLockedSlotOp = false;
  CurrentCycleHasHwloopSetup = false;
  CurrentCycleHasHwloopCsrw = false;
  CurrentCycleSlots = 0;
  TRI = nullptr;
}

// Build the per-cycle resource footprint of MI at the current cycle:
// the union of MI's slot sets, plus the GPR port demand and one issue-count.
// This is the building block for both getHazardType (which calls conflict)
// and EmitInstruction (which calls operator|=).
HaydnFuncUnitWrapper
HaydnHazardRecognizer::buildCandidate(const MachineInstr &MI) const {
  HaydnFuncUnitWrapper Candidate;
  // The candidate represents ONE instruction's footprint: IssueCount=1 and
  // its slot/ports. Empty itinerary (pseudo) still counts as 1 issue.
  Candidate.setIssueCountOne();

  // pack-fill: slot Required bits come from FlexMap legal slots when
  // the opcode has a flex family. Several logical opcodes (notably
  // D_LDW_CB_IMM, D_LDW_POST_IMM) still carry a single-slot itinerary
  // (Slot0_LS) even though FlexMap places them in S0/S1/S2. Using the
  // itinerary alone made two independent CB loads look like exclusive S0
  // conflicts and serialized them — dual-load + MAC fill never formed.
  // Multi-bit Required never exclusive-conflicts; the CurrentCycleSlots
  // auction remains the real one-instr-per-slot gate.
  SlotBits Legal = Fmts.getLegalSlots(MI.getOpcode());
  if (Legal != 0) {
    StaticBitSet<HAYDN_NUM_FU_BITS> Slots;
    for (unsigned Bit = 0; Bit < HAYDN_NUM_FU_BITS; ++Bit)
      if ((Legal >> Bit) & 1u)
        Slots.set(Bit);
    Candidate.mergeRequired(Slots);
  } else if (ItinData && !ItinData->isEmpty()) {
    // No flex family (standalone WIDE / rare ops): fall back to itinerary
    // stage units so exclusive single-slot ops still conflict correctly.
    const unsigned SchedClass = TII->get(MI.getOpcode()).getSchedClass();
    for (const InstrStage *IS = ItinData->beginStage(SchedClass),
                          *E = ItinData->endStage(SchedClass);
         IS != E; ++IS) {
      HaydnFuncUnitWrapper StageFootprint(*IS);
      Candidate.mergeRequired(StageFootprint.getRequired());
    }
  }

  // GPR port demand for this instruction (shared countGPRPorts helper
  // identical accounting to HaydnVLIWPacketizer so the two stay consistent
  // until Phase B2 retires the packetizer).
  auto [Reads, Writes] = countGPRPorts(MI);
  Candidate.setGPRPorts(Reads, Writes);

  // DR64 port demand (countDRPorts). Separate 7R3W budget from the GPR
  // 4R2W budget — DR64 is its own register file.
  auto [DRReads, DRWrites] = countDRPorts(MI);
  Candidate.setDRPorts(DRReads, DRWrites);
  // AR (aligned-register) port demand (countARPorts). Spec 2R2W budget. No
  // Haydn instruction currently lists an AR operand, so this is always 0 today
  // (forward-compat; correct-by-construction when an AR-operand op lands).
  auto [ARReads, ARWrites] = countARPorts(MI);
  Candidate.setARPorts(ARReads, ARWrites);
  return Candidate;
}

const TargetRegisterInfo *
HaydnHazardRecognizer::getTRI(const MachineInstr &MI) {
  if (!TRI)
    TRI = MI.getMF()->getSubtarget().getRegisterInfo();
  return TRI;
}

bool HaydnHazardRecognizer::hasSameBundleWAW(const MachineInstr &MI) const {
  // same-bundle destination-register WAW. The candidate's defs are
  // checked here (not in buildCandidate, which carries only port counts) for
  // overlap against CurrentCycleDefs via regsOverlap — the same alias semantic
  // as MachineInstr::modifiesRegister (the retired packetizer's
  // hasWAWHazard). R0 (soft-zero) and SFR (slot-ordered safe parallel
  // writes) are excluded. Returns false if TRI is not yet cached: that
  // only happens before the first emit, when CurrentCycleDefs is empty and no
  // WAW is possible anyway.
  if (!TRI)
    return false;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (!Reg || !Reg.isPhysical())
      continue;
    if (Reg == Haydn::R0 || Reg == Haydn::SFR)
      continue;
    for (Register D : CurrentCycleDefs)
      if (TRI->regsOverlap(Reg, D))
        return true;
  }
  return false;
}

bool HaydnHazardRecognizer::hasSameBundleRAW(const MachineInstr &MI) const {
  // same-bundle RAW (the dual of WAW). Haydn spec §Constraints:
  // "All instructions within the same bundle read their source registers
  // simultaneously" — there is no intra-bundle forwarding, so a reader that
  // shares a cycle with a writer of the same register observes the OLD value.
  //
  // BUT a same-bundle read+write is only a hazard when it is a TRUE RAW — i.e.
  // the reader consumes the writer's value. If the write is DEAD (no consumer)
  // the reader correctly observes the OLD value and the bundle is legal (WAR
  // style). So we check the candidate's reads against CurrentCycleLiveDefs
  // (live defs only), not the full CurrentCycleDefs. The dead flag is the
  // scheduling-DAG/liveness verdict on "does this def have a consumer"; checking
  // it is equivalent to consulting the SDag for a real RAW edge, without
  // storing SUnit pointers (UAF avoidance).
  //
  // The scheduler issues writers before their RAW readers (topological order)
  // so a live writer is in CurrentCycleLiveDefs by the time its consumer-reader
  // is evaluated. WAR (reader issued before writer) does not trip: the writer
  // candidate does not read that reg, and WAR in a bundle is legal on Haydn
  // anyway. R0 (soft-zero) and SFR are excluded as in hasSameBundleWAW.
  // root cause (its prologue write was LIVE — consumed by the same-bundle
  // store — so this still trips).
  if (!TRI)
    return false;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.readsReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg || !Reg.isPhysical())
      continue;
    if (Reg == Haydn::R0 || Reg == Haydn::SFR)
      continue;
    for (Register D : CurrentCycleLiveDefs)
      if (TRI->regsOverlap(Reg, D))
        return true;
  }
  return false;
}

void HaydnHazardRecognizer::appendDefs(const MachineInstr &MI) {
  // record this instruction's destination registers. R0/SFR
  // excluded (matching the hazard checks). Every def goes into CurrentCycleDefs
  // for the WAW check (spec forbids two writes to one register regardless of
  // liveness); only LIVE defs go into CurrentCycleLiveDefs for the RAW check
  // because a dead write has no consumer and a same-bundle reader of that reg
  // correctly observes the OLD value.
  if (!TRI)
    (void)getTRI(MI);
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (!Reg || !Reg.isPhysical())
      continue;
    if (Reg == Haydn::R0 || Reg == Haydn::SFR)
      continue;
    CurrentCycleDefs.insert(Reg);
    if (!MO.isDead())
      CurrentCycleLiveDefs.insert(Reg);
  }
}

bool HaydnHazardRecognizer::isLockedSlotDspOp(const MachineInstr &MI) const {
  // ARCTAN/SIN_COS lock their issuing slot's D-ALU for (uimm4+2)
  // bundles (spec §Special). Identified by opcode (enum, no magic numbers).
  unsigned Opc = MI.getOpcode();
  return Opc == Haydn::ARCTAN || Opc == Haydn::SIN_COS;
}

ScheduleHazardRecognizer::HazardType
HaydnHazardRecognizer::getHazardType(SUnit *SU, int DeltaCycles) {
  if (!SU)
    return NoHazard;
  MachineInstr *MI = SU->getInstr();
  if (!MI)
    return NoHazard;

  // Bundles and meta-instructions do not consume issue resources.
  if (MI->isBundle() || MI->isDebugInstr() || MI->isPseudo())
    return NoHazard;

  const HaydnFuncUnitWrapper Candidate = buildCandidate(*MI);

  if (Scoreboard[DeltaCycles].conflict(Candidate)) {
    LLVM_DEBUG({
      dbgs() << "Hazard for ";
      MI->print(dbgs());
      dbgs() << " at delta " << DeltaCycles << " (cycle has ";
      Scoreboard[DeltaCycles].dump();
      dbgs() << ", candidate needs ";
      Candidate.dump();
      dbgs() << ")\n";
    });
    return Hazard;
  }

  // same-bundle destination-register WAW (see CurrentCycleDefs doc).
  // Only meaningful at DeltaCycles == 0 (CurrentCycleDefs records instrs already
  // issued THIS cycle). Returning Hazard defers the candidate so its consumers
  // are placed with correct latencies. Cache TRI up front for hasSameBundleWAW.
  (void)getTRI(*MI);
  if (DeltaCycles == 0 && hasSameBundleWAW(*MI)) {
    LLVM_DEBUG({
      dbgs() << "WAW hazard for ";
      MI->print(dbgs());
      dbgs() << " (cycle already writes an overlapping def)\n";
    });
    return Hazard;
  }

  // same-bundle RAW (see hasSameBundleRAW doc). The dual of the WAW
  // gate: Haydn has no intra-bundle forwarding (all reads happen in ID before
  // any write commits), so a reader cannot share a cycle with its writer — it
  // would observe the OLD value. This is the true root cause of (the
  // strcmp-loop miscompile was a broken PROLOGUE constant-init store bundle
  // not the loop). Same family as; prior revision only patched the
  // materializeBundles splice path — this gates the scheduler's cycle
  // assignment directly.
  if (DeltaCycles == 0 && hasSameBundleRAW(*MI)) {
    LLVM_DEBUG({
      dbgs() << "RAW hazard for ";
      MI->print(dbgs());
      dbgs() << " (cycle already writes a source this instr reads; Haydn has "
                "no intra-bundle forwarding)\n";
    });
    return Hazard;
  }

  // CSRW↔SET_HWLOOP same-bundle hazard (spec §5.10: a CSRW targeting
  // CSR 0x20-0x25 = HWLR_BEGIN/END/COUNT must NOT share a bundle with any
  // SET_HWLOOP variant — they race the implicit HWLR update). The two sides
  // are tracked bidirectionally in CurrentCycleHasHwloop{Setup,Csrw} so the
  // hazard fires regardless of which side was issued first. Only meaningful
  // at DeltaCycles == 0 (the flags record instrs issued THIS cycle). See
  // isHwloopSetupOp / getHwloopCsrAddr in the anonymous namespace above.
  if (DeltaCycles == 0) {
    bool IsHwloopSetup = isHwloopSetupOp(MI->getOpcode());
    bool IsHwloopCsrw = getHwloopCsrAddr(*MI) >= 0;
    if ((IsHwloopSetup && CurrentCycleHasHwloopCsrw) ||
        (IsHwloopCsrw && CurrentCycleHasHwloopSetup)) {
      LLVM_DEBUG({
        dbgs() << "CSRW↔SET_HWLOOP hazard for ";
        MI->print(dbgs());
        dbgs() << " (CSR 0x20-0x25 HWLR write vs SET_HWLOOP variant in same "
                  "cycle; spec §5.10)\n";
      });
      return Hazard;
    }
  }

  // minimal guard for ARCTAN/SIN_COS (DSP math ops that lock their
  // issuing slot's D-ALU for (uimm4+2) bundles, spec §Special). Force them
  // into a single-instruction bundle: a locked op may not join a non-empty
  // cycle, and nothing may join a cycle that already issued one. Deliberately
  // over-conservative but safe; the precise variable-length (uimm4+2) slot
  // reservation is deferred to M5 (real encoding). Prevents the worst case
  // (NoItinerary invisibility + intra-bundle consumer of the multi-cycle
  // result) for these rare ops today.
  if (DeltaCycles == 0) {
    bool IsLocked = isLockedSlotDspOp(*MI);
    if (IsLocked && Scoreboard[DeltaCycles].getIssueCount() > 0) {
      LLVM_DEBUG({
        dbgs() << "Locked-slot hazard for ";
        MI->print(dbgs());
        dbgs() << " (ARCTAN/SIN_COS must issue alone)\n";
      });
      return Hazard;
    }
    if (!IsLocked && CurrentCycleHasLockedSlotOp) {
      LLVM_DEBUG({
        dbgs() << "Locked-slot hazard for ";
        MI->print(dbgs());
        dbgs() << " (cycle already issued an ARCTAN/SIN_COS)\n";
      });
      return Hazard;
    }
  }

  // single-authority slot auction (current cycle only). The candidate's
  // legal slots come from the FlexMap (Fmts.getLegalSlots — the tblgen ground
  // truth), NOT the hand DClass getAltSlotSet/itinerary. A candidate is a
  // Hazard if none of its legal slots is free this cycle (AIE isFormatAvailable
  // Hexagon HexagonUnitAuction equivalent) — the scheduler then defers it to
  // the next cycle, so an un-encodable bundle (two ops forced to the same slot)
  // is never formed. Ops with no flex family (getLegalSlots==0: standalone
  // WIDE, pseudo) are not bundle-slot ops — defer to the other gates.
  if (DeltaCycles == 0) {
    SlotBits Avail = Fmts.getLegalSlots(MI->getOpcode());
    if (Avail != 0 && (Avail & ~CurrentCycleSlots) == 0) {
      LLVM_DEBUG({
        dbgs() << "Slot-auction hazard for ";
        MI->print(dbgs());
        dbgs() << " (legal slots full this cycle; no distinct slot)\n";
      });
      return Hazard;
    }
  }

  return NoHazard;
}

void HaydnHazardRecognizer::emitInstruction(SUnit *SU, int DeltaCycles) {
  if (!SU)
    return;
  MachineInstr *MI = SU->getInstr();
  if (!MI || MI->isBundle() || MI->isDebugInstr() || MI->isPseudo())
    return;

  const HaydnFuncUnitWrapper Candidate = buildCandidate(*MI);
  Scoreboard[DeltaCycles] |= Candidate;
  // record this instr's defs so a later same-cycle candidate is checked
  // for a destination-register WAW overlap.
  if (DeltaCycles == 0) {
    appendDefs(*MI);
    // single-authority slot auction. Pick the lowest FREE legal slot
    // (FlexMap-derived via Fmts.getLegalSlots) for this candidate, occupy it
    // and record it in AltDescs for materializeMultiOpcodeInstrs to commit.
    // getHazardType already guaranteed a free legal slot exists.
    SlotBits Avail = Fmts.getLegalSlots(MI->getOpcode());
    if (Avail != 0) {
      for (unsigned S = 0; S < 3; ++S)
        if (((Avail >> S) & 1u) && !((CurrentCycleSlots >> S) & 1u)) {
          CurrentCycleSlots |= (SlotBits(1) << S);
          if (EnableHaydnHRSlotSelect && AltDescs)
            AltDescs->setSlot(MI, S);
          break;
        }
    }
    // track ARCTAN/SIN_COS issue for the single-instruction-bundle guard.
    if (isLockedSlotDspOp(*MI))
      CurrentCycleHasLockedSlotOp = true;
    // track CSRW↔SET_HWLOOP same-bundle hazard (spec §5.10).
    if (isHwloopSetupOp(MI->getOpcode()))
      CurrentCycleHasHwloopSetup = true;
    if (getHwloopCsrAddr(*MI) >= 0)
      CurrentCycleHasHwloopCsrw = true;
  }
  LLVM_DEBUG({
    dbgs() << "Emit ";
    MI->print(dbgs());
    dbgs() << " at delta " << DeltaCycles << " -> cycle now ";
    Scoreboard[DeltaCycles].dump();
    dbgs() << "\n";
  });
}

void HaydnHazardRecognizer::EmitInstruction(SUnit *SU) {
  // The MachineScheduler calls this overload with DeltaCycles implicit (the
  // framework tracks the SUnit's emit cycle and has already consulted
  // getHazardType at the chosen delta). We record at DeltaCycles=0 (current
  // cycle); if a future scheduler variant passes an explicit delta, use
  // emitInstruction(SU, Delta) instead.
  emitInstruction(SU, /*DeltaCycles=*/0);
}

void HaydnHazardRecognizer::EmitInstruction(MachineInstr *MI) {
  // Non-scheduling-pass overload. Wrap the MI in a trivial SUnit-like path:
  // build the candidate directly and record it. There is no SUnit available
  // so reuse buildCandidate via a synthetic path.
  if (!MI || MI->isBundle() || MI->isDebugInstr() || MI->isPseudo())
    return;
  const HaydnFuncUnitWrapper Candidate = buildCandidate(*MI);
  Scoreboard[0] |= Candidate;
  appendDefs(*MI);
  // single-authority slot auction (see emitInstruction). Non-scheduling
  // pass overload: occupy the lowest free legal FlexMap slot + record it.
  SlotBits Avail = Fmts.getLegalSlots(MI->getOpcode());
  if (Avail != 0) {
    for (unsigned S = 0; S < 3; ++S)
      if (((Avail >> S) & 1u) && !((CurrentCycleSlots >> S) & 1u)) {
        CurrentCycleSlots |= (SlotBits(1) << S);
        if (EnableHaydnHRSlotSelect && AltDescs)
          AltDescs->setSlot(MI, S);
        break;
      }
  }
  if (isLockedSlotDspOp(*MI))
    CurrentCycleHasLockedSlotOp = true;
  // track CSRW↔SET_HWLOOP same-bundle hazard (spec §5.10).
  if (isHwloopSetupOp(MI->getOpcode()))
    CurrentCycleHasHwloopSetup = true;
  if (getHwloopCsrAddr(*MI) >= 0)
    CurrentCycleHasHwloopCsrw = true;
}

void HaydnHazardRecognizer::AdvanceCycle() {
  CurrentCycleDefs.clear();
  CurrentCycleLiveDefs.clear();
  CurrentCycleHasLockedSlotOp = false;
  CurrentCycleHasHwloopSetup = false;
  CurrentCycleHasHwloopCsrw = false;
  CurrentCycleSlots = 0;
  Scoreboard.advance();
}

void HaydnHazardRecognizer::RecedeCycle() {
  CurrentCycleDefs.clear();
  CurrentCycleLiveDefs.clear();
  CurrentCycleHasLockedSlotOp = false;
  CurrentCycleHasHwloopSetup = false;
  CurrentCycleHasHwloopCsrw = false;
  CurrentCycleSlots = 0;
  Scoreboard.recede();
}

bool HaydnHazardRecognizer::atIssueLimit() const {
  return Scoreboard[0].getIssueCount() >= IssueLimit;
}
