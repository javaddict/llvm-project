//===-- HaydnFixupHwLoops.h - Post-stamp HWLoop layout closer ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Library closer (not a pass): internal alignment first, then inner-first
// Off1/Off2 FitPatch plus EncodedBytes NOP pads on retained SET_HWLOOP
// members. Consults LayoutSite canFitPatch/Dest/Dest2/Rank. Caller is
// stamped LongBranchNormalize. Never CFG-demotes, peels, or sinks.
// No-op unless MF has PostCommitCfgSnapshot.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNFIXUPHWLOOPS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNFIXUPHWLOOPS_H

namespace llvm {

class HaydnInstrInfo;
class MachineFunction;

namespace haydn::hwloop {

/// Inner-first FitPatch of retained HWLoop Off1/Off2 plus EncodedBytes
/// NOP pads. No-op unless \p MF has PostCommitCfgSnapshot. Never calls
/// demoteHardwareLoopToSoftware. A still-hard-unencodable retained loop
/// is a named fatal (formation must have demoted).
bool closeRetainedHwLoops(MachineFunction &MF, const HaydnInstrInfo &TII);

} // namespace haydn::hwloop
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNFIXUPHWLOOPS_H
