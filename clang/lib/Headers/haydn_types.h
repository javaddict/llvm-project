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
 * Fail-closed target guard (xmmintrin.h peer) and public API revision.
 * Compiler capability macros (__HAYDN_ARCH__, __HAYDN_FEATURE_*) come from
 * TargetInfo — not this header. Bundle FormatID / slot / AltDesc are never
 * part of the public C surface.
 *
 *===------------------------------------------------------------------------===
 */

#ifndef __HAYDN_TYPES_H
#define __HAYDN_TYPES_H

/* Fail closed: public Haydn headers are Haydn-only (peer: xmmintrin.h). */
#if !defined(__haydn__) && !defined(__HAYDN__)
#error "This header is only meant to be used on Haydn architecture"
#endif

/* Public C API revision (header contract). Bump when public types / calling
 * conventions change in a source-incompatible way. Independent of
 * __HAYDN_ARCH__ (ISA family) and of any backend bundle format. */
#ifndef __HAYDN_API_VERSION
#define __HAYDN_API_VERSION 1
#endif

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

//===----------------------------------------------------------------------===//
// Predicate value types — SSA ABI, not ambient SFR
//===----------------------------------------------------------------------===//
//
// Hardware SFR is a 4-bit side-effect register. Public predicate values are
// ordinary unsigned integers carrying those bits in SSA form (Hexagon peer:
// C2_cmplt → i32 pred; C2_mux(Pu, …) consumes the value). Low bits:
//   haydn_pred2_t — bits [1:0] for X2 dual-32 lanes
//   haydn_pred4_t — bits [3:0] for X4 quad-16 lanes
// Ambient SFR APIs (x2slt32 / x2movt32) remain for low-level ordered SFR
// sequencing (x2/x4 movt/movf are IntrHasSideEffects). Public AE_LT32 /
// AE_MOVT* wrappers route through cmplt/mux SSA; prefer value + fused
// compare-select for multi-epoch code.

/// 2-lane predicate value (X2 SFR bits [1:0] captured into SSA).
typedef unsigned int haydn_pred2_t;

/// 4-lane predicate value (X4 SFR bits [3:0] captured into SSA).
typedef unsigned int haydn_pred4_t;

#endif /* __HAYDN_TYPES_H */
