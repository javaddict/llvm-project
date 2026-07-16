//===-- HaydnISelLowering.h - Haydn DAG Lowering Interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Haydn uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNISELLOWERING_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNISELLOWERING_H

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class HaydnSubtarget;

class HaydnTargetLowering : public TargetLowering {
  const HaydnSubtarget &Subtarget;

public:
  explicit HaydnTargetLowering(const TargetMachine &TM,
                              const HaydnSubtarget &STI);

  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

  MVT getScalarShiftAmountTy(const DataLayout &DL, EVT VT) const override;

  // Jump tables are enabled for dense switches (>= 4 cases).
  // G_BRJT is selected to SHL32 + ADD32 + LD32 + BR_JT sequence.
  // BR_JT is expanded by the AsmPrinter to JALR.
  unsigned getMinimumJumpTableEntries() const override;
  bool areJTsAllowed(const Function *Fn) const override;

  // Defer atomic load/store/RMW expansion to the default IR libcall lowering
  // (__atomic_load_*, __atomic_store_*, __atomic_*_fetch_*). Haydn has no
  // hardware atomics; per CLAUDE.md atomics are libcalls, not silently
  // demoted to non-atomic. See -atomics-libcall-expansion.
  AtomicExpansionKind shouldExpandAtomicLoadInIR(LoadInst *LI) const override;
  AtomicExpansionKind shouldExpandAtomicStoreInIR(StoreInst *SI) const override;
  AtomicExpansionKind
  shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const override;

  // Map GCC-style inline asm constraints to Haydn register classes.
  // Required by GlobalISel InlineAsmLowering for `"r"(ptr)` barriers
  // (e.g. libc memset_explicit); without this, IRTranslator fatals on call.
  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNISELLOWERING_H
