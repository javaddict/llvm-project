//===-- HaydnTargetMachine.h - Define TargetMachine for Haydn --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Haydn specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNTARGETMACHINE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNTARGETMACHINE_H

#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include "llvm/IR/DataLayout.h"
#include <optional>

namespace llvm {

class MachineSchedContext;
class ScheduleDAGInstrs;

class HaydnTargetMachine : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  mutable StringMap<std::unique_ptr<HaydnSubtarget>> SubtargetMap;

  // Immutable production object-encoding profile for the whole target.
  // Selected once; never inferred from parcel byte width or mnemonic.
  // Shipping HaydnFormat is E96 only — not a multi-bundle fixture selector.
  haydn::format::ObjectEncodingProfileID EncodingProfile =
      haydn::format::ObjectEncodingProfileID::E96;

public:
  HaydnTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                    StringRef FS, const TargetOptions &Options,
                    std::optional<Reloc::Model> RM,
                    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                    bool JIT);

  // Product default for the SCEV-proven hardware-loop path. AIE inserts
  // HardwareLoops unconditionally at O1+ (AIE2TargetMachine.cpp:81-82);
  // Hexagon defaults ON via DisableHardwareLoops (HexagonTargetMachine.cpp:48-49).
  // Haydn stays OFF until independent then combined qualification.
  static constexpr bool hardwareLoopsProductDefaultEnabled() { return false; }

  /// Sole production ObjectEncodingProfile (E96). Not a runtime selector flip.
  haydn::format::ObjectEncodingProfileID getObjectEncodingProfileID() const {
    return EncodingProfile;
  }

  const haydn::format::ObjectEncodingProfileDesc &
  getObjectEncodingProfile() const {
    const haydn::format::ObjectEncodingProfileDesc *P =
        haydn::format::getObjectEncodingProfile(EncodingProfile);
    assert(P && "production encoding profile missing from registry");
    return *P;
  }

  // Single AIE-aligned pipeline (AIE2TargetMachine.cpp:229-244). Order lives
  // in HaydnTargetMachine.cpp and contracts/pipeline.md. Late Finalize+Verify
  // after BranchRelaxation reuses the same Finalize/Verify
  // (AIEFinalizeBundle.cpp:40-59). Residual hygiene (R15 leftover, P19
  // Auto.td/CMake) lives in Inputs SOURCE-AUTHORITY / FAULT / CORRUPTION —
  // inventory pins, not a product registry, and not a second format pipeline.
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  // provide TTI so the IR unroller + optimizer get Haydn-specific hints.
  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;

  const HaydnSubtarget *getSubtargetImpl(const Function &F) const override;
  // DO NOT IMPLEMENT: There is no such thing as a valid default subtarget
  // subtargets are per-function entities based on the target-specific
  // attributes of each function.
  const HaydnSubtarget *getSubtargetImpl() const = delete;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;

  // Pre-RA dual-sched: HaydnScheduleDAGMILive + HaydnPreRASchedStrategy
  // (never nullptr). Pressure/live-range order for RA only — not VLIW pack.
  ScheduleDAGInstrs *
  createMachineScheduler(MachineSchedContext *C) const override;

  // Post-RA pack owner (AIE2 dual-sched contract): HaydnScheduleDAGMI +
  // HaydnPostRASchedStrategy — bundles in leaveRegion/leaveMBB. Sole VLIW
  // pack path; installed from addPreSched2 (see AIE2TargetMachine).
  ScheduleDAGInstrs *
  createPostMachineScheduler(MachineSchedContext *C) const override;

  // Like AIE2: PostRA pack is installed in addPreSched2; suppress the
  // duplicate upstream PostMachineScheduler slot after addPreSched2.
  bool targetSchedulesPostRAScheduling() const override { return true; }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNTARGETMACHINE_H
