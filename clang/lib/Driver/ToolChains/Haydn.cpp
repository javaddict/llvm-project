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
// minimal `Triple::haydn` dispatch hooks that forward here (HC#0).
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"

#include "clang/Driver/Driver.h"
#include "clang/Options/Options.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/VirtualFileSystem.h"

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
  // Automatically provide the baremetal linker script if the user hasn't
  // supplied one via -T. NOTE: GetFilePath returns the unresolved basename
  // (not "") when nothing is found, so !empty() cannot be used as an existence
  // test (D179) — check llvm::sys::fs::exists on the resolved path.
  if (!Args.hasArg(options::OPT_T)) {
    std::string LDSrc = TC.GetFilePath("haydn.ld");
    if (!LDSrc.empty() && llvm::sys::fs::exists(LDSrc))
      CmdArgs.push_back(Args.MakeArgString("-T" + LDSrc));
  }

  // Haydn ships its own runtime as libhaydn.a (mem*, atomics, integer div/mod,
  // and the compiler-rt soft-float builtins — see D251). It is a static
  // archive (not a relocatable object) so the linker lazily pulls only the
  // referenced symbols. Looked up by name so a -L override still works.
  // GetFilePath returns the unresolved basename when not found, so verify
  // existence on the resolved path (D179); silently omit if absent so an
  // out-of-tree runtime supplied via -L still links.
  std::string LibHaydn = TC.GetFilePath("libhaydn.a");
  if (!LibHaydn.empty() && llvm::sys::fs::exists(LibHaydn))
    CmdArgs.push_back(Args.MakeArgString(LibHaydn));
}
