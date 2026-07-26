//===-- Implementation header of vfscanf ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDIO_BAREMETAL_VFPRINTF_INTERNAL_H
#define LLVM_LIBC_SRC_STDIO_BAREMETAL_VFPRINTF_INTERNAL_H

#include "hdr/stdio_macros.h" // for EOF.
#include "hdr/types/FILE.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/OSUtil/io.h"
#include "src/__support/arg_list.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/stdio/scanf_core/reader.h"
#include "src/stdio/scanf_core/scanf_main.h"

namespace LIBC_NAMESPACE_DECL {

namespace internal {

// Baremetal FILE has no libc File::ungetc; scanf converters always
// read one past and push back once (leading whitespace skip for %s/%d,
// terminator for %s, etc.). A single-char pushback buffer is required.
class StreamReader : public scanf_core::Reader<StreamReader> {
  ::FILE *stream;
  // -1 = empty; otherwise the pushed-back character (as unsigned char).
  int pushback = -1;

public:
  LIBC_INLINE StreamReader(::FILE *stream) : stream(stream) {}

  LIBC_INLINE char getc() {
    if (pushback >= 0) {
      char c = static_cast<char>(pushback);
      pushback = -1;
      return c;
    }
    char c;
    auto result = __llvm_libc_stdio_read(stream, &c, 1);
    if (result != 1)
      return '\0';
    return c;
  }
  LIBC_INLINE void ungetc(int c) { pushback = c & 0xff; }
};

} // namespace internal

LIBC_INLINE int vfscanf_internal(::FILE *__restrict stream,
                                 const char *__restrict format,
                                 internal::ArgList &args) {
  internal::StreamReader reader(stream);
  // This is done to avoid including stdio.h in the internals. On most systems
  // EOF is -1, so this will be transformed into just "return retval".
  int retval = scanf_core::scanf_main(&reader, format, args);
  return (retval == 0) ? EOF : retval;
}

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDIO_BAREMETAL_VFPRINTF_INTERNAL_H
