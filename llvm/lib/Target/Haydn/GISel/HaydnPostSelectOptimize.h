//===-- HaydnPostSelectOptimize.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// Post-select peepholes (O1+). Product: lane-store + DR-constant CSE under
// elideCrossBankRoundTrips. Pack-shape MOV is not folded to SEXT (see
// tryFoldSextMovToDirect — pack ≠ sign-extend).
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
  // and (stub) sext-shape MOV that intentionally does not fold pack→sext.
  bool elideCrossBankRoundTrips(MachineFunction &MF);

  // Historically folded MOV_GPR_TO_DR64(x,x) → SEXT; wrong for dual-lane pack.
  // Stub returns false; G_SEXT i32→i64 selects SEXT_GPR32_TO_DR64 directly.
  bool tryFoldSextMovToDirect(MachineInstr &MovInst, MachineRegisterInfo &MRI,
                              const HaydnInstrInfo &TII);

  // MOVE32_DR_L/H + ST32 → D_SW_L/H_WITH_IMM (lane-store).
  bool tryFoldMove32DrToSw(MachineInstr &MovInst, MachineRegisterInfo &MRI,
                           const HaydnInstrInfo &TII);

  // Same-BB CSE of MOV_GPR_TO_DR64 whose GPR32 sources are constants.
  bool tryCSEConstantDR64(MachineInstr &MovInst, MachineRegisterInfo &MRI,
                          const HaydnInstrInfo &TII);
};

// Create a Haydn Post-Selection Optimization pass.
FunctionPass *createHaydnPostSelectOptimizePass();

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPOSTSELECTOPTIMIZE_H
