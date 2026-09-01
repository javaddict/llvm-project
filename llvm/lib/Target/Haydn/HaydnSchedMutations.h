//===-- HaydnSchedMutations.h - Pre/Post-RA DAG mutations -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AIE dual-sched mutation factories.
//
// Pre-RA: PropagateIncomingLatencies, EnforceCopyEdges, FuncArgCopyEdges
// ( CopyConstrain in createHaydnPreRAScheduler)
// Post-RA: RegionEndEdges (MaxLatencyFinder + successorsAreScheduled),
// MemoryEdges (ExactLatencies), MachineSchedWAWEdges (SFR/CBR). Product
// defaults for -haydn-postra-interblock, -haydn-postra-region-end-edges,
// and -haydn-postra-waw-edges are ON since the G004 flip 2026-08-27
// (same-artifact matrix green; see HaydnTargetMachine.cpp). Enabled shape:
// IncludeStages drop plus DDG remaining-latency cut; no PerSuccEdges invent.
//
// Parked (no Haydn peer): LockDelays, BiasDepth, EmitFixedSUnits
// WAWStickyRegistersEdges. PerSuccEdges is not invented.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNSCHEDMUTATIONS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNSCHEDMUTATIONS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include <memory>
#include <vector>

namespace llvm {

std::vector<std::unique_ptr<ScheduleDAGMutation>> getHaydnPreRAMutations();
std::vector<std::unique_ptr<ScheduleDAGMutation>> getHaydnPostRAMutations();

// Product defaults for the post-RA inter-block mutations stay OFF. QoR
// campaigns 2026-08-27 (CM/DHRY, ON-vs-OFF bisect on the flip artifact):
// sms2=1 + edges=0 delivers CM -3.76% / DH -0.86% bundles, but the combined
// sms2=1 + edges=1 arm is super-additively bundle-REGRESSIVE (CM +33.38%,
// DH +12.18%). Correctness is green in every arm; the loss is pure QoR.
// The interaction (edges change the DDG the S2 replay consumes) must be
// understood before any edge flag joins the product default.
// AIE turns InterBlockLatency on (AIEMaxLatencyFinder.cpp:27). Haydn
// declined the HC#0 next-block driver; no PerSuccEdges invent.
constexpr bool haydnPostRAInterblockProductDefaultEnabled() { return false; }
constexpr bool haydnPostRARegionEndEdgesProductDefaultEnabled() { return false; }
constexpr bool haydnPostRAWAWEdgesProductDefaultEnabled() { return false; }

/// W68.2R: -haydn-postra-interblock (default off). True when the
/// inter-block DDG substrate and its consumers (effective-latency cut) are
/// enabled.
bool haydnInterBlockEnabled();

class HaydnInterBlockEdges;
class MachineBasicBlock;
class MachineFunction;
using HaydnIBEdgesByPredMap =
    DenseMap<const MachineBasicBlock *,
             SmallVector<std::unique_ptr<HaydnInterBlockEdges>, 2>>;

/// W68.2R per-function owning store for the inter-block DDGs (STATUS limit
/// #9 closure). Owned via HaydnMachineFunctionInfo (shared_ptr — MFI must
/// stay copy-constructible for MachineFunction::cloneInfo; clone() clears
/// it). Destroying the graphs requires the complete element type, so the
/// store is only created/destroyed in TUs that include
/// HaydnInterBlockScheduling.h.
struct HaydnInterBlockEdgesRegistry {
  HaydnIBEdgesByPredMap ByPred;
};

/// Publish the per-function inter-block DDGs for the effective-latency cut.
/// \p MF owns the registry (HaydnMachineFunctionInfo); the graphs are moved
/// in and keyed against the function's CURRENT CFG. Re-publishing for the
/// same function (S2 re-gather) merges under the same keys and inherits S1
/// recorded depths; publishing an empty map drops records for MBBs that no
/// longer exist (CFG mutation invalidation — STATUS limit #9 closure).
/// A null registry (flag off) clears the owning store.
void setHaydnInterBlockEdgesForFunction(MachineFunction &MF,
                                        HaydnIBEdgesByPredMap *ByPred);

/// True when the S1 pass recorded scheduled post-boundary depths for Succ
/// (W68.2 S2 gate; false when only static depths exist).
bool haydnSuccHasS1Depths(const MachineFunction &MF,
                          const MachineBasicBlock *Succ);

/// The owning registry for \p MF (HaydnMachineFunctionInfo store), or null
/// when none was published for it. Never a process-static lookup.
HaydnIBEdgesByPredMap *haydnGetInterBlockEdgesRegistry(MachineFunction &MF);
const HaydnIBEdgesByPredMap *
haydnGetInterBlockEdgesRegistry(const MachineFunction &MF);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNSCHEDMUTATIONS_H
