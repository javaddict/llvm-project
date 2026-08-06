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
// Buffer layout byte offsets (see llvm-libc-types/__jmp_buf.h):
//   0:r8  4:r9  8:r10  12:r11  16:sp  20:lr
//   24:d8 32:d9 40:d10 48:d11 56:d12 64:d13 72:d14 80:d15
//
// ST32/LD32 and ST64/LD64 take a SCALED ELEMENT INDEX as the immediate, not a
// raw byte offset (matches the compiler's own spill convention: ST32 imm is a
// word index, EA = base + imm<<2; ST64 imm is a dword index, EA = base + imm<<3
// — see HaydnFrameLowering Shift = ST64?3:2). So byte offset 4 → st32 idx 1,
// byte 24 → st64 idx 3, etc. Writing raw byte offsets here previously saved
// every register to the wrong slot (ST32 off by x4, ST64 off by x8) and
// overflowed the jmp_buf segment (pr56982 MEMORY_FAULT).

namespace LIBC_NAMESPACE_DECL {

[[gnu::naked]]
LLVM_LIBC_FUNCTION(int, setjmp, ([[maybe_unused]] jmp_buf buf)) {
  asm(R"(
      st32 r8,  r1, 0
      st32 r9,  r1, 1
      st32 r10, r1, 2
      st32 r11, r1, 3
      st32 sp,  r1, 4
      st32 lr,  r1, 5
      st64 d8,  r1, 3
      st64 d9,  r1, 4
      st64 d10, r1, 5
      st64 d11, r1, 6
      st64 d12, r1, 7
      st64 d13, r1, 8
      st64 d14, r1, 9
      st64 d15, r1, 10
      # return 0
      addi32_w r1, r0, 0
      jalr_w r0, lr, 0
  )");
}

} // namespace LIBC_NAMESPACE_DECL
