//===- HaydnTargetTransformInfo.cpp - Haydn-specific TTI -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for licensing information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnTargetTransformInfo.h"
#include "HaydnHWLoopContracts.h"
#include "MCTargetDesc/HaydnMatInt.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-tti"

STATISTIC(NumHWLoopMultiBBDeclined,
          "Number of multi-BB loops declined for Role-A hardware loops "
          "(KPI seat; early-exit / nested-outer / multi-latch residual)");
STATISTIC(NumHWLoopMultiBBAccepted,
          "Number of innermost latch-only multi-BB loops accepted for Role-A "
          "(measured SCEV/CFG extension)");
STATISTIC(NumHWLoopAccepted,
          "Number of Role-A hardware-loop candidates accepted at TTI");
STATISTIC(NumHWLoopZeroTripDeclined,
          "Number of constant zero-trip loops declined for hardware loops");
STATISTIC(NumHWLoopMultiExitDeclined,
          "Number of multi-exit loops declined for hardware loops");
STATISTIC(NumDensifyUnroll,
          "short streams given densify Partial/Runtime unroll");
STATISTIC(NumSWPDefer,
          "densify streams left rolled for software pipelining");

// AIE AIEBaseTargetTransformInfo.cpp:72-73. Default 9. Overlay uses SCEV
// small constant trip (not loop-ID min-trip metadata).
static cl::opt<unsigned> PreferSwpOverUnroll(
    "haydn-prefer-swp-over-unroll", cl::Hidden, cl::init(9),
    cl::desc("Leave densify-eligible loops rolled for software pipelining "
             "when SCEV small constant trip is at least this value."));

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

  // AIE AIEBaseTargetTransformInfo.cpp:194-208 clears Partial/Runtime on
  // every single-BB loop whose loop-ID min trip >= aie-prefer-swp-over-unroll.
  // Overlay: SCEV small constant trip (not loop-ID metadata). Apply before
  // densify eligibility so BaseT Partial/Runtime cannot still unroll a loop
  // reserved for software pipelining. Runtime / unknown trip still densifies.
  if (unsigned SmallTrip = SE.getSmallConstantTripCount(L)) {
    if (PreferSwpOverUnroll && SmallTrip >= PreferSwpOverUnroll) {
      UP.Partial = false;
      UP.Runtime = false;
      ++NumSWPDefer;
      LLVM_DEBUG(dbgs() << "HaydnTTI: defer densify (SCEV trip=" << SmallTrip
                        << " >= " << PreferSwpOverUnroll << ")\n");
      return;
    }
  }

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

  ++NumDensifyUnroll;
  LLVM_DEBUG(dbgs() << "HaydnTTI: prefer partial/runtime unroll×" << UF << " "
                    << (DualStreamMAC ? "dual-stream-MAC" : "memcopy-stream")
                    << " (InstCount=" << InstCount << " Loads=" << Loads
                    << " Stores=" << Stores << " MinEltBits=" << MinEltBits
                    << ")\n");
}

TTI::AddressingModeKind
HaydnTTIImpl::getPreferredAddressingMode(const Loop *L,
                                         ScalarEvolution *SE) const {
  // Hexagon HexagonTargetTransformInfo.cpp:100-103.
  (void)L;
  (void)SE;
  return TTI::AMK_PostIndexed;
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

  // Overlay of AIE AIEBaseTargetTransformInfo.cpp:317-320 (AIE declines
  // every multi-BB). Accept innermost single-latch/single-exit only,
  // including a measured latch-only diamond. Nested-outer, multi-latch,
  // and early-exit stay declined. Never post-RA rediscovery.
  const bool MultiBB = L->getNumBlocks() > 1;
  auto declineMultiBB = [&](const char *Why) {
    ++NumHWLoopMultiBBDeclined;
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): multi-BB declined (" << Why
                      << ")\n");
    return false;
  };
  if (MultiBB && !L->isInnermost())
    return declineMultiBB("not innermost");

  // Closed CFG seats before SCEV trip proof: unique latch + unique exit,
  // and the latch is that unique exiting block (latch-only overlay).
  if (!L->getLoopLatch()) {
    if (MultiBB)
      return declineMultiBB("no unique latch");
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): missing latch\n");
    return false;
  }
  SmallVector<BasicBlock *, 4> ExitBlocks;
  L->getExitBlocks(ExitBlocks);
  BasicBlock *Exiting = L->getExitingBlock();
  if (!Exiting || ExitBlocks.size() != 1) {
    ++NumHWLoopMultiExitDeclined;
    if (MultiBB)
      return declineMultiBB("multi-exit");
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): multi-exit declined\n");
    return false;
  }
  if (Exiting != L->getLoopLatch()) {
    if (MultiBB)
      return declineMultiBB("exit is not the latch");
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): unique exit is not the latch\n");
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
  // Gate on the unsigned *value*, not SCEV type width: an i64-typed trip
  // of 100 is legal; a trip whose unsigned max may exceed 2^32-1 is not.
  const SCEV *TripCountSCEV =
      SE.getAddExpr(BETC, SE.getOne(BETC->getType()));
  if (SE.getUnsignedRangeMax(TripCountSCEV).ugt(0xFFFFFFFFULL)) {
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): trip count exceeds 32-bit "
                         "HWLR_COUNT\n");
    return false;
  }

  // Activated COUNT must meet MinCount (HaydnHWLoopContracts). Constant
  // zero trips never arm a selector. AIE AIEBaseTargetTransformInfo.cpp
  // 322-328 uses a min-iter reject; overlay is the contracts COUNT floor.
  if (const auto *TC = dyn_cast<SCEVConstant>(TripCountSCEV)) {
    if (!haydn::hwloop::countMeetsMinLaw(TC->getAPInt().getSExtValue())) {
      ++NumHWLoopZeroTripDeclined;
      LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): constant trip below MinCount\n");
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

  if (MultiBB)
    ++NumHWLoopMultiBBAccepted;
  ++NumHWLoopAccepted;
  LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): accepted "
                    << (MultiBB ? "latch-only multi-BB"
                                : (L->isInnermost() ? "innermost ZOL"
                                                    : "outer JNZD"))
                    << " candidate\n");
  return true;
}

// Port of RISCVTargetTransformInfo.cpp:120-135. Haydn overlay: R0 is
// soft-zero (TCC_Free); otherwise HaydnMatInt (ADDI32_W simm20 / ORI32_W
// uimm20 / LUI+ADDI32_W). LOADI32 expands through the same sequence.
// Do not invent encodings; do not add a FreeZeroes parameter MatInt lacks.
static InstructionCost getIntImmCostImpl(const DataLayout &DL, const APInt &Imm,
                                         Type *Ty) {
  assert(Ty->isIntegerTy() &&
         "getIntImmCost can only estimate cost of materialising integers");
  // Port: RISCVTargetTransformInfo.cpp:129-130.
  if (Imm == 0)
    return TTI::TCC_Free;
  // Port: RISCVTargetTransformInfo.cpp:133-134.
  return HaydnMatInt::getIntMatCost(Imm, DL.getTypeSizeInBits(Ty));
}

InstructionCost
HaydnTTIImpl::getIntImmCost(const APInt &Imm, Type *Ty,
                            TTI::TargetCostKind CostKind) const {
  (void)CostKind;
  // Port: RISCVTargetTransformInfo.cpp:140.
  return getIntImmCostImpl(getDataLayout(), Imm, Ty);
}

InstructionCost HaydnTTIImpl::getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                                const APInt &Imm, Type *Ty,
                                                TTI::TargetCostKind CostKind,
                                                Instruction *Inst) const {
  assert(Ty->isIntegerTy() &&
         "getIntImmCost can only estimate cost of materialising integers");

  // Port: RISCVTargetTransformInfo.cpp:213-215.
  if (Imm == 0)
    return TTI::TCC_Free;

  // i32 RI forms only (HaydnGISel.td). i64 lives in DR64 with no ADDI64.
  const bool FitsGPRImm = Ty->getIntegerBitWidth() <= 32;
  bool TakesSimm20 = false;
  bool TakesUimm20 = false;
  unsigned ImmArgIdx = ~0U;

  switch (Opcode) {
  case Instruction::GetElementPtr:
    // Port: RISCVTargetTransformInfo.cpp:223-227.
    return TTI::TCC_Free;
  case Instruction::Store: {
    // Port: RISCVTargetTransformInfo.cpp:228-245.
    if (Idx == 1 || !Inst)
      return getIntImmCostImpl(DL, Imm, Ty);
    const auto *StoreI = cast<StoreInst>(Inst);
    if (!getTLI()->allowsMemoryAccessForAlignment(
            Ty->getContext(), DL, getTLI()->getValueType(DL, Ty),
            StoreI->getPointerAddressSpace(), StoreI->getAlign()))
      return TTI::TCC_Free;
    return getIntImmCostImpl(DL, Imm, Ty);
  }
  case Instruction::Load:
    // Port: RISCVTargetTransformInfo.cpp:246-248.
    return getIntImmCost(Imm, Ty, CostKind);
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    // ANDI32 / ORI32 / XORI32 are uimm20 (HaydnGISel.td:53-55). Not invented.
    // i64 is DR64 RR-only — materialize via LOADI64 / MatInt.
    if (!FitsGPRImm)
      return getIntImmCost(Imm, Ty, CostKind);
    TakesUimm20 = true;
    break;
  case Instruction::Add:
    // ADDI32 / ADDI32_W simm20 (HaydnGISel.td:51; HaydnMatInt.cpp:78-80).
    if (!FitsGPRImm)
      return getIntImmCost(Imm, Ty, CostKind);
    TakesSimm20 = true;
    break;
  case Instruction::Mul:
    // No MULI. Power-of-2 is SLLI32; Imm±1 power-of-2 is SLLI+ADD/SUB.
    // Port: RISCVTargetTransformInfo.cpp:278-284 (idiom, not an encoding).
    if (Imm.isPowerOf2() || Imm.isNegatedPowerOf2())
      return TTI::TCC_Free;
    if ((Imm + 1).isPowerOf2() || (Imm - 1).isPowerOf2())
      return TTI::TCC_Free;
    // Port: RISCVTargetTransformInfo.cpp:310 (no 12-bit MULI overlay).
    return getIntImmCost(Imm, Ty, CostKind);
  case Instruction::Sub:
    if (!FitsGPRImm)
      return getIntImmCost(Imm, Ty, CostKind);
    TakesSimm20 = true;
    ImmArgIdx = 1;
    break;
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    // SLLI32 / SRLI32 / SRAI32 uimm5 (HaydnGISel.td:105-107).
    if (FitsGPRImm && Idx == 1 && Imm.getSignificantBits() <= 64 &&
        isUInt<5>(Imm.getZExtValue()))
      return TTI::TCC_Free;
    // Port: RISCVTargetTransformInfo.cpp:310.
    return getIntImmCost(Imm, Ty, CostKind);
  default:
    break;
  }

  if (TakesSimm20) {
    // Port: RISCVTargetTransformInfo.cpp:299-306; Haydn overlay is simm20.
    if (Instruction::isCommutative(Opcode) || Idx == ImmArgIdx) {
      if (Imm.getSignificantBits() <= 64 && isInt<20>(Imm.getSExtValue()))
        return TTI::TCC_Free;
    }
    // Port: RISCVTargetTransformInfo.cpp:310.
    return getIntImmCost(Imm, Ty, CostKind);
  }

  if (TakesUimm20) {
    // Same RISCV 12-bit seat (cpp:299-310); Haydn overlay is uimm20.
    if (Instruction::isCommutative(Opcode) || Idx == ImmArgIdx) {
      if (Imm.isNonNegative() && Imm.getSignificantBits() <= 64 &&
          isUInt<20>(Imm.getZExtValue()))
        return TTI::TCC_Free;
    }
    // Port: RISCVTargetTransformInfo.cpp:310.
    return getIntImmCost(Imm, Ty, CostKind);
  }

  // Port: RISCVTargetTransformInfo.cpp:313-314.
  return TTI::TCC_Free;
}

InstructionCost
HaydnTTIImpl::getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx,
                                  const APInt &Imm, Type *Ty,
                                  TTI::TargetCostKind CostKind) const {
  (void)IID;
  (void)Idx;
  (void)Imm;
  (void)Ty;
  (void)CostKind;
  // Port: RISCVTargetTransformInfo.cpp:321-322.
  return TTI::TCC_Free;
}

