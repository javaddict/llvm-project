//===-- Implementation of abort -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/common.h"
#include "src/__support/macros/config.h"

#include "src/stdlib/abort.h"

namespace LIBC_NAMESPACE_DECL {

// Vendor MUST provide this (BundleSim: plat/abort_llvm_libc.c).
// Required strong symbol — mirror __llvm_libc_exit. Weak undeclared + null
// check emitted JAL to 0 on Haydn; BundleSim code_image rejects.
extern "C" [[noreturn]] void __llvm_libc_abort(void);

[[noreturn]] LLVM_LIBC_FUNCTION(void, abort, ()) {
  __llvm_libc_abort();
}

} // namespace LIBC_NAMESPACE_DECL
