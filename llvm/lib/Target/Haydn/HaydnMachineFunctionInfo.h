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
#include "HaydnSchedMutations.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include <memory>
#include <string>

namespace llvm {

class HaydnMachineFunctionInfo : public MachineFunctionInfo {
  // Object-encoding profile for this function (copied from Subtarget at
  // construction). Module lowering rejects function-level disagreement.
  haydn::format::ObjectEncodingProfileID EncodingProfile =
      haydn::format::ObjectEncodingProfileID::E96;

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

  // Permanent 8-byte in-frame pack slot for DR64 construction from two GPR32
  // halves (LOADI64 both-halves-nonzero constants; MOV_GPR_TO_DR64 two-live-
  // GPR general case). Reserved by determineCalleeSaves ONLY when such a pack
  // is present (leaf functions pay no frame growth); addressed via
  // getFrameIndexReference (stable FP/SP base). This replaces the old dynamic
  // SUBI32 $r13,8 / ST32 / ST32 / LD64 / ADDI32_W $r13,8 transient, which
  // shifted SP mid-function and corrupted sibling SP-relative fixed objects
  // (CB mac_mula64_all: the hoisted i64 compare-constant 979 was stored at
  // sp=BASE-16 but loaded at sp=BASE-8 → wrong value → guest exit 11).
  // -1 when not reserved.
  int DR64PackFI = -1;

  // Permanent 4-byte in-frame spill *home* for the DR64 pack base scavenger
  // (the large-frame fallback of withDR64PackBase). Reserved by
  // determineCalleeSaves alongside DR64PackFI. The fallback nests an inner
  // withPostRAScratch for the MatInt scratch (LOADI64) that may itself spill
  // to PostRAScratchFI; this dedicated FI keeps the two spills disjoint so
  // they never overwrite each other's saved value. Lives ABOVE SP (no red
  // zone / transient subi). -1 when not reserved.
  int DR64PackBaseSpillFI = -1;

  // Frame index for BranchRelaxation insertIndirectBranch when all GPRs are
  // live (seed 3148 / large yarpgen). BranchRelaxation builds a fresh
  // RegScavenger without PEI's scavenger FIs, so this dedicated spill is
  // re-registered on that RS (or used for a manual spill like RISC-V).
  // -1 when not reserved.
  int BranchRelaxationScratchFI = -1;

  // Permanent 4-byte in-frame save slot for hwloop-demote value preservation
  // (CB-162): when the demoted loop's trip register is live after the loop
  // and no free GPR countdown exists, the trip value is stored here before
  // the loop and reloaded at the exit. Kept DISJOINT from PostRAScratchFI
  // (which holds the demote stack-counter) so the two never collide.
  // Reserved lazily by HaydnHardwareLoops demote; -1 when not reserved.
  int HwLoopDemoteSaveFI = -1;

  // Frame-freeze snapshot: first post-PEI Haydn pass (HardwareLoops then
  // ExpandPseudos; earliest wins). NumObjects+StackSize plus per-live-FI
  // offset/size so an equal-size offset swap is a violation. Skip dead FIs
  // (getObjectOffset asserts). -1 = unset. No separate snapshot type
  // (AIE AIEMachineFunctionInfo.h:69-126 has none; clone is identity).
  int64_t FrameFreezeNumObjects = -1;
  int64_t FrameFreezeStackSize = -1;
  SmallVector<int, 8> FrameFreezeLiveFIs;
  SmallVector<int64_t, 8> FrameFreezeOffsets;
  SmallVector<int64_t, 8> FrameFreezeSizes;

  static void collectLiveFrameObjects(const MachineFrameInfo &MFI,
                                      SmallVectorImpl<int> &FIs,
                                      SmallVectorImpl<int64_t> &Offsets,
                                      SmallVectorImpl<int64_t> &Sizes) {
    FIs.clear();
    Offsets.clear();
    Sizes.clear();
    for (int FI = MFI.getObjectIndexBegin(), E = MFI.getObjectIndexEnd();
         FI != E; ++FI) {
      if (MFI.isDeadObjectIndex(FI))
        continue;
      FIs.push_back(FI);
      Offsets.push_back(MFI.getObjectOffset(FI));
      Sizes.push_back(MFI.getObjectSize(FI));
    }
  }

  // Transient post-RA alt-descriptor side-map (not durable placement).
  // HaydnHazardRecognizer records chosen member opcodes during post-RA
  // scheduling; leaveRegion materialize reads then clear()s it
  // (AIEAlternateDescriptors peer). clone() clears AltDescs so chosen
  // member descriptors never leak across outline.
  HaydnAlternateDescriptors AltDescs;

  // W68.2R S1/S2 lifecycle (STATUS limit #1): how many times the post-RA
  // scheduler has been invoked on this function (S1 at addPreSched2 = 1;
  // each S2/convergence-driver invocation increments further). The
  // strategy consults this to reopen provisional BUNDLEs before any
  // second-or-later scheduling invocation, so S2 always rebuilds from
  // current bare MIs. Per-function by construction (MFI lifetime);
  // clone() zeros this so outlined/cloned MFs do not inherit the source
  // count.
  unsigned PostRASchedInvocations = 0;

  //===--------------------------------------------------------------------===
  // W68.2R per-function inter-block DDG registry (STATUS limit #9 closure).
  // S1 publishes one HaydnInterBlockEdges per CFG edge; S2 re-gathers and
  // inherits S1's recorded post-boundary depths. Lifetime = this
  // MachineFunction only: the store dies with the MF (no process-static
  // raw-pointer registry), and re-publishing is keyed against the CURRENT
  // CFG so erased/replaced MBBs drop their records (CFG/MI mutation
  // invalidation). shared_ptr (MFI must stay copy-constructible for
  // cloneInfo): element destruction happens only in TUs that include
  // HaydnInterBlockScheduling.h. clone() clears it — keys point into the
  // source MF. Dest starts a fresh per-function DDG lifetime.
  std::shared_ptr<HaydnInterBlockEdgesRegistry> InterBlockRegistry;

public:
  HaydnMachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI);

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  /// Object-encoding profile for this MachineFunction (production E96).
  haydn::format::ObjectEncodingProfileID getObjectEncodingProfileID() const {
    return EncodingProfile;
  }

  const haydn::format::ObjectEncodingProfileDesc &
  getObjectEncodingProfile() const {
    const haydn::format::ObjectEncodingProfileDesc *P =
        haydn::format::getObjectEncodingProfile(EncodingProfile);
    assert(P && "function encoding profile missing from registry");
    return *P;
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

  // \name DR64-from-GPR32-halves pack slot (no dynamic SP adjust).
  //@{
  int getDR64PackFI() const { return DR64PackFI; }
  void setDR64PackFI(int FI) { DR64PackFI = FI; }
  //@}

  // \name DR64 pack base scavenger spill home (large-frame fallback).
  //@{
  int getDR64PackBaseSpillFI() const { return DR64PackBaseSpillFI; }
  void setDR64PackBaseSpillFI(int FI) { DR64PackBaseSpillFI = FI; }
  //@}

  // \name Branch-relaxation scratch spill.
  //@{
  int getBranchRelaxationScratchFI() const { return BranchRelaxationScratchFI; }
  void setBranchRelaxationScratchFI(int FI) { BranchRelaxationScratchFI = FI; }
  //@}

  // \name Hwloop-demote live-trip save slot (CB-162).
  //@{
  int getHwLoopDemoteSaveFI() const { return HwLoopDemoteSaveFI; }
  void setHwLoopDemoteSaveFI(int FI) { HwLoopDemoteSaveFI = FI; }
  //@}

  // \name Frame-freeze snapshot (frame-deadline law).
  //@{
  /// True when the snapshot was taken (first post-PEI Haydn pass).
  bool hasFrameFreezeSnapshot() const {
    return FrameFreezeNumObjects >= 0;
  }
  /// Take the snapshot if absent (earliest-wins).
  void takeFrameFreezeSnapshot(const MachineFrameInfo &MFI) {
    if (hasFrameFreezeSnapshot())
      return;
    FrameFreezeNumObjects = static_cast<int64_t>(MFI.getNumObjects());
    FrameFreezeStackSize = static_cast<int64_t>(MFI.getStackSize());
    collectLiveFrameObjects(MFI, FrameFreezeLiveFIs, FrameFreezeOffsets,
                            FrameFreezeSizes);
  }
  /// Frame-freeze violation description, or empty when the frame is
  /// unchanged since the snapshot (or no snapshot was taken — MIR tests
  /// that skip the post-PEI passes stay legal).
  std::string frameFreezeViolation(const MachineFrameInfo &MFI) const {
    if (!hasFrameFreezeSnapshot())
      return {};
    if (static_cast<int64_t>(MFI.getNumObjects()) != FrameFreezeNumObjects ||
        static_cast<int64_t>(MFI.getStackSize()) != FrameFreezeStackSize)
      return ("frame grew after the post-PEI snapshot: objects " +
              std::to_string(FrameFreezeNumObjects) + "->" +
              std::to_string(MFI.getNumObjects()) + ", stack " +
              std::to_string(FrameFreezeStackSize) + "->" +
              std::to_string(MFI.getStackSize()));
    SmallVector<int, 8> LiveFIs;
    SmallVector<int64_t, 8> Offsets;
    SmallVector<int64_t, 8> Sizes;
    collectLiveFrameObjects(MFI, LiveFIs, Offsets, Sizes);
    if (LiveFIs == FrameFreezeLiveFIs && Offsets == FrameFreezeOffsets &&
        Sizes == FrameFreezeSizes)
      return {};
    return "frame object offsets or sizes changed after the post-PEI snapshot";
  }
  //@}

  // slice 2a: alt-descriptor side-map access. The HR records; the
  // finalizer reads.
  HaydnAlternateDescriptors &getAltDescs() { return AltDescs; }
  const HaydnAlternateDescriptors &getAltDescs() const { return AltDescs; }

  // \name W68.2R S1/S2 invocation lifecycle.
  //@{
  unsigned getPostRASchedInvocations() const { return PostRASchedInvocations; }
  /// Bump and return the invocation number (1 = S1 at addPreSched2).
  unsigned bumpPostRASchedInvocation() { return ++PostRASchedInvocations; }
  //@}

  // \name W68.2R inter-block DDG registry (per-function lifetime).
  //@{
  /// The owning registry, or null when none was published (flag off).
  const HaydnInterBlockEdgesRegistry *getInterBlockRegistry() const {
    return InterBlockRegistry.get();
  }
  /// Lazily create the owning store (first publish). Defined in
  /// HaydnSchedMutations.cpp where the element type is complete.
  HaydnInterBlockEdgesRegistry &getOrCreateInterBlockRegistry();
  /// Drop every record (flag-off publish / function transition).
  void clearInterBlockRegistry() { InterBlockRegistry.reset(); }
  //@}

};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNMACHINEFUNCTIONINFO_H
