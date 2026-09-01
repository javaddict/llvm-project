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
  // G005 remark observations are per-function KPI, not placement truth:
  // never remap them into the outlined copy.
  Copy->SMSLoopRecords.clear();
  // Durable SMS kernel metadata is MBB-keyed: remap into DestMF, drop
  // entries whose source block was not cloned.
  DenseMap<const MachineBasicBlock *, SMSSWPSInfo> Remapped;
  for (const auto &KV : SMSLoopInfos) {
    MachineBasicBlock *SrcBB = const_cast<MachineBasicBlock *>(KV.first);
    auto It = Src2DstMBB.find(SrcBB);
    if (It == Src2DstMBB.end() || !It->second)
      continue;
    Remapped[It->second] = KV.second;
  }
  Copy->SMSLoopInfos = std::move(Remapped);
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
