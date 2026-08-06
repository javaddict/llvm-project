//===-- HaydnMachineFunctionInfo.cpp - Haydn Machine Function Info --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

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
