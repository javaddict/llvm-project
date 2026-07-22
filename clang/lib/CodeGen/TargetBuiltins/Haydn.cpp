//===------ Haydn.cpp - Emit LLVM Code for builtins -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache License v2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// EmitHaydnBuiltinExpr handles builtins that cannot use the generic
// ClangBuiltin<> auto-map in CGBuiltin.cpp:
//
//   __builtin_haydn_*_pair  — frexp-pattern 2-dest (returns rtd1, stores rtd2)
//   __builtin_ae_*          — AE Tier B (no ClangBuiltin annotation)
//
// Switch cases are TableGen'd into haydn_builtin_cg.inc from BuiltinsHaydn.td
// (pair classification + CodeGen= recipes). This file only keeps the small
// shared helpers those cases call.
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

/// Emit a 1:1 unary lanewise intrinsic (v4i16 in, v4i16 out).
Value *emitLanewiseUnary(CodeGenFunction &CGF, unsigned ID, const CallExpr *E) {
  Value *Src = toV4I16(CGF, CGF.EmitScalarExpr(E->getArg(0)));
  Value *Call = CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(ID), {Src});
  return fromV4I16(CGF, Call);
}

/// Emit a ternary AE recipe that maps to a lanewise golden-lane intrinsic
/// (v4i16). AE surface stays int64_t bags; bitcast to/from <4 x i16>.
/// (Historically named TernaryI64 when the LLVM side was also i64.)
Value *emitTernaryI64(CodeGenFunction &CGF, unsigned ID, const CallExpr *E) {
  Value *Acc = toV4I16(CGF, CGF.EmitScalarExpr(E->getArg(0)));
  Value *A = toV4I16(CGF, CGF.EmitScalarExpr(E->getArg(1)));
  Value *B = toV4I16(CGF, CGF.EmitScalarExpr(E->getArg(2)));
  Value *Call = CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(ID), {Acc, A, B});
  return fromV4I16(CGF, Call);
}

/// Emit a 2-dest D_RR2 frexp intrinsic.
/// Signature: int64_t(int64_t *rtd2_out, int64_t src1, int64_t src2)
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

/// Emit a 2-dest D_RRA2 frexp accumulator intrinsic.
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

/// CB load frexp: {i64 data, i32 new_ptr}.
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

/// AE_ADDANDSUBRNG16RAS: alternating add/sub interleave via SFR masked move
/// (D210). X4ADDSUB16S is an ISA gap; slot suffix is HiFi-only (identical IR).
Value *emitAeAddAndSubRng(CodeGenFunction &CGF, const CallExpr *E) {
  Value *A = toV4I16(CGF, CGF.EmitScalarExpr(E->getArg(0)));
  Value *B = toV4I16(CGF, CGF.EmitScalarExpr(E->getArg(1)));
  Value *Add =
      CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(haydn_x4add16s), {A, B});
  Value *Sub =
      CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(haydn_x4sub16s), {A, B});
  // SFR[3:0] = 0b0101 → lanes 2,0 selected for overwrite with sub.
  (void)CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(haydn_movegpr2sfr),
                               {CGF.Builder.getInt32(0b0101)});
  Value *Res =
      CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(haydn_x4movt16), {Add, Sub});
  return fromV4I16(CGF, Res);
}
} // namespace

Value *CodeGenFunction::EmitHaydnBuiltinExpr(unsigned BuiltinID,
                                              const CallExpr *E) {
  switch (BuiltinID) {
  default:
    break;

  // All frexp-pair / AE CodeGen= recipes — generated from BuiltinsHaydn.td.
#include "clang/Basic/haydn_builtin_cg.inc"
  }

  return nullptr;
}
