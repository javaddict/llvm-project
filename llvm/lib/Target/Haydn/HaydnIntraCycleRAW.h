//===- HaydnIntraCycleRAW.h - no-forwarding intra-bundle RAW -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The ONE shared mechanism for Haydn's no-forwarding intra-bundle RAW law
// (hard constraint #7 — one mechanism, many instances). Haydn spec
// §Constraints: "All instructions within the same bundle read their source
// registers simultaneously" — there is NO intra-bundle forwarding, so a reader
// that shares a cycle/bundle with a writer of the same register observes the
// OLD value. A same-cycle read+write is only a TRUE RAW hazard when the write
// is LIVE (has a consumer); a dead write has no consumer and a same-bundle
// reader correctly observes the OLD value (WAR-style), so it is excluded.
//
// This helper is invoked from TWO placement authorities so they enforce the
// identical law:
//   * post-RA HaydnHazardRecognizer (MachineScheduler commit) —
//     hasSameBundleRAW / appendDefs over CurrentCycleLiveDefs.
//   * SMS HaydnResourceCycle (MachinePipeliner placement + ResMII) —
     // D999: SMS placement now calls the MI overload (operand-aware).
//
// The scheduler/pipeliner issues writers before their RAW readers (dataflow /
// topological order), so when the reader is the candidate the writer's live
// def is already in the live-def set and the predicate trips. WAR (reader
// issued before writer) does NOT trip: the writer candidate does not read that
// register, and WAR in a bundle is legal on Haydn anyway. SFR is excluded
// from the live-def RAW set (dead flag side-effects have no consumer; one
// SFR writer is enforced by WAW/ports, not this RAW predicate); R0 is NOT
// excluded (soft-zero is a real register — a same-bundle reader of a live R0
// write would observe the OLD value). Virtual registers (pre-RA) match by
// Register identity; physical registers use TRI::regsOverlap for alias/subreg
// overlap.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNINTRACYCLERAW_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNINTRACYCLERAW_H

#include "MCTargetDesc/HaydnMCTargetDesc.h" // Haydn::SFR
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

namespace llvm {

/// True iff \p MI reads a register that an instruction already issued in the
/// CURRENT cycle/bundle defines as LIVE (non-dead, non-SFR). This is the
/// no-forwarding intra-bundle RAW law (Haydn spec §Constraints). \p LiveDefs is
/// the set of live defs accumulated for the current cycle; \p TRI is required
/// only for physical-register alias checks (may be null before the first emit
/// when only virtual registers are in play, mirroring the HR predicate).
/// Works with any set-like container exposing `contains()` and iteration
/// (`SmallSet<Register, N>`, `SmallSetVector<Register, N>`, ...).
template <typename LiveDefSet>
bool haydnHasIntraCycleRAW(const MachineInstr &MI, const LiveDefSet &LiveDefs,
                           const TargetRegisterInfo *TRI) {
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.readsReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    if (Reg.isVirtual()) {
      if (LiveDefs.contains(Reg))
        return true;
      continue;
    }
    if (!Reg.isPhysical() || Reg == Haydn::SFR || !TRI)
      continue;
    for (Register D : LiveDefs)
      if (D.isPhysical() && TRI->regsOverlap(Reg, D))
        return true;
  }
  return false;
}

/// Record \p MI's LIVE destination registers (non-dead, non-SFR) into
/// \p LiveDefs — the dual of haydnHasIntraCycleRAW. Every non-SFR def is a
/// candidate live def; the DEAD flag is the scheduling-DAG/liveness verdict on
/// "does this def have a consumer" — dead writes have no consumer and a
/// same-bundle reader correctly observes the OLD value, so they are NOT
/// inserted (true-RAW-only). Virtual defs (pre-RA) are tracked by Register
/// identity. Works with any set-like container exposing `insert()`.
template <typename LiveDefSet>
void haydnAppendLiveDefs(const MachineInstr &MI, LiveDefSet &LiveDefs) {
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    if (!Reg.isPhysical() && !Reg.isVirtual())
      continue;
    if (Reg == Haydn::SFR)
      continue;
    if (!MO.isDead())
      LiveDefs.insert(Reg);
  }
}

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNINTRACYCLERAW_H
