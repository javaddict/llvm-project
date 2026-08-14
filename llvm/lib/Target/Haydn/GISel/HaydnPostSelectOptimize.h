//===-- HaydnPostSelectOptimize.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// Post-select peepholes (O1). Product elideCrossBankRoundTrips (ON O1):
// lane-store, DR-constant CSE, identity GPR↔DR pack recombine.
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

class HaydnPostSelectOptimize : public MachineFunctionPass {
public:
  static char ID;

  HaydnPostSelectOptimize();

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "Haydn Post-Selection Optimizer";
  }

private:
  // Live peeps: lane-store (MOVE32_DR + ST32 → D_SW_*), DR64 const CSE,
  // identity pack recombine.
  bool elideCrossBankRoundTrips(MachineFunction &MF);

  // MOVE32_DR_L/H + ST32 → D_SW_L/H_WITH_IMM (lane-store).
  bool tryFoldMove32DrToSw(MachineInstr &MovInst, MachineRegisterInfo &MRI,
                           const HaydnInstrInfo &TII);

  // Same-BB CSE of MOV_GPR_TO_DR64 whose GPR32 sources are constants.
  bool tryCSEConstantDR64(MachineInstr &MovInst, MachineRegisterInfo &MRI,
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
