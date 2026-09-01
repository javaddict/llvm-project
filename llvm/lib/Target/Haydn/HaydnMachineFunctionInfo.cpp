//===-- HaydnMachineFunctionInfo.cpp - Haydn Machine Function Info --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

MachineFunctionInfo *HaydnMachineFunctionInfo::clone(
    BumpPtrAllocator &Allocator, MachineFunction &DestMF,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
    const {
  auto *Copy = DestMF.cloneInfo<HaydnMachineFunctionInfo>(*this);
  // Transient MI* alternate-descriptor placement map must never cross
  // clone/outline: keys point into the source MF and would retain stale
  // setDesc/placement state (phase firewall).
  Copy->AltDescs.clear();
  // Same law for the inter-block DDG registry: the graphs' MBB/MI keys
  // belong to the source function.
  Copy->InterBlockRegistry.reset();
  // Per-function S1/S2 invocation count must not cross clone/outline.
  // Inheriting the source count skips or mis-fires first-S2 reopen
  // (counter == 2) on the dest. AIE clone is identity
  // (AIEMachineFunctionInfo.cpp:33-37); Haydn overlays per-function
  // lifecycle state (pipeline.md: S1 records are one-MF lifetime).
  Copy->PostRASchedInvocations = 0;
  return Copy;
}

HaydnMachineFunctionInfo::HaydnMachineFunctionInfo(const Function &F,
                                                   const TargetSubtargetInfo *STI)
    : UsesAGU(false), HasFP(false), VarArgsStackOffset(0) {
  // Propagate the immutable production ObjectEncodingProfile from the
  // subtarget. Reject any non-production profile so synthetic test families
  // cannot leak into a MachineFunction.
  // Haydn MFI is only constructed for Haydn subtargets. Avoid dyn_cast: the
  // subtarget type is not registered in LLVM's classof hierarchy.
  if (STI) {
    EncodingProfile =
        static_cast<const HaydnSubtarget *>(STI)->getObjectEncodingProfileID();
  } else {
    EncodingProfile = haydn::format::ObjectEncodingProfileID::E96;
  }
  if (!haydn::format::isProductionProfile(EncodingProfile))
    report_fatal_error(
        "Haydn MachineFunction requires the production ObjectEncodingProfile");
}
