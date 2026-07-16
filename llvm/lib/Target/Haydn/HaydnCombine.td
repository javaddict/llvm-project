//===-- HaydnCombine.td - Define Haydn Combine Rules ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Post-legalizer combiner patterns for Haydn.
//
// NOTE: MAC fusion (G_MUL + G_ADD -> MAC32) is NOT done here because this
// combiner runs before RegBankSelect — registers are still generic but MAC32
// requires GPR32. MAC fusion is handled in PostSelectOptimize instead, where
// registers have been assigned to register banks.
//
// The post-legalizer combiner includes both TableGen-generated rules (below)
// and target-specific C++ rules in HaydnPostLegalizerCombiner.cpp.
//
// TableGen rules leverage the generic GICombiner infrastructure (e.g.
// select_same_val, redundant_and/or, constant folding). Additional
// Haydn-specific patterns that don't map neatly to TableGen are implemented
// in C++ in HaydnPostLegalizerCombiner.cpp::tryCombineAll.
//
//===----------------------------------------------------------------------===//

include "llvm/Target/GlobalISel/Combine.td"

// Post-legalizer combiner for Haydn.
// MAC fusion is handled in PostSelectOptimize (after RegBankSelect).
//
// TableGen rules (dispatched via tryCombineAllImpl):
// select_same_val: G_SELECT(cond, x, x) -> x
// redundant_and: G_AND x, -1 -> x
// redundant_or: G_OR x, 0 -> x
// constant_fold_binops: G_ADD/G_SUB/G_MUL const, const -> const
// redundant_sext_inreg: redundant sext_inreg patterns
// right_identity_zero: G_ADD/G_SUB/G_OR/G_XOR/G_SHL/G_ASHR/G_LSHR x, 0 -> x
// right_identity_one_int: G_MUL x, 1 -> x
// binop_same_val: G_AND x, x -> x; G_OR x, x -> x
//
// Additional C++ rules (in HaydnPostLegalizerCombiner.cpp::tryCombineAll
// run after the TableGen dispatch):
// trunc_of_anyext: G_TRUNC(G_ANYEXT x) -> COPY x
// trunc_of_ext_identity: G_TRUNC(G_SEXT/G_ZEXT x) -> COPY x (if same type)
// ext_trunc_identity: G_SEXT/G_ZEXT x -> G_TRUNC -> src (if same type)
// redundant_copy: G_COPY x, x -> eliminate
// constant_fold_icmp: G_ICMP const, const -> const 0 or 1
// icmp_known_bits: G_ICMP with KnownBits -> const true/false
// and_zero: G_AND x, 0 -> 0
// or_all_ones: G_OR x, -1 -> -1
// bswap_of_bswap: G_BSWAP(G_BSWAP x) -> COPY x
// redundant_sext: G_SEXT(G_SEXT x) where no-op -> COPY x
// redundant_zext: G_ZEXT(G_ZEXT x) where no-op -> COPY x
// add_of_neg: G_ADD x, G_SUB(0, y) -> G_SUB x, y
// sub_of_neg: G_SUB x, G_SUB(0, y) -> G_ADD x, y
// mul_by_neg_one: G_MUL x, -1 -> G_SUB 0, x
// sext_inreg_of_sext: G_SEXT_INREG(G_SEXT x, K) where sext covers K -> COPY
// xor_xor_constant_fold: (A ^ C1) ^ C2 -> A ^ (C1^C2) (constant fold XOR pair)
// double_not: XOR(NOT(x)) -> x (double NOT cancellation)
// and_or_disjoint: (A & MaskC) | SetC with disjoint MaskC/SetC -> A | SetC
// shift_mask_redundant: (x >> C) & exact_remaining_mask -> x >> C
// xor_zero: G_XOR x, 0 -> x (identity)
// sext_inreg_of_ashr: G_SEXT_INREG(G_ASHR x, C) where redundant -> COPY
// mul_to_shift: G_MUL x, pow2 -> G_SHL x, log2(pow2) (strength reduction)
// byte_pack_recognition: G_ADD(AND(x,0xFF), SHL(AND(y,0xFF),C)) recognition
def HaydnPostLegalizerCombiner : GICombiner<
  "HaydnPostLegalizerCombinerImpl", [
  // G_SELECT(cond, x, x) -> x (true/false values are identical)
  select_same_val,

  // G_AND x, -1 -> x (AND with all-ones is identity)
  redundant_and,

  // G_OR x, 0 -> x (OR with zero is identity)
  redundant_or,

  // Constant fold: G_ADD/G_SUB/G_MUL with two constant operands
  constant_fold_binops,

  // Redundant sext_inreg: when the shift amount covers the full width
  redundant_sext_inreg,

  // G_ADD/G_SUB/G_OR/G_XOR/G_SHL/G_ASHR/G_LSHR x, 0 -> x
  right_identity_zero,

  // G_MUL x, 1 -> x
  right_identity_one_int,

  // G_AND x, x -> x; G_OR x, x -> x
  binop_same_val,
]> {
  let CombineAllMethodName = "tryCombineAllImpl";
}

// Pre-legalizer combiner for Haydn.
// Runs before the legalizer on generic G_* instructions.
// Combines here reduce IR complexity before type/action legalization.
// Target-specific combines (trunc-of-anyext, redundant and/or, etc.) are
// implemented in C++ in HaydnPreLegalizerCombiner.cpp.
def HaydnPreLegalizerCombiner : GICombiner<
  "HaydnPreLegalizerCombinerImpl", []> {
  let CombineAllMethodName = "tryCombineAllImpl";
}
