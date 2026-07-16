//===------ SemaHaydn.cpp ------ Haydn target-specific routines -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to Haydn.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaHaydn.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/Sema.h"

namespace clang {

SemaHaydn::SemaHaydn(Sema &S) : SemaBase(S) {}

bool SemaHaydn::CheckHaydnBuiltinFunctionCall(unsigned BuiltinID,
                                               CallExpr *TheCall) {
  // Phase 1: Basic type checking is handled by the .td prototype declarations.
  // Range checks for immediate operands and more detailed validation will be
  // added in a later phase.
  return false;
}

} // namespace clang
