//===- HaydnTargetTransformInfo.cpp - Haydn-specific TTI -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for licensing information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnTargetTransformInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "haydn-tti"

void HaydnTTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                            TTI::UnrollingPreferences &UP,
                                            OptimizationRemarkEmitter *ORE) const {
  BaseT::getUnrollingPreferences(L, SE, UP, ORE);
  // P1: HiFi dual-stream MAC SWPS uses unroll=2 (vec_dot ops=6 fill=3.0 vs
  // Haydn ops=4 fill=2.0). Enable a *narrow* runtime partial unroll:
  // innermost single-BB only
  // hard MaxCount=2 (never full-unroll FIR/FFT giants)
  // only small IR bodies (≤24 non-PHI/non-debug ops) so FIR/FFT stay out
  // AllowRemainder so trip need not be multiple of 2; remainder is a
  // separate soft loop, main unrolled body stays single-BB ZOL-eligible
  // Prior global enable broke ZOL on larger loops — size cap is the catch.
  if (!L || !L->isInnermost() || L->getNumBlocks() != 1)
    return;
  // Extremely narrow: only tiny dual-stream MAC kernels (vec_dot class).
  // Broader caps (≤24) unrolled FIR/corr loops and destroyed their ZOLs.
  unsigned InstCount = 0;
  unsigned Loads = 0;
  bool HasMul = false;
  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : *BB) {
      if (isa<PHINode>(I) || I.isDebugOrPseudoInst() || I.isTerminator())
        continue;
      ++InstCount;
      if (isa<LoadInst>(I))
        ++Loads;
      if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
        if (BO->getOpcode() == Instruction::Mul ||
            BO->getOpcode() == Instruction::FMul)
          HasMul = true;
      }
      if (auto *CB = dyn_cast<CallBase>(&I)) {
        if (const Function *F = CB->getCalledFunction()) {
          StringRef N = F->getName();
          if (N.contains_insensitive("mul") || N.contains_insensitive("mac"))
            HasMul = true;
        }
      }
    }
  }
  // NAT-P1: dual-stream MAC densify (vec_dot class). ≥2 loads + mul/mac.
  // Cap raised 12→18 so slightly larger dual-stream bodies still get ×2
  // without opening FIR/FFT (those are InstCount≫18 or single-stream).
  // Remainder loop stays separate → main body single-BB ZOL-eligible.
  if (InstCount == 0 || InstCount > 18 || !HasMul || Loads < 2)
    return;
  UP.Partial = true;
  UP.Runtime = true;
  UP.AllowRemainder = true;
  UP.UnrollRemainder = true;
  // No Force: cost model still gates; threshold 96 densifies dual-stream.
  UP.Count = 2;
  UP.MaxCount = 2;
  if (UP.PartialThreshold < 96)
    UP.PartialThreshold = 96;
  LLVM_DEBUG(dbgs() << "HaydnTTI NAT-P1: partial/runtime unroll×2 dual-stream "
                       "MAC (InstCount="
                    << InstCount << " Loads=" << Loads << ")\n");
}

bool HaydnTTIImpl::isIndexedLoadLegal(TTI::MemIndexedMode Mode, Type *Ty) const {
  // Haydn supports post-increment loads (D_LDW_POST_IMM for immediate
  // stride, D_LDW_POST_REG for register stride). Tell LSR to generate
  // post-increment addressing patterns.
  return Mode == TTI::MIM_PostInc;
}

bool HaydnTTIImpl::isIndexedStoreLegal(TTI::MemIndexedMode Mode, Type *Ty) const {
  // Haydn has ST64_POST_INC (currently splits to ST64+ADDI, but the
  // LoadStoreOptimizer will fuse when possible).
  return Mode == TTI::MIM_PostInc;
}

bool HaydnTTIImpl::isHardwareLoopProfitable(
    Loop *L, ScalarEvolution &SE, AssumptionCache &AC,
    TargetLibraryInfo *LibInfo, HardwareLoopInfo &HWLoopInfo) const {
  // gate on the hardware-loop feature. The `generic` CPU model leaves
  // this off; `-mcpu=haydn` enables it.
  if (!ST.hasHWLoop())
    return false;


  // Require a loop-invariant backedge-taken count — SCEV must be able to
  // derive a symbolic trip count. This is the key advantage over the post-RA
  // recognizer: SCEV handles runtime inits, runtime limits, and non-unit
  // strides symbolically, while the post-RA recognizer has to recover them
  // from physical registers after spills (and fails on most of them).
  if (!SE.hasLoopInvariantBackedgeTakenCount(L)) {
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): no loop-invariant BETC\n");
    return false;
  }

  const SCEV *BETC = SE.getBackedgeTakenCount(L);
  if (isa<SCEVCouldNotCompute>(BETC)) {
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): BETC = CouldNotCompute\n");
    return false;
  }

  // Trip count = BETC + 1. Must fit in the 32-bit HWLR_COUNT register
  // (Haydn's HWLOOP counter is uimm16 for the immediate form, but the
  // register form SET_HWLOOP_REG reads a full 32-bit GPR).
  const SCEV *TripCountSCEV =
      SE.getAddExpr(BETC, SE.getOne(BETC->getType()));
  if (SE.getUnsignedRangeMax(TripCountSCEV).getBitWidth() > 32) {
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): trip count > 32 bits\n");
    return false;
  }

  // AIE policy: IR ZOL is single-BB only
  // (AIETTICommon::isHardwareLoopProfitable). AsmPrinter can place HWLR_END on
  // the latch (LoopStart BFS for PseudoLoopEnd), but multi-BB still needs a
  // single-exit / no-early-break contract that Role B validates on MIR. Keep
  // IR multi-BB closed; Role B residual (`-haydn-hwloop-role-b`) forms those.
  if (L->getNumBlocks() > 1) {
    LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): multi-BB, deferring to Role B\n");
    return false;
  }

  // Reject loops containing calls. A hardware loop is NOT preserved across a
  // call: the callee may itself lower to a hwloop (the soft-division routine
  // __udivsi3/__modsi3 is the canonical example — it contains its own
  // set_hwloop at level 1, which clobbers the caller's HWLR1 mid-loop), and
  // the call sequence can also modify the loop CSRs.
  //
  // Every call in the loop body disqualifies the loop, with NO
  // `isLoweredToCall` escape hatch. The previous form let through any function
  // for which `isLoweredToCall(F)` returned false, which wrongly included
  // `__modsi3` / `__udivsi3` (these are not in TargetLibraryInfo's known
  // list, yet they ARE emitted as real `jal` calls in the final asm) — so
  // main's mod-sum loop was incorrectly accepted and formed into a hwloop
  // around a `jal __modsi3`, and the callee's own hwloop clobbered the
  // caller's HWLR1. The conservative "any call disqualifies" rule matches
  // what the post-RA recognizer already enforces via `MI.isCall` in
  // `containsInvalidInstruction` and is what ARM/Hexagon ZOL passes do.
  // Any CallInst/InvokeInst is rejected unconditionally, which is strictly
  // stronger than an `isa<InlineAsm>` check on the callee operand (it also
  // rejects normal calls, indirect calls, and inline asm uniformly). Pure
  // intrinsic calls (e.g. llvm.dbg) are not CallInst/InvokeInst and don't
  // reach here.
  // TODO: CallBrInst (asm-goto with outputs) is not matched at the IR level
  // here — it is not a CallInst/InvokeInst. It is caught post-RA via the
  // `MI.isCall` scan in `containsInvalidInstruction`. Add an isa<CallBrInst>
  // arm here for completeness if a future IR shape lets a callbr sneak through.
  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : *BB) {
      if (isa<CallInst>(I) || isa<InvokeInst>(I))
        return false;
      // Also reject loops containing arithmetic that this target
      // expands to a runtime libcall. The IR-level call scan above only
      // catches explicit CallInst/InvokeInst; an `srem`/`udiv`/`sdiv`
      // `urem` is not a CallInst in the IR, but Haydn (baremetal, no
      // hardware divider, soft-float) lowers every integer division and
      // remainder to a `__modsi3`/`__udivsi3`/`__moddi3`/... libcall during
      // GlobalISel legalization — AFTER this TTI hook has already accepted
      // the loop and inserted `llvm.set.loop.iterations`. The resulting
      // `jal __modsi3` lands inside the hardware-loop body, and because the
      // soft-division routine itself lowers to a hwloop at level 1, the
      // callee's `set_hwloop 1` clobbers the caller's HWLR1 → corrupted
      // iteration. Floating-point division/remainder (`fdiv`, `frem`) are
      // likewise always soft-libcall on this target and would do the same.
      // Rejecting these ops here keeps such loops as the original
      // compare-and-branch software loops the IR-level pass would otherwise
      // have deleted. (If a future Haydn model adds a native divider or a
      // call-preserving hwloop level-allocation scheme, this guard can be
      // narrowed.)
      if (I.getOpcode() == Instruction::SDiv ||
          I.getOpcode() == Instruction::UDiv ||
          I.getOpcode() == Instruction::SRem ||
          I.getOpcode() == Instruction::URem ||
          I.getOpcode() == Instruction::FDiv ||
          I.getOpcode() == Instruction::FRem)
        return false;
    }
  }

  // Fill in the HardwareLoopInfo fields. The upstream HardwareLoops pass
  // (llvm/lib/CodeGen/HardwareLoops.cpp) uses these to insert the
  // llvm.set.loop.iterations / llvm.loop.decrement intrinsics.
  LLVMContext &C = L->getHeader()->getContext();
  HWLoopInfo.CountType = Type::getInt32Ty(C);
  HWLoopInfo.LoopDecrement = ConstantInt::get(HWLoopInfo.CountType, 1);
  // Phase C2b / PR6 (AIE-first nesting seed):
  // innermost single-BB → ZOL (CounterInReg=false, LoopStart/PseudoLoopEnd)
  // outer single-BB parent of a ZOL child → JNZD
  // (CounterInReg=true, LoopDec/LoopJNZ) when nesting is legal
  // Dual HWLR (sel=0+1) is post-RA free-list work. IsNestingLegal=true so
  // upstream HardwareLoops may convert a single-BB outer as JNZD; multi-BB
  // still rejected above (ZOL END-label constraint — outer nests are almost
  // always multi-BB and therefore stay software / post-RA).
  if (L->isInnermost()) {
    // Innermost ZOL: keep CounterInReg=false. IsNestingLegal=true so a
    // single-BB parent may still be accepted as JNZD alongside this child.
    HWLoopInfo.IsNestingLegal = true;
    HWLoopInfo.CounterInReg = false;
  } else {
    // Outer (has at least one child). With hasHWLoop already gated above and
    // multi-BB rejected, only a rare single-BB outer parent of a ZOL child
    // reaches here — form JNZD (software-managed counter in a GPR).
    HWLoopInfo.IsNestingLegal = true;
    HWLoopInfo.CounterInReg = true; // LoopDec + LoopJNZ
  }
  // No guarded entry-test form — Haydn's SET_HWLOOP_REG is unguarded (the
  // do-while form; zero-trip loops are handled by the post-RA pass's
  // createPreheaderForLoop guard insertion if needed).
  HWLoopInfo.PerformEntryTest = false;

  LLVM_DEBUG(dbgs() << "Haydn HWLoop(IR): accepted "
                    << (L->isInnermost() ? "innermost ZOL" : "outer JNZD")
                    << " candidate\n");
  return true;
}
