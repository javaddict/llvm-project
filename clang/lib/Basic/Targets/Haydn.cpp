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
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringSwitch.h"

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
  // data layout via i64:32-f64:32.
  PointerWidth = PointerAlign = 32;
  IntWidth = IntAlign = 32;
  LongWidth = LongAlign = 32;
  LongLongWidth = 64;
  LongLongAlign = 32;
  FloatWidth = 32;
  FloatAlign = 32;
  DoubleWidth = 64;
  DoubleAlign = 32; // double is 64-bit but aligned to 32-bit boundary
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

  // A.4 feature gates (toolchain-architecture-residual Wave A):
  // Narrow baremetal profile — fail closed on unsupported language/runtime.
  TLSSupported = false;
  // Cap _BitInt; wide multi-limb BitInt is not a product path.
  MaxBitIntWidth = 64;

  // Default ABI
  setABI("ilp32");

  // Match LLVM HaydnSubtarget: empty CPU → "generic".
  if (Opts.CPU.empty())
    CPU = "generic";
  else
    CPU = Opts.CPU;
}

bool HaydnTargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::StringSwitch<bool>(Name)
      .Case("generic", true)
      .Case("haydn", true)
      .Default(false);
}

void HaydnTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  Values.append({"generic", "haydn"});
}

bool HaydnTargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPUName,
    const std::vector<std::string> &FeaturesVec) const {
  // Mirror HaydnGeneric.td ProcessorModel feature lists.
  // Empty CPU → generic (same as HaydnSubtarget::initializeSubtargetDependencies).
  StringRef CPURef = CPUName.empty() ? "generic" : CPUName;

  // Clear ISA feature keys so a later re-init (target attribute) does not
  // accumulate stale positives before CPU defaults are applied.
  Features["agu"] = false;
  Features["circular-buffer"] = false;
  Features["bit-reversed"] = false;
  Features["hwloop"] = false;
  Features["simd"] = false;

  if (CPURef == "generic") {
    // Product baseline densify path: post/pre-inc fuse + hwloop.
    Features["agu"] = true;
    Features["hwloop"] = true;
  } else if (CPURef == "haydn") {
    Features["agu"] = true;
    Features["circular-buffer"] = true;
    Features["bit-reversed"] = true;
    Features["hwloop"] = true;
    Features["simd"] = true;
  }
  // Unknown CPU names are rejected by setCPU/isValidCPUName; leave ISA
  // features false here so +feature overrides still apply cleanly.

  return TargetInfo::initFeatureMap(Features, Diags, CPUName, FeaturesVec);
}

bool HaydnTargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                           DiagnosticsEngine &Diags) {
  HasAGU = HasCircularBuffer = HasBitReversed = HasHWLoop = HasSIMD = false;

  for (const auto &F : Features) {
    if (F == "+agu")
      HasAGU = true;
    else if (F == "-agu")
      HasAGU = false;
    else if (F == "+circular-buffer")
      HasCircularBuffer = true;
    else if (F == "-circular-buffer")
      HasCircularBuffer = false;
    else if (F == "+bit-reversed")
      HasBitReversed = true;
    else if (F == "-bit-reversed")
      HasBitReversed = false;
    else if (F == "+hwloop")
      HasHWLoop = true;
    else if (F == "-hwloop")
      HasHWLoop = false;
    else if (F == "+simd")
      HasSIMD = true;
    else if (F == "-simd")
      HasSIMD = false;
  }
  return true;
}

bool HaydnTargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("haydn", true)
      .Case("agu", HasAGU)
      .Case("circular-buffer", HasCircularBuffer)
      .Case("bit-reversed", HasBitReversed)
      .Case("hwloop", HasHWLoop)
      .Case("simd", HasSIMD)
      .Default(false);
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

  // ISA family revision (Hexagon peer: __HEXAGON_ARCH__). Current product
  // generation is 1 for both -mcpu=generic and -mcpu=haydn. Differentiate
  // semantic availability with capability macros below — never with bundle
  // FormatID / slot / AltDesc macros.
  Builder.defineMacro("__HAYDN_ARCH__", "1");

  // Per-CPU convenience macros (optional compile-time selection).
  if (CPU == "haydn")
    Builder.defineMacro("__HAYDN_CPU_HAYDN__");
  else
    // Empty / generic / unknown → baseline densify path (matches ctor).
    Builder.defineMacro("__HAYDN_CPU_GENERIC__");

  // Semantic ISA capability macros. Names track HaydnFeatures.td /
  // BuiltinsHaydn Features= strings only.
  if (HasAGU)
    Builder.defineMacro("__HAYDN_FEATURE_AGU__");
  if (HasCircularBuffer)
    Builder.defineMacro("__HAYDN_FEATURE_CIRCULAR_BUFFER__");
  if (HasBitReversed)
    Builder.defineMacro("__HAYDN_FEATURE_BIT_REVERSED__");
  if (HasHWLoop)
    Builder.defineMacro("__HAYDN_FEATURE_HWLOOP__");
  if (HasSIMD)
    Builder.defineMacro("__HAYDN_FEATURE_SIMD__");
}

ArrayRef<const char *> HaydnTargetInfo::getGCCRegNames() const {
  static const char *const GCCRegNames[] = {
      // GPR32: R0-R15
      "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
      "r8",  "r9",  "r10", "r11", "r12", "sp",  "fp",  "lr",
      // DR64: D0-D15
      "d0",  "d1",  "d2",  "d3",  "d4",  "d5",  "d6",  "d7",
      "d8",  "d9",  "d10", "d11", "d12", "d13", "d14", "d15",
      // AR: AR0–AR3 (4×64-bit AGU file; full 2-bit ar_sel domain, CB-149)
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
  // Basic register constraints. 'd' (DR64) rejected until backend implements
  // a real DR constraint path (A.4 — was silently accepted with weak lower).
  switch (*Name) {
  case 'r': // General purpose register (GPR)
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
