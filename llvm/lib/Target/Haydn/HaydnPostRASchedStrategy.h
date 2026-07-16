//===-- HaydnPostRASchedStrategy.h - Haydn post-RA scheduler strategy -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Post-RA MachineScheduler strategy for the Haydn VLIW datapath.
//
// Phase B1 (Stream B, ): a thin subclass of PostGenericScheduler
// that delegates all scheduling decisions to the base. Its sole purpose in
// Phase B1 was to be a concrete strategy type the framework could instantiate
// via createSchedPostRA<HaydnPostRASchedStrategy> — the resource awareness
// comes from the HaydnHazardRecognizer, which the base PostGenericScheduler
// picks up automatically via TargetInstrInfo::CreateTargetMIHazardRecognizer
// (MachineScheduler.cpp:4295-4300).
//
// Phase B2 (Stream B, ): bundle formation moves INTO this
// strategy. enterMBB runs the dual-load slot promotion over the MBB
// before scheduling; leaveRegion groups same-cycle Top-zone SUs into an
// in-memory bundle list (port of AIE's computeAndFinalizeBundles); leaveMBB
// materializes the accumulated list into the MBB — a NOP per empty cycle +
// bundleWithPred/finalizeBundle for non-empty bundles (port of AIE's
// commitBlockSchedule). The HaydnVLIWPacketizer pass is retired.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCHEDSTRATEGY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCHEDSTRATEGY_H

#include "HaydnInterBlockScheduling.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include <optional>

namespace llvm {

class HaydnInstrInfo;
class MachineInstr;

// Post-RA scheduler strategy that forms VLIW bundles in leaveRegion/leaveMBB
// (Stream B Phase B2, ). Bundling is driven purely by the
// already-scheduled SUs' TopReadyCycle values (set by the base list
// scheduler) and the HaydnHazardRecognizer's resource model — AIE-pure, no
// separate data-hazard gate (the scheduler DAG's SDep edges carry data deps).
class HaydnPostRASchedStrategy : public PostGenericScheduler {
public:
  HaydnPostRASchedStrategy(const MachineSchedContext *C);

  ~HaydnPostRASchedStrategy() override = default;

  // Run dual-load slot promotion over the whole MBB BEFORE the base
  // drive loop begins scheduling regions. The HaydnHazardRecognizer then
  // sees LD32_S1 (Slot1_LD) naturally and schedules a slot-0 load and the
  // promoted slot-1 load into the same cycle. (Promotion cannot wait until
  // leaveRegion because the HR's slot-exclusivity rule would otherwise reject
  // two LD32s — both {SLOT0} — from ever sharing a cycle.)
  void enterMBB(MachineBasicBlock *MBB) override;

  // Override tryCandidate to prioritize memory ops as the cycle's first issue
  // (ISA-34 Gap D: hide load latency) while still allowing a ready MAC/ALU to
  // beat a second load once a load is already the best candidate — dual-load
  // + MAC co-issue for hot-loop slot_fill.
  bool tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand) override;

  // Mark that the current region was actually scheduled (the base drive loop
  // calls exitRegion even for skipped regions — MachineScheduler.cpp:862-866).
  void schedNode(SUnit *SU, bool IsTopNode) override {
    RegionWasScheduled = true;
    PostGenericScheduler::schedNode(SU, IsTopNode);
  }

  // Stage-0 PostPipeliner path: mark the region as scheduled without
  // touching SchedBoundary queues (which are only live during list schedule).
  // leaveRegion requires RegionWasScheduled to form bundles from TopReadyCycle.
  // \p SkipMultiOpcode when true, leaveRegion skips materializeMultiOpcodeInstrs
  // (PostPipeliner never ran the HR auction, so AltDescs slots are empty and
  // begin/top region iterators may not match the list-schedule contract).
  void noteRegionScheduledByPostPipeliner() {
    RegionWasScheduled = true;
    PostPipelinerRegion = true;
  }

  // Materialize the accumulated per-region bundle lists into the MBB: insert
  // a standalone NOP for each empty cycle (AIE-style cycle-level padding) and
  // a real BUNDLE MI for each non-empty cycle. Stock bundleWithPred +
  // finalizeBundle (MachineInstrBundle.h). Port of AIE commitBlockSchedule.
  // When -haydn-enable-interblock is on, runs Stage-0 HaydnInterBlockScheduling
  // after materialize (acyclic fallthrough pack only; no ZOL exit hoist).
  void leaveMBB() override;

  // ensure each scheduler-placed MI has a recorded placement slot in
  // AltDescs (HR slot or legal-slot derive). Does NOT bake `_S<k>` into
  // the MachineInstr opcode. Port of AIE's materializeMultiOpcodeInstrs shape
  // (region-only: DAG top/bottom ranges) without setDesc. Slots persist for
  // MCInstLower / AsmPrinter; encoder materializes Flex at encode time.
  void materializeMultiOpcodeInstrs();

  // Compute the bundle list for the region just scheduled: walk the Top
  // zone's scheduled MIs, group SUs sharing the same TopReadyCycle into a
  // bundle, pad idle cycles with empty bundles. The list is stashed on
  // RegionBundles for leaveMBB to materialize. The MBB is NOT mutated here.
  // (Invoked by HaydnScheduleDAGMI::exitRegion.)
  void leaveRegion(const SUnit &ExitSU);

private:
  // A single cycle's worth of instructions, in MBB order. Empty Instrs means
  // an idle cycle (materialized as a standalone NOP). Mirrors AIE's
  // MachineBundle but stripped to just the instruction list (no slot/format
  // bookkeeping — Haydn assigns slots in the MC emitter, not here).
  struct CycleBundle {
    SmallVector<MachineInstr *, 3> Instrs;
    bool empty() const { return Instrs.empty(); }
  };

  // Bundles accumulated across all regions of the current MBB, in MBB order.
  // leaveRegion appends each region's bundles here; leaveMBB materializes
  // them and clears it.
  SmallVector<CycleBundle> MBBBundles;

  const HaydnInstrInfo *HII = nullptr;

  // Stage-0 inter-block helper (constructed in ctor; gated by
  // haydn-enable-interblock inside runOnMBB).
  std::optional<HaydnInterBlockScheduling> InterBlock;

  // Current MBB (stashed in enterMBB; the DAG's BB is protected and has no
  // public accessor, so the strategy tracks the block itself).
  MachineBasicBlock *CurrentMBB = nullptr;

  // True iff schedule was called for the current region (initialize sets it
  // false; schedNode sets it true). The base drive loop calls exitRegion even
  // for empty/single-MI regions that were SKIPPED (MachineScheduler.cpp:862
  // 866) — leaveRegion must not compute bundles for those, because the DAG's
  // SUnits/region iterators are stale from the previous region.
  bool RegionWasScheduled = false;

  // True when this region was scheduled by HaydnPostPipeliner (not list
  // schedule). Cleared together with RegionWasScheduled in leaveRegion.
  bool PostPipelinerRegion = false;

  // Push the in-progress bundle as the current cycle, then pad with empty
  // bundles until reaching \p ToCycle. Invariant: Bundles.size == current
  // cycle index. Mirrors AIE's bumpCycleForBundles.
  static void bumpCycleForBundles(unsigned ToCycle,
                                  SmallVectorImpl<CycleBundle> &Bundles,
                                  CycleBundle &CurrBundle);

  // Build the bundle list for the just-scheduled region from the Top zone.
  // Port of AIE::computeAndFinalizeBundles (AIEMachineScheduler.cpp:158-234)
  // Top-zone only (PostGenericScheduler schedules top-down). Returns the
  // bundle list for the region.
  SmallVector<CycleBundle> computeRegionBundles();

  // Insert one NOP (via TII->insertNoop) per empty cycle in \p Bundles, and
  // build a BUNDLE MI per non-empty cycle via bundleWithPred + finalizeBundle.
  // Port of AIE materializeEmptyBundles + applyBundles
  // (AIEMachineScheduler.cpp:806-863, AIEHazardRecognizer.cpp:317-351).
  void materializeBundles(MachineBasicBlock &MBB,
                          SmallVector<CycleBundle> &Bundles);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCHEDSTRATEGY_H
