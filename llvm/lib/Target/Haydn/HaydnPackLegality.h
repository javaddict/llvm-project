//===- HaydnPackLegality.h - packet oracle ------------*- C++ -*-===
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Single product contract for "may these ops share one Format E issue cycle?"
// Densify / post-RA pack / exhaustive tests consume this pin.
//
// Dual authority (must stay consistent; do not invent a third table):
//
//   Schedule-time  HaydnHazardRecognizer (units, ports, WAW/RAW, locked DSP,
//                  format tryAdd, CSRW↔SET_HWLOOP)
//   Encode-time    Haydn::Bundle + HaydnMCFormats (alts-derived placement /
//                  format coverage; leaveRegion setDesc(member))
//
// Product rules vs constraints / golden residual:
//
//   1. Dual R0 defs in one cycle are ILLEGAL. R0 is soft-zero (not hardwired);
//      XOR32 R0,R0,R0 and LD into R0 are real write-port consumers.
//   2. Dual live same-reg WAW is ILLEGAL.
//   3. Dual SFR writers in one cycle are ILLEGAL — including dual dead
//      implicit-def $sfr when present on MIR. Ordinary ALU/LS descriptors
//      do not name SFR; the compiler does not inject leftover implicits.
//   4. ARCTAN / SIN_COS issue alone in their cycle. Post-RA HR books the
//      exact uimm4+2 occupancy: NOP-on-unit (Reserved) and no dest-writer
//      until the dest commits. Pre-RA leaves latency on DAG SDep (bottom-up
//      recede must not grow dest windows). Scaffold OperandCycles 17 stay
//      clamped so they do not size the scoreboard.
//   5. Issue ≤ 3 entries; seven-unit injectivity; GPR 4R2W; DR 8R3W; AR 2R2W;
//      SFR 2R1W (every SFR def counts as a write, dead or live). Port law
//      is the shared haydnCycleMembersExceedPortBudget predicate (HR +
//      commit + verify). Stores (D_SW_L_WITH_IMM, S_SB_WITH_IMM / ST8,
//      ST32, ST64, …) exist only at LOADSTORE0 e0 — two stores in one
//      issue cycle are illegal. Two loads in one cycle are legal
//      (LOADSTORE0 + LOAD1), including same-base different-offset
//      (p[0]/p[1]). Same-object load-load is not an issue-cycle reject;
//      AIE memory-object bits are wait-cycle avoidance only.
//   6. Format placement: no two ops forced onto an illegal entry/unit pair
//      (exact tryAdd / generated alternatives — entry ≠ unit resource).
//   7. CSRW CSR 0x20–0x25 must not share a cycle with SET_HWLOOP family.
//   Store→load pack law (Constraints:67, not a new numbered rule): a
//   store and a load in one Format E cycle must be proven disjoint.
//   may-alias / missing AA / missing MMO refuse. Ordinary list-sched
//   is protected by ReadyCycle; multi-stage (Order latency 0) must
//   call cycleHasMayAliasStoreLoad at pack time.
//
// Exhaustive unit coverage lives in:
//   unittests/Target/Haydn/HaydnHazardRecognizerTest.cpp
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPACKLEGALITY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPACKLEGALITY_H

#include "HaydnBundlePortBudget.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnPortModel.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineInstr.h"

namespace llvm {
namespace haydn {
namespace pack {

/// Port / issue / unit-footprint conflict (rule 5). Pure data — no MI needed.
inline bool resourcesConflict(const HaydnFuncUnitWrapper &A,
                              const HaydnFuncUnitWrapper &B) {
  return A.conflict(B);
}

/// AIE memory-object wait-cycle bits stay dump-only. Golden dual-load of
/// one object (LOADSTORE0 + LOAD1, including p[0]/p[1]) is legal; a
/// same-object wait-cycle reject is an unadmitted ISA row.
inline constexpr bool memoryObjectWaitCyclesAdmitted() {
  return haydnMemoryObjectWaitCyclesAdmitted();
}

/// Generated Format E row entry capacity (registry EntryCount). One name
/// for the E2=2 / E3=3 fact — do not write a second 2/3 literal at a
/// pack-legality call site.
inline unsigned formatEEntryCapacity(format::BundleFormatRowID Row) {
  const format::BundleFormatRowDesc *Desc = format::getBundleFormatRow(Row);
  return Desc ? Desc->EntryCount : 0u;
}

/// Compile-time pins matching generated ProductRows EntryCount.
inline constexpr unsigned FormatEE2EntryCapacity = 2;
inline constexpr unsigned FormatEE3EntryCapacity = 3;

static_assert(FormatEE2EntryCapacity == 2 && FormatEE3EntryCapacity == 3,
              "generated Format E entry capacity is E2=2 / E3=3");
static_assert(!haydnMemoryObjectWaitCyclesAdmitted(),
              "memory-object wait-cycle reject stays unadmitted");
static_assert(haydnSchedCompleteModelPin() == 0,
              "CompleteModel stays 0 until admitted per-op records");

/// Product issue cap: max Format E entries per cycle (rule 5). Equals the
/// generated E3 row capacity, not a free-floating 3.
inline constexpr unsigned MaxIssuePerCycle = FormatEE3EntryCapacity;

/// Number of named Format E execution units (resource identity).
inline constexpr unsigned NumExecutionUnits = HAYDN_NUM_FU_BITS;

/// Shared RF-port predicate (rule 5). Same haydnCycleMembersExceedPortBudget
/// consumed by commit (Materialize) — never a second port check here.
/// Verify consumes the same predicate on its own track.
inline bool cycleExceedsSharedPortBudget(ArrayRef<MachineInstr *> Instrs) {
  return bundle::haydnCycleMembersExceedPortBudget(Instrs);
}

/// Rule 4 / solo-class matrix (PortModel). SIN_COS/ARCTAN issue-alone is
/// the class-1 arm; LUI/ADDI32_W e0-alone and CSRW↔SET are the other two
/// named gates. Member identity stays the HR peel, not a second list.
inline HaydnSoloIssueClass soloIssueClass(unsigned Opcode) {
  return haydnClassifySoloIssueOpcode(Opcode);
}

inline bool issuesAloneInCycle(unsigned Opcode) {
  return haydnOpcodeIssuesAloneInCycle(Opcode);
}

/// Rule 4 window law (PortModel). Occupancy is uimm4+2; NOP-on-unit and
/// no dest-writer ride the same named law. Member peel stays at HR.
inline HaydnSinCosArctanWindowLaw sinCosArctanWindowLaw() {
  return haydnSinCosArctanWindowLaw();
}

inline unsigned sinCosWindowOccupancy(const MachineInstr &MI) {
  return haydnSinCosWindowOccupancy(MI);
}

/// Named SF1 / HR same-cycle laws: SIN_COS/ARCTAN issue-alone,
/// CSRW 0x20-0x25 vs SET_HWLOOP, and LUI/ADDI32_W e0-alone.
/// One predicate for the HR emit path, ResourceCycle MI reserve, and
/// the multi-stage fitInInterval conjunct. Occupied empty = no violation.
inline bool cycleViolatesNamedSameCycleLaws(
    const MachineInstr &Cand, ArrayRef<const MachineInstr *> Occupied) {
  if (Occupied.empty())
    return false;
  const unsigned CandOpc = Cand.getOpcode();
  if (haydnOpcodeIssuesAloneInCycle(CandOpc))
    return true;
  const bool CandAbs = haydnIsAbsMaterializeOpcode(CandOpc);
  const bool CandSetup = haydnClassifySoloIssueOpcode(CandOpc) ==
                         HaydnSoloIssueClass::CsrwSetHwloop;
  const bool CandCsrw = haydnHwloopCsrAddr(Cand) >= 0;
  for (const MachineInstr *O : Occupied) {
    if (!O)
      continue;
    const unsigned Opc = O->getOpcode();
    if (haydnOpcodeIssuesAloneInCycle(Opc))
      return true;
    if (CandAbs || haydnIsAbsMaterializeOpcode(Opc))
      return true;
    const bool OccSetup = haydnClassifySoloIssueOpcode(Opc) ==
                          HaydnSoloIssueClass::CsrwSetHwloop;
    if ((CandSetup && haydnHwloopCsrAddr(*O) >= 0) ||
        (CandCsrw && OccSetup))
      return true;
  }
  return false;
}

inline bool cycleViolatesNamedSameCycleLaws(ArrayRef<MachineInstr *> Occupied,
                                            const MachineInstr &Cand) {
  SmallVector<const MachineInstr *, 4> ConstOcc(Occupied.begin(),
                                                Occupied.end());
  return cycleViolatesNamedSameCycleLaws(
      Cand, ArrayRef<const MachineInstr *>(ConstOcc));
}

/// Tag printed by the multi-stage host so the three named laws stay one
/// string (HR + ResourceCycle + remarks).
inline constexpr const char *NamedSameCycleLawsTag =
    HAYDN_NAMED_SAME_CYCLE_LAWS_TAG;

/// True when the cycle breaks the SIN_COS/ARCTAN alone-in-cycle window
/// (companion members, two window ops, or occupancy below the min).
inline bool cycleViolatesSinCosWindow(ArrayRef<MachineInstr *> Instrs) {
  unsigned WindowOps = 0;
  const MachineInstr *WindowMI = nullptr;
  for (MachineInstr *MI : Instrs) {
    if (!MI)
      continue;
    if (!haydnOpcodeIssuesAloneInCycle(MI->getOpcode()))
      continue;
    ++WindowOps;
    WindowMI = MI;
  }
  if (WindowOps == 0)
    return false;
  if (WindowOps > 1 || Instrs.size() > 1)
    return true;
  return !WindowMI || haydnSinCosWindowOccupancy(*WindowMI) <
                          HAYDN_SINCOS_OCCUPANCY_MIN;
}

/// Constraints:67 pack-layer gate. True when a store/load pair in this
/// cycle is not proven disjoint. Null AA or empty MMOs fail closed
/// (MachineInstr::mayAlias is conservative). Dual-load is not this law.
inline bool cycleHasMayAliasStoreLoad(ArrayRef<const MachineInstr *> Instrs,
                                      AAResults *AA) {
  for (const MachineInstr *StoreMI : Instrs) {
    if (!StoreMI || !StoreMI->mayStore())
      continue;
    for (const MachineInstr *LoadMI : Instrs) {
      if (!LoadMI || LoadMI == StoreMI || !LoadMI->mayLoad())
        continue;
      if (StoreMI->mayAlias(AA, *LoadMI, /*UseTBAA=*/true))
        return true;
    }
  }
  return false;
}

inline bool cycleHasMayAliasStoreLoad(ArrayRef<MachineInstr *> Instrs,
                                      AAResults *AA) {
  SmallVector<const MachineInstr *, 4> ConstInstrs(Instrs.begin(),
                                                   Instrs.end());
  return cycleHasMayAliasStoreLoad(ArrayRef<const MachineInstr *>(ConstInstrs),
                                   AA);
}

} // namespace pack
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPACKLEGALITY_H
