//===--- Haydn.cpp - Haydn baremetal driver helpers --------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn-specific driver helpers. Extracted from the shared upstream
// `clang/lib/Driver/ToolChains/BareMetal.cpp` so that file only retains
// minimal `Triple::haydn` dispatch hooks that forward here.
//
// Runtime / libc: Haydn links like other baremetal targets — compiler-rt
// builtins and llvm-libc (`-lc` / `-lm`) when the sysroot provides them.
// There is no target-private libhaydn.a / haydn.ld auto-injection.
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::driver::toolchains;

const char *toolchains::getDefaultHaydnLinker() {
  // Haydn baremetal has no system ld; always use ld.lld.
  return "ld.lld";
}

void toolchains::addHaydnLinkArgs(const ToolChain &TC,
                                  const llvm::Triple &Triple,
                                  const llvm::opt::ArgList &Args,
                                  llvm::opt::ArgStringList &CmdArgs) {
  // Intentionally empty: no private runtime archive or default linker script.
  // Supply -T / -L / -lc yourself (or via a configured llvm-libc sysroot).
  (void)TC;
  (void)Triple;
  (void)Args;
  (void)CmdArgs;
}
