//===-- Implementation of ferror for baremetal ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/ferror.h"

#include "hdr/types/FILE.h"
#include "src/__support/OSUtil/baremetal/stdio_cookie.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, ferror, (::FILE * stream)) {
  if (stream == nullptr)
    return 0;
  auto *cookie = reinterpret_cast<__llvm_libc_stdio_cookie *>(stream);
  return cookie->err ? 1 : 0;
}

} // namespace LIBC_NAMESPACE_DECL
