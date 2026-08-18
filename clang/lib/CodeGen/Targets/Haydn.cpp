//===- Haydn.cpp - Implement Haydn target support for Clang --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Haydn TargetInfo and CodeGenInfo for Clang.
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// Haydn ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

/// Haydn calling-convention classification.
///
/// The Haydn ilp32-ish baremetal ABI is described by `HaydnCallingConv.td`.
/// Key facts:
///   * GPR32 arguments: R1-R7 (R0 is hard-reserved as soft-zero).
///   * DR64 arguments (i64/f64/SIMD): D0-D3.
///   * Scalar i32 / pointer / f32 returns go in R1. i64 / f64 / 64-bit
///     SIMD returns go in D0 only (R2 is IR-pair only; no D1 return bank).
///   * Product contract: stack alignment is 8 bytes.
///   * Aggregates are passed Indirect as a plain pointer (no byval).
///
/// Why no byval for aggregates:
///   `HaydnCallingConv.td` has no `CCIfByVal`/`CCPassByVal`. Stack slots are
///   sized for a pointer (`CCAssignToStack<8,8>`). EmitVAArg (and G_VAARG of
///   pointer type) loads that pointer from the GPR bank / stack. Emitting
///   `byval` makes GlobalISel CallLowering memcpy aggregate *contents* into
///   those 8-byte slots, so multi-byval varargs (va-arg-22) overlap and the
///   callee reads garbage pointers → ABORT.
///
/// F16: DefaultABIInfo::classifyArgumentType is non-virtual; classification
/// must live in computeInfo / EmitVAArg (not a shadowed free method).
class HaydnABIInfo : public DefaultABIInfo {
public:
  HaydnABIInfo(CodeGen::CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  /// 64-bit SIMD vectors (v2i32 / v4i16 / v8i8 / v2f32) map to one DR64 —
  /// Direct, never sret/Indirect (HaydnCallingConv.td).
  bool isHaydnDR64Vector(QualType Ty) const {
    if (!Ty->isVectorType())
      return false;
    return getContext().getTypeSize(Ty) == 64;
  }

  /// Residual 32-bit SLP packs (v2i16 / v4i8) live in one GPR32 — Direct.
  bool isHaydnGPR32Vector(QualType Ty) const {
    if (!Ty->isVectorType())
      return false;
    return getContext().getTypeSize(Ty) == 32;
  }

  ABIArgInfo classifyIndirectNoByVal(QualType Ty) const {
    return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                   /*ByVal=*/false);
  }

  /// Classify an argument. Aggregates → Indirect, ByVal=false (pointer in
  /// R1–R7 / stack). 64b SIMD → Direct (DR); 32b residual SIMD → Direct
  /// (GPR). Overaligned scalars stay Direct: bits travel in a register or
  /// an 8-byte CC slot and are copied into an aligned local (AArch64
  /// EmitAAPCSVAArg copies, it does not invent a second CC). Aggregates
  /// with align > 8 keep that align on the Indirect object; FrameLowering
  /// realigns static MaxAlign (RISCVFrameLowering.cpp:1142-1153).
  ABIArgInfo classifyHaydnArgumentType(QualType Ty) const {
    Ty = useFirstFieldIfTransparentUnion(Ty);

    if (isHaydnDR64Vector(Ty) || isHaydnGPR32Vector(Ty))
      return ABIArgInfo::getDirect();

    if (isAggregateTypeForABI(Ty)) {
      // C++ non-trivial records: match DefaultABIInfo ByVal policy for the
      // DirectInMemory case; otherwise plain Indirect (no byval).
      if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
        return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                       RAA == CGCXXABI::RAA_DirectInMemory);
      return classifyIndirectNoByVal(Ty);
    }

    return classifyArgumentType(Ty);
  }

  /// Classify a return value.
  ///
  ///   * 64-bit SIMD (v2i32 / v4i16 / v2f32) → Direct in D0. Never sret;
  ///     never multi-GPR. Matches RetCC_Haydn / HaydnCallingConv.td.
  ///   * Aggregates (any size that isAggregateTypeForABI) → hidden sret
  ///     pointer in R1 (Indirect). Haydn has no multi-GPR aggregate return
  ///     bank beyond R1–R2 for scalar i32; large/struct returns always use
  ///     caller-allocated memory via sret (DefaultABIInfo policy).
  ///   * Scalars follow DefaultABIInfo width (i32 in GPR / R1; i64/f64 in D0).
  ABIArgInfo classifyHaydnReturnType(QualType RetTy) const {
    if (isHaydnDR64Vector(RetTy) || isHaydnGPR32Vector(RetTy))
      return ABIArgInfo::getDirect();

    if (isAggregateTypeForABI(RetTy))
      return getNaturalAlignIndirect(RetTy,
                                     getDataLayout().getAllocaAddrSpace());

    return classifyReturnType(RetTy);
  }

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyHaydnReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyHaydnArgumentType(I.type);
  }
  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override {
    // Structured AArch64-style va_list is handled in the backend (VASTART /
    // G_VAARG). Classify first so aggregates and overaligned values become
    // Indirect: EmitVAArgInstr then emits `va_arg` of pointer type (P0),
    // which the legalizer walks on the GPR cursor. Direct i32/i64 stay
    // G_VAARG of the value type (GPR / DR cursor). Never emit byval —
    // contents in 8-byte pointer slots overlap (va-arg-22).
    const ABIArgInfo AI = classifyHaydnArgumentType(Ty);
    return CGF.EmitLoadOfAnyValue(
        CGF.MakeAddrLValue(EmitVAArgInstr(CGF, VAListAddr, Ty, AI), Ty), Slot);
  }
};

class HaydnTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  HaydnTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<HaydnABIInfo>(CGT)) {}

  // No special target attributes needed for basic Haydn support
  // Future: may add DSP interrupt/attribute handling here
};

} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createHaydnTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<HaydnTargetCodeGenInfo>(CGM.getTypes());
}
