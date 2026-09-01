//===-- HaydnLateConvergence.h - bounded late repair loop -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// W68.3R: the bounded late convergence driver (contracts/pipeline.md "Range,
// alignment, and convergence"). When -haydn-sms2 is on, this pass owns the
// W68.2R S2 seat at addPostBBSections: after every common executable writer
// (outliner/split/BB sections) and before the closure Finalize+Verify. It
// chooses current physical MIs and runs the contract loop
//
//   S2 on current inventory (fresh PostMachineScheduler invocation)
//   -> regenerate stalls/alignment (fresh HaydnLatencyStalls)
//   -> validate or monotonically demote HWLoops (fresh HaydnFixupHwLoops;
//      fixupOne recomputes Off1/Off2 windows from CURRENT layout, so each
//      invocation IS the post-S2 revalidation)
//   -> BranchRelaxation LAST in the mutating iteration
//   -> repeat when any executable inventory or relevant prefix changed
//
// Per-prefix budgets (pipeline.md "Range, alignment, and convergence"):
// each source/target pair that range decisions consume (branch dest or
// HWLoop START/END) records encoded bytes via TII->getInstSizeInBytes plus
// worst-case alignment pad from the same Offset/postOffset model
// BranchRelaxation uses, overlaying Haydn's parcel-rounded MBB gap. S2 is
// no-growth when every surviving pair's final encoded+pad charge is within
// that entry budget. Local prefix growth still runs the bounded loop;
// whole-function byte totals are not a prefix proof.
//
// Termination: HWLoops only demote (SET_* -> software loop; growth is a
// hard diagnostic), padding is regenerated (not accumulated) each
// iteration, and the census (per-MBB bytes + per-prefix charges) must
// reach a fixed point. Indirect (JALR long-form) promotion is
// IRREVERSIBLE: the TII guards (analyzeBranch returns unanalyzable at
// JALR; removeBranch keeps LUI+ADDI32_W+JALR_W sites intact top-level
// and bundled) close every swap-back path, so IndirectCount is
// non-decreasing. The historical gcc_layout t018 swap-back narrative
// (ad8bbc4ac8c9) predates those guards and is stale; the
// #indirect-sites term in the bound is dead slack, retained because it
// is slack-safe. The bound is MaxIterations = (#conditional branches +
// #hardware-loop setups + #indirect sites) + 2; exhaustion of the bound
// without a fixed point is a hard diagnostic.
//
// Every mutator is an existing pass instantiated fresh per iteration — there
// is no second scheduler, no fingerprint, no accumulated pad state, and no
// persistent frontier here.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNLATECONVERGENCE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNLATECONVERGENCE_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

/// Bounded convergence loop for one function. Returns true if any iteration
/// changed the function. Exposed for the driver pass below; the loop itself
/// is reusable by the pass only (single seat).
bool runHaydnLateConvergence(MachineFunction &MF,
                             MachineFunctionPass &DriverPass);

/// The pipeline-registered driver pass. Seated in addPostBBSections under
/// -haydn-sms2 (after the common executable tail; before closure Finalize).
/// Default-off; no-op otherwise. Replaces the one-shot S2+BR pair.
class HaydnLateConvergencePass : public MachineFunctionPass {
public:
  static char ID;
  HaydnLateConvergencePass();

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "Haydn Late Layout Convergence Loop";
  }

  /// Required: MLI/MDT/AA/TargetPassConfig for inner S2. Preserves AA and
  /// TargetPassConfig (IR-level / immutable). Preserves MDT/MLI after
  /// in-place recalculate. Does not preserve CFG: inner BranchRelaxation
  /// and HWLoop demote may split or insert blocks.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

FunctionPass *createHaydnLateConvergencePass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNLATECONVERGENCE_H
