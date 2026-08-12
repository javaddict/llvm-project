//===-- HaydnIntrinEmitter.cpp - Generate Haydn headers and .inc ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TableGen backends driven by BuiltinsHaydn.td:
//
//   -gen-haydn-intrin-header      → haydn.h
//   -gen-haydn-builtin-codegen    → haydn_builtin_cg.inc
//   -gen-haydn-builtin-sema       → haydn_builtin_sema.inc
//   -gen-haydn-op-manifest        → haydn_op_manifest.inc (public-op contract)
//   -gen-haydn-op-closure-probe   → exhaustive PublicEnabled C probe
//   -gen-haydn-op-imm-audit       → exhaustive Imm non-ICE + range-neg Sema
//   -gen-haydn-op-feature-audit   → exhaustive Features-gate Sema audit
//
// BuiltinsHaydn.inc is still produced by -gen-clang-builtins (shared TD).
//
// PublicEnabled (HaydnPublicAPI): haydn.h emits only PublicEnabled=1 ops.
// HaydnAeBuiltin defaults PublicEnabled=0. Publish checks are fail-closed.
// Manifest columns cover effect, features, arity, ImmChecks, prototype so CI
// can audit every public op without FormatID/slot/AltDesc leakage.
// Exhaustive probe calls every public haydn_* with type dummies + ImmArg ICE
// (OpenCL exhaustive-test peer) so C → Sema → IR → ISel → object is proven.
// Imm audit emits -verify non-ICE + out-of-range calls on __builtin_haydn_*
// (Sema ImmCheck home; not UA public switch wrappers).
// Feature audit emits multi-verify generic/full/noagu on every PublicEnabled
// op with non-empty Features (err_builtin_needs_feature; Hexagon peer =
// checkTargetFeatures).
//
//===----------------------------------------------------------------------===//

#include "TableGenBackends.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

enum class Kind {
  Scalar,
  V2,
  V4,
  ExtV2,
  ExtV4,
  PairRR2,
  PairRRA2,
  PairCbLoad,
  PairBrevLoad,
  /// POST/PRE AGU writeback loads: frexp (void** new_ptr, base, off) → {data,new_ptr}.
  PairLdWb,
  Skip,
};

/// NEON-style ImmCheck from TD: arg index + [Lo, Hi] inclusive.
struct ImmCheckSpec {
  unsigned ArgIdx = 0;
  int Lo = 0;
  int Hi = 0;
};

struct BuiltinEntry {
  std::string Name;
  std::string PublicName;
  std::string Builtin;         // __builtin_haydn_X / __builtin_ae_X
  std::string Prototype;       // clang builtin Prototype
  std::string PublicPrototype; // optional override for haydn.h
  std::string Mnemonic;
  std::string Semantics;
  std::string CodeGen; // optional EmitHaydnBuiltinExpr recipe (AE / specials)
  /// Effect category: Pure|SFR|State|Mem|StateMem (from TD Effect=).
  std::string Effect = "Pure";
  /// TargetBuiltin Features expression (comma=AND, pipe=OR).
  std::string Features;
  /// Arity of clang Prototype (ImmCheck ImmArgIdx is 0-based against this).
  unsigned Arity = 0;
  SmallVector<ImmCheckSpec, 2> ImmChecks; // from TD ImmChecks = [ImmCheck<…>]
  Kind K = Kind::Scalar;
  bool IsAe = false;
  bool IsPair = false;
  /// When false, skip haydn.h wrapper (builtin may still exist for dsp/CG).
  bool PublicEnabled = true;
};

static std::string upperSnake(StringRef N) {
  std::string O;
  for (char C : N)
    O.push_back(C == '.' ? '_' : std::toupper(static_cast<unsigned char>(C)));
  return O;
}

//===----------------------------------------------------------------------===//
// Golden instruction DB (optional fill-in for empty TD public fields)
//===----------------------------------------------------------------------===//

/// Count top-level commas in a prototype arg list (skip nested <...>).
static unsigned topLevelArgCommas(StringRef Proto) {
  auto L = Proto.find('(');
  auto R = Proto.rfind(')');
  if (L == StringRef::npos || R == StringRef::npos || R <= L)
    return 0;
  unsigned Depth = 0, Commas = 0;
  for (char C : Proto.slice(L + 1, R)) {
    if (C == '<')
      ++Depth;
    else if (C == '>')
      --Depth;
    else if (C == ',' && Depth == 0)
      ++Commas;
  }
  return Commas;
}

static Kind classify(StringRef Name, StringRef Proto) {
  if (Name.ends_with("_pair")) {
    if (Name.starts_with("ldw_cb_"))
      return Kind::PairCbLoad;
    // BREV loads: frexp {data, new_ptr} without cbr_sel (ptr, stride).
    if (Name.starts_with("ldw_brev_") || Name.starts_with("lw_brev_"))
      return Kind::PairBrevLoad;
    // POST/PRE AGU writeback loads: frexp void** new_ptr (not int64_t* MAC pairs).
    // Prototype: data_ty(void**, void const*, int) — same frexp shape as BREV.
    if (Proto.contains("(void**,") || Proto.contains("(int*,"))
      return Kind::PairLdWb;
    // RR2: out*, a, b  → 2 top-level commas (3 args)
    // RRA2: out*, acc1, acc2, a, b → 4 top-level commas (5 args)
    // Must ignore commas inside _ExtVector<N, T>.
    if (topLevelArgCommas(Proto) >= 4)
      return Kind::PairRRA2;
    return Kind::PairRR2;
  }

  // Golden-lane builtins: ExtVector appears in Prototype.
  // Classify by *return* type so reduce (i64 out, vector in) is not treated as
  // a same-width vector op. ExtV2/ExtV4 = full thin ExtVector surface.
  // Any ExtVector in args with non-vector ret still uses the Ext thin path
  // (see emitOne) via ExtV2/ExtV4 when ret is ExtVector; otherwise Scalar +
  // PublicPrototype handles reduce wrappers.
  if (Proto.starts_with("_ExtVector<2, int>"))
    return Kind::ExtV2;
  if (Proto.starts_with("_ExtVector<4, short>"))
    return Kind::ExtV4;
  // Mixed / reduce: ExtVector only in operands (e.g. x4energy16, x2dot32).
  if (Proto.contains("_ExtVector"))
    return Kind::ExtV2; // shared thin path; emitOne uses PublicPrototype/args

  static const char *Specials[] = {
      "mulfc32x16ras_low",   "mulfc32x16ras_high", "mulfp32x16x2ras_low",
      "mulfp32x16x2ras_high", "ldw_cb_imm_pair",    "ldw_cb_reg_pair",
  };
  for (const char *S : Specials)
    if (Name == S)
      return Kind::Skip;

  auto isRed = [](StringRef N) {
    return N.starts_with("x2hadd") || N.starts_with("x2hmax") ||
           N.starts_with("x2hmin") || N.starts_with("x2dot") ||
           N.starts_with("x4hadd") || N.starts_with("x4hmax") ||
           N.starts_with("x4hmin") || N.starts_with("x4dot");
  };
  auto isCplx = [](StringRef N) {
    return N.contains("cmul") || N.contains("fcmul") || N.contains("cjmul") ||
           N.contains("ff2mul") || N.contains("ff2rs");
  };
  static const char *MulPair[] = {"x2mulph32", "x2mulpl32", "x2mulaph32",
                                  "x2mulapl32", "x2mulsph32", "x2mulspl32"};
  for (const char *M : MulPair)
    if (Name == M)
      return Kind::Scalar;

  // Name-based V2/V4: public vector surface over legacy i64 bag builtins.
  if (Name.starts_with("x2") && !isRed(Name) && !isCplx(Name))
    return Kind::V2;
  if (Name.starts_with("x4") && !isRed(Name) && !isCplx(Name))
    return Kind::V4;
  return Kind::Scalar;
}

/// Map clang Prototype / PublicPrototype type tokens to haydn.h C types.
static std::string mapCType(StringRef T) {
  T = T.trim();
  if (T == "int64_t*")
    return "int64_t *";
  if (T == "int*")
    return "int *";
  if (T == "void**")
    return "void **";
  if (T == "void*")
    return "void *";
  if (T == "void const*" || T == "const void*")
    return "const void *";
  if (T == "size_t*")
    return "size_t *";
  if (T == "_ExtVector<2, int>")
    return "haydn_x2int32";
  if (T == "_ExtVector<4, short>")
    return "haydn_x4int16";
  if (T.starts_with("_ExtVector"))
    return "/*ext*/";
  return std::string(T);
}

static bool parseProto(StringRef Proto, std::string &Ret,
                       SmallVectorImpl<std::string> &Args) {
  Proto = Proto.trim();
  auto L = Proto.find('(');
  auto R = Proto.rfind(')');
  if (L == StringRef::npos || R == StringRef::npos || R < L)
    return false;
  Ret = mapCType(Proto.take_front(L));
  StringRef Inside = Proto.slice(L + 1, R).trim();
  if (Inside.empty())
    return true;
  unsigned Depth = 0;
  std::string Cur;
  for (char C : Inside) {
    if (C == '<')
      ++Depth;
    else if (C == '>')
      --Depth;
    if (C == ',' && Depth == 0) {
      Args.push_back(mapCType(StringRef(Cur).trim()));
      Cur.clear();
      continue;
    }
    Cur.push_back(C);
  }
  if (!Cur.empty())
    Args.push_back(mapCType(StringRef(Cur).trim()));
  return true;
}

/// Fail-closed publish / contract checks.
/// Aborts clang-tblgen rather than silently shipping a broken public wrapper
/// or an invalid Effect/ImmCheck contract.
static void validatePublish(const BuiltinEntry &E) {
  // Effect must be one of the five documented categories (all records).
  if (E.Effect != "Pure" && E.Effect != "SFR" && E.Effect != "State" &&
      E.Effect != "Mem" && E.Effect != "StateMem")
    PrintFatalError("HaydnIntrin: op '" + E.Name + "' has invalid Effect='" +
                    E.Effect +
                    "' (want Pure|SFR|State|Mem|StateMem)");

  // ImmArgIdx must address a real clang-Prototype argument.
  // ImmChecks are on the builtin frexp/clang Prototype, not PublicPrototype.
  {
    std::string RetB;
    SmallVector<std::string, 8> ArgsB;
    if (!parseProto(E.Prototype, RetB, ArgsB))
      PrintFatalError("HaydnIntrin: op '" + E.Name +
                      "' has unparseable clang Prototype '" + E.Prototype +
                      "'");
    if (E.Arity != ArgsB.size())
      PrintFatalError("HaydnIntrin: op '" + E.Name +
                      "' Arity cache mismatch vs Prototype");
    for (const ImmCheckSpec &IC : E.ImmChecks) {
      if (IC.ArgIdx >= ArgsB.size())
        PrintFatalError(
            "HaydnIntrin: op '" + E.Name + "' ImmArgIdx=" +
            std::to_string(IC.ArgIdx) + " >= Prototype arity " +
            std::to_string(ArgsB.size()) + " ('" + E.Prototype + "')");
      if (IC.Lo > IC.Hi)
        PrintFatalError("HaydnIntrin: op '" + E.Name +
                        "' ImmCheck inverted range [" + std::to_string(IC.Lo) +
                        "," + std::to_string(IC.Hi) + "]");
    }
  }

  if (!E.PublicEnabled)
    return;

  if (E.PublicName.empty())
    PrintFatalError("HaydnIntrin: PublicEnabled op '" + E.Name +
                    "' has empty PublicName");

  // Bundle format / slot / AltDesc stay backend-side (not public C surface).
  StringRef PN = E.PublicName;
  StringRef Mn = E.Mnemonic;
  auto forbidden = [](StringRef S) {
    return S.contains_insensitive("formatid") ||
           S.contains_insensitive("altdesc") ||
           S.contains_insensitive("bundle") ||
           S.contains_insensitive("_slot") || S.ends_with_insensitive("slot");
  };
  if (forbidden(PN) || forbidden(Mn))
    PrintFatalError(
        "HaydnIntrin: PublicEnabled op '" + E.Name +
        "' leaks FormatID/AltDesc/Bundle/slot into public name/mnemonic ('" +
        E.PublicName + "' / '" + E.Mnemonic + "')");

  // AE bag builtins are unpublished by default; re-enable only with a recipe.
  if (E.IsAe && E.CodeGen.empty())
    PrintFatalError("HaydnIntrin: PublicEnabled HaydnAeBuiltin '" + E.Name +
                    "' requires non-empty CodeGen= recipe");

  // Public wrapper prototype must be parseable for any published path.
  std::string Ret;
  SmallVector<std::string, 6> Args;
  StringRef Proto =
      E.PublicPrototype.empty() ? StringRef(E.Prototype) : StringRef(E.PublicPrototype);
  if (!parseProto(Proto, Ret, Args))
    PrintFatalError("HaydnIntrin: PublicEnabled op '" + E.Name +
                    "' has unparseable prototype '" + Proto.str() + "'");
}

static std::vector<BuiltinEntry> collect(const RecordKeeper &Records) {
  std::vector<BuiltinEntry> Entries;
  for (StringRef Cls : {"HaydnBuiltin", "HaydnPairBuiltin", "HaydnAeBuiltin"}) {
    for (const Record *R : Records.getAllDerivedDefinitions(Cls)) {
      BuiltinEntry E;
      E.Name = R->getName().str();
      E.Prototype = R->getValueAsString("Prototype").str();
      E.IsAe = Cls == "HaydnAeBuiltin";
      E.IsPair = Cls == "HaydnPairBuiltin" || StringRef(E.Name).ends_with("_pair");
      E.K = classify(E.Name, E.Prototype);
      E.PublicEnabled = R->getValueAsBit("PublicEnabled");

      std::string Pub = R->getValueAsString("PublicName").str();
      if (Pub.empty()) {
        if (StringRef(E.Name).ends_with("_pair"))
          Pub = E.Name.substr(0, E.Name.size() - 5);
        else
          Pub = E.Name;
      }
      E.PublicName = Pub;
      E.PublicPrototype = R->getValueAsString("PublicPrototype").str();
      E.Mnemonic = R->getValueAsString("Mnemonic").str();
      if (E.Mnemonic.empty()) {
        StringRef Base = E.Name;
        if (Base.ends_with("_pair"))
          Base = Base.drop_back(5);
        E.Mnemonic = upperSnake(Base);
      }
      E.Semantics = R->getValueAsString("Semantics").str();
      E.CodeGen = R->getValueAsString("CodeGen").str();
      // Pure|SFR|State|Mem|StateMem — drives op-manifest + attr parity.
      E.Effect = R->getValueAsString("Effect").str();
      if (E.Effect.empty())
        E.Effect = "Pure";

      // TargetBuiltin feature expression (empty = always available).
      E.Features = R->getValueAsString("Features").str();

      // NEON-style ImmChecks = [ImmCheck<ArgIdx, ImmCheck0_31>, …]
      for (const Record *IC : R->getValueAsListOfDefs("ImmChecks")) {
        ImmCheckSpec S;
        S.ArgIdx = static_cast<unsigned>(IC->getValueAsInt("ImmArgIdx"));
        const Record *Kind = IC->getValueAsDef("Kind");
        S.Lo = static_cast<int>(Kind->getValueAsInt("Lo"));
        S.Hi = static_cast<int>(Kind->getValueAsInt("Hi"));
        E.ImmChecks.push_back(S);
      }

      // Arity from clang Prototype (ImmArgIdx contract surface).
      {
        std::string RetA;
        SmallVector<std::string, 8> ArgsA;
        if (parseProto(E.Prototype, RetA, ArgsA))
          E.Arity = static_cast<unsigned>(ArgsA.size());
        else
          E.Arity = 0;
      }

      if (E.IsAe)
        E.Builtin = "__builtin_ae_" + E.Name;
      else
        E.Builtin = "__builtin_haydn_" + E.Name;

      validatePublish(E);

      // AR load/store still participate in Sema ImmCheck emission; public
      // vector wrappers live in emitSpecials (SpecialPublic skip in emitOne).
      Entries.push_back(std::move(E));
    }
  }
  llvm::sort(Entries, [](const BuiltinEntry &A, const BuiltinEntry &B) {
    return A.PublicName < B.PublicName;
  });
  return Entries;
}

/// Emit a public wrapper. \p Features is the op's TargetBuiltin feature
/// expression; a non-empty one becomes an `__attribute__((target(...)))` so the
/// DEFINITION is accepted under the default feature set and only a CALL from a
/// TU without the feature is an error. Without it, `#include <haydn.h>` fails
/// before the user has written anything (CB-136): the defaults are
/// `-simd,-bit-reversed,-circular-buffer`, and 168 wrapper BODIES reference
/// builtins that need one of those.
///
/// The expression is comma=AND, pipe=OR in TableGen. Every Haydn op today
/// names exactly one feature, which maps to the attribute directly; an OR
/// cannot be expressed as a target attribute at all, so it is rejected here
/// rather than silently dropped.
static void emitFn(raw_ostream &OS, StringRef Ret, StringRef Name,
                   StringRef Params, ArrayRef<std::string> Body,
                   StringRef Doc = {}, StringRef Features = {}) {
  if (!Doc.empty())
    OS << "/// " << Doc << "\n";
  if (Features.empty()) {
    OS << "__HAYDN_INTRIN_FN\n";
  } else {
    if (Features.contains('|'))
      PrintFatalError("HaydnIntrin: " + Name + " has an OR feature expression (" +
                      Features + "); a target attribute cannot express OR");
    OS << "__HAYDN_INTRIN_FN_T(\"" << Features << "\")\n";
  }
  OS << Ret << " haydn_" << Name << "(" << Params << ") {\n";
  for (const auto &L : Body)
    OS << "  " << L << "\n";
  OS << "}\n\n";
}

/// ImmArg-safe public API: macro so ImmArg/Sema checks the *user* call site.
/// ParamsMacro is comma-separated names, e.g. "a, sh". Expansion is the
/// full builtin expression (no trailing semicolon).
static void emitImmMacro(raw_ostream &OS, StringRef Ret, StringRef Name,
                         StringRef ParamsMacro, StringRef Expansion,
                         StringRef Doc = {}) {
  if (!Doc.empty())
    OS << "/// " << Doc << "\n";
  OS << "/* ImmArg: require constant imm at call site (not a C function). */\n"
     << "#define haydn_" << Name << "(" << ParamsMacro << ") "
     << "((" << Ret << ")(" << Expansion << "))\n\n";
}

static void emitPreamble(raw_ostream &OS) {
  OS << R"HDR(/*===---- haydn.h - Haydn DSP public API ---------------------*- C -*-===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===------------------------------------------------------------------------===
 *
 * DO NOT EDIT — generated by clang-tblgen -gen-haydn-intrin-header from
 * clang/include/clang/Basic/BuiltinsHaydn.td (HaydnIntrinEmitter.cpp).
 *
 *   #include <haydn.h>
 *   haydn_x2int32 z = haydn_x2add32s(x, y);
 *
 *===------------------------------------------------------------------------===
 */

#ifndef __HAYDN_H
#define __HAYDN_H

/* Fail-closed target + toolchain guard (haydn_types.h owns the
 * architecture #error; also require compiler capability macros so a
 * stale/non-Haydn frontend cannot silently publish empty feature surface).
 * Never emit FormatID / slot / AltDesc macros here. */
#include "haydn_types.h"

#if !defined(__HAYDN_ARCH__)
#error "haydn.h requires a Haydn-aware Clang that defines __HAYDN_ARCH__"
#endif

#ifndef __HAYDN_INTRIN_FN
#define __HAYDN_INTRIN_FN \
  static __inline__ __attribute__((__always_inline__, __nodebug__))
#endif

/* Feature-gated wrapper. The DSP builtins need target features the default
 * Haydn feature set does not have (-simd, -bit-reversed, -circular-buffer),
 * and an unguarded body makes a bare `#include <haydn.h>` fail with 168
 * "needs target feature" errors before the user writes any code. The
 * attribute moves that to the call site, the way immintrin.h does. */
#ifndef __HAYDN_INTRIN_FN_T
#define __HAYDN_INTRIN_FN_T(F) \
  static __inline__ __attribute__((__always_inline__, __nodebug__, \
                                   __target__(F)))
#endif

/// Two-result DR64 pair (rtd1 = hi, rtd2 = lo).
typedef struct {
  haydn_dr64_t hi;
  haydn_dr64_t lo;
} haydn_dpair_t;

/// Circular-buffer / 64-bit BREV load result: data + AGU-updated base pointer.
typedef struct {
  haydn_dr64_t data;
  void *new_ptr;
} haydn_cb_ld_t;

/// 32-bit BREV / S_* load result: data + AGU-updated base pointer.
typedef struct {
  int data;
  void *new_ptr;
} haydn_sld_t;

/// Alias used by POST/PRE load frexp public wrappers (S_* GPR data).
typedef haydn_sld_t haydn_ld_t;

typedef int __haydn_ext_v2i32 __attribute__((__ext_vector_type__(2)));
typedef short __haydn_ext_v4i16 __attribute__((__ext_vector_type__(4)));

/* Bitcast helpers: vector_size <-> DR64 bag (long long / haydn_dr64_t). */
__HAYDN_INTRIN_FN haydn_dr64_t __haydn_v2_as_i64(haydn_x2int32 v) {
  union { haydn_x2int32 v; haydn_dr64_t i; } u;
  u.v = v;
  return u.i;
}
__HAYDN_INTRIN_FN haydn_x2int32 __haydn_i64_as_v2(haydn_dr64_t i) {
  union { haydn_dr64_t i; haydn_x2int32 v; } u;
  u.i = i;
  return u.v;
}
__HAYDN_INTRIN_FN haydn_dr64_t __haydn_v4_as_i64(haydn_x4int16 v) {
  union { haydn_x4int16 v; haydn_dr64_t i; } u;
  u.v = v;
  return u.i;
}
__HAYDN_INTRIN_FN haydn_x4int16 __haydn_i64_as_v4(haydn_dr64_t i) {
  union { haydn_dr64_t i; haydn_x4int16 v; } u;
  u.i = i;
  return u.v;
}

)HDR";
}

// If PublicPrototype set, use it for ret/params naming (types only).
static bool usePublicProto(const BuiltinEntry &E, std::string &Ret,
                           SmallVectorImpl<std::string> &Args) {
  if (E.PublicPrototype.empty())
    return false;
  return parseProto(E.PublicPrototype, Ret, Args);
}

// On the plain call-through path the wrapper declares PublicPrototype's
// parameters and forwards them verbatim, so the two arities must agree or the
// generated header will not compile. PublicPrototype is a second,
// hand-maintained copy of an arity the builtin already states, and it drifted
// every time a prototype was reshaped — six store forms and the two clamps
// that gained an accumulator were all sitting wrong. Refuse rather than
// quietly realign to the builtin: realigning discards the type spellings that
// are the whole reason PublicPrototype exists, and says nothing.
//
// Pair kinds are exempt and must not reach here: their public form returns
// what the builtin writes through an out pointer, so it is one parameter
// shorter by design.
static void checkPublicProtoArity(const BuiltinEntry &E,
                                  ArrayRef<std::string> Args) {
  std::string BRet;
  SmallVector<std::string, 6> BArgs;
  if (!parseProto(E.Prototype, BRet, BArgs) || BArgs.size() == Args.size())
    return;
  PrintFatalError("HaydnIntrin: " + E.Name + " PublicPrototype takes " +
                  std::to_string(Args.size()) +
                  " parameter(s) but the builtin takes " +
                  std::to_string(BArgs.size()) +
                  " — the wrapper forwards them verbatim, so they must agree");
}

static std::string docLine(const BuiltinEntry &E) {
  std::string D;
  if (!E.Mnemonic.empty())
    D += "ISA: " + E.Mnemonic;
  if (!E.Semantics.empty()) {
    if (!D.empty())
      D += " — ";
    D += E.Semantics;
  }
  return D;
}

static void emitOne(raw_ostream &OS, const BuiltinEntry &E,
                    StringSet<> &Emitted) {
  // Only PublicEnabled ops appear in haydn.h.
  if (!E.PublicEnabled)
    return;
  if (E.K == Kind::Skip)
    return;
  if (Emitted.contains(E.PublicName))
    return;

  static const char *SpecialPublic[] = {
      "ldw_cb_imm",          "ldw_cb_reg",
      "ldw_brev_imm",        "ldw_brev_reg",
      "lw_brev_imm",         "lw_brev_reg",
      "mulfp32x16x2ras_low", "mulfp32x16x2ras_high",
      "mulfc32x16ras_low",   "mulfc32x16ras_high",
      "d_lqhwua_post",       "d_ltwua_post",
      "d_sqhwua_post",       "d_stwua_post",
  };
  for (const char *S : SpecialPublic)
    if (E.PublicName == S)
      return;

  std::string Ret;
  SmallVector<std::string, 6> Args;
  bool HasPub = usePublicProto(E, Ret, Args);
  if (!HasPub && !parseProto(E.Prototype, Ret, Args))
    PrintFatalError("HaydnIntrin: bad prototype for " + E.Name);

  auto Mark = [&]() { Emitted.insert(E.PublicName); };
  std::string Doc = docLine(E);

  // When PublicPrototype is set, emit a simple call-through using those types
  // and bitcasts as needed for i64 builtins.
  auto emitGenericWithNames = [&](StringRef RetT, ArrayRef<std::string> ArgTs) {
    SmallVector<std::string, 6> PNames;
    static const char *A3[] = {"acc", "a", "b"};
    static const char *A2[] = {"a", "b"};
    for (unsigned I = 0, N = ArgTs.size(); I != N; ++I) {
      std::string P;
      if (StringRef(ArgTs[I]).ends_with("**"))
        P = "out";
      else if (StringRef(ArgTs[I]).contains("void"))
        P = "base";
      else if (StringRef(ArgTs[I]).ends_with("*"))
        P = "out";
      else if (N == 1)
        P = "a";
      else if (N == 2)
        P = A2[I];
      else if (N == 3)
        P = A3[I];
      else
        P = "a" + std::to_string(I);
      std::string Base = P;
      unsigned K = 0;
      while (llvm::is_contained(PNames, P))
        P = Base + std::to_string(++K);
      PNames.push_back(P);
    }
    std::string Params, Call;
    for (unsigned I = 0, N = ArgTs.size(); I != N; ++I) {
      if (I) {
        Params += ", ";
        Call += ", ";
      }
      Params += ArgTs[I] + " " + PNames[I];
      // bitcast helpers for vector public types calling i64 builtins
      if (ArgTs[I] == "haydn_x2int32" || ArgTs[I] == "haydn_x2fract32")
        Call += "__haydn_v2_as_i64(" + PNames[I] + ")";
      else if (ArgTs[I] == "haydn_x4int16" || ArgTs[I] == "haydn_x4fract16")
        Call += "__haydn_v4_as_i64(" + PNames[I] + ")";
      else
        Call += PNames[I];
    }
    std::string CallExpr = E.Builtin + "(" + Call + ")";
    if (RetT == "haydn_x2int32" || RetT == "haydn_x2fract32")
      CallExpr = "__haydn_i64_as_v2(" + CallExpr + ")";
    else if (RetT == "haydn_x4int16" || RetT == "haydn_x4fract16")
      CallExpr = "__haydn_i64_as_v4(" + CallExpr + ")";
    if (!E.ImmChecks.empty()) {
      // Macro params: same order as PNames.
      std::string MacroParams;
      for (unsigned I = 0, N = PNames.size(); I != N; ++I) {
        if (I)
          MacroParams += ", ";
        MacroParams += PNames[I];
      }
      // Parenthesize each arg in the expansion for safety.
      std::string ExpCall;
      for (unsigned I = 0, N = ArgTs.size(); I != N; ++I) {
        if (I)
          ExpCall += ", ";
        std::string Arg = "(" + PNames[I] + ")";
        if (ArgTs[I] == "haydn_x2int32" || ArgTs[I] == "haydn_x2fract32")
          ExpCall += "__haydn_v2_as_i64" + Arg;
        else if (ArgTs[I] == "haydn_x4int16" || ArgTs[I] == "haydn_x4fract16")
          ExpCall += "__haydn_v4_as_i64" + Arg;
        else
          ExpCall += Arg;
      }
      std::string Exp = E.Builtin + "(" + ExpCall + ")";
      if (RetT == "haydn_x2int32" || RetT == "haydn_x2fract32")
        Exp = "__haydn_i64_as_v2(" + Exp + ")";
      else if (RetT == "haydn_x4int16" || RetT == "haydn_x4fract16")
        Exp = "__haydn_i64_as_v4(" + Exp + ")";
      emitImmMacro(OS, RetT, E.PublicName, MacroParams, Exp, Doc);
    } else if (RetT == "void")
      emitFn(OS, RetT, E.PublicName, Params.empty() ? "void" : Params,
             {CallExpr + ";"}, Doc, E.Features);
    else
      emitFn(OS, RetT, E.PublicName, Params.empty() ? "void" : Params,
             {"return " + CallExpr + ";"}, Doc, E.Features);
  };

  // ExtVector clang Prototype (golden lanes). Public C API:
  //   - same-width SIMD (ret vector): thin passthrough
  //   - reduce/product (ret int64_t, args v2i32/v4i16): bag-friendly
  //     int64_t params with __haydn_i64_as_v2/v4 into the ExtVector builtin
  //     (NatureDSP/BundleSim historically pass DR64 bags).
  auto emitThinExt = [&]() {
    // Builtin types always from clang Prototype (ExtVector / mixed).
    std::string BRet;
    SmallVector<std::string, 6> BArgs;
    parseProto(E.Prototype, BRet, BArgs);

    std::string R;
    SmallVector<std::string, 6> A;
    if (HasPub) {
      R = Ret;
      A.assign(Args.begin(), Args.end());
    } else {
      R = BRet;
      A = BArgs;
    }
    // Align public arity with builtin if PublicPrototype omitted slots.
    if (A.size() != BArgs.size()) {
      R = BRet;
      A = BArgs;
    }

    // Bag-friendly public form for i64-result lane products/MACs.
    const bool BagFriendly =
        (R == "int64_t" || R == "long long" || R == "haydn_dr64_t");
    if (BagFriendly) {
      for (unsigned I = 0, N = A.size(); I != N; ++I) {
        if (A[I] == "haydn_x2int32" || A[I] == "haydn_x2fract32" ||
            A[I] == "haydn_x4int16" || A[I] == "haydn_x4fract16")
          A[I] = "int64_t";
      }
    }

    static const char *N1[] = {"a"};
    static const char *N2[] = {"a", "b"};
    static const char *N3[] = {"acc", "a", "b"};
    static const char *N4[] = {"a0", "a1", "a2", "a3"};
    // X4SEL*: two vector sources + lane mask (imm or reg) — not a MAC acc form.
    const bool IsX4Sel =
        E.PublicName == "x4seli16" || E.PublicName == "x4sel16";
    SmallVector<std::string, 6> PNames;
    for (unsigned I = 0, N = A.size(); I != N; ++I) {
      std::string P;
      if (StringRef(A[I]).ends_with("*"))
        P = "out";
      else if (IsX4Sel && N == 3)
        P = (I == 0) ? "a" : (I == 1) ? "b" : "mask";
      else if (N == 1)
        P = N1[0];
      else if (N == 2)
        P = N2[I];
      else if (N == 3)
        P = N3[I];
      else if (N <= 4)
        P = N4[I];
      else
        P = "a" + std::to_string(I);
      // imm shift second/last arg (skip X4SEL mask naming above)
      if (!IsX4Sel) {
        if ((A[I] == "int" || A[I] == "unsigned int") &&
            (I + 1 == N || I == 1))
          P = (I + 1 == N && N >= 2) ? (N == 3 && I == 2 ? "imm" : "sh") : P;
        if (N == 3 && I == 2 && (A[2] == "int" || A[2] == "unsigned int"))
          P = "imm";
        if (N == 2 && I == 1 && (A[1] == "int" || A[1] == "unsigned int"))
          P = "sh";
      }
      PNames.push_back(P);
    }
    std::string Params, Call;
    for (unsigned I = 0, N = A.size(); I != N; ++I) {
      if (I) {
        Params += ", ";
        Call += ", ";
      }
      Params += A[I] + " " + PNames[I];
      // Map public bag → ExtVector for the builtin when needed.
      std::string Arg = PNames[I];
      if (I < BArgs.size()) {
        const std::string &BT = BArgs[I];
        const bool PubBag =
            A[I] == "int64_t" || A[I] == "long long" || A[I] == "haydn_dr64_t";
        if (PubBag && (BT.find("ExtVector") != std::string::npos ||
                       BT == "haydn_x2int32" || BT == "haydn_x2fract32"))
          Arg = "__haydn_i64_as_v2(" + PNames[I] + ")";
        else if (PubBag &&
                 (BT == "haydn_x4int16" || BT == "haydn_x4fract16"))
          Arg = "__haydn_i64_as_v4(" + PNames[I] + ")";
      }
      Call += Arg;
    }
    if (!E.ImmChecks.empty()) {
      std::string MacroParams, ExpCall;
      for (unsigned I = 0, N = A.size(); I != N; ++I) {
        if (I) {
          MacroParams += ", ";
          ExpCall += ", ";
        }
        MacroParams += PNames[I];
        // Same bag→vector cast as Call.
        std::string Arg = "(" + PNames[I] + ")";
        if (I < BArgs.size()) {
          const std::string &BT = BArgs[I];
          const bool PubBag =
              A[I] == "int64_t" || A[I] == "long long" || A[I] == "haydn_dr64_t";
          if (PubBag && (BT.find("ExtVector") != std::string::npos ||
                         BT == "haydn_x2int32" || BT == "haydn_x2fract32"))
            Arg = "__haydn_i64_as_v2" + Arg;
          else if (PubBag &&
                   (BT == "haydn_x4int16" || BT == "haydn_x4fract16"))
            Arg = "__haydn_i64_as_v4" + Arg;
        }
        ExpCall += Arg;
      }
      emitImmMacro(OS, R, E.PublicName, MacroParams,
                   E.Builtin + "(" + ExpCall + ")", Doc);
    } else if (R == "void")
      emitFn(OS, R, E.PublicName, Params.empty() ? "void" : Params,
             {E.Builtin + "(" + Call + ");"}, Doc, E.Features);
    else
      emitFn(OS, R, E.PublicName, Params.empty() ? "void" : Params,
             {"return " + E.Builtin + "(" + Call + ");"}, Doc, E.Features);
  };

  // PublicPrototype call-through only for plain APIs. Pair frexp kinds must
  // hit the switch below (PairLdWb / PairBrevLoad / PairCbLoad / MAC pairs).
  if (HasPub && E.K != Kind::PairRR2 && E.K != Kind::PairRRA2 &&
      E.K != Kind::PairLdWb && E.K != Kind::PairBrevLoad &&
      E.K != Kind::PairCbLoad && E.K != Kind::ExtV2 && E.K != Kind::ExtV4) {
    checkPublicProtoArity(E, Args);
    emitGenericWithNames(Ret, Args);
    Mark();
    return;
  }

  switch (E.K) {
  case Kind::ExtV2:
  case Kind::ExtV4: {
    // Golden lanes: clang Prototype is ExtVector (or reduce with ExtVector
    // args). Public API matches; emit thin call (NEON/RVV style).
    emitThinExt();
    Mark();
    return;
  }
  case Kind::PairRR2: {
    // Builtins take lane vectors (golden); frexp out-pointer stays i64*.
    StringRef PN = E.PublicName;
    if (PN.starts_with("x2"))
      emitFn(OS, "haydn_dpair_t", PN, "haydn_x2int32 a, haydn_x2int32 b",
             {"haydn_dpair_t r;",
              "r.hi = " + E.Builtin + "(&r.lo, a, b);", "return r;"},
             Doc.empty() ? "Two-result: .hi=rtd1, .lo=rtd2." : Doc, E.Features);
    else if (PN.starts_with("x4"))
      emitFn(OS, "haydn_dpair_t", PN, "haydn_x4int16 a, haydn_x4int16 b",
             {"haydn_dpair_t r;",
              "r.hi = " + E.Builtin + "(&r.lo, a, b);", "return r;"},
             Doc.empty() ? "Two-result: .hi=rtd1, .lo=rtd2." : Doc, E.Features);
    else
      emitFn(OS, "haydn_dpair_t", PN, "int64_t a, int64_t b",
             {"haydn_dpair_t r;", "r.hi = " + E.Builtin + "(&r.lo, a, b);",
              "return r;"},
             Doc, E.Features);
    Mark();
    return;
  }
  case Kind::PairRRA2: {
    StringRef PN = E.PublicName;
    if (PN.starts_with("x2"))
      emitFn(OS, "haydn_dpair_t", PN,
             "int64_t acc1, int64_t acc2, haydn_x2int32 a, haydn_x2int32 b",
             {"haydn_dpair_t r;",
              "r.hi = " + E.Builtin + "(&r.lo, acc1, acc2, a, b);",
              "return r;"},
             Doc, E.Features);
    else if (PN.starts_with("x4"))
      emitFn(OS, "haydn_dpair_t", PN,
             "int64_t acc1, int64_t acc2, haydn_x4int16 a, haydn_x4int16 b",
             {"haydn_dpair_t r;",
              "r.hi = " + E.Builtin + "(&r.lo, acc1, acc2, a, b);",
              "return r;"},
             Doc, E.Features);
    else
      emitFn(OS, "haydn_dpair_t", PN,
             "int64_t acc1, int64_t acc2, int64_t a, int64_t b",
             {"haydn_dpair_t r;",
              "r.hi = " + E.Builtin + "(&r.lo, acc1, acc2, a, b);",
              "return r;"},
             Doc, E.Features);
    Mark();
    return;
  }
  case Kind::PairLdWb: {
    // POST/PRE load frexp: public returns {.data, .new_ptr} with pointer base/wb.
    // S_* → haydn_sld_t (i32 data); D_* → haydn_cb_ld_t (i64 data).
    // IMM forms use macros so ImmArg/Sema sees a constant at the user site.
    StringRef RetT = "haydn_sld_t";
    if (StringRef(E.Prototype).starts_with("int64_t"))
      RetT = "haydn_cb_ld_t";
    StringRef PN = E.PublicName;
    if (!E.ImmChecks.empty()) {
      // Macro: (base, off) — off must be an integer constant expression.
      // Hygiene: reserved temp so caller identifiers (e.g. `r` as base/off)
      // are not captured by the stmt-expr locals; __extension__ for -pedantic.
      OS << "/* ImmArg: require constant imm at call site (not a C function). */\n";
      if (!Doc.empty())
        OS << "/// " << Doc << "\n";
      OS << "#define haydn_" << PN << "(base, off) \\\n"
         << "  __extension__ ({ " << RetT << " __haydn_frexp; \\\n"
         << "     __haydn_frexp.data = " << E.Builtin
         << "(&__haydn_frexp.new_ptr, (base), (off)); \\\n"
         << "     __haydn_frexp; })\n\n";
    } else {
      emitFn(OS, RetT, PN, "const void *base, int off",
             {std::string(RetT) + " r;",
              "r.data = " + E.Builtin + "(&r.new_ptr, base, off);",
              "return r;"},
             Doc, E.Features);
    }
    Mark();
    return;
  }
  case Kind::V2: {
    // Legacy name-based V2: public haydn_x2int32 over i64 bag builtin.
    unsigned N = Args.size();
    auto isImm = [](StringRef A) {
      return A == "int" || A == "unsigned int";
    };
    bool Bag = !StringRef(E.Prototype).contains("_ExtVector");
    auto wrap = [&](StringRef Call) -> std::string {
      if (!Bag)
        return std::string(Call);
      return "__haydn_i64_as_v2(" + std::string(Call) + ")";
    };
    auto castA = [&](StringRef V) -> std::string {
      if (!Bag)
        return std::string(V);
      return "__haydn_v2_as_i64(" + std::string(V) + ")";
    };
    if (N == 1)
      emitFn(OS, "haydn_x2int32", E.PublicName, "haydn_x2int32 a",
             {"return " + wrap(E.Builtin + "(" + castA("a") + ")") + ";"}, Doc);
    else if (N == 2 && isImm(Args[1])) {
      if (!E.ImmChecks.empty())
        emitImmMacro(OS, "haydn_x2int32", E.PublicName, "a, sh",
                     wrap(E.Builtin + "(" + castA("(a)") + ", (sh)"), Doc);
      else
        emitFn(OS, "haydn_x2int32", E.PublicName, "haydn_x2int32 a, int sh",
               {"return " +
                wrap(E.Builtin + "(" + castA("a") + ", sh)") +
                ";"},
               Doc);
    } else if (N == 3 && isImm(Args[2])) {
      if (!E.ImmChecks.empty())
        emitImmMacro(OS, "haydn_x2int32", E.PublicName, "a, b, imm",
                     wrap(E.Builtin + "(" + castA("(a)") + ", " + castA("(b)") +
                          ", (imm)"),
                     Doc);
      else
        emitFn(OS, "haydn_x2int32", E.PublicName,
               "haydn_x2int32 a, haydn_x2int32 b, int imm",
               {"return " +
                wrap(E.Builtin + "(" + castA("a") + ", " + castA("b") +
                     ", imm)") +
                ";"},
               Doc);
    } else
      emitFn(OS, "haydn_x2int32", E.PublicName,
             "haydn_x2int32 a, haydn_x2int32 b",
             {"return " +
              wrap(E.Builtin + "(" + castA("a") + ", " + castA("b") + ")") +
              ";"},
             Doc);
    Mark();
    return;
  }
  case Kind::V4: {
    unsigned N = Args.size();
    bool Bag = !StringRef(E.Prototype).contains("_ExtVector");
    auto wrap = [&](StringRef Call) -> std::string {
      if (!Bag)
        return std::string(Call);
      return "__haydn_i64_as_v4(" + std::string(Call) + ")";
    };
    auto castA = [&](StringRef V) -> std::string {
      if (!Bag)
        return std::string(V);
      return "__haydn_v4_as_i64(" + std::string(V) + ")";
    };
    if (E.PublicName == "x4seli16" || E.PublicName == "x4sel16") {
      // Select: sources a,b + lane mask (imm form is ImmArg macro).
      if (!E.ImmChecks.empty())
        emitImmMacro(OS, "haydn_x4int16", E.PublicName, "a, b, mask",
                     wrap(E.Builtin + "(" + castA("(a)") + ", " + castA("(b)") +
                          ", (mask)"),
                     Doc);
      else
        emitFn(OS, "haydn_x4int16", E.PublicName,
               "haydn_x4int16 a, haydn_x4int16 b, int mask",
               {"return " +
                wrap(E.Builtin + "(" + castA("a") + ", " + castA("b") +
                     ", mask)") +
                ";"},
               Doc);
    } else if (N == 1)
      emitFn(OS, "haydn_x4int16", E.PublicName, "haydn_x4int16 a",
             {"return " + wrap(E.Builtin + "(" + castA("a") + ")") + ";"}, Doc);
    else if (N == 2 && (Args[1] == "int" || Args[1] == "unsigned int")) {
      if (!E.ImmChecks.empty())
        emitImmMacro(OS, "haydn_x4int16", E.PublicName, "a, sh",
                     wrap(E.Builtin + "(" + castA("(a)") + ", (sh)"), Doc);
      else
        emitFn(OS, "haydn_x4int16", E.PublicName, "haydn_x4int16 a, int sh",
               {"return " +
                wrap(E.Builtin + "(" + castA("a") + ", sh)") +
                ";"},
               Doc);
    } else
      emitFn(OS, "haydn_x4int16", E.PublicName,
             "haydn_x4int16 a, haydn_x4int16 b",
             {"return " +
              wrap(E.Builtin + "(" + castA("a") + ", " + castA("b") + ")") +
              ";"},
             Doc);
    Mark();
    return;
  }
  case Kind::Scalar: {
    // packsr32 / satsr64 / packsr32x2_* are not builtins (composites in
    // haydn_dsp.h only). Scalar ImmArg ops (e.g. srai64r) use macros via
    // emitGenericWithNames when ImmChecks is non-empty.
    emitGenericWithNames(Ret, Args);
    Mark();
    return;
  }
  case Kind::Skip:
  case Kind::PairCbLoad:
  case Kind::PairBrevLoad:
    return;
  }
}

static void emitSpecials(raw_ostream &OS) {
  OS << "//===----------------------------------------------------------------------===//\n"
        "// Specials (composed / frexp CB / AR helpers)\n"
        "//===----------------------------------------------------------------------===//\n\n";

  // CB/BREV IMM frexp wrappers are macros so ImmArg/Sema checks the *user*
  // call site (cbr_sel / stride constants). CB REG cbr_sel is also ImmArg.
  // Hygiene: reserved __haydn_frexp temp (never capture caller `r` used as
  // base/offset/stride) + __extension__ for pedantic C (Hexagon peer pattern).
  OS << "/// ISA: LDW_CB_IMM — circular-buffer load (imm stride).\n"
        "/* ImmArg: cbr_sel [0,1], stride simm8 at call site. */\n"
        "#define haydn_ldw_cb_imm(base, cbr_sel, stride) \\\n"
        "  __extension__ ({ haydn_cb_ld_t __haydn_frexp; \\\n"
        "     __haydn_frexp.data = __builtin_haydn_ldw_cb_imm_pair("
        "&__haydn_frexp.new_ptr, (base), (cbr_sel), (stride)); \\\n"
        "     __haydn_frexp; })\n\n";
  OS << "/// ISA: LDW_CB_REG — circular-buffer load (reg stride).\n"
        "/* ImmArg: cbr_sel [0,1] at call site; stride is variable GPR. */\n"
        "#define haydn_ldw_cb_reg(base, cbr_sel, stride) \\\n"
        "  __extension__ ({ haydn_cb_ld_t __haydn_frexp; \\\n"
        "     __haydn_frexp.data = __builtin_haydn_ldw_cb_reg_pair("
        "&__haydn_frexp.new_ptr, (base), (cbr_sel), (stride)); \\\n"
        "     __haydn_frexp; })\n\n";

  // BREV loads — frexp {data, new_ptr}; IMM stride is ImmArg (simm6).
  OS << "/// ISA: D_LDW_BREV_IMM — 64-bit bit-reversed load (imm stride).\n"
        "/* ImmArg: stride simm6 at call site. */\n"
        "#define haydn_ldw_brev_imm(base, stride) \\\n"
        "  __extension__ ({ haydn_cb_ld_t __haydn_frexp; \\\n"
        "     __haydn_frexp.data = __builtin_haydn_ldw_brev_imm_pair("
        "&__haydn_frexp.new_ptr, (base), (stride)); \\\n"
        "     __haydn_frexp; })\n\n";
  emitFn(OS, "haydn_cb_ld_t", "ldw_brev_reg", "const void *base, int stride",
         {"haydn_cb_ld_t r;",
          "r.data = __builtin_haydn_ldw_brev_reg_pair(&r.new_ptr, base, "
          "stride);",
          "return r;"},
         "ISA: D_LDW_BREV_REG — 64-bit bit-reversed load (reg stride).",
         "bit-reversed");
  OS << "/// ISA: S_LW_BREV_IMM — 32-bit bit-reversed load (imm stride).\n"
        "/* ImmArg: stride simm6 at call site. */\n"
        "#define haydn_lw_brev_imm(base, stride) \\\n"
        "  __extension__ ({ haydn_sld_t __haydn_frexp; \\\n"
        "     __haydn_frexp.data = __builtin_haydn_lw_brev_imm_pair("
        "&__haydn_frexp.new_ptr, (base), (stride)); \\\n"
        "     __haydn_frexp; })\n\n";
  emitFn(OS, "haydn_sld_t", "lw_brev_reg", "const void *base, int stride",
         {"haydn_sld_t r;",
          "r.data = __builtin_haydn_lw_brev_reg_pair(&r.new_ptr, base, "
          "stride);",
          "return r;"},
         "ISA: S_LW_BREV_REG — 32-bit bit-reversed load (reg stride).",
         "bit-reversed");

  emitFn(OS, "haydn_x2fract32", "mulfp32x16x2ras_low",
         "haydn_x2fract32 acc, haydn_x2fract32 a32, haydn_x4fract16 b16",
         {"return __haydn_i64_as_v2(__builtin_haydn_mulfp32x16x2ras_low("
          "__haydn_v2_as_i64(acc), __haydn_v2_as_i64(a32), "
          "__haydn_v4_as_i64(b16)));"},
         /*Doc=*/{}, "simd");
  emitFn(OS, "haydn_x2fract32", "mulfp32x16x2ras_high",
         "haydn_x2fract32 acc, haydn_x2fract32 a32, haydn_x4fract16 b16",
         {"return __haydn_i64_as_v2(__builtin_haydn_mulfp32x16x2ras_high("
          "__haydn_v2_as_i64(acc), __haydn_v2_as_i64(a32), "
          "__haydn_v4_as_i64(b16)));"},
         /*Doc=*/{}, "simd");

  // Twiddle widen uses only uimm in [0,31] (x2s*i32 range). x2slli32/x2srai32
  // take ExtVector (golden lanes); bag↔vector via helpers. High half via C
  // shift on the bag (not x2srli32(..., 32)).
  emitFn(OS, "haydn_x2fract32", "mulfc32x16ras_low",
         "haydn_x2fract32 acc, haydn_x2fract32 data, haydn_x4fract16 tw",
         {"haydn_x2int32 tw_v = __haydn_i64_as_v2(__haydn_v4_as_i64(tw));",
          "haydn_x2int32 tw_shifted = __builtin_haydn_x2slli32(tw_v, 16);",
          "haydn_x2int32 tw_widened = __builtin_haydn_x2srai32(tw_shifted, 16);",
          "return __haydn_i64_as_v2(__builtin_haydn_mulfc32x16ras_low("
          "__haydn_v2_as_i64(acc), __haydn_v2_as_i64(data), "
          "__haydn_v2_as_i64(tw_widened)));"},
         /*Doc=*/{}, "simd");
  emitFn(OS, "haydn_x2fract32", "mulfc32x16ras_high",
         "haydn_x2fract32 acc, haydn_x2fract32 data, haydn_x4fract16 tw",
         {"int64_t tw_i = __haydn_v4_as_i64(tw);",
          "int64_t tw_hi = (int64_t)(uint32_t)((uint64_t)tw_i >> 32);",
          "haydn_x2int32 tw_v = __haydn_i64_as_v2(tw_hi);",
          "haydn_x2int32 tw_shifted = __builtin_haydn_x2slli32(tw_v, 16);",
          "haydn_x2int32 tw_widened = __builtin_haydn_x2srai32(tw_shifted, 16);",
          "return __haydn_i64_as_v2(__builtin_haydn_mulfc32x16ras_high("
          "__haydn_v2_as_i64(acc), __haydn_v2_as_i64(data), "
          "__haydn_v2_as_i64(tw_widened)));"},
         /*Doc=*/{}, "simd");

  // ar_sel is an ImmArg encoding field. Public wrappers accept a runtime ar
  // (NatureDSP ar&=3) but only pass literal 0..3 to the builtin via switch so
  // Sema's ImmCheck sees an ICE at the builtin call site.
  //
  // The re-delivered database dropped the direction select and fixed the
  // post-increment at +8 (FORMAT-E-SWITCH-PLAN.md § 7), so these bodies carry
  // neither `stride` nor `dir_sel` any more. They are the emitter's own copy
  // of each builtin's arity and drifted silently when the prototypes moved.
  // Nothing checks these particular bodies: they are literal text, and the
  // only thing that reads them is a C compiler on the generated haydn.h.
  // (An earlier revision of this comment named scripts/haydn_builtin_call_
  // arity.py as the standing check. There is no such script and there never
  // was — a check that exists only in a comment reads exactly like one that
  // runs.)
  OS << "/* ImmArg ar_sel: switch-literal dispatch. */\n";
  emitFn(OS, "haydn_x4int16", "d_lqhwua_post", "const void *ptr, int ar_sel",
         {"switch (ar_sel & 3) {",
          "case 0: return __haydn_i64_as_v4(__builtin_haydn_d_lqhwua_post(ptr, 0));",
          "case 1: return __haydn_i64_as_v4(__builtin_haydn_d_lqhwua_post(ptr, 1));",
          "case 2: return __haydn_i64_as_v4(__builtin_haydn_d_lqhwua_post(ptr, 2));",
          "default: return __haydn_i64_as_v4(__builtin_haydn_d_lqhwua_post(ptr, 3));",
          "}"});
  emitFn(OS, "haydn_x2int32", "d_ltwua_post", "const void *ptr, int ar_sel",
         {"switch (ar_sel & 3) {",
          "case 0: return __haydn_i64_as_v2(__builtin_haydn_d_ltwua_post(ptr, 0));",
          "case 1: return __haydn_i64_as_v2(__builtin_haydn_d_ltwua_post(ptr, 1));",
          "case 2: return __haydn_i64_as_v2(__builtin_haydn_d_ltwua_post(ptr, 2));",
          "default: return __haydn_i64_as_v2(__builtin_haydn_d_ltwua_post(ptr, 3));",
          "}"});
  emitFn(OS, "void", "d_sqhwua_post",
         "haydn_x4int16 data, void *ptr, int ar_sel",
         {"int64_t d = __haydn_v4_as_i64(data);",
          "switch (ar_sel & 3) {",
          "case 0: __builtin_haydn_d_sqhwua_post(d, ptr, 0); break;",
          "case 1: __builtin_haydn_d_sqhwua_post(d, ptr, 1); break;",
          "case 2: __builtin_haydn_d_sqhwua_post(d, ptr, 2); break;",
          "default: __builtin_haydn_d_sqhwua_post(d, ptr, 3); break;",
          "}"});
  emitFn(OS, "void", "d_stwua_post",
         "haydn_x2int32 data, void *ptr, int ar_sel",
         {"int64_t d = __haydn_v2_as_i64(data);",
          "switch (ar_sel & 3) {",
          "case 0: __builtin_haydn_d_stwua_post(d, ptr, 0); break;",
          "case 1: __builtin_haydn_d_stwua_post(d, ptr, 1); break;",
          "case 2: __builtin_haydn_d_stwua_post(d, ptr, 2); break;",
          "default: __builtin_haydn_d_stwua_post(d, ptr, 3); break;",
          "}"});

  // The hardware post-increment is a fixed +8, so the _ip wrappers advance by
  // that rather than by a caller-supplied signed stride.
  emitFn(OS, "haydn_x4int16", "d_lqhwua_post_ip", "void **pptr, int ar_sel",
         {"haydn_x4int16 v = haydn_d_lqhwua_post(*pptr, ar_sel);",
          "*pptr = (void *)((char *)*pptr + 8);", "return v;"});
  emitFn(OS, "haydn_x2int32", "d_ltwua_post_ip", "void **pptr, int ar_sel",
         {"haydn_x2int32 v = haydn_d_ltwua_post(*pptr, ar_sel);",
          "*pptr = (void *)((char *)*pptr + 8);", "return v;"});
  emitFn(OS, "void", "d_sqhwua_post_ip",
         "haydn_x4int16 data, void **pptr, int ar_sel",
         {"haydn_d_sqhwua_post(data, *pptr, ar_sel);",
          "*pptr = (void *)((char *)*pptr + 8);"});
  emitFn(OS, "void", "d_stwua_post_ip",
         "haydn_x2int32 data, void **pptr, int ar_sel",
         {"haydn_d_stwua_post(data, *pptr, ar_sel);",
          "*pptr = (void *)((char *)*pptr + 8);"});

  OS << "#define haydn_movad32_h haydn_movad32_high\n"
        "#define haydn_movad32_l haydn_movad32_low\n\n";
}

/// Intrinsic enum suffix: x2mul32_pair → x2mul32
static std::string llvmIntrinSuffix(StringRef Name) {
  if (Name.ends_with("_pair"))
    return Name.drop_back(5).str();
  return Name.str();
}

//===----------------------------------------------------------------------===//
// Exhaustive PublicEnabled closure probe (OpenCL builtin-tests peer)
//===----------------------------------------------------------------------===//

/// Specials hard-coded in emitSpecials (skipped by emitOne). Public shapes here
/// must match haydn.h, not a stale PublicPrototype bag form.
static bool specialPublicShape(StringRef PN, std::string &Ret,
                               SmallVectorImpl<std::string> &Args) {
  Args.clear();
  if (PN == "ldw_cb_imm" || PN == "ldw_cb_reg") {
    Ret = "haydn_cb_ld_t";
    Args.assign({"const void *", "int", "int"});
    return true;
  }
  if (PN == "ldw_brev_imm" || PN == "ldw_brev_reg") {
    Ret = "haydn_cb_ld_t";
    Args.assign({"const void *", "int"});
    return true;
  }
  if (PN == "lw_brev_imm" || PN == "lw_brev_reg") {
    Ret = "haydn_sld_t";
    Args.assign({"const void *", "int"});
    return true;
  }
  if (PN == "mulfp32x16x2ras_low" || PN == "mulfp32x16x2ras_high" ||
      PN == "mulfc32x16ras_low" || PN == "mulfc32x16ras_high") {
    Ret = "haydn_x2fract32";
    Args.assign({"haydn_x2fract32", "haydn_x2fract32", "haydn_x4fract16"});
    return true;
  }
  // UA wrappers accept a runtime ar (switch-literal ImmArg at the builtin).
  // Format E has no stride and no direction select, so ar_sel is the last
  // argument — these lists must stay in step with BuiltinsHaydn.td, which
  // resolvePublicShape() now enforces rather than asks for.
  if (PN == "d_lqhwua_post") {
    Ret = "haydn_x4int16";
    Args.assign({"const void *", "int"});
    return true;
  }
  if (PN == "d_ltwua_post") {
    Ret = "haydn_x2int32";
    Args.assign({"const void *", "int"});
    return true;
  }
  if (PN == "d_sqhwua_post") {
    Ret = "void";
    Args.assign({"haydn_x4int16", "void *", "int"});
    return true;
  }
  if (PN == "d_stwua_post") {
    Ret = "void";
    Args.assign({"haydn_x2int32", "void *", "int"});
    return true;
  }
  return false;
}

/// How many leading frexp out-pointer args the clang Prototype has that the
/// public haydn_* surface drops (Pair frexp → haydn_dpair_t / cb_ld / sld).
static unsigned frexpOutDrop(const BuiltinEntry &E) {
  std::string RetB;
  SmallVector<std::string, 8> ArgsB;
  if (!parseProto(E.Prototype, RetB, ArgsB) || ArgsB.empty())
    return 0;
  auto isOutPtr = [](StringRef T) {
    return T == "int64_t *" || T == "void **" || T == "int *" ||
           T == "size_t *";
  };
  if (!isOutPtr(ArgsB[0]))
    return 0;
  // PublicPrototype (when set) should not start with the frexp out pointer.
  if (!E.PublicPrototype.empty()) {
    std::string RetP;
    SmallVector<std::string, 8> ArgsP;
    if (parseProto(E.PublicPrototype, RetP, ArgsP) && !ArgsP.empty() &&
        isOutPtr(ArgsP[0]))
      return 0;
  }
  return 1;
}

/// Apply emitThinExt bag-friendly rewrite: i64-result lane products/MACs
/// publish vector operands as int64_t DR64 bags (NatureDSP peer).
static void applyBagFriendlyPublic(std::string &Ret,
                                   SmallVectorImpl<std::string> &Args) {
  if (Ret != "int64_t" && Ret != "long long" && Ret != "haydn_dr64_t")
    return;
  for (std::string &A : Args) {
    if (A == "haydn_x2int32" || A == "haydn_x2fract32" ||
        A == "haydn_x4int16" || A == "haydn_x4fract16" ||
        A == "_ExtVector<2, int>" || A == "_ExtVector<4, short>")
      A = "int64_t";
  }
}

/// Resolve the public haydn_<PublicName> C signature used by the probe.
/// Must match emitOne / emitThinExt / emitSpecials (not stale TD bags).
static bool resolvePublicShape(const BuiltinEntry &E, std::string &Ret,
                               SmallVectorImpl<std::string> &Args) {
  if (specialPublicShape(E.PublicName, Ret, Args)) {
    // specialPublicShape() is a hand-written restatement of an arity the .td
    // already carries, and checkPublicProtoArity never saw it because it wins
    // before PublicPrototype is even parsed. That is how the AR family drifted:
    // BuiltinsHaydn.td dropped the stride and dir_sel, this table kept handing
    // them out, and the only thing that noticed was the closure probe failing
    // to compile — one test, in a directory nobody reads first. Same rule as
    // checkPublicProtoArity, applied where the shape actually comes from.
    std::string BRet;
    SmallVector<std::string, 8> BArgs;
    unsigned Drop = frexpOutDrop(E);
    if (parseProto(E.Prototype, BRet, BArgs) && BArgs.size() >= Drop &&
        Args.size() != BArgs.size() - Drop)
      PrintFatalError("HaydnIntrin: specialPublicShape(" + E.PublicName +
                      ") gives " + std::to_string(Args.size()) +
                      " parameter(s) but the builtin takes " +
                      std::to_string(BArgs.size()) + " less " +
                      std::to_string(Drop) +
                      " frexp out-pointer(s) — update the table in this file "
                      "when a prototype in BuiltinsHaydn.td moves");
    return true;
  }

  // Prefer PublicPrototype when set (same as emitOne HasPub path).
  bool HasPub = !E.PublicPrototype.empty() &&
                parseProto(E.PublicPrototype, Ret, Args);

  // Kind-based fallback when PublicPrototype is empty (mirrors emitOne).
  if (!HasPub) {
    switch (E.K) {
    case Kind::PairRR2: {
      Ret = "haydn_dpair_t";
      StringRef PN = E.PublicName;
      if (PN.starts_with("x2"))
        Args.assign({"haydn_x2int32", "haydn_x2int32"});
      else if (PN.starts_with("x4"))
        Args.assign({"haydn_x4int16", "haydn_x4int16"});
      else
        Args.assign({"int64_t", "int64_t"});
      break;
    }
    case Kind::PairRRA2: {
      Ret = "haydn_dpair_t";
      StringRef PN = E.PublicName;
      if (PN.starts_with("x2"))
        Args.assign(
            {"int64_t", "int64_t", "haydn_x2int32", "haydn_x2int32"});
      else if (PN.starts_with("x4"))
        Args.assign(
            {"int64_t", "int64_t", "haydn_x4int16", "haydn_x4int16"});
      else
        Args.assign({"int64_t", "int64_t", "int64_t", "int64_t"});
      break;
    }
    case Kind::PairLdWb: {
      Ret = StringRef(E.Prototype).starts_with("int64_t") ? "haydn_cb_ld_t"
                                                          : "haydn_sld_t";
      Args.assign({"const void *", "int"});
      break;
    }
    case Kind::PairCbLoad: {
      Ret = "haydn_cb_ld_t";
      Args.assign({"const void *", "int", "int"});
      break;
    }
    case Kind::PairBrevLoad: {
      if (StringRef(E.PublicName).starts_with("lw_")) {
        Ret = "haydn_sld_t";
        Args.assign({"const void *", "int"});
      } else {
        Ret = "haydn_cb_ld_t";
        Args.assign({"const void *", "int"});
      }
      break;
    }
    case Kind::V2: {
      std::string RetP;
      SmallVector<std::string, 8> ArgsP;
      if (!parseProto(E.Prototype, RetP, ArgsP))
        return false;
      if (!StringRef(E.Prototype).contains("_ExtVector")) {
        Ret = "haydn_x2int32";
        Args.clear();
        for (unsigned I = 0, N = ArgsP.size(); I != N; ++I) {
          if (ArgsP[I] == "int" || ArgsP[I] == "unsigned int")
            Args.push_back(ArgsP[I]);
          else
            Args.push_back("haydn_x2int32");
        }
      } else {
        Ret = RetP;
        Args = ArgsP;
      }
      break;
    }
    case Kind::V4: {
      std::string RetP;
      SmallVector<std::string, 8> ArgsP;
      if (!parseProto(E.Prototype, RetP, ArgsP))
        return false;
      if (!StringRef(E.Prototype).contains("_ExtVector")) {
        Ret = "haydn_x4int16";
        Args.clear();
        for (unsigned I = 0, N = ArgsP.size(); I != N; ++I) {
          if (ArgsP[I] == "int" || ArgsP[I] == "unsigned int")
            Args.push_back(ArgsP[I]);
          else
            Args.push_back("haydn_x4int16");
        }
      } else {
        Ret = RetP;
        Args = ArgsP;
      }
      break;
    }
    case Kind::ExtV2:
    case Kind::ExtV4:
    case Kind::Scalar:
    case Kind::Skip:
      if (!parseProto(E.Prototype, Ret, Args))
        return false;
      break;
    }
  }

  // emitThinExt: for ExtV2/ExtV4, align arity with clang Prototype and apply
  // bag-friendly rewrite (vector public args → int64_t when ret is i64).
  if (E.K == Kind::ExtV2 || E.K == Kind::ExtV4) {
    std::string BRet;
    SmallVector<std::string, 8> BArgs;
    if (parseProto(E.Prototype, BRet, BArgs) && Args.size() != BArgs.size()) {
      // PublicPrototype omitted slots — fall back to clang Prototype mapped.
      Ret = BRet;
      Args = BArgs;
    }
    applyBagFriendlyPublic(Ret, Args);
  }

  return true;
}

static int pickInRangeImm(int Lo, int Hi) {
  if (Lo <= 0 && 0 <= Hi)
    return 0;
  if (Lo <= 1 && 1 <= Hi)
    return 1;
  return Lo;
}

static void emitSinkOf(raw_ostream &OS, StringRef Ret, StringRef Tmp) {
  if (Ret == "void")
    return;
  if (Ret == "void *" || Ret == "void*") {
    OS << "  sink_p = (void *)(" << Tmp << ");\n";
    return;
  }
  if (Ret == "int" || Ret == "unsigned int" || Ret == "haydn_pred2_t" ||
      Ret == "haydn_pred4_t") {
    OS << "  sink_i = (int)(" << Tmp << ");\n";
    return;
  }
  if (Ret == "int64_t" || Ret == "long long" || Ret == "haydn_dr64_t") {
    OS << "  sink_ll = (long long)(" << Tmp << ");\n";
    return;
  }
  if (Ret == "haydn_x2int32" || Ret == "haydn_x2fract32") {
    OS << "  sink_v2 = (" << Tmp << ");\n";
    return;
  }
  if (Ret == "haydn_x4int16" || Ret == "haydn_x4fract16") {
    OS << "  sink_v4 = (" << Tmp << ");\n";
    return;
  }
  if (Ret == "haydn_dpair_t") {
    OS << "  sink_ll = (long long)((" << Tmp << ").hi ^ (" << Tmp
       << ").lo);\n";
    return;
  }
  if (Ret == "haydn_cb_ld_t") {
    OS << "  sink_ll = (long long)((" << Tmp << ").data);\n"
       << "  sink_p = (" << Tmp << ").new_ptr;\n";
    return;
  }
  if (Ret == "haydn_sld_t" || Ret == "haydn_ld_t") {
    OS << "  sink_i = (" << Tmp << ").data;\n"
       << "  sink_p = (" << Tmp << ").new_ptr;\n";
    return;
  }
  // Fallback: bit-cast-ish keep-alive via volatile char sink.
  OS << "  sink_i ^= (int)(long)(void *)&(" << Tmp << ");\n";
}

static void emitOneClosureProbe(raw_ostream &OS, const BuiltinEntry &E,
                                StringSet<> &Emitted) {
  if (!E.PublicEnabled)
    return;
  if (Emitted.contains(E.PublicName))
    return;

  std::string Ret;
  SmallVector<std::string, 8> Args;
  if (!resolvePublicShape(E, Ret, Args))
    PrintFatalError("HaydnIntrin: closure probe: cannot resolve public shape "
                    "for '" +
                    E.Name + "' / haydn_" + E.PublicName);

  // Map ImmCheck (clang Prototype indices) → public arg indices.
  unsigned Drop = frexpOutDrop(E);
  DenseMap<unsigned, std::pair<int, int>> ImmAt;
  for (const ImmCheckSpec &IC : E.ImmChecks) {
    if (IC.ArgIdx < Drop)
      PrintFatalError("HaydnIntrin: closure probe: ImmArgIdx=" +
                      std::to_string(IC.ArgIdx) + " under frexp drop on '" +
                      E.Name + "'");
    unsigned PubIdx = IC.ArgIdx - Drop;
    if (PubIdx >= Args.size())
      PrintFatalError("HaydnIntrin: closure probe: public ImmArgIdx=" +
                      std::to_string(PubIdx) + " out of range for haydn_" +
                      E.PublicName + " arity " + std::to_string(Args.size()));
    ImmAt[PubIdx] = {IC.Lo, IC.Hi};
  }

  Emitted.insert(E.PublicName);

  // External linkage so -O2 does not DCE unreferenced probes.
  OS << "// PUB " << E.Name << " → haydn_" << E.PublicName << " effect="
     << E.Effect;
  if (!E.Features.empty())
    OS << " features=" << E.Features;
  OS << "\n";
  OS << "void capi_op_closure_" << E.PublicName << "(";
  // Non-ImmArg parameters only (type-driven dummies from caller).
  bool FirstParam = true;
  for (unsigned I = 0, N = Args.size(); I != N; ++I) {
    if (ImmAt.count(I))
      continue;
    if (!FirstParam)
      OS << ", ";
    FirstParam = false;
    OS << Args[I] << " a" << I;
  }
  if (FirstParam)
    OS << "void";
  OS << ") {\n";

  // Build call: ImmArg slots get in-range ICE; others use parameters.
  std::string Call = "haydn_" + E.PublicName + "(";
  for (unsigned I = 0, N = Args.size(); I != N; ++I) {
    if (I)
      Call += ", ";
    auto It = ImmAt.find(I);
    if (It != ImmAt.end()) {
      Call += std::to_string(pickInRangeImm(It->second.first, It->second.second));
    } else {
      Call += "a" + std::to_string(I);
    }
  }
  Call += ")";

  if (Ret == "void") {
    OS << "  " << Call << ";\n";
  } else {
    OS << "  " << Ret << " r = " << Call << ";\n";
    emitSinkOf(OS, Ret, "r");
  }
  OS << "}\n\n";
}

} // namespace

void clang::EmitHaydnIntrinHeader(const RecordKeeper &Records, raw_ostream &OS) {
  emitPreamble(OS);
  auto Entries = collect(Records);
  OS << "//===----------------------------------------------------------------------===//\n"
        "// Generated from BuiltinsHaydn.td (PublicEnabled only)\n"
        "//===----------------------------------------------------------------------===//\n\n";
  StringSet<> Emitted;
  for (const BuiltinEntry &E : Entries)
    emitOne(OS, E, Emitted);
  emitSpecials(OS);
  OS << "#endif /* __HAYDN_H */\n";
}

/// Public-op contract manifest for continuous CI parity audits.
/// Columns cover publish bit, effect, features, arity, ImmChecks, prototype.
/// No FormatID / slot / AltDesc / draft bundle fields — backend only.
void clang::EmitHaydnOpManifest(const RecordKeeper &Records, raw_ostream &OS) {
  OS << "//===-- haydn_op_manifest.inc - Haydn public op contract -===//\n"
        "//\n"
        "// Automatically generated from BuiltinsHaydn.td. DO NOT EDIT.\n"
        "// Backend: clang-tblgen -gen-haydn-op-manifest\n"
        "//\n"
        "// Fail-closed PublicEnabled vs unpublished listing.\n"
        "// Effect column (Pure|SFR|State|Mem|StateMem) for attr parity.\n"
        "// Full public-op contract — features, arity, ImmChecks, prototype.\n"
        "// CI audits every public op for type/arity, Imm boundaries,\n"
        "// feature gates, IR effects (no FormatID/slot/AltDesc).\n"
        "//\n"
        "// Columns (comma-separated after the tag; field commas → '_'):\n"
        "//   HAYDN_OP_{PUB|UNPUB},<td_name>,<public_name>,<builtin>,<mnemonic>,\n"
        "//     <codegen>,<effect>,<features>,<arity>,<imm_list>,<prototype>\n"
        "//   imm_list: empty | idx:lo:hi | idx:lo:hi;idx:lo:hi (0-based)\n"
        "//   prototype: clang Prototype (ImmCheck surface), not PublicPrototype\n"
        "//\n"
        "// No FormatID / slot / AltDesc / draft bundle fields — backend only.\n"
        "//\n"
        "//===----------------------------------------------------------------------===//\n\n";

  auto Entries = collect(Records);
  unsigned Pub = 0, Unpub = 0;
  unsigned NPure = 0, NSFR = 0, NState = 0, NMem = 0, NStateMem = 0, NOther = 0;
  unsigned NFeat = 0, NImm = 0;
  for (const BuiltinEntry &E : Entries) {
    auto esc = [](StringRef S) {
      std::string O;
      for (char C : S) {
        if (C == ',' || C == '\\' || C == '\n' || C == '\r')
          O.push_back('_');
        else
          O.push_back(C);
      }
      return O;
    };
    auto immList = [](const BuiltinEntry &BE) {
      std::string O;
      for (unsigned I = 0, N = BE.ImmChecks.size(); I < N; ++I) {
        if (I)
          O.push_back(';');
        const ImmCheckSpec &IC = BE.ImmChecks[I];
        O += std::to_string(IC.ArgIdx);
        O.push_back(':');
        O += std::to_string(IC.Lo);
        O.push_back(':');
        O += std::to_string(IC.Hi);
      }
      return O;
    };
    StringRef Eff = E.Effect;
    if (Eff == "Pure")
      ++NPure;
    else if (Eff == "SFR")
      ++NSFR;
    else if (Eff == "State")
      ++NState;
    else if (Eff == "Mem")
      ++NMem;
    else if (Eff == "StateMem")
      ++NStateMem;
    else
      ++NOther;
    if (!E.Features.empty())
      ++NFeat;
    if (!E.ImmChecks.empty())
      ++NImm;
    const char *Tag = E.PublicEnabled ? "HAYDN_OP_PUB" : "HAYDN_OP_UNPUB";
    if (E.PublicEnabled)
      ++Pub;
    else
      ++Unpub;
    OS << Tag << "," << esc(E.Name) << "," << esc(E.PublicName) << ","
       << esc(E.Builtin) << "," << esc(E.Mnemonic) << "," << esc(E.CodeGen)
       << "," << esc(E.Effect) << "," << esc(E.Features) << "," << E.Arity
       << "," << immList(E) << "," << esc(E.Prototype) << "\n";
  }
  OS << "\n// Summary: PublicEnabled=" << Pub << " unpublished=" << Unpub
     << " total=" << (Pub + Unpub) << "\n";
  OS << "// Effect: Pure=" << NPure << " SFR=" << NSFR << " State=" << NState
     << " Mem=" << NMem << " StateMem=" << NStateMem;
  if (NOther)
    OS << " Other=" << NOther;
  OS << "\n";
  OS << "// Contract: with_features=" << NFeat << " with_immchecks=" << NImm
     << "\n";
}

/// Exhaustive PublicEnabled C probe (continuous closure).
/// OpenCL `-gen-clang-opencl-builtin-tests` peer over BuiltinsHaydn.td.
/// Each public haydn_* is called with type-driven dummies + in-range ImmArg
/// ICE so lit can prove C → Sema → IR → ISel → object at -O0/-O2.
/// No FormatID / slot / AltDesc / draft bundle fields in the public surface.
void clang::EmitHaydnOpClosureProbe(const RecordKeeper &Records,
                                    raw_ostream &OS) {
  OS << "/*===-- haydn-op-closure-probe.c - PublicEnabled C→object matrix -===//\n"
        " *\n"
        " * Automatically generated from BuiltinsHaydn.td. DO NOT EDIT.\n"
        " * Backend: clang-tblgen -gen-haydn-op-closure-probe\n"
        " *\n"
        " * Continuous PublicEnabled closure:\n"
        " *   Every PublicEnabled op has a haydn_* call site with type-driven\n"
        " *   dummies and in-range ImmArg ICE. Compile this TU to object at\n"
        " *   -O0 and -O2 (fail closed: fix ISel or unpublish any miss).\n"
        " *\n"
        " * Hexagon peer = ImmArg / feature-gated public surface.\n"
        " * Never emits FormatID / slot / AltDesc / draft compact formats.\n"
        " *\n"
        " *===-------------------------------------------------------------------===*/\n"
        "\n"
        "#include <haydn.h>\n"
        "\n"
        "/* Keep results live under -O2 (pure ops still touch volatile). */\n"
        "volatile long long sink_ll;\n"
        "volatile int sink_i;\n"
        "volatile void *sink_p;\n"
        "volatile haydn_x2int32 sink_v2;\n"
        "volatile haydn_x4int16 sink_v4;\n"
        "\n";

  auto Entries = collect(Records);
  StringSet<> Emitted;
  unsigned N = 0;
  for (const BuiltinEntry &E : Entries) {
    if (!E.PublicEnabled)
      continue;
    if (Emitted.contains(E.PublicName))
      continue;
    emitOneClosureProbe(OS, E, Emitted);
    ++N;
  }
  OS << "/* Summary: probed=" << N
     << " PublicEnabled unique public names (haydn_*) */\n";
}

//===----------------------------------------------------------------------===//
// Imm non-ICE + range-negative Sema audit
//===----------------------------------------------------------------------===//

/// True for frexp out-pointer slots on the clang Prototype (not public shape).
static bool isFrexpOutPtrType(StringRef T) {
  return T == "void **" || T == "int64_t *" || T == "int *" || T == "size_t *";
}

static void emitFrexpOutLocals(raw_ostream &OS, ArrayRef<std::string> Args) {
  for (unsigned I = 0, N = Args.size(); I != N; ++I) {
    if (Args[I] == "void **")
      OS << "  void *o" << I << ";\n";
    else if (Args[I] == "int64_t *")
      OS << "  int64_t o" << I << ";\n";
    else if (Args[I] == "int *")
      OS << "  int o" << I << ";\n";
    else if (Args[I] == "size_t *")
      OS << "  size_t o" << I << ";\n";
  }
}

/// Emit one ImmArg expression: frexp local, under-test (nc / bad ICE), sibling
/// ImmArg in-range ICE, or non-imm parameter aN.
static void appendImmAuditArg(
    raw_ostream &OS, StringRef T, unsigned ArgIdx, unsigned UnderTest,
    bool HasImmOverride, int ImmOverride,
    const DenseMap<unsigned, std::pair<int, int>> &ImmAt) {
  if (isFrexpOutPtrType(T)) {
    OS << "&o" << ArgIdx;
    return;
  }
  if (ArgIdx == UnderTest) {
    if (HasImmOverride)
      OS << ImmOverride;
    else
      OS << "nc";
    return;
  }
  auto It = ImmAt.find(ArgIdx);
  if (It != ImmAt.end()) {
    OS << pickInRangeImm(It->second.first, It->second.second);
    return;
  }
  OS << "a" << ArgIdx;
}

static void emitImmAuditFnHeader(raw_ostream &OS, const BuiltinEntry &E,
                                 StringRef Ret, ArrayRef<std::string> Args,
                                 const DenseMap<unsigned, std::pair<int, int>> &ImmAt,
                                 StringRef Suffix, unsigned UnderTest,
                                 bool NeedsNcParam) {
  OS << Ret << " imm_audit_" << E.Name << "_" << Suffix << "(";
  bool First = true;
  for (unsigned I = 0, N = Args.size(); I != N; ++I) {
    if (isFrexpOutPtrType(Args[I]))
      continue;
    if (I == UnderTest) {
      if (!NeedsNcParam)
        continue; // range form: Imm is a literal in the call
      if (!First)
        OS << ", ";
      First = false;
      OS << "int nc";
      continue;
    }
    if (ImmAt.count(I))
      continue; // sibling ImmArgs are ICE literals
    if (!First)
      OS << ", ";
    First = false;
    OS << Args[I] << " a" << I;
  }
  if (First)
    OS << "void";
  OS << ") {\n";
}

static void emitImmAuditCall(raw_ostream &OS, const BuiltinEntry &E,
                             StringRef Ret, ArrayRef<std::string> Args,
                             const DenseMap<unsigned, std::pair<int, int>> &ImmAt,
                             unsigned UnderTest, bool HasImmOverride,
                             int ImmOverride) {
  OS << "  ";
  if (Ret != "void")
    OS << "return ";
  OS << E.Builtin << "(";
  for (unsigned I = 0, N = Args.size(); I != N; ++I) {
    if (I)
      OS << ", ";
    appendImmAuditArg(OS, Args[I], I, UnderTest, HasImmOverride, ImmOverride,
                      ImmAt);
  }
  OS << ");\n";
  if (Ret == "void")
    OS << "  return;\n";
}

/// One PublicEnabled ImmChecked builtin → non-ICE + range-negative -verify tests.
/// Calls __builtin_haydn_* / __builtin_ae_* (Sema ImmCheck home), never UA
/// public switch wrappers or haydn_dsp.h.
static void emitOneImmAudit(raw_ostream &OS, const BuiltinEntry &E,
                            unsigned &NOps, unsigned &NNonConst,
                            unsigned &NRange) {
  if (!E.PublicEnabled || E.ImmChecks.empty())
    return;

  std::string Ret;
  SmallVector<std::string, 8> Args;
  if (!parseProto(E.Prototype, Ret, Args))
    PrintFatalError("HaydnIntrin: imm audit: unparseable Prototype for '" +
                    E.Name + "'");

  DenseMap<unsigned, std::pair<int, int>> ImmAt;
  for (const ImmCheckSpec &IC : E.ImmChecks) {
    if (IC.ArgIdx >= Args.size())
      PrintFatalError("HaydnIntrin: imm audit: ImmArgIdx=" +
                      std::to_string(IC.ArgIdx) + " out of range on '" +
                      E.Name + "'");
    ImmAt[IC.ArgIdx] = {IC.Lo, IC.Hi};
  }

  ++NOps;
  OS << "// --- " << E.Builtin << " ImmChecks=";
  for (unsigned I = 0, N = E.ImmChecks.size(); I != N; ++I) {
    if (I)
      OS << ";";
    const ImmCheckSpec &IC = E.ImmChecks[I];
    OS << IC.ArgIdx << ":" << IC.Lo << ":" << IC.Hi;
  }
  OS << " effect=" << E.Effect;
  if (!E.Features.empty())
    OS << " features=" << E.Features;
  OS << " ---\n";

  auto emitCase = [&](StringRef Kind, unsigned UnderTest, bool NeedsNc,
                      bool HasOverride, int OverrideVal, StringRef ErrMsg) {
    std::string Suffix =
        std::string(Kind) + "_a" + std::to_string(UnderTest);
    emitImmAuditFnHeader(OS, E, Ret, Args, ImmAt, Suffix, UnderTest, NeedsNc);
    emitFrexpOutLocals(OS, Args);
    // expected-error immediately above the diagnosing call (always next line).
    OS << "  // expected-error@+1 {{" << ErrMsg << "}}\n";
    emitImmAuditCall(OS, E, Ret, Args, ImmAt, UnderTest, HasOverride,
                     OverrideVal);
    OS << "}\n\n";
  };

  for (const ImmCheckSpec &IC : E.ImmChecks) {
    const unsigned A = IC.ArgIdx;

    // non-ICE
    {
      std::string Err =
          "argument to '" + E.Builtin + "' must be a constant integer";
      emitCase("nc", A, /*NeedsNc=*/true, /*HasOverride=*/false, 0, Err);
      ++NNonConst;
    }

    // range high (Hi+1) when representable as signed 32-bit
    if (IC.Hi < 2147483647) {
      int Bad = IC.Hi + 1;
      std::string Err = "argument value " + std::to_string(Bad) +
                        " is outside the valid range [" +
                        std::to_string(IC.Lo) + ", " + std::to_string(IC.Hi) +
                        "]";
      emitCase("hi", A, /*NeedsNc=*/false, /*HasOverride=*/true, Bad, Err);
      ++NRange;
    }

    // range low (Lo-1) when representable as signed 32-bit
    if (IC.Lo > (-2147483647 - 1)) { // Lo > INT_MIN
      int Bad = IC.Lo - 1;
      std::string Err = "argument value " + std::to_string(Bad) +
                        " is outside the valid range [" +
                        std::to_string(IC.Lo) + ", " + std::to_string(IC.Hi) +
                        "]";
      emitCase("lo", A, /*NeedsNc=*/false, /*HasOverride=*/true, Bad, Err);
      ++NRange;
    }
  }
}

/// Exhaustive ImmCheck non-ICE + range-negative Sema audit.
/// NEON ImmCheck / OpenCL exhaustive-test peer over BuiltinsHaydn.td ImmChecks.
/// Emits a -verify TU that calls every PublicEnabled ImmChecked __builtin_haydn_*
/// with a runtime non-ICE ImmArg and out-of-range ICE (Lo-1 / Hi+1 when
/// representable). Full SImm32 (movei) is non-ICE-only (any i32 is in range).
/// Never emits FormatID / slot / AltDesc / haydn_dsp.h dual-owner surface.
void clang::EmitHaydnOpImmAudit(const RecordKeeper &Records, raw_ostream &OS) {
  OS << "/*===-- haydn-op-imm-audit.c - ImmCheck non-ICE + range Sema audit -===//\n"
        " *\n"
        " * Automatically generated from BuiltinsHaydn.td. DO NOT EDIT.\n"
        " * Backend: clang-tblgen -gen-haydn-op-imm-audit\n"
        " *\n"
        " * Continuous Imm contract:\n"
        " *   Every PublicEnabled op with ImmChecks rejects non-ICE ImmArgs and\n"
        " *   out-of-range constants at the __builtin_haydn_* Sema home\n"
        " *   (BuiltinConstantArgRange). Do not call UA public switch wrappers.\n"
        " *\n"
        " * Hexagon peer = ImmArg-gated builtins. No FormatID / slot / AltDesc.\n"
        " *\n"
        " *===-------------------------------------------------------------------===*/\n"
        "\n"
        "// NOTE: Lit driver runs this TU under -verify (diag expectations below).\n"
        "\n"
        "typedef long long int64_t;\n"
        "typedef __SIZE_TYPE__ size_t;\n"
        "typedef int haydn_x2int32 __attribute__((__vector_size__(8)));\n"
        "typedef short haydn_x4int16 __attribute__((__vector_size__(8)));\n"
        "\n";

  auto Entries = collect(Records);
  unsigned NOps = 0, NNonConst = 0, NRange = 0;
  for (const BuiltinEntry &E : Entries)
    emitOneImmAudit(OS, E, NOps, NNonConst, NRange);

  OS << "/* Summary: imm_ops=" << NOps << " nonconst_tests=" << NNonConst
     << " range_tests=" << NRange << " */\n";
}

//===----------------------------------------------------------------------===//
// Features-gate Sema audit (generic / full / noagu multi-verify)
//===----------------------------------------------------------------------===//

/// Profile feature maps (match HaydnTargetInfo / HaydnGeneric.td / product CPU):
///   generic → agu + hwloop
///   full    → agu + circular-buffer + bit-reversed + hwloop + simd  (-target-cpu haydn)
///   noagu   → full minus agu  (-target-cpu haydn -target-feature -agu)
static bool profileHasFeature(StringRef Profile, StringRef Feat) {
  Feat = Feat.trim();
  if (Feat.empty())
    return true;
  if (Profile == "full")
    return true;
  if (Profile == "generic")
    return Feat == "agu" || Feat == "hwloop";
  if (Profile == "noagu")
    return Feat != "agu";
  return false;
}

/// Builtin Features expression: comma=AND, pipe=OR (same as TargetBuiltin).
static bool featuresSatisfied(StringRef Features, StringRef Profile) {
  if (Features.empty())
    return true;
  SmallVector<StringRef, 4> Ors;
  Features.split(Ors, '|', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  for (StringRef Alt : Ors) {
    SmallVector<StringRef, 4> Ands;
    Alt.split(Ands, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
    bool Ok = !Ands.empty();
    for (StringRef F : Ands) {
      if (!profileHasFeature(Profile, F)) {
        Ok = false;
        break;
      }
    }
    if (Ok)
      return true;
  }
  return false;
}

/// One PublicEnabled feature-gated builtin → multi-verify -verify Sema cases.
/// Calls __builtin_haydn_* / __builtin_ae_* (Sema feature home; Hexagon peer =
/// checkTargetFeatures). ImmArgs use in-range ICE so only feature diags fire.
static void emitOneFeatureAudit(raw_ostream &OS, const BuiltinEntry &E,
                                unsigned &NOps, unsigned &NGenericFail,
                                unsigned &NNoAguFail, unsigned &NFullFail) {
  if (!E.PublicEnabled || E.Features.empty())
    return;

  std::string Ret;
  SmallVector<std::string, 8> Args;
  if (!parseProto(E.Prototype, Ret, Args))
    PrintFatalError("HaydnIntrin: feature audit: unparseable Prototype for '" +
                    E.Name + "'");

  DenseMap<unsigned, std::pair<int, int>> ImmAt;
  for (const ImmCheckSpec &IC : E.ImmChecks) {
    if (IC.ArgIdx >= Args.size())
      PrintFatalError("HaydnIntrin: feature audit: ImmArgIdx=" +
                      std::to_string(IC.ArgIdx) + " out of range on '" +
                      E.Name + "'");
    ImmAt[IC.ArgIdx] = {IC.Lo, IC.Hi};
  }

  const bool FailGeneric = !featuresSatisfied(E.Features, "generic");
  const bool FailFull = !featuresSatisfied(E.Features, "full");
  const bool FailNoAgu = !featuresSatisfied(E.Features, "noagu");
  if (FailGeneric)
    ++NGenericFail;
  if (FailFull)
    ++NFullFail;
  if (FailNoAgu)
    ++NNoAguFail;
  ++NOps;

  OS << "// --- " << E.Builtin << " features=" << E.Features
     << " effect=" << E.Effect << " ---\n";
  OS << Ret << " feat_audit_" << E.Name << "(";
  bool First = true;
  for (unsigned I = 0, N = Args.size(); I != N; ++I) {
    if (isFrexpOutPtrType(Args[I]) || ImmAt.count(I))
      continue;
    if (!First)
      OS << ", ";
    First = false;
    OS << Args[I] << " a" << I;
  }
  if (First)
    OS << "void";
  OS << ") {\n";
  emitFrexpOutLocals(OS, Args);

  // Stack multi-prefix expected-error lines immediately above the call.
  // full should never fail for product feature names (simd|cb|brev|agu|hwloop).
  unsigned ExpectLines = 0;
  if (FailGeneric)
    ++ExpectLines;
  if (FailFull)
    ++ExpectLines;
  if (FailNoAgu)
    ++ExpectLines;
  if (ExpectLines == 0) {
    // Feature available on all three profiles (e.g. hwloop-only, if any).
    OS << "  // available on generic + full + noagu\n";
  } else {
    unsigned Remaining = ExpectLines;
    auto emitErr = [&](StringRef Prefix) {
      // expected-error@+N must point at the call (next Remaining lines of
      // other prefixes + the call itself when Remaining==1).
      OS << "  // " << Prefix << "-error@+" << Remaining << " {{'" << E.Builtin
         << "' needs target feature " << E.Features << "}}\n";
      --Remaining;
    };
    if (FailGeneric)
      emitErr("generic");
    if (FailFull)
      emitErr("full");
    if (FailNoAgu)
      emitErr("noagu");
  }

  OS << "  ";
  if (Ret != "void")
    OS << "return ";
  OS << E.Builtin << "(";
  for (unsigned I = 0, N = Args.size(); I != N; ++I) {
    if (I)
      OS << ", ";
    if (isFrexpOutPtrType(Args[I])) {
      OS << "&o" << I;
      continue;
    }
    auto It = ImmAt.find(I);
    if (It != ImmAt.end()) {
      OS << pickInRangeImm(It->second.first, It->second.second);
      continue;
    }
    OS << "a" << I;
  }
  OS << ");\n";
  if (Ret == "void")
    OS << "  return;\n";
  OS << "}\n\n";
}

/// Exhaustive Features-gate Sema audit.
/// OpenCL/NEON exhaustive peer: every PublicEnabled op with non-empty
/// Features is called as __builtin_haydn_* under multi-verify generic /
/// full / noagu so err_builtin_needs_feature is continuous CI.
/// Hexagon peer = checkTargetFeatures. No FormatID / slot / AltDesc /
/// haydn_dsp.h dual-owner surface.
void clang::EmitHaydnOpFeatureAudit(const RecordKeeper &Records,
                                    raw_ostream &OS) {
  OS << "/*===-- haydn-op-feature-audit.c - Features-gate Sema audit -===//\n"
        " *\n"
        " * Automatically generated from BuiltinsHaydn.td. DO NOT EDIT.\n"
        " * Backend: clang-tblgen -gen-haydn-op-feature-audit\n"
        " *\n"
        " * Continuous feature contract:\n"
        " *   Every PublicEnabled op with non-empty Features rejects calls\n"
        " *   when the caller's target feature map lacks the required\n"
        " *   simd|circular-buffer|bit-reversed|agu|hwloop expression\n"
        " *   (SemaHaydn err_builtin_needs_feature). Profiles:\n"
        " *     generic = agu + hwloop\n"
        " *     full    = -target-cpu haydn (all five)\n"
        " *     noagu   = haydn -agu\n"
        " *   Call __builtin_haydn_* Sema home only (not UA public wrappers).\n"
        " *\n"
        " * Hexagon peer = checkTargetFeatures. No FormatID / slot / AltDesc.\n"
        " *\n"
        " *===-------------------------------------------------------------------===*/\n"
        "\n"
        "// NOTE: Lit driver multi-verifies this TU (generic/full/noagu).\n"
        "// Product CPU (-target-cpu haydn) enables every gated Features string.\n"
        "// full-no-diagnostics\n"
        "\n"
        "typedef long long int64_t;\n"
        "typedef __SIZE_TYPE__ size_t;\n"
        "typedef int haydn_x2int32 __attribute__((__vector_size__(8)));\n"
        "typedef short haydn_x4int16 __attribute__((__vector_size__(8)));\n"
        "\n";

  auto Entries = collect(Records);
  unsigned NOps = 0, NGenericFail = 0, NNoAguFail = 0, NFullFail = 0;
  unsigned NSimd = 0, NCb = 0, NBrev = 0, NAgu = 0, NOther = 0;
  for (const BuiltinEntry &E : Entries) {
    if (!E.PublicEnabled || E.Features.empty())
      continue;
    if (E.Features == "simd")
      ++NSimd;
    else if (E.Features == "circular-buffer")
      ++NCb;
    else if (E.Features == "bit-reversed")
      ++NBrev;
    else if (E.Features == "agu")
      ++NAgu;
    else
      ++NOther;
    emitOneFeatureAudit(OS, E, NOps, NGenericFail, NNoAguFail, NFullFail);
  }

  OS << "/* Summary: feat_ops=" << NOps << " generic_fail=" << NGenericFail
     << " full_fail=" << NFullFail << " noagu_fail=" << NNoAguFail
     << " by_feat: simd=" << NSimd << " circular-buffer=" << NCb
     << " bit-reversed=" << NBrev << " agu=" << NAgu;
  if (NOther)
    OS << " other=" << NOther;
  OS << " */\n";
}

void clang::EmitHaydnBuiltinCG(const RecordKeeper &Records, raw_ostream &OS) {
  OS << "//===-- haydn_builtin_cg.inc - Generated Haydn builtin CodeGen -------===//\n"
        "//\n"
        "// Automatically generated from BuiltinsHaydn.td. DO NOT EDIT.\n"
        "// Included inside EmitHaydnBuiltinExpr switch (after default:).\n"
        "//\n"
        "// Covers:\n"
        "//   - frexp-pattern _pair / CB+BREV+POST/PRE load (PairRR2 /\n"
        "//     PairRRA2 / PairCbLoad / PairBrevLoad / PairLdWb)\n"
        "//   - HaydnAeBuiltin / special CodeGen= recipes (LanewiseUnary /\n"
        "//     TernaryI64 / AddAndSubRng / ComposeCmulAcc / SoftISqrt)\n"
        "//   - CodeGen= on PairRRA2 overrides auto 1:1 intrinsic (no phantoms)\n"
        "//\n"
        "//===----------------------------------------------------------------------===//\n\n";

  auto Entries = collect(Records);
  auto caseId = [](const BuiltinEntry &E) {
    if (E.IsAe)
      return std::string("Haydn::BI__builtin_ae_") + E.Name;
    return std::string("Haydn::BI__builtin_haydn_") + E.Name;
  };

  // Frexp-pattern pairs. Non-empty CodeGen= overrides the auto 1:1 intrinsic
  // map so composed ops (e.g. x2cmula32 → x2cmul32+add64) do not emit phantom
  // int_haydn_x2cmula* references.
  for (const BuiltinEntry &E : Entries) {
    if (!E.CodeGen.empty())
      continue;
    if (E.K == Kind::PairRR2) {
      std::string Intr = llvmIntrinSuffix(E.Name);
      OS << "  case " << caseId(E) << ":\n"
         << "    return emitPairRR2(*this, haydn_" << Intr << ", E);\n";
    } else if (E.K == Kind::PairRRA2) {
      std::string Intr = llvmIntrinSuffix(E.Name);
      OS << "  case " << caseId(E) << ":\n"
         << "    return emitPairRRA2(*this, haydn_" << Intr << ", E);\n";
    } else if (E.K == Kind::PairCbLoad) {
      std::string Intr = llvmIntrinSuffix(E.Name);
      OS << "  case " << caseId(E) << ":\n"
         << "    return emitCbLoadPair(*this, haydn_" << Intr << ", E);\n";
    } else if (E.K == Kind::PairBrevLoad) {
      std::string Intr = llvmIntrinSuffix(E.Name);
      OS << "  case " << caseId(E) << ":\n"
         << "    return emitBrevLoadPair(*this, haydn_" << Intr << ", E);\n";
    } else if (E.K == Kind::PairLdWb) {
      // Same frexp shape as BREV: (void** new_ptr, base, off) → {data, new_ptr}.
      std::string Intr = llvmIntrinSuffix(E.Name);
      OS << "  case " << caseId(E) << ":\n"
         << "    return emitLoadWbPair(*this, haydn_" << Intr << ", E);\n";
    }
  }

  // Explicit CodeGen= recipes (AE specials + compose / soft expands).
  SmallVector<std::string, 4> AddAndSubCases;
  SmallVector<std::string, 4> SoftISqrtCases;
  for (const BuiltinEntry &E : Entries) {
    if (E.CodeGen.empty())
      continue;
    StringRef CG = E.CodeGen;
    if (CG.consume_front("LanewiseUnary:")) {
      OS << "  case " << caseId(E) << ":\n"
         << "    return emitLanewiseUnary(*this, haydn_" << CG << ", E);\n";
    } else if (CG.consume_front("TernaryI64:")) {
      OS << "  case " << caseId(E) << ":\n"
         << "    return emitTernaryI64(*this, haydn_" << CG << ", E);\n";
    } else if (CG.consume_front("ComposeCmulAcc:")) {
      // ComposeCmulAcc:<mul_suffix>:<acc_suffix>
      // e.g. ComposeCmulAcc:x2cmul32:add64
      auto Sep = CG.find(':');
      if (Sep == StringRef::npos)
        PrintFatalError("ComposeCmulAcc needs <mul>:<acc> on " + E.Name);
      StringRef Mul = CG.take_front(Sep);
      StringRef Acc = CG.drop_front(Sep + 1);
      OS << "  case " << caseId(E) << ":\n"
         << "    return emitComposeCmulAcc(*this, haydn_" << Mul << ", haydn_"
         << Acc << ", E);\n";
    } else if (CG == "AddAndSubRng") {
      AddAndSubCases.push_back(caseId(E));
    } else if (CG == "SoftISqrt") {
      SoftISqrtCases.push_back(caseId(E));
    } else {
      PrintFatalError("unknown CodeGen recipe '" + E.CodeGen + "' on " +
                      E.Name);
    }
  }
  if (!AddAndSubCases.empty()) {
    for (const std::string &C : AddAndSubCases)
      OS << "  case " << C << ":\n";
    OS << "    return emitAeAddAndSubRng(*this, E);\n";
  }
  if (!SoftISqrtCases.empty()) {
    for (const std::string &C : SoftISqrtCases)
      OS << "  case " << C << ":\n";
    OS << "    return emitSoftISqrt(*this, E);\n";
  }
}

void clang::EmitHaydnBuiltinSema(const RecordKeeper &Records, raw_ostream &OS) {
  OS << R"HDR(//===-- haydn_builtin_sema.inc - Generated Haydn builtin Sema --------===//
//
// Automatically generated from BuiltinsHaydn.td. DO NOT EDIT.
//
// Include at file scope from SemaHaydn.cpp. Provides:
//   - HaydnBuiltinInfos[] / findHaydnBuiltinInfo()
//   - HAYDN_BUILTIN_SEMA_CASES  (switch body for CheckHaydnBuiltinFunctionCall)
//
// Imm ranges come from NEON-style ImmChecks on each builtin record:
//   let ImmChecks = [ImmCheck<1, ImmCheck0_31>];
//
//===----------------------------------------------------------------------===//

)HDR";

  auto Entries = collect(Records);

  auto esc = [](StringRef S) {
    std::string O;
    for (char C : S) {
      if (C == '\\' || C == '"')
        O.push_back('\\');
      if (C == '\n' || C == '\r') {
        O += " ";
        continue;
      }
      O.push_back(C);
    }
    return O;
  };

  auto builtinId = [](const BuiltinEntry &E) {
    if (E.IsAe)
      return std::string("Haydn::BI__builtin_ae_") + E.Name;
    return std::string("Haydn::BI__builtin_haydn_") + E.Name;
  };

  //--- Metadata table (file scope) ------------------------------------------//
  OS << "namespace {\n"
        "struct HaydnBuiltinInfo {\n"
        "  unsigned BuiltinID;\n"
        "  const char *PublicName;\n"
        "  const char *Mnemonic;\n"
        "  const char *Semantics;\n"
        "  bool IsPairFrexp;\n"
        "};\n"
        "static const HaydnBuiltinInfo HaydnBuiltinInfos[] = {\n";

  for (const BuiltinEntry &E : Entries) {
    bool Pair = E.K == Kind::PairRR2 || E.K == Kind::PairRRA2 ||
                E.K == Kind::PairCbLoad || E.K == Kind::PairBrevLoad ||
                E.K == Kind::PairLdWb || E.IsPair;
    OS << "  {" << builtinId(E) << ", \"haydn_" << esc(E.PublicName) << "\", \""
       << esc(E.Mnemonic) << "\", \"" << esc(E.Semantics) << "\", "
       << (Pair ? "true" : "false") << "},\n";
  }
  OS << "};\n\n"
        "[[maybe_unused]] static const HaydnBuiltinInfo *\n"
        "findHaydnBuiltinInfo(unsigned ID) {\n"
        "  for (const HaydnBuiltinInfo &I : HaydnBuiltinInfos)\n"
        "    if (I.BuiltinID == ID)\n"
        "      return &I;\n"
        "  return nullptr;\n"
        "}\n"
        "} // namespace\n\n";

  //--- Switch cases as a macro (expand inside switch (BuiltinID)) ---------//
  // One case body per unique (frexp-null?, ImmCheck-list) signature so frexp
  // pairs with ImmArg and multi-ImmCheck CB forms never emit duplicate labels.
  OS << "// clang-format off\n"
        "#define HAYDN_BUILTIN_SEMA_CASES \\\n";

  auto isFrexpPair = [](const BuiltinEntry &E) {
    return E.K == Kind::PairRR2 || E.K == Kind::PairRRA2 ||
           E.K == Kind::PairCbLoad || E.K == Kind::PairBrevLoad ||
           E.K == Kind::PairLdWb ||
           (E.IsPair && StringRef(E.Name).ends_with("_pair"));
  };

  // Key: "F|..." or "N|arg:lo:hi;..." → case ids sharing that body.
  std::map<std::string, SmallVector<std::string, 8>> BodyByKey;
  for (const BuiltinEntry &E : Entries) {
    bool Frexp = isFrexpPair(E);
    if (!Frexp && E.ImmChecks.empty())
      continue;
    std::string Key = Frexp ? "F|" : "N|";
    for (const ImmCheckSpec &IC : E.ImmChecks) {
      Key += std::to_string(IC.ArgIdx) + ':' + std::to_string(IC.Lo) + ':' +
             std::to_string(IC.Hi) + ';';
    }
    BodyByKey[Key].push_back(builtinId(E));
  }

  for (const auto &KV : BodyByKey) {
    for (const std::string &C : KV.second)
      OS << "  case " << C << ": \\\n";
    StringRef Key = KV.first;
    bool Frexp = Key.starts_with("F|");
    Key = Key.drop_front(2);
    if (Frexp) {
      OS << "    if (const Expr *Out = TheCall->getArg(0)->IgnoreParenCasts()) { \\\n"
            "      if (Out->isNullPointerConstant(getASTContext(), \\\n"
            "                                     Expr::NPC_ValueDependentIsNotNull)) { \\\n"
            "        Diag(Out->getExprLoc(), diag::warn_null_arg) \\\n"
            "            << Out->getSourceRange(); \\\n"
            "      } \\\n"
            "    } \\\n";
    }
    if (!Key.empty()) {
      SmallVector<StringRef, 4> Parts;
      Key.split(Parts, ';', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
      for (StringRef Part : Parts) {
        unsigned ArgIdx = 0;
        int Lo = 0, Hi = 0;
        auto A = Part.split(':');
        auto B = A.second.split(':');
        if (A.first.getAsInteger(10, ArgIdx) || B.first.getAsInteger(10, Lo) ||
            B.second.getAsInteger(10, Hi))
          continue;
        OS << "    if (SemaRef.BuiltinConstantArgRange(TheCall, " << ArgIdx
           << ", " << Lo << ", " << Hi << ")) \\\n"
              "      return true; \\\n";
      }
    }
    OS << "    break; \\\n";
  }

  OS << "\n// clang-format on\n";
}
