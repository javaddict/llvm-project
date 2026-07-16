//===-- Implementation of longjmp for Haydn -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/setjmp/longjmp.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

// Haydn ABI (relevant bits):
//   Arg0 = r1 (jmp_buf *), Arg1 = r2 (val), return = r1
//   Callee-saved: r8-r11, sp (r13), lr (r15), d8-d15
//
// Buffer layout must match setjmp.cpp / __jmp_buf.h.
// longjmp returns max(val, 1) so a zero val becomes 1 (C standard).
// maxu32 is used so negative vals keep their bit pattern (unlike signed max).

namespace LIBC_NAMESPACE_DECL {

[[gnu::naked]]
LLVM_LIBC_FUNCTION(void, longjmp,
                   ([[maybe_unused]] jmp_buf buf, [[maybe_unused]] int val)) {
  asm(R"(
      ld32 r8,  r1, 0
      ld32 r9,  r1, 4
      ld32 r10, r1, 8
      ld32 r11, r1, 12
      ld32 sp,  r1, 16
      ld32 lr,  r1, 20
      ld64 d8,  r1, 24
      ld64 d9,  r1, 32
      ld64 d10, r1, 40
      ld64 d11, r1, 48
      ld64 d12, r1, 56
      ld64 d13, r1, 64
      ld64 d14, r1, 72
      ld64 d15, r1, 80
      # return val ?: 1  →  r1 = maxu32(r2, 1)
      addi32_w r3, r0, 1
      maxu32 r1, r2, r3
      jalr_w r0, lr, 0
  )");
}

} // namespace LIBC_NAMESPACE_DECL
