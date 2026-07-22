//===--- Haydn.h - Haydn baremetal driver helpers ---------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn-specific driver helpers. The shared `BareMetal.cpp` retains only a
// minimal `Triple::haydn` dispatch that forwards here.
//
//   1. `getDefaultHaydnLinker()` — always ld.lld (no system ld).
//   2. `addHaydnLinkArgs(...)` — reserved hook; currently a no-op. Haydn
//      uses the normal baremetal link set (compiler-rt / llvm-libc).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HAYDN_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HAYDN_H

#include "clang/Driver/ToolChain.h"
#include "llvm/Option/ArgList.h"

namespace clang {
namespace driver {
namespace toolchains {

// Returns the default linker executable for Haydn baremetal targets.
const char *getDefaultHaydnLinker();

// Optional Haydn-specific linker args. Currently empty — runtime comes from
// the configured sysroot (llvm-libc), not a private libhaydn archive.
void addHaydnLinkArgs(const ToolChain &TC, const llvm::Triple &Triple,
                      const llvm::opt::ArgList &Args,
                      llvm::opt::ArgStringList &CmdArgs);

} // namespace toolchains
} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HAYDN_H
