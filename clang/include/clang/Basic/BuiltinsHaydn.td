//==- BuiltinsHaydn.td - Haydn Builtin function database ---*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Haydn DSP-specific builtin function database.
// Every def NAME must exactly match the corresponding LLVM intrinsic suffix
// (after "llvm.haydn.") so that ClangBuiltin<> auto-mapping works.
//
//===----------------------------------------------------------------------===//

include "clang/Basic/BuiltinsBase.td"

//===----------------------------------------------------------------------===//
// Shared Haydn intrinsic record (one source for builtins + public header).
//
// Fields used by:
//   -gen-clang-builtins          → BuiltinsHaydn.inc  (Spellings, Prototype)
//   -gen-haydn-intrin-header     → haydn.h     (Public*, Mnemonic, …)
//
// Also same TD:
//   -gen-haydn-builtin-codegen   → haydn_builtin_cg.inc
//   -gen-haydn-builtin-sema      → haydn_builtin_sema.inc
//
// Public* / Mnemonic / Semantics are filled in this TD (from ISA golden DB
// offline). Emitter still defaults empty fields from NAME + Prototype.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Immediate checks (NEON ImmCheck style, fixed ranges for Haydn DR64 ISA)
//
// Usage (same shape as arm_neon ImmCheck<arg, kind>):
//   let ImmChecks = [ImmCheck<1, ImmCheck0_31>];
//
// Sema: -gen-haydn-builtin-sema emits range checks via BuiltinConstantArg-
// style diagnostics (err_argument_invalid_range) when the arg is a constant.
//===----------------------------------------------------------------------===//

class ImmCheckType<int lo, int hi> {
  int Lo = lo;
  int Hi = hi;
}

// Named ranges mirror arm_immcheck_incl.td fixed windows used by X2/X4 ISA.
def ImmCheck0_15 : ImmCheckType<0, 15>;  // uimm4 — X4 16-bit lane / seli
def ImmCheck0_31 : ImmCheckType<0, 31>;  // uimm5 — X2 32-bit lane shift
def ImmCheck0_63 : ImmCheckType<0, 63>;  // uimm6 — 64-bit / pack shift
def ImmCheck0_1  : ImmCheckType<0, 1>;   // 1-bit selects
def ImmCheck0_3  : ImmCheckType<0, 3>;   // 2-bit selects (ar_sel / cbr pair)
def ImmCheckSimm6 : ImmCheckType<-32, 31>; // simm6 — LS POST/PRE/WITH element index
def ImmCheckSimm8 : ImmCheckType<-128, 127>; // simm8 — CB IMM stride element index

/// NEON-style: ImmCheck<ImmArgIdx, ImmCheckType>.
class ImmCheck<int immArgIdx, ImmCheckType kind> {
  int ImmArgIdx = immArgIdx;
  ImmCheckType Kind = kind;
}

// Public API surface for haydn.h wrappers.
class HaydnPublicAPI {
  // Public C name without "haydn_" prefix. Empty → strip trailing "_pair"
  // from NAME (e.g. x2mul32_pair → x2mul32).
  string PublicName = "";

  // Public C prototype using haydn_* types where applicable, e.g.
  //   "haydn_x2int32(haydn_x2int32, haydn_x2int32)"
  //   "haydn_dpair_t(haydn_x2int32, haydn_x2int32)"
  // Empty → derive from Prototype (i64 ↔ vector classification).
  string PublicPrototype = "";

  // ISA mnemonic for docs / tooling (e.g. "X2ADD32S", "MUL64_LL").
  // Empty → uppercase(NAME) with "_pair" stripped.
  string Mnemonic = "";

  // Short semantic description for /// comments (behavior / ports summary).
  // Empty → no extra doc line.
  string Semantics = "";

  // Optional EmitHaydnBuiltinExpr recipe (HaydnAeBuiltin / specials). Empty =
  // no generated case (1:1 HaydnBuiltin uses ClangBuiltin auto-map instead).
  // Forms:
  //   "LanewiseUnary:<llvm_suffix>"  — i64 bag ↔ v4i16, unary intrinsic
  //   "TernaryI64:<llvm_suffix>"     — three i64 args, i64 result
  //   "AddAndSubRng"                 — composed AE_ADDANDSUBRNG16RAS
  // Frexp _pair builtins are detected from name/class, not this field.
  string CodeGen = "";

  // NEON-style immediate argument range checks for Sema
  // (→ haydn_builtin_sema.inc). Empty = no imm range check.
  // ImmArg ops (non-empty ImmChecks) MUST have a DB reg-form twin when the
  // ISA encodes both (e.g. x2srai32 + x2sra32, x4seli16 + x4sel16). Variable
  // shift/mask amount uses the reg twin — do not soft-invent a third API.
  list<ImmCheck> ImmChecks = [];
}

class HaydnBuiltin<string prototype, string features = "">
    : TargetBuiltin, HaydnPublicAPI {
  let Spellings = ["__builtin_haydn_" # NAME];
  let Prototype = prototype;
  let Features = features;
}

// HiFi AE_* builtins as target builtins (`__builtin_ae_*`). Composed /
// load-store expansions live in EmitHaydnBuiltinExpr (Haydn.cpp).
class HaydnAeBuiltin<string prototype, string features = "">
    : TargetBuiltin, HaydnPublicAPI {
  let Spellings = ["__builtin_ae_" # NAME];
  let Prototype = prototype;
  let Features = features;
}

// 2-dest frexp-pattern builtins (`__builtin_haydn_<NAME>` with _pair suffix
// on NAME). Public API returns haydn_dpair_t; clang Prototype stays frexp.
class HaydnPairBuiltin<string prototype, string features = "">
    : TargetBuiltin, HaydnPublicAPI {
  let Spellings = ["__builtin_haydn_" # NAME];
  let Prototype = prototype;
  let Features = features;
}

let Attributes = [NoThrow, Const] in {

//===----------------------------------------------------------------------===//
// 64-bit Multiply (MUL64) -- 16 variants
// All: (DR64, DR64) -> DR64
//===----------------------------------------------------------------------===//

// Signed-signed: _ll, _lh, _hl, _hh
def mul64_ss_ll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_ss_ll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_LL";
  let Semantics = "signed×signed multiply, low×low 32-bit lanes → 64-bit product";
}
def mul64_ss_lh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let Mnemonic = "MUL64_LH";
  let Semantics = "signed×signed multiply, low×high lanes → 64-bit product";
}
def mul64_ss_hl : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let Mnemonic = "MUL64_HL";
}
def mul64_ss_hh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let Mnemonic = "MUL64_HH";
}

// Signed-unsigned: _lul, _ulh, _uhl, _uhh
def mul64_su_lul : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_su_lul";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_LUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned. The full 64-bit product is written to rtd.";
}
def mul64_su_ulh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_su_ulh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed. The full 64-bit product is written to rtd.";
}
def mul64_su_uhl : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_su_uhl";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_UHL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed. The full 64-bit product is written to rtd.";
}
def mul64_su_uhh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_su_uhh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_UHH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed. The full 64-bit product is written to rtd.";
}
def mul64_us_luh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_us_luh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_LUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned. The full 64-bit product is written to rtd.";
}
def mul64_us_uhuh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_us_uhuh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_UHUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2. The full 64-bit product is written to rtd.";
}
def mul64_us_hul : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_us_hul";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_HUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned. The full 64-bit product is written to rtd.";
}
def mul64_us_uhul : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_us_uhul";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_UHUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2. The full 64-bit product is written to rtd.";
}
def mul64_uu_uluh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_uu_uluh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_ULUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2. The full 64-bit product is written to rtd.";
}
def mul64_uu_ulul : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_uu_ulul";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_ULUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2. The full 64-bit product is written to rtd.";
}
def mul64_uu_ull : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_uu_ull";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_ULL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed. The full 64-bit product is written to rtd.";
}
def mul64_uu_ulh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_uu_ulh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed. The full 64-bit product is written to rtd.";
}
def mula64_ss_ll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_ss_ll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_LL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, and accumulates the 64-bit product into the 64-bit target register rtd.";
}
def mula64_ss_lh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_ss_lh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_LH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_ss_hl : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_ss_hl";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_HL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_ss_hh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_ss_hh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_HH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_su_lul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_su_lul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_LUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, and accumulates the 64-bit product into rtd.";
}
def mula64_su_ulh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_su_ulh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, and accumulates the 64-bit product into rtd.";
}
def mula64_su_uhl : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_su_uhl";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_UHL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, and accumulates the 64-bit product into rtd.";
}
def mula64_su_uhh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_su_uhh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_UHH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, and accumulates the 64-bit product into rtd.";
}
def mula64_us_luh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_us_luh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_LUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned, and accumulates the 64-bit product into rtd.";
}
def mula64_us_uhuh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_us_uhuh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_UHUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_us_hul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_us_hul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_HUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, and accumulates the 64-bit product into rtd.";
}
def mula64_us_uhul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_us_uhul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_UHUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_uu_uluh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_uu_uluh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_ULUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_uu_ulul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_uu_ulul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_ULUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_uu_ull : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_uu_ull";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_ULL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, and accumulates the 64-bit product into rtd.";
}
def mula64_uu_ulh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_uu_ulh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, and accumulates the 64-bit product into rtd.";
}
def muls64_ss_ll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_ss_ll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_LL";
  let Semantics = "signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, then subtracts the 64-bit product from the 64-bit accumulator rtd.";
}
def muls64_ss_lh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_ss_lh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_LH";
  let Semantics = "signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, then subtracts the product from rtd.";
}
def muls64_ss_hl : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_ss_hl";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_HL";
  let Semantics = "signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, then subtracts the product from rtd.";
}
def muls64_ss_hh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_ss_hh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_HH";
  let Semantics = "signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2, then subtracts the product from rtd.";
}
def muls64_su_lul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_su_lul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_LUL";
  let Semantics = "signed × unsigned multiplication using the lower 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, then subtracts the product from rtd.";
}
def muls64_su_ulh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_su_ulh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_ULH";
  let Semantics = "unsigned × signed multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, then subtracts the product from rtd.";
}
def muls64_su_uhl : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_su_uhl";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_UHL";
  let Semantics = "unsigned × signed multiplication using the upper 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, then subtracts the product from rtd.";
}
def muls64_su_uhh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_su_uhh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_UHH";
  let Semantics = "unsigned × signed multiplication using the upper 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, then subtracts the product from rtd.";
}
def muls64_us_luh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_us_luh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_LUH";
  let Semantics = "signed × unsigned multiplication using the lower 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned, then subtracts the product from rtd.";
}
def muls64_us_uhuh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_us_uhuh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_UHUH";
  let Semantics = "unsigned × unsigned multiplication using the upper 32 bits of rsd1 and rsd2, then subtracts the product from rtd.";
}
def muls64_us_hul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_us_hul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_HUL";
  let Semantics = "signed × unsigned multiplication using the upper 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, then subtracts the product from rtd.";
}
def muls64_us_uhul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_us_uhul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_UHUL";
  let Semantics = "unsigned × unsigned multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, then subtracts the product from rtd.";
}
def muls64_uu_uluh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_uu_uluh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_ULUH";
  let Semantics = "unsigned × unsigned multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, then subtracts the product from rtd.";
}
def muls64_uu_ulul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_uu_ulul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_ULUL";
  let Semantics = "unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, then subtracts the product from rtd.";
}
def muls64_uu_ull : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_uu_ull";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_ULL";
  let Semantics = "unsigned × signed multiplication using the lower 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, then subtracts the product from rtd.";
}
def muls64_uu_ulh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_uu_ulh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_ULH";
  let Semantics = "unsigned × signed multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, then subtracts the product from rtd.";
}
def mulas64_ss_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_ss_ll";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_LL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, then adds the 64-bit product to the 64-bit accumulator rtd with signed 64-bit saturation.";
}
def mulas64_ss_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_ss_lh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_LH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_ss_hl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_ss_hl";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_HL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_ss_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_ss_hh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_HH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_su_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_su_lul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_LUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, then adds the 64-bit product to rtd with signed 64-b...";
}
def mulas64_su_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_su_ulh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, then adds the 64-bit product to rtd with signed 64-...";
}
def mulas64_su_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_su_uhl";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_UHL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, then adds the 64-bit product to rtd with signed 64-...";
}
def mulas64_su_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_su_uhh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_UHH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, then adds the 64-bit product to rtd with signed 64-...";
}
def mulas64_us_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_us_luh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_LUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned, then adds the 64-bit product to rtd with signed 64-b...";
}
def mulas64_us_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_us_uhuh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_UHUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_us_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_us_hul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_HUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, then adds the 64-bit product to rtd with signed 64-b...";
}
def mulas64_us_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_us_uhul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_UHUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_uu_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_uu_uluh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_ULUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_uu_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_uu_ulul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_ULUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, then adds the 64-bit product to the 64-bit accumulator rtd with signed 64-bit saturation.";
}
def mulas64_uu_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_uu_ull";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_ULL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, then adds the 64-bit product to rtd with signed 64-...";
}
def mulas64_uu_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_uu_ulh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, then adds the 64-bit product to rtd with signed 64-...";
}
def mulss64_ss_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_ss_ll";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_LL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2. The 64-bit product is subtracted from the 64-bit accumulator rtd, and the result is written back ...";
}
def mulss64_ss_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_ss_lh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_LH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2. The 64-bit product is subtracted from the 64-bit accumulator rtd, and the re...";
}
def mulss64_ss_hl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_ss_hl";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_HL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2. The 64-bit product is subtracted from the 64-bit accumulator rtd, and the re...";
}
def mulss64_ss_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_ss_hh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_HH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the upper 32 bits of rsd2. The 64-bit product is subtracted from the 64-bit accumulator rtd, and the re...";
}
def mulss64_su_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_su_lul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_LUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 (signed) and the lower 32 bits of rsd2 (unsigned). The 64-bit product is subtracted from rtd, and the res...";
}
def mulss64_su_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_su_ulh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 (unsigned) and the upper 32 bits of rsd2 (signed). The 64-bit product is subtracted from rtd, and the re...";
}
def mulss64_su_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_su_uhl";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_UHL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 (unsigned) and the lower 32 bits of rsd2 (signed). The 64-bit product is subtracted from rtd, and the re...";
}
def mulss64_su_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_su_uhh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_UHH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 (unsigned) and the upper 32 bits of rsd2 (signed). The 64-bit product is subtracted from rtd, and the re...";
}
def mulss64_us_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_us_luh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_LUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 (signed) and the upper 32 bits of rsd2 (unsigned). The 64-bit product is subtracted from rtd, and the res...";
}
def mulss64_us_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_us_uhuh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_UHUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the upper 32 bits of rsd2. The 64-bit product is subtracted from rtd, and the result is written ba...";
}
def mulss64_us_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_us_hul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_HUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 (signed) and the lower 32 bits of rsd2 (unsigned). The 64-bit product is subtracted from rtd, and the res...";
}
def mulss64_us_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_us_uhul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_UHUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2. The 64-bit product is subtracted from rtd, and the result is written ba...";
}
def mulss64_uu_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_uu_uluh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_ULUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2. The 64-bit product is subtracted from rtd, and the result is written ba...";
}
def mulss64_uu_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_uu_ulul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_ULUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the lower 32 bits of rsd2. The 64-bit product is subtracted from rtd, and the result is written ba...";
}
def mulss64_uu_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_uu_ull";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_ULL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 (interpreted as unsigned) and the lower 32 bits of rsd2 (interpreted as signed). The 64-bit product is s...";
}
def mulss64_uu_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_uu_ulh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 (unsigned) and the upper 32 bits of rsd2 (signed). The 64-bit product is subtracted from rtd, and the re...";
}
def add32s : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "add32s";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "ADD32S";
  let Semantics = "32-bit add with signed saturation.";
}
def sub32s : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "sub32s";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "SUB32S";
  let Semantics = "32-bit subtract with signed saturation.";
}
def add64s : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "add64s";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "ADD64S";
  let Semantics = "performs a 64-bit signed addition of rsd1 and rsd2, and writes the result to rtd with signed 64-bit saturation (SAT64).";
}
def sub64s : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "sub64s";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SUB64S";
  let Semantics = "performs a 64-bit signed subtraction of rsd2 from rsd1, and writes the result to rtd with signed 64-bit saturation (SAT64).";
}
def abs32s : HaydnBuiltin<"int(int)"> {
  let PublicName = "abs32s";
  let PublicPrototype = "int(int)";
  let Mnemonic = "ABS32S";
  let Semantics = "computes the absolute value of rs as a signed 32-bit integer, with saturation of the result. If rs = 0x80000000, the result saturates to 0x7FFFFFFF.";
}
// Golden slot0 scalar-32 shifts (IMM RI5 + REG RR). REG amount is signed
// bidirectional simm in rs2; IMM is unsigned one-way uimm5.
def slli32 : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "slli32";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "SLLI32";
  let ImmChecks = [ImmCheck<1, ImmCheck0_31>];
  let Semantics = "32-bit logical left by uimm5 (golden SLLI32). Variable: haydn_sll32.";
}
def srli32 : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "srli32";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "SRLI32";
  let ImmChecks = [ImmCheck<1, ImmCheck0_31>];
  let Semantics = "32-bit logical right by uimm5 (golden SRLI32). Variable: haydn_srl32.";
}
def srai32 : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "srai32";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "SRAI32";
  let ImmChecks = [ImmCheck<1, ImmCheck0_31>];
  let Semantics = "32-bit arithmetic right by uimm5 (golden SRAI32). Variable: haydn_sra32.";
}
def srai32r : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "srai32r";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "SRAI32R";
  let ImmChecks = [ImmCheck<1, ImmCheck0_31>];
  let Semantics = "32-bit arithmetic right with rounding by uimm5 (golden SRAI32R). Variable: haydn_sra32r.";
}
def sll32 : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "sll32";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "SLL32";
  let Semantics = "32-bit variable left/right logical by signed amount in rs2 (golden SLL32). Imm: haydn_slli32.";
}
def srl32 : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "srl32";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "SRL32";
  let Semantics = "32-bit variable right/left logical by signed amount in rs2 (golden SRL32). Imm: haydn_srli32.";
}
def sra32 : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "sra32";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "SRA32";
  let Semantics = "32-bit variable ASR/left by signed amount in rs2 (golden SRA32). Imm: haydn_srai32.";
}
def sra32r : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "sra32r";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "SRA32R";
  let Semantics = "32-bit variable ASR+round/left by signed amount in rs2 (golden SRA32R). Imm: haydn_srai32r.";
}
def neg32s : HaydnBuiltin<"int(int)"> {
  let PublicName = "neg32s";
  let PublicPrototype = "int(int)";
  let Mnemonic = "NEG32S";
  let Semantics = "computes the two's complement negation of rs with saturation to the signed 32-bit range. If rs = 0x80000000, the result saturates to 0x7FFFFFFF; otherwise, the result is equal to -rs.";
}
def abs64s : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "abs64s";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "ABS64S";
  let Semantics = "computes the absolute value of the 64-bit signed value in rsd, and writes the result to rtd with signed 64-bit saturation (SAT64). If rsd is the most negative 64-bit value (0x8000_0000_0000_0000), ...";
}
def neg64s : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "neg64s";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "NEG64S";
  let Semantics = "computes the two's complement negation of the 64-bit signed value in rsd, and writes the result to rtd with signed 64-bit saturation (SAT64). If rsd is the most negative 64-bit value (0x8000_0000_0...";
}
def addbrba32 : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "addbrba32";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "BREV32";
  let Semantics = "performs bit-reversed addressing on rs1 with an additional linear offset rs2. All operations are on 32-bit unsigned values. The bits of rs1 are reversed, the offset rs2 is added, and then the resul...";
}
def abs64 : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "abs64";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "ABS64";
  let Semantics = "computes the absolute value of the 64-bit signed integer in rsd, and writes the result to rtd. The operand rsd is interpreted as a signed 64-bit two's complement integer. No saturation is applied; ...";
}
def neg64 : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "neg64";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "NEG64";
  let Semantics = "computes the two's complement negation of the 64-bit signed value in rsd, and writes the result to rtd.";
}
def fmul32s_ll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "fmul32s_ll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FMUL32S_LL";
  let Semantics = "performs a signed fixed-point multiplication between the lower 32-bit Q1.31 operands of rsd1 and rsd2, and writes the saturated Q1.63 result to rtd. rsd1[31:0] and rsd2[31:0] are interpreted as sig...";
}
def fmul32s_lh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "fmul32s_lh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FMUL32S_LH";
  let Semantics = "performs a signed fixed-point multiplication between the lower 32-bit Q1.31 operand of rsd1 and the upper 32-bit Q1.31 operand of rsd2, and writes the saturated Q1.63 result to rtd. rsd1[31:0] and ...";
}
def fmul32s_hh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "fmul32s_hh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FMUL32S_HH";
  let Semantics = "performs a signed fixed-point multiplication between the upper 32-bit Q1.31 operands of rsd1 and rsd2, and writes the saturated Q1.63 result to rtd. rsd1[63:32] and rsd2[63:32] are interpreted as s...";
}
def fmula32s_ll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "fmula32s_ll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FMULA32S_LL";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation. The lower 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit intermediate product in Q2.62 format. The final sum ...";
}
def fmula32s_lh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "fmula32s_lh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FMULA32S_LH";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation. The lower 32-bit Q1.31 operand of rsd1 and the upper 32-bit Q1.31 operand of rsd2 are multiplied to produce a 64-bit intermediate produc...";
}
def fmula32s_hh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "fmula32s_hh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FMULA32S_HH";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation. The upper 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit intermediate product in Q2.62 format. The final sum ...";
}
def fmuls32s_ll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "fmuls32s_ll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FMULS32S_LL";
  let Semantics = "performs a signed fixed-point multiply-subtract operation with Q1.63 saturation. The lower 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit product, which is converted to Q...";
}
def fmuls32s_lh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "fmuls32s_lh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FMULS32S_LH";
  let Semantics = "performs a signed fixed-point multiply-subtract operation with Q1.63 saturation. The lower 32-bit Q1.31 operand of rsd1 and the upper 32-bit Q1.31 operand of rsd2 are multiplied to produce a 64-bit...";
}
def fmuls32s_hh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "fmuls32s_hh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FMULS32S_HH";
  let Semantics = "performs a signed fixed-point multiply-subtract operation with Q1.63 saturation. The upper 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit product, which is converted to Q...";
}
def ff2mul32rs_ll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mul32rs_ll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MUL32RS_LL";
  let Semantics = "performs a signed 32×32 fixed-point multiplication on the lower 32-bit lanes of rsd1 and rsd2, treating them as Q1.31 values. The 64-bit product is interpreted as Q2.62 and then converted to Q17.47...";
}
def ff2mul32rs_lh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mul32rs_lh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MUL32RS_LH";
  let Semantics = "performs a signed 32×32 fixed-point multiplication on the lower 32-bit lane of rsd1 and the upper 32-bit lane of rsd2, treating them as Q1.31 values. The 64-bit product is interpreted as Q2.62 and ...";
}
def ff2mul32rs_hh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mul32rs_hh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MUL32RS_HH";
  let Semantics = "performs a signed 32×32 fixed-point multiplication on the upper 32-bit lanes of rsd1 and rsd2, treating them as Q1.31 values. The 64-bit product is interpreted as Q2.62 and then converted to Q17.47...";
}
def ff2mula32rs_ll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mula32rs_ll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULA32RS_LL";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation. The lower 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit intermediate product in Q2.62 format. The product is...";
}
def ff2mula32rs_lh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mula32rs_lh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULA32RS_LH";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation. The lower 32-bit Q1.31 operand of rsd1 and the upper 32-bit Q1.31 operand of rsd2 are multiplied to produce a 64-bit intermediate produc...";
}
def ff2mula32rs_hh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mula32rs_hh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULA32RS_HH";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation. The upper 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit intermediate product in Q2.62 format. The product is...";
}
def ff2muls32rs_ll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2muls32rs_ll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULS32RS_LL";
  let Semantics = "performs a signed fixed-point multiply-subtract operation in Q17.47 format. The lower 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit intermediate product in Q2.62 format....";
}
def ff2muls32rs_lh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2muls32rs_lh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULS32RS_LH";
  let Semantics = "performs a signed fixed-point multiply-subtract operation in Q17.47 format. The lower 32-bit Q1.31 operand of rsd1 and the upper 32-bit Q1.31 operand of rsd2 are multiplied to produce a 64-bit inte...";
}
def ff2muls32rs_hh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2muls32rs_hh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULS32RS_HH";
  let Semantics = "performs a signed fixed-point multiply-subtract operation in Q17.47 format. The upper 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit intermediate product in Q2.62 format....";
}
def ff2mul32r_ll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mul32r_ll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MUL32R_LL";
  let Semantics = "performs a signed 32×32 fixed-point multiplication on the lower 32-bit lanes of rsd1 and rsd2, treating them as Q1.31 values. The 64-bit product is interpreted as Q2.62 and then converted to Q17.47...";
}
def ff2mul32r_lh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mul32r_lh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MUL32R_LH";
  let Semantics = "performs a signed 32×32 fixed-point multiplication on the lower 32-bit lane of rsd1 and the upper 32-bit lane of rsd2, treating them as Q1.31 values. The 64-bit product is interpreted as Q2.62 and ...";
}
def ff2mul32r_hh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mul32r_hh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MUL32R_HH";
  let Semantics = "performs a signed 32×32 fixed-point multiplication on the upper 32-bit lanes of rsd1 and rsd2, treating them as Q1.31 values. The 64-bit product is interpreted as Q2.62 and then converted to Q17.47...";
}
def ff2mula32r_ll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mula32r_ll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULA32R_LL";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation. The lower 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit intermediate product in Q2.62 format. The product is...";
}
def ff2mula32r_lh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mula32r_lh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULA32R_LH";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation. The lower 32-bit Q1.31 operand of rsd1 and the upper 32-bit Q1.31 operand of rsd2 are multiplied to produce a 64-bit intermediate produc...";
}
def ff2mula32r_hh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2mula32r_hh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULA32R_HH";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation. The upper 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit intermediate product in Q2.62 format. The product is...";
}
def ff2muls32r_ll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2muls32r_ll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULS32R_LL";
  let Semantics = "performs a signed fixed-point multiply-subtract operation in Q17.47 format. The lower 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit intermediate product in Q2.62 format....";
}
def ff2muls32r_lh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2muls32r_lh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULS32R_LH";
  let Semantics = "performs a signed fixed-point multiply-subtract operation in Q17.47 format. The lower 32-bit Q1.31 operand of rsd1 and the upper 32-bit Q1.31 operand of rsd2 are multiplied to produce a 64-bit inte...";
}
def ff2muls32r_hh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "ff2muls32r_hh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FF2MULS32R_HH";
  let Semantics = "performs a signed fixed-point multiply-subtract operation in Q17.47 format. The upper 32-bit Q1.31 operands of rsd1 and rsd2 are multiplied to produce a 64-bit intermediate product in Q2.62 format....";
}
def mulfp32x16x2ras_low : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulfp32x16x2ras_low";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULFP32X16X2RAS_LOW";
}
def mulfp32x16x2ras_high : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulfp32x16x2ras_high";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULFP32X16X2RAS_HIGH";
}
def mull : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "mull";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "MULL";
  let Semantics = "performs a 32-bit × 32-bit multiplication and writes the lower 32 bits of the 64-bit product to the target register.";
}
def mulssh : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "mulssh";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "MULSSH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication and writes the upper 32 bits of the 64-bit product to the target register.";
}
def mulsuh : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "mulsuh";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "MULSUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication. rs1 is treated as a signed 32-bit integer and rs2 as an unsigned 32-bit integer. The upper 32 bits of the 64-bit product are written to ...";
}
def muluuh : HaydnBuiltin<"unsigned int(unsigned int, unsigned int)"> {
  let PublicName = "muluuh";
  let PublicPrototype = "unsigned int(unsigned int, unsigned int)";
  let Mnemonic = "MULUUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication and writes the upper 32 bits of the 64-bit product to the target register.";
}
def mulq31 : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "mulq31";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "MULQ31";
}
def macq31 : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "macq31";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "MACQ31";
}
def mulq63 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulq63";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULQ63";
}
def mac32 : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "mac32";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "MAC32";
}
def x2mula32_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2mula32";
  let PublicPrototype = "haydn_dpair_t(int64_t, int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MULA32";
  let Semantics = "performs two parallel 32-bit × 32-bit signed multiply-accumulate operations. The product of the high 32-bit sub-words of rsd1 and rsd2 is added to the 64-bit accumulator rtd1. The product of the lo...";
}
def x2muls32_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2muls32";
  let PublicPrototype = "haydn_dpair_t(int64_t, int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MULS32";
  let Semantics = "performs two parallel 32-bit × 32-bit signed multiply-subtract operations. The product of the high 32-bit sub-words of rsd1 and rsd2 is subtracted from the 64-bit accumulator rtd1. The product of t...";
}
def x4mula16_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4mula16";
  let PublicPrototype = "haydn_dpair_t(int64_t, int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4MULA16";
  let Semantics = "performs four parallel signed 16×16-bit multiply-accumulate operations on packed 16-bit operands. Each source register rsdX is interpreted as four signed 16-bit integers: rsdX[63:48] - lane 3 (sign...";
}
def x4muls16_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4muls16";
  let PublicPrototype = "haydn_dpair_t(int64_t, int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4MULS16";
  let Semantics = "performs four parallel signed 16×16-bit multiply-subtract operations on packed 16-bit operands. Each source register rsdX is interpreted as four signed 16-bit integers: rsdX[63:48] - lane 3 (signed...";
}
def x4mula16s_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4mula16s";
  let PublicPrototype = "haydn_dpair_t(int64_t, int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4MULA16S";
  let Semantics = "performs four parallel signed 16×16-bit multiply-accumulate operations on packed 16-bit operands with 32-bit signed saturation on each lane. Each source register rsdX is interpreted as four signed ...";
}
def x4muls16s_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4muls16s";
  let PublicPrototype = "haydn_dpair_t(int64_t, int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4MULS16S";
  let Semantics = "performs four parallel signed 16×16-bit multiply-subtract operations on packed 16-bit operands with 32-bit signed saturation on each lane. Each source register rsdX is interpreted as four signed 16...";
}
def log2 : HaydnBuiltin<"int(int)"> {
  let PublicName = "log2";
  let PublicPrototype = "int(int)";
  let Mnemonic = "LOG2";
  let Semantics = "Fixed-point base-2 logarithm. Interprets rs as an unsigned Q1.31 fixed-point value in the range [1, 2) and computes log2(rs). The result is written to rt as an unsigned Q32 fixed-point value in the...";
}
def exp2 : HaydnBuiltin<"int(int)"> {
  let PublicName = "exp2";
  let PublicPrototype = "int(int)";
  let Mnemonic = "EXP2";
  let Semantics = "Fixed-point base-2 exponentiation. Interprets rs as an unsigned Q32 fixed-point value in the range [0, 1) and computes 2^rs. The result is written to rt as an unsigned Q1.31 fixed-point value in th...";
}
def recip : HaydnBuiltin<"int(int)"> {
  let PublicName = "recip";
  let PublicPrototype = "int(int)";
  let Mnemonic = "RECIP";
  let Semantics = "Fixed-point reciprocal. Interprets rs as an unsigned Q1.31 fixed-point offset from 1.0 in the range [1, 2) and computes 1/rs. The result is written to rt as an unsigned Q1.31 fixed-point value in t...";
}
def sqrt : HaydnBuiltin<"int(int)"> {
  let PublicName = "sqrt";
  let PublicPrototype = "int(int)";
  let Mnemonic = "SQRT";
  let Semantics = "Fixed-point square root. Interprets rs as an unsigned Q1.31 fixed-point offset from 1.0 in the range [1, 2) and computes sqrt(rs). The result is written to rt as an unsigned Q1.31 fixed-point value...";
}
def nsa32 : HaydnBuiltin<"int(int)"> {
  let PublicName = "nsa32";
  let PublicPrototype = "int(int)";
  let Mnemonic = "NSA32";
  let Semantics = "returns the number of left shifts required to normalize a signed 32-bit two's complement value rs, i.e., the number of leading redundant sign bits. equivalently, it counts the number of leading sig...";
}
def nsau32 : HaydnBuiltin<"int(int)"> {
  let PublicName = "nsau32";
  let PublicPrototype = "int(int)";
  let Mnemonic = "NSAU32";
  let Semantics = "returns the number of leading zeros of rs when interpreted as an unsigned 32-bit integer. This is typically used to derive a shift amount for normalization of unsigned data.";
}
def nsa64 : HaydnBuiltin<"int(int)"> {
  let PublicName = "nsa64";
  let PublicPrototype = "int(int)";
  let Mnemonic = "NSA64";
  let Semantics = "returns the number of leading sign bits of rsd when interpreted as a signed 64-bit integer, minus one. This is typically used to derive a shift amount for normalization of signed data. The result r...";
}
def nsa16_l : HaydnBuiltin<"int(int)"> {
  let PublicName = "nsa16_l";
  let PublicPrototype = "int(int)";
  let Mnemonic = "NSA16_L";
  let Semantics = "returns the number of leading sign bits of the lower 16-bit portion of rsd when interpreted as a signed 16-bit integer, minus one. This is typically used to derive a shift amount for normalization ...";
}
def nsa32_l : HaydnBuiltin<"int(int)"> {
  let PublicName = "nsa32_l";
  let PublicPrototype = "int(int)";
  let Mnemonic = "NSA32_L";
  let Semantics = "returns the number of leading sign bits of the lower 32-bit portion of rsd when interpreted as a signed 32-bit integer, minus one. This is typically used to derive a shift amount for normalization ...";
}
def nsaz64 : HaydnBuiltin<"int(int)"> {
  let PublicName = "nsaz64";
  let PublicPrototype = "int(int)";
  let Mnemonic = "NSAZ64";
  let Semantics = "calculates the left shift amount that will normalize the two's complement contents of rsd when interpreted as a signed 64-bit integer, and writes the amount to rt. The result ranges from 0 to 63. I...";
}
def nsaz16_l : HaydnBuiltin<"int(int)"> {
  let PublicName = "nsaz16_l";
  let PublicPrototype = "int(int)";
  let Mnemonic = "NSAZ16_L";
  let Semantics = "calculates the left shift amount that will normalize the two's complement contents of the lower 16-bit portion of rsd (i.e., rsd[15:0]) when interpreted as a signed 16-bit integer, and writes the a...";
}
def nsaz32_l : HaydnBuiltin<"int(int)"> {
  let PublicName = "nsaz32_l";
  let PublicPrototype = "int(int)";
  let Mnemonic = "NSAZ32_L";
  let Semantics = "calculates the left shift amount that will normalize the two's complement contents of the lower 32-bit portion of rsd when interpreted as a signed 32-bit integer, and writes the amount to rt. The r...";
}
def mulsa32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulsa32_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULSA32_HHLL";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications, subtracts the first 64-bit product from the 64-bit accumulator rtd, and adds the second 64-bit product to the result. No saturation is ...";
}
def mulsa32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulsa32_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULSA32_HLLH";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications, subtracts the first 64-bit product from the 64-bit accumulator rtd, and adds the second 64-bit product to the result. No saturation is ...";
}
def mulss32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulss32_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULSS32_HHLL";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications and accumulates both 64-bit products into the 64-bit accumulator rtd by subtraction. No saturation is performed; the accumulation uses 6...";
}
def mulss32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulss32_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULSS32_HLLH";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications and accumulates both 64-bit products into the 64-bit accumulator rtd by subtraction. No saturation is performed; the accumulation uses 6...";
}
// NOTE: satsr64 / packsr32 / packsr32x2_* are NOT golden encodings.
// They are composites (SRA64/SRA64R/X2SRA32R+X2SEL32_* or soft sat) and live
// only in haydn_dsp.h — do not re-add as builtins/intrinsics.
// Golden MAXABS32S: dual 32-bit abs+max lanes (same shape as X2 abs/max family).
def maxabs32s : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "maxabs32s";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MAXABS32S";
}
def fmul16_hs00 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_hs00";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_HS00";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the upper 32 bits of rtd. Each 64-bit source r...";
}
def fmulaa16_hs_11_00 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulaa16_hs_11_00";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULAA16_HS_11_00";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation with Q1.31 saturation into the upper 32 bits of rtd. Each 64-bit source register rsdX is treated as four signed 16-bit Q1.15 lanes. For ea...";
}
def fmulss16_hs_11_00 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulss16_hs_11_00";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULSS16_HS_11_00";
  let Semantics = "perform a signed fixed-point multiply-subtract operation with Q1.31 saturation on 16-bit lanes, accumulating into the upper 32 bits of rtd. Each 64-bit source register rsdX is treated as four signe...";
}
def fmul16_hs01 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_hs01";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_HS01";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the upper 32 bits of rtd.";
}
def fmul16_hs02 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_hs02";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_HS02";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the upper 32 bits of rtd.";
}
def fmul16_hs03 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_hs03";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_HS03";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the upper 32 bits of rtd.";
}
def fmul16_hs11 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_hs11";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_HS11";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the upper 32 bits of rtd.";
}
def fmul16_hs12 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_hs12";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_HS12";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the upper 32 bits of rtd.";
}
def fmul16_hs13 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_hs13";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_HS13";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the upper 32 bits of rtd.";
}
def fmul16_hs22 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_hs22";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_HS22";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the upper 32 bits of rtd.";
}
def fmul16_hs23 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_hs23";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_HS23";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the upper 32 bits of rtd.";
}
def fmul16_hs33 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_hs33";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_HS33";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the upper 32 bits of rtd.";
}
def fmul16_ls00 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_ls00";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_LS00";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd. Each 64-bit source r...";
}
def fmul16_ls01 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_ls01";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_LS01";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd.";
}
def fmul16_ls02 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_ls02";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_LS02";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd.";
}
def fmul16_ls03 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_ls03";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_LS03";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd.";
}
def fmul16_ls11 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_ls11";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_LS11";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd.";
}
def fmul16_ls12 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_ls12";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_LS12";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd.";
}
def fmul16_ls13 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_ls13";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_LS13";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd.";
}
def fmul16_ls22 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_ls22";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_LS22";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd.";
}
def fmul16_ls23 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_ls23";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_LS23";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd.";
}
def fmul16_ls33 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmul16_ls33";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMUL16_LS33";
  let Semantics = "perform a signed fixed-point multiplication between two selected 16-bit Q1.15 lanes from rsd1 and rsd2, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd.";
}
def fmuls16_hs00 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_hs00";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_HS00";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the upper 32 bits of rtd with Q1.31 saturation. Each 64-bit source register rsdX is treated as fou...";
}
def fmuls16_hs01 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_hs01";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_HS01";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the upper 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_hs02 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_hs02";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_HS02";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the upper 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_hs03 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_hs03";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_HS03";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the upper 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_hs11 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_hs11";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_HS11";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the upper 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_hs12 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_hs12";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_HS12";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the upper 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_hs13 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_hs13";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_HS13";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the upper 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_hs22 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_hs22";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_HS22";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the upper 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_hs23 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_hs23";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_HS23";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the upper 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_hs33 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_hs33";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_HS33";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the upper 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_ls00 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_ls00";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_LS00";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the lower 32 bits of rtd with Q1.31 saturation. Each 64-bit source register rsdX is treated as fou...";
}
def fmuls16_ls01 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_ls01";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_LS01";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the lower 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_ls02 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_ls02";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_LS02";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the lower 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_ls03 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_ls03";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_LS03";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the lower 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_ls11 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_ls11";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_LS11";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the lower 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_ls12 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_ls12";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_LS12";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the lower 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_ls13 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_ls13";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_LS13";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the lower 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_ls22 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_ls22";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_LS22";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the lower 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_ls23 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_ls23";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_LS23";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the lower 32 bits of rtd with Q1.31 saturation.";
}
def fmuls16_ls33 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmuls16_ls33";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULS16_LS33";
  let Semantics = "perform a signed fixed-point multiply-subtract operation on selected 16-bit Q1.15 lanes and update the lower 32 bits of rtd with Q1.31 saturation.";
}
def fmulaa16_hs_13_02 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulaa16_hs_13_02";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULAA16_HS_13_02";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation with Q1.31 saturation into the upper 32 bits of rtd.";
}
def fmulaa16_hs_33_22 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulaa16_hs_33_22";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULAA16_HS_33_22";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation with Q1.31 saturation into the upper 32 bits of rtd.";
}
def fmulaa16_ls_11_00 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulaa16_ls_11_00";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULAA16_LS_11_00";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation with Q1.31 saturation on 16-bit lanes. Each 64-bit source register rsdX is treated as four signed 16-bit Q1.15 lanes: rsdX[15:0] - lane 0 ...";
}
def fmulaa16_ls_13_02 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulaa16_ls_13_02";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULAA16_LS_13_02";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation with Q1.31 saturation on 16-bit lanes.";
}
def fmulaa16_ls_33_22 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulaa16_ls_33_22";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULAA16_LS_33_22";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation with Q1.31 saturation on 16-bit lanes.";
}
def fmulss16_hs_13_02 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulss16_hs_13_02";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULSS16_HS_13_02";
  let Semantics = "perform a signed fixed-point multiply-subtract operation with Q1.31 saturation on 16-bit lanes, accumulating into the upper 32 bits of rtd.";
}
def fmulss16_hs_33_22 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulss16_hs_33_22";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULSS16_HS_33_22";
  let Semantics = "perform a signed fixed-point multiply-subtract operation with Q1.31 saturation on 16-bit lanes, accumulating into the upper 32 bits of rtd.";
}
def fmulss16_ls_11_00 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulss16_ls_11_00";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULSS16_LS_11_00";
  let Semantics = "performs a signed fixed-point multiply-subtract operation with Q1.31 saturation on 16-bit lanes. Each 64-bit source register rsdX is treated as four signed 16-bit Q1.15 lanes: rsdX[15:0] - lane 0 (...";
}
def fmulss16_ls_13_02 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulss16_ls_13_02";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULSS16_LS_13_02";
  let Semantics = "performs a signed fixed-point multiply-subtract operation with Q1.31 saturation on 16-bit lanes.";
}
def fmulss16_ls_33_22 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmulss16_ls_33_22";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULSS16_LS_33_22";
  let Semantics = "performs a signed fixed-point multiply-subtract operation with Q1.31 saturation on 16-bit lanes.";
}
def f2mulaa32rs_hhll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulaa32rs_hhll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULAA32RS_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by SYM_ROUNDING17.47(). Both Q17.4...";
}
def f2mulaa32rs_hllh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulaa32rs_hllh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULAA32RS_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by SYM_ROUNDING17.47(). Both Q17.4...";
}
def f2mulss32rs_hhll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulss32rs_hhll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULSS32RS_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by SYM_ROUNDING17.47(). Both Q17.4...";
}
def f2mulss32rs_hllh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulss32rs_hllh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULSS32RS_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by SYM_ROUNDING17.47(). Both Q17.4...";
}
def srai64r : HaydnBuiltin<"int64_t(int64_t, int)"> {
  let PublicName = "srai64r";
  let PublicPrototype = "int64_t(int64_t, int)";
  let Mnemonic = "SRAI64R";
  let ImmChecks = [ImmCheck<1, ImmCheck0_63>];
  let Semantics = "performs a 64-bit arithmetic right shift of rsd by uimm6 bits with rounding. If uimm6 > 0, a rounding bias of 1 << (uimm6 - 1) is added to rsd before shifting. If uimm6 == 0, the result is equal to...";
}
// Imm forms (DB SRAI64/SLLI64/SRLI64). Reg twins: sra64 / sll64 / srl64.
def srai64 : HaydnBuiltin<"int64_t(int64_t, int)"> {
  let PublicName = "srai64";
  let PublicPrototype = "int64_t(int64_t, int)";
  let Mnemonic = "SRAI64";
  let ImmChecks = [ImmCheck<1, ImmCheck0_63>];
  let Semantics = "64-bit arithmetic right shift by immediate uimm6 (DB SRAI64). Variable shift: haydn_sra64.";
}
def slli64 : HaydnBuiltin<"int64_t(int64_t, int)"> {
  let PublicName = "slli64";
  let PublicPrototype = "int64_t(int64_t, int)";
  let Mnemonic = "SLLI64";
  let ImmChecks = [ImmCheck<1, ImmCheck0_63>];
  let Semantics = "64-bit logical left shift by immediate uimm6 (DB SLLI64). Variable shift: haydn_sll64.";
}
def srli64 : HaydnBuiltin<"int64_t(int64_t, int)"> {
  let PublicName = "srli64";
  let PublicPrototype = "int64_t(int64_t, int)";
  let Mnemonic = "SRLI64";
  let ImmChecks = [ImmCheck<1, ImmCheck0_63>];
  let Semantics = "64-bit logical right shift by immediate uimm6 (DB SRLI64). Variable shift: haydn_srl64.";
}
def x4fcmul16rs : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4fcmul16rs";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FCMUL16RS";
  let Semantics = "performs four parallel complex 16×16-bit fixed-point multiplications on packed Q1.15 operands, with rounding back to Q1.15 and saturation. Each source register rsdX is treated as two complex Q1.15 ...";
}
def x4fcmula16rs : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4fcmula16rs";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FCMULA16RS";
  let Semantics = "performs four parallel complex 16×16-bit fixed-point multiply-accumulate operations on packed Q1.15 operands, with rounding back to Q1.15 and saturation. Each source register rsdX is treated as two...";
}
def x4fcmul16rss : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4fcmul16rss";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FCMUL16RSS";
  let Semantics = "performs four parallel complex 16×16-bit fixed-point multiplications on packed Q1.15 operands, with symmetric rounding back to Q1.15 and saturation. Each source register rsdX is treated as two comp...";
}
def x4fcmula16rss : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4fcmula16rss";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FCMULA16RSS";
  let Semantics = "performs four parallel complex 16×16-bit fixed-point multiply-accumulate operations on packed Q1.15 operands, with symmetric rounding back to Q1.15 and saturation. Each source register rsdX is trea...";
}
def x2cmul32_pair : HaydnPairBuiltin<"int64_t(int64_t*, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2cmul32";
  let PublicPrototype = "haydn_dpair_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2CMUL32";
  let Semantics = "performs a complex multiplication of two 32-bit complex operands packed in 64-bit registers. Each operand is represented as: rsdX[63:32] = real part (signed 32-bit). rsdX[31:00] = imaginary part (s...";
}
def x2cmul32s_pair : HaydnPairBuiltin<"int64_t(int64_t*, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2cmul32s";
  let PublicPrototype = "haydn_dpair_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2CMUL32S";
  let Semantics = "performs a saturated complex multiplication using Format-1 packing of 32-bit complex operands in 64-bit data registers. Each operand is represented as: rsdX[63:32] = real part (signed 32-bit). rsdX...";
}
def smula16_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16_00";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16_00";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The 32-bit product is sign-extended to 64 ...";
}
def smula16_10 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16_10";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16_10";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The 32-bit product is sign-extended to 64 ...";
}
def smula16_11 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16_11";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16_11";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The 32-bit product is sign-extended to 64 ...";
}
def smula16_20 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16_20";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16_20";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The 32-bit product is sign-extended to 64 ...";
}
def smula16_21 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16_21";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16_21";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The 32-bit product is sign-extended to 64 ...";
}
def smula16_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16_22";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16_22";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The 32-bit product is sign-extended to 64 ...";
}
def smula16_30 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16_30";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16_30";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The 32-bit product is sign-extended to 64 ...";
}
def smula16_31 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16_31";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16_31";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The 32-bit product is sign-extended to 64 ...";
}
def smula16_32 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16_32";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16_32";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The 32-bit product is sign-extended to 64 ...";
}
def smula16_33 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16_33";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16_33";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The 32-bit product is sign-extended to 64 ...";
}
def smula16s_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16s_00";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16S_00";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The selected...";
}
def smula16s_10 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16s_10";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16S_10";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smula16s_11 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16s_11";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16S_11";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smula16s_20 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16s_20";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16S_20";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smula16s_21 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16s_21";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16S_21";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smula16s_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16s_22";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16S_22";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smula16s_30 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16s_30";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16S_30";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smula16s_31 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16s_31";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16S_31";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smula16s_32 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16s_32";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16S_32";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smula16s_33 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smula16s_33";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULA16S_33";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16_00";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16_00";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The 32-bit product is sign-extend...";
}
def smuls16_10 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16_10";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16_10";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16_11 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16_11";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16_11";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16_20 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16_20";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16_20";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16_21 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16_21";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16_21";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16_22";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16_22";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16_30 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16_30";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16_30";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16_31 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16_31";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16_31";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16_32 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16_32";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16_32";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16_33 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16_33";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16_33";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16s_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16s_00";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16S_00";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2. The...";
}
def smuls16s_10 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16s_10";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16S_10";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16s_11 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16s_11";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16S_11";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16s_20 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16s_20";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16S_20";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16s_21 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16s_21";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16S_21";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16s_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16s_22";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16S_22";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16s_30 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16s_30";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16S_30";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16s_31 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16s_31";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16S_31";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16s_32 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16s_32";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16S_32";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def smuls16s_33 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "smuls16s_33";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SMULS16S_33";
  let Semantics = "performs a signed 16-bit × 16-bit multiply-subtract-accumulate operation with signed 64-bit saturation on one selected 16-bit lane from each of the packed 64-bit source registers rsd1 and rsd2.";
}
def x4cmul16s : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<4, short>)"> {
  let PublicName = "x4cmul16s";
  let PublicPrototype = "haydn_x2int32(haydn_x4int16)";
  let Mnemonic = "X4CMUL16S";
  let Semantics = "performs a complex 16-bit × 16-bit multiplication using two complex operands packed into a single 64-bit source register, with signed 32-bit saturation applied to the real and imaginary results. Th...";
}
def x4cmul16s_f2 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<4, short>)"> {
  let PublicName = "x4cmul16s_f2";
  let PublicPrototype = "haydn_x2int32(haydn_x4int16)";
  let Mnemonic = "X4CMUL16S_F2";
  let Semantics = "performs a complex 16-bit × 16-bit multiplication using two complex operands packed into a single 64-bit source register, using the same alternative result format (Format-2) as X4CMUL16_F2, and app...";
}
def x2sel32_hh : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2sel32_hh";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SEL32_HH";
  let Semantics = "selects and reorders 32-bit sub-words from two source registers rsd1 and rsd2, and packs them into the destination register rtd.";
}
def x2sel32_hl : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2sel32_hl";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SEL32_HL";
  let Semantics = "selects and reorders 32-bit sub-words from two source registers rsd1 and rsd2, and packs them into the destination register rtd.";
}
def x2sel32_lh : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2sel32_lh";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SEL32_LH";
  let Semantics = "selects and reorders 32-bit sub-words from two source registers rsd1 and rsd2, and packs them into the destination register rtd.";
}
def x2sel32_ll : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2sel32_ll";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SEL32_LL";
  let Semantics = "selects and reorders 32-bit sub-words from two source registers rsd1 and rsd2, and packs them into the destination register rtd.";
}
def x4cmul16s_h : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4cmul16s_h";
  let PublicPrototype = "haydn_x2int32(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4CMUL16S_H";
  let Semantics = "performs a saturated complex 16-bit × 16-bit multiplication on the high complex elements (lane 3 and lane 2) of two packed 64-bit source registers. Each source register rsdX is interpreted as four ...";
}
def x4cmul16s_l : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4cmul16s_l";
  let PublicPrototype = "haydn_x2int32(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4CMUL16S_L";
  let Semantics = "performs a saturated complex 16-bit × 16-bit multiplication on the low complex elements (lane 1 and lane 0) of two packed 64-bit source registers. Each source register rsdX is interpreted as four s...";
}
def x4cmula16s_h : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4cmula16s_h";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4CMULA16S_H";
  let Semantics = "performs a saturated complex 16-bit × 16-bit multiply-accumulate operation on the high complex elements (lane 3 and lane 2) of two packed 64-bit source registers. Each source register rsdX is inter...";
}
def x4cmula16s_l : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4cmula16s_l";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4CMULA16S_L";
  let Semantics = "performs a saturated complex 16-bit × 16-bit multiply-accumulate operation on the low complex elements (lane 1 and lane 0) of two packed 64-bit source registers. Each source register rsdX is interp...";
}
def x4cjmul16s_h : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4cjmul16s_h";
  let PublicPrototype = "haydn_x2int32(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4CJMUL16S_H";
  let Semantics = "performs a saturated complex 16-bit × 16-bit multiplication between the high complex element of rsd1 and the complex-conjugate of the high complex element of rsd2. Each source register rsdX is inte...";
}
def x4cjmul16s_l : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4cjmul16s_l";
  let PublicPrototype = "haydn_x2int32(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4CJMUL16S_L";
  let Semantics = "performs a saturated complex 16-bit × 16-bit multiplication between the low complex element of rsd1 and the complex-conjugate of the low complex element of rsd2. Each source register rsdX is interp...";
}
def x4cjmula16s_h : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4cjmula16s_h";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4CJMULA16S_H";
  let Semantics = "performs a saturated complex 16-bit × 16-bit multiply-accumulate between the high complex element of rsd1 and the complex-conjugate of the high complex element of rsd2. Each source register rsdX is...";
}
def x4cjmula16s_l : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4cjmula16s_l";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4CJMULA16S_L";
  let Semantics = "performs a saturated complex 16-bit × 16-bit multiply-accumulate between the low complex element of rsd1 and the complex-conjugate of the low complex element of rsd2. Each source register rsdX is i...";
}
def add64_h : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "add64_h";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "ADD64_H";
  let Semantics = "performs a 64-bit signed addition using the upper 32 bits of rsd2. The upper 32 bits of rsd2 are sign-extended to 64 bits before addition. The 64-bit sum is written to rtd. No saturation is applied.";
}
def add64_l : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "add64_l";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "ADD64_L";
  let Semantics = "performs a 64-bit signed addition using the lower 32 bits of rsd2. The lower 32 bits of rsd2 are sign-extended to 64 bits before addition. The 64-bit sum is written to rtd. No saturation is applied.";
}
def sub64_h : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "sub64_h";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SUB64_H";
  let Semantics = "performs a 64-bit signed subtraction of the sign-extended upper 32 bits of rsd2 from rsd1, and writes the result to rtd.";
}
def sub64_l : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "sub64_l";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SUB64_L";
  let Semantics = "performs a 64-bit signed subtraction of the sign-extended lower 32 bits of rsd2 from rsd1, and writes the result to rtd.";
}
def max64 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "max64";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MAX64";
  let Semantics = "compares rsd1 and rsd2 as 64-bit signed integers, and writes the larger value to rtd.";
}
def min64 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "min64";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MIN64";
  let Semantics = "compares rsd1 and rsd2 as 64-bit signed integers, and writes the smaller value to rtd.";
}
def not64 : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "not64";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "NOT64";
  let Semantics = "performs a 64-bit bitwise NOT operation (one's complement) on rsd, and writes the result to rtd.";
}
def mul64_ll : HaydnBuiltin<"int64_t(int, int)"> {
  let PublicName = "mul64_ll";
  let PublicPrototype = "int64_t(int, int)";
  let Mnemonic = "MUL64_LL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2. The full 64-bit product is written to rtd.";
}
// Golden SLL64: rtd, rsd (DR64), rs (GPR32 shift). Reg twin of SLLI64.
def sll64 : HaydnBuiltin<"int64_t(int64_t, int)"> {
  let PublicName = "sll64";
  let PublicPrototype = "int64_t(int64_t, int)";
  let Mnemonic = "SLL64";
  let Semantics = "64-bit logical variable left/right shift of rsd by signed amount in rs (DB SLL64: rtd,rsd,rs). Immediate twin: haydn_slli64.";
}
def and64 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "and64";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "AND64";
  let Semantics = "performs a 64-bit bitwise AND operation between rsd1 and rsd2, and writes the result to rtd.";
}
def or64 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "or64";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "OR64";
  let Semantics = "performs a 64-bit bitwise OR operation between rsd1 and rsd2, and writes the result to rtd.";
}
// Golden SRA64/SRL64/SRA64R: rtd, rsd (DR64), rs (GPR32). Full 64-bit result.
def sra64 : HaydnBuiltin<"int64_t(int64_t, int)"> {
  let PublicName = "sra64";
  let PublicPrototype = "int64_t(int64_t, int)";
  let Mnemonic = "SRA64";
  let Semantics = "64-bit arithmetic variable right (or logical left) shift of rsd by amount in rs (DB SRA64). Immediate twin: haydn_srai64.";
}
def sra64r : HaydnBuiltin<"int64_t(int64_t, int)"> {
  let PublicName = "sra64r";
  let PublicPrototype = "int64_t(int64_t, int)";
  let Mnemonic = "SRA64R";
  let Semantics = "64-bit arithmetic variable right shift with rounding of rsd by amount in rs (DB SRA64R). Immediate twin: haydn_srai64r. Pack-to-i32 composites cast the low 32 bits in haydn_dsp.h.";
}
def srl64 : HaydnBuiltin<"int64_t(int64_t, int)"> {
  let PublicName = "srl64";
  let PublicPrototype = "int64_t(int64_t, int)";
  let Mnemonic = "SRL64";
  let Semantics = "64-bit logical variable right (or left) shift of rsd by amount in rs (DB SRL64). Immediate twin: haydn_srli64.";
}
def f2mulas32r_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulas32r_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULAS32R_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by ROUNDING17.47(). The upper-lane...";
}
def f2mulas32r_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulas32r_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULAS32R_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by ROUNDING17.47(). The upper-lane...";
}
def f2mulas32rs_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulas32rs_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULAS32RS_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by SYM_ROUNDING17.47(). The upper-...";
}
def f2mulas32rs_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulas32rs_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULAS32RS_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by SYM_ROUNDING17.47(). The upper-...";
}
def f2mulsa32r_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulsa32r_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULSA32R_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by ROUNDING17.47(). The upper-lane...";
}
def f2mulsa32r_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulsa32r_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULSA32R_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by ROUNDING17.47(). The upper-lane...";
}
def f2mulsa32rs_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulsa32rs_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULSA32RS_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by SYM_ROUNDING17.47(). The upper-...";
}
def f2mulsa32rs_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulsa32rs_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULSA32RS_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by SYM_ROUNDING17.47(). The upper-...";
}
def fmulas32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "fmulas32s_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "FMULAS32S_HHLL";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation on two 32-bit Q1.31 lane pairs with add/sub combination and writes the saturated Q1.63 result to rtd. The instruction operates on the pac...";
}
def fmulas32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "fmulas32s_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "FMULAS32S_HLLH";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation on two cross-lane 32-bit Q1.31 pairs with add/sub combination and writes the saturated Q1.63 result to rtd. The instruction operates on t...";
}
def fmulsa32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "fmulsa32s_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "FMULSA32S_HHLL";
  let Semantics = "performs a signed fixed-point multiply-subtract operation on two 32-bit Q1.31 lane pairs with sub/add combination and writes the saturated Q1.63 result to rtd. The instruction operates on the packe...";
}
def fmulsa32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "fmulsa32s_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "FMULSA32S_HLLH";
  let Semantics = "performs a signed fixed-point multiply-subtract operation on two cross-lane 32-bit Q1.31 pairs with sub/add combination and writes the saturated Q1.63 result to rtd. The instruction operates on the...";
}
def x2abs32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)"> {
  let PublicName = "x2abs32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32)";
  let Mnemonic = "X2ABS32";
  let Semantics = "performs two parallel 32-bit absolute value operations without saturation. The absolute value of the upper 32-bit sub-word (rsd[63:32]) is written to rtd[63:32], and the absolute value of the lower...";
}
def x2abs32s : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)"> {
  let PublicName = "x2abs32s";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32)";
  let Mnemonic = "X2ABS32S";
  let Semantics = "performs two parallel 32-bit absolute value operations with signed saturation (SAT32). The absolute value of the upper 32-bit sub-word (rsd[63:32]) is computed, saturated to 32-bit signed range, an...";
}
def x2neg32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)"> {
  let PublicName = "x2neg32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32)";
  let Mnemonic = "X2NEG32";
  let Semantics = "performs two parallel 32-bit two's complement negations without saturation. The negation of the upper 32-bit sub-word (rsd[63:32]) is written to rtd[63:32], and the negation of the lower 32-bit sub...";
}
def x2neg32s : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)"> {
  let PublicName = "x2neg32s";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32)";
  let Mnemonic = "X2NEG32S";
  let Semantics = "performs two parallel 32-bit two's complement negations with signed saturation (SAT32). The negation of the upper 32-bit sub-word (rsd[63:32]) is computed, saturated to 32-bit signed range, and wri...";
}
def x2swap32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)"> {
  let PublicName = "x2swap32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32)";
  let Mnemonic = "X2SWAP32";
  let Semantics = "swaps the upper and lower 32-bit sub-words of rsd and writes the result to rtd.";
}
def x2mjswap32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)"> {
  let PublicName = "x2mjswap32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32)";
  let Mnemonic = "X2MJSWAP32";
  let Semantics = "performs a complex multiply-by-j operation on a packed 32-bit complex operand stored in a 64-bit data register. The 64-bit source register rsd is interpreted as a complex number in Format-1 packing...";
}
def x2mjswap32s : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)"> {
  let PublicName = "x2mjswap32s";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32)";
  let Mnemonic = "X2MJSWAP32S";
  let Semantics = "performs a complex multiply-by-j operation on a packed 32-bit complex operand stored in a 64-bit data register, with signed 32-bit saturation (SAT32) applied to the negation result. The 64-bit sour...";
}
def x2addsub32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2addsub32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2ADDSUB32";
  let Semantics = "performs a parallel 32-bit add-subtract operation without saturation. The upper lane adds rsd1[63:32] to rsd2[63:32] and writes the result to rtd[63:32]. The lower lane subtracts rsd2[31:0] from rs...";
}
def x2subadd32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2subadd32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SUBADD32";
  let Semantics = "performs a parallel 32-bit subtract-add operation without saturation. The upper lane subtracts rsd2[63:32] from rsd1[63:32] and writes the result to rtd[63:32]. The lower lane adds rsd1[31:0] to rs...";
}
def x2max32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2max32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MAX32";
  let Semantics = "performs two parallel 32-bit signed maximum comparisons.";
}
def x2min32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2min32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MIN32";
  let Semantics = "performs two parallel 32-bit signed minimum comparisons.";
}
def x2frst32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2frst32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FRST32";
  let Semantics = "performs two parallel asymmetrical roundings of two signed Q1.63 fixed-point values in rsd1 and rsd2 to the Q1.31 range, with saturation, and writes the two packed Q1.31 results into rtd[63:32] and...";
}
def x2frsst32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2frsst32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FRSST32";
  let Semantics = "performs two parallel symmetrical roundings of two signed Q1.63 fixed-point values in rsd1 and rsd2 to the Q1.31 range, with saturation, and writes the two packed Q1.31 results into rtd[63:32] and ...";
}
def x2srai32r : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, int)"> {
  let PublicName = "x2srai32r";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, int)";
  let Mnemonic = "X2SRAI32R";
  let ImmChecks = [ImmCheck<1, ImmCheck0_31>];
  let Semantics = "performs two parallel 32-bit arithmetic right shifts with rounding by an immediate value.";
}
def x4abs16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)"> {
  let PublicName = "x4abs16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16)";
  let Mnemonic = "X4ABS16";
  let Semantics = "performs four parallel 16-bit absolute value operations without saturation. For the most negative 16-bit signed value (0x8000), the result wraps around to 0x8000.";
}
def x4abs16s : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)"> {
  let PublicName = "x4abs16s";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16)";
  let Mnemonic = "X4ABS16S";
  let Semantics = "performs four parallel 16-bit absolute value operations with signed saturation (SAT16). For the most negative value 0x8000, the result saturates to 0x7FFF.";
}
def x4neg16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)"> {
  let PublicName = "x4neg16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16)";
  let Mnemonic = "X4NEG16";
  let Semantics = "performs four parallel 16-bit two's complement negations without saturation. For the most negative 16-bit signed value 0x8000, the result wraps around to 0x8000.";
}
def x4neg16s : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)"> {
  let PublicName = "x4neg16s";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16)";
  let Mnemonic = "X4NEG16S";
  let Semantics = "performs four parallel 16-bit two's complement negations with signed saturation (SAT16). For the most negative value 0x8000, the result saturates to 0x7FFF.";
}
def x4swap16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)"> {
  let PublicName = "x4swap16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16)";
  let Mnemonic = "X4SWAP16";
  let Semantics = "reverses the order of the four 16-bit sub-words in rsd and writes the result to rtd.";
}
def x4mjswap16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)"> {
  let PublicName = "x4mjswap16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16)";
  let Mnemonic = "X4MJSWAP16";
  let Semantics = "performs two independent complex multiply-by-j operations on two packed 16-bit complex operands stored in a 64-bit data register, without saturation. Multiplying a complex number (a + j·b) by the i...";
}
def x4mjswap16s : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)"> {
  let PublicName = "x4mjswap16s";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16)";
  let Mnemonic = "X4MJSWAP16S";
  let Semantics = "performs two independent complex multiply-by-j operations on two packed 16-bit complex operands stored in a 64-bit data register, with saturation. Multiplying a complex number (a + j·b) by the imag...";
}
def x4conj16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)"> {
  let PublicName = "x4conj16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16)";
  let Mnemonic = "X4CONJ16";
  let Semantics = "computes the complex conjugate of two 16-bit complex numbers packed in rsd, each represented as a pair of 16-bit real and imaginary components. The real parts (upper 16 bits of each pair, i.e., rsd...";
}
def x4conj16s : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)"> {
  let PublicName = "x4conj16s";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16)";
  let Mnemonic = "X4CONJ16S";
  let Semantics = "computes the complex conjugate of two 16-bit complex numbers packed in rsd, each represented as a pair of 16-bit real and imaginary components. The real parts (upper 16 bits of each pair, i.e., rsd...";
}
def x4max16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4max16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4MAX16";
  let Semantics = "performs four parallel 16-bit signed maximum comparisons.";
}
def x4min16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4min16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4MIN16";
  let Semantics = "performs four parallel 16-bit signed minimum comparisons.";
}
def x4srai16r : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, int)"> {
  let PublicName = "x4srai16r";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, int)";
  let Mnemonic = "X4SRAI16R";
  let ImmChecks = [ImmCheck<1, ImmCheck0_15>];
  let Semantics = "performs four parallel 16-bit arithmetic right shifts with rounding by an immediate value. For each lane, if uimm4 > 0, a rounding bias of (1 << (uimm4-1)) is added to the sign-extended 17-bit valu...";
}
def x2neg32_l : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)"> {
  let PublicName = "x2neg32_l";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32)";
  let Mnemonic = "X2NEG32_L";
  let Semantics = "performs a single-lane 32-bit two's complement negation on the lower sub-word only, without saturation. The upper 32-bit sub-word (rsd[63:32]) is copied unchanged to rtd[63:32]. The lower 32-bit su...";
}
def x2neg32s_l : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)"> {
  let PublicName = "x2neg32s_l";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32)";
  let Mnemonic = "X2NEG32S_L";
  let Semantics = "performs a single-lane 32-bit two's complement negation on the lower sub-word only, with signed saturation (SAT32). The upper 32-bit sub-word (rsd[63:32]) is copied unchanged to rtd[63:32]. The low...";
}
def x2addsub32_hllh : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2addsub32_hllh";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2ADDSUB32_HLLH";
  let Semantics = "performs a cross-lane parallel 32-bit add-subtract operation without saturation. The upper lane adds the upper 32-bit sub-word of rsd1 (rsd1[63:32]) to the lower 32-bit sub-word of rsd2 (rsd2[31:0]...";
}
def x2addsub32s_hllh : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2addsub32s_hllh";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2ADDSUB32S_HLLH";
  let Semantics = "performs a cross-lane parallel 32-bit add-subtract operation with signed saturation (SAT32). The upper lane adds the upper 32-bit sub-word of rsd1 (rsd1[63:32]) to the lower 32-bit sub-word of rsd2...";
}
def x2subadd32_hllh : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2subadd32_hllh";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SUBADD32_HLLH";
  let Semantics = "performs a cross-lane parallel 32-bit subtract-add operation without saturation. The upper lane subtracts the lower 32-bit sub-word of rsd2 (rsd2[31:00]) from the upper 32-bit sub-word of rsd1 (rsd...";
}
def x2subadd32s_hllh : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2subadd32s_hllh";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SUBADD32S_HLLH";
  let Semantics = "performs a cross-lane parallel 32-bit subtract-add operation with signed saturation (SAT32). The upper lane subtracts the lower 32-bit sub-word of rsd2 (rsd2[31:00]) from the upper 32-bit sub-word ...";
}
def x2clamp32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2clamp32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2CLAMP32";
  let Semantics = "performs per-lane clamping of two packed 32-bit signed values in rtd. For each 32-bit lane, the value in rtd is first limited to the upper bound from rsd1, then limited to the lower bound from rsd2.";
}
def x2fmul32rs : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fmul32rs";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FMUL32RS";
  let Semantics = "performs two parallel 32-bit fixed-point multiplications in Q1.31 format with rounding and saturation. The upper 32-bit elements rsd1[63:32] and rsd2[63:32] are treated as signed Q1.31 values and m...";
}
def x2fmul32rss : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fmul32rss";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FMUL32RSS";
  let Semantics = "performs two parallel 32-bit fixed-point multiplications in Q1.31 format with symmetric rounding and saturation. The upper 32-bit elements rsd1[63:32] and rsd2[63:32] are treated as signed Q1.31 va...";
}
def x2fmul32ts : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fmul32ts";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FMUL32TS";
  let Semantics = "performs two parallel 32-bit fixed-point multiplications on packed Q1.31 operands and writes the Q1.31 truncated products to the destination lanes with saturation. rsd1[63:32] and rsd2[63:32] are i...";
}
def x2fmula32rs : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fmula32rs";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FMULA32RS";
  let Semantics = "performs two parallel 32-bit fixed-point multiply-accumulate operations on packed Q1.31 operands, with rounding and saturation applied on both the product and the accumulator. rsd1[63:32] and rsd2[...";
}
def x2fmula32rss : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fmula32rss";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FMULA32RSS";
  let Semantics = "performs two parallel 32-bit fixed-point multiply-accumulate operations on packed Q1.31 operands, with symmetric rounding and saturation applied on both the product and the accumulator. rsd1[63:32]...";
}
def x2fmula32ts : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fmula32ts";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FMULA32TS";
  let Semantics = "performs two parallel 32-bit fixed-point multiply-accumulate operations on packed Q1.31 operands, with truncation and saturation applied on the product and saturation on the accumulator. The upper ...";
}
def x2fmuls32rs : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fmuls32rs";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FMULS32RS";
  let Semantics = "performs two parallel 32-bit fixed-point multiply-subtract operations on packed Q1.31 operands, with rounding and saturation applied on both the product and the accumulator. rsd1[63:32] and rsd2[63...";
}
def x2fmuls32rss : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fmuls32rss";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FMULS32RSS";
  let Semantics = "performs two parallel 32-bit fixed-point multiply-subtract operations on packed Q1.31 operands, with symmetric rounding and saturation applied on both the product and the accumulator. rsd1[63:32] a...";
}
def x2fmuls32ts : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fmuls32ts";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FMULS32TS";
  let Semantics = "performs two parallel 32-bit fixed-point multiply-subtract operations on packed Q1.31 operands, with truncation and saturation applied on the product and saturation on the accumulator. The upper 32...";
}
def x4cmul16 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<4, short>)"> {
  let PublicName = "x4cmul16";
  let PublicPrototype = "haydn_x2int32(haydn_x4int16)";
  let Mnemonic = "X4CMUL16";
  let Semantics = "performs a complex 16-bit × 16-bit multiplication using two complex operands packed into a single 64-bit source register. The source register rsd is interpreted as four signed 16-bit values: rsd[63...";
}
def x4cmul16_f2 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<4, short>)"> {
  let PublicName = "x4cmul16_f2";
  let PublicPrototype = "haydn_x2int32(haydn_x4int16)";
  let Mnemonic = "X4CMUL16_F2";
  let Semantics = "performs a complex 16-bit × 16-bit multiplication using two complex operands packed into a single 64-bit source register, and writes the result in an alternative output format (Format-2). The sourc...";
}
def x4energy16 : HaydnBuiltin<"int64_t(_ExtVector<4, short>)"> {
  let PublicName = "x4energy16";
  let PublicPrototype = "int64_t(haydn_x4int16)";
  let Mnemonic = "X4ENERGY16";
  let Semantics = "computes the sum of squares (energy) of four packed 16-bit signed values. The 32-bit result is written to the lower 32 bits of rtd. The upper 32 bits are cleared to zero.";
}
def x4clamp16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4clamp16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4CLAMP16";
  let Semantics = "performs per-lane clamping of four packed 16-bit signed values in rtd. For each 16-bit lane, the value in rtd is first limited to the upper bound from rsd1, then limited to the lower bound from rsd2.";
}
def x4fmul16rs : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4fmul16rs";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FMUL16RS";
  let Semantics = "performs four parallel signed 16×16-bit fixed-point multiplications on packed Q1.15 operands, with rounding back to Q1.15 and saturation. Each source register rsdX is treated as four signed 16-bit ...";
}
def x4fmul16rss : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4fmul16rss";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FMUL16RSS";
  let Semantics = "performs four parallel signed 16×16-bit fixed-point multiplications on packed Q1.15 operands, with symmetric rounding back to Q1.15 and saturation. Each source register rsdX is treated as four sign...";
}
def x4fmul16ts : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4fmul16ts";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FMUL16TS";
  let Semantics = "performs four parallel signed 16×16-bit fixed-point multiplications on packed Q1.15 operands, with truncation back to Q1.15 and saturation. Each source register rsdX is treated as four signed 16-bi...";
}
def x4frst16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4frst16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FRST16";
  let Semantics = "performs four parallel asymmetrical roundings of four signed Q1.31 fixed-point values packed in rsd1 and rsd2 to the Q1.15 range, with saturation, and writes the four packed Q1.15 results into rtd[...";
}
def x4frsst16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4frsst16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FRSST16";
  let Semantics = "performs four parallel symmetrical roundings of four signed Q1.31 fixed-point values packed in rsd1 and rsd2 to the Q1.15 range, with saturation, and writes the four packed Q1.15 results into rtd[6...";
}
def x2seq32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2seq32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SEQ32";
  let Semantics = "performs two parallel 32-bit equality comparisons to update the SFR.";
}
def x2slt32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2slt32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SLT32";
  let Semantics = "performs two parallel 32-bit signed less-than comparisons to update the SFR.";
}
def x2sle32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2sle32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SLE32";
  let Semantics = "performs two parallel 32-bit signed less-than-or-equal comparisons to update the SFR.";
}
def x2movf32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2movf32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MOVF32";
  let Semantics = "conditionally moves two 32-bit sub-words of rsd into the corresponding lanes of rtd. Each lane is updated independently based on its SFR bits.";
}
def x2movt32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2movt32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MOVT32";
  let Semantics = "conditionally moves two 32-bit sub-words of rsd into the corresponding lanes of rtd. Each lane is updated independently based on its SFR bits.";
}
def x4seq16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4seq16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4SEQ16";
  let Semantics = "performs four parallel 16-bit equality comparisons to update the SFR.";
}
def x4slt16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4slt16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4SLT16";
  let Semantics = "performs four parallel 16-bit signed less-than comparisons to update the SFR.";
}
def x4sle16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4sle16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4SLE16";
  let Semantics = "performs four parallel 16-bit signed less-than-or-equal comparisons to update the SFR.";
}
def x4movf16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4movf16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4MOVF16";
  let Semantics = "conditionally moves four 16-bit sub-words of rsd into the corresponding lanes of rtd. Each lane is updated independently based on the corresponding SFR bit.";
}
def x4movt16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4movt16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4MOVT16";
  let Semantics = "conditionally moves four 16-bit sub-words of rsd into the corresponding lanes of rtd. Each lane is updated independently based on the corresponding SFR bit.";
}
def slt64 : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "slt64";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "SLT64";
  let Semantics = "performs a 64-bit signed comparison. If rsd1 is less than rsd2, SFR is set to 4'b1111; otherwise SFR is set to 4'b0000.";
}
def sle64 : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "sle64";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "SLE64";
  let Semantics = "performs a 64-bit signed comparison. If rsd1 is less than or equal to rsd2, SFR is set to 4'b1111; otherwise SFR is set to 4'b0000.";
}
def seq64 : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "seq64";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "SEQ64";
  let Semantics = "performs a 64-bit equality comparison. If rsd1 is equal to rsd2, SFR is set to 4'b1111; otherwise SFR is set to 4'b0000.";
}
def movt64 : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "movt64";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "MOVT64";
  let Semantics = "conditionally moves the 64-bit value of rsd into rtd based on the SFR. If SFR equals 4'b1111 (condition true), rtd is updated with rsd; otherwise rtd retains its current value.";
}
def movf64 : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "movf64";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "MOVF64";
  let Semantics = "conditionally moves the 64-bit value of rsd into rtd based on the SFR. If SFR equals 4'b0000 (condition false), rtd is updated with rsd; otherwise rtd retains its current value.";
}
def popcount32 : HaydnBuiltin<"int(int)"> {
  let PublicName = "popcount32";
  let PublicPrototype = "int(int)";
  let Mnemonic = "POPCOUNT32";
  let Semantics = "counts the number of bits set to 1 in rs. The operand is treated as a 32-bit unsigned value; only the bit pattern matters.";
}
def popcount64 : HaydnBuiltin<"int(int64_t)"> {
  let PublicName = "popcount64";
  let PublicPrototype = "int(int64_t)";
  let Mnemonic = "POPCOUNT64";
  let Semantics = "counts the number of bits set to 1 in rsd. The operand is treated as a 64-bit unsigned value; only the bit pattern matters. The result ranges from 0 to 64.";
}
def brev32 : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "brev32";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "BREV32";
  let Semantics = "performs bit-reversed addressing on rs1 with an additional linear offset rs2. All operations are on 32-bit unsigned values. The bits of rs1 are reversed, the offset rs2 is added, and then the resul...";
}
def x2subadd32s : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2subadd32s";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SUBADD32S";
  let Semantics = "performs a parallel 32-bit subtract-add operation with signed saturation (SAT32). The upper lane subtracts rsd2[63:32] from rsd1[63:32], saturates the result, and writes to rtd[63:32]. The lower la...";
}
def x2dot32 : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2dot32";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2DOT32";
  let Semantics = "performs a 2-way SIMD 32×32-bit signed dot product operation. The full 64-bit result is written to rtd.";
}
def x4dot16 : HaydnBuiltin<"int64_t(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4dot16";
  let PublicPrototype = "int64_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4DOT16";
  let Semantics = "performs a 4-way SIMD 16×16-bit signed dot product operation. The 32-bit result is written to the lower 32 bits of rtd. The upper 32 bits of rtd are cleared to zero.";
}
def x2cmula32 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)"> {
  let PublicName = "x2cmula32";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)";
  let Mnemonic = "X2CMULA32";
}
def x2cmula32s : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)"> {
  let PublicName = "x2cmula32s";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)";
  let Mnemonic = "X2CMULA32S";
}
def x2cmuls32 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)"> {
  let PublicName = "x2cmuls32";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)";
  let Mnemonic = "X2CMULS32";
}
def x2cmuls32s : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)"> {
  let PublicName = "x2cmuls32s";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)";
  let Mnemonic = "X2CMULS32S";
}
def isqrt : HaydnBuiltin<"int(int)"> {
  let PublicName = "isqrt";
  let PublicPrototype = "int(int)";
  let Mnemonic = "ISQRT";
}
def arctan : HaydnBuiltin<"int(int64_t, int)"> {
  let PublicName = "arctan";
  let PublicPrototype = "int(int64_t, int)";
  let Mnemonic = "ARCTAN";
  let Semantics = "Fixed-point arctangent (atan2) via programmable-iteration CORDIC vectoring mode. rsd[63:32] = x (signed 32-bit), rsd[31:0] = y (signed 32-bit). Computes atan2(y, x) and writes 16-bit phase (0=0°, 6...";
}
def movesfr2gpr : HaydnBuiltin<"unsigned int()"> {
  let PublicName = "movesfr2gpr";
  let PublicPrototype = "unsigned int()";
  let Mnemonic = "MOVESFR2GPR";
  let Semantics = "move the SFR to the GPR.";
}
def movegpr2sfr : HaydnBuiltin<"void(unsigned int)"> {
  let PublicName = "movegpr2sfr";
  let PublicPrototype = "void(unsigned int)";
  let Mnemonic = "MOVEGPR2SFR";
  let Semantics = "move the GPR to the SFR.";
}
def zero_sfr : HaydnBuiltin<"void()"> {
  let PublicName = "zero_sfr";
  let PublicPrototype = "void()";
  let Mnemonic = "ZERO_SFR";
  let Semantics = "clears the SFR by setting all its bits to zero.";
}
def move32_dr_l : HaydnBuiltin<"unsigned int(int64_t)"> {
  let PublicName = "move32_dr_l";
  let PublicPrototype = "unsigned int(int64_t)";
  let Mnemonic = "MOVE32_DR_L";
  let Semantics = "move the lower 32 bits of rsd to rt.";
}
def move32_dr_h : HaydnBuiltin<"unsigned int(int64_t)"> {
  let PublicName = "move32_dr_h";
  let PublicPrototype = "unsigned int(int64_t)";
  let Mnemonic = "MOVE32_DR_H";
  let Semantics = "move the upper 32 bits of rsd to rt.";
}
def ldw_cb_imm_pair : HaydnPairBuiltin<"int64_t(int*, int, int, int)"> {
  let PublicName = "ldw_cb_imm";
  let PublicPrototype = "haydn_cb_ld_t(int, int, int)";
  let Mnemonic = "D_LDW_CB_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheck0_1>, ImmCheck<3, ImmCheckSimm8>];
}
def ldw_cb_reg_pair : HaydnPairBuiltin<"int64_t(int*, int, int, int)"> {
  let PublicName = "ldw_cb_reg";
  let PublicPrototype = "haydn_cb_ld_t(int, int, int)";
  let Mnemonic = "D_LDW_CB_REG";
}
def sdw_cb_imm : HaydnBuiltin<"int(int64_t, int, int, int)"> {
  let PublicName = "sdw_cb_imm";
  let PublicPrototype = "int(int64_t, int, int, int)";
  let Mnemonic = "D_SDW_CB_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheck0_1>, ImmCheck<3, ImmCheckSimm8>];
}
def sdw_cb_reg : HaydnBuiltin<"int(int64_t, int, int, int)"> {
  let PublicName = "sdw_cb_reg";
  let PublicPrototype = "int(int64_t, int, int, int)";
  let Mnemonic = "D_SDW_CB_REG";
}
def setcbr_begin : HaydnBuiltin<"void(int, int)"> {
  let PublicName = "setcbr_begin";
  let PublicPrototype = "void(int, int)";
  let Mnemonic = "SETCBR_BEGIN";
}
def setcbr_end : HaydnBuiltin<"void(int, int)"> {
  let PublicName = "setcbr_end";
  let PublicPrototype = "void(int, int)";
  let Mnemonic = "SETCBR_END";
}
def pldwwua : HaydnBuiltin<"void(int, int)"> {
  let PublicName = "pldwwua";
  let PublicPrototype = "void(int, int)";
  let Mnemonic = "PLDWWUA_POST";
  let Semantics = "pre-load aligned double-word block into aligned register for subsequent unaligned access.";
}
def d_lqhwua_post : HaydnBuiltin<"int64_t(int, int, int, int)"> {
  let PublicName = "d_lqhwua_post";
  let PublicPrototype = "int64_t(int, int, int, int)";
  let Mnemonic = "D_LQHWUA_POST";
  let Semantics = "load quad half words with hardware unaligned access mechanism. handles accesses where the data is 2-byte aligned but not necessarily 8-byte aligned.";
}
def d_ltwua_post : HaydnBuiltin<"int64_t(int, int, int, int)"> {
  let PublicName = "d_ltwua_post";
  let PublicPrototype = "int64_t(int, int, int, int)";
  let Mnemonic = "D_LTWUA_POST";
  let Semantics = "load two words with hardware unaligned access mechanism. handles accesses where the data is 4-byte aligned but not necessarily 8-byte aligned.";
}
def flar : HaydnBuiltin<"void(int)"> {
  let PublicName = "flar";
  let PublicPrototype = "void(int)";
  let Mnemonic = "FLAR";
  let Semantics = "flush the target aligned register.";
}
def wbarwua : HaydnBuiltin<"void(int, int, int)"> {
  let PublicName = "wbarwua";
  let PublicPrototype = "void(int, int, int)";
  let Mnemonic = "WBARWUA";
  let Semantics = "write back the rest data of the aligned register with hardware unaligned access mechanism. and, flush the target aligned register. rs must be the post-incremented value from the last D_STWUA_POST /...";
}
def d_sqhwua_post : HaydnBuiltin<"void(int64_t, int, int, int, int)"> {
  let PublicName = "d_sqhwua_post";
  let PublicPrototype = "void(int64_t, int, int, int, int)";
  let Mnemonic = "D_SQHWUA_POST";
  let Semantics = "store quad half words with hardware unaligned access mechanism, post-increment. handles accesses where the data is 2-byte aligned but not necessarily 8-byte aligned. bytes before the first data ele...";
}
def d_stwua_post : HaydnBuiltin<"void(int64_t, int, int, int, int)"> {
  let PublicName = "d_stwua_post";
  let PublicPrototype = "void(int64_t, int, int, int, int)";
  let Mnemonic = "D_STWUA_POST";
  let Semantics = "store two words hardware unaligned access mechanism, post-increment. handles accesses where the data is 4-byte aligned but not necessarily 8-byte aligned. bytes before the first data element within...";
}
// BREV loads: frexp pair {data, new_ptr} (same AGU model as CB loads).
// Clang Prototype is frexp (out* for new_ptr); public returns haydn_*_ld_t.
// D_LDW_BREV: i64 data; S_LW_BREV: i32 data. IMM stride is ImmArg (simm6).
def ldw_brev_imm_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "ldw_brev_imm";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LDW_BREV_IMM";
  // frexp args: out*, base, stride — ImmCheck on stride.
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
}
def ldw_brev_reg_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "ldw_brev_reg";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LDW_BREV_REG";
}
def lw_brev_imm_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "lw_brev_imm";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LW_BREV_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
}
def lw_brev_reg_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "lw_brev_reg";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LW_BREV_REG";
}
// BREV stores: return AGU-updated base. D data is DR64 (i64); S data is GPR.
def sdw_brev_imm : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "sdw_brev_imm";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SDW_BREV_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
}
def sdw_brev_reg : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "sdw_brev_reg";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SDW_BREV_REG";
}
def sw_brev_imm : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "sw_brev_imm";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SW_BREV_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
}
def sw_brev_reg : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "sw_brev_reg";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SW_BREV_REG";
}
def x2add32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2add32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2ADD32";
  let Semantics = "performs two parallel 32-bit additions without saturation. The upper 32-bit sub-word of rsd1 is added to the upper 32-bit sub-word of rsd2, and the result is written to rtd[63:32]. The lower 32-bit...";
}

def x2add32s : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2add32s";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2ADD32S";
  let Semantics = "performs a dual 32-bit saturating addition on the high and low 32-bit sub-words of rsd1 and rsd2.";
}
def x2sub32s : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2sub32s";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SUB32S";
  let Semantics = "performs a dual 32-bit saturating subtraction on the high and low 32-bit sub-words of rsd1 and rsd2.";
}
def x2addsub32s : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2addsub32s";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2ADDSUB32S";
  let Semantics = "dual 32-bit add-sub with saturation.";
}
def x4add16s : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4add16s";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4ADD16S";
  let Semantics = "performs a quad 16-bit saturating addition on the four 16-bit sub-words of rsd1 and rsd2.";
}
def x4sub16s : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4sub16s";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4SUB16S";
  let Semantics = "performs a quad 16-bit saturating subtraction on the four 16-bit sub-words of rsd1 and rsd2.";
}
def x2mul32_pair : HaydnPairBuiltin<"int64_t(int64_t*, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2mul32";
  let PublicPrototype = "haydn_dpair_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MUL32";
  let Semantics = "dual 32×32→64 multiply; .hi=high lane product, .lo=low";
}

def x2sub32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2sub32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SUB32";
  let Semantics = "performs two parallel 32-bit subtractions without saturation. The upper 32-bit sub-word of rsd2 is subtracted from the upper 32-bit sub-word of rsd1, and the result is written to rtd[63:32]. The lo...";
}
def x4add16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4add16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4ADD16";
  let Semantics = "performs four parallel 16-bit signed additions without saturation.";
}
def x4sub16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4sub16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4SUB16";
  let Semantics = "performs four parallel 16-bit signed subtractions without saturation.";
}
def x2add32_hllh : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2add32_hllh";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2ADD32_HLLH";
  let Semantics = "performs a cross-lane parallel 32-bit addition without saturation. The upper lane adds the upper 32-bit sub-word of rsd1 (rsd1[63:32]) to the lower 32-bit sub-word of rsd2 (rsd2[31:00]), and writes...";
}
def x2add32s_hllh : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2add32s_hllh";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2ADD32S_HLLH";
  let Semantics = "performs a cross-lane parallel 32-bit addition with signed saturation (SAT32). The upper lane adds the upper 32-bit sub-word of rsd1 (rsd1[63:32]) to the lower 32-bit sub-word of rsd2 (rsd2[31:00])...";
}
def x2sub32_hllh : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2sub32_hllh";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SUB32_HLLH";
  let Semantics = "performs a cross-lane parallel 32-bit subtraction without saturation. The upper lane subtracts the lower 32-bit sub-word of rsd2 (rsd2[31:00]) from the upper 32-bit sub-word of rsd1 (rsd1[63:32]), ...";
}
def x2sub32s_hllh : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2sub32s_hllh";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2SUB32S_HLLH";
  let Semantics = "performs a cross-lane parallel 32-bit subtraction with signed saturation (SAT32). The upper lane subtracts the lower 32-bit sub-word of rsd2 (rsd2[31:00]) from the upper 32-bit sub-word of rsd1 (rs...";
}
def x2sll32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, int)"> {
  let PublicName = "x2sll32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, int)";
  let Mnemonic = "X2SLL32";
  let Semantics = "performs two parallel 32-bit variable logical left shifts or logical right shifts depending on the sign of the shift amount. The shift amount simm6 is a signed 6-bit value extracted from rs[5:0] an...";
}
def x2sra32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, int)"> {
  let PublicName = "x2sra32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, int)";
  let Mnemonic = "X2SRA32";
  let Semantics = "performs two parallel 32-bit variable arithmetic right shifts or logical left shifts depending on the sign of the shift amount. The shift amount simm6 is a signed 6-bit value extracted from rs[5:0]...";
}
def x2srl32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, int)"> {
  let PublicName = "x2srl32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, int)";
  let Mnemonic = "X2SRL32";
  let Semantics = "performs two parallel 32-bit variable logical right shifts or logical left shifts depending on the sign of the shift amount. The shift amount simm6 is a signed 6-bit value extracted from rs[5:0] an...";
}
def x4sll16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, int)"> {
  let PublicName = "x4sll16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, int)";
  let Mnemonic = "X4SLL16";
  let Semantics = "performs four parallel 16-bit variable logical left/right shifts by a signed 5-bit amount from register rs. The shift amount simm5 is extracted from rs[4:0] and sign-extended to 32 bits. Valid rang...";
}
def x4sra16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, int)"> {
  let PublicName = "x4sra16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, int)";
  let Mnemonic = "X4SRA16";
  let Semantics = "performs four parallel 16-bit variable arithmetic right or logical left shifts by a signed 5-bit amount from register rs. The shift amount simm5 is extracted from rs[4:0] and sign-extended to 32 bi...";
}
def x4srl16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, int)"> {
  let PublicName = "x4srl16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, int)";
  let Mnemonic = "X4SRL16";
  let Semantics = "performs four parallel 16-bit variable logical right/left shifts by a signed 5-bit amount from register rs. The shift amount simm5 is extracted from rs[4:0] and sign-extended to 32 bits. Valid rang...";
}
def x2slli32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, int)"> {
  let PublicName = "x2slli32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, int)";
  let Mnemonic = "X2SLLI32";
  let ImmChecks = [ImmCheck<1, ImmCheck0_31>];
  let Semantics = "performs two parallel 32-bit logical left shifts by an immediate value.";
}
def x2srai32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, int)"> {
  let PublicName = "x2srai32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, int)";
  let Mnemonic = "X2SRAI32";
  let ImmChecks = [ImmCheck<1, ImmCheck0_31>];
  let Semantics = "performs two parallel 32-bit arithmetic right shifts by an immediate value.";
}
def x2srli32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, int)"> {
  let PublicName = "x2srli32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, int)";
  let Mnemonic = "X2SRLI32";
  let ImmChecks = [ImmCheck<1, ImmCheck0_31>];
  let Semantics = "performs two parallel 32-bit logical right shifts by an immediate value.";
}
def x4slli16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, int)"> {
  let PublicName = "x4slli16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, int)";
  let Mnemonic = "X4SLLI16";
  let ImmChecks = [ImmCheck<1, ImmCheck0_15>];
  let Semantics = "performs four parallel 16-bit logical left shifts by an immediate value. Each of the four 16-bit sub-words in rsd is shifted left by uimm4 bits (0-15), with zeros filling the vacated low-order posi...";
}
def x4srai16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, int)"> {
  let PublicName = "x4srai16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, int)";
  let Mnemonic = "X4SRAI16";
  let ImmChecks = [ImmCheck<1, ImmCheck0_15>];
  let Semantics = "performs four parallel 16-bit arithmetic right shifts by an immediate value. Each of the four signed 16-bit sub-words in rsd is shifted right arithmetically by uimm4 bits (0-15), with sign extension.";
}
def x4srli16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, int)"> {
  let PublicName = "x4srli16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, int)";
  let Mnemonic = "X4SRLI16";
  let ImmChecks = [ImmCheck<1, ImmCheck0_15>];
  let Semantics = "performs four parallel 16-bit logical right shifts by an immediate value. Each of the four 16-bit sub-words in rsd is shifted right logically by uimm4 bits (0-15), with zeros filling the vacated hi...";
}
def x2sra32r : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, int)"> {
  let PublicName = "x2sra32r";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, int)";
  let Mnemonic = "X2SRA32R";
  let Semantics = "performs two parallel 32-bit variable arithmetic right shifts with rounding, or logical left shifts, depending on the sign of the shift amount. The shift amount simm6 is a signed 6-bit value extrac...";
}
def x4sra16r : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, int)"> {
  let PublicName = "x4sra16r";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, int)";
  let Mnemonic = "X4SRA16R";
  let Semantics = "performs four parallel 16-bit variable arithmetic right shifts with rounding, or logical left shifts, by a signed 5-bit amount from register rs. The shift amount simm5 is extracted from rs[4:0] and...";
}
def x2hadd32_h : HaydnBuiltin<"int64_t(_ExtVector<2, int>)"> {
  let PublicName = "x2hadd32_h";
  let PublicPrototype = "int64_t(haydn_x2int32)";
  let Mnemonic = "X2HADD32_H";
  let Semantics = "performs a horizontal 32-bit addition. Adds the upper 32-bit sub-word (rsd[63:32]) to the lower 32-bit sub-word (rsd[31:0]) of rsd. The _H variant writes the 32-bit result to rtd[63:32] (lower 32 b...";
}
def x2hadd32s_h : HaydnBuiltin<"int64_t(_ExtVector<2, int>)"> {
  let PublicName = "x2hadd32s_h";
  let PublicPrototype = "int64_t(haydn_x2int32)";
  let Mnemonic = "X2HADD32S_H";
  let Semantics = "performs a horizontal 32-bit addition with signed saturation (SAT32). Adds the upper 32-bit sub-word (rsd[63:32]) to the lower 32-bit sub-word (rsd[31:0]) of rsd. The _H variant writes the saturate...";
}
def x2hadd32_l : HaydnBuiltin<"int64_t(_ExtVector<2, int>)"> {
  let PublicName = "x2hadd32_l";
  let PublicPrototype = "int64_t(haydn_x2int32)";
  let Mnemonic = "X2HADD32_L";
  let Semantics = "performs a horizontal 32-bit addition. Adds the upper 32-bit sub-word (rsd[63:32]) to the lower 32-bit sub-word (rsd[31:0]) of rsd. The _L variant writes the 32-bit result to rtd[31:0] (upper 32 bi...";
}
def x2hadd32s_l : HaydnBuiltin<"int64_t(_ExtVector<2, int>)"> {
  let PublicName = "x2hadd32s_l";
  let PublicPrototype = "int64_t(haydn_x2int32)";
  let Mnemonic = "X2HADD32S_L";
  let Semantics = "performs a horizontal 32-bit addition with signed saturation (SAT32). Adds the upper 32-bit sub-word (rsd[63:32]) to the lower 32-bit sub-word (rsd[31:0]) of rsd. The _L variant writes the saturate...";
}
def x4hadd16_h : HaydnBuiltin<"int64_t(_ExtVector<4, short>)"> {
  let PublicName = "x4hadd16_h";
  let PublicPrototype = "int64_t(haydn_x4int16)";
  let Mnemonic = "X4HADD16_H";
  let Semantics = "performs a horizontal addition across all four 16-bit sub-words of rsd, reducing them to a single 32-bit sum.";
}
def x4hadd16_l : HaydnBuiltin<"int64_t(_ExtVector<4, short>)"> {
  let PublicName = "x4hadd16_l";
  let PublicPrototype = "int64_t(haydn_x4int16)";
  let Mnemonic = "X4HADD16_L";
  let Semantics = "performs a horizontal addition across all four 16-bit sub-words of rsd, reducing them to a single 32-bit sum.";
}
def x2hmax32 : HaydnBuiltin<"int64_t(_ExtVector<2, int>)"> {
  let PublicName = "x2hmax32";
  let PublicPrototype = "int64_t(haydn_x2int32)";
  let Mnemonic = "X2HMAX32";
  let Semantics = "performs a horizontal maximum operation across the two 32-bit sub-words of rsd. The larger of the upper 32-bit sub-word (rsd[63:32]) and the lower 32-bit sub-word (rsd[31:0]) is written to the dest...";
}
def x2hmin32 : HaydnBuiltin<"int64_t(_ExtVector<2, int>)"> {
  let PublicName = "x2hmin32";
  let PublicPrototype = "int64_t(haydn_x2int32)";
  let Mnemonic = "X2HMIN32";
  let Semantics = "performs a horizontal minimum operation across the two 32-bit sub-words of rsd. The smaller of the upper 32-bit sub-word (rsd[63:32]) and the lower 32-bit sub-word (rsd[31:0]) is written to the des...";
}
def x4hmax16 : HaydnBuiltin<"int64_t(_ExtVector<4, short>)"> {
  let PublicName = "x4hmax16";
  let PublicPrototype = "int64_t(haydn_x4int16)";
  let Mnemonic = "X4HMAX16";
  let Semantics = "performs a horizontal maximum operation across all four 16-bit sub-words of rsd. The largest of the four signed 16-bit values (rsd[63:48], rsd[47:32], rsd[31:16], and rsd[15:0]) is determined and w...";
}
def x4hmin16 : HaydnBuiltin<"int64_t(_ExtVector<4, short>)"> {
  let PublicName = "x4hmin16";
  let PublicPrototype = "int64_t(haydn_x4int16)";
  let Mnemonic = "X4HMIN16";
  let Semantics = "performs a horizontal minimum operation across all four 16-bit sub-words of rsd. The smallest of the four signed 16-bit values (rsd[63:48], rsd[47:32], rsd[31:16], and rsd[15:0]) is determined and ...";
}
def x4mul16_pair : HaydnPairBuiltin<"int64_t(int64_t*, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4mul16";
  let PublicPrototype = "haydn_dpair_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4MUL16";
  let Semantics = "performs four parallel signed 16×16-bit multiplications on packed 16-bit operands stored in 64-bit data registers. Each source register rsdX is treated as four signed 16-bit integers: rsdX[63:48] -...";
}
def x2mulph32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2mulph32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MULPH32";
  let Semantics = "performs two parallel 32-bit × 32-bit signed multiplications on packed 32-bit operands and writes the upper 32 bits of each 64-bit product to rtd. The upper lane uses rsd1[63:32] and rsd2[63:32]; t...";
}
def x2mulpl32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2mulpl32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MULPL32";
  let Semantics = "performs two parallel 32-bit × 32-bit signed multiplications on packed 32-bit operands. For each lane, the full 64-bit product is computed and the lower 32 bits of the product are written to the co...";
}
def x2mulaph32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2mulaph32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MULAPH32";
  let Semantics = "performs two parallel 32-bit × 32-bit signed multiply-accumulate operations on packed 32-bit operands. For each lane, the full 64-bit product is computed, its upper 32 bits are added to the corresp...";
}
def x2mulapl32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2mulapl32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MULAPL32";
  let Semantics = "performs two parallel 32-bit × 32-bit signed multiply-accumulate operations on packed 32-bit operands. For each lane, the full 64-bit product is computed, its lower 32 bits are added to the corresp...";
}
def x2mulsph32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2mulsph32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MULSPH32";
  let Semantics = "performs two parallel 32-bit × 32-bit signed multiply-subtract operations on packed 32-bit operands. For each lane, the full 64-bit product is computed, its upper 32 bits are subtracted from the co...";
}
def x2mulspl32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2mulspl32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2MULSPL32";
  let Semantics = "performs two parallel 32-bit × 32-bit signed multiply-subtract operations on packed 32-bit operands. For each lane, the full 64-bit product is computed, its lower 32 bits are subtracted from the co...";
}
def x2fcmul32rs : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fcmul32rs";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FCMUL32RS";
  let Semantics = "performs a saturated fixed-point complex multiplication between two packed 32-bit complex Q1.31 operands. Each source operand is interpreted as a complex number in Format-1 packing: rsdX[63:32] = r...";
}
def x2fcmul32rss : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fcmul32rss";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FCMUL32RSS";
  let Semantics = "performs a saturated fixed-point complex multiplication between two packed 32-bit complex Q1.31 operands. Each source operand is interpreted as a complex number in Format-1 packing: rsdX[63:32] = r...";
}
def x2fcmula32rs : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fcmula32rs";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FCMULA32RS";
  let Semantics = "performs a saturated fixed-point complex multiply-accumulate operation on packed 32-bit complex Q1.31 operands. Each source operand is interpreted as a complex number in Format-1 packing: rsdX[63:3...";
}
def x2fcmula32rss : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2fcmula32rss";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FCMULA32RSS";
  let Semantics = "performs a saturated fixed-point complex multiply-accumulate operation on packed 32-bit complex Q1.31 operands. Each source operand is interpreted as a complex number in Format-1 packing: rsdX[63:3...";
}
def x2cmul32_f2_pair : HaydnPairBuiltin<"int64_t(int64_t*, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2cmul32_f2";
  let PublicPrototype = "haydn_dpair_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2CMUL32_F2";
  let Semantics = "performs a complex multiplication using Format-2 packing of 32-bit complex operands in 64-bit data registers. For each source register: rsdX[63:32] = imaginary part (signed 32-bit). rsdX[31:00] = r...";
}
def x2cmul32s_f2_pair : HaydnPairBuiltin<"int64_t(int64_t*, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2cmul32s_f2";
  let PublicPrototype = "haydn_dpair_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2CMUL32S_F2";
  let Semantics = "performs a saturated complex multiplication using Format-2 packing of 32-bit complex operands in 64-bit data registers. The result is returned with imaginary part in rtd1 and real part in rtd2. For...";
}
def x2ff2rsst32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2ff2rsst32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FF2RSST32";
  let Semantics = "performs two parallel symmetrical roundings of two signed Q17.47 fixed-point values in rsd1 and rsd2 to the Q1.31 range, with saturation, and writes the two packed Q1.31 results into rtd[63:32] and...";
}
def x2ff2rst32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x2ff2rst32";
  let PublicPrototype = "haydn_x2int32(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X2FF2RST32";
  let Semantics = "performs two parallel asymmetrical roundings of two signed Q17.47 fixed-point values in rsd1 and rsd2 to the Q1.31 range, with saturation, and writes the two packed Q1.31 results into rtd[63:32] an...";
}
def x4ff2mul16s_pair : HaydnPairBuiltin<"int64_t(int64_t*, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4ff2mul16s";
  let PublicPrototype = "haydn_dpair_t(haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FF2MUL16S";
  let Semantics = "performs four parallel signed 16×16-bit fixed-point multiplications on packed Q1.15 operands and returns saturated Q1.31 results. Each source register rsdX is treated as four signed 16-bit Q1.15 la...";
}
def x4ff2mula16s_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4ff2mula16s";
  let PublicPrototype = "haydn_dpair_t(int64_t, int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FF2MULA16S";
  let Semantics = "performs four parallel signed 16×16-bit fixed-point multiply-accumulate operations on packed Q1.15 operands, with Q1.31 saturation into four independent 32-bit accumulators. No rounding is applied ...";
}
def x4ff2muls16s_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "x4ff2muls16s";
  let PublicPrototype = "haydn_dpair_t(int64_t, int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "X4FF2MULS16S";
  let Semantics = "performs four parallel signed 16×16-bit fixed-point multiply-subtract operations on packed Q1.15 operands, with Q1.31 saturation into four independent 32-bit accumulators. No rounding is applied ot...";
}
def x4sat32t16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "x4sat32t16";
  let PublicPrototype = "haydn_x4int16(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "X4SAT32T16";
  let Semantics = "saturates four signed 32-bit integer values packed in rsd1 and rsd2 to the 16-bit signed range, and writes the four packed 16-bit results into rtd.";
}
def x4seli16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>, int)"> {
  let PublicName = "x4seli16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16, int)";
  let Mnemonic = "X4SELI16";
  let ImmChecks = [ImmCheck<2, ImmCheck0_15>];
  let Semantics = "selects and reorders four 16-bit sub-words from two source registers rsd1 and rsd2 according to a 4-bit immediate uimm4, and packs them into the destination register rtd. The immediate selects one ...";
}
// Reg twin of X4SELI16 (DB X4SEL16): mask in GPR — any int / variable OK.
def x4sel16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>, _ExtVector<4, short>, int)"> {
  let PublicName = "x4sel16";
  let PublicPrototype = "haydn_x4int16(haydn_x4int16, haydn_x4int16, int)";
  let Mnemonic = "X4SEL16";
  let Semantics = "selects and reorders four 16-bit sub-words from rsd1/rsd2 with lane mask in register rs (DB X4SEL16). Immediate mask: haydn_x4seli16.";
}
def movda32 : HaydnBuiltin<"int64_t(int)"> {
  let PublicName = "movda32";
  let PublicPrototype = "int64_t(int)";
  let Mnemonic = "MOVDA32";
}
def movda32x2 : HaydnBuiltin<"int64_t(int, int)"> {
  let PublicName = "movda32x2";
  let PublicPrototype = "int64_t(int, int)";
  let Mnemonic = "MOVDA32X2";
}
def movda16 : HaydnBuiltin<"int64_t(int)"> {
  let PublicName = "movda16";
  let PublicPrototype = "int64_t(int)";
  let Mnemonic = "MOVDA16";
}
def movad32_low : HaydnBuiltin<"int(int64_t)"> {
  let PublicName = "movad32_low";
  let PublicPrototype = "int(int64_t)";
  let Mnemonic = "MOVE32_DR_L";
  let Semantics = "move the lower 32 bits of rsd to rt.";
}
def movad32_high : HaydnBuiltin<"int(int64_t)"> {
  let PublicName = "movad32_high";
  let PublicPrototype = "int(int64_t)";
  let Mnemonic = "MOVE32_DR_H";
  let Semantics = "move the upper 32 bits of rsd to rt.";
}
def mulfc32x16ras_low : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulfc32x16ras_low";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULFC32X16RAS_LOW";
}
def mulfc32x16ras_high : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulfc32x16ras_high";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULFC32X16RAS_HIGH";
}
def mulaa32s_fir_hh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mulaa32s_fir_hh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULAA32S_FIR_HH";
}
def mulaa32s_fir_hl : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mulaa32s_fir_hl";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULAA32S_FIR_HL";
}
def mulfq16x2_fir_3 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulfq16x2_fir_3";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULFQ16X2_FIR_3";
}
def mulfq16x2_fir_1 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulfq16x2_fir_1";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULFQ16X2_FIR_1";
}
def mulafq16x2_fir_3 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulafq16x2_fir_3";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAFQ16X2_FIR_3";
}
def mulafq16x2_fir_1 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulafq16x2_fir_1";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAFQ16X2_FIR_1";
}
def f2mulaa32r_hhll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulaa32r_hhll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULAA32R_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by ROUNDING17.47(). Both Q17.47 pr...";
}
def f2mulaa32r_hllh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulaa32r_hllh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULAA32R_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by ROUNDING17.47(). Both Q17.47 pr...";
}
def f2mulss32r_hhll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulss32r_hhll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULSS32R_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by ROUNDING17.47(). Both Q17.47 pr...";
}
def f2mulss32r_hllh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulss32r_hllh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULSS32R_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by ROUNDING17.47(). Both Q17.47 pr...";
}
def f2mulzaa32rs_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzaa32rs_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZAA32RS_HHLL";
  let Semantics = "performs two signed fixed-point multiplications between pairs of 32-bit Q1.31 operands. Each 32×32-bit multiplication produces a 64-bit intermediate product in Q2.62 format. The product is then con...";
}
def f2mulzaa32rs_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzaa32rs_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZAA32RS_HLLH";
  let Semantics = "performs two signed fixed-point multiplications between pairs of 32-bit Q1.31 operands. Each 32×32-bit multiplication produces a 64-bit intermediate product in Q2.62 format. The product is then con...";
}
def f2mulzaa32r_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzaa32r_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZAA32R_HHLL";
  let Semantics = "performs two signed fixed-point multiplications between pairs of 32-bit Q1.31 operands. Each 32×32-bit multiplication produces a 64-bit intermediate product in Q2.62 format. The product is then con...";
}
def f2mulzaa32r_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzaa32r_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZAA32R_HLLH";
  let Semantics = "performs two signed fixed-point multiplications between pairs of 32-bit Q1.31 operands. Each 32×32-bit multiplication produces a 64-bit intermediate product in Q2.62 format. The product is then con...";
}
def f2mulzas32rs_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzas32rs_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZAS32RS_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by SYM_ROUNDING17.47(). The upper-...";
}
def f2mulzas32rs_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzas32rs_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZAS32RS_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by SYM_ROUNDING17.47(). The upper-...";
}
def f2mulzas32r_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzas32r_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZAS32R_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by ROUNDING17.47(). The upper-lane...";
}
def f2mulzas32r_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzas32r_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZAS32R_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit Q2.62 product is converted to Q17.47 by ROUNDING17.47(). The upper-lane...";
}
def f2mulzsa32rs_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzsa32rs_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZSA32RS_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit product is a signed Q2.62 value, which is converted to Q17.47 by SYM_RO...";
}
def f2mulzsa32rs_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzsa32rs_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZSA32RS_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit product is a signed Q2.62 value, which is converted to Q17.47 by SYM_RO...";
}
def f2mulzsa32r_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzsa32r_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZSA32R_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit product is a signed Q2.62 value, which is converted to Q17.47 by ROUNDI...";
}
def f2mulzsa32r_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzsa32r_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZSA32R_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit product is a signed Q2.62 value, which is converted to Q17.47 by ROUNDI...";
}
def f2mulzss32rs_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzss32rs_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZSS32RS_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit product is a signed Q2.62 value, which is converted to Q17.47 by SYM_RO...";
}
def f2mulzss32rs_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzss32rs_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZSS32RS_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit product is a signed Q2.62 value, which is converted to Q17.47 by SYM_RO...";
}
def f2mulzss32r_hhll : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzss32r_hhll";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZSS32R_HHLL";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit product is a signed Q2.62 value, which is converted to Q17.47 by ROUNDI...";
}
def f2mulzss32r_hllh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "f2mulzss32r_hllh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "F2MULZSS32R_HLLH";
  let Semantics = "performs two signed fixed-point multiplications using the upper and lower 32-bit Q1.31 lanes of rsd1 and rsd2. Each 32×32-bit product is a signed Q2.62 value, which is converted to Q17.47 by ROUNDI...";
}
def fmula16_hs00 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_hs00";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_HS00";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the upper 32 bits of rtd. Each 64-bit source register...";
}
def fmula16_hs01 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_hs01";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_HS01";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the upper 32 bits of rtd.";
}
def fmula16_hs02 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_hs02";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_HS02";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the upper 32 bits of rtd.";
}
def fmula16_hs03 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_hs03";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_HS03";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the upper 32 bits of rtd.";
}
def fmula16_hs11 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_hs11";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_HS11";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the upper 32 bits of rtd.";
}
def fmula16_hs12 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_hs12";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_HS12";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the upper 32 bits of rtd.";
}
def fmula16_hs13 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_hs13";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_HS13";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the upper 32 bits of rtd.";
}
def fmula16_hs22 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_hs22";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_HS22";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the upper 32 bits of rtd.";
}
def fmula16_hs23 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_hs23";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_HS23";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the upper 32 bits of rtd.";
}
def fmula16_hs33 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_hs33";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_HS33";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the upper 32 bits of rtd.";
}
def fmula16_ls00 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_ls00";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_LS00";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the lower 32 bits of rtd. Each 64-bit source register...";
}
def fmula16_ls01 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_ls01";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_LS01";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the lower 32 bits of rtd.";
}
def fmula16_ls02 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_ls02";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_LS02";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the lower 32 bits of rtd.";
}
def fmula16_ls03 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_ls03";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_LS03";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the lower 32 bits of rtd.";
}
def fmula16_ls11 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_ls11";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_LS11";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the lower 32 bits of rtd.";
}
def fmula16_ls12 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_ls12";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_LS12";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the lower 32 bits of rtd.";
}
def fmula16_ls13 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_ls13";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_LS13";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the lower 32 bits of rtd.";
}
def fmula16_ls22 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_ls22";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_LS22";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the lower 32 bits of rtd.";
}
def fmula16_ls23 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_ls23";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_LS23";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the lower 32 bits of rtd.";
}
def fmula16_ls33 : HaydnBuiltin<"int64_t(int64_t, _ExtVector<4, short>, _ExtVector<4, short>)"> {
  let PublicName = "fmula16_ls33";
  let PublicPrototype = "int64_t(int64_t, haydn_x4int16, haydn_x4int16)";
  let Mnemonic = "FMULA16_LS33";
  let Semantics = "perform a signed fixed-point multiply-accumulate operation on selected 16-bit Q1.15 lanes and accumulate the saturated 32-bit Q1.31 result into the lower 32 bits of rtd.";
}
def fmulaa32s_hhll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "fmulaa32s_hhll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FMULAA32S_HHLL";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation on two 32-bit Q1.31 lane pairs with add/add combination and writes the saturated Q1.63 result to rtd. The instruction operates on the pac...";
}
def fmulaa32s_hllh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "fmulaa32s_hllh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "FMULAA32S_HLLH";
  let Semantics = "performs a signed fixed-point multiply-accumulate operation on two cross-lane 32-bit Q1.31 pairs with add/add combination and writes the saturated Q1.63 result to rtd. The instruction operates on t...";
}
def fmulss32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "fmulss32s_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "FMULSS32S_HHLL";
  let Semantics = "performs a signed fixed-point multiply-subtract operation on two 32-bit Q1.31 lane pairs with sub/sub combination and writes the saturated Q1.63 result to rtd. The instruction operates on the packe...";
}
def fmulss32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "fmulss32s_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "FMULSS32S_HLLH";
  let Semantics = "performs a signed fixed-point multiply-subtract operation on two cross-lane 32-bit Q1.31 pairs with sub/sub combination and writes the saturated Q1.63 result to rtd. The instruction operates on the...";
}
def fmulzaa16_hs_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzaa16_hs_11_00";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZAA16_HS_11_00";
  let Semantics = "perform the sum of two signed 16-bit Q1.15 products selected from 64-bit source registers rsd1 and rsd2. The saturated Q1.31 result is written to the upper 32 bits of rtd (rtd[63:32]). The accumula...";
}
def fmulzaa16_hs_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzaa16_hs_13_02";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZAA16_HS_13_02";
  let Semantics = "perform the sum of two signed 16-bit Q1.15 products selected from 64-bit source registers rsd1 and rsd2.";
}
def fmulzaa16_hs_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzaa16_hs_33_22";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZAA16_HS_33_22";
  let Semantics = "perform the sum of two signed 16-bit Q1.15 products selected from 64-bit source registers rsd1 and rsd2.";
}
def fmulzaa16_ls_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzaa16_ls_11_00";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZAA16_LS_11_00";
  let Semantics = "perform the sum of two signed fixed-point 16-bit Q1.15 products, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd. The accumulator is implicitly zero (the previous value in r...";
}
def fmulzaa16_ls_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzaa16_ls_13_02";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZAA16_LS_13_02";
  let Semantics = "perform the sum of two signed fixed-point 16-bit Q1.15 products, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd. The accumulator is implicitly zero (the previous value in r...";
}
def fmulzaa16_ls_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzaa16_ls_33_22";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZAA16_LS_33_22";
  let Semantics = "perform the sum of two signed fixed-point 16-bit Q1.15 products, and write the saturated 32-bit Q1.31 result to the lower 32 bits of rtd. The accumulator is implicitly zero (the previous value in r...";
}
def fmulzaa32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzaa32s_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZAA32S_HHLL";
  let Semantics = "performs a signed fixed-point accumulation of two 32-bit Q1.31 products and writes the saturated Q1.63 result to rtd. The instruction multiplies: the upper 32-bit Q1.31 operand of rsd1 by the upper...";
}
def fmulzaa32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzaa32s_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZAA32S_HLLH";
  let Semantics = "performs a signed fixed-point accumulation of two 32-bit Q1.31 products and writes the saturated Q1.63 result to rtd. The instruction multiplies: the upper 32-bit Q1.31 operand of rsd1 by the lower...";
}
def fmulzas32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzas32s_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZAS32S_HHLL";
  let Semantics = "performs a signed fixed-point zero-start multiply-add/subtract operation on two 32-bit Q1.31 lane pairs and writes the saturated Q1.63 result to rtd. The instruction operates on the packed Q1.31 op...";
}
def fmulzas32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzas32s_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZAS32S_HLLH";
  let Semantics = "performs a signed fixed-point zero-start multiply-add/subtract operation on two cross-lane 32-bit Q1.31 pairs and writes the saturated Q1.63 result to rtd. The instruction operates on the packed Q1...";
}
def fmulzsa32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzsa32s_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZSA32S_HHLL";
  let Semantics = "performs a signed fixed-point zero-start multiply-subtract/add operation on two 32-bit Q1.31 lane pairs and writes the saturated Q1.63 result to rtd. The instruction operates on the packed Q1.31 op...";
}
def fmulzsa32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzsa32s_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZSA32S_HLLH";
  let Semantics = "performs a signed fixed-point zero-start multiply-subtract/add operation on two cross-lane 32-bit Q1.31 pairs and writes the saturated Q1.63 result to rtd. The instruction operates on the packed Q1...";
}
def fmulzss16_hs_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzss16_hs_11_00";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZSS16_HS_11_00";
  let Semantics = "perform the negated sum of two signed 16-bit Q1.15 products selected from 64-bit source registers rsd1 and rsd2. The saturated Q1.31 result is written to the upper 32 bits of rtd (rtd[63:32]). The ...";
}
def fmulzss16_hs_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzss16_hs_13_02";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZSS16_HS_13_02";
  let Semantics = "perform the negated sum of two signed 16-bit Q1.15 products selected from 64-bit source registers rsd1 and rsd2.";
}
def fmulzss16_hs_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzss16_hs_33_22";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZSS16_HS_33_22";
  let Semantics = "perform the negated sum of two signed 16-bit Q1.15 products selected from 64-bit source registers rsd1 and rsd2.";
}
def fmulzss16_ls_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzss16_ls_11_00";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZSS16_LS_11_00";
  let Semantics = "perform the negated sum of two signed 16-bit Q1.15 products selected from 64-bit source registers rsd1 and rsd2. The saturated Q1.31 result is written to the lower 32 bits of rtd. The accumulator i...";
}
def fmulzss16_ls_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzss16_ls_13_02";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZSS16_LS_13_02";
  let Semantics = "perform the negated sum of two signed 16-bit Q1.15 products selected from 64-bit source registers rsd1 and rsd2.";
}
def fmulzss16_ls_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzss16_ls_33_22";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZSS16_LS_33_22";
  let Semantics = "perform the negated sum of two signed 16-bit Q1.15 products selected from 64-bit source registers rsd1 and rsd2.";
}
def fmulzss32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzss32s_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZSS32S_HHLL";
  let Semantics = "performs a signed fixed-point zero-start multiply-subtract operation on two pairs of 32-bit Q1.31 operands and writes the saturated Q1.63 result to rtd. The instruction operates on the packed Q1.31...";
}
def fmulzss32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "fmulzss32s_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "FMULZSS32S_HLLH";
  let Semantics = "performs a signed fixed-point zero-start multiply-subtract operation on two cross-lane pairs of 32-bit Q1.31 operands and writes the saturated Q1.63 result to rtd. The instruction operates on the p...";
}
def mul16aq : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mul16aq";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MUL16AQ";
  let Semantics = "performs four parallel signed 16-bit × 16-bit multiplications on packed 16-bit operands from rsd1 and rsd2, and accumulates the sum of all four 32-bit products into the 64-bit destination register ...";
}
def mul16zaq : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mul16zaq";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MUL16ZAQ";
  let Semantics = "performs four parallel signed 16-bit × 16-bit multiplications on packed 16-bit operands from rsd1 and rsd2, and accumulates the sum of all four 32-bit products into the 64-bit destination register ...";
}
def mul64_hh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_hh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_HH";
  let Semantics = "Performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2. The full 64-bit product is written to rtd.";
}
def mul64_hl : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_hl";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_HL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2. The full 64-bit product is written to rtd.";
}
def mul64_huh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_huh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_HUH";
  let Semantics = "Performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned. The full 64-bit product is written to rtd.";
}
def mul64_hul : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_hul";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_HUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned. The full 64-bit product is written to rtd.";
}
def mul64_lh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_lh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_LH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2. The full 64-bit product is written to rtd.";
}
def mul64_luh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_luh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_LUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned. The full 64-bit product is written to rtd.";
}
def mul64_lul : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_lul";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_LUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned. The full 64-bit product is written to rtd.";
}
def mul64_uhh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_uhh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_UHH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed. The full 64-bit product is written to rtd.";
}
def mul64_uhl : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_uhl";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_UHL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed. The full 64-bit product is written to rtd.";
}
def mul64_uhuh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_uhuh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_UHUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2. The full 64-bit product is written to rtd.";
}
def mul64_uhul : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_uhul";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_UHUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2. The full 64-bit product is written to rtd.";
}
def mul64_ulh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_ulh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed. The full 64-bit product is written to rtd.";
}
def mul64_ull : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_ull";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_ULL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed. The full 64-bit product is written to rtd.";
}
def mul64_uluh : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_uluh";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_ULUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2. The full 64-bit product is written to rtd.";
}
def mul64_ulul : HaydnBuiltin<"int64_t(_ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mul64_ulul";
  let PublicPrototype = "int64_t(haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MUL64_ULUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2. The full 64-bit product is written to rtd.";
}
def mula64_hh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_hh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_HH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_hl : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_hl";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_HL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_huh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_huh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_HUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned, and accumulates the 64-bit product into rtd.";
}
def mula64_hul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_hul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_HUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, and accumulates the 64-bit product into rtd.";
}
def mula64_lh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_lh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_LH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_ll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_ll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_LL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, and accumulates the 64-bit product into the 64-bit target register rtd.";
}
def mula64_luh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_luh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_LUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned, and accumulates the 64-bit product into rtd.";
}
def mula64_lul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_lul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_LUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, and accumulates the 64-bit product into rtd.";
}
def mula64_uhh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_uhh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_UHH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, and accumulates the 64-bit product into rtd.";
}
def mula64_uhl : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_uhl";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_UHL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, and accumulates the 64-bit product into rtd.";
}
def mula64_uhuh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_uhuh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_UHUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_uhul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_uhul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_UHUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_ulh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_ulh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, and accumulates the 64-bit product into rtd.";
}
def mula64_ull : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_ull";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_ULL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, and accumulates the 64-bit product into rtd.";
}
def mula64_uluh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_uluh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_ULUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, and accumulates the 64-bit product into rtd.";
}
def mula64_ulul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mula64_ulul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULA64_ULUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, and accumulates the 64-bit product into rtd.";
}
def mulaa32_hhll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mulaa32_hhll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULAA32_HHLL";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications and accumulates both 64-bit products into the 64-bit accumulator rtd by addition. No saturation is performed; the accumulation uses 64-b...";
}
def mulaa32_hllh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "mulaa32_hllh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULAA32_HLLH";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications and accumulates both 64-bit products into the 64-bit accumulator rtd by addition. No saturation is performed; the accumulation uses 64-b...";
}
def mulas32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas32_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS32_HHLL";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications, adds the first 64-bit product to the 64-bit accumulator rtd, and subtracts the second 64-bit product from the result. No saturation is ...";
}
def mulas32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas32_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS32_HLLH";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications, adds the first 64-bit product to the 64-bit accumulator rtd, and subtracts the second 64-bit product from the result. No saturation is ...";
}
def mulas64_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_hh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_HH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_hl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_hl";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_HL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_huh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_huh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_HUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned, then adds the 64-bit product to rtd with signed 64-b...";
}
def mulas64_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_hul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_HUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, then adds the 64-bit product to rtd with signed 64-b...";
}
def mulas64_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_lh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_LH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_ll";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_LL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, then adds the 64-bit product to the 64-bit accumulator rtd with signed 64-bit saturation.";
}
def mulas64_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_luh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_LUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned, then adds the 64-bit product to rtd with signed 64-b...";
}
def mulas64_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_lul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_LUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, then adds the 64-bit product to rtd with signed 64-b...";
}
def mulas64_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_uhh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_UHH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, then adds the 64-bit product to rtd with signed 64-...";
}
def mulas64_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_uhl";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_UHL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, then adds the 64-bit product to rtd with signed 64-...";
}
def mulas64_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_uhuh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_UHUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_uhul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_UHUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_ulh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, then adds the 64-bit product to rtd with signed 64-...";
}
def mulas64_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_ull";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_ULL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, then adds the 64-bit product to rtd with signed 64-...";
}
def mulas64_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_uluh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_ULUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, then adds the 64-bit product to rtd with signed 64-bit saturation.";
}
def mulas64_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulas64_ulul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAS64_ULUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, then adds the 64-bit product to the 64-bit accumulator rtd with signed 64-bit saturation.";
}
def muls64_hh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_hh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_HH";
  let Semantics = "signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and rsd2, then subtracts the product from rtd.";
}
def muls64_hl : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_hl";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_HL";
  let Semantics = "signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, then subtracts the product from rtd.";
}
def muls64_huh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_huh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_HUH";
  let Semantics = "signed × unsigned multiplication using the upper 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned, then subtracts the product from rtd.";
}
def muls64_hul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_hul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_HUL";
  let Semantics = "signed × unsigned multiplication using the upper 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, then subtracts the product from rtd.";
}
def muls64_lh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_lh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_LH";
  let Semantics = "signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, then subtracts the product from rtd.";
}
def muls64_ll : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_ll";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_LL";
  let Semantics = "signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, then subtracts the 64-bit product from the 64-bit accumulator rtd.";
}
def muls64_luh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_luh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_LUH";
  let Semantics = "signed × unsigned multiplication using the lower 32 bits of rsd1 as signed and the upper 32 bits of rsd2 as unsigned, then subtracts the product from rtd.";
}
def muls64_lul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_lul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_LUL";
  let Semantics = "signed × unsigned multiplication using the lower 32 bits of rsd1 as signed and the lower 32 bits of rsd2 as unsigned, then subtracts the product from rtd.";
}
def muls64_uhh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_uhh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_UHH";
  let Semantics = "unsigned × signed multiplication using the upper 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, then subtracts the product from rtd.";
}
def muls64_uhl : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_uhl";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_UHL";
  let Semantics = "unsigned × signed multiplication using the upper 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, then subtracts the product from rtd.";
}
def muls64_uhuh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_uhuh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_UHUH";
  let Semantics = "unsigned × unsigned multiplication using the upper 32 bits of rsd1 and rsd2, then subtracts the product from rtd.";
}
def muls64_uhul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_uhul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_UHUL";
  let Semantics = "unsigned × unsigned multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2, then subtracts the product from rtd.";
}
def muls64_ulh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_ulh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_ULH";
  let Semantics = "unsigned × signed multiplication using the lower 32 bits of rsd1 as unsigned and the upper 32 bits of rsd2 as signed, then subtracts the product from rtd.";
}
def muls64_ull : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_ull";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_ULL";
  let Semantics = "unsigned × signed multiplication using the lower 32 bits of rsd1 as unsigned and the lower 32 bits of rsd2 as signed, then subtracts the product from rtd.";
}
def muls64_uluh : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_uluh";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_ULUH";
  let Semantics = "unsigned × unsigned multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2, then subtracts the product from rtd.";
}
def muls64_ulul : HaydnBuiltin<"int64_t(int64_t, _ExtVector<2, int>, _ExtVector<2, int>)"> {
  let PublicName = "muls64_ulul";
  let PublicPrototype = "int64_t(int64_t, haydn_x2int32, haydn_x2int32)";
  let Mnemonic = "MULS64_ULUL";
  let Semantics = "unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2, then subtracts the product from rtd.";
}
def mulss64_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_hh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_HH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the upper 32 bits of rsd2. The 64-bit product is subtracted from the 64-bit accumulator rtd, and the re...";
}
def mulss64_hl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_hl";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_HL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2. The 64-bit product is subtracted from the 64-bit accumulator rtd, and the re...";
}
def mulss64_huh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_huh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_HUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 (signed) and the upper 32 bits of rsd2 (unsigned). The 64-bit product is subtracted from rtd, and the res...";
}
def mulss64_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_hul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_HUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 (signed) and the lower 32 bits of rsd2 (unsigned). The 64-bit product is subtracted from rtd, and the res...";
}
def mulss64_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_lh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_LH";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2. The 64-bit product is subtracted from the 64-bit accumulator rtd, and the re...";
}
def mulss64_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_ll";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_LL";
  let Semantics = "performs a signed × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and rsd2. The 64-bit product is subtracted from the 64-bit accumulator rtd, and the result is written back ...";
}
def mulss64_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_luh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_LUH";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 (signed) and the upper 32 bits of rsd2 (unsigned). The 64-bit product is subtracted from rtd, and the res...";
}
def mulss64_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_lul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_LUL";
  let Semantics = "performs a signed × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 (signed) and the lower 32 bits of rsd2 (unsigned). The 64-bit product is subtracted from rtd, and the res...";
}
def mulss64_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_uhh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_UHH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 (unsigned) and the upper 32 bits of rsd2 (signed). The 64-bit product is subtracted from rtd, and the re...";
}
def mulss64_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_uhl";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_UHL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 (unsigned) and the lower 32 bits of rsd2 (signed). The 64-bit product is subtracted from rtd, and the re...";
}
def mulss64_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_uhuh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_UHUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the upper 32 bits of rsd2. The 64-bit product is subtracted from rtd, and the result is written ba...";
}
def mulss64_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_uhul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_UHUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the upper 32 bits of rsd1 and the lower 32 bits of rsd2. The 64-bit product is subtracted from rtd, and the result is written ba...";
}
def mulss64_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_ulh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_ULH";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 (unsigned) and the upper 32 bits of rsd2 (signed). The 64-bit product is subtracted from rtd, and the re...";
}
def mulss64_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_ull";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_ULL";
  let Semantics = "performs an unsigned × signed 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 (interpreted as unsigned) and the lower 32 bits of rsd2 (interpreted as signed). The 64-bit product is s...";
}
def mulss64_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_uluh";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_ULUH";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the upper 32 bits of rsd2. The 64-bit product is subtracted from rtd, and the result is written ba...";
}
def mulss64_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulss64_ulul";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULSS64_ULUL";
  let Semantics = "performs an unsigned × unsigned 32-bit × 32-bit multiplication using the lower 32 bits of rsd1 and the lower 32 bits of rsd2. The 64-bit product is subtracted from rtd, and the result is written ba...";
}
def mulzaa32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulzaa32_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULZAA32_HHLL";
  let Semantics = "performs two signed 32-bit × 32-bit multiplications using the upper and lower 32-bit parts of rsd1 and rsd2. The two 64-bit products are added together, and the 64-bit sum is written to rtd. The pr...";
}
def mulzaa32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulzaa32_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULZAA32_HLLH";
  let Semantics = "performs two signed 32-bit × 32-bit cross multiplications between the upper and lower 32-bit parts of rsd1 and rsd2. The two 64-bit products are added together using 64-bit two's-complement arithme...";
}
def mulzas32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulzas32_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULZAS32_HHLL";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications. The first 64-bit product is added to zero and the second 64-bit product is subtracted from the sum. The 64-bit result is written to rtd...";
}
def mulzas32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulzas32_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULZAS32_HLLH";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications. The first 64-bit product is added to zero and the second 64-bit product is subtracted from the sum. The 64-bit result is written to rtd...";
}
def mulzsa32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulzsa32_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULZSA32_HHLL";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications. The first 64-bit product is subtracted from zero and the second 64-bit product is added to the difference. The 64-bit result is written...";
}
def mulzsa32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulzsa32_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULZSA32_HLLH";
  let Semantics = "performs two signed × signed 32-bit × 32-bit multiplications. The first 64-bit product is subtracted from zero and the second 64-bit product is added to the difference. The 64-bit result is written...";
}
def mulzss32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulzss32_hhll";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULZSS32_HHLL";
  let Semantics = "performs two signed 32-bit × 32-bit multiplications and subtracts both 64-bit products from zero. The previous value of rtd is discarded. No saturation is performed; the result uses 64-bit two's-co...";
}
def mulzss32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "mulzss32_hllh";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "MULZSS32_HLLH";
  let Semantics = "performs two signed 32-bit × 32-bit multiplications and subtracts both 64-bit products from zero. The previous value of rtd is discarded. No saturation is performed; the result uses 64-bit two's-co...";
}
def mul16js : HaydnAeBuiltin<"int64_t(int64_t)"> {
  let PublicName = "mul16js";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "MUL16JS";
  // AE_MUL16JS → X4MJSWAP16S (multiply-by-j).
  let CodeGen = "LanewiseUnary:x4mjswap16s";
}
def conj16s : HaydnAeBuiltin<"int64_t(int64_t)"> {
  let PublicName = "conj16s";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "CONJ16S";
  // AE_CONJ16S → X4CONJ16S.
  let CodeGen = "LanewiseUnary:x4conj16s";
}
def mulafc16ras : HaydnAeBuiltin<"int64_t(int64_t, int64_t, int64_t)"> {
  let PublicName = "mulafc16ras";
  let PublicPrototype = "int64_t(int64_t, int64_t, int64_t)";
  let Mnemonic = "MULAFC16RAS";
  // AE_MULAFC16RAS → X4FCMULA16RS (true accumulate form).
  let CodeGen = "TernaryI64:x4fcmula16rs";
}
def addandsubrng16ras_s0 : HaydnAeBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "addandsubrng16ras_s0";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "ADDANDSUBRNG16RAS_S0";
  // Slot suffix is HiFi-only; all three lower identically (D210).
  let CodeGen = "AddAndSubRng";
}
def addandsubrng16ras_s1 : HaydnAeBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "addandsubrng16ras_s1";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "ADDANDSUBRNG16RAS_S1";
  let CodeGen = "AddAndSubRng";
}
def addandsubrng16ras_s2 : HaydnAeBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "addandsubrng16ras_s2";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "ADDANDSUBRNG16RAS_S2";
  let CodeGen = "AddAndSubRng";
}
} // Attributes = [NoThrow, Const]

//===----------------------------------------------------------------------===
// Golden D-ALU coverage (auto-added; S-ALU skipped by policy)
//===----------------------------------------------------------------------===
def add64 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "add64";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "ADD64";
  let Semantics = "Golden D-ALU ADD64.";
}
def sub64 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "sub64";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SUB64";
  let Semantics = "Golden D-ALU SUB64.";
}
def xor64 : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "xor64";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "XOR64";
  let Semantics = "Golden D-ALU XOR64.";
}
def add64s_h : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "add64s_h";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "ADD64S_H";
  let Semantics = "Golden D-ALU ADD64S_H.";
}
def add64s_l : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "add64s_l";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "ADD64S_L";
  let Semantics = "Golden D-ALU ADD64S_L.";
}
def sub64s_h : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "sub64s_h";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SUB64S_H";
  let Semantics = "Golden D-ALU SUB64S_H.";
}
def sub64s_l : HaydnBuiltin<"int64_t(int64_t, int64_t)"> {
  let PublicName = "sub64s_l";
  let PublicPrototype = "int64_t(int64_t, int64_t)";
  let Mnemonic = "SUB64S_L";
  let Semantics = "Golden D-ALU SUB64S_L.";
}
def move64 : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "move64";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "MOVE64";
  let Semantics = "Golden D-ALU MOVE64.";
}
def transf64_h : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "transf64_h";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "TRANSF64_H";
  let Semantics = "Golden D-ALU TRANSF64_H.";
}
def transf64_l : HaydnBuiltin<"int64_t(int64_t)"> {
  let PublicName = "transf64_l";
  let PublicPrototype = "int64_t(int64_t)";
  let Mnemonic = "TRANSF64_L";
  let Semantics = "Golden D-ALU TRANSF64_L.";
}
def sext32t64 : HaydnBuiltin<"int64_t(int)"> {
  let PublicName = "sext32t64";
  let PublicPrototype = "int64_t(int)";
  let Mnemonic = "SEXT32T64";
  let Semantics = "Golden D-ALU SEXT32T64.";
}
def transf64 : HaydnBuiltin<"int64_t(int)"> {
  let PublicName = "transf64";
  let PublicPrototype = "int64_t(int)";
  let Mnemonic = "TRANSF64";
  let Semantics = "Golden D-ALU TRANSF64.";
}
def transf64f2 : HaydnBuiltin<"int64_t(int)"> {
  let PublicName = "transf64f2";
  let PublicPrototype = "int64_t(int)";
  let Mnemonic = "TRANSF64F2";
  let Semantics = "Golden D-ALU TRANSF64F2.";
}
def movei_h : HaydnBuiltin<"int64_t(int)"> {
  let PublicName = "movei_h";
  let PublicPrototype = "int64_t(int)";
  let Mnemonic = "MOVEI_H";
  let Semantics = "Golden D-ALU MOVEI_H — materialize imm32 into DR high half.";
}
def movei_l : HaydnBuiltin<"int64_t(int)"> {
  let PublicName = "movei_l";
  let PublicPrototype = "int64_t(int)";
  let Mnemonic = "MOVEI_L";
  let Semantics = "Golden D-ALU MOVEI_L — materialize imm32 into DR low half.";
}
def zero_dr : HaydnBuiltin<"int64_t()"> {
  let PublicName = "zero_dr";
  let PublicPrototype = "int64_t()";
  let Mnemonic = "ZERO_DR";
  let Semantics = "Golden D-ALU ZERO_DR.";
}
def sin_cos : HaydnBuiltin<"int64_t(int, int)"> {
  let PublicName = "sin_cos";
  let PublicPrototype = "int64_t(int, int)";
  let Mnemonic = "SIN_COS";
  let ImmChecks = [ImmCheck<1, ImmCheck0_15>];
  let Semantics = "Golden D-ALU SIN_COS rtd, rs, uimm4.";
}

//===----------------------------------------------------------------------===
// Golden Load&Store coverage (D_* and S_*; systematic)
//===----------------------------------------------------------------------===
def d_ldw_post_imm_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_ldw_post_imm";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LDW_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_LDW_POST_IMM: data + AGU writeback (mem64).";
}
def d_ldw_post_reg_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_ldw_post_reg";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LDW_POST_REG";
  let Semantics = "Golden LS D_LDW_POST_REG: data + AGU writeback (mem64).";
}
def d_ldw_pre_imm_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_ldw_pre_imm";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LDW_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_LDW_PRE_IMM: data + AGU writeback (mem64).";
}
def d_ldw_pre_reg_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_ldw_pre_reg";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LDW_PRE_REG";
  let Semantics = "Golden LS D_LDW_PRE_REG: data + AGU writeback (mem64).";
}
def d_ldw_with_imm : HaydnBuiltin<"int64_t(int, int)"> {
  let PublicName = "d_ldw_with_imm";
  let PublicPrototype = "int64_t(int, int)";
  let Mnemonic = "D_LDW_WITH_IMM";
  let ImmChecks = [ImmCheck<1, ImmCheckSimm6>];
  let Semantics = "Golden LS D_LDW_WITH_IMM.";
}
def d_ldw_with_reg : HaydnBuiltin<"int64_t(int, int)"> {
  let PublicName = "d_ldw_with_reg";
  let PublicPrototype = "int64_t(int, int)";
  let Mnemonic = "D_LDW_WITH_REG";
  let Semantics = "Golden LS D_LDW_WITH_REG.";
}
def d_lhw_post_imm_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_lhw_post_imm";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LHW_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_LHW_POST_IMM: data + AGU writeback (broadcast mem16→DR64).";
}
def d_lhw_post_reg_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_lhw_post_reg";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LHW_POST_REG";
  let Semantics = "Golden LS D_LHW_POST_REG: data + AGU writeback (broadcast mem16→DR64).";
}
def d_lhw_pre_imm_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_lhw_pre_imm";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LHW_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_LHW_PRE_IMM: data + AGU writeback (broadcast mem16→DR64).";
}
def d_lhw_pre_reg_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_lhw_pre_reg";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LHW_PRE_REG";
  let Semantics = "Golden LS D_LHW_PRE_REG: data + AGU writeback (broadcast mem16→DR64).";
}
def d_lhw_with_imm : HaydnBuiltin<"int64_t(int, int)"> {
  let PublicName = "d_lhw_with_imm";
  let PublicPrototype = "int64_t(int, int)";
  let Mnemonic = "D_LHW_WITH_IMM";
  let ImmChecks = [ImmCheck<1, ImmCheckSimm6>];
  let Semantics = "Golden LS D_LHW_WITH_IMM.";
}
def d_lhw_with_reg : HaydnBuiltin<"int64_t(int, int)"> {
  let PublicName = "d_lhw_with_reg";
  let PublicPrototype = "int64_t(int, int)";
  let Mnemonic = "D_LHW_WITH_REG";
  let Semantics = "Golden LS D_LHW_WITH_REG.";
}
def d_lw_post_imm_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_lw_post_imm";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LW_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_LW_POST_IMM: data + AGU writeback (broadcast mem32→DR64).";
}
def d_lw_post_reg_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_lw_post_reg";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LW_POST_REG";
  let Semantics = "Golden LS D_LW_POST_REG: data + AGU writeback (broadcast mem32→DR64).";
}
def d_lw_pre_imm_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_lw_pre_imm";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LW_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_LW_PRE_IMM: data + AGU writeback (broadcast mem32→DR64).";
}
def d_lw_pre_reg_pair : HaydnPairBuiltin<"int64_t(int*, int, int)"> {
  let PublicName = "d_lw_pre_reg";
  let PublicPrototype = "haydn_cb_ld_t(int, int)";
  let Mnemonic = "D_LW_PRE_REG";
  let Semantics = "Golden LS D_LW_PRE_REG: data + AGU writeback (broadcast mem32→DR64).";
}
def d_lw_with_imm : HaydnBuiltin<"int64_t(int, int)"> {
  let PublicName = "d_lw_with_imm";
  let PublicPrototype = "int64_t(int, int)";
  let Mnemonic = "D_LW_WITH_IMM";
  let ImmChecks = [ImmCheck<1, ImmCheckSimm6>];
  let Semantics = "Golden LS D_LW_WITH_IMM.";
}
def d_lw_with_reg : HaydnBuiltin<"int64_t(int, int)"> {
  let PublicName = "d_lw_with_reg";
  let PublicPrototype = "int64_t(int, int)";
  let Mnemonic = "D_LW_WITH_REG";
  let Semantics = "Golden LS D_LW_WITH_REG.";
}
def d_sdw_post_imm : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sdw_post_imm";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SDW_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SDW_POST_IMM.";
}
def d_sdw_post_reg : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sdw_post_reg";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SDW_POST_REG";
  let Semantics = "Golden LS D_SDW_POST_REG.";
}
def d_sdw_pre_imm : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sdw_pre_imm";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SDW_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SDW_PRE_IMM.";
}
def d_sdw_pre_reg : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sdw_pre_reg";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SDW_PRE_REG";
  let Semantics = "Golden LS D_SDW_PRE_REG.";
}
def d_sdw_with_imm : HaydnBuiltin<"void(int64_t, int, int)"> {
  let PublicName = "d_sdw_with_imm";
  let PublicPrototype = "void(int64_t, int, int)";
  let Mnemonic = "D_SDW_WITH_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SDW_WITH_IMM.";
}
def d_sdw_with_reg : HaydnBuiltin<"void(int64_t, int, int)"> {
  let PublicName = "d_sdw_with_reg";
  let PublicPrototype = "void(int64_t, int, int)";
  let Mnemonic = "D_SDW_WITH_REG";
  let Semantics = "Golden LS D_SDW_WITH_REG.";
}
def d_shw_post_imm : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_shw_post_imm";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SHW_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SHW_POST_IMM.";
}
def d_shw_post_reg : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_shw_post_reg";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SHW_POST_REG";
  let Semantics = "Golden LS D_SHW_POST_REG.";
}
def d_shw_pre_imm : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_shw_pre_imm";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SHW_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SHW_PRE_IMM.";
}
def d_shw_pre_reg : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_shw_pre_reg";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SHW_PRE_REG";
  let Semantics = "Golden LS D_SHW_PRE_REG.";
}
def d_shw_with_imm : HaydnBuiltin<"void(int64_t, int, int)"> {
  let PublicName = "d_shw_with_imm";
  let PublicPrototype = "void(int64_t, int, int)";
  let Mnemonic = "D_SHW_WITH_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SHW_WITH_IMM.";
}
def d_shw_with_reg : HaydnBuiltin<"void(int64_t, int, int)"> {
  let PublicName = "d_shw_with_reg";
  let PublicPrototype = "void(int64_t, int, int)";
  let Mnemonic = "D_SHW_WITH_REG";
  let Semantics = "Golden LS D_SHW_WITH_REG.";
}
def d_sw_h_post_imm : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sw_h_post_imm";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SW_H_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SW_H_POST_IMM.";
}
def d_sw_h_post_reg : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sw_h_post_reg";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SW_H_POST_REG";
  let Semantics = "Golden LS D_SW_H_POST_REG.";
}
def d_sw_h_pre_imm : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sw_h_pre_imm";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SW_H_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SW_H_PRE_IMM.";
}
def d_sw_h_pre_reg : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sw_h_pre_reg";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SW_H_PRE_REG";
  let Semantics = "Golden LS D_SW_H_PRE_REG.";
}
def d_sw_h_with_imm : HaydnBuiltin<"void(int64_t, int, int)"> {
  let PublicName = "d_sw_h_with_imm";
  let PublicPrototype = "void(int64_t, int, int)";
  let Mnemonic = "D_SW_H_WITH_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SW_H_WITH_IMM.";
}
def d_sw_h_with_reg : HaydnBuiltin<"void(int64_t, int, int)"> {
  let PublicName = "d_sw_h_with_reg";
  let PublicPrototype = "void(int64_t, int, int)";
  let Mnemonic = "D_SW_H_WITH_REG";
  let Semantics = "Golden LS D_SW_H_WITH_REG.";
}
def d_sw_l_post_imm : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sw_l_post_imm";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SW_L_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SW_L_POST_IMM.";
}
def d_sw_l_post_reg : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sw_l_post_reg";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SW_L_POST_REG";
  let Semantics = "Golden LS D_SW_L_POST_REG.";
}
def d_sw_l_pre_imm : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sw_l_pre_imm";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SW_L_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SW_L_PRE_IMM.";
}
def d_sw_l_pre_reg : HaydnBuiltin<"int(int64_t, int, int)"> {
  let PublicName = "d_sw_l_pre_reg";
  let PublicPrototype = "int(int64_t, int, int)";
  let Mnemonic = "D_SW_L_PRE_REG";
  let Semantics = "Golden LS D_SW_L_PRE_REG.";
}
def d_sw_l_with_imm : HaydnBuiltin<"void(int64_t, int, int)"> {
  let PublicName = "d_sw_l_with_imm";
  let PublicPrototype = "void(int64_t, int, int)";
  let Mnemonic = "D_SW_L_WITH_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS D_SW_L_WITH_IMM.";
}
def d_sw_l_with_reg : HaydnBuiltin<"void(int64_t, int, int)"> {
  let PublicName = "d_sw_l_with_reg";
  let PublicPrototype = "void(int64_t, int, int)";
  let Mnemonic = "D_SW_L_WITH_REG";
  let Semantics = "Golden LS D_SW_L_WITH_REG.";
}
def s_lbs_post_imm_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lbs_post_imm";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LBS_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LBS_POST_IMM: data + AGU writeback (sext mem8).";
}
def s_lbs_post_reg_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lbs_post_reg";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LBS_POST_REG";
  let Semantics = "Golden LS S_LBS_POST_REG: data + AGU writeback (sext mem8).";
}
def s_lbs_pre_imm_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lbs_pre_imm";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LBS_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LBS_PRE_IMM: data + AGU writeback (sext mem8).";
}
def s_lbs_pre_reg_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lbs_pre_reg";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LBS_PRE_REG";
  let Semantics = "Golden LS S_LBS_PRE_REG: data + AGU writeback (sext mem8).";
}
def s_lbs_with_imm : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "s_lbs_with_imm";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "S_LBS_WITH_IMM";
  let ImmChecks = [ImmCheck<1, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LBS_WITH_IMM.";
}
def s_lbs_with_reg : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "s_lbs_with_reg";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "S_LBS_WITH_REG";
  let Semantics = "Golden LS S_LBS_WITH_REG.";
}
def s_lbu_post_imm_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lbu_post_imm";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LBU_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LBU_POST_IMM: data + AGU writeback (zext mem8).";
}
def s_lbu_post_reg_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lbu_post_reg";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LBU_POST_REG";
  let Semantics = "Golden LS S_LBU_POST_REG: data + AGU writeback (zext mem8).";
}
def s_lbu_pre_imm_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lbu_pre_imm";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LBU_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LBU_PRE_IMM: data + AGU writeback (zext mem8).";
}
def s_lbu_pre_reg_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lbu_pre_reg";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LBU_PRE_REG";
  let Semantics = "Golden LS S_LBU_PRE_REG: data + AGU writeback (zext mem8).";
}
def s_lbu_with_imm : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "s_lbu_with_imm";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "S_LBU_WITH_IMM";
  let ImmChecks = [ImmCheck<1, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LBU_WITH_IMM.";
}
def s_lbu_with_reg : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "s_lbu_with_reg";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "S_LBU_WITH_REG";
  let Semantics = "Golden LS S_LBU_WITH_REG.";
}
def s_lhws_post_imm_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lhws_post_imm";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LHWS_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LHWS_POST_IMM: data + AGU writeback (sext mem16).";
}
def s_lhws_post_reg_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lhws_post_reg";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LHWS_POST_REG";
  let Semantics = "Golden LS S_LHWS_POST_REG: data + AGU writeback (sext mem16).";
}
def s_lhws_pre_imm_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lhws_pre_imm";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LHWS_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LHWS_PRE_IMM: data + AGU writeback (sext mem16).";
}
def s_lhws_pre_reg_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lhws_pre_reg";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LHWS_PRE_REG";
  let Semantics = "Golden LS S_LHWS_PRE_REG: data + AGU writeback (sext mem16).";
}
def s_lhws_with_imm : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "s_lhws_with_imm";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "S_LHWS_WITH_IMM";
  let ImmChecks = [ImmCheck<1, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LHWS_WITH_IMM.";
}
def s_lhws_with_reg : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "s_lhws_with_reg";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "S_LHWS_WITH_REG";
  let Semantics = "Golden LS S_LHWS_WITH_REG.";
}
def s_lhwu_post_imm_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lhwu_post_imm";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LHWU_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LHWU_POST_IMM: data + AGU writeback (zext mem16).";
}
def s_lhwu_post_reg_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lhwu_post_reg";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LHWU_POST_REG";
  let Semantics = "Golden LS S_LHWU_POST_REG: data + AGU writeback (zext mem16).";
}
def s_lhwu_pre_imm_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lhwu_pre_imm";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LHWU_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LHWU_PRE_IMM: data + AGU writeback (zext mem16).";
}
def s_lhwu_pre_reg_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lhwu_pre_reg";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LHWU_PRE_REG";
  let Semantics = "Golden LS S_LHWU_PRE_REG: data + AGU writeback (zext mem16).";
}
def s_lhwu_with_imm : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "s_lhwu_with_imm";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "S_LHWU_WITH_IMM";
  let ImmChecks = [ImmCheck<1, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LHWU_WITH_IMM.";
}
def s_lhwu_with_reg : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "s_lhwu_with_reg";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "S_LHWU_WITH_REG";
  let Semantics = "Golden LS S_LHWU_WITH_REG.";
}
def s_lw_post_imm_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lw_post_imm";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LW_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LW_POST_IMM: data + AGU writeback (mem32).";
}
def s_lw_post_reg_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lw_post_reg";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LW_POST_REG";
  let Semantics = "Golden LS S_LW_POST_REG: data + AGU writeback (mem32).";
}
def s_lw_pre_imm_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lw_pre_imm";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LW_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LW_PRE_IMM: data + AGU writeback (mem32).";
}
def s_lw_pre_reg_pair : HaydnPairBuiltin<"int(int*, int, int)"> {
  let PublicName = "s_lw_pre_reg";
  let PublicPrototype = "haydn_sld_t(int, int)";
  let Mnemonic = "S_LW_PRE_REG";
  let Semantics = "Golden LS S_LW_PRE_REG: data + AGU writeback (mem32).";
}
def s_lw_with_imm : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "s_lw_with_imm";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "S_LW_WITH_IMM";
  let ImmChecks = [ImmCheck<1, ImmCheckSimm6>];
  let Semantics = "Golden LS S_LW_WITH_IMM.";
}
def s_lw_with_reg : HaydnBuiltin<"int(int, int)"> {
  let PublicName = "s_lw_with_reg";
  let PublicPrototype = "int(int, int)";
  let Mnemonic = "S_LW_WITH_REG";
  let Semantics = "Golden LS S_LW_WITH_REG.";
}
def s_sb_post_imm : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_sb_post_imm";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SB_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_SB_POST_IMM.";
}
def s_sb_post_reg : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_sb_post_reg";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SB_POST_REG";
  let Semantics = "Golden LS S_SB_POST_REG.";
}
def s_sb_pre_imm : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_sb_pre_imm";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SB_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_SB_PRE_IMM.";
}
def s_sb_pre_reg : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_sb_pre_reg";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SB_PRE_REG";
  let Semantics = "Golden LS S_SB_PRE_REG.";
}
def s_sb_with_imm : HaydnBuiltin<"void(int, int, int)"> {
  let PublicName = "s_sb_with_imm";
  let PublicPrototype = "void(int, int, int)";
  let Mnemonic = "S_SB_WITH_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_SB_WITH_IMM.";
}
def s_sb_with_reg : HaydnBuiltin<"void(int, int, int)"> {
  let PublicName = "s_sb_with_reg";
  let PublicPrototype = "void(int, int, int)";
  let Mnemonic = "S_SB_WITH_REG";
  let Semantics = "Golden LS S_SB_WITH_REG.";
}
def s_shw_post_imm : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_shw_post_imm";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SHW_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_SHW_POST_IMM.";
}
def s_shw_post_reg : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_shw_post_reg";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SHW_POST_REG";
  let Semantics = "Golden LS S_SHW_POST_REG.";
}
def s_shw_pre_imm : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_shw_pre_imm";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SHW_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_SHW_PRE_IMM.";
}
def s_shw_pre_reg : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_shw_pre_reg";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SHW_PRE_REG";
  let Semantics = "Golden LS S_SHW_PRE_REG.";
}
def s_shw_with_imm : HaydnBuiltin<"void(int, int, int)"> {
  let PublicName = "s_shw_with_imm";
  let PublicPrototype = "void(int, int, int)";
  let Mnemonic = "S_SHW_WITH_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_SHW_WITH_IMM.";
}
def s_shw_with_reg : HaydnBuiltin<"void(int, int, int)"> {
  let PublicName = "s_shw_with_reg";
  let PublicPrototype = "void(int, int, int)";
  let Mnemonic = "S_SHW_WITH_REG";
  let Semantics = "Golden LS S_SHW_WITH_REG.";
}
def s_sw_post_imm : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_sw_post_imm";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SW_POST_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_SW_POST_IMM.";
}
def s_sw_post_reg : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_sw_post_reg";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SW_POST_REG";
  let Semantics = "Golden LS S_SW_POST_REG.";
}
def s_sw_pre_imm : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_sw_pre_imm";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SW_PRE_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_SW_PRE_IMM.";
}
def s_sw_pre_reg : HaydnBuiltin<"int(int, int, int)"> {
  let PublicName = "s_sw_pre_reg";
  let PublicPrototype = "int(int, int, int)";
  let Mnemonic = "S_SW_PRE_REG";
  let Semantics = "Golden LS S_SW_PRE_REG.";
}
def s_sw_with_imm : HaydnBuiltin<"void(int, int, int)"> {
  let PublicName = "s_sw_with_imm";
  let PublicPrototype = "void(int, int, int)";
  let Mnemonic = "S_SW_WITH_IMM";
  let ImmChecks = [ImmCheck<2, ImmCheckSimm6>];
  let Semantics = "Golden LS S_SW_WITH_IMM.";
}
def s_sw_with_reg : HaydnBuiltin<"void(int, int, int)"> {
  let PublicName = "s_sw_with_reg";
  let PublicPrototype = "void(int, int, int)";
  let Mnemonic = "S_SW_WITH_REG";
  let Semantics = "Golden LS S_SW_WITH_REG.";
}
