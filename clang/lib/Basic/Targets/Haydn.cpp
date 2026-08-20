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
#include "llvm/BinaryFormat/ELF.h"

using namespace clang;
using namespace clang::targets;

// Object identity. Peer: AIE ELF.h:498-502 (public EF_AIE_*) +
// AIEELFObjectWriter.cpp:49-51 (one e_machine). Official ELF 259 is
// Kalray KVX. Stay on EM_HAYDN; distinguisher is EF_HAYDN_E96. Do not
// invent a replacement e_machine, extra e_flags image-version bits, or a
// preprocessor macro that treats the provisional flag as qualification.
static_assert(llvm::ELF::EM_HAYDN == 259,
              "EM_HAYDN stays 259; do not invent a replacement (KVX collision)");
static_assert(llvm::ELF::EF_HAYDN_E96 == 0x1u,
              "EF_HAYDN_E96 stays 0x1; do not invent e_flags image-versioning");

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
  // Soft-float product: _Float16 / bf16 stay Sema-unavailable. RISC-V
  // TargetInfo sets HasFloat16 when F/D is present (RISCV.h:50); Haydn has
  // no FPU feature, so the default false is restated as a contract.
  HasFloat16 = false;
  HasBFloat16 = false;
  HalfArgsAndReturns = false;

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

  // Soft-float product ABI (no FPU). ARM peer: __SOFTFP__ (ARM.cpp:841).
  // gcc-torture float/double remains a classified skip; this is frontend
  // identity of that leftover, not IEEE vector QUALIFY.
  Builder.defineMacro("__SOFTFP__");
  Builder.defineMacro("__HAYDN_SOFT_FLOAT__");

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
  // SIMD is opt-in on -mcpu=haydn. Do not define this on generic
  // (agu+hwloop) and do not silently rewrite the driver CPU.
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
  // AIE validateAsmConstraint returns false for every letter
  // (llvm-aie clang/lib/Basic/Targets/AIE.h:134). Haydn cannot follow
  // that: the two-bank CC has a typed DR64 file. Peer is RISCV 'f'
  // (RISCV.cpp:91) — one letter, setAllowsRegister, no memory/imm.
  switch (*Name) {
  case 'r':
  case 'd':
    Info.setAllowsRegister();
    return true;
  default:
    return false;
  }
}

static bool validateHaydnAsmOperandSize(StringRef Constraint, unsigned Size) {
  // X86.cpp:1730 strips "=+&" then checks the letter. Haydn also
  // strips '%' (early-clobber twin). 'r' is GPR32; i64/f64 must use 'd'.
  Constraint = Constraint.ltrim("=+&%");
  if (Constraint.empty())
    return true;
  switch (Constraint[0]) {
  case 'r':
    return Size <= 32;
  case 'd':
    return Size == 64;
  default:
    return true;
  }
}

bool HaydnTargetInfo::validateOutputSize(const llvm::StringMap<bool> &FeatureMap,
                                         StringRef Constraint,
                                         unsigned Size) const {
  (void)FeatureMap;
  return validateHaydnAsmOperandSize(Constraint, Size);
}

bool HaydnTargetInfo::validateInputSize(const llvm::StringMap<bool> &FeatureMap,
                                        StringRef Constraint,
                                        unsigned Size) const {
  (void)FeatureMap;
  return validateHaydnAsmOperandSize(Constraint, Size);
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
