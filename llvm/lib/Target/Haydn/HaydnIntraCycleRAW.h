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
// Incremental API (haydnHasIntraCycleRAW + haydnAppendLiveDefs) is
// writer-first: it tests the candidate's reads against PREVIOUS live defs
// only. The scheduler/pipeliner issues writers before their RAW readers
// (dataflow / topological order), so when the reader is the candidate the
// writer's live def is already in the live-def set and the predicate trips.
// WAR (reader issued before writer) does NOT trip: the writer candidate does
// not read that register, and WAR in a bundle is legal on Haydn anyway
// (hardware snapshot — the earlier reader correctly observes the OLD value).
// Data latency >= 1 keeps a true-RAW producer/consumer out of one ReadyCycle,
// so the incremental walk is safe for HR/RC.
//
// Cycle-list pack-reject (haydnCycleMembersHaveTrueRAW) is the same
// no-forwarding law on a whole candidate cycle in schedule order: live
// def-before-use trips; InternalRead-hidden uses still trip (readsReg()
// would hide them); dead-def read-old is excluded; legal WAR
// (use-before-def) does not trip. Bundle.h-free so the freeze verifier can
// call it without HaydnBundle.h / HaydnBundleFormatSolver.h. The Materialize
// wrapper cycleMembersHaveTrueRAW is an alias of this body.
//
// The SET_HWLOOP trip/Off vs same-cycle-producer overlay is NOT defined
// here (D1.11): its single body is haydnCycleMembersHaveHwloopTripConflict
// in HaydnPortModel.h, whose membership is derived only from
// haydnClassifyHwloopSetupOpcode over generated logicalOpcodeOrSelf —
// never from a TII name peel. Freeze/verify, commit, bake, free-pack, and
// repair all route through that one body.
//
// F49: a later live def vs an earlier read in the same LiveDefs walk is a
// silent miss of the incremental predicate. Closed both-direction
// (haydnPairHasIntraCycleRAW) applies the SAME read-vs-live-def check in
// both orders. That pair form does not distinguish legal WAR from true RAW
// and is NOT a pack-reject (cycleMembersHaveTrueRAW already rejected that
// conflation). haydnLiveDefsWalkHidesLaterDef / haydnAssertWriterFirstLiveDefsWalk
// make the miss unsilenceable on any walk that claims writer-first order.
// Live SFR is included in the live-def RAW set (same-cycle true RAW is
// illegal: Constraints simultaneous-read, Data_Latency=1, no SFR
// forwarding exception). Leftover unnamed implicit-def dead $sfr stays
// RAW-legal via !MO.isDead(), not via Haydn::SFR identity. R0 is NOT
// excluded (soft-zero is a real register — a same-bundle reader of a live
// R0 write would observe the OLD value). Virtual registers (pre-RA) match
// by Register identity; physical registers use TRI::regsOverlap for
// alias/subreg overlap.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNINTRACYCLERAW_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNINTRACYCLERAW_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include <cassert>

namespace llvm {

/// True iff \p Reg overlaps a member of \p LiveRegs under the intra-cycle
/// RAW identity rule: virtual registers match by Register identity; physical
/// registers use TRI::regsOverlap (live SFR included); a null TRI (phys)
/// does not match. This is the ONE overlap used by the incremental walk
/// and the closed both-direction pair check.
template <typename LiveRegSet>
bool haydnRegOverlapsLiveSet(Register Reg, const LiveRegSet &LiveRegs,
                             const TargetRegisterInfo *TRI) {
  if (!Reg)
    return false;
  if (Reg.isVirtual())
    return LiveRegs.contains(Reg);
  if (!Reg.isPhysical() || !TRI)
    return false;
  for (Register D : LiveRegs)
    if (D.isPhysical() && TRI->regsOverlap(Reg, D))
      return true;
  return false;
}

/// True iff \p MI reads a register that an instruction already issued in the
/// CURRENT cycle/bundle defines as LIVE (non-dead, including live SFR). This
/// is the no-forwarding intra-bundle RAW law (Haydn spec §Constraints).
/// \p LiveDefs is the set of live defs accumulated for the current cycle;
/// \p TRI is required only for physical-register alias checks (may be null
/// before the first emit when only virtual registers are in play, mirroring
/// the HR predicate).
/// Works with any set-like container exposing `contains()` and iteration
/// (`SmallSet<Register, N>`, `SmallSetVector<Register, N>`, ...).
///
/// Writer-first: only previous live defs are visible. A later live def vs an
/// earlier read in this walk is a silent miss — use
/// haydnPairHasIntraCycleRAW (closed both-direction) or
/// haydnAssertWriterFirstLiveDefsWalk (writer-first contract).
template <typename LiveDefSet>
bool haydnHasIntraCycleRAW(const MachineInstr &MI, const LiveDefSet &LiveDefs,
                           const TargetRegisterInfo *TRI) {
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.readsReg())
      continue;
    if (haydnRegOverlapsLiveSet(MO.getReg(), LiveDefs, TRI))
      return true;
  }
  return false;
}

/// Record \p MI's LIVE destination registers (non-dead, including live SFR)
/// into \p LiveDefs — the dual of haydnHasIntraCycleRAW. Every def is a
/// candidate live def; the DEAD flag is the scheduling-DAG/liveness verdict on
/// "does this def have a consumer" — dead writes have no consumer and a
/// same-bundle reader correctly observes the OLD value, so they are NOT
/// inserted (true-RAW-only). Leftover unnamed implicit-def dead $sfr stays
/// out via isDead, not register identity. Virtual defs (pre-RA) are tracked
/// by Register identity. Works with any set-like container exposing
/// `insert()`.
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
    if (!MO.isDead())
      LiveDefs.insert(Reg);
  }
}

/// Closed both-direction check: true iff either instruction's live def is
/// read by the other. Same mechanism as haydnHasIntraCycleRAW, both orders.
/// Hardware reads simultaneously, so this reports any same-cycle live-def vs
/// read, including legal WAR snapshot packs. Not a pack-reject — HR/RC keep
/// the incremental writer-first predicate so WAR stays legal.
inline bool haydnPairHasIntraCycleRAW(const MachineInstr &A,
                                      const MachineInstr &B,
                                      const TargetRegisterInfo *TRI) {
  SmallSet<Register, 8> ALive;
  SmallSet<Register, 8> BLive;
  haydnAppendLiveDefs(A, ALive);
  haydnAppendLiveDefs(B, BLive);
  return haydnHasIntraCycleRAW(B, ALive, TRI) ||
         haydnHasIntraCycleRAW(A, BLive, TRI);
}

/// True when an incremental LiveDefs walk of \p MIs would miss a later live
/// def that an earlier member reads. That is the F49 silent-miss shape
/// (reader issued before writer). Does not by itself mean the pack is
/// illegal — WAR is legal on Haydn — it means the walk is not writer-first
/// for that pair. \p MIs is a range of `const MachineInstr *` / `MachineInstr *`.
template <typename MIRange>
bool haydnLiveDefsWalkHidesLaterDef(const MIRange &MIs,
                                    const TargetRegisterInfo *TRI) {
  SmallVector<const MachineInstr *, 4> Seen;
  SmallSet<Register, 8> LiveDefs;
  for (const MachineInstr *MI : MIs) {
    if (!MI)
      continue;
    const bool ForwardHit = haydnHasIntraCycleRAW(*MI, LiveDefs, TRI);
    SmallSet<Register, 8> MILive;
    haydnAppendLiveDefs(*MI, MILive);
    for (const MachineInstr *Prev : Seen) {
      // Prev reads a live def of MI, and the incremental walk did not see
      // a writer-first RAW on MI — later def vs earlier read, missed.
      if (!ForwardHit && haydnHasIntraCycleRAW(*Prev, MILive, TRI))
        return true;
    }
    haydnAppendLiveDefs(*MI, LiveDefs);
    Seen.push_back(MI);
  }
  return false;
}

/// Writer-first contract for an incremental LiveDefs walk. Fires when a later
/// live def vs an earlier read would be a silent miss of haydnHasIntraCycleRAW.
/// Call on walks that claim dataflow / topological (writer-first) order — not
/// on arbitrary bundle member lists that may be legal WAR.
template <typename MIRange>
void haydnAssertWriterFirstLiveDefsWalk(const MIRange &MIs,
                                        const TargetRegisterInfo *TRI) {
  if (haydnLiveDefsWalkHidesLaterDef(MIs, TRI)) {
    assert(false &&
           "haydnHasIntraCycleRAW LiveDefs walk hid a later live def vs an "
           "earlier read (writer-first contract; F49)");
  }
}

/// **Available-cycle detect** (and no-forwarding RAW): true iff \p MIs in
/// schedule/issue order have a **live** def of R by an EARLIER member and a
/// use of R by a LATER member. That shape must not share one ReadyCycle.
///
/// Order-sensitive, matching HaydnHazardRecognizer::hasSameBundleRAW (which
/// tracks CurrentCycleLiveDefs incrementally as members append in issue
/// order).
///
/// A LATER member's live def read by an EARLIER member is WAR/snapshot: the
/// earlier reader correctly observes the pre-cycle (OLD) value — legal.
/// haydnPairHasIntraCycleRAW does not distinguish that shape and is NOT a
/// pack-reject.
///
///   * **Read vs dead def** of the same physreg: legal — a dead write has no
///     consumer of the new value (dead defs are not live defs).
///   * Same-MI use+def (tied / normal overwrite): legal (not cross-member).
///
/// Uses are detected via isUse() / partial-def subreg reads, not only
/// MachineOperand::readsReg() (InternalRead would hide a true dep).
inline bool haydnCycleMembersHaveTrueRAW(ArrayRef<const MachineInstr *> Instrs,
                                         const TargetRegisterInfo *TRI) {
  if (Instrs.size() < 2)
    return false;

  auto overlaps = [&](Register A, Register B) -> bool {
    if (A == B)
      return true;
    if (!TRI || !A.isPhysical() || !B.isPhysical())
      return false;
    return TRI->regsOverlap(A, B);
  };

  SmallVector<Register, 4> PriorLiveDefs;
  for (const MachineInstr *MI : Instrs) {
    if (!MI)
      continue;
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || MO.isUndef() || !MO.getReg())
        continue;
      bool IsRead = MO.isUse() || (MO.isDef() && MO.getSubReg());
      if (!IsRead)
        continue;
      Register Reg = MO.getReg();
      if (!(Reg.isPhysical() || Reg.isVirtual()))
        continue;
      for (Register D : PriorLiveDefs)
        if (overlaps(Reg, D))
          return true;
    }
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || !MO.getReg() || !MO.isDef() || MO.isDead())
        continue;
      Register Reg = MO.getReg();
      if (Reg.isPhysical() || Reg.isVirtual())
        PriorLiveDefs.push_back(Reg);
    }
  }
  return false;
}

template <typename MIRange>
bool haydnCycleMembersHaveTrueRAW(const MIRange &MIs,
                                  const TargetRegisterInfo *TRI) {
  SmallVector<const MachineInstr *, 8> Instrs;
  for (auto *MI : MIs)
    if (MI)
      Instrs.push_back(MI);
  return haydnCycleMembersHaveTrueRAW(
      ArrayRef<const MachineInstr *>(Instrs), TRI);
}

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNINTRACYCLERAW_H
