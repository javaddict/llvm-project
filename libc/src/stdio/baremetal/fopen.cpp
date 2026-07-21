//===-- Implementation of fopen for baremetal -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Baremetal FILE* is a cookie pointer. Vendor plat provides:
//   struct __llvm_libc_stdio_cookie { int fd; };  // stdio_cookie.h
//   __llvm_libc_stdio_read/write(cookie, ...) → read/write(cookie->fd, ...)
// This fopen allocates a matching cookie after open(); fclose frees it.
// open/close come from the freestanding sys layer (e.g. BundleSim hostcall).
//
//===----------------------------------------------------------------------===//

#include "src/stdio/fopen.h"

#include "hdr/errno_macros.h"
#include "hdr/func/malloc.h"
#include "hdr/types/FILE.h"
#include "src/__support/OSUtil/baremetal/stdio_cookie.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

// POSIX open/close supplied by the vendor sys layer at link time.
extern "C" int open(const char *path, int flags, ...);
extern "C" int close(int fd);

namespace LIBC_NAMESPACE_DECL {
namespace {

// Flag values match Linux and BundleSim hostcall ABI for the common set.
constexpr int kO_RDONLY = 0;
constexpr int kO_WRONLY = 1;
constexpr int kO_RDWR = 2;
constexpr int kO_CREAT = 0x40;
constexpr int kO_EXCL = 0x80;
constexpr int kO_TRUNC = 0x200;
constexpr int kO_APPEND = 0x400;

// Default permission bits when creating (rw-rw-rw-).
constexpr int kOpenMode = 0666;

// Parse fopen mode string → open(2) flags. Returns -1 on invalid mode.
int open_flags_from_mode(const char *mode) {
  if (mode == nullptr || *mode == '\0')
    return -1;

  char main_mode = *mode++;
  bool plus = false;
  bool exclusive = false;
  for (; *mode != '\0'; ++mode) {
    switch (*mode) {
    case '+':
      plus = true;
      break;
    case 'b':
      // Binary is a no-op on baremetal.
      break;
    case 'x':
      exclusive = true;
      break;
    case 'r':
    case 'w':
    case 'a':
      // Only one main mode character is allowed.
      return -1;
    default:
      return -1;
    }
  }

  int flags = 0;
  switch (main_mode) {
  case 'r':
    flags = plus ? kO_RDWR : kO_RDONLY;
    break;
  case 'w':
    flags = kO_CREAT | kO_TRUNC | (plus ? kO_RDWR : kO_WRONLY);
    if (exclusive)
      flags |= kO_EXCL;
    break;
  case 'a':
    flags = kO_CREAT | kO_APPEND | (plus ? kO_RDWR : kO_WRONLY);
    break;
  default:
    return -1;
  }
  return flags;
}

} // namespace

LLVM_LIBC_FUNCTION(::FILE *, fopen,
                   (const char *__restrict path, const char *__restrict mode)) {
  if (path == nullptr) {
    libc_errno = EINVAL;
    return nullptr;
  }

  int flags = open_flags_from_mode(mode);
  if (flags < 0) {
    libc_errno = EINVAL;
    return nullptr;
  }

  int fd;
  if (flags & kO_CREAT)
    fd = open(path, flags, kOpenMode);
  else
    fd = open(path, flags);
  if (fd < 0)
    return nullptr;

  auto *cookie = static_cast<__llvm_libc_stdio_cookie *>(
      malloc(sizeof(__llvm_libc_stdio_cookie)));
  if (cookie == nullptr) {
    close(fd);
    libc_errno = ENOMEM;
    return nullptr;
  }
  cookie->fd = fd;
  cookie->eof = 0;
  cookie->err = 0;
  return reinterpret_cast<::FILE *>(cookie);
}

} // namespace LIBC_NAMESPACE_DECL
