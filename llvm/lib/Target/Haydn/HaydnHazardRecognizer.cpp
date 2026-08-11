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
#include "HaydnPlacementAlternative.h"
#include "HaydnPortModel.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::haydn::bundle;

//===----------------------------------------------------------------------===//
// applyFormatOrdering — AIEHazardRecognizer.cpp:278-314 peer
//===----------------------------------------------------------------------===//
// Rebuild BUNDLE children in Format.getSlots() field order. Product
// BUNDLE128_FULL FormatSlotData is S2,S1,S0 (HaydnGenFormats.inc). Call site
// is finalizeLegalMultiMI when size()>1 (AIE applyBundles 343-344).
void llvm::applyFormatOrdering(Haydn::MachineBundle &Bundle,
                               const VLIWFormat &Format,
                               MachineBasicBlock::iterator InsertPoint) {
  assert(Bundle.getSlotMap().size() == Bundle.getInstrs().size() &&
         "Bundle has instructions without slot");
  if (Bundle.empty())
    return;

  MachineBasicBlock &MBB = *Bundle.getInstrs()[0]->getParent();
  // AIE uses TII->getSlotInfo; Haydn slot tables live on HaydnMCFormats.
  HaydnMCFormats Fmts;

  // Run over the slots of the format and re-insert the occupying
  // instruction. Reapply bundling. (AIE may insert NOPs for empty slots;
  // product Bundle128 leaves holes un-filled — only present members move.)
  MachineInstr *FirstMI = nullptr;
  for (MCSlotKind Slot : Format.getSlots()) {
    const MCSlotInfo *SlotInfo = Fmts.getSlotInfo(Slot);
    assert(SlotInfo);
    (void)SlotInfo;

    MachineInstr *Instr = Bundle.at(Slot);
    if (!Instr)
      continue;

    Instr->removeFromBundle();
    MBB.insert(InsertPoint, Instr);
    if (!FirstMI)
      FirstMI = Instr;
    else
      Instr->bundleWithPred();
  }

  // AIE skips finalize on AIE1; Haydn always finalizes so kill flags and
  // bundle-header properties are correct (AIEHazardRecognizer.cpp:309-312).
  assert(FirstMI && "applyFormatOrdering: format had no members");
  finalizeBundle(MBB, FirstMI->getIterator());
}

#define DEBUG_TYPE "haydn-hazard-rec"

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
// These helpers identify both sides of the hazard. They cover every
// hwloop-setup opcode: the two pre-expansion pseudos (SET_HWLOOP_PSEUDO and
// SET_HWLOOP_F2_PSEUDO, which carry MBB operands until HaydnExpandPseudos
// converts them), plus the three real instructions the database names —
// SET_HWLOOP, SET_HWLOOP_F2 and SET_HWLOOP_REG. The pseudo variants are
// normally skipped by getHazardType's isPseudo early-out, but the guard
// is correct-by-construction regardless: if a future pass lowers a hwloop
// setup to a real (non-pseudo) opcode before postmisched, the hazard fires
// automatically.

// True iff MI is any SET_HWLOOP variant (the writer side of the hazard).
bool isHwloopSetupOp(unsigned Opcode) {
  return Opcode == Haydn::SET_HWLOOP_PSEUDO || Opcode == Haydn::SET_HWLOOP_F2_PSEUDO ||
         Opcode == Haydn::SET_HWLOOP || Opcode == Haydn::SET_HWLOOP_F2 ||
         Opcode == Haydn::SET_HWLOOP_REG;
}

// If MI is a CSRW whose CSR operand is in the HWLR range 0x20-0x25, return
// that CSR address; otherwise return -1.
// Operand layout (HaydnInstrInfo.td): CSRW is (ins uimm8:$csr_addr,
// GPR32:$rs), so the CSR address is op0. It used to carry a leading dead $rd
// for MC parity, which forced a second case for the 2-operand CSRW_W; CSRW
// now has that same shape and CSRW_W is no longer selected by anything, so
// the two cases collapse into one.
// The CSR operand is a uimm8 immediate; we read getImm directly. The .td
// marks it uimm8 so a well-formed MI always carries an immediate here; we
// still guard isImm against malformed/MIR test input.
int getHwloopCsrAddr(const MachineInstr &MI) {
  if (MI.getOpcode() != Haydn::CSRW)
    return -1;
  const unsigned CsrOpIdx = 0;
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
  // positions because HAYDN_NUM_FU_BITS covers every FuncUnit
  // HaydnItineraries declares: the three retired slots at 0..2 and the seven
  // format E units at 3..9. It covered only the first three until recently,
  // which made this whole class inert -- see the header. For a Required stage
  // the whole
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
  // Unit exclusivity, and it is a pairwise approximation of a matching
  // problem: no two entries of a bundle may map to the same unit, so a set of
  // instructions is legal iff their Available sets admit a system of distinct
  // representatives. What is checked here is the pair case — BOTH require the
  // same single unit — which is exact for two and conservative-in-the-wrong-
  // direction for three (three instructions each needing {ALU1, ALU2} pass
  // pairwise and cannot all issue). The packer's PlacementAlternative search
  // is what decides legality (FORMAT-E-SWITCH-PLAN.md section 7); this only
  // has to stop the scheduler proposing cycles the packer will reject, and
  // the single-unit case is the one that matters — two stores both needing
  // LOADSTORE0 is the common one.
  //
  // A multi-unit instruction (e.g. Unit_ALU0ALU1ALU2_L1) NEVER conflicts with
  // any other single instruction — it can always find a free unit (the
  // issue-count cap below limits how many can coexist). This
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
    : TII(TII), ItinData(ItinData), IsPreRA(IsPreRA), AltDescs(AltDescs),
      Fmts(*TII) {
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
  CurrentCycleState = makeProductCycleState();
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

  // pack-fill: multi-bit Required from PlacementAlternative FieldSlots
  // (alts-only; no getLegalSlots fallback) so two multi-slot ops never
  // exclusive-conflict on Required alone. One-instr-per-slot is gated by
  // product CycleState tryAdd, not Required bits.
  SlotBits Legal = 0;
  {
    SmallVector<PlacementAlternative, 4> Alts;
    if (enumeratePlacementAlternatives(Fmts, MI.getOpcode(), Alts)) {
      for (const PlacementAlternative &A : Alts)
        Legal |= A.FieldSlots;
    }
    // No-alt: Legal stays 0 → itinerary stage fallback below (standalone).
  }
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
  // hasWAWHazard).
  //
  // R0 is *soft*-zero (not hardwired): XOR32 R0,R0,R0 and LD into R0 are real
  // write-port consumers. Excluding R0 allowed illegal packets such as
  //   { xor32 r0,r0,r0; ld32 r0, base, 0; ... }
  // which BundleSim correctly rejects as WRITE_CONFLICT (va-arg-14 @ 0x10340
  // and every other WRITE_CONFLICT in gcc-torture). Only SFR stays excluded
  // (slot-ordered safe parallel implicit-def $sfr).
  // Returns false if TRI is not yet cached: that only happens before the first
  // emit, when CurrentCycleDefs is empty and no WAW is possible anyway.
  if (!TRI)
    return false;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (!Reg || !Reg.isPhysical())
      continue;
    if (Reg == Haydn::SFR)
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
  // anyway. SFR excluded (parallel dead implicit-def $sfr is normal).
  // R0 is NOT excluded: soft-zero is a real register (borrow/restore); a
  // same-bundle reader of a live R0 write would observe the OLD value.
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
    if (Reg == Haydn::SFR)
      continue;
    for (Register D : CurrentCycleLiveDefs)
      if (TRI->regsOverlap(Reg, D))
        return true;
  }
  return false;
}

void HaydnHazardRecognizer::appendDefs(const MachineInstr &MI) {
  // record this instruction's destination registers. SFR excluded
  // (matching the hazard checks). R0 is tracked: soft-zero restores and
  // R0-borrow loads are real defs; omitting them let PostRA pack
  // XOR32 R0,R0,R0 with LD …,R0 into one bundle → WRITE_CONFLICT.
  // Every def goes into CurrentCycleDefs for the WAW check (spec forbids two
  // writes to one register regardless of liveness); only LIVE defs go into
  // CurrentCycleLiveDefs for the RAW check because a dead write has no
  // consumer and a same-bundle reader of that reg correctly observes the OLD
  // value.
  if (!TRI)
    (void)getTRI(MI);
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (!Reg || !Reg.isPhysical())
      continue;
    if (Reg == Haydn::SFR)
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

void HaydnHazardRecognizer::commitPlacementForEmit(MachineInstr *MI) {
  // Port of AIEResourceCycle::reserveResources alt try
  // (AIEHazardRecognizer.cpp:197-214): getAlternateInstsOpcode + first
  // canAdd AltOpcode → Bundle.add. Haydn maps that to tryAddProduct.
  // Stamp MemberOpcode via setAlternateDescriptor (AIE
  // AIEHazardRecognizer.cpp:389 SelectedAltDescs.setAlternateDescriptor)
  // so leaveRegion materializeMultiOpcodeInstrs can MI.setDesc.
  // Placement after materialize is getSlotKind on the member Desc
  // (AIEBaseMCFormats.cpp:66-75). Alts-only — no getLegalSlots fallback.
  const unsigned Opc = MI->getOpcode();
  if (!hasPlacementAlternatives(Fmts, Opc))
    return;
  if (!tryAddProduct(CurrentCycleState, Fmts, Opc)) {
    // getHazardType should have rejected; still tolerate empty-escape /
    // race by leaving state unchanged.
    LLVM_DEBUG(dbgs() << "commitPlacementForEmit: tryAdd failed for opc "
                      << Opc << "\n");
    return;
  }
  const haydn::bundle::CycleMember &Member =
      CurrentCycleState.Members.back();
  if (AltDescs && TII) {
    // AIE AIEHazardRecognizer.cpp:389 — record selected member opcode for
    // leaveRegion setDesc materialize (AIEAlternateDescriptors.h:39-44).
    AltDescs->setAlternateDescriptor(MI, Member.MemberOpcode, *TII);
  }
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
        dbgs() << "CSRW↔SET_HWLOOP_PSEUDO hazard for ";
        MI->print(dbgs());
        dbgs() << " (CSR 0x20-0x25 HWLR write vs SET_HWLOOP_PSEUDO variant in same "
                  "cycle; spec §5.10)\n";
      });
      return Hazard;
    }
  }

  // ARCTAN/SIN_COS (current design): issue alone this cycle only — no
  // multi-cycle slot lock / (uimm4+2) scoreboard. Prevents packing an
  // independent op into the same bundle as a locked DSP op.
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

  // Placement authority (current cycle only): product CycleState
  // tryAddProduct — AIEHazardRecognizer.cpp:174-214 alt try /
  // AIEBundle.h:62-105 canAdd. Alts-only (no getLegalSlots fallback).
  // Ops without PlacementAlternatives skip the gate (standalone / no-slot).
  if (DeltaCycles == 0) {
    const unsigned Opc = MI->getOpcode();
    if (hasPlacementAlternatives(Fmts, Opc) &&
        !canTryAddProduct(CurrentCycleState, Fmts, Opc)) {
      LLVM_DEBUG({
        dbgs() << "tryAdd hazard for ";
        MI->print(dbgs());
        dbgs() << " (no free PlacementAlternative field this cycle; occ="
               << CurrentCycleState.OccupiedSlots << ")\n";
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
    // Commit field via product tryAdd (S2→S1→S0). Stamp
    // setAlternateDescriptor(MemberOpcode) (AIEHazardRecognizer.cpp:389).
    // setDesc happens in leaveRegion materializeMultiOpcodeInstrs
    // (AIEMachineScheduler.cpp:1121-1139). getHazardType already probed
    // canTryAdd for alts-bearing logicals.
    commitPlacementForEmit(MI);
    // ARCTAN/SIN_COS: alone this cycle only (no multi-cycle slot lock).
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
  // Same tryAdd commit + setAlternateDescriptor as emitInstruction.
  commitPlacementForEmit(MI);
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
  CurrentCycleState = makeProductCycleState();
  Scoreboard.advance();
}

void HaydnHazardRecognizer::RecedeCycle() {
  CurrentCycleDefs.clear();
  CurrentCycleLiveDefs.clear();
  CurrentCycleHasLockedSlotOp = false;
  CurrentCycleHasHwloopSetup = false;
  CurrentCycleHasHwloopCsrw = false;
  CurrentCycleState = makeProductCycleState();
  Scoreboard.recede();
}

bool HaydnHazardRecognizer::atIssueLimit() const {
  return Scoreboard[0].getIssueCount() >= IssueLimit;
}
