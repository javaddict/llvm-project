//=== lib/builtins/haydn/fp_mode.c - Soft-float mode utilities -*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Peer: compiler-rt/lib/builtins/fp_mode.c (generic software default) and
// the RISC-V no-FPU arm of riscv/fp_mode.c:17-34 (CRT_FE_TONEAREST; no CSR).
// Haydn is ILP32 software IEEE-754; there is no hardware rounding mode.
// Parent haydn_SOURCES still lists GENERIC_SOURCES (same default). This
// file is the PORT-FIRST overlay home for a later one-line CMake hook.
//
//===----------------------------------------------------------------------===//

#include "../fp_mode.h"

CRT_FE_ROUND_MODE __fe_getround(void) { return CRT_FE_TONEAREST; }

int __fe_raise_inexact(void) { return 0; }
