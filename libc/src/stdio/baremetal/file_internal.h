//===--- Helper functions for file I/O on baremetal -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDIO_BAREMETAL_FILE_INTERNAL_H
#define LLVM_LIBC_SRC_STDIO_BAREMETAL_FILE_INTERNAL_H

#include "hdr/types/FILE.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/OSUtil/baremetal/stdio_cookie.h"
#include "src/__support/OSUtil/io.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {

// TODO: Deduplicate this with __support/File/file.h.
struct FileIOResult {
  size_t value;
  int error;

  constexpr FileIOResult(size_t val) : value(val), error(0) {}
  constexpr FileIOResult(size_t val, int error) : value(val), error(error) {}

  constexpr bool has_error() { return error != 0; }

  constexpr operator size_t() { return value; }
};

LIBC_INLINE __llvm_libc_stdio_cookie *cookie_from_stream(::FILE *stream) {
  return reinterpret_cast<__llvm_libc_stdio_cookie *>(stream);
}

// Update C stream indicators after a vendor I/O return.
//   ret < 0  → error (ret is -errno); clear not done here
//   ret == 0 && requested > 0 → EOF
//   ret > 0  → success (leave prior eof until clearerr; set eof on short read
//              when fewer bytes than requested — typical file-at-EOF shape)
LIBC_INLINE void apply_io_indicators(__llvm_libc_stdio_cookie *cookie,
                                     ssize_t ret, size_t requested) {
  if (cookie == nullptr)
    return;
  if (ret < 0) {
    cookie->err = 1;
    return;
  }
  if (requested == 0)
    return;
  if (ret == 0 || static_cast<size_t>(ret) < requested)
    cookie->eof = 1;
}

LIBC_INLINE FileIOResult read_internal(char *buf, size_t size, ::FILE *stream) {
  auto *cookie = cookie_from_stream(stream);
  ssize_t ret = __llvm_libc_stdio_read(stream, buf, size);
  apply_io_indicators(cookie, ret, size);
  if (ret < 0)
    return {0, static_cast<int>(-ret)};
  return ret;
}

LIBC_INLINE FileIOResult write_internal(const char *buf, size_t size,
                                        ::FILE *stream) {
  auto *cookie = cookie_from_stream(stream);
  ssize_t ret = __llvm_libc_stdio_write(stream, buf, size);
  // Writes: only errors set the indicator (short write ≠ EOF).
  if (cookie != nullptr && ret < 0)
    cookie->err = 1;
  if (ret < 0)
    return {0, static_cast<int>(-ret)};
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDIO_BAREMETAL_FILE_INTERNAL_H
