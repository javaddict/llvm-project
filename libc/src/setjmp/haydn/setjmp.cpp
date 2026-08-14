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
      // Format E: § 5.6 renamed the load/store family and the immediate is an
      // ELEMENT index, not a byte offset — S_SW/S_LW are `rs + (imm6 << 2)`
      // and D_SDW/D_LDW are `rs + (imm6 << 3)`. The byte offsets in the frame
      // comment above therefore divide by 4 and 8 respectively. Getting that
      // wrong assembles and addresses the wrong slot, so the two must be read
      // together. `_w` suffixes are gone with the § 5.1 fold.
      s_sw_with_imm r8, r1, 0
      s_sw_with_imm r9, r1, 1
      s_sw_with_imm r10, r1, 2
      s_sw_with_imm r11, r1, 3
      s_sw_with_imm sp, r1, 4
      s_sw_with_imm lr, r1, 5
      d_sdw_with_imm d8, r1, 3
      d_sdw_with_imm d9, r1, 4
      d_sdw_with_imm d10, r1, 5
      d_sdw_with_imm d11, r1, 6
      d_sdw_with_imm d12, r1, 7
      d_sdw_with_imm d13, r1, 8
      d_sdw_with_imm d14, r1, 9
      d_sdw_with_imm d15, r1, 10
      # return 0
      addi32 r1, r0, 0
      jalr r0, lr, 0
  )");
}

} // namespace LIBC_NAMESPACE_DECL
