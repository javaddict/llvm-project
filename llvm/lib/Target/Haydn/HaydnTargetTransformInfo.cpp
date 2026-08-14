//===- HaydnTargetTransformInfo.cpp - Haydn-specific TTI -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for licensing information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnTargetTransformInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-tti"

STATISTIC(NumHWLoopMultiBBDeclined,
          "Number of multi-BB loops declined for Role-A hardware loops "
          "(KPI seat; multi-BB remains gated)");
STATISTIC(NumHWLoopAccepted,
          "Number of Role-A hardware-loop candidates accepted at TTI");
STATISTIC(NumHWLoopZeroTripDeclined,
          "Number of constant zero-trip loops declined for hardware loops");
STATISTIC(NumHWLoopMultiExitDeclined,
          "Number of multi-exit loops declined for hardware loops");

namespace {

// DR bank is 64-bit. Prefer partial UF so scalar element streams fill a DR:
//   32-bit → ×2, 16-bit → ×4, 8-bit → ×8. Wider (≥64) needs no densify UF.
// Returns 0 when element size is unknown / not a DR sub-multiple.
unsigned preferDRFillUnrollFactor(unsigned EltBits) {
  if (EltBits == 0 || EltBits >= 64 || (64 % EltBits) != 0)
    return 0;
  return 64 / EltBits;
}

unsigned typeSizeInBits(Type *Ty, const DataLayout &DL) {
  if (!Ty)
    return 0;
  if (Ty->isPointerTy())
    return DL.getPointerSizeInBits(Ty->getPointerAddressSpace());
  TypeSize TS = DL.getTypeSizeInBits(Ty);
  if (TS.isScalable())
    return 0;
  return TS.getFixedValue();
}

} // namespace

void HaydnTTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                            TTI::UnrollingPreferences &UP,
                                            OptimizationRemarkEmitter *ORE) const {
  BaseT::getUnrollingPreferences(L, SE, UP, ORE);
  // Prefer partial/runtime densify for short single-BB ZOL-eligible streams.
  // Architecture: 1 load + 1 store per cycle; DR is 64-bit → preferred UF is
  // 64/eltBits (i32×2, i16×4, i8×8). Remainder stays a soft epilog so the
  // main body remains single-BB hwloop-eligible.
  //
  // Eligible classes only (keeps FIR/FFT out):
  //   dual-stream MAC: ≥2 loads + mul/mac
  //   memcopy stream:  1 load + 1 store, tiny body, no mul
  //
  // Spill / profitability: do not invent local pressure heuristics. Leave
  // Force off and let LoopUnroll's cost model (PartialThreshold + size)
  // refuse unprofitable / high-pressure cases.
  if (!L || !L->isInnermost() || L->getNumBlocks() != 1)
    return;

  unsigned InstCount = 0;
  unsigned Loads = 0;
  unsigned Stores = 0;
  unsigned MinEltBits = 0;
  bool HasMul = false;
  const DataLayout &DL = getDataLayout();

  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : *BB) {
      if (isa<PHINode>(I) || I.isDebugOrPseudoInst() || I.isTerminator())
        continue;
      ++InstCount;
      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        ++Loads;
        unsigned B = typeSizeInBits(LI->getType(), DL);
        if (B && (!MinEltBits || B < MinEltBits))
          MinEltBits = B;
      } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        ++Stores;
        unsigned B = typeSizeInBits(SI->getValueOperand()->getType(), DL);
        if (B && (!MinEltBits || B < MinEltBits))
          MinEltBits = B;
      } else if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
        if (BO->getOpcode() == Instruction::Mul ||
            BO->getOpcode() == Instruction::FMul)
          HasMul = true;
      } else if (auto *CB = dyn_cast<CallBase>(&I)) {
        if (const Function *F = CB->getCalledFunction()) {
          StringRef N = F->getName();
          if (N.contains_insensitive("mul") || N.contains_insensitive("mac"))
            HasMul = true;
        }
      }
    }
  }

  // Size guard only: densify is for short kernels; large bodies stay ZOL as-is.
  if (InstCount == 0 || InstCount > 18)
    return;

  const bool DualStreamMAC = HasMul && Loads >= 2;
  const bool MemCopyStream =
      !HasMul && Loads == 1 && Stores == 1 && InstCount <= 8;
  if (!DualStreamMAC && !MemCopyStream)
    return;

  // DR fill. Unknown element size → single densify step (legacy MAC default).
  unsigned UF = preferDRFillUnrollFactor(MinEltBits);
  if (UF == 0)
    UF = 2;

  UP.Partial = true;
  UP.Runtime = true;
  UP.AllowRemainder = true;
  UP.UnrollRemainder = true;
  // Never Force: unroller cost model is the spill/profit gate.
  UP.Count = UF;
  UP.MaxCount = UF;
  // Allow DR-fill bodies through partial cost checks (i8×8 is still small).
  if (UP.PartialThreshold < 200)
    UP.PartialThreshold = 200;

  LLVM_DEBUG(dbgs() << "HaydnTTI: prefer partial/runtime unroll×" << UF << " "
                    << (DualStreamMAC ? "dual-stream-MAC" : "memcopy-stream")
                    << " (InstCount=" << InstCount << " Loads=" << Loads
                    << " Stores=" << Stores << " MinEltBits=" << MinEltBits
                    << ")\n");
}

bool HaydnTTIImpl::isIndexedLoadLegal(TTI::MemIndexedMode Mode, Type *Ty) const {
  // Haydn supports post-increment loads (D_LDW_POST_IMM for immediate
  // stride, D_LDW_POST_REG for register stride). Tell LSR to generate
  // post-increment addressing patterns.
  return Mode == TTI::MIM_PostInc;
}

bool HaydnTTIImpl::isIndexedStoreLegal(TTI::MemIndexedMode Mode, Type *Ty) const {
  // Haydn has ST64_POST_INC; HaydnExpandPseudos lowers it to ST64+ADDI.
  // There is no LoadStoreOptimizer on this target.
  return Mode == TTI::MIM_PostInc;
}

bool HaydnTTIImpl::isHardwareLoopProfitable(
    Loop *L, ScalarEvolution &SE, AssumptionCache &AC,
    TargetLibraryInfo *LibInfo, HardwareLoopInfo &HWLoopInfo) const {
  // Product ISA feature gate. Formation still requires -haydn-enable-hwloops
  // (pass default OFF) so TTI acceptance alone never flips product policy.
  if (!ST.hasHWLoop())
    return false;

  // Role A is single-BB only. Multi-BB formation is a separate measured
  // SCEV/CFG extension and never post-RA physical-register rediscovery.
  if (L->getNumBlocks() > 1) {
    ++NumHWLoopMultiBBDeclined;
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): multi-BB declined (Role A "
                         "single-BB; multi-BB measured extension)\n");
    return false;
  }

  // Closed CFG seats before SCEV trip proof: single latch + single exit.
  if (!L->getLoopLatch() || !L->getExitingBlock()) {
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): missing latch or single exit\n");
    return false;
  }
  SmallVector<BasicBlock *, 4> ExitBlocks;
  L->getExitBlocks(ExitBlocks);
  if (ExitBlocks.size() != 1) {
    ++NumHWLoopMultiExitDeclined;
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): multi-exit declined\n");
    return false;
  }

  // Require a loop-invariant backedge-taken count — SCEV must derive a
  // symbolic trip count before ISel. Role A must not rediscover IV/limit/
  // step from physical registers after RA.
  if (!SE.hasLoopInvariantBackedgeTakenCount(L)) {
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): no loop-invariant BETC\n");
    return false;
  }

  const SCEV *BETC = SE.getBackedgeTakenCount(L);
  if (isa<SCEVCouldNotCompute>(BETC)) {
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): BETC = CouldNotCompute\n");
    return false;
  }

  // Trip count = BETC + 1. Must fit in the 32-bit HWLR_COUNT register.
  const SCEV *TripCountSCEV =
      SE.getAddExpr(BETC, SE.getOne(BETC->getType()));
  if (SE.getUnsignedRangeMax(TripCountSCEV).getBitWidth() > 32) {
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): trip count > 32 bits\n");
    return false;
  }

  // Activated COUNT must be >= 1. Constant zero trips never arm a selector.
  if (const auto *TC = dyn_cast<SCEVConstant>(TripCountSCEV)) {
    if (TC->getAPInt().isZero()) {
      ++NumHWLoopZeroTripDeclined;
      LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): constant zero trip declined\n");
      return false;
    }
  }

  // Reject calls/callbr/va_arg and soft-libcall div/rem ops.
  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : *BB) {
      if (isa<CallInst>(I) || isa<InvokeInst>(I) || isa<CallBrInst>(I))
        return false;
      if (isa<VAArgInst>(I))
        return false;
      if (I.getOpcode() == Instruction::SDiv ||
          I.getOpcode() == Instruction::UDiv ||
          I.getOpcode() == Instruction::SRem ||
          I.getOpcode() == Instruction::URem ||
          I.getOpcode() == Instruction::FDiv ||
          I.getOpcode() == Instruction::FRem)
        return false;
    }
  }

  LLVMContext &C = L->getHeader()->getContext();
  HWLoopInfo.CountType = Type::getInt32Ty(C);
  HWLoopInfo.LoopDecrement = ConstantInt::get(HWLoopInfo.CountType, 1);
  if (L->isInnermost()) {
    HWLoopInfo.IsNestingLegal = true;
    HWLoopInfo.CounterInReg = false;
  } else {
    HWLoopInfo.IsNestingLegal = true;
    HWLoopInfo.CounterInReg = true;
  }
  HWLoopInfo.PerformEntryTest = false;

  ++NumHWLoopAccepted;
  LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): accepted "
                    << (L->isInnermost() ? "innermost ZOL" : "outer JNZD")
                    << " candidate\n");
  return true;
}

