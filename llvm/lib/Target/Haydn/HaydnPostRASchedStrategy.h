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
// Phase B2 (Stream B): bundle formation moves INTO this strategy.
// enterMBB stashes CurrentMBB; leaveRegion groups same-cycle Top-zone SUs
// into an in-memory bundle list (port of AIE's computeAndFinalizeBundles);
// leaveMBB materializes the accumulated list into the MBB — a NOP per empty
// cycle + bundleWithPred/finalizeBundle for non-empty bundles (port of AIE's
// commitBlockSchedule). Dual-load packing is HR tryAddProduct → setDesc
// members (no promoteLoads residual).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCHEDSTRATEGY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCHEDSTRATEGY_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/ScheduleDAG.h"

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

  // Stash CurrentMBB for leaveMBB materialize (DAG BB is not publicly
  // accessible). Dual-load packing is HR alts tryAdd → setDesc members
  // (AIEHazardRecognizer.cpp:389; no promoteLoads residual).
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

  // Materialize the accumulated per-region bundle lists into the MBB: insert
  // a standalone NOP for each empty cycle (AIE-style cycle-level padding) and
  // a real BUNDLE MI for each non-empty cycle. Stock bundleWithPred +
  // finalizeBundle (MachineInstrBundle.h). Port of AIE commitBlockSchedule.
  void leaveMBB() override;

  // AIE materializeMultiOpcodeInstrs
  // (AIEMachineScheduler.cpp:1121-1139) — for each MI in the DAG top/bottom
  // region ranges with a selected format-member opcode in AltDescs, call
  // MI.setDesc(TII->get(*AltOpcode)). HR wrote the selection via
  // setAlternateDescriptor in commitPlacementForEmit. Ends with full
  // AltDescs.clear() (AIEMachineScheduler.cpp:1081-1082;
  // AIEAlternateDescriptors.h:74).
  void materializeMultiOpcodeInstrs();

  // Compute the bundle list for the region just scheduled: walk the Top
  // zone's scheduled MIs, group SUs sharing the same TopReadyCycle into a
  // bundle, pad idle cycles with empty bundles. The list is stashed on
  // RegionBundles for leaveMBB to materialize. The MBB is NOT mutated here.
  // (Invoked by HaydnScheduleDAGMI::exitRegion.)
  void leaveRegion(const SUnit &ExitSU);

private:
  // A single cycle's worth of instructions, in MBB order. Empty Instrs means
  // an idle cycle (materialized as a rolling-position NOP). Product format is
  // a product FormatID row (HaydnBundlePlan). Multi-MI materialize stamps
  // FormatID imm on the BUNDLE root. Singletons stay standalone MIR
  // here and are wrapped by HaydnFinalizeBundle after PostMachineScheduler
  // (AIEFinalizeBundle peer). Post-commit placement is getSlotKind on
  // member Desc (AIEBaseMCFormats.cpp:66-75).
  struct CycleBundle {
    SmallVector<MachineInstr *, 3> Instrs;
    bool empty() const { return Instrs.empty(); }
  };

  // Bundles accumulated across all regions of the current MBB, in MBB order.
  // leaveRegion appends each region's bundles here; leaveMBB materializes
  // them and clears it.
  SmallVector<CycleBundle> MBBBundles;

  const HaydnInstrInfo *HII = nullptr;

  // Current MBB (stashed in enterMBB; the DAG's BB is protected and has no
  // public accessor, so the strategy tracks the block itself).
  MachineBasicBlock *CurrentMBB = nullptr;

  // True iff schedule was called for the current region (initialize sets it
  // false; schedNode sets it true). The base drive loop calls exitRegion even
  // for empty/single-MI regions that were SKIPPED (MachineScheduler.cpp:862
  // 866) — leaveRegion must not compute bundles for those, because the DAG's
  // SUnits/region iterators are stale from the previous region.
  bool RegionWasScheduled = false;

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
