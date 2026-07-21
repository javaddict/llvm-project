//===-- HaydnMachineFunctionInfo.h - Haydn machine function info -*- C++ -*-=
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares Haydn-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNMACHINEFUNCTIONINFO_H

#include "HaydnAlternateDescriptors.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class HaydnMachineFunctionInfo : public MachineFunctionInfo {
  bool UsesAGU = false;
  bool HasFP = false;
  int VarArgsStackOffset = 0;
  // Frame index of the varargs GPR save area (legacy single-cursor alias for
  // VarArgsGprFI). Set during lowerFormalArguments for variadic functions.
  // 1 if not a variadic function. Kept for back-compat with older readers.
  int VarArgsFrameIndex = -1;

  //===--------------------------------------------------------------------===
  // Two-bank varargs. The Haydn ABI uses TWO argument register
  // banks — GPR R1–R7 for i32/ptr and DR D0–D3 for i64/f64/SIMD — for both
  // fixed and variadic args. A variadic callee therefore spills the unallocated
  // tail of BOTH banks into separate save areas, and va_list carries an
  // independent cursor per bank (AArch64-style __gr_top/__vr_top/__gr_offs
  // __vr_offs plus an overflow __stack pointer). See.
  //===--------------------------------------------------------------------===
  // Frame index of the GPR (R1–R7) varargs save area.
  int VarArgsGprFI = -1;
  // Frame index of the DR (D0–D3) varargs save area.
  int VarArgsDrFI = -1;
  // Frame index of the overflow stack-arg region (first unnamed arg that did
  // not fit in either register bank).
  int VarArgsStackFI = -1;
  // Size in bytes of the GPR save area (NumVarGPRs * 4).
  int VarArgsGprSize = 0;
  // Size in bytes of the DR save area (NumVarDRs * 8).
  int VarArgsDrSize = 0;
  // True once saveVarArgRegisters has created the GPR/DR/stack varargs save
  // areas. The VASTART handler gates on THIS, not on `FI < 0`:
  // CreateFixedObject returns negative indices by LLVM contract, which
  // collides with the `-1` "unset" sentinel on the FI fields and would
  // false-trigger, skipping va_list initialization.
  bool HasVarArgsSaveAreas = false;

  // Permanent 4-byte in-frame spill *home* for post-RA GPR scavenge when no
  // free physreg is available (HaydnPostRAScratch: MatInt, VASTART/VACOPY, …).
  // Any scavenged GPR may land here — not tied to R12. Reserved by
  // determineCalleeSaves; lives ABOVE SP (no red zone / transient subi).
  // -1 when not yet reserved.
  int PostRAScratchFI = -1;

  // Frame index for BranchRelaxation insertIndirectBranch when all GPRs are
  // live (seed 3148 / large yarpgen). BranchRelaxation builds a fresh
  // RegScavenger without PEI's scavenger FIs, so this dedicated spill is
  // re-registered on that RS (or used for a manual spill like RISC-V).
  // -1 when not reserved.
  int BranchRelaxationScratchFI = -1;

  // slice 2a: function-lifetime alt-descriptor side-map. The post-RA
  // HaydnHazardRecognizer records the chosen (slot, variant) per MI here during
  // scheduling; the HaydnSlotVariantFinalizer reads it post-SMS to bake the
  // variant opcode (so MC emit trusts the EW tag — E-2/E-3). Owned by the MFI
  // (not the HR) because the HR is per-region while the finalizer runs after
  // all regions. Mirrors AIE's AIEAlternateDescriptors ownership.
  HaydnAlternateDescriptors AltDescs;

public:
  /// SMS result for release `#<swps>` asm comments (HiFi-like). Keyed by
  /// kernel / loop-header MBB number (stable across ModuloSchedule expand).
  struct SMSSWPSInfo {
    unsigned ResMII = 0;
    unsigned RecMII = 0;
    unsigned MII = 0;
    unsigned StageCount = 0;
    unsigned NumOps = 0;
    unsigned ScheduledII = 0;
  };

private:
  // Key by MBB pointer (stable through layout; numbers are renumbered).
  DenseMap<const MachineBasicBlock *, SMSSWPSInfo> SMSLoopInfos;

public:
  HaydnMachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI);

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override {
    return DestMF.cloneInfo<HaydnMachineFunctionInfo>(*this);
  }

  bool usesAGU() const { return UsesAGU; }
  void setUsesAGU(bool Value) { UsesAGU = Value; }

  bool hasFP() const { return HasFP; }
  void setHasFP(bool Value) { HasFP = Value; }

  int getVarArgsStackOffset() const { return VarArgsStackOffset; }
  void setVarArgsStackOffset(int Offset) { VarArgsStackOffset = Offset; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }

  // Two-bank varargs accessors.
  int getVarArgsGprFI() const { return VarArgsGprFI; }
  void setVarArgsGprFI(int FI) { VarArgsGprFI = FI; }
  int getVarArgsDrFI() const { return VarArgsDrFI; }
  void setVarArgsDrFI(int FI) { VarArgsDrFI = FI; }
  int getVarArgsStackFI() const { return VarArgsStackFI; }
  void setVarArgsStackFI(int FI) { VarArgsStackFI = FI; }
  bool hasVarArgsSaveAreas() const { return HasVarArgsSaveAreas; }
  void setHasVarArgsSaveAreas(bool V) { HasVarArgsSaveAreas = V; }
  int getVarArgsGprSize() const { return VarArgsGprSize; }
  void setVarArgsGprSize(int Size) { VarArgsGprSize = Size; }
  int getVarArgsDrSize() const { return VarArgsDrSize; }
  void setVarArgsDrSize(int Size) { VarArgsDrSize = Size; }

  // \name Post-RA emergency GPR spill home (any scavenged physreg).
  //@{
  int getPostRAScratchFI() const { return PostRAScratchFI; }
  void setPostRAScratchFI(int FI) { PostRAScratchFI = FI; }
  //@}

  // \name Branch-relaxation scratch spill.
  //@{
  int getBranchRelaxationScratchFI() const { return BranchRelaxationScratchFI; }
  void setBranchRelaxationScratchFI(int FI) { BranchRelaxationScratchFI = FI; }
  //@}

  // slice 2a: alt-descriptor side-map access. The HR records; the
  // finalizer reads.
  HaydnAlternateDescriptors &getAltDescs() { return AltDescs; }
  const HaydnAlternateDescriptors &getAltDescs() const { return AltDescs; }

  void recordSMSLoop(const MachineBasicBlock *KernelBB, SMSSWPSInfo Info) {
    SMSLoopInfos[KernelBB] = Info;
  }
  const SMSSWPSInfo *getSMSLoop(const MachineBasicBlock *KernelBB) const {
    auto It = SMSLoopInfos.find(KernelBB);
    return It == SMSLoopInfos.end() ? nullptr : &It->second;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNMACHINEFUNCTIONINFO_H
