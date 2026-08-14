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
// * GPR32 — 4 read / 2 write (compiler view; 5R3W physical with AGU bypass).
// * DR64 — 7 read / 3 write.
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
// pooled demand as equivalent physregs. Repeated operands and tied use/defs
// dedupe by Register identity under the same architectural rule. Unknown
// regs (no class yet) stay existential / uncounted.
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
// shared by ResourceCycle and post-RA / pre-RA HR. MI-versus-descriptor
// *port* demand (MOVE32 repeated-src 1R1W vs desc 2R1W) must not be read as a
// format accept/reject gap — format polarity agrees for equal opcodes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H

#include "HaydnRegisterInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCRegister.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

namespace llvm {

// GPR port budget constants (CLAUDE.md "Slot architecture"; compiler view
// 5R3W physical incl. AGU bypass).
inline constexpr unsigned HAYDN_GPR_READ_PORTS = 4;
inline constexpr unsigned HAYDN_GPR_WRITE_PORTS = 2;

// DR64 port budget constants — spec hard cap, enforced as an RTL SVA
// assertion (formal_verification_decoder_sva.json a_dr_read_budget<=7
// a_dr_write_budget<=3; port_budget_analysis.py:119-122). Shared across all
// three slots. The 3W ceiling is the binding constraint once fused-MAC
// dual-write DR ops arrive.
inline constexpr unsigned HAYDN_DR_READ_PORTS = 7;
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
inline constexpr unsigned HAYDN_ISSUE_WIDTH = 3;
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
  unsigned IssueWidth = HAYDN_ISSUE_WIDTH;
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

/// Class-1 issue-alone capacity (PackLegality rule 4 / HR peer). Not a
/// multi-cycle invent and not a competitive latency claim.
/// Single opcode list: ResourceCycle isHaydnSMSAloneOpcode and HR
/// opcodeIssuesAloneInCycle call this. Do not re-list ARCTAN/SIN_COS
/// elsewhere. Member / _S* identity is the HR predicate, not this list.
inline bool haydnOpcodeIssuesAloneInCycle(unsigned Opcode) {
  return Opcode == Haydn::ARCTAN || Opcode == Haydn::SIN_COS;
}

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
  if (Surf.GPRWritePorts != HAYDN_GPR_WRITE_PORTS ||
      Surf.IssueWidth != HAYDN_ISSUE_WIDTH ||
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
  if (Rec.Aggregate.IssueWidth != HAYDN_ISSUE_WIDTH ||
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
// 3. **Duplicate source registers on one instruction count once.** MOVE32 is
// modeled in the .td with two source operands (`$rs1`, `$rs2`) for the
// R-type encoding, but `copyPhysReg` passes the same SrcReg twice and the
// hardware move reads a single register (1R/1W, RI-like).
// Without dedup the packetizer would reject a 2-issue packet of two moves
// as needing 4 read ports when only 2 are physically consumed. Dedup key is
// Register identity (works for both physregs and vregs).
//
// MI-versus-descriptor differential (MOVE32-class): this MI path is exact —
// `MOVE32 rd, rs, rs` → 1R1W after same-reg dedup. Descriptor-only estimates
// (SMS placement via MCInstrDesc, no operand identity) always see the 1-def +
// 2-use shape → 2R1W and overcount. Pre-RA HR / list-sched use only this MI
// PortModel path (CreateTargetMIHazardRecognizer IsPreRA). The overcount is
// intentional conservative placement on the SMS MID path, not a PortModel bug
// and not an operand-dependent format predicate.
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
  // Track registers seen as reads/writes on this instruction so duplicate
  // operands (e.g. MOVE32 rd, rs, rs) count once per port type. Key is the
  // Register unit (phys or virt) — tied use/def of the same reg still charge
  // independently because they go to separate Seen sets.
  //
  // Skip implicit operands: call ABI clobbers (JAL_W/JALR_W regmask +
  // implicit-def of every CSR) are not same-cycle RF port traffic. Counting
  // them as writes made a lone JAL_W exceed 2W and assert in
  // ResourceManager::calculateResMIIDFA (pr28982a/b SMS ResMII). Hardware
  // ports only see explicit data-path operands; tied use/def are explicit.
  SmallSet<unsigned, 6> SeenReads;
  SmallSet<unsigned, 6> SeenWrites;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || MO.isImplicit())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    if (!isHaydnGPRPortReg(Reg, MRI))
      continue;

    const bool IsUse = MO.isUse() && !MO.isUndef();
    const bool IsDef = MO.isDef();
    // Count reads and writes independently — a read-write operand consumes
    // one port of each type (not one or the other). SmallSet::insert returns
    // pair<iterator, bool>; the bool is true iff the element was newly added.
    if (IsUse && SeenReads.insert(Reg.id()).second)
      ++Reads;
    if (IsDef && SeenWrites.insert(Reg.id()).second)
      ++Writes;
  }
  return {Reads, Writes};
}

// Count DR64 read and write port usage for an instruction. DR64 is a separate
// register file from GPR32 with its own 7R3W port budget (see file header).
// Accounting mirrors countGPRPorts (read/write counted independently, dead
// defs still occupy a write port, duplicate sources count once per port
// type) with two differences:
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
  SmallSet<unsigned, 6> SeenReads;
  SmallSet<unsigned, 6> SeenWrites;
  for (const MachineOperand &MO : MI.operands()) {
    // Same as countGPRPorts: skip ABI/implicit clobbers (not RF port traffic).
    if (!MO.isReg() || MO.isImplicit())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    if (!isHaydnDRPortReg(Reg, MRI))
      continue;

    const bool IsUse = MO.isUse() && !MO.isUndef();
    const bool IsDef = MO.isDef();
    if (IsUse && SeenReads.insert(Reg.id()).second)
      ++Reads;
    if (IsDef && SeenWrites.insert(Reg.id()).second)
      ++Writes;
  }
  return {Reads, Writes};
}

// Count AR (aligned-register) read and write port usage for an instruction.
// Spec budget: 2R2W, shared across units. Accounting mirrors countDRPorts
// (read/write counted independently, dead defs still occupy a write port,
// duplicate sources count once per port type). Vregs via MRI → AR bank.
// \returns {Reads, Writes} AR port demand for the instruction.
inline std::pair<unsigned, unsigned>
countARPorts(const MachineInstr &MI,
             const MachineRegisterInfo *MRI = nullptr) {
  MRI = haydnPortMRI(MI, MRI);
  unsigned Reads = 0, Writes = 0;
  SmallSet<unsigned, 6> SeenReads;
  SmallSet<unsigned, 6> SeenWrites;
  for (const MachineOperand &MO : MI.operands()) {
    // Same as countGPRPorts: skip ABI/implicit clobbers (not RF port traffic).
    if (!MO.isReg() || MO.isImplicit())
      continue;
    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    if (!isHaydnARPortReg(Reg, MRI))
      continue;

    const bool IsUse = MO.isUse() && !MO.isUndef();
    const bool IsDef = MO.isDef();
    if (IsUse && SeenReads.insert(Reg.id()).second)
      ++Reads;
    if (IsDef && SeenWrites.insert(Reg.id()).second)
      ++Writes;
  }
  return {Reads, Writes};
}

// Count SFR read and write port usage for an instruction.
// Spec budget is 2R1W and product law is at most one SFR writer per cycle:
// every SFR def (dead or live) reserves the exclusive write port so dual
// dead implicit-def $sfr cannot co-issue (PackLegality rule 3).
// \returns {Reads, Writes}.
inline std::pair<unsigned, unsigned>
countSFRPorts(const MachineInstr &MI,
              const MachineRegisterInfo *MRI = nullptr) {
  (void)MRI;
  unsigned Reads = 0, Writes = 0;
  SmallSet<unsigned, 2> SeenReads;
  SmallSet<unsigned, 2> SeenWrites;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (!isHaydnSFRPortReg(Reg))
      continue;
    const bool IsUse = MO.isUse() && !MO.isUndef();
    const bool IsDef = MO.isDef();
    if (IsUse && SeenReads.insert(Reg.id()).second)
      ++Reads;
    if (IsDef && SeenWrites.insert(Reg.id()).second)
      ++Writes;
  }
  return {Reads, Writes};
}

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPORTMODEL_H
