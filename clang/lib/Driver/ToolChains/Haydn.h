//===--- Haydn.h - Haydn baremetal driver helpers ---------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn-specific driver helpers. Lives in a Haydn-owned file so the shared
// upstream `BareMetal.cpp` only retains a minimal Haydn dispatch hook
// (a `Triple::haydn` test that forwards here), satisfying HC#0.
//
// Two responsibilities are Haydn-owned here:
//   1. `getDefaultHaydnLinker()` — Haydn baremetal has no system ld; always
//      use ld.lld.
//   2. `addHaydnLinkArgs(...)` — append Haydn-specific linker flags:
//      automatically provide `haydn.ld` linker script (if not -T supplied)
//      and `libhaydn.o` runtime (memset/memcpy/atomics/64-bit arithmetic).
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
// Haydn has no system ld, so always ld.lld.
const char *getDefaultHaydnLinker();

// Append Haydn-specific linker arguments to CmdArgs:
//   - `-T<haydn.ld>` if the user has not supplied a linker script via -T
//   - `libhaydn.o` runtime path (mem*, atomics, integer div/mod), if resolved
//     path exists. Lookup is by basename so a `-L` override still works.
//
// `Triple` is the effective target triple. `TC` is the active toolchain
// (used to resolve file paths via GetFilePath). `Args` and `CmdArgs` are the
// driver argument list and the outgoing linker argument list, respectively.
void addHaydnLinkArgs(const ToolChain &TC, const llvm::Triple &Triple,
                      const llvm::opt::ArgList &Args,
                      llvm::opt::ArgStringList &CmdArgs);

} // namespace toolchains
} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HAYDN_H
