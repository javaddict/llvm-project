//===-- HaydnCallLowering.h - Call lowering -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// This file describes how to lower LLVM calls to machine code calls for GlobalISel.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNCALLLOWERING_H
#define LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNCALLLOWERING_H

#include "llvm/CodeGen/GlobalISel/CallLowering.h"

namespace llvm {

class HaydnTargetLowering;
class MachineInstrBuilder;
class MachineIRBuilder;

class HaydnCallLowering : public CallLowering {
public:
  HaydnCallLowering(const HaydnTargetLowering &TLI);

  bool lowerReturn(MachineIRBuilder &MIRBuilder, const Value *Val,
                   ArrayRef<Register> VRegs,
                   FunctionLoweringInfo &FLI) const override;

  bool canLowerReturn(MachineFunction &MF, CallingConv::ID CallConv,
                      SmallVectorImpl<BaseArgInfo> &Outs,
                      bool IsVarArg) const override;

  bool lowerFormalArguments(MachineIRBuilder &MIRBuilder, const Function &F,
                            ArrayRef<ArrayRef<Register>> VRegs,
                            FunctionLoweringInfo &FLI) const override;

  bool lowerCall(MachineIRBuilder &MIRBuilder,
                 CallLoweringInfo &Info) const override;

private:
  // AIE AIECallLowering.cpp:592 / :622. Musttail jalr is JALR_TCO
  // (AIE2 PseudoJ_TCO_jump_ind). Musttail direct is JAL_TCO
  // (PseudoJ_TCO_jump_imm). Non-tail fnptr is JALR_CALL. Soft tail is
  // JALR_CALL+RET. Short JAL_W is cycle-neutral LLD encoding relax, not
  // a second ISel path.
  bool isEligibleForTailCallOptimization(
      MachineIRBuilder &MIRBuilder, CallLoweringInfo &Info) const;

  bool lowerTailCall(MachineIRBuilder &MIRBuilder,
                     CallLoweringInfo &Info) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNCALLLOWERING_H
