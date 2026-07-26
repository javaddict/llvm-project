//===-- Linux implementation of remove ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/remove.h"

#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

// Vendor freestanding OS layer (e.g. BundleSim hostcall) provides unlink.
extern "C" int unlink(const char *path);

namespace LIBC_NAMESPACE_DECL {

// Baremetal remove: delete a file via unlink. Directories need rmdir/unlinkat
// (BSP may provide a fuller remove that tries AT_REMOVEDIR).
LLVM_LIBC_FUNCTION(int, remove, (const char *path)) {
  if (path == nullptr) {
    libc_errno = EINVAL;
    return -1;
  }
  if (unlink(path) != 0)
    return -1;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
