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
/// The Haydn ilp32-ish baremetal ABI is described by `HaydnCallingConv.td` and
/// is correctly modelled by `DefaultABIInfo` (which delegates to that table).
/// Key facts:
///   * GPR32 arguments: R1-R7 (R0 is hard-reserved as soft-zero).
///   * DR64 arguments (i64/f64/SIMD): D0-D3.
///   * Scalar i32 returns go in R1-R2; i64/SIMD returns go in D0-D3.
///   * Stack alignment is 8 bytes (CLAUDE.md hard constraint #4).
///
/// F16 (consolidated-fix-list §2): the previous version of this class declared
/// non-virtual `classifyReturnType`/`classifyArgumentType` that shadowed
/// `DefaultABIInfo`'s virtual methods. Because `computeInfo` dispatched to the
/// base class, the custom classifiers were dead. They are removed here; the
/// real classification path stays `DefaultABIInfo` (driven by the .td), which
/// matches the spec.
class HaydnABIInfo : public DefaultABIInfo {
public:
  HaydnABIInfo(CodeGen::CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  void computeInfo(CGFunctionInfo &FI) const override {
    // DefaultABIInfo routes through HaydnCallingConv.td (R1-R7 / D0-D3).
    DefaultABIInfo::computeInfo(FI);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override {
    // Haydn uses void* for va_list (VoidPtrBuiltinVaList).
    return CGF.EmitLoadOfAnyValue(
        CGF.MakeAddrLValue(
            EmitVAArgInstr(CGF, VAListAddr, Ty, classifyArgumentType(Ty)), Ty),
        Slot);
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
