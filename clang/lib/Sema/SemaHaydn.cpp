//===------ SemaHaydn.cpp ------ Haydn target-specific routines -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Semantic analysis for Haydn target builtins.
//
// BuiltinsHaydn.td → clang-tblgen -gen-haydn-builtin-sema →
// haydn_builtin_sema.inc (metadata table + HAYDN_BUILTIN_SEMA_CASES).
//
// Feature-gated builtins (Features= in BuiltinsHaydn.td) fail closed here so
// -fsyntax-only diagnoses missing simd|circular-buffer|bit-reversed|agu|hwloop
// (CodeGen checkTargetFeatures is the Hexagon peer path for -S/-emit-llvm;
// both use the same Builtin Features strings).
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaHaydn.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include <optional>

namespace clang {

// File-scope table + HAYDN_BUILTIN_SEMA_CASES (needs clang::Haydn::*).
#include "clang/Basic/haydn_builtin_sema.inc"

SemaHaydn::SemaHaydn(Sema &S) : SemaBase(S) {}

bool SemaHaydn::CheckHaydnBuiltinFunctionCall(unsigned BuiltinID,
                                               CallExpr *TheCall) {
  // Require BuiltinsHaydn Features against the caller's feature map.
  // Empty Features → always available (scalar ALU baseline).
  if (const FunctionDecl *FD = TheCall->getDirectCallee()) {
    StringRef FeatureList(
        SemaRef.Context.BuiltinInfo.getRequiredFeatures(BuiltinID));
    if (!FeatureList.empty()) {
      llvm::StringMap<bool> CallerFeatureMap;
      const FunctionDecl *CallerFD =
          dyn_cast_or_null<FunctionDecl>(SemaRef.CurContext);
      SemaRef.Context.getFunctionFeatureMap(CallerFeatureMap, CallerFD);
      if (!Builtin::evaluateRequiredTargetFeatures(FeatureList,
                                                   CallerFeatureMap)) {
        SemaRef.Diag(TheCall->getBeginLoc(), diag::err_builtin_needs_feature)
            << FD->getDeclName() << FeatureList;
        return true;
      }
    }
  }

  switch (BuiltinID) {
  default:
    break;
    HAYDN_BUILTIN_SEMA_CASES
  }
  return false;
}

} // namespace clang
