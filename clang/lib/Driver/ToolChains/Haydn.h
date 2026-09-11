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
//   2. `addHaydnLinkArgs(...)` — `--nmagic` (AIE AIE.cpp:36; 12-byte
//      parcel vs 4 KiB page) and matching-sysroot `-lm` when
//      `$sysroot/lib/libm.a` exists (AIE AIE.cpp:42-43). No private
//      libhaydn.a / haydn.ld auto-injection (product ld is install-owned).
//   3. `addHaydnClangTargetArgs(...)` — default `-tune-cpu haydn`.
//      Empty `-mcpu` names generic; generic and haydn are the same ISA.
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

// Optional Haydn-specific linker args. `--nmagic` keeps the exec pack on
// EncodedBytes. Soft-float `-lm` comes from the matching sysroot `libm.a`
// when present. Product `haydn.ld` is not auto-injected.
void addHaydnLinkArgs(const ToolChain &TC, const llvm::Triple &Triple,
                      const llvm::opt::ArgList &Args,
                      llvm::opt::ArgStringList &CmdArgs);

// Default `-mtune=haydn` for haydn-unknown-elf. Does not change ISA
// features. Explicit `-mtune=` wins. Empty `-mcpu` names generic.
void addHaydnClangTargetArgs(const llvm::opt::ArgList &Args,
                             llvm::opt::ArgStringList &CmdArgs);

} // namespace toolchains
} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HAYDN_H
