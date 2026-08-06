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
//
// pushback is volatile: Haydn VLIW + LTO has been observed to dead-store
// eliminate ungetc's write when the whitespace-skip while-loop is not
// entered (raw_match(" ") on non-space input), so the subsequent
// convert_string/int getc() never sees the restored character and %s/%d
// match zero conversions (fscanf returns EOF). Leading-space inputs that
// exercise the while body still worked. volatile forces the store/load.
class StreamReader : public scanf_core::Reader<StreamReader> {
  ::FILE *stream;
  // -1 = empty; otherwise the pushed-back character (as unsigned char).
  volatile int pushback = -1;
  // Member read buffer (not a getc stack temporary) so the hostcall write
  // target is a stable object; pairs with volatile pushback for Haydn.
  char read_buf = 0;

public:
  LIBC_INLINE StreamReader(::FILE *stream) : stream(stream) {}

  LIBC_INLINE char getc() {
    int pb = pushback;
    if (pb >= 0) {
      pushback = -1;
      return static_cast<char>(pb);
    }
    read_buf = 0;
    auto result = __llvm_libc_stdio_read(stream, &read_buf, 1);
    if (result != 1)
      return '\0';
    return read_buf;
  }
  LIBC_INLINE void ungetc(int c) { pushback = c & 0xff; }
};

} // namespace internal

LIBC_INLINE int vfscanf_internal(::FILE *__restrict stream,
                                 const char *__restrict format,
                                 internal::ArgList &args) {
  internal::StreamReader reader(stream);
  // scanf_main returns the conversion count. Only input failure before any
  // conversion should yield EOF; matching failure after consuming input
  // returns 0 (C standard). The old `retval == 0 → EOF` collapsed both.
  int retval = scanf_core::scanf_main(&reader, format, args);
  if (retval == 0)
    return reader.chars_read() == 0 ? EOF : 0;
  return retval;
}

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDIO_BAREMETAL_VFPRINTF_INTERNAL_H
