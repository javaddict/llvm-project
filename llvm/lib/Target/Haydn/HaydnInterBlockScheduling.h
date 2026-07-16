//===-- HaydnInterBlockScheduling.h - Stage-0 inter-block sched -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Stage-0 Haydn inter-block scheduling (pragmatic subset of AIE's
// AIEInterBlockScheduling — NOT a full port).
//
// Problem: post-RA list scheduling is per-MBB. Independent work that sits in a
// fallthrough successor cannot fill under-used trailing bundles of its
// predecessor.
//
// Stage-0 scope (this file):
// 1. Acyclic fallthrough pack — when Pred layout-falls-through to Succ
// Succ has a unique predecessor, and neither BB is a hwloop body, pull
// leading independent MIs from Succ into Pred (before terminators) and
// co-issue them with underfilled trailing Pred instructions when safe.
//
// Explicitly NOT done (and not in AIE Stage-0 either):
// * ZOL exit→preheader hoist of “independent” exit MIs into the SET→body
// window. Invented for t−3 fill; rejected after CoreMark/ showed the
// independence contract (body clobber + preheader IV uses + …) costs more
// than any measured benefit. FixupHwLoops remains the sole deficit padder.
//
// Explicitly deferred vs full AIE InterBlockScheduling (residual):
// * Fixpoint / gathering phase / BlockType Loop|Epilogue classification
// * Bottom-zone scoreboard + inter-block residual into epilogue
// * PerSuccEdges inter-block DDG, latency/resource convergence
// * Moving epilogue ops into the ZOL body (last-iteration only / SWP)
// * Safety-margin emission between loop and epilogue
//
// Gate: -haydn-enable-interblock (default OFF; Stage-0 incomplete until
// hazard/slot model —). When off, leaveMBB is bit-identical to the
// pre-interblock path.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNINTERBLOCKSCHEDULING_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNINTERBLOCKSCHEDULING_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {

class HaydnInstrInfo;
class MachineInstr;
class TargetRegisterInfo;

// When false, HaydnInterBlockScheduling::runOnMBB is a no-op.
extern cl::opt<bool> EnableHaydnInterBlock;

// Stage-0 post-RA inter-block code motion + opportunistic co-issue.
// Owned/invoked by HaydnPostRASchedStrategy::leaveMBB after bundles for the
// current MBB have been materialized.
class HaydnInterBlockScheduling {
public:
  explicit HaydnInterBlockScheduling(const HaydnInstrInfo *HII) : HII(HII) {}

  // Attempt Stage-0 inter-block transforms for \p MBB (as Pred).
  // \return true if the MF was mutated.
  bool runOnMBB(MachineBasicBlock &MBB);

private:
  const HaydnInstrInfo *HII = nullptr;

  // Acyclic Pred→Succ fallthrough pack (see file header).
  bool tryFallthroughPack(MachineBasicBlock &Pred, MachineBasicBlock &Succ);

  // True if \p MI is a safe Stage-0 motion candidate (not a terminator
  // call, branch, bundled header, debug/meta, or FI-touching op).
  static bool isMovableCandidate(const MachineInstr &MI);

  // True if \p MI reads any physical register defined by a non-meta MI in
  // \p DefBB.
  static bool readsRegDefinedIn(const MachineInstr &MI,
                                const MachineBasicBlock &DefBB,
                                const TargetRegisterInfo *TRI);

  // True if \p A and \p B may legally share a Haydn VLIW bundle (no
  // RAW/WAW/WAR on GPRs/DR, no memory conflict, neither is a barrier).
  static bool canCoIssue(const MachineInstr &A, const MachineInstr &B,
                         const TargetRegisterInfo *TRI);

  // Splice \p MI from its parent into \p Dest before \p InsertBefore, then
  // try to co-issue with the preceding real instruction. Updates Succ
  // live-ins for any regs \p MI defined.
  bool moveAndMaybeBundle(MachineInstr &MI, MachineBasicBlock &Dest,
                          MachineBasicBlock::iterator InsertBefore);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNINTERBLOCKSCHEDULING_H
