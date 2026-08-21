//===-- HaydnPortModel.h - Haydn GPR port accounting ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared register-file port accounting for the Haydn VLIW architecture.
//
// Pooled per-cycle budgets across the seven Format E execution units
// (constraints §Registers / port table):
// * GPR32 — 4 read / 2 write (golden Constraints §Registers; no golden
// source publishes a 5R3W physical or AGU-bypass figure — old folklore).
// * DR64 — 8 read / 3 write (stated twice in golden Constraints; GE96-10).
// * AR — 2 read / 2 write.
// * SFR — 2 read / 1 write (and at most one SFR writer per bundle).
//
// Single port authority for post-RA packing AND pre-RA HR.
// HaydnHazardRecognizer builds HaydnFuncUnitWrapper port counts from these
// helpers. AIE-style exclusive named-port FuncUnits in the itinerary are the
// wrong model for Haydn's shared pools (see HaydnSchedule.td).
//
// Pre-RA (vreg) path: physreg class membership alone treats every virtual
// register as port-free. Classification uses MachineRegisterInfo regclass
// facts (BankRC.hasSubClassEq(RC)) so GPR32/DR64/AR vregs charge the same
// pooled demand as equivalent physregs. Every explicit operand field
// reserves one port (MOVE32 rd,rs,rs is 2R1W). Tied use/defs still charge
// independently (one R + one W). Unknown regs (no class yet) stay
// existential / uncounted.
//
// SMS-RESMII: the pure *format* ResMII oracle lives in
// HaydnBundleFormatSolver (computeExhaustiveProductResMII) and is exposed on
// HaydnPreRASchedStrategy. MI-aware DFA ResMII additionally charges these
// pooled ports via count*Ports / ResourceCycle (sibling SMS track). Pre-RA HR
// uses the same PortModel so vreg demand never undercounts vs SMS MI packing.
//
// Port lower-bound ResMII (haydnPortLowerBoundResMII): ceil of total bank
// demand over per-cycle budgets — independent of format packing. Three
// independent GPR writes force ≥2 cycles under HAYDN_GPR_WRITE_PORTS=2 even
// when Full has three slots (format-only ResMII can still report 1).
// Soft-exit QoR (VF3): PreRASchedStrategy::productSoftExitIIFloor takes
// max(format exhaustive ResMII, this port floor) so II floors cannot drop
// below either bound. RecMII is not computed here (DDG/SMS sibling).
//
// SMS-HANDOFF / packability: PortModel is metrics and feasibility only on the
// pre-RA path. It never invents hard BUNDLE membership. Qualification kernels
// that stay under bank budgets remain candidates for post-RA exact no-split
// pack; the durable clone→BUNDLE handoff is a separate SMS-gate (sibling).
//
// Ports are issue-cycle capacity (pooled demand per issue). They do not model
// multi-cycle FU occupancy wrapping under SMS II. Anonymous cross-cycle
// capacity / II-wrap false-accept is SMS-HOOK catalog + PreRASchedStrategy
// fail-closed polarity (product stages cycles==1); PortModel stays issue-only.
//
// Format-acceptance differential (plan §8.4 #7) is orthogonal to ports:
// descriptor-derived slot legality is pure exactTryAddProduct (opcode-keyed),
// shared by ResourceCycle and post-RA / pre-RA HR. MI and descriptor
// *port* demand for MOVE32 are both 2R1W (per-field). That is not a
// format accept/reject gap — format polarity agrees for equal opcodes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H

#include "HaydnFormatERecords.h"
#include "HaydnRegisterInfo.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegister.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

namespace llvm {

// GPR port budget constants — golden VLIW_Engine_Compiler_Constraints.md
// §Registers ("4 read ports and 2 write ports"); no golden source publishes
// a 5R3W physical or AGU-bypass figure (old folklore, comment-only).
inline constexpr unsigned HAYDN_GPR_READ_PORTS = 4;
inline constexpr unsigned HAYDN_GPR_WRITE_PORTS = 2;

// DR64 port budget constants — golden VLIW_Engine_Compiler_Constraints.md
// states 8 read ports and 3 write ports twice (§Registers DR bullet; §bundle
// port law "[GPR : 4 / DR : 8 / AR : 2 / SFR : 2] read"). GE96-10
// reconciliation 2026-08-21: the prior 7R constant cited
// formal_verification_decoder_sva.json / port_budget_analysis.py — files
// outside the golden nine (non-authority per CLAUDE.md; the SVA citation
// does not exist in golden). Golden wins: 8R/3W, shared across all three
// slots. Historical note: 7R was the shipped conservative value (stricter
// direction — no illegal bundle was ever admitted under it); the golden
// value restores truthful capacity for 8-read cycles.
inline constexpr unsigned HAYDN_DR_READ_PORTS = 8;
inline constexpr unsigned HAYDN_DR_WRITE_PORTS = 3;

// AR (aligned-register) port budget constants — 2R2W shared across units
// (VLIW_Engine_Compiler_Constraints.md §Registers).
inline constexpr unsigned HAYDN_AR_READ_PORTS = 2;
inline constexpr unsigned HAYDN_AR_WRITE_PORTS = 2;

// SFR port budget constants — 2R1W shared across units. Constraints also
// require at most one SFR writer per bundle (write budget 1 encodes that).
inline constexpr unsigned HAYDN_SFR_READ_PORTS = 2;
inline constexpr unsigned HAYDN_SFR_WRITE_PORTS = 1;

/// True only when the complete per-operation port / latency / pipeline /
/// required-alignment table has been admitted from the golden authority into
/// generated records consumed by ResourceCycle, pre/post-RA hazard
/// recognizers, exact commit, and the inverse verifier.
///
/// Remains false while only aggregate pooled ceilings (above) and common
/// latency scaffolds are golden-admitted. Do not invent per-op numbers, peer
/// II/density thresholds, or competitive scheduling quality claims while this
/// is false. Flip to true only together with a generated import of the
/// complete admitted table and differential positive/negative tests.
inline constexpr bool haydnHasAdmittedPerOpResourceRecords() { return false; }

/// AIE HR memory-object wait-cycle reject (same-kind overlap) is not
/// admitted. Golden dual-load of one object (LOADSTORE0 + LOAD1, p[0]/p[1])
/// is legal. Enumerator bits stay dump-only; conflict() must not consult
/// them. Flip only with an admitted ISA row.
inline constexpr bool haydnMemoryObjectWaitCyclesAdmitted() { return false; }

/// SchedMachineModel CompleteModel polarity consumed by measurement and
/// release-visible pins. Mirrors HaydnSchedModel.CompleteModel: 0 while the
/// per-op table is not admitted; never invent a complete model early.
inline constexpr unsigned haydnSchedCompleteModelPin() {
  return haydnHasAdmittedPerOpResourceRecords() ? 1u : 0u;
}

/// Room for an OperandCycles-style vector on the reserved per-op seat.
/// Overlay of AIE InstrItinData OperandCycles
/// (llvm-aie llvm/include/llvm/Target/TargetItinerary.td:109-116) and
/// Hexagon LD/ST lists (HexagonScheduleV55.td:24-26). Count stays 0 until
/// a generated import fills it; not a filled latency table.
inline constexpr unsigned HAYDN_ADMITTED_OPERAND_CYCLE_ROOM = 8;

/// Format E unit bits for an admitted per-op UnitMask. Order matches
/// HaydnSchedule.td ProcessorItineraries FuncUnit list (bit 0 LOADSTORE0
/// ... bit 6 MAC1) and HaydnHazardRecognizer HaydnExecUnit. Overlay of AIE
/// InstrStage Units (TargetItinerary.td:56-62) / Hexagon SLOT* choice sets
/// (HexagonScheduleV55.td:24) onto Haydn's seven shared units — not
/// Hexagon packet-slot identity.
inline constexpr uint32_t HAYDN_ADMITTED_UNIT_LOADSTORE0 = 1u << 0;
inline constexpr uint32_t HAYDN_ADMITTED_UNIT_LOAD1 = 1u << 1;
inline constexpr uint32_t HAYDN_ADMITTED_UNIT_ALU0 = 1u << 2;
inline constexpr uint32_t HAYDN_ADMITTED_UNIT_ALU1 = 1u << 3;
inline constexpr uint32_t HAYDN_ADMITTED_UNIT_ALU2 = 1u << 4;
inline constexpr uint32_t HAYDN_ADMITTED_UNIT_MAC0 = 1u << 5;
inline constexpr uint32_t HAYDN_ADMITTED_UNIT_MAC1 = 1u << 6;
inline constexpr uint32_t HAYDN_ADMITTED_UNIT_MASK_ALL =
    HAYDN_ADMITTED_UNIT_LOADSTORE0 | HAYDN_ADMITTED_UNIT_LOAD1 |
    HAYDN_ADMITTED_UNIT_ALU0 | HAYDN_ADMITTED_UNIT_ALU1 |
    HAYDN_ADMITTED_UNIT_ALU2 | HAYDN_ADMITTED_UNIT_MAC0 |
    HAYDN_ADMITTED_UNIT_MAC1;

/// Structural seat for one admitted per-operation resource record once golden
/// publishes the complete table. Fields stay reserved (defaults 0 / empty)
/// until the generated import lands; aggregate PortModel ceilings remain the
/// only product-safe demand model while haydnHasAdmittedPerOpResourceRecords
/// is false. Zero-init is an empty seat, not a competitive scheduling claim.
struct HaydnAdmittedPerOpResourceRecord {
  unsigned Opcode = 0;
  /// Pooled bank port demand (Haydn GPR/DR/AR/SFR overlay of AIE itinerary
  /// operand traffic; not exclusive named-port FuncUnits).
  unsigned GPRReadPorts = 0;
  unsigned GPRWritePorts = 0;
  unsigned DRReadPorts = 0;
  unsigned DRWritePorts = 0;
  unsigned ARReadPorts = 0;
  unsigned ARWritePorts = 0;
  unsigned SFRReadPorts = 0;
  unsigned SFRWritePorts = 0;
  /// InstrStage Units bitmask over the seven execution units.
  uint32_t UnitMask = 0;
  /// Scalar max Data_Latency (AIE dest OperandCycle / Hexagon LD first
  /// OperandCycles entry). Not a filled special-op table.
  unsigned DataLatency = 0;
  /// Occupied OperandCycles entries in OperandCycles[0, Count).
  unsigned OperandCycleCount = 0;
  unsigned OperandCycles[HAYDN_ADMITTED_OPERAND_CYCLE_ROOM] = {};
  /// InstrStage Cycles occupancy (TargetItinerary.td:59; AIE SimpleCycle /
  /// Hexagon InstrStage<1, ...>). Reserved 0 — not an invented 1-cycle claim.
  unsigned PipelineOccupancy = 0;
  /// Required memory alignment in bytes. Reserved 0 until import.
  unsigned RequiredAlignment = 0;
};

/// Fail-closed import lookup. Always returns nullptr until admission is true
/// and a generated table is wired.
inline const HaydnAdmittedPerOpResourceRecord *
haydnLookupAdmittedPerOpResourceRecord(unsigned /*Opcode*/) {
  if (!haydnHasAdmittedPerOpResourceRecords())
    return nullptr;
  return nullptr;
}

inline bool haydnCompetitivePerOpResourceClaimsAllowed(unsigned Opcode) {
  return haydnLookupAdmittedPerOpResourceRecord(Opcode) != nullptr;
}

/// Availability of resource data for one opcode under the current golden.
/// Aggregate ceilings are always product-safe for structural packing legality.
/// Per-op competitive claims stay unavailable until a generated import lands.
enum class HaydnResourceRecordAvailability : uint8_t {
  /// No competitive per-op record; only pooled ceilings apply.
  AggregateCeilingsOnly = 0,
  /// Full per-op port/latency/pipeline/alignment record is admitted.
  PerOpAdmitted = 1,
};

/// Issue-cycle capacity constants that match HaydnSchedModel / itineraries.
/// These are aggregate structural facts, not competitive per-op invent.
// Issue width is Haydn::ISSUE_SLOT_COUNT (HaydnBaseInfo.h) — the single
// truth shared with the solver / materialize / verify / ResourceCycle
// surfaces. The local HAYDN_ISSUE_WIDTH mirror is deleted (SF1 spec
// closure, gap 4): one constant, MCTargetDesc-level.
inline constexpr unsigned HAYDN_NUM_EXEC_UNITS = 7;
inline constexpr unsigned HAYDN_LOAD_LATENCY_SCAFFOLD = 2;
static_assert(HAYDN_NUM_EXEC_UNITS == 7,
              "seven Format E units in the admitted UnitMask");
static_assert(HAYDN_ADMITTED_UNIT_MASK_ALL ==
                  ((1u << HAYDN_NUM_EXEC_UNITS) - 1u),
              "UnitMask bits cover exactly the seven execution units");

/// Current golden aggregate resource surface (pooled ceilings + model pins).
/// One shared authority for every ordinary-scheduler consumer; not a local
/// post-RA / callback mirror.
struct HaydnGoldenAggregateResourceSurface {
  unsigned GPRReadPorts = HAYDN_GPR_READ_PORTS;
  unsigned GPRWritePorts = HAYDN_GPR_WRITE_PORTS;
  unsigned DRReadPorts = HAYDN_DR_READ_PORTS;
  unsigned DRWritePorts = HAYDN_DR_WRITE_PORTS;
  unsigned ARReadPorts = HAYDN_AR_READ_PORTS;
  unsigned ARWritePorts = HAYDN_AR_WRITE_PORTS;
  unsigned SFRReadPorts = HAYDN_SFR_READ_PORTS;
  unsigned SFRWritePorts = HAYDN_SFR_WRITE_PORTS;
  unsigned IssueWidth = Haydn::ISSUE_SLOT_COUNT;
  unsigned NumExecUnits = HAYDN_NUM_EXEC_UNITS;
  unsigned LoadLatencyScaffold = HAYDN_LOAD_LATENCY_SCAFFOLD;
  bool PerOpRecordsAdmitted = false;
  unsigned CompleteModel = 0;
};

inline HaydnGoldenAggregateResourceSurface
haydnCurrentGoldenAggregateResourceSurface() {
  HaydnGoldenAggregateResourceSurface S;
  S.PerOpRecordsAdmitted = haydnHasAdmittedPerOpResourceRecords();
  S.CompleteModel = haydnSchedCompleteModelPin();
  return S;
}

/// One solo-class matrix for same-cycle issue-alone / e0-alone gates.
/// Four named classes, one classify site — do not add a fifth boolean next
/// to these. Hexagon packet solo bits overlay; AIE has no solo enum
/// (AIEBaseSubtarget.cpp itinerary-only). Golden Constraints §Special /
/// HI12-LO20 FieldLsb / CSR 0x20-0x25.
enum class HaydnSoloIssueClass : uint8_t {
  None = 0,
  SinCosArctan,  // ARCTAN / SIN_COS — alone in the issue cycle
  LuiAddiE0,     // LUI / ADDI32_W — e0-alone (HI12/LO20 FieldLsb)
  CsrwSetHwloop, // CSRW 0x20-0x25 must not share a cycle with SET_HWLOOP
};

/// Logical-opcode classify. Format E members peel at the HR / Materialize
/// sites; this list stays the logical identity only.
inline HaydnSoloIssueClass haydnClassifySoloIssueOpcode(unsigned Opcode) {
  const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(Opcode);
  if (Log == Haydn::ARCTAN || Log == Haydn::SIN_COS)
    return HaydnSoloIssueClass::SinCosArctan;
  if (Log == Haydn::LUI || Log == Haydn::ADDI32_W)
    return HaydnSoloIssueClass::LuiAddiE0;
  switch (Log) {
  case Haydn::SET_HWLOOP:
  case Haydn::SET_HWLOOP_REG:
  case Haydn::SET_HWLOOP_W:
  case Haydn::SET_HWLOOP_F2_W:
  case Haydn::SET_HWLOOP_REG_W:
  case Haydn::LoopStart:
    return HaydnSoloIssueClass::CsrwSetHwloop;
  default:
    return HaydnSoloIssueClass::None;
  }
}

/// LUI / ADDI32_W e0-alone (HI12/LO20 FieldLsb). Same peel as HR
/// isAbsMaterializeOp.
inline bool haydnIsAbsMaterializeOpcode(unsigned Opcode) {
  return haydnClassifySoloIssueOpcode(Opcode) == HaydnSoloIssueClass::LuiAddiE0;
}

/// CSRW targeting HWLR 0x20-0x25. -1 if not that writer. Shared by HR and
/// ResourceCycle so the race with SET_HWLOOP is one predicate.
inline int haydnHwloopCsrAddr(const MachineInstr &MI) {
  const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
  if (Log != Haydn::CSRW && Log != Haydn::CSRW_W)
    return -1;
  if (MI.getNumOperands() == 0 || !MI.getOperand(0).isImm())
    return -1;
  const int64_t Addr = MI.getOperand(0).getImm();
  if (Addr >= 0x20 && Addr <= 0x25)
    return static_cast<int>(Addr);
  return -1;
}

/// Class-1 issue-alone capacity (PackLegality rule 4 / HR peer). Not a
/// multi-cycle invent and not a competitive latency claim.
/// Single opcode list: ResourceCycle isHaydnSMSAloneOpcode and HR
/// opcodeIssuesAloneInCycle call this. Do not re-list ARCTAN/SIN_COS
/// elsewhere. Member / _S* identity is the HR predicate, not this list.
inline bool haydnOpcodeIssuesAloneInCycle(unsigned Opcode) {
  return haydnClassifySoloIssueOpcode(Opcode) ==
         HaydnSoloIssueClass::SinCosArctan;
}

/// Constraints §Special SIN_COS/ARCTAN window. Occupancy is uimm4+2
/// (2..17). The issue cycle is alone; following occupancy-1 cycles are
/// NOP-on-unit (Reserved) with no dest writer until the dest commits.
/// Conservative dest OperandCycles 17 is the published max, not a
/// competitive per-op invent. AIE has no solo window enum
/// (AIEBaseSubtarget.cpp itinerary-only); Hexagon packet solo bits
/// overlay onto this occupancy math.
inline constexpr unsigned HAYDN_SINCOS_UIMM4_BITS = 4;
inline constexpr unsigned HAYDN_SINCOS_UIMM4_MAX = 15;
inline constexpr unsigned HAYDN_SINCOS_OCCUPANCY_BIAS = 2;
inline constexpr unsigned HAYDN_SINCOS_OCCUPANCY_MIN = 2;
inline constexpr unsigned HAYDN_SINCOS_OCCUPANCY_MAX =
    HAYDN_SINCOS_UIMM4_MAX + HAYDN_SINCOS_OCCUPANCY_BIAS;

static_assert(HAYDN_SINCOS_OCCUPANCY_MAX == 17,
              "SIN_COS/ARCTAN occupancy max is uimm4_max+2");
static_assert(HAYDN_SINCOS_OCCUPANCY_MIN == HAYDN_SINCOS_OCCUPANCY_BIAS,
              "minimum occupancy is uimm4=0 plus bias 2");

/// Golden occupancy from the uimm4 operand. Missing or out-of-range imm
/// fail-closes to the published max so an under-booked window cannot
/// encode.
inline unsigned haydnSinCosWindowOccupancyFromImm(int64_t Imm) {
  if (Imm < 0 || Imm > static_cast<int64_t>(HAYDN_SINCOS_UIMM4_MAX))
    return HAYDN_SINCOS_OCCUPANCY_MAX;
  return static_cast<unsigned>(Imm) + HAYDN_SINCOS_OCCUPANCY_BIAS;
}

/// Occupancy for a logical SIN_COS/ARCTAN. Returns 0 for every other
/// opcode. Format E members peel at the HR / Materialize sites.
inline unsigned haydnSinCosWindowOccupancy(const MachineInstr &MI) {
  if (!haydnOpcodeIssuesAloneInCycle(MI.getOpcode()))
    return 0;
  int64_t Imm = -1;
  for (unsigned I = MI.getNumOperands(); I > 0; --I) {
    const MachineOperand &MO = MI.getOperand(I - 1);
    if (!MO.isImm())
      continue;
    Imm = MO.getImm();
    break;
  }
  return haydnSinCosWindowOccupancyFromImm(Imm);
}

/// Single named window law. Issue-alone + NOP-on-unit + no dest-writer
/// until dest commit. Occupancy length is uimm4+2, not a second table.
struct HaydnSinCosArctanWindowLaw {
  bool IssuesAloneInCycle = true;
  bool NopOnSelectedUnit = true;
  bool NoDestWriterUntilCommit = true;
  unsigned OccupancyMin = HAYDN_SINCOS_OCCUPANCY_MIN;
  unsigned OccupancyMax = HAYDN_SINCOS_OCCUPANCY_MAX;
};

inline HaydnSinCosArctanWindowLaw haydnSinCosArctanWindowLaw() {
  return HaydnSinCosArctanWindowLaw{};
}

/// Tag for the three named same-cycle laws (SIN_COS/ARCTAN alone,
/// CSRW↔SET_HWLOOP, LUI/ADDI32_W e0-alone). Shared by HR, ResourceCycle,
/// and the multi-stage remark so the name is one string.
inline constexpr const char *HAYDN_NAMED_SAME_CYCLE_LAWS_TAG =
    "arctan-sincos+csrw-set+abs-e0";

/// Availability-aware resource record generated from the current golden
/// surface. Shared by pre-RA, ordinary post-RA, hazard recognizer, exact
/// commit, verification, and measurement. Carries the admitted aggregate
/// ceilings on every opcode so consumers never re-declare local budgets.
/// Not a competitive invent: when the per-op table is closed, Availability is
/// AggregateCeilingsOnly and CompetitiveClaimsAllowed is false.
struct HaydnAvailabilityAwareResourceRecord {
  unsigned Opcode = 0;
  HaydnResourceRecordAvailability Availability =
      HaydnResourceRecordAvailability::AggregateCeilingsOnly;
  bool CompetitiveClaimsAllowed = false;
  /// Aggregate surface binding (always filled from current golden ceilings).
  HaydnGoldenAggregateResourceSurface Aggregate;
  /// True for ARCTAN/SIN_COS class-1 alone-in-cycle capacity.
  bool IssuesAloneInCycle = false;
};

/// Build the availability-aware record for \p Opcode from the single PortModel
/// authority. Missing per-op records make only competitive claims for that
/// opcode unavailable; aggregate packing ceilings remain enforceable.
inline HaydnAvailabilityAwareResourceRecord
haydnMakeAvailabilityAwareResourceRecord(unsigned Opcode) {
  HaydnAvailabilityAwareResourceRecord R;
  R.Opcode = Opcode;
  R.Aggregate = haydnCurrentGoldenAggregateResourceSurface();
  R.IssuesAloneInCycle = haydnOpcodeIssuesAloneInCycle(Opcode);
  if (haydnLookupAdmittedPerOpResourceRecord(Opcode) != nullptr) {
    R.Availability = HaydnResourceRecordAvailability::PerOpAdmitted;
    R.CompetitiveClaimsAllowed = true;
  } else {
    R.Availability = HaydnResourceRecordAvailability::AggregateCeilingsOnly;
    R.CompetitiveClaimsAllowed = false;
  }
  return R;
}

/// True when \p Rec binds the product aggregate surface and respects the
/// current admission polarity (no competitive invent while closed).
inline bool haydnAvailabilityAwareRecordBindsAggregateSurface(
    const HaydnAvailabilityAwareResourceRecord &Rec) {
  const HaydnGoldenAggregateResourceSurface Surf =
      haydnCurrentGoldenAggregateResourceSurface();
  if (Rec.Aggregate.GPRReadPorts != Surf.GPRReadPorts ||
      Rec.Aggregate.GPRWritePorts != Surf.GPRWritePorts ||
      Rec.Aggregate.DRReadPorts != Surf.DRReadPorts ||
      Rec.Aggregate.DRWritePorts != Surf.DRWritePorts ||
      Rec.Aggregate.ARReadPorts != Surf.ARReadPorts ||
      Rec.Aggregate.ARWritePorts != Surf.ARWritePorts ||
      Rec.Aggregate.SFRReadPorts != Surf.SFRReadPorts ||
      Rec.Aggregate.SFRWritePorts != Surf.SFRWritePorts ||
      Rec.Aggregate.IssueWidth != Surf.IssueWidth ||
      Rec.Aggregate.NumExecUnits != Surf.NumExecUnits ||
      Rec.Aggregate.LoadLatencyScaffold != Surf.LoadLatencyScaffold)
    return false;
  if (Rec.IssuesAloneInCycle != haydnOpcodeIssuesAloneInCycle(Rec.Opcode))
    return false;
  if (haydnHasAdmittedPerOpResourceRecords())
    return false;
  if (Rec.CompetitiveClaimsAllowed ||
      Rec.Availability == HaydnResourceRecordAvailability::PerOpAdmitted)
    return false;
  if (Rec.Aggregate.PerOpRecordsAdmitted || Rec.Aggregate.CompleteModel != 0u)
    return false;
  return true;
}

/// Closed-admission pin with explicit Admitted / TableHead so a unit test can
/// prove that flipping admission without a generated table fails. Product
/// callers keep using haydnProductResourceAdmissionPinsHold(), which forwards
/// the live constexpr and lookup. When Admitted is true and TableHead is
/// null, this returns false.
inline bool haydnProductResourceAdmissionPinsHoldAssuming(
    bool Admitted, const HaydnAdmittedPerOpResourceRecord *TableHead) {
  if (Admitted && TableHead == nullptr)
    return false;
  if (Admitted)
    return false;
  if (TableHead != nullptr)
    return false;
  if (haydnCompetitivePerOpResourceClaimsAllowed(/*Opcode=*/0))
    return false;
  if (haydnSchedCompleteModelPin() != 0u)
    return false;
  const HaydnGoldenAggregateResourceSurface Surf =
      haydnCurrentGoldenAggregateResourceSurface();
  if (Surf.PerOpRecordsAdmitted || Surf.CompleteModel != 0u)
    return false;
  if (haydnMemoryObjectWaitCyclesAdmitted())
    return false;
  if (Surf.GPRWritePorts != HAYDN_GPR_WRITE_PORTS ||
      Surf.IssueWidth != Haydn::ISSUE_SLOT_COUNT ||
      Surf.NumExecUnits != HAYDN_NUM_EXEC_UNITS)
    return false;
  const HaydnAvailabilityAwareResourceRecord Rec0 =
      haydnMakeAvailabilityAwareResourceRecord(/*Opcode=*/0);
  if (!haydnAvailabilityAwareRecordBindsAggregateSurface(Rec0))
    return false;
  const HaydnAvailabilityAwareResourceRecord RecAdd =
      haydnMakeAvailabilityAwareResourceRecord(Haydn::ADD32);
  if (!haydnAvailabilityAwareRecordBindsAggregateSurface(RecAdd) ||
      RecAdd.IssuesAloneInCycle)
    return false;
  const HaydnAvailabilityAwareResourceRecord RecAlone =
      haydnMakeAvailabilityAwareResourceRecord(Haydn::ARCTAN);
  if (!haydnAvailabilityAwareRecordBindsAggregateSurface(RecAlone) ||
      !RecAlone.IssuesAloneInCycle)
    return false;
  return true;
}

/// Release-visible fail-closed pin: true while per-op admission stays closed
/// and competitive per-op claims remain disallowed. Flip only with generated
/// import + differential tests when golden admits the complete table.
/// Shared by pre-RA leaveRegion, post-RA enterMBB, hazard recognizer,
/// ResourceCycle certificate, and measurement polarity — one gate, no local
/// mirrors.
inline bool haydnProductResourceAdmissionPinsHold() {
  return haydnProductResourceAdmissionPinsHoldAssuming(
      haydnHasAdmittedPerOpResourceRecords(),
      haydnLookupAdmittedPerOpResourceRecord(/*Opcode=*/0));
}

/// Structural polarity for measurement / release-visible seats: competitive
/// II/density claims remain closed while the availability-aware record is not
/// PerOpAdmitted for the complete table.
inline bool haydnCompetitiveIIDensityClaimsAllowed() {
  return haydnHasAdmittedPerOpResourceRecords() &&
         !haydnProductResourceAdmissionPinsHold();
}

/// One consume pin for pre-RA, ordinary post-RA, SMS/shouldUseSchedule,
/// hazard-recognizer Reset, exact commit, and late latency verify. True only
/// while the shared availability-aware record binds the current aggregate
/// surface and the complete per-op table stays closed.
inline bool haydnAvailabilityAwareConsumePinsHold(
    unsigned Opcode = Haydn::ADD32) {
  const HaydnAvailabilityAwareResourceRecord Rec =
      haydnMakeAvailabilityAwareResourceRecord(Opcode);
  if (!haydnProductResourceAdmissionPinsHold())
    return false;
  if (!haydnAvailabilityAwareRecordBindsAggregateSurface(Rec))
    return false;
  if (haydnHasAdmittedPerOpResourceRecords() ||
      haydnSchedCompleteModelPin() != 0u ||
      haydnCompetitiveIIDensityClaimsAllowed())
    return false;
  if (Rec.CompetitiveClaimsAllowed ||
      Rec.Availability == HaydnResourceRecordAvailability::PerOpAdmitted)
    return false;
  if (Rec.Aggregate.PerOpRecordsAdmitted || Rec.Aggregate.CompleteModel != 0u)
    return false;
  if (Rec.Aggregate.IssueWidth != Haydn::ISSUE_SLOT_COUNT ||
      Rec.Aggregate.NumExecUnits != HAYDN_NUM_EXEC_UNITS ||
      Rec.Aggregate.LoadLatencyScaffold != HAYDN_LOAD_LATENCY_SCAFFOLD ||
      Rec.Aggregate.GPRWritePorts != HAYDN_GPR_WRITE_PORTS)
    return false;
  return true;
}

/// Ceiling division (N/D), 0 when N==0. D must be > 0.
inline unsigned haydnCeilDivPorts(unsigned N, unsigned D) {
  assert(D > 0 && "port budget divisor");
  if (N == 0)
    return 0;
  return (N + D - 1) / D;
}

/// Pure port-pressure lower bound on issue cycles for a loop body (or any
/// multiset of operand traffic). Max of ceil(total_bank_demand / budget) over
/// GPR/DR/AR/SFR read and write pools. Zero demand → 0. Nonzero demand → ≥ 1.
///
/// This is *not* format packing: unit geometry may place three ops in one
/// cycle, but three independent GPR writes still need ≥2 cycles under 2W.
/// Pre-RA HR and SMS MI ResMII must both respect this floor (same PortModel).
inline unsigned haydnPortLowerBoundResMII(unsigned GPRReads, unsigned GPRWrites,
                                         unsigned DRReads = 0,
                                         unsigned DRWrites = 0,
                                         unsigned ARReads = 0,
                                         unsigned ARWrites = 0,
                                         unsigned SFRReads = 0,
                                         unsigned SFRWrites = 0) {
  if (GPRReads == 0 && GPRWrites == 0 && DRReads == 0 && DRWrites == 0 &&
      ARReads == 0 && ARWrites == 0 && SFRReads == 0 && SFRWrites == 0)
    return 0;
  unsigned Cycles = 1;
  Cycles = std::max(Cycles, haydnCeilDivPorts(GPRReads, HAYDN_GPR_READ_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(GPRWrites, HAYDN_GPR_WRITE_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(DRReads, HAYDN_DR_READ_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(DRWrites, HAYDN_DR_WRITE_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(ARReads, HAYDN_AR_READ_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(ARWrites, HAYDN_AR_WRITE_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(SFRReads, HAYDN_SFR_READ_PORTS));
  Cycles = std::max(Cycles, haydnCeilDivPorts(SFRWrites, HAYDN_SFR_WRITE_PORTS));
  return Cycles;
}

/// True iff \p Reg is classified into port bank \p BankRC.
/// * Physical: BankRC.contains(Reg).
/// * Virtual: MRI regclass is BankRC or a subclass (e.g. GPR32Lo ⊂ GPR32).
/// * No MRI / no class: false (existential — do not invent demand).
inline bool isHaydnPortBankReg(Register Reg, const TargetRegisterClass &BankRC,
                               const MachineRegisterInfo *MRI) {
  if (!Reg)
    return false;
  if (Reg.isPhysical())
    return BankRC.contains(Reg);
  if (!MRI)
    return false;
  const TargetRegisterClass *RC = MRI->getRegClassOrNull(Reg);
  if (!RC)
    return false;
  // hasSubClassEq: RC is BankRC or a subclass (GPR32Lo / GPR32NoSPNoLR).
  return BankRC.hasSubClassEq(RC);
}

inline bool isHaydnGPRPortReg(Register Reg, const MachineRegisterInfo *MRI) {
  // DR64 is a separate file; never charge it as GPR even if misclassified.
  if (isHaydnPortBankReg(Reg, Haydn::DR64RegClass, MRI))
    return false;
  if (isHaydnPortBankReg(Reg, Haydn::ARRegClass, MRI))
    return false;
  return isHaydnPortBankReg(Reg, Haydn::GPR32RegClass, MRI);
}

inline bool isHaydnDRPortReg(Register Reg, const MachineRegisterInfo *MRI) {
  return isHaydnPortBankReg(Reg, Haydn::DR64RegClass, MRI);
}

inline bool isHaydnARPortReg(Register Reg, const MachineRegisterInfo *MRI) {
  return isHaydnPortBankReg(Reg, Haydn::ARRegClass, MRI);
}

inline bool isHaydnSFRPortReg(Register Reg) {
  return Reg.isPhysical() && Reg == Haydn::SFR;
}

/// AIE AIE2PSRegisterInfo.cpp:775-778 isSimplifiableReservedReg overlay.
/// Only reserved status/control regs (SFR + CBR banks). Soft-zero R0, SP,
/// and LR stay data-path reserved and keep dest-read / Output edges.
inline bool haydnIsSimplifiableReservedReg(Register PhysReg) {
  return PhysReg.isPhysical() &&
         (PhysReg == Haydn::SFR || PhysReg == Haydn::CBR0 ||
          PhysReg == Haydn::CBR1);
}

inline const MachineRegisterInfo *
haydnPortMRI(const MachineInstr &MI, const MachineRegisterInfo *MRI) {
  if (MRI)
    return MRI;
  if (const MachineFunction *MF = MI.getMF())
    return &MF->getRegInfo();
  return nullptr;
}

// Count GPR32 read and write port usage for an instruction.
// DR64 accesses use a separate register file with own ports — not counted.
// Per-instruction accounting rules:
// 1. **Reads and writes are counted independently.** A single operand that is
// both a use and a def (read-write, e.g. ADD32 rd, rd, rs) consumes one
// read port AND one write port — not one or the other. The previous
// `else if` form undercounted these by treating them as reads-only.
// 2. **All physical writes count, including dead defs.** A dead def still
// occupies a write port for the cycle (the register file port is reserved
// before liveness is considered). The previous `!MO.isDead` filter
// undercounted writes.
// 2b. **Undef explicit uses still charge a read port.** LLVM may mark a
// tied dest-read (MOVT32/MOVF32 `$rd_src`, MAC acc) `<undef>` when the
// fallthrough is poison. The encoding still samples that field: golden
// MOVT32 `GPR_Read_Port` is rt, rs1, rs2. Skipping undef uses under-counts
// 3R as 2R so ADD32 (2R) + MOVT32 looks like 4R and coissues; the catalog
// charges 3R and BundleSim rejects the pack. RAW/WAW still skip undef
// (no incoming value). Peer: AIE AIEHazardRecognizer.cpp books itinerary
// resources with no undef-use skip; Hexagon packet walks are per operand.
// 3. **Each explicit operand field reserves one port.** MOVE32 is modeled
// with two source operands (`$rs1`, `$rs2`) for the R-type encoding and
// `copyPhysReg` emits `MOVE32 rd, rs, rs`. Golden Constraints count
// physical-port arbitration, not value identity: `rs1==rs2` does not
// prove the two fields share a port. Charge 2R1W so the MI path matches
// the descriptor shape. A 1-read exception needs a golden fact that is
// not published. Peer: AIEHazardRecognizer.cpp books itinerary resources
// with no operand-identity dedup; Hexagon packet port walks are per
// operand. Under-count can accept a bundle hardware rejects (2×MOVE32 +
// one extra GPR read looks like 3R after dedup, 5R per-field).
// 4. **No R0 exemption.** R0 is soft-zero, not hardwired (HaydnRegisterInfo):
// silicon does not force R0==0 and does not discard R0 traffic. A MatInt
// ADDI rd,R0,imm still reads the R0 file port; XOR32 R0,R0,R0 restore and
// R0-borrow loads occupy real write ports. Dual R0 defs in one bundle are
// WRITE_CONFLICT (HaydnHazardRecognizer WAW). Counting R0 is required for
// correct 4R2W budgeting — the old "R0 is free" assumption was wrong.
// 5. **Vregs:** classified via MRI regclass → GPR bank (see
// isHaydnGPRPortReg). Same 4R2W budget as physregs; no undercount.
// \p MRI optional; defaults to MI's MachineFunction MRI when present.
// \returns {Reads, Writes} GPR32 port demand for the instruction.
inline std::pair<unsigned, unsigned>
countGPRPorts(const MachineInstr &MI,
              const MachineRegisterInfo *MRI = nullptr) {
  MRI = haydnPortMRI(MI, MRI);
  unsigned Reads = 0, Writes = 0;
  // Per-field: every explicit GPR use/def operand reserves one port.
  // Tied use/def of one register still charge independently (one R + one W).
  // Logical MOVE32 rd, rs, rs is 2R1W (two use fields). Generated Format E
  // members expose one source field and therefore charge 1R — that is the
  // member encoding, not a return of identity dedup.
  //
  // Skip implicit operands: call ABI clobbers (JAL_W/JALR_W regmask +
  // implicit-def of every CSR) are not same-cycle RF port traffic. Counting
  // them as writes made a lone JAL_W exceed 2W and assert in
  // ResourceManager::calculateResMIIDFA (pr28982a/b SMS ResMII). Hardware
  // ports only see explicit data-path operands; tied use/def are explicit.
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || MO.isImplicit())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    if (!isHaydnGPRPortReg(Reg, MRI))
      continue;

    // Undef uses still occupy the encoded read port (MOVT32 dest-read).
    const bool IsUse = MO.isUse();
    const bool IsDef = MO.isDef();
    if (IsUse)
      ++Reads;
    if (IsDef)
      ++Writes;
  }
  return {Reads, Writes};
}

// Count DR64 read and write port usage for an instruction. DR64 is a separate
// register file from GPR32 with its own 8R3W port budget (see file header).
// Accounting mirrors countGPRPorts (read/write counted independently, dead
// defs still occupy a write port, each explicit operand field one port)
// with two differences:
// * Only DR64 registers are counted (GPR32 — including soft-zero R0 — is
// handled by countGPRPorts; neither file exempts a "zero" reg from ports).
// * D0 is a normal allocatable register; materialize zero with xor64 d,d,d.
// Every DR64 operand consumes a real port — nothing to skip.
// Tied accumulator operands (FmtALU64Acc `$rd = $rd_in`, e.g. MULA64) appear
// as one def + one use of the same physical register; with the read/write
// independent rule this correctly charges one read port (the accumulator
// input) and one write port (the result) — matching how the hardware reads
// the accumulator and writes the new value.
// Vregs classified via MRI regclass → DR64 bank.
// \returns {Reads, Writes} DR64 port demand for the instruction.
inline std::pair<unsigned, unsigned>
countDRPorts(const MachineInstr &MI,
             const MachineRegisterInfo *MRI = nullptr) {
  MRI = haydnPortMRI(MI, MRI);
  unsigned Reads = 0, Writes = 0;
  for (const MachineOperand &MO : MI.operands()) {
    // Same as countGPRPorts: skip ABI/implicit clobbers (not RF port traffic).
    if (!MO.isReg() || MO.isImplicit())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    if (!isHaydnDRPortReg(Reg, MRI))
      continue;

    const bool IsUse = MO.isUse();
    const bool IsDef = MO.isDef();
    if (IsUse)
      ++Reads;
    if (IsDef)
      ++Writes;
  }
  return {Reads, Writes};
}

// Count AR (aligned-register) read and write port usage for an instruction.
// Spec budget: 2R2W, shared across units. Accounting mirrors countDRPorts
// (read/write counted independently, dead defs still occupy a write port,
// each explicit operand field one port). Vregs via MRI → AR bank.
// \returns {Reads, Writes} AR port demand for the instruction.
inline std::pair<unsigned, unsigned>
countARPorts(const MachineInstr &MI,
             const MachineRegisterInfo *MRI = nullptr) {
  MRI = haydnPortMRI(MI, MRI);
  unsigned Reads = 0, Writes = 0;
  for (const MachineOperand &MO : MI.operands()) {
    // Same as countGPRPorts: skip ABI/implicit clobbers (not RF port traffic).
    if (!MO.isReg() || MO.isImplicit())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    if (!isHaydnARPortReg(Reg, MRI))
      continue;

    const bool IsUse = MO.isUse();
    const bool IsDef = MO.isDef();
    if (IsUse)
      ++Reads;
    if (IsDef)
      ++Writes;
  }
  return {Reads, Writes};
}

// True when the descriptor names SFR as an implicit def or use.
// Peer: AIE AIEPseudoBranchExpansion.cpp:85
// (hasImplicitDefOfPhysReg / hasImplicitUseOfPhysReg). SET_HWLOOP / CSR /
// flag-setters list SFR on the desc and charge.
inline bool haydnDescNamesSfrPort(const MachineInstr &MI) {
  const MCInstrDesc &D = MI.getDesc();
  return D.hasImplicitDefOfPhysReg(Haydn::SFR) ||
         D.hasImplicitUseOfPhysReg(Haydn::SFR);
}

// True when \p Opcode is a generated private Format E member (a committed
// row×entry×unit×type stamp, per D493). Members appear in MIR only after the
// exact post-RA commit; their descriptors are generated geometry (fields,
// itinerary, Constraints) and may drop the SFR Uses/Defs naming their
// authored logical shells carry (X2MOVT32_E3_*_R has no Uses=[SFR] while
// X2MOVT32 does). One classification site — the inverse-table map lookup
// (haydn::bundle::lookupPrivateFormatEMember, out-of-line in
// HaydnBundleVerify.cpp); never a name peel or a second opcode set.
bool haydnIsPrivateFormatEMemberOpcode(unsigned Opcode);

// Count SFR read and write port usage for an instruction.
// Spec budget is 2R1W (golden VLIW_Engine_Compiler_Constraints.md
// §Registers: "Only one instruction per bundle is allowed to write to an
// SFR"; total read ports SFR : 2).
//
// CB-161 law (2026-08-21): SFR port demand is an OPERAND fact, charged for
// (a) descriptor-named SFR traffic (SET_HWLOOP / CSR / flag-setters —
// logical shells) and (b) private Format E members, whose anonymous
// implicit(-def) $sfr operands survive setAlternateDescriptor onto
// geometry-only member descs that dropped the logical's Uses/Defs=[SFR]
// naming. Skipping class (b) left the SFR 2R read ceiling unenforced for
// every committed member (MOVESFR2GPR_E3_*_SFR / X2MOVT32_E3_*_R carry
// implicit $sfr reads their descs never name).
//
// Ordinary (non-member) MIs with a silent desc keep the historical skip:
// current codegen no longer adds leftover implicit-def $sfr to ordinary ALU
// (verified: committed ADD32_E3_* members carry no $sfr operand), so no
// legal pack relies on it; if such an operand reappears it is unattributed
// traffic and stays a WAW-law question, not this ceiling.
//
// HR, commit, and verify all call this — do not fork a second SFR count.
// \returns {Reads, Writes}.
inline std::pair<unsigned, unsigned>
countSFRPorts(const MachineInstr &MI,
              const MachineRegisterInfo *MRI = nullptr) {
  (void)MRI;
  unsigned Reads = 0, Writes = 0;
  const bool ChargeImplicits =
      haydnDescNamesSfrPort(MI) ||
      haydnIsPrivateFormatEMemberOpcode(MI.getOpcode());
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    if (MO.isImplicit() && !ChargeImplicits)
      continue;
    Register Reg = MO.getReg();
    if (!isHaydnSFRPortReg(Reg))
      continue;
    const bool IsUse = MO.isUse();
    const bool IsDef = MO.isDef();
    if (IsUse)
      ++Reads;
    if (IsDef)
      ++Writes;
  }
  return {Reads, Writes};
}

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H
