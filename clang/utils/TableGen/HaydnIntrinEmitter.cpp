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
//   -gen-haydn-intrin-header   → haydn.h
//   -gen-haydn-builtin-codegen → haydn_builtin_cg.inc
//   -gen-haydn-builtin-sema    → haydn_builtin_sema.inc
//
// BuiltinsHaydn.inc is still produced by -gen-clang-builtins (shared TD).
//
//===----------------------------------------------------------------------===//

#include "TableGenBackends.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <cctype>
#include <string>
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
  SmallVector<ImmCheckSpec, 2> ImmChecks; // from TD ImmChecks = [ImmCheck<…>]
  Kind K = Kind::Scalar;
  bool IsAe = false;
  bool IsPair = false;
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

      // NEON-style ImmChecks = [ImmCheck<ArgIdx, ImmCheck0_31>, …]
      for (const Record *IC : R->getValueAsListOfDefs("ImmChecks")) {
        ImmCheckSpec S;
        S.ArgIdx = static_cast<unsigned>(IC->getValueAsInt("ImmArgIdx"));
        const Record *Kind = IC->getValueAsDef("Kind");
        S.Lo = static_cast<int>(Kind->getValueAsInt("Lo"));
        S.Hi = static_cast<int>(Kind->getValueAsInt("Hi"));
        E.ImmChecks.push_back(S);
      }

      if (E.IsAe)
        E.Builtin = "__builtin_ae_" + E.Name;
      else
        E.Builtin = "__builtin_haydn_" + E.Name;

      // AR ops re-emitted as specials with vector surface.
      if (E.Name == "d_lqhwua_post" || E.Name == "d_ltwua_post" ||
          E.Name == "d_sqhwua_post" || E.Name == "d_stwua_post")
        continue;

      Entries.push_back(std::move(E));
    }
  }
  llvm::sort(Entries, [](const BuiltinEntry &A, const BuiltinEntry &B) {
    return A.PublicName < B.PublicName;
  });
  return Entries;
}

static void emitFn(raw_ostream &OS, StringRef Ret, StringRef Name,
                   StringRef Params, ArrayRef<std::string> Body,
                   StringRef Doc = {}) {
  if (!Doc.empty())
    OS << "/// " << Doc << "\n";
  OS << "__HAYDN_INTRIN_FN\n"
     << Ret << " haydn_" << Name << "(" << Params << ") {\n";
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

#include "haydn_types.h"

#ifndef __HAYDN_INTRIN_FN
#define __HAYDN_INTRIN_FN \
  static __inline__ __attribute__((__always_inline__, __nodebug__))
#endif

/// Two-result DR64 pair (rtd1 = hi, rtd2 = lo).
typedef struct {
  haydn_dr64_t hi;
  haydn_dr64_t lo;
} haydn_dpair_t;

/// Circular-buffer load result: data + AGU-updated base.
typedef struct {
  haydn_dr64_t data;
  int new_ptr;
} haydn_cb_ld_t;

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
  if (E.K == Kind::Skip)
    return;
  if (Emitted.contains(E.PublicName))
    return;

  static const char *SpecialPublic[] = {
      "ldw_cb_imm",          "ldw_cb_reg",
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
      if (StringRef(ArgTs[I]).ends_with("*"))
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
             {CallExpr + ";"}, Doc);
    else
      emitFn(OS, RetT, E.PublicName, Params.empty() ? "void" : Params,
             {"return " + CallExpr + ";"}, Doc);
  };

  // ExtVector clang Prototype (golden lanes) → thin passthrough, no bag
  // bitcasts. Covers same-width SIMD, reduce (i64 out), ternary MAC, imm
  // shifts, and mixed (x4sat32t16: v4i16(v2i32,v2i32)).
  auto emitThinExt = [&]() {
    std::string R;
    SmallVector<std::string, 6> A;
    if (HasPub) {
      R = Ret;
      A.assign(Args.begin(), Args.end());
    } else {
      parseProto(E.Prototype, R, A);
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
      Call += PNames[I];
    }
    if (!E.ImmChecks.empty()) {
      std::string MacroParams, ExpCall;
      for (unsigned I = 0, N = A.size(); I != N; ++I) {
        if (I) {
          MacroParams += ", ";
          ExpCall += ", ";
        }
        MacroParams += PNames[I];
        ExpCall += "(" + PNames[I] + ")";
      }
      emitImmMacro(OS, R, E.PublicName, MacroParams,
                   E.Builtin + "(" + ExpCall + ")", Doc);
    } else if (R == "void")
      emitFn(OS, R, E.PublicName, Params.empty() ? "void" : Params,
             {E.Builtin + "(" + Call + ");"}, Doc);
    else
      emitFn(OS, R, E.PublicName, Params.empty() ? "void" : Params,
             {"return " + E.Builtin + "(" + Call + ");"}, Doc);
  };

  if (HasPub && E.K != Kind::PairRR2 && E.K != Kind::PairRRA2 &&
      E.K != Kind::ExtV2 && E.K != Kind::ExtV4) {
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
             Doc.empty() ? "Two-result: .hi=rtd1, .lo=rtd2." : Doc);
    else if (PN.starts_with("x4"))
      emitFn(OS, "haydn_dpair_t", PN, "haydn_x4int16 a, haydn_x4int16 b",
             {"haydn_dpair_t r;",
              "r.hi = " + E.Builtin + "(&r.lo, a, b);", "return r;"},
             Doc.empty() ? "Two-result: .hi=rtd1, .lo=rtd2." : Doc);
    else
      emitFn(OS, "haydn_dpair_t", PN, "int64_t a, int64_t b",
             {"haydn_dpair_t r;", "r.hi = " + E.Builtin + "(&r.lo, a, b);",
              "return r;"},
             Doc);
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
             Doc);
    else if (PN.starts_with("x4"))
      emitFn(OS, "haydn_dpair_t", PN,
             "int64_t acc1, int64_t acc2, haydn_x4int16 a, haydn_x4int16 b",
             {"haydn_dpair_t r;",
              "r.hi = " + E.Builtin + "(&r.lo, acc1, acc2, a, b);",
              "return r;"},
             Doc);
    else
      emitFn(OS, "haydn_dpair_t", PN,
             "int64_t acc1, int64_t acc2, int64_t a, int64_t b",
             {"haydn_dpair_t r;",
              "r.hi = " + E.Builtin + "(&r.lo, acc1, acc2, a, b);",
              "return r;"},
             Doc);
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
    return;
  }
}

static void emitSpecials(raw_ostream &OS) {
  OS << "//===----------------------------------------------------------------------===//\n"
        "// Specials (composed / frexp CB / AR helpers)\n"
        "//===----------------------------------------------------------------------===//\n\n";

  emitFn(OS, "haydn_cb_ld_t", "ldw_cb_imm", "int base, int cbr_sel, int stride",
         {"haydn_cb_ld_t r;",
          "r.data = __builtin_haydn_ldw_cb_imm_pair(&r.new_ptr, base, cbr_sel, "
          "stride);",
          "return r;"},
         "ISA: LDW_CB_IMM — circular-buffer load (imm stride).");
  emitFn(OS, "haydn_cb_ld_t", "ldw_cb_reg", "int base, int cbr_sel, int stride",
         {"haydn_cb_ld_t r;",
          "r.data = __builtin_haydn_ldw_cb_reg_pair(&r.new_ptr, base, cbr_sel, "
          "stride);",
          "return r;"},
         "ISA: LDW_CB_REG — circular-buffer load (reg stride).");

  emitFn(OS, "haydn_x2fract32", "mulfp32x16x2ras_low",
         "haydn_x2fract32 acc, haydn_x2fract32 a32, haydn_x4fract16 b16",
         {"return __haydn_i64_as_v2(__builtin_haydn_mulfp32x16x2ras_low("
          "__haydn_v2_as_i64(acc), __haydn_v2_as_i64(a32), "
          "__haydn_v4_as_i64(b16)));"});
  emitFn(OS, "haydn_x2fract32", "mulfp32x16x2ras_high",
         "haydn_x2fract32 acc, haydn_x2fract32 a32, haydn_x4fract16 b16",
         {"return __haydn_i64_as_v2(__builtin_haydn_mulfp32x16x2ras_high("
          "__haydn_v2_as_i64(acc), __haydn_v2_as_i64(a32), "
          "__haydn_v4_as_i64(b16)));"});

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
          "__haydn_v2_as_i64(tw_widened)));"});
  emitFn(OS, "haydn_x2fract32", "mulfc32x16ras_high",
         "haydn_x2fract32 acc, haydn_x2fract32 data, haydn_x4fract16 tw",
         {"int64_t tw_i = __haydn_v4_as_i64(tw);",
          "int64_t tw_hi = (int64_t)(uint32_t)((uint64_t)tw_i >> 32);",
          "haydn_x2int32 tw_v = __haydn_i64_as_v2(tw_hi);",
          "haydn_x2int32 tw_shifted = __builtin_haydn_x2slli32(tw_v, 16);",
          "haydn_x2int32 tw_widened = __builtin_haydn_x2srai32(tw_shifted, 16);",
          "return __haydn_i64_as_v2(__builtin_haydn_mulfc32x16ras_high("
          "__haydn_v2_as_i64(acc), __haydn_v2_as_i64(data), "
          "__haydn_v2_as_i64(tw_widened)));"});

  emitFn(OS, "haydn_x4int16", "d_lqhwua_post",
         "int ptr, int ar_sel, int stride, int dir_sel",
         {"return __haydn_i64_as_v4(__builtin_haydn_d_lqhwua_post("
          "ptr, ar_sel, stride, dir_sel));"});
  emitFn(OS, "haydn_x2int32", "d_ltwua_post",
         "int ptr, int ar_sel, int stride, int dir_sel",
         {"return __haydn_i64_as_v2(__builtin_haydn_d_ltwua_post("
          "ptr, ar_sel, stride, dir_sel));"});
  emitFn(OS, "void", "d_sqhwua_post",
         "haydn_x4int16 data, int ptr, int ar_sel, int stride, int dir_sel",
         {"__builtin_haydn_d_sqhwua_post(__haydn_v4_as_i64(data), ptr, ar_sel, "
          "stride, dir_sel);"});
  emitFn(OS, "void", "d_stwua_post",
         "haydn_x2int32 data, int ptr, int ar_sel, int stride, int dir_sel",
         {"__builtin_haydn_d_stwua_post(__haydn_v2_as_i64(data), ptr, ar_sel, "
          "stride, dir_sel);"});

  emitFn(OS, "haydn_x4int16", "d_lqhwua_post_ip",
         "int *pptr, int ar_sel, int stride, int dir_sel",
         {"haydn_x4int16 v = haydn_d_lqhwua_post(*pptr, ar_sel, stride, dir_sel);",
          "*pptr = dir_sel ? (*pptr - stride) : (*pptr + stride);",
          "return v;"});
  emitFn(OS, "haydn_x2int32", "d_ltwua_post_ip",
         "int *pptr, int ar_sel, int stride, int dir_sel",
         {"haydn_x2int32 v = haydn_d_ltwua_post(*pptr, ar_sel, stride, dir_sel);",
          "*pptr = dir_sel ? (*pptr - stride) : (*pptr + stride);",
          "return v;"});
  emitFn(OS, "void", "d_sqhwua_post_ip",
         "haydn_x4int16 data, int *pptr, int ar_sel, int stride, int dir_sel",
         {"haydn_d_sqhwua_post(data, *pptr, ar_sel, stride, dir_sel);",
          "*pptr = dir_sel ? (*pptr - stride) : (*pptr + stride);"});
  emitFn(OS, "void", "d_stwua_post_ip",
         "haydn_x2int32 data, int *pptr, int ar_sel, int stride, int dir_sel",
         {"haydn_d_stwua_post(data, *pptr, ar_sel, stride, dir_sel);",
          "*pptr = dir_sel ? (*pptr - stride) : (*pptr + stride);"});

  OS << "#define haydn_movad32_h haydn_movad32_high\n"
        "#define haydn_movad32_l haydn_movad32_low\n\n";
}

/// Intrinsic enum suffix: x2mul32_pair → x2mul32
static std::string llvmIntrinSuffix(StringRef Name) {
  if (Name.ends_with("_pair"))
    return Name.drop_back(5).str();
  return Name.str();
}

} // namespace

void clang::EmitHaydnIntrinHeader(const RecordKeeper &Records, raw_ostream &OS) {
  emitPreamble(OS);
  auto Entries = collect(Records);
  OS << "//===----------------------------------------------------------------------===//\n"
        "// Generated from BuiltinsHaydn.td\n"
        "//===----------------------------------------------------------------------===//\n\n";
  StringSet<> Emitted;
  for (const BuiltinEntry &E : Entries)
    emitOne(OS, E, Emitted);
  emitSpecials(OS);
  OS << "#endif /* __HAYDN_H */\n";
}

void clang::EmitHaydnBuiltinCG(const RecordKeeper &Records, raw_ostream &OS) {
  OS << "//===-- haydn_builtin_cg.inc - Generated Haydn builtin CodeGen -------===//\n"
        "//\n"
        "// Automatically generated from BuiltinsHaydn.td. DO NOT EDIT.\n"
        "// Included inside EmitHaydnBuiltinExpr switch (after default:).\n"
        "//\n"
        "// Covers:\n"
        "//   - frexp-pattern _pair / CB load (PairRR2 / PairRRA2 / PairCbLoad)\n"
        "//   - HaydnAeBuiltin CodeGen= recipes (LanewiseUnary / TernaryI64 /\n"
        "//     AddAndSubRng)\n"
        "//\n"
        "//===----------------------------------------------------------------------===//\n\n";

  auto Entries = collect(Records);
  auto caseId = [](const BuiltinEntry &E) {
    if (E.IsAe)
      return std::string("Haydn::BI__builtin_ae_") + E.Name;
    return std::string("Haydn::BI__builtin_haydn_") + E.Name;
  };

  // Frexp-pattern pairs.
  for (const BuiltinEntry &E : Entries) {
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
    }
  }

  // Explicit CodeGen= recipes (primarily HaydnAeBuiltin).
  SmallVector<std::string, 4> AddAndSubCases;
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
    } else if (CG == "AddAndSubRng") {
      AddAndSubCases.push_back(caseId(E));
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
                E.K == Kind::PairCbLoad || E.IsPair;
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
  OS << "// clang-format off\n"
        "#define HAYDN_BUILTIN_SEMA_CASES \\\n";

  // Frexp-pattern pair / CB: warn on null out-pointer constant.
  SmallVector<std::string, 32> PairCases;
  for (const BuiltinEntry &E : Entries) {
    if (!(E.K == Kind::PairRR2 || E.K == Kind::PairRRA2 ||
          E.K == Kind::PairCbLoad ||
          (E.IsPair && StringRef(E.Name).ends_with("_pair"))))
      continue;
    PairCases.push_back(builtinId(E));
  }
  if (!PairCases.empty()) {
    for (const std::string &C : PairCases)
      OS << "  case " << C << ": \\\n";
    OS << "    if (const Expr *Out = TheCall->getArg(0)->IgnoreParenCasts()) { \\\n"
          "      if (Out->isNullPointerConstant(getASTContext(), \\\n"
          "                                     Expr::NPC_ValueDependentIsNotNull)) { \\\n"
          "        Diag(Out->getExprLoc(), diag::warn_null_arg) \\\n"
          "            << Out->getSourceRange(); \\\n"
          "      } \\\n"
          "    } \\\n"
          "    break; \\\n";
  }

  // Immediate ranges from TD ImmChecks, grouped by (ArgIdx, Lo, Hi).
  struct ImmGroup {
    unsigned ArgIdx;
    int Lo, Hi;
    SmallVector<std::string, 8> Cases;
  };
  SmallVector<ImmGroup, 8> Groups;
  for (const BuiltinEntry &E : Entries) {
    for (const ImmCheckSpec &IC : E.ImmChecks) {
      auto *G = llvm::find_if(Groups, [&](const ImmGroup &X) {
        return X.ArgIdx == IC.ArgIdx && X.Lo == IC.Lo && X.Hi == IC.Hi;
      });
      if (G == Groups.end())
        Groups.push_back({IC.ArgIdx, IC.Lo, IC.Hi, {builtinId(E)}});
      else
        G->Cases.push_back(builtinId(E));
    }
  }

  for (const ImmGroup &G : Groups) {
    for (const std::string &C : G.Cases)
      OS << "  case " << C << ": \\\n";
    // NEON/ARM style: require a constant imm in range [Lo, Hi].
    OS << "    if (SemaRef.BuiltinConstantArgRange(TheCall, " << G.ArgIdx
       << ", " << G.Lo << ", " << G.Hi << ")) \\\n"
          "      return true; \\\n"
          "    break; \\\n";
  }

  OS << "\n// clang-format on\n";
}
