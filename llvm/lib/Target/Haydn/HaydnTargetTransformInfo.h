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

  // Prefer partial/runtime densify UF = 64/eltBits (DR=64: i32×2, i16×4,
  // i8×8) for short dual-stream MAC and 1-ld/1-st memcopy loops. Spill gate
  // is the unroller cost model (Force stays off), not a local heuristic.
  // Constant / estimated trips at/above -haydn-prefer-swp-over-unroll
  // (AIE AIEBaseTargetTransformInfo.cpp:72-73 / :204-208, default 9)
  // stay rolled so software pipelining can measure the original loop.
  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE) const override;

  // Tell LSR that Haydn supports post-increment loads/stores
  // (D_LDW_POST_IMM/REG and D_SDW_POST_IMM). When LSR knows the target
  // supports post-increment addressing, it converts multi-offset GEP chains
  // into sequential pointer advances that ISel can match as post-increment
  // load/store (HaydnExpandPseudos still splits ST64_POST_INC to ST64+ADDI).
  bool isIndexedLoadLegal(TTI::MemIndexedMode Mode, Type *Ty) const override;
  bool isIndexedStoreLegal(TTI::MemIndexedMode Mode, Type *Ty) const override;

  /// Hexagon HexagonTargetTransformInfo.cpp:104-106: bias LSR toward
  /// post-increment (Haydn D_LDW_POST_* / D_SDW_POST_*).
  TTI::AddressingModeKind
  getPreferredAddressingMode(const Loop *L, ScalarEvolution *SE) const override;

  // IR-level hardware-loop recognition. Delegates trip-count
  // derivation to ScalarEvolution (which resolves runtime inits, runtime
  // limits, and non-unit strides symbolically — the patterns that the
  // post-RA recognizer cannot recover from physical registers after spills).
  // Mirrors AIE's AIETTICommon::isHardwareLoopProfitable
  // (AIEBaseTargetTransformInfo.cpp:292-367) and ARM's hook
  // (ARMTargetTransformInfo.cpp:2380-2472). Role A is SCEV-proven
  // innermost single-latch/single-exit (single-BB, or the measured
  // multi-BB latch-only overlay of AIE's all-multi-BB decline at
  // AIEBaseTargetTransformInfo.cpp:317-320). The upstream
  // HardwareLoops pass (llvm/lib/CodeGen/HardwareLoops.cpp) inserts
  // llvm.set.loop.iterations / llvm.loop.decrement, which GlobalISel
  // selects to LoopStart / PseudoLoopEnd.
  bool isHardwareLoopProfitable(Loop *L, ScalarEvolution &SE,
                                AssumptionCache &AC, TargetLibraryInfo *LibInfo,
                                HardwareLoopInfo &HWLoopInfo) const override;

  /// Cap auto-vectorization. Haydn DR is 64-bit and intentional SIMD (intrinsics
  /// / builtins) still selects X2/X4 ops, but residual SLP/LV on under-aligned
  /// halfword streams (coremark matrix_add_const align-2, yarpgen struct
  /// stores) produced G_LOAD/STORE vectors that ISel lowered to LD32/ST32 and
  /// MEMORY_FAULT or wrong CRC after scalarize+rebuild. Report no fixed vector
  /// registers so LoopVectorize/SLP stay off for product C; legalizer still
  /// accepts explicit v2i32/v4i16/v8i8 from IR/builtins.
  TypeSize
  getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const override {
    switch (K) {
    case TargetTransformInfo::RGK_Scalar:
      return TypeSize::getFixed(32);
    case TargetTransformInfo::RGK_FixedWidthVector:
      return TypeSize::getZero();
    case TargetTransformInfo::RGK_ScalableVector:
      return TypeSize::getZero();
    }
    llvm_unreachable("unknown register kind");
  }

  unsigned getMinVectorRegisterBitWidth() const override { return 0; }

  unsigned getNumberOfRegisters(unsigned ClassID) const override {
    // ClassID 0 = scalar GPR, non-zero used as vector bank by some analyses.
    bool Vector = (ClassID != 0);
    if (Vector)
      return 16; // D0–D15
    return 16;   // R0–R15 (soft-zero / SP / LR reserved at RA)
  }

  /// Materialization cost for a standalone integer immediate (LOADI32 /
  /// HaydnMatInt). Used by ConstantHoisting when the opcode-specific hook
  /// does not keep the immediate attached.
  InstructionCost getIntImmCost(const APInt &Imm, Type *Ty,
                                TTI::TargetCostKind CostKind) const override;
  InstructionCost getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                    const APInt &Imm, Type *Ty,
                                    TTI::TargetCostKind CostKind,
                                    Instruction *Inst = nullptr) const override;
  InstructionCost
  getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx, const APInt &Imm,
                      Type *Ty, TTI::TargetCostKind CostKind) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNTARGETTRANSFORMINFO_H
