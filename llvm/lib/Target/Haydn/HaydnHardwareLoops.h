//===-- HaydnHardwareLoops.h - Haydn Hardware Loop Detection ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Interface for the Haydn post-RA hardware loop pass.
//
// Dual roles (see HaydnHardwareLoops.cpp + HaydnHWLoopContracts.h) — AIE split:
// Role A — IR ZOL already formed (LoopStart/PseudoLoopEnd or LoopDec
// LoopJNZ): expand LoopStart → SET_HWLOOP_REG pre-sched (AIE
// order); skip Role B convert; FixupHwLoops pads/demotes.
// Authority: IR HardwareLoops + HaydnTTI (single-BB like AIE).
// Role B — residual convert for multi-BB / IR miss (-haydn-hwloop-role-b
// default OFF = AIE-like expand-only;). Structure contract: 
// single latch, single true exit, no early break.
// Layout-owned setup: after SET only InterveningCycles deficit pads
// (SetupIssueDistance=3, Following>=2); useful work stays before SET. Dual
// HWLR nesting; body parcels BEGIN..END inclusive >= MinBodyBundles (3)
// measured on final product EncodedBytes parcels (not SMS II proxy);
// strict END > BEGIN (END = last body cycle). Gates: FeatureHWLoop,
// -haydn-enable-hwloops (product default OFF until FE96 post-link residual
// closes atomically; Role B stays off).
//
// Hardware loop semantics (from ISA spec):
// SET_HWLOOP sel, offset1, offset2, cnt
// HWLR_BEGIN[sel] = PC + (offset1 << 2)
// HWLR_END[sel] = PC + (offset2 << 2)
// HWLR_COUNT[sel] = cnt
//
// The hardware automatically decrements HWLR_COUNT and branches back to
// HWLR_BEGIN when COUNT > 0, providing zero-overhead loop control.
//
// 2-level nesting: sel=0 for outer loop, sel=1 for inner loop.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNHARDWARELOOPS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNHARDWARELOOPS_H

#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"

namespace llvm {

class HaydnHardwareLoops : public MachineFunctionPass {
public:
  static char ID;

  HaydnHardwareLoops();

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "Haydn Hardware Loop Detection";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  // Attempt to convert a single loop to a hardware loop, processing nested
  // sub-loops recursively (inside-out). \p SelUsed tracks which HWLOOP
  // selector levels (0 or 1) are already consumed by inner loops.
  // Returns true if the loop was successfully converted.
  bool convertToHardwareLoop(MachineLoop *L, MachineFunction &MF,
                             bool &Sel0Used, bool &Sel1Used);

  // Check if the loop contains any instructions that prevent hardware loop
  // conversion (calls, indirect branches, etc.).
  // When \p AllowChildHwloop is true (PR7 dual nesting), SET_HWLOOP
  // LoopStart forms belonging to an already-converted child are tolerated
  // so an outer loop may still convert onto the free HWLR sel.
  bool containsInvalidInstruction(const MachineLoop *L,
                                  bool AllowChildHwloop = false) const;

  // Role A: true if THIS loop's preheader/latch already carries IR-form
  // hardware-loop pseudos — LoopStart, PseudoLoopEnd, LoopDec, LoopJNZ
  // or expanded SET_HWLOOP{,_REG}. Does not scan nested-child blocks.
  // Convert must skip Role A loops (expand runs first in this pass).
  bool hasIRZOLForm(const MachineLoop *L) const;

  // Kind of preheader trip-count computation the caller must emit when the
  // trip count is a runtime value derived from IV/limit registers.
  // \sa findTripCount
  enum class TripComputeKind {
    // No preheader arithmetic: TripCountReg already holds the trip count
    // (Cases 1 and 2 — the bound register IS the trip count).
    None,
    // Pointer-IV Case 3: emit `SUB32 LimitReg, LimitReg, IVReg` then
    // `SRLI32 LimitReg, LimitReg, shift`. trip = (limit - iv) >> shift.
    PointerIV,
    // Scalar count-up Case 4: emit `ADDI32 Tmp, R0, -init` (if init != 0)
    // `ADD32 LimitReg, LimitReg, Tmp`, then `SRLI32 LimitReg, LimitReg, shift`
    // (if shift > 0). trip = (limit - init) >> log2(bump). init is the
    // compile-time IV initial value; shift is log2(IVBump).
    ScalarCountUp,
    // Scalar count-down EQ Case 5: emit `SUB32 LimitReg, LimitReg, IVInitReg`.
    // trip = iv_init - limit (both runtime registers, step = -1).
    ScalarCountDown,
  };

  // Check if the loop has a countable trip count that can be expressed
  // as an immediate or register value.
  // Sets \p TripCount to the immediate trip count if known at compile time.
  // Sets \p TripCountReg to the register holding the trip count if not
  // immediate. Returns true if a countable trip count was found.
  // When the trip count is a runtime value computed from IV/limit registers
  // \p ComputeKind indicates which preheader arithmetic the caller emits, and
  // \p TripShift carries log2(IVBump) for the SRLI32 (PointerIV / ScalarCountUp).
  // \p IVRegOut / \p LimitRegOut carry the identified IV and limit registers
  // (LimitReg is also the clobber target for the trip computation and becomes
  // TripCountReg; IVReg/IVInitReg is read once). See (Case 3, GAP-3) and
  // (Cases 4 and 5 — runtime-limit count-up / count-down broadening).
  bool findTripCount(MachineLoop *L, int64_t &TripCount,
                     Register &TripCountReg, TripComputeKind &ComputeKind,
                     unsigned &TripShift, int64_t &ScalarTripInit,
                     Register &IVRegOut, Register &LimitRegOut,
                     MachineInstr *&CmpMIOut);

  // Check if the loop body is a single basic block (required for initial
  // implementation).
  bool isSingleBBLoop(const MachineLoop *L) const;

  // Validate that a multi-BB loop has a structure compatible with hardware
  // loop conversion: single latch, single exit, and all internal branches
  // stay within the loop body.
  bool hasValidMultiBBStructure(const MachineLoop *L) const;

  // Estimate the loop body size in bytes and verify it fits the HWLOOP
  // PC-relative offset field (16-bit signed, shifted left 2 for word
  // alignment → ±128KB range). Returns true if the loop body fits within
  // the encodable offset range, false if the loop must fall back to a
  // compare-and-branch loop (handled by normal branch relaxation).
  // The estimate is conservative: it sums \c getInstSizeInBytes over all
  // non-pseudo instructions in the loop body. Pseudo instructions are
  // skipped since they are either no-ops at emission (e.g. ADJCALLSTACKDOWN)
  // or expand to real instructions counted at their expansion point. The
  // estimate is taken before VLIW packetization, so the actual post-pack
  // size is smaller (multiple ops per bundle).
  bool loopBodyFitsRange(const MachineLoop *L) const;

  // Create a dedicated preheader block for a guarded loop where the entry
  // block has multiple successors (e.g. a runtime trip-count guard branch).
  // Splits the non-backedge predecessor→header edge by inserting a new
  // empty MBB, redirecting the predecessor's branch to it, and adding an
  // unconditional fallthrough to the header. Returns the new preheader, or
  // nullptr if the structure is not splittable. Post-RA safe (no PHIs).
  MachineBasicBlock *createPreheaderForLoop(MachineLoop *L);

  // Cached MachineDominatorTree for the current function. Used by
  // findImmediateDefOnDomChain to walk the dominator chain from the loop
  // preheader up to the entry block, resolving loop-invariant constants
  // materialized once in a dominating block (e.g. the function entry).
  // Required because the single-predecessor walker in findImmediateDefChain
  // breaks at loop headers with multiple predecessors, missing the entry
  // block. See (root cause) and (this fix).
  MachineDominatorTree *MDT = nullptr;

  // Cached MachineLoopInfo for the current function. Used by
  // findImmediateDefOnDomChainScoped to skip sibling-loop blocks when
  // resolving loop-invariant constants, so that a physreg redefined with
  // different values in sibling-loop preheaders does not trigger a false
 // "conflicting constants" rejection. See ( §0.3 / §4 #2-#3).
  MachineLoopInfo *MLI = nullptr;
};

// Pass creation function.
FunctionPass *createHaydnHardwareLoopsPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNHARDWARELOOPS_H
