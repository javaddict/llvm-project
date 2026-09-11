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
// refuses. Post-stamp layout mutators that can re-overflow a pre-S1 short:
//
//   * closeRetainedHwLoops EncodedBytes NOP pads grow SET->END spans
//     (product-library FIR kernels);
//   * padInternalMBBAlignment idle packets grow internal spans.
//
// GR1.5 unseated post-stamp generic BR; GR1.7 deleted S2/LateConvergence.
// This pass rewrites each such site to the TERMINAL in-block long form
// so no CFG-creating repair remains:
//
//   LUI     scratch, <Dest>          ; before first remaining control
//   ADDI32_W scratch, scratch, <Dest>; (firstControlMI, not getFirstTerminator)
//   <inverted near cond to analyzed FBB; layout-next only when FBB is
//    null and MBB.isLayoutSuccessor && successor && != TBB && in-range
//    (one-way fallthrough); or original cond to in-range TBB>
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
//   addPreSched2 before the one generic BR (RestoreBB while CFG is
//                  mutable; unstamped insertIndirectBranch stays legal);
//   addPreSched2 after S1 (post-stamp in-block long form; no BR);
//   addPreEmitPass (library closeRetainedHwLoops then in-block long form).
// GR1.7 deleted LateConvergence / extra post-stamp BR seats.
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
