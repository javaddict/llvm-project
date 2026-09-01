//===-- HaydnPostSelectOptimize.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// Post-select peepholes (O1). Product elideCrossBankRoundTrips (ON O1):
// identity GPR↔DR pack recombine.
// Prefer end-to-end DR64; no GPR-pair aliasing of DR64.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPOSTSELECTOPTIMIZE_H
#define LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPOSTSELECTOPTIMIZE_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class FunctionPass;
class MachineInstr;
class MachineRegisterInfo;
class HaydnInstrInfo;

namespace haydn {
namespace postselect {

/// Lane-store immediate fit law (CB-160). The golden S_SW (ST32) and
/// D_SW_L/H store families — plain WITH_IMM and the PRE/POST writeback
/// forms — share ONE word-scaled EA law: EA = rs + (imm6 << 2). A selected
/// ST32 offset operand is already that word-scaled imm, so folding
/// MOVE32_DR_L/H + ST32 into D_SW_L/H_WITH_IMM passes it through unscaled;
/// only the signed imm6 range gate applies. On success \p ScaledImm is the
/// D_SW immediate (== \p WordOffset); returns false (fold must not fire)
/// when the value is outside signed imm6.
bool laneStoreImmForWordScaledOffset(int64_t WordOffset, int64_t &ScaledImm);

} // namespace postselect
} // namespace haydn

class HaydnPostSelectOptimize : public MachineFunctionPass {
public:
  static char ID;

  HaydnPostSelectOptimize();

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "Haydn Post-Selection Optimizer";
  }

private:
  // Identity pack recombine. Lane-store of G_TRUNC/G_UNMERGE is
  // HaydnCombine.td form_lane_store; this pass still folds selected
  // MOVE32_DR + ST32 (underaligned s64 store splits after ISel).
  // Constant-pack CSE is generic MachineCSE.
  bool elideCrossBankRoundTrips(MachineFunction &MF);

  bool tryFoldMove32DrToSw(MachineInstr &MovInst, MachineRegisterInfo &MRI,
                           const HaydnInstrInfo &TII);

  // Elide identity cross-bank round-trip:
  //   lo = MOVE32_DR_L src; hi = MOVE32_DR_H src; dst = MOV_GPR_TO_DR64 lo,hi
  //   or lo,hi = MOV_DR64_TO_GPR src; dst = MOV_GPR_TO_DR64 lo,hi
  // → OR64 dst, src, src (DR64 copy). Avoids post-RA SP pack expansion.
  bool tryElideIdentityPack(MachineInstr &MovInst, MachineRegisterInfo &MRI,
                            const HaydnInstrInfo &TII);
};

// Create a Haydn Post-Selection Optimization pass.
FunctionPass *createHaydnPostSelectOptimizePass();

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPOSTSELECTOPTIMIZE_H
