//===-- HaydnBundlePortBudget.h - shared cycle port-budget law -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ONE shared register-file port-budget predicate for one Format E issue
// cycle, consumed by BOTH the commit side (canCoissueProductCycle /
// instrsFormOneLegalCycle / commitExactMultiMIProductCycle) and the
// independent verifier (verifyCommittedBundle). The commit-site helper
// and the verify-site helper must be the same predicate, not two
// parallel helpers that silently drift.
//
// Law: pooled per-cycle RF demand of the cycle's members must fit ONE issue
// cycle (haydnPortLowerBoundResMII <= 1). GPR 4R/2W, DR64 8R/3W, AR 2R/2W,
// SFR 2R/1W per golden Constraints §Registers. Unit geometry may place
// three ALU32s on ALU0/1/2 — three GPR writes still need >= 2 cycles under
// 2W, so the pack is illegal regardless of entry placement.
//
// This is the one-commit-site family: no second legality surface. The
// predicate takes MachineInstr* — port demand is an operand fact (MOVE32
// rd,rs,rs is 2R1W per-field), so the opcode-only view cannot evaluate it
// (same split as ResourceCycle MID-vs-MI port counting).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEPORTBUDGET_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEPORTBUDGET_H

#include "HaydnPortModel.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/MachineInstr.h"

namespace llvm {
namespace haydn {
namespace bundle {

/// Pooled RF port demand of one issue cycle's members.
struct HaydnCyclePortDemandV {
  unsigned GPRReads = 0, GPRWrites = 0;
  unsigned DRReads = 0, DRWrites = 0;
  unsigned ARReads = 0, ARWrites = 0;
  unsigned SFRReads = 0, SFRWrites = 0;
};

/// Sum per-member port demand via the shared PortModel (MI operand facts).
/// SFR uses countSFRPorts: descriptor-named SFR traffic AND private Format E
/// members' anonymous implicit(-def) $sfr operands charge (CB-161, 2026-08-21)
/// — member descs are generated geometry and may drop the logical's
/// Uses/Defs=[SFR] naming, so the operand set is the port truth.
inline HaydnCyclePortDemandV
haydnSumCyclePortDemand(ArrayRef<MachineInstr *> Instrs) {
  HaydnCyclePortDemandV D;
  for (MachineInstr *MI : Instrs) {
    if (!MI)
      continue;
    const auto [GR, GW] = countGPRPorts(*MI);
    D.GPRReads += GR;
    D.GPRWrites += GW;
    const auto [DR, DW] = countDRPorts(*MI);
    D.DRReads += DR;
    D.DRWrites += DW;
    const auto [AR, AW] = countARPorts(*MI);
    D.ARReads += AR;
    D.ARWrites += AW;
    const auto [SR, SW] = countSFRPorts(*MI);
    D.SFRReads += SR;
    D.SFRWrites += SW;
  }
  return D;
}

/// THE shared port-budget predicate for one Format E issue cycle: true when
/// the members' pooled RF demand needs more than one issue cycle.
/// Consumed by commit (P4) and by the independent verifier (P7) — never
/// fork a second port check beside this one.
///
/// SFR 2R/1W charges descriptor-named SFR traffic (SET_HWLOOP / CSR
/// / flag-setters) and, since CB-161 (2026-08-21), every implicit(-def)
/// $sfr operand on private Format E members: their generated descs can
/// drop the logical's Uses/Defs=[SFR] naming, and the operand set is the
/// port truth. Ordinary non-member ALU has no SFR operand in current MIR
/// (verified on committed ADD32_E3_* members), so no legal two-ALU / pad
/// pack exhausts the ceiling. Two SFR writers still fail the shared WAW
/// law; the port ceiling independently re-checks that pack so commit
/// /verify cannot accept a pack the HR would have refused.
inline bool
haydnCycleMembersExceedPortBudget(ArrayRef<MachineInstr *> Instrs) {
  const HaydnCyclePortDemandV D = haydnSumCyclePortDemand(Instrs);
  return haydnPortLowerBoundResMII(D.GPRReads, D.GPRWrites, D.DRReads,
                                   D.DRWrites, D.ARReads, D.ARWrites,
                                   D.SFRReads, D.SFRWrites) > 1u;
}

/// Independent verify/commit re-check of the same RF port-budget law.
/// P4 commit and P7 verifyCommittedBundle both call this; do not fork a
/// second arithmetic (AIE AIEBundle.h:62-105 canAdd overlay).
inline bool
haydnVerifyCommittedBundlePortBudget(ArrayRef<MachineInstr *> Instrs) {
  return haydnCycleMembersExceedPortBudget(Instrs);
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEPORTBUDGET_H
