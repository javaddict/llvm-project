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
      // Format E: § 5.6 renamed the load/store family and the immediate is an
      // ELEMENT index, not a byte offset — S_SW/S_LW are `rs + (imm6 << 2)`
      // and D_SDW/D_LDW are `rs + (imm6 << 3)`. The byte offsets in the frame
      // comment above therefore divide by 4 and 8 respectively. Getting that
      // wrong assembles and addresses the wrong slot, so the two must be read
      // together. `_w` suffixes are gone with the § 5.1 fold.
      s_lw_with_imm r8, r1, 0
      s_lw_with_imm r9, r1, 1
      s_lw_with_imm r10, r1, 2
      s_lw_with_imm r11, r1, 3
      s_lw_with_imm sp, r1, 4
      s_lw_with_imm lr, r1, 5
      d_ldw_with_imm d8, r1, 3
      d_ldw_with_imm d9, r1, 4
      d_ldw_with_imm d10, r1, 5
      d_ldw_with_imm d11, r1, 6
      d_ldw_with_imm d12, r1, 7
      d_ldw_with_imm d13, r1, 8
      d_ldw_with_imm d14, r1, 9
      d_ldw_with_imm d15, r1, 10
      # return val ?: 1  →  r1 = maxu32(r2, 1)
      addi32 r3, r0, 1
      maxu32 r1, r2, r3
      jalr r0, lr, 0
  )");
}

} // namespace LIBC_NAMESPACE_DECL
