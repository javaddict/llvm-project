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
// builtins and llvm-libc (`-lc` from BareMetal, `-lm` here when the matching
// sysroot provides libm.a). No target-private libhaydn.a / haydn.ld injection.
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"
#include "clang/Driver/Driver.h"
#include "clang/Options/Options.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace llvm::opt;

const char *toolchains::getDefaultHaydnLinker() {
  // Haydn baremetal has no system ld; always use ld.lld.
  return "ld.lld";
}

void toolchains::addHaydnLinkArgs(const ToolChain &TC,
                                  const llvm::Triple &Triple,
                                  const llvm::opt::ArgList &Args,
                                  llvm::opt::ArgStringList &CmdArgs) {
  (void)Triple;
  // AIE AIE.cpp:36: `--nmagic` disables 4 KiB page pack. Format E EncodedBytes
  // is 12; 4096 % 12 == 4, so a paged PT_LOAD is not product-packable.
  CmdArgs.push_back("--nmagic");
  // AIE AIE.cpp:42-43 links `-lc` then `-lm`. BareMetal already emits `-lc`;
  // add matching-sysroot `-lm` here so soft-float helpers resolve on the same
  // ARTIFACT. Do not auto-inject haydn.ld (haydn-runtime-existence.c; product
  // script is install-owned at $sysroot/lib/haydn.ld or bundlesim.ld).
  if (Args.hasArg(options::OPT_nostdlib) ||
      Args.hasArg(options::OPT_nodefaultlibs) ||
      Args.hasArg(options::OPT_nolibc))
    return;
  const std::string &SysRoot = TC.getDriver().SysRoot;
  if (SysRoot.empty())
    return;
  SmallString<128> Libm(SysRoot);
  llvm::sys::path::append(Libm, "lib", "libm.a");
  if (llvm::sys::fs::exists(Libm))
    CmdArgs.push_back("-lm");
}
