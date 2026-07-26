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
//   ComposeCmulAcc / SoftISqrt — composed / pure-ALU expands (no native MI)
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

/// Store AGU writeback pointer into frexp out-arg (void **).
/// If the out pointee is an integer (size_t*/uintptr_t*), ptrtoint first.
static void storeWbPtr(CodeGenFunction &CGF, Value *NewPtrOut,
                       const CallExpr *E, Value *NewPtr) {
  QualType PointeeTy = E->getArg(0)->getType()->getPointeeType();
  if (PointeeTy->isIntegerType()) {
    llvm::Type *IntTy = CGF.ConvertType(PointeeTy);
    NewPtr = CGF.Builder.CreatePtrToInt(NewPtr, IntTy, "wb.ptrtoint");
  } else {
    llvm::Type *DstTy = CGF.ConvertType(PointeeTy);
    if (NewPtr->getType() != DstTy)
      NewPtr = CGF.Builder.CreateBitCast(NewPtr, DstTy);
  }
  LValue NewPtrLV = CGF.MakeNaturalAlignAddrLValue(NewPtrOut, PointeeTy);
  CGF.EmitStoreOfScalar(NewPtr, NewPtrLV);
}

/// CB load frexp: {i64 data, ptr new_ptr}.
Value *emitCbLoadPair(CodeGenFunction &CGF, unsigned ID, const CallExpr *E) {
  Value *NewPtrOut = CGF.EmitScalarExpr(E->getArg(0));
  Value *Ptr = CGF.EmitScalarExpr(E->getArg(1));
  Value *Cbr = CGF.EmitScalarExpr(E->getArg(2));
  Value *Stride = CGF.EmitScalarExpr(E->getArg(3));
  Value *Call =
      CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(ID), {Ptr, Cbr, Stride});
  Value *NewPtr = CGF.Builder.CreateExtractValue(Call, 1);
  storeWbPtr(CGF, NewPtrOut, E, NewPtr);
  return CGF.Builder.CreateExtractValue(Call, 0);
}

/// POST/PRE AGU writeback load frexp: {data, ptr new_ptr} = op(base, off).
/// Builtin: data_ty(void **new_ptr_out, const void *base, int off).
Value *emitLoadWbPair(CodeGenFunction &CGF, unsigned ID, const CallExpr *E) {
  Value *NewPtrOut = CGF.EmitScalarExpr(E->getArg(0));
  Value *Base = CGF.EmitScalarExpr(E->getArg(1));
  Value *Off = CGF.EmitScalarExpr(E->getArg(2));
  Value *Call =
      CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(ID), {Base, Off});
  Value *NewPtr = CGF.Builder.CreateExtractValue(Call, 1);
  storeWbPtr(CGF, NewPtrOut, E, NewPtr);
  return CGF.Builder.CreateExtractValue(Call, 0);
}

/// BREV load frexp: {data, ptr new_ptr} — no cbr_sel (ptr, stride only).
/// Covers D_LDW_BREV (i64 data) and S_LW_BREV (i32 data).
Value *emitBrevLoadPair(CodeGenFunction &CGF, unsigned ID, const CallExpr *E) {
  Value *NewPtrOut = CGF.EmitScalarExpr(E->getArg(0));
  Value *Ptr = CGF.EmitScalarExpr(E->getArg(1));
  Value *Stride = CGF.EmitScalarExpr(E->getArg(2));
  Value *Call =
      CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(ID), {Ptr, Stride});
  Value *NewPtr = CGF.Builder.CreateExtractValue(Call, 1);
  storeWbPtr(CGF, NewPtrOut, E, NewPtr);
  return CGF.Builder.CreateExtractValue(Call, 0);
}

/// AE_ADDANDSUBRNG16RAS: alternating add/sub interleave via SFR masked move.
/// X4ADDSUB16S is an ISA gap; slot suffix is HiFi-only (identical IR).
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

/// Complex MAC frexp compose over existing x2cmul32{s} + add64/sub64(+sat).
/// Builtin frexp shape matches x2mula32: (int64_t *lo_out, acc1, acc2, a, b).
///   {prod_hi, prod_lo} = mul(a, b)
///   hi = acc_op(acc1, prod_hi); lo = acc_op(acc2, prod_lo); *lo_out = lo; ret hi
/// No native X2CMULA*/X2CMULS* MI — do not invent phantom int_haydn_x2cmula*.
Value *emitComposeCmulAcc(CodeGenFunction &CGF, unsigned MulID, unsigned AccID,
                          const CallExpr *E) {
  Value *Rtd2Ptr = CGF.EmitScalarExpr(E->getArg(0));
  Value *Acc1 = CGF.EmitScalarExpr(E->getArg(1));
  Value *Acc2 = CGF.EmitScalarExpr(E->getArg(2));
  Value *A = CGF.EmitScalarExpr(E->getArg(3));
  Value *B = CGF.EmitScalarExpr(E->getArg(4));
  Value *Mul = CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(MulID), {A, B});
  Value *ProdHi = CGF.Builder.CreateExtractValue(Mul, 0);
  Value *ProdLo = CGF.Builder.CreateExtractValue(Mul, 1);
  Value *Hi =
      CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(AccID), {Acc1, ProdHi});
  Value *Lo =
      CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(AccID), {Acc2, ProdLo});
  QualType PointeeTy = E->getArg(0)->getType()->getPointeeType();
  LValue Rtd2LV = CGF.MakeNaturalAlignAddrLValue(Rtd2Ptr, PointeeTy);
  CGF.EmitStoreOfScalar(Lo, Rtd2LV);
  return Hi;
}

/// SoftISqrt: floor(sqrt(n)) pure-ALU expand (no native ISQRT).
/// Negatives clamp to 0. Digit-by-digit binary isqrt — only and/or/add/sub/
/// lshr/icmp/select so O0/O2 ISel closes without libcalls.
Value *emitSoftISqrt(CodeGenFunction &CGF, const CallExpr *E) {
  Value *Xin = CGF.EmitScalarExpr(E->getArg(0));
  llvm::Type *I32 = CGF.Int32Ty;
  Value *Zero = llvm::ConstantInt::get(I32, 0);
  // Clamp negative signed inputs to 0 (isqrt domain is non-negative).
  Value *IsNeg = CGF.Builder.CreateICmpSLT(Xin, Zero, "isqrt.neg");
  Value *X = CGF.Builder.CreateSelect(IsNeg, Zero, Xin, "isqrt.x");

  // bit = 1u << 30; while (bit > x) bit >>= 2; — fully unrolled shifts.
  Value *Bit = llvm::ConstantInt::get(I32, 1u << 30);
  for (int I = 0; I < 16; ++I) {
    Value *Gt = CGF.Builder.CreateICmpUGT(Bit, X);
    Value *Shr = CGF.Builder.CreateLShr(Bit, llvm::ConstantInt::get(I32, 2));
    Bit = CGF.Builder.CreateSelect(Gt, Shr, Bit);
  }

  // while (bit) { if (x >= r+bit) { x -= r+bit; r = (r>>1)+bit; } else r>>=1;
  //               bit >>= 2; } — 16 iterations cover a 32-bit root. Guard with
  // bit!=0 so trailing zero-bit steps (after early shrink) do not mutate r/x.
  Value *R = Zero;
  for (int I = 0; I < 16; ++I) {
    Value *BitNZ = CGF.Builder.CreateICmpNE(Bit, Zero, "isqrt.bitnz");
    Value *Sum = CGF.Builder.CreateAdd(R, Bit, "isqrt.sum");
    Value *Ge = CGF.Builder.CreateICmpUGE(X, Sum, "isqrt.ge");
    Value *Take = CGF.Builder.CreateAnd(Ge, BitNZ, "isqrt.take");
    Value *XSub = CGF.Builder.CreateSub(X, Sum, "isqrt.xsub");
    X = CGF.Builder.CreateSelect(Take, XSub, X);
    Value *RShr = CGF.Builder.CreateLShr(R, llvm::ConstantInt::get(I32, 1));
    Value *RNew = CGF.Builder.CreateAdd(RShr, Bit, "isqrt.rnew");
    Value *RCand = CGF.Builder.CreateSelect(Ge, RNew, RShr, "isqrt.rcand");
    R = CGF.Builder.CreateSelect(BitNZ, RCand, R, "isqrt.r");
    Bit = CGF.Builder.CreateLShr(Bit, llvm::ConstantInt::get(I32, 2), "isqrt.bit");
  }
  return R;
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
