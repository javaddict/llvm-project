//===- HaydnIntraCycleWAW.h - no-dual-write intra-bundle WAW --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The ONE shared mechanism for Haydn's same-cycle WAW law (hard constraint
// #7 — one mechanism, many instances; the WAW dual of HaydnIntraCycleRAW.h).
// Golden Constraints §General: "Instructions within the same bundle must not
// write to the same register ... Only one instruction per bundle is allowed
// to write to an SFR." The law is a property of the REGISTER FILE, not of
// liveness: two writes to one register in one cycle are a dual write the file
// cannot serialize, regardless of whether either def has a consumer. A dead
// def still occupies the write port and still collides (R0 is soft-zero, a
// real file; SFR is included — one SFR writer per cycle, dead implicit-def
// $sfr counts).
//
// W39 (2026-08-16): this law was previously enforced by three divergent
// predicates — materialize's cycleMembersHaveWAW skipped dead defs (so
// commitExactMultiMIProductCycle could commit a dead-def dual write), while
// HR hasSameBundleWAW and ResourceCycle haydnHasIntraCycleWAW included them
// ("no dual write regardless of liveness"). All three now route through this
// header; golden's no-dual-write law wins.
//
// Invoked from THREE placement/commit authorities so they enforce the
// identical law:
//   * materialize (HaydnBundleMaterialize.h cycleMembersHaveWAW — the
//     canCoissueProductCycle / commitExactMultiMIProductCycle seat);
//   * post-RA HaydnHazardRecognizer (MachineScheduler commit) —
//     hasSameBundleWAW / appendDefs over CurrentCycleDefs;
//   * SMS HaydnResourceCycle (MachinePipeliner placement + ResMII) —
//     canReserveResources(MI) / reserveResources(MI) over CurrentCycleDefs.
//
// Register identity rule (same as the RAW law): virtual registers (pre-RA)
// match by Register identity; physical registers use TRI::regsOverlap for
// alias/subreg overlap. SFR is a physreg and is deliberately INCLUDED (live
// SFR is also in the RAW live-def set; leftover unnamed dead $sfr stays
// RAW-legal via isDead). R0 is included. A null TRI disables
// physreg alias checks only — vreg identity checks still work.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNINTRACYCLEWAW_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNINTRACYCLEWAW_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

namespace llvm {

/// True iff \p Reg overlaps a member of \p Defs under the intra-cycle WAW
/// identity rule: virtual registers match by Register identity; physical
/// registers use TRI::regsOverlap (SFR and R0 included — the law is per
/// register file, not per liveness). A null TRI disables physreg alias
/// checks only.
template <typename DefSet>
bool haydnRegOverlapsDefSet(Register Reg, const DefSet &Defs,
                            const TargetRegisterInfo *TRI) {
  if (!Reg)
    return false;
  if (Reg.isVirtual())
    return Defs.contains(Reg);
  if (!Reg.isPhysical() || !TRI)
    return false;
  for (Register D : Defs)
    if (D.isPhysical() && TRI->regsOverlap(Reg, D))
      return true;
  return false;
}

/// True iff \p MI defines a register already defined by an instruction
/// issued in the CURRENT cycle/bundle — the no-dual-write WAW law (golden
/// Constraints §General). \p Defs is the set of ALL destination registers
/// accumulated for the current cycle (see haydnAppendCycleDefs); \p TRI is
/// required only for physical-register alias checks (may be null before the
/// first emit when only virtual registers are in play, mirroring the HR
/// predicate). Works with any set-like container exposing `contains()` and
/// iteration (`SmallSet<Register, N>`, `SmallSetVector<Register, N>`, ...).
template <typename DefSet>
bool haydnHasIntraCycleWAW(const MachineInstr &MI, const DefSet &Defs,
                           const TargetRegisterInfo *TRI) {
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    if (haydnRegOverlapsDefSet(MO.getReg(), Defs, TRI))
      return true;
  }
  return false;
}

/// Record \p MI's destination registers into \p Defs — the dual of
/// haydnHasIntraCycleWAW. ALL defs count, live or dead, including SFR
/// (product law is one SFR writer per cycle — a dead implicit-def $sfr
/// still collides with a second SFR def) and R0 (soft-zero is a real
/// register; borrow/restore writes are real defs). Virtual defs (pre-RA)
/// are tracked by Register identity. Works with any set-like container
/// exposing `insert()`.
template <typename DefSet>
void haydnAppendCycleDefs(const MachineInstr &MI, DefSet &Defs) {
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    if (!Reg.isPhysical() && !Reg.isVirtual())
      continue;
    Defs.insert(Reg);
  }
}

/// Closed list fold of the same law: true iff any two members of \p MIs
/// (any order — dual writes are order-independent) define overlapping
/// registers. This is the pack-reject form for a whole candidate cycle
/// (materialize's cycleMembersHaveWAW seat); the incremental
/// haydnHasIntraCycleWAW + haydnAppendCycleDefs pair is the writer-issues-
/// first form for HR/RC commit walks. Both are the SAME law: every def
/// counts, liveness does not.
template <typename MIRange>
bool haydnCycleMembersHaveWAW(const MIRange &MIs,
                              const TargetRegisterInfo *TRI) {
  SmallSet<Register, 8> Defs;
  for (const MachineInstr *MI : MIs) {
    if (!MI)
      continue;
    if (haydnHasIntraCycleWAW(*MI, Defs, TRI))
      return true;
    haydnAppendCycleDefs(*MI, Defs);
  }
  return false;
}

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNINTRACYCLEWAW_H
