//===- HaydnTargetTransformInfo.h - Haydn-specific TTI ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for licensing information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file implements a TargetTransformInfo analysis pass specific to the
// Haydn target machine. The key customization is enabling runtime partial
// loop unrolling, which is the prerequisite for software pipelining (SMS)
// on Haydn's short VLIW loops.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNTARGETTRANSFORMINFO_H

#include "Haydn.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"

namespace llvm {

class HaydnTTIImpl final : public BasicTTIImplBase<HaydnTTIImpl> {
  using BaseT = BasicTTIImplBase<HaydnTTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const HaydnSubtarget &ST;
  const HaydnTargetLowering &TLI;

  const TargetSubtargetInfo *getST() const { return &ST; }
  const TargetLowering *getTLI() const { return &TLI; }

public:
  explicit HaydnTTIImpl(const HaydnTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getParent()->getDataLayout()), ST(*TM->getSubtargetImpl(F)),
        TLI(*ST.getTargetLowering()) {}

  // Enable runtime partial unrolling. This is the prerequisite for SMS:
  // Haydn's MAC-reduction loops are too short for multi-stage pipelining
  // because ResMII ≈ schedule span. Unrolling ×2 doubles the body →
  // span > II → multi-stage schedules appear. Mirrors Hexagon's approach.
  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE) const override;

  // Tell LSR that Haydn supports post-increment loads/stores
  // (D_LDW_POST_IMM/REG and D_SDW_POST_IMM). When LSR knows the target
  // supports post-increment addressing, it converts multi-offset GEP chains
  // into sequential pointer advances that the LoadStoreOptimizer can fuse
  // into single post-increment load/store instructions.
  bool isIndexedLoadLegal(TTI::MemIndexedMode Mode, Type *Ty) const override;
  bool isIndexedStoreLegal(TTI::MemIndexedMode Mode, Type *Ty) const override;


  // IR-level hardware-loop recognition. Delegates trip-count
  // derivation to ScalarEvolution (which resolves runtime inits, runtime
  // limits, and non-unit strides symbolically — the patterns that the
  // post-RA recognizer cannot recover from physical registers after spills).
  // Mirrors AIE's AIETTICommon::isHardwareLoopProfitable
  // (AIEBaseTargetTransformInfo.cpp:292-367) and ARM's hook
  // (ARMTargetTransformInfo.cpp:2380-2472). The upstream HardwareLoops pass
  // (llvm/lib/CodeGen/HardwareLoops.cpp) inserts llvm.set.loop.iterations
  // llvm.loop.decrement intrinsics, which GlobalISel then selects to
  // LoopStart / PseudoLoopEnd pseudos (Phase 3).
  bool isHardwareLoopProfitable(Loop *L, ScalarEvolution &SE,
                                AssumptionCache &AC, TargetLibraryInfo *LibInfo,
                                HardwareLoopInfo &HWLoopInfo) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNTARGETTRANSFORMINFO_H
