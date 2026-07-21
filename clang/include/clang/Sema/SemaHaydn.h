//===----- SemaHaydn.h -- Haydn target-specific routines --*- C++ -*-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares semantic analysis functions specific to Haydn.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAHAYDN_H
#define LLVM_CLANG_SEMA_SEMAHAYDN_H

#include "clang/AST/ASTFwd.h"
#include "clang/Sema/SemaBase.h"

namespace clang {
/// Semantic analysis for Haydn DSP target-specific builtins.
class SemaHaydn : public SemaBase {
public:
  SemaHaydn(Sema &S);

  /// Check a Haydn builtin function call for validity.
  /// \returns true on error.
  bool CheckHaydnBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall);
};
} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAHAYDN_H
