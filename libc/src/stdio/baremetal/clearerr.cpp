//===-- Implementation of clearerr for baremetal ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/clearerr.h"

#include "hdr/types/FILE.h"
#include "src/__support/OSUtil/baremetal/stdio_cookie.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void, clearerr, (::FILE * stream)) {
  if (stream == nullptr)
    return;
  auto *cookie = reinterpret_cast<__llvm_libc_stdio_cookie *>(stream);
  cookie->eof = 0;
  cookie->err = 0;
}

} // namespace LIBC_NAMESPACE_DECL
