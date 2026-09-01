//===- HaydnLongBranchNormalize.h - GR2.7 in-block long form ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions; See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// GR2.7 owner of postcommit long-form promotion. Once the first
// HaydnFinalizeBundle run stamped the per-function postcommit block budget,
// generic BranchRelaxation's only promotion path (fixupUnconditionalBranch
// trampoline + insertIndirectBranch) is a CFG-creation form the wall
// refuses. But legitimate post-stamp layout mutators still exist:
//
//   * S2's single repack inside HaydnLateConvergence can shrink the
//     fallthrough span of a site the pre-S1 normalization BR accepted
//     (bundlesim_reg_cb_wua_cbr: bb.12/bb.13 -> bb.22 re-overflow after a
//     612B drop; matmult-int bb.7 -> bb.3 after bb.4/bb.5 shrink);
//   * HaydnFixupHwLoops pads/demotes grow SET->END spans
//     (product-library FIR kernels);
//   * HaydnMachineAlignment pads grow the entry span.
//
// Those events re-overflow a site whose conservatively-accepted pre-S1 form
// was short. This pass, seated immediately BEFORE every post-stamp
// BranchRelaxation invocation, rewrites each such site to the TERMINAL
// in-block long form first, so BR's fixup arms have no far site left:
//
//   LUI     scratch, <Dest>          ; committed singleton (HI12 reloc)
//   ADDI32_W scratch, scratch, <Dest>; committed singleton (LO20 reloc)
//   <inverted near cond to layout-next or original cond to Dest>
//   JALR_W  scratch, scratch, 0      ; committed singleton, last terminator
//
// Same encoding vocabulary the pre-S1 trampoline promotion and the demote
// long-latch template already use (gr27-demote-long-latch.ll); no new MBB,
// no RestoreBB, no successor edits — MF.size() is unchanged, so the wall
// holds. Scratch proof (D1.49, computed — never stored MBB livein lists):
// a GPR proven dead on EVERY clobber-obligation out-edge by the one-block
// guarded-tail LivePhysRegs walk (pristines included; a JALR_W this pass
// already installed behind a near cond executes only on the fail edge, so
// its implicit defs kill nothing on the taken edge — the ls_reg_scalar
// CHECK(13) live-through class). Arm A/B clobber every successor (LUI/ADDI
// always execute); the ZOL arm clobbers only the software-exit successor
// (parcels sit after PseudoLoopEnd). Disjoint (TRI::regsOverlap) from every
// explicit/implicit operand of every tail control-flow MI, never an
// unmentioned CSR; refusal precedes every mutation (fail-closed, no
// fallback spill).
//
// Seating (HaydnTargetMachine):
//   addPreSched2 : NOT seated — the pre-stamp normalization BR owns that
//                  window (unstamped insertIndirectBranch stays legal).
//   addPreEmitPass, before each BR after the stamp;
//   HaydnLateConvergence closure iteration, before its BR.
// No common-file edits: the pass is target-owned and called only from
// target-owned seats.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNLONGBRANCHNORMALIZE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNLONGBRANCHNORMALIZE_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnLongBranchNormalize : public MachineFunctionPass {
public:
  static char ID;

  HaydnLongBranchNormalize();

  StringRef getPassName() const override {
    return "Haydn Long-Branch Normalize";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    // Post-RA, post-commit seats only.
    return MachineFunctionProperties();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

FunctionPass *createHaydnLongBranchNormalizePass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNLONGBRANCHNORMALIZE_H
