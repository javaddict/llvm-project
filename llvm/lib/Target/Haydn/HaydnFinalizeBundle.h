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
// Same as generic MachineInstrBundle finalization, plus wrapping remaining
// standalone real MIs. Haydn overlay stamps Format E BundleFormatRowID +
// CompletionStateID on newly wrapped singletons and copies the earliest
// member DebugLoc onto any BUNDLE root that has none. Already-bundled
// stamped roots are identity. Construction only: no row resettle, name
// peel, DFS/mode retry, keep-map rewrite, or late setDesc.
//
// Mixed-stream code-bearing inline asm is fail-closed. Mixed MemberId +
// leftover FieldSlot is fail-closed. Product Finalize after the S1 stamp
// is wrap-only (AIEFinalizeBundle.cpp:40-59). Leftover RET expand, one
// dest-window stall net, and leftover-logical inverse bake run in the
// S1 owner before the stamp so leaveMBB packets include JALR membership
// inverse and the stall overlay is in the inventory wall. Isolated
// skipped-postmisched Finalize still runs that S1 sequence (stall net
// on the logical itinerary, leftover-logical bake, then wrap) before
// the stamp. After stamp, Finalize still wraps post-stamp LBN/BR bares
// as fixed complete packet templates (same-row pad NOP completion) and
// inverse-completes those new members without restamping a committed
// row, without growing EncodedBytes, and without inserting dest-window
// stall cycles. Finalize does not sequentialize or peel.
//
// Never calls skipFunction: target-local no-reorder residual commit for
// remaining bare MIs (since GR2.4 PostMachineScheduler itself runs for
// optnone; Finalize owns true residuals such as late BR parcels).
//
// Pipeline:
//   * addPreSched2 after PostMachineScheduler (AIE2TargetMachine.cpp:242-244)
//   * addPreEmit after LBN closer (wrap-only residual templates)
//   * addPostBBSections empty (GR2.9 read-only TPC default;
//     TargetPassConfig.h:447). Freeze Verify is addPreEmitPass2.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNFINALIZEBUNDLE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNFINALIZEBUNDLE_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class AAResults;

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

/// Leftover RET / BR_JT / PseudoCALLIndirect expand. S1 runs this before
/// stampPostCommitCfgSnapshot so leaveMBB packets include JALR membership
/// inverse. Isolated -run-pass Finalize still expands unstamped fixtures.
bool haydnExpandLeftoverRetJtCall(MachineFunction &MF);

/// Wrap leftover bare reals as singleton BUNDLEs (AIEFinalizeBundle.cpp:40-59)
/// including JALR_CALL without baking onto terminator JALR. LLD HaydnCallRelax
/// rewrites a returning LUI+ADDI+JALR triple to JAL only; the compiler does
/// not splice a same-row NOP to obfuscate the matcher.
bool haydnWrapBareAndStamp(MachineFunction &MF);

/// Leftover-logical inverse bake of bundled FieldSlot children onto
/// generated members. S1 and unstamped Finalize run this before wrap+stamp
/// (PreserveStampedRow=false: leftover already-bundled may reselect row).
/// After the CFG stamp, wrap-only Finalize bakes newly wrapped members with
/// PreserveStampedRow so a committed row is not reselected.
bool haydnBakeLeftoverLogicalBundles(MachineFunction &MF, AAResults *AA,
                                     bool PreserveStampedRow);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNFINALIZEBUNDLE_H
