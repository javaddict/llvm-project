/*===---- haydn_types.h - Haydn native DSP data types --------------*- C -*-===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===------------------------------------------------------------------------===
 *
 * Haydn-native register / SIMD types used by haydn.h.
 *
 * Naming mirrors ISA X2/X4 packs (same as haydn_x2add32 / haydn_x4add16):
 *   haydn_x2int32  — dual 32-bit lanes  (<2 x i32>)
 *   haydn_x4int16  — quad 16-bit lanes  (<4 x i16>)
 * Lanewise types use vector_size(8) so one value is one DR64.
 * NatureDSP ae_* aliases live in haydn_dsp.h, not here.
 *
 *===------------------------------------------------------------------------===
 */

#ifndef __HAYDN_TYPES_H
#define __HAYDN_TYPES_H

#include <stdint.h>
#include <stddef.h>

//===----------------------------------------------------------------------===//
// Opaque 64-bit data register (DR64)
//===----------------------------------------------------------------------===//

/// Raw 64-bit DR bag-of-bits (accumulators / non-lanewise packing).
typedef long long haydn_dr64_t;

//===----------------------------------------------------------------------===//
// SIMD vector types — vector_size(8) == one DR64
//===----------------------------------------------------------------------===//

/// Quad 16-bit signed — `<4 x i16>`.
typedef short haydn_x4int16 __attribute__((vector_size(8)));

/// Dual 32-bit signed — `<2 x i32>`.
typedef int haydn_x2int32 __attribute__((vector_size(8)));

/// Quad 16-bit fractional (Q1.15 lanes) — `<4 x i16>`.
typedef short haydn_x4fract16 __attribute__((vector_size(8)));

/// Dual 32-bit fractional (Q1.31 lanes) — `<2 x i32>`.
typedef int haydn_x2fract32 __attribute__((vector_size(8)));

/// Dual 32-bit float — `<2 x float>` (soft-float path).
typedef float haydn_x2float32 __attribute__((vector_size(8)));

#endif /* __HAYDN_TYPES_H */
