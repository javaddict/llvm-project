/*===---- haydn_types.h - Haydn DSP data types (HiFi-compatible) -*- C -*-===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: LLVM-exception
 *
 *===------------------------------------------------------------------------===
 *
 * This header defines the NatureDSP/HiFi-compatible `ae_*` data types on top
 * of real Haydn vector types. It is the type layer that backs the native
 * `__builtin_ae_*` Haydn target builtins (D208), replacing the opaque-i64
 * typedef block that previously lived in haydn_dsp.h.
 *
 * Design (D208): the lanewise vector types use GCC `vector_size` syntax so the
 * compiler sees real `<4 x i16>` / `<2 x i32>` IR and can reach the Haydn
 * GISel vector pipeline (X4ADD16, X2ADD32, ...). Accumulator / cross-lane
 * types stay opaque `long long` (DR64) because their lane structure is
 * internal to the MAC/complex instruction and a vector type would mislead
 * codegen. This split is verified end-to-end: `add <4 x i16>` lowers to a
 * single `x4add16 d0, d1, d0` instruction today.
 *
 * Include this header (or haydn_dsp.h, which includes it) to get the types.
 * The builtins themselves are declared by the Haydn target and do not require
 * this header to be visible to clang's parser.
 *
 *===------------------------------------------------------------------------===
 */

#ifndef __HAYDN_TYPES_H
#define __HAYDN_TYPES_H

/* Provide fixed-width integer types (int32_t, int64_t, uintptr_t, size_t).
 * On a freestanding target the clang resource dir supplies these via the
 * freestanding headers, so the include is always available. */
#include <stdint.h>
#include <stddef.h>

//===----------------------------------------------------------------------===//
// Opaque 64-bit data register type (DR64)
//===----------------------------------------------------------------------===//

/// The 64-bit Haydn data register (DR64). All SIMD vector types and the
/// opaque accumulator types below are 8 bytes and live in this register file.
/// Exposed as a distinct C type for intrinsic argument compatibility.
typedef long long haydn_dr64_t;

//===----------------------------------------------------------------------===//
// Scalar integer / fractional types (HiFi conventions)
//===----------------------------------------------------------------------===//

/// Scalar types matching NatureDSP conventions. Q-format fractional data is
/// stored in plain ints on Haydn (no dedicated fractional scalar type).
typedef long long      ae_int64;    ///< 64-bit signed integer / accumulator
typedef int            ae_int32;    ///< 32-bit signed integer (Q1.31 fractional)
typedef short          ae_int16;    ///< 16-bit signed integer (Q1.15 fractional)
typedef unsigned       ae_uint32;   ///< 32-bit unsigned integer
typedef unsigned short ae_uint16;   ///< 16-bit unsigned integer
typedef unsigned char  ae_uint8;    ///<  8-bit unsigned integer

typedef int            ae_f32;      ///< 32-bit fractional (Q1.31)
typedef short          ae_f16;      ///< 16-bit fractional (Q1.15)
typedef long long      ae_f64;      ///< 64-bit fractional accumulator (opaque)
typedef int            ae_f24;      ///< 24-bit fractional (promoted to Q1.31)

//===----------------------------------------------------------------------===//
// SIMD vector types — real <N x M> IR via vector_size(8)
//===----------------------------------------------------------------------===//
// These map 1:1 to a DR64 register but expose lane structure to the
// compiler, enabling lanewise CSE, dead-lane elimination, vector icmp/select
// folding, and direct selection to X4*/X2* SIMD instructions. Verified:
//   typedef short v4i16 __attribute__((vector_size(8)));
//   v4i16 add(v4i16 a, v4i16 b) { return a+b; }
//   ->  { x4add16 d0, d1, d0 }   (single bundle slot, DR64 operands)
//
// vector_size(8) = 64 bits, matching DR64. The element type sets the lane
// count: short -> 4 lanes (<4 x i16>), int -> 2 lanes (<2 x i32>),
// float -> 2 lanes (<2 x f32>).
//
// The Haydn-native names (haydn_4xint16, haydn_2xint32, ...) are the
// canonical types; the HiFi-compatible `ae_*` names below are aliases of
// them, so the two naming conventions produce identical IR and are
// mutually assignable.

/// Quad 16-bit signed integer packed in DR64 — `<4 x i16>`. Haydn-native.
typedef short haydn_4xint16 __attribute__((vector_size(8)));

/// Dual 32-bit signed integer packed in DR64 — `<2 x i32>`. Haydn-native.
typedef int haydn_2xint32 __attribute__((vector_size(8)));

/// Quad 16-bit fractional packed in DR64 — `<4 x i16>` (Q1.15 lanes).
/// Haydn-native.
typedef short haydn_4xfract16 __attribute__((vector_size(8)));

/// Dual 32-bit fractional packed in DR64 — `<2 x i32>` (Q1.31 lanes).
/// Haydn-native.
typedef int haydn_2xfract32 __attribute__((vector_size(8)));

/// Dual 32-bit float packed in DR64 — `<2 x float>` (soft-float on Haydn;
/// arithmetic lowers via the soft-float path, not a HW SIMD float unit).
/// Haydn-native.
typedef float haydn_2xfloat32 __attribute__((vector_size(8)));

//===----------------------------------------------------------------------===//
// HiFi-compatible aliases (NatureDSP ae_* naming)
//===----------------------------------------------------------------------===//
// These alias the Haydn-native vector types above so ported NatureDSP
// sources see the same IR. Each is exactly the underlying native type.

typedef haydn_4xint16   ae_int16x4;    ///< Quad 16-bit signed (HiFi name)
typedef haydn_2xint32   ae_int32x2;    ///< Dual 32-bit signed (HiFi name)
typedef haydn_4xfract16 ae_f16x4;      ///< Quad 16-bit fractional (HiFi name)
typedef haydn_2xfract32 ae_f32x2;      ///< Dual 32-bit fractional (HiFi name)
typedef haydn_2xfloat32 ae_float32x2;  ///< Dual 32-bit float (HiFi name)

//===----------------------------------------------------------------------===//
// Opaque / cross-lane types — stay as plain 64-bit (no vector semantics)
//===----------------------------------------------------------------------===//
// These represent packed accumulators with cross-lane or widening semantics
// (complex MAC, fractional multiply with mixed lanes). Forcing a vector type
// would misrepresent the operation and would not help codegen — the HW lane
// structure is internal to the instruction. Kept as haydn_dr64_t aliases so
// they are mutually assignable with the DR64 accumulator intrinsics.

typedef haydn_dr64_t ae_int24x2;   ///< Dual 24-bit packed (emulated in 32-bit)
typedef haydn_dr64_t ae_f24x2;     ///< Dual 24-bit fractional (alias of int24x2)
typedef haydn_dr64_t ae_int64x2;   ///< Dual 64-bit (logical; stored as one DR64)
typedef haydn_dr64_t ae_p24x2;     ///< Pair-of-24 unsigned (alias of int24x2)
typedef haydn_dr64_t ae_p16x2;     ///< Pair-of-16 in upper/lower halves
typedef haydn_dr64_t ae_p16x2s;    ///< Pair-of-16 signed (alias)
typedef ae_int16     ae_p16s;      ///< Single 16-bit signed predicate-lane value

#endif /* __HAYDN_TYPES_H */
