//===-- Implementation of setjmp for Haydn --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/setjmp/setjmp_impl.h"

// Haydn ABI (relevant bits):
//   Arg0 = r1 (jmp_buf *), return = r1
//   Callee-saved: r8-r11, sp (r13), lr (r15), d8-d15
//
// Buffer layout (see llvm-libc-types/__jmp_buf.h):
//   0:r8  4:r9  8:r10  12:r11  16:sp  20:lr
//   24:d8 32:d9 40:d10 48:d11 56:d12 64:d13 72:d14 80:d15

namespace LIBC_NAMESPACE_DECL {

[[gnu::naked]]
LLVM_LIBC_FUNCTION(int, setjmp, ([[maybe_unused]] jmp_buf buf)) {
  asm(R"(
      st32 r8,  r1, 0
      st32 r9,  r1, 4
      st32 r10, r1, 8
      st32 r11, r1, 12
      st32 sp,  r1, 16
      st32 lr,  r1, 20
      st64 d8,  r1, 24
      st64 d9,  r1, 32
      st64 d10, r1, 40
      st64 d11, r1, 48
      st64 d12, r1, 56
      st64 d13, r1, 64
      st64 d14, r1, 72
      st64 d15, r1, 80
      # return 0
      addi32_w r1, r0, 0
      jalr_w r0, lr, 0
  )");
}

} // namespace LIBC_NAMESPACE_DECL
