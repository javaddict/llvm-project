//===--- Haydn.cpp - Implement Haydn target feature support ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Haydn TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"
#include "clang/Basic/TargetBuiltins.h"

using namespace clang;
using namespace clang::targets;

HaydnTargetInfo::HaydnTargetInfo(const llvm::Triple &Triple,
                                 const TargetOptions &Opts)
    : TargetInfo(Triple) {
  // Data layout: e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64
  // Little-endian, 32-bit pointers, i64/f64 aligned to 32 bits
  resetDataLayout("e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64");

  // Type widths and alignments (32-bit DSP, ILP32 ABI).
  // The declared ABI "ilp32" mandates sizeof(int)==sizeof(long)==sizeof(ptr)==4.
  // long long stays 64-bit so 64-bit accumulator arithmetic is still available.
  // DR64 accumulator values (i64/f64) are aligned to 32 bits, encoded in the
  // data layout via i64:32-f64:32. See D102-ilp32-long-width.md.
  PointerWidth = PointerAlign = 32;
  IntWidth = IntAlign = 32;
  LongWidth = LongAlign = 32;
  LongLongWidth = 64;
  LongLongAlign = 32;
  FloatWidth = 32;
  FloatAlign = 32;
  DoubleWidth = 64;
  DoubleAlign = 32;  // double is 64-bit but aligned to 32-bit boundary
  LongDoubleWidth = 64;
  LongDoubleAlign = 32;

  // Type sizes
  SizeType = UnsignedInt;
  PtrDiffType = SignedInt;
  IntPtrType = SignedInt;
  WCharType = SignedInt;
  WIntType = UnsignedInt;

  // No default alignment padding
  UseZeroLengthBitfieldAlignment = true;

  // Advertise lock-free atomics up to 32 bits (i32 / pointer). Haydn has no
  // native atomics; the backend still expands IR atomics to __atomic_*
  // libcalls (MaxAtomicSizeInBitsSupported=0) implemented as plain loads/
  // stores in the single-core baremetal runtime. Reporting 0 here makes
  // Clang diagnose every atomic as oversized (-Watomic-alignment) and
  // blocks freestanding llvm-libc rand/srand (cpp::Atomic<unsigned long>).
  MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 32;

  // Default ABI
  setABI("ilp32");
}

void HaydnTargetInfo::getTargetDefines(const LangOptions &Opts,
                                       MacroBuilder &Builder) const {
  // Target identification
  Builder.defineMacro("__haydn__");
  Builder.defineMacro("__HAYDN__");

  // Baremetal
  Builder.defineMacro("__ELF__");

  // Endianness
  Builder.defineMacro("__haydn_LE__");
  Builder.defineMacro("__HAYDN_LE__");

  // 32-bit target
  Builder.defineMacro("__haydn_32__");
  Builder.defineMacro("__HAYDN_32__");
}

ArrayRef<const char *> HaydnTargetInfo::getGCCRegNames() const {
  static const char *const GCCRegNames[] = {
      // GPR32: R0-R15
      "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
      "r8",  "r9",  "r10", "r11", "r12", "sp",  "fp",  "lr",
      // DR64: D0-D15
      "d0",  "d1",  "d2",  "d3",  "d4",  "d5",  "d6",  "d7",
      "d8",  "d9",  "d10", "d11", "d12", "d13", "d14", "d15",
      // AR: AR0-AR3
      "ar0", "ar1", "ar2", "ar3",
  };
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::GCCRegAlias> HaydnTargetInfo::getGCCRegAliases() const {
  static const TargetInfo::GCCRegAlias GCCRegAliases[] = {
      {{"r13"}, "sp"}, {{"r14"}, "fp"}, {{"r15"}, "lr"},
  };
  return llvm::ArrayRef(GCCRegAliases);
}

bool HaydnTargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  // Basic register constraints
  switch (*Name) {
  case 'r': // General purpose register (GPR)
  case 'd': // Data register (DR64)
    Info.setAllowsRegister();
    return true;
  default:
    return false;
  }
}

static constexpr unsigned NumBuiltins =
    clang::Haydn::LastTSBuiltin - clang::Builtin::FirstTSBuiltin;

#define GET_BUILTIN_STR_TABLE
#include "clang/Basic/BuiltinsHaydn.inc"
#undef GET_BUILTIN_STR_TABLE

static constexpr Builtin::Info BuiltinInfos[] = {
#define GET_BUILTIN_INFOS
#include "clang/Basic/BuiltinsHaydn.inc"
#undef GET_BUILTIN_INFOS
};
static_assert(std::size(BuiltinInfos) == NumBuiltins);

llvm::SmallVector<Builtin::InfosShard>
HaydnTargetInfo::getTargetBuiltins() const {
  return {{&BuiltinStrings, BuiltinInfos}};
}
