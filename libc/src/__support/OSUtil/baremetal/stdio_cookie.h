//===-- Baremetal stdio cookie layout (C-compatible) ------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Cookie / baremetal FILE* body for fd-based stdio (semihost / hostcall).
//
// Contract:
//   - `fd` is at offset 0. Plat read/write only need `fd`.
//   - `eof` / `err` are C stream indicators (feof / ferror / clearerr).
//     Owned and updated by llvm-libc baremetal stdio (file_internal), not by
//     the hostcall. Vendors zero them on static stdin/stdout/stderr cookies.
//   - fopen allocates `sizeof(__llvm_libc_stdio_cookie)` and zeros the object.
//   - Vendors may append private fields after this layout.
//
// Include from C or C++ (plat, fopen, fclose, feof).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_BAREMETAL_STDIO_COOKIE_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_BAREMETAL_STDIO_COOKIE_H

struct __llvm_libc_stdio_cookie {
  int fd;
  unsigned char eof; // non-zero ⇒ feof() true
  unsigned char err; // non-zero ⇒ ferror() true
};

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_BAREMETAL_STDIO_COOKIE_H
