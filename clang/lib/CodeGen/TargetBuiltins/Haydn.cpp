//===------ Haydn.cpp - Emit LLVM Code for builtins -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache License v2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This emits Haydn target builtins as LLVM code. Three builtin families reach
// here via different paths (see D210 and D400 for the dispatch analysis):
//
//   __builtin_haydn_X       — declared as HaydnBuiltin but each maps 1:1 to a
//                             llvm.haydn.X intrinsic via the ClangBuiltin<>
//                             auto-map in CGBuiltin.cpp (the generic path runs
//                             first and never falls through to this file).
//
//   __builtin_ae_X          — declared as HaydnAeBuiltin, which is NOT
//                             ClangBuiltin<>-annotated.
//                             getIntrinsicForClangBuiltin returns
//                             not_intrinsic, so control falls through to
//                             EmitTargetBuiltinExpr -> EmitHaydnBuiltinExpr
//                             here. These are the Tier B composed-CodeGen
//                             builtins that lower to one or more
//                             llvm.haydn.* intrinsics.
//
//   __builtin_haydn_X_pair  — D400 Path B 2-dest SIMD MAC builtins. The LLVM
//                             intrinsics (int_haydn_X) carry ClangBuiltin<>
//                             and return `{i64, i64}`. The generic auto-map
//                             path cannot lower multi-result intrinsics (no
//                             extractvalue logic — see CGBuiltin.cpp:6434-
//                             6505), so these clang builtins are spelled with
//                             a `_pair` suffix and declared as HaydnAeBuiltin
//                             (no ClangBuiltin<> annotation on the clang
//                             side), bypassing the auto-map. We emit the
//                             2-result intrinsic call + CreateExtractValue
//                             for each field (the frexp pattern, mirroring
//                             emitFrexpBuiltin in CGBuiltin.cpp:684-700).
//
// Type model: the C-level ae_int16x4 is `long long` (i64), but the lanewise
// Haydn intrinsics operate on <4 x i16>. Args are bit-cast i64 -> v4i16 before
// the call and results v4i16 -> i64 after, mirroring the auto-map's cast logic
// in CGBuiltin.cpp. Intrinsics already typed i64 (e.g. x4fcmula16rs) take the
// i64 directly with no cast.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsHaydn.h"

using namespace clang;
using namespace CodeGen;
using namespace llvm;
using namespace llvm::Intrinsic;

namespace {
/// Bit-cast a scalar i64 value to <4 x i16> for lanewise Haydn intrinsics.
/// ae_int16x4 is `long long` at the C level but the lanewise intrinsics are
/// declared with llvm_v4i16_ty operands.
Value *toV4I16(CodeGenFunction &CGF, Value *V) {
  if (V->getType()->isVectorTy())
    return V;
  return CGF.Builder.CreateBitCast(V, FixedVectorType::get(CGF.Int16Ty, 4));
}

/// Bit-cast a <4 x i16> result back to scalar i64 for the C-level return.
Value *fromV4I16(CodeGenFunction &CGF, Value *V) {
  if (!V->getType()->isVectorTy())
    return V;
  return CGF.Builder.CreateBitCast(V, CGF.Int64Ty);
}

/// Emit a 1:1 unary lanewise intrinsic (v4i16 in, v4i16 out), casting the
/// i64 C arg to v4i16 and the result back to i64.
Value *emitLanewiseUnary(CodeGenFunction &CGF, unsigned ID, const CallExpr *E) {
  Value *Src = toV4I16(CGF, CGF.EmitScalarExpr(E->getArg(0)));
  Value *Call = CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(ID), {Src});
  return fromV4I16(CGF, Call);
}

/// Emit a 2-dest D_RR2 intrinsic (D400 Path B). The builtin has signature
/// `int64_t(int64_t *rtd2_out, int64_t src1, int64_t src2)` — returns rtd1
/// (high pair) and writes rtd2 (low pair) through the out-pointer. The
/// underlying LLVM intrinsic returns `{i64, i64}`; we call it, extract each
/// field via CreateExtractValue, store field 1 to *rtd2_out, and return
/// field 0. This mirrors the clang `__builtin_frexp` → `int_frexp` pattern
/// (CGBuiltin.cpp:684-700, emitFrexpBuiltin) — the only mechanism clang
/// provides for lowering a multi-result intrinsic, because the generic
/// ClangBuiltin<> auto-map path in CGBuiltin.cpp:6434-6505 has no
/// `extractvalue` logic and cannot lower multi-result intrinsics.
///
/// The out-pointer arg is emitted via EmitScalarExpr + MakeNaturalAlignAddrLValue
/// (exactly as emitFrexpBuiltin does at CGBuiltin.cpp:696), NOT EmitLValue.
/// EmitLValue on the arg would try to treat the address-of expression (`&r.lo`
/// in the wrapper) as an lvalue, hitting `EmitUnsupportedLValue("unexpected
/// cast lvalue")` or `UNREACHABLE` at CGExpr.cpp:3639. EmitScalarExpr evaluates
/// the address-of to a pointer VALUE; MakeNaturalAlignAddrLValue then wraps it
/// as the store target.
Value *emitPairRR2(CodeGenFunction &CGF, unsigned ID, const CallExpr *E) {
  Value *Rtd2Ptr = CGF.EmitScalarExpr(E->getArg(0));
  Value *A = CGF.EmitScalarExpr(E->getArg(1));
  Value *B = CGF.EmitScalarExpr(E->getArg(2));
  Value *Call = CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(ID), {A, B});
  Value *Rtd2 = CGF.Builder.CreateExtractValue(Call, 1);
  QualType PointeeTy = E->getArg(0)->getType()->getPointeeType();
  LValue Rtd2LV = CGF.MakeNaturalAlignAddrLValue(Rtd2Ptr, PointeeTy);
  CGF.EmitStoreOfScalar(Rtd2, Rtd2LV);
  return CGF.Builder.CreateExtractValue(Call, 0);
}

/// Emit a 2-dest D_RRA2 accumulator intrinsic (D400 Path B). The builtin has
/// signature `int64_t(int64_t *rtd2_out, int64_t acc1, int64_t acc2,
/// int64_t src1, int64_t src2)` — returns rtd1 (high accumulator new value)
/// and writes rtd2 (low accumulator new value) through the out-pointer. The
/// underlying LLVM intrinsic returns `{i64, i64}` and reads (acc1, acc2,
/// src1, src2). See emitPairRR2 for the EmitScalarExpr rationale.
Value *emitPairRRA2(CodeGenFunction &CGF, unsigned ID, const CallExpr *E) {
  Value *Rtd2Ptr = CGF.EmitScalarExpr(E->getArg(0));
  Value *Acc1 = CGF.EmitScalarExpr(E->getArg(1));
  Value *Acc2 = CGF.EmitScalarExpr(E->getArg(2));
  Value *A = CGF.EmitScalarExpr(E->getArg(3));
  Value *B = CGF.EmitScalarExpr(E->getArg(4));
  Value *Call =
      CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(ID), {Acc1, Acc2, A, B});
  Value *Rtd2 = CGF.Builder.CreateExtractValue(Call, 1);
  QualType PointeeTy = E->getArg(0)->getType()->getPointeeType();
  LValue Rtd2LV = CGF.MakeNaturalAlignAddrLValue(Rtd2Ptr, PointeeTy);
  CGF.EmitStoreOfScalar(Rtd2, Rtd2LV);
  return CGF.Builder.CreateExtractValue(Call, 0);
}

/// CB load frexp pattern (D208):
///   int64_t data = __builtin_haydn_ldw_cb_*_pair(int *new_ptr_out,
///                                                int ptr, int cbr, int stride);
/// Underlying intrinsic returns {i64 data, i32 new_ptr}. Store field 1 to
/// *new_ptr_out and return field 0.
Value *emitCbLoadPair(CodeGenFunction &CGF, unsigned ID, const CallExpr *E) {
  Value *NewPtrOut = CGF.EmitScalarExpr(E->getArg(0));
  Value *Ptr = CGF.EmitScalarExpr(E->getArg(1));
  Value *Cbr = CGF.EmitScalarExpr(E->getArg(2));
  Value *Stride = CGF.EmitScalarExpr(E->getArg(3));
  Value *Call =
      CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(ID), {Ptr, Cbr, Stride});
  Value *NewPtr = CGF.Builder.CreateExtractValue(Call, 1);
  QualType PointeeTy = E->getArg(0)->getType()->getPointeeType();
  LValue NewPtrLV = CGF.MakeNaturalAlignAddrLValue(NewPtrOut, PointeeTy);
  CGF.EmitStoreOfScalar(NewPtr, NewPtrLV);
  return CGF.Builder.CreateExtractValue(Call, 0);
}
} // namespace

Value *CodeGenFunction::EmitHaydnBuiltinExpr(unsigned BuiltinID,
                                              const CallExpr *E) {
  // The 1:1 __builtin_haydn_* builtins never reach here — they are
  // ClangBuiltin<>-annotated and auto-map in CGBuiltin.cpp before this
  // function is called. This switch handles the __builtin_ae_* Tier B
  // composed builtins (HaydnAeBuiltin class, no ClangBuiltin<> annotation).
  switch (BuiltinID) {
  default:
    break;

  //--- Native 1:1 callouts (single correct intrinsic) -------------------//
  // These replace broken haydn_dsp.h decompositions that routed to the wrong
  // or non-existent intrinsic (e.g. AE_MUL16JS used to chain seli16 + the
  // missing x4addsub16s; AE_MULAFC16RAS used the non-accumulate x4fcmul16rs
  // + a C add instead of the native accumulate x4fcmula16rs).
  case Haydn::BI__builtin_ae_mul16js:
    // AE_MUL16JS: multiply-by-j (swap real/imag, negate new imag) = X4MJSWAP16S.
    return emitLanewiseUnary(*this, haydn_x4mjswap16s, E);
  case Haydn::BI__builtin_ae_conj16s:
    // AE_CONJ16S: complex conjugate {re, -im} per lane = X4CONJ16S.
    return emitLanewiseUnary(*this, haydn_x4conj16s, E);
  case Haydn::BI__builtin_ae_mulafc16ras: {
    // AE_MULAFC16RAS: 16-bit complex MAC, accumulate form = X4FCMULA16RS
    // (ternary: acc, a, b — reads the accumulator, unlike x4fcmul16rs).
    // The intrinsic is already typed i64, so no v4i16 cast is needed.
    Value *Acc = EmitScalarExpr(E->getArg(0));
    Value *A = EmitScalarExpr(E->getArg(1));
    Value *B = EmitScalarExpr(E->getArg(2));
    return Builder.CreateCall(CGM.getIntrinsic(haydn_x4fcmula16rs),
                              {Acc, A, B});
  }

  //--- 1:N composed: AE_ADDANDSUBRNG16RAS_S0/S1/S2 ----------------------//
  // HiFi semantics: result lanes = {a3+b3, a2-b2, a1+b1, a0-b0} (alternating
  // add/sub interleave). X4ADDSUB16S is an ISA gap (ISA-36), and X4SELI16's
  // 16-entry permutation table cannot express the alternating interleave from
  // (add, sub) sources (ISA-39). The only correct lowering with current
  // silicon is the SFR-based masked-move composition (D210):
  //   add = x4add16s(a, b);  sub = x4sub16s(a, b);
  //   movegpr2sfr(0b0101);   // SFR[3:0] = 0101 -> lanes 2,0 selected
  //   x4movt16(add, sub);    // overwrite lanes 2,0 of add with sub
  //                          //   -> {add3, sub2, add1, sub0}
  // This clobbers SFR; the slot suffix (_s0/_s1/_s2) is a HiFi macro
  // convention only and emits identical IR (no Haydn slot tagging).
  case Haydn::BI__builtin_ae_addandsubrng16ras_s0:
  case Haydn::BI__builtin_ae_addandsubrng16ras_s1:
  case Haydn::BI__builtin_ae_addandsubrng16ras_s2: {
    Value *A = toV4I16(*this, EmitScalarExpr(E->getArg(0)));
    Value *B = toV4I16(*this, EmitScalarExpr(E->getArg(1)));
    Value *Add =
        Builder.CreateCall(CGM.getIntrinsic(haydn_x4add16s), {A, B});
    Value *Sub =
        Builder.CreateCall(CGM.getIntrinsic(haydn_x4sub16s), {A, B});
    // Program SFR[3:0] = 0b0101 so X4MOVT16 overwrites lanes 2 and 0 (where
    // the SFR bit is 1) with the sub result, yielding the alternating
    // {add,sub,add,sub} interleave.
    (void)Builder.CreateCall(CGM.getIntrinsic(haydn_movegpr2sfr),
                             {Builder.getInt32(0b0101)});
    Value *Res =
        Builder.CreateCall(CGM.getIntrinsic(haydn_x4movt16), {Add, Sub});
    return fromV4I16(*this, Res);
  }

  //--- D400 Path B: 2-dest SIMD MAC (frexp-pattern `_pair` builtins) -----//
  // These 15 ops produce TWO i64 results (golden slot1_mac_instruction_list.json:
  // DR_Write_Port:[rtd1,rtd2]). The LLVM intrinsics return `{i64, i64}`.
  // The clang builtins use the frexp pattern (returns rtd1, writes rtd2
  // through the first-arg out-pointer) because the Prototype grammar has no
  // struct return and the generic ClangBuiltin<> auto-map cannot lower
  // multi-result intrinsics. See emitPairRR2/emitPairRRA2 helpers above.
  // D_RR2 non-accum: {X2MUL32, X4MUL16, X2CMUL32, X2CMUL32S, X2CMUL32_F2,
  // X2CMUL32S_F2, X4FF2MUL16S}.
  case Haydn::BI__builtin_haydn_x2mul32_pair:
    return emitPairRR2(*this, haydn_x2mul32, E);
  case Haydn::BI__builtin_haydn_x4mul16_pair:
    return emitPairRR2(*this, haydn_x4mul16, E);
  case Haydn::BI__builtin_haydn_x2cmul32_pair:
    return emitPairRR2(*this, haydn_x2cmul32, E);
  case Haydn::BI__builtin_haydn_x2cmul32s_pair:
    return emitPairRR2(*this, haydn_x2cmul32s, E);
  case Haydn::BI__builtin_haydn_x2cmul32_f2_pair:
    return emitPairRR2(*this, haydn_x2cmul32_f2, E);
  case Haydn::BI__builtin_haydn_x2cmul32s_f2_pair:
    return emitPairRR2(*this, haydn_x2cmul32s_f2, E);
  case Haydn::BI__builtin_haydn_x4ff2mul16s_pair:
    return emitPairRR2(*this, haydn_x4ff2mul16s, E);
  // D_RRA2 accum: {X2MULA32, X2MULS32, X4MULA16, X4MULS16, X4MULA16S,
  // X4MULS16S, X4FF2MULA16S, X4FF2MULS16S}.
  case Haydn::BI__builtin_haydn_x2mula32_pair:
    return emitPairRRA2(*this, haydn_x2mula32, E);
  case Haydn::BI__builtin_haydn_x2muls32_pair:
    return emitPairRRA2(*this, haydn_x2muls32, E);
  case Haydn::BI__builtin_haydn_x4mula16_pair:
    return emitPairRRA2(*this, haydn_x4mula16, E);
  case Haydn::BI__builtin_haydn_x4muls16_pair:
    return emitPairRRA2(*this, haydn_x4muls16, E);
  case Haydn::BI__builtin_haydn_x4mula16s_pair:
    return emitPairRRA2(*this, haydn_x4mula16s, E);
  case Haydn::BI__builtin_haydn_x4muls16s_pair:
    return emitPairRRA2(*this, haydn_x4muls16s, E);
  case Haydn::BI__builtin_haydn_x4ff2mula16s_pair:
    return emitPairRRA2(*this, haydn_x4ff2mula16s, E);
  case Haydn::BI__builtin_haydn_x4ff2muls16s_pair:
    return emitPairRRA2(*this, haydn_x4ff2muls16s, E);

  //--- D208: CB load 2-ret (data + AGU-updated ptr) frexp _pair builtins ---//
  case Haydn::BI__builtin_haydn_ldw_cb_imm_pair:
    return emitCbLoadPair(*this, haydn_ldw_cb_imm, E);
  case Haydn::BI__builtin_haydn_ldw_cb_reg_pair:
    return emitCbLoadPair(*this, haydn_ldw_cb_reg, E);
  }

  return nullptr;
}
