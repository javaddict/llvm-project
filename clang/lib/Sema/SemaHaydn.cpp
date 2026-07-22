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
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaHaydn.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringExtras.h"
#include <optional>

namespace clang {

// File-scope table + HAYDN_BUILTIN_SEMA_CASES (needs clang::Haydn::*).
#include "clang/Basic/haydn_builtin_sema.inc"

SemaHaydn::SemaHaydn(Sema &S) : SemaBase(S) {}

bool SemaHaydn::CheckHaydnBuiltinFunctionCall(unsigned BuiltinID,
                                               CallExpr *TheCall) {
  switch (BuiltinID) {
  default:
    break;
    HAYDN_BUILTIN_SEMA_CASES
  }
  return false;
}

} // namespace clang
