//===-- Implementation of fclose for baremetal ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Closes a FILE* cookie produced by baremetal fopen. Does not close or free
// the static stdin/stdout/stderr cookies provided by the vendor plat.
//
//===----------------------------------------------------------------------===//

#include "src/stdio/fclose.h"

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/stdio_macros.h" // EOF
#include "hdr/types/FILE.h"
#include "src/__support/OSUtil/baremetal/io.h"
#include "src/__support/OSUtil/baremetal/stdio_cookie.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

extern "C" int close(int fd);

namespace LIBC_NAMESPACE_DECL {
namespace {

bool is_standard_stream(::FILE *stream) {
  auto *p = reinterpret_cast<void *>(stream);
  return p == static_cast<void *>(&__llvm_libc_stdin_cookie) ||
         p == static_cast<void *>(&__llvm_libc_stdout_cookie) ||
         p == static_cast<void *>(&__llvm_libc_stderr_cookie);
}

} // namespace

LLVM_LIBC_FUNCTION(int, fclose, (::FILE * stream)) {
  if (stream == nullptr) {
    libc_errno = EBADF;
    return EOF;
  }

  // Never free/close the static stdio cookies.
  if (is_standard_stream(stream)) {
    libc_errno = EBADF;
    return EOF;
  }

  auto *cookie = reinterpret_cast<__llvm_libc_stdio_cookie *>(stream);
  int fd = cookie->fd;
  free(cookie);

  if (close(fd) != 0)
    return EOF;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
