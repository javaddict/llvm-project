//===- HaydnFinalizeBundle.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Port of AIEFinalizeBundle (AIEFinalizeBundle.h:17-19 / .cpp:22-54).
//
// This pass provides the same functionality as the generic Bundle Finalization
// in MachineInstrBundle, except that it also bundles standalone instructions.
// After PostMachineScheduler multi-MI materialize (Format E row/completion
// stamp), every remaining non-meta, non-bundled real MI becomes a singleton
// BUNDLE with durable Format E BundleFormatRowID + CompletionStateID.
//
// Never calls skipFunction: this is target-local no-reorder commit ownership
// for remaining bare MIs (including when PostMachineScheduler quality-skips
// optnone). Plain O0 without optnone still runs postmisched first and may
// already hold multi-MI full-fill packs; already-bundled roots are left alone
// by the wrap loop. Cutover is identity on roots the independent inverse
// already accepts; residual FieldSlots still bind to generated members.
// Mixed MemberId + leftover FieldSlot is fail-closed. Reloc CSRW_W carries
// typed (row, entry, MemberId, CSR I8 fixup-kind) through setDesc. Both
// paths leave only committed Format-E cycles for product emission.
// Singleton completion is full-slot architectural NOP pad (AllEntriesReal),
// not unqualified underfill/singleton stub invent.
//
// Also empty-cycle tryAdd → setDesc on bare multi-slot logicals before wrap
// (AIEMachineScheduler.cpp:1121-1139 peer). Idempotent on already-setDesc
// members / ops without PlacementAlternatives.
//
// After wrap/stamp, copy the earliest member DebugLoc onto any BUNDLE root
// that has none (generic finalizeBundle already does this for new wraps —
// MachineInstrBundle.cpp:90-136; Hexagon packetize-debug-loc.mir). This pass
// also fills already-bundled roots it otherwise skips, so DwarfDebug
// beginInstruction on the top-level BUNDLE (AsmPrinter iterates MBB, not
// bundled children) still records line-table / is_stmt.
//
// Pipeline:
//   * addPreSched2 after PostMachineScheduler (AIE2TargetMachine.cpp:242-244)
//   * addPreEmit after BR/Fixup/BR (Haydn late firewall; AIE PreEmit empty)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNFINALIZEBUNDLE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNFINALIZEBUNDLE_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnFinalizeBundle : public MachineFunctionPass {
public:
  static char ID;

  HaydnFinalizeBundle();

  StringRef getPassName() const override {
    return "Haydn Bundle Finalization";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    // Post-RA (after PostMachineScheduler); tolerate either reg form.
    return MachineFunctionProperties();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

FunctionPass *createHaydnFinalizeBundlePass();

/// Wrap remaining bare MIs as Format E singleton cycles. Does not restamp
/// already-bundled roots (missing-row E2 default stays fatal at the
/// printer). Shared first loop of Finalize; late PreEmit Finalize reuses
/// it after BranchRelaxation insertIndirectBranch (AIEFinalizeBundle.cpp:40-59).
bool haydnRecommitLateMixedBare(MachineFunction &MF);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNFINALIZEBUNDLE_H
