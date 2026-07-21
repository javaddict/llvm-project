//===-- HaydnISelLowering.cpp - Haydn DAG Lowering Implementation ----------===//
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

#include "HaydnISelLowering.h"
#include "HaydnSubtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/RuntimeLibcalls.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-isel-lowering"

HaydnTargetLowering::HaydnTargetLowering(const TargetMachine &TM,
                                        const HaydnSubtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  // Set up the register classes
  addRegisterClass(MVT::i32, &Haydn::GPR32RegClass);
  addRegisterClass(MVT::i64, &Haydn::DR64RegClass);

  // Compute register properties
  computeRegisterProperties(STI.getRegisterInfo());

  // Soft-float FP math libcalls — name the rounding + min/max family..
  // For a baremetal/UnknownOS triple, TargetLoweringBase::initLibcalls leaves
  // these libcall impls unset (assumes no libm), so the GISel generic libcall
  // path aborts with "unable to legalize" the moment one is needed
  // (createLibcall -> getLibcallName -> null -> UnableToLegalize). The
  // transcendental/sqrt/fma/fmod libcalls keep their default impls; only this
  // rounding/min/max family is unset. Binding their standard libm impl converts
  // a compile-time CRASH into a normal libcall (which, with no libm linked, is
  // a clean "undefined floorf" link error — the baremetal convention, per
  // codex-D). copysign is NOT here: it has no case in
  // LegalizerHelper::libcall and is instead custom bit-trick-lowered in
  // HaydnLegalizerInfo (no runtime symbol).
  setLibcallImpl(RTLIB::FLOOR_F32, RTLIB::impl_floorf);
  setLibcallImpl(RTLIB::FLOOR_F64, RTLIB::impl_floor);
  setLibcallImpl(RTLIB::CEIL_F32, RTLIB::impl_ceilf);
  setLibcallImpl(RTLIB::CEIL_F64, RTLIB::impl_ceil);
  setLibcallImpl(RTLIB::RINT_F32, RTLIB::impl_rintf);
  setLibcallImpl(RTLIB::RINT_F64, RTLIB::impl_rint);
  setLibcallImpl(RTLIB::NEARBYINT_F32, RTLIB::impl_nearbyintf);
  setLibcallImpl(RTLIB::NEARBYINT_F64, RTLIB::impl_nearbyint);
  setLibcallImpl(RTLIB::FMIN_F32, RTLIB::impl_fminf);
  setLibcallImpl(RTLIB::FMIN_F64, RTLIB::impl_fmin);
  setLibcallImpl(RTLIB::FMAX_F32, RTLIB::impl_fmaxf);
  setLibcallImpl(RTLIB::FMAX_F64, RTLIB::impl_fmax);

  // Haydn has no native atomic instructions. CLAUDE.md mandates
  // "atomics as libcalls": atomic ops lower to __atomic_* runtime calls
  // (compiler-rt/libgcc). A max atomic size of 0 tells AtomicExpandPass that
  // NO size is supported, so every atomic load/store/RMW/cmpxchg is rewritten
  // to a __atomic_* libcall at IR level (before GISel), via
  // expandAtomic{Load,Store,RMW}ToLibcall. With MaxAtomicSizeInBitsSupported
  // > 0, atomicSizeSupported returns true and the pass skips the libcall
  // path entirely, leaving atomics for GISel to fail on ("unable to legalize
  // G_ATOMICRMW_*"). The shouldExpandAtomic*InIR overrides below then never
  // run. See atomics-libcall-expansion,.
  setMaxAtomicSizeInBitsSupported(0);

  // Enable jump tables for dense switches. The GISel pipeline handles
  // G_JUMP_TABLE (materialize base) and G_BRJT (indirect branch) via
  // the instruction selector. BR_JT is expanded to JALR by AsmPrinter.
  setMinimumJumpTableEntries(4);
}

EVT HaydnTargetLowering::getSetCCResultType(const DataLayout &DL,
                                            LLVMContext &Context,
                                            EVT VT) const {
  return MVT::i32;
}

MVT HaydnTargetLowering::getScalarShiftAmountTy(const DataLayout &DL,
                                                EVT VT) const {
  return MVT::i32;
}

unsigned HaydnTargetLowering::getMinimumJumpTableEntries() const {
  // Use jump tables for switches with 4+ dense cases.
  return 4;
}

bool HaydnTargetLowering::areJTsAllowed(const Function *Fn) const {
  // Jump tables are enabled. The GISel selector handles G_BRJT → BR_JT
  // and AsmPrinter expands BR_JT to JALR for the indirect branch.
  return true;
}

TargetLowering::AtomicExpansionKind
HaydnTargetLowering::shouldExpandAtomicLoadInIR(LoadInst *LI) const {
  // With setMaxAtomicSizeInBitsSupported(0), atomicSizeSupported returns
  // false for every size, so AtomicExpandPass routes loads through
  // expandAtomicLoadToLibcall BEFORE consulting this hook. The hook therefore
  // never runs in practice — it's kept only as a defensive belt-and-suspenders
  // for any future code path that reaches it. None = "no preference".
  return AtomicExpansionKind::None;
}

TargetLowering::AtomicExpansionKind
HaydnTargetLowering::shouldExpandAtomicStoreInIR(StoreInst *SI) const {
  // See shouldExpandAtomicLoadInIR — never reached; kept defensively.
  return AtomicExpansionKind::None;
}

TargetLowering::AtomicExpansionKind
HaydnTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const {
  // See shouldExpandAtomicLoadInIR — never reached; kept defensively.
  return AtomicExpansionKind::None;
}

std::pair<unsigned, const TargetRegisterClass *>
HaydnTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                  StringRef Constraint,
                                                  MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      // General-purpose register. Haydn GPRs are 32-bit; pointers are i32
      // (datalayout p:32:32). i64 values live in DR64.
      if (VT == MVT::i64 || VT == MVT::f64)
        return std::make_pair(0U, &Haydn::DR64RegClass);
      // i1/i8/i16/i32/f32 and any other ≤32-bit integer (incl. pointer VT).
      if (VT == MVT::i32 || VT == MVT::i16 || VT == MVT::i8 || VT == MVT::i1 ||
          VT == MVT::f32 || (VT.isInteger() && VT.getSizeInBits() <= 32))
        return std::make_pair(0U, &Haydn::GPR32RegClass);
      break;
    default:
      break;
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}
