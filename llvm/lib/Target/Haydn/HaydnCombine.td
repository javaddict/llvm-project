//===-- HaydnCombine.td - Define Haydn Combine Rules ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// GISel combiner patterns for Haydn (Pre- and Post-legalizer).
//
// North star (Wave T2–T7): TableGen-primary algebraic combines
// and target fuse *registry*. Import generic Combine.td groups before inventing
// Haydn rules. C++ residual is match/apply helpers only for target fuses that
// GICombineRule cannot express pure-TD (AGU MMO rewrite, G_MULA64 multi-def).
//
// Soft-float Haydn: prune AIE-style full generics that densify hard float
// (fma_combines, constant_fold_fma, fsub_to_fneg, fabs_fneg_fold). FP identity
// folds that remain inside identity_combines / constant_fold_binops are harmless
// no-ops when no FP gMIR remains.
//
// form_bitfield_extract is intentionally omitted until BFX legal policy lands.
//
// Dual-home policy: when a rule lives in a Haydn Combine group, matching C++
// switch arms in Haydn{Pre,Post}LegalizerCombiner.cpp must be deleted in the
// same land. Pass shells call tryCombineAllImpl only (no free-form fuse switch).
//
// MAC densify (G_MUL + G_ADD -> MAC32) is NOT done here — formMACs is FATED.
// Product MAC = intrinsic path + PreLegalizer G_MULA64 (TD-registered).
//
//===----------------------------------------------------------------------===//

include "llvm/Target/GlobalISel/Combine.td"

// Shared legal-preserving generic subset (AIE aie_generic_combines shape,
// pruned for soft-float Haydn). Used by both Pre and Post.
//
// Group membership expands individuals:
//   trivial_combines  -> copy_prop, mul_to_shl, sub_to_add, mul_by_neg_one, ...
//   identity_combines -> select_same_val, right_identity_zero, binop_same_val,
//                        right_identity_one (int+fp), anyext_trunc_fold, ...
//   known_bits_simplifications -> redundant_and, redundant_or,
//                                  redundant_sext_inreg, urem_pow2_to_mask, ...
//   constant_fold_binops -> constant_fold_binop, constant_fold_fp_binop
//   cast_combines -> cast_of_cast (truncate_of_{z,s,any}ext, {z,s}ext_of_*), ...
//   cmp_combines -> canonicalize_icmp/fcmp, icmp known-bits, double icmp zero
//
// intdiv/intrem intentionally NOT shared: strength-reduce before legalizer
// libcall only (Pre). Post must stay legal-preserving and must not re-create
// illegal div ops after legalization (T3.7 / T7.2).
def haydn_shared_generic_combines : GICombineGroup<[
  // --- optnone / identity core ---
  trivial_combines,
  combines_for_extload,
  identity_combines,
  ptr_add_immed_chain,
  not_cmp_fold,
  opt_brcond_by_inverting_cond,

  // --- const / select / cast / cmp ---
  constant_fold_binops,
  select_to_minmax,
  select_to_iminmax,
  cast_combines,
  cmp_combines,

  // --- known-bits identities ---
  known_bits_simplifications,

  // --- extra safe integer-ish generics (no hard-float FMA) ---
  undef_combines,
  phi_combines,
  simplify_add_to_sub,
  shifts_too_big,
  reassocs,
  ptr_add_with_zero,
  commute_constant_to_rhs,
  xor_of_and_with_same_reg,
  and_or_disjoint_mask,
  sub_add_reg,
  redundant_neg_operands,
  mulh_combines,
]>;

// Pre-only: no native div → strength-reduce before legalizer libcall.
def haydn_pre_generic_combines : GICombineGroup<[
  haydn_shared_generic_combines,
  intdiv_combines,
  intrem_combines,
]>;

// Post: legal-preserving shared generics only (no intdiv/intrem).
def haydn_post_generic_combines : GICombineGroup<[
  haydn_shared_generic_combines,
]>;

//===----------------------------------------------------------------------===//
// PreLegalizer target fuse registry (Wave T7.2)
// Match/apply live in HaydnPreLegalizerCombiner.cpp — TD owns dispatch.
//===----------------------------------------------------------------------===//

// G_ADD<s64>(acc, G_MUL(ext s32, ext s32)) -> G_MULA64 / G_MULA64U
def mula64_matchdata : GIDefMatchData<"MULA64CombineInfo">;
def form_mula64 : GICombineRule<
  (defs root:$root, mula64_matchdata:$matchinfo),
  (match (wip_match_opcode G_ADD):$root,
         [{ return matchCombineMULA64(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyCombineMULA64(*${root}, MRI, B, Observer, ${matchinfo}); }])
>;

def haydn_pre_target_combines : GICombineGroup<[
  form_mula64,
]>;

//===----------------------------------------------------------------------===//
// Haydn-specific residual GICombineRules (Post algebraic residuals).
// Thin C++ match/apply live in HaydnPostLegalizerCombiner.cpp (AIE style).
// None of these exist as generic Combine.td rules (or root/semantics differ).
//===----------------------------------------------------------------------===//

// G_SUB x, G_SUB(0, y) -> G_ADD x, y
def sub_of_neg_matchdata : GIDefMatchData<"std::pair<Register, Register>">;
def sub_of_neg : GICombineRule<
  (defs root:$root, sub_of_neg_matchdata:$matchinfo),
  (match (wip_match_opcode G_SUB):$root,
         [{ return matchSubOfNeg(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applySubOfNeg(*${root}, MRI, B, Observer, ${matchinfo}); }])
>;

// G_BSWAP(G_BSWAP x) -> x
def bswap_of_bswap : GICombineRule<
  (defs root:$root, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_BSWAP):$root,
         [{ return matchBswapOfBswap(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyBswapOfBswap(*${root}, MRI, B, Observer, ${matchinfo}); }])
>;

// (A ^ C1) ^ C2 -> A ^ (C1^C2)  (or COPY if folded const is 0)
def xor_xor_const_matchdata : GIDefMatchData<"std::pair<Register, APInt>">;
def xor_xor_constant_fold : GICombineRule<
  (defs root:$root, xor_xor_const_matchdata:$matchinfo),
  (match (wip_match_opcode G_XOR):$root,
         [{ return matchXorXorConstantFold(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyXorXorConstantFold(*${root}, MRI, B, Observer, ${matchinfo}); }])
>;

// XOR(XOR(x, -1), -1) -> x
def double_not : GICombineRule<
  (defs root:$root, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_XOR):$root,
         [{ return matchDoubleNot(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyDoubleNot(*${root}, MRI, B, Observer, ${matchinfo}); }])
>;

// OR-rooted: (A & MaskC) | SetC -> A | SetC when MaskC & SetC == 0 and
// MaskC | SetC == all-ones. TD and_or_disjoint_mask is AND-rooted only.
def and_or_disjoint_or_matchdata
    : GIDefMatchData<"std::tuple<Register, APInt, APInt>">;
def and_or_disjoint_or : GICombineRule<
  (defs root:$root, and_or_disjoint_or_matchdata:$matchinfo),
  (match (wip_match_opcode G_OR):$root,
         [{ return matchAndOrDisjoint(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyAndOrDisjoint(*${root}, MRI, B, Observer, ${matchinfo}); }])
>;

// Haydn BNEZ / anyext-i1: xor (anyext i1), -1 -> xor (and x, 1), 1
def xor_allones_bool_matchdata : GIDefMatchData<"bool">;
def xor_allones_boolean : GICombineRule<
  (defs root:$root, xor_allones_bool_matchdata:$matchinfo),
  (match (wip_match_opcode G_XOR):$root,
         [{ return matchXorAllOnesBoolean(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyXorAllOnesBoolean(*${root}, MRI, B, Observer, ${matchinfo}); }])
>;

// G_MUL x, (pow2±1) is not rewritten to shift-add/sub. Haydn has 1-slot
// MULL (MAC), so SHL+ADD/SUB is two ALU ops vs one MAC issue — an
// unconditional pessimization. Generic mul_to_shl (pure pow2) stays in
// trivial_combines: SLLI is one ALU vs one MAC and packs on ALU0/1/2.

// G_SEXT(G_ZEXT x) -> G_ZEXT x (wider). Not in cast_of_cast_combines.
def sext_of_zext : GICombineRule<
  (defs root:$root, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_SEXT):$root,
         [{ return matchSExtOfZExt(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applySExtOfZExt(*${root}, MRI, B, Observer, ${matchinfo}); }])
>;

// Priority: xor folds before boolean rewrite.
def haydn_post_residual_combines : GICombineGroup<[
  sub_of_neg,
  bswap_of_bswap,
  xor_xor_constant_fold,
  double_not,
  and_or_disjoint_or,
  xor_allones_boolean,
  sext_of_zext,
]>;

//===----------------------------------------------------------------------===//
// PostLegalizer target fuse registry (Wave T7.2) — AGU pre/post-inc
// Match/apply live in HaydnPostLegalizerCombiner.cpp. Gated on Subtarget
// hasAGU() + -haydn-enable-gisel-update-addr (default ON).
//===----------------------------------------------------------------------===//

def agu_inc_mem_matchdata : GIDefMatchData<"HaydnIncMemInfo">;
def form_agu_inc_mem : GICombineRule<
  (defs root:$root, agu_inc_mem_matchdata:$matchinfo),
  (match (wip_match_opcode G_LOAD, G_ZEXTLOAD, G_SEXTLOAD, G_STORE):$root,
         [{ return matchCombineAGUIncMem(*${root}, MRI, Helper, ${matchinfo}); }]),
  (apply [{ applyIncMem(*${root}, MRI, B, Observer, ${matchinfo}); }])
>;

def haydn_post_target_combines : GICombineGroup<[
  form_agu_inc_mem,
]>;

// Post-legalizer combiner for Haydn.
// TableGen owns algebraic generics, residual GICombineRules, and AGU fuse.
// Pass shell: tryCombineAllImpl only (no free-form switch, no sanitizer).
def HaydnPostLegalizerCombiner : GICombiner<
  "HaydnPostLegalizerCombinerImpl", [
  haydn_post_generic_combines,
  haydn_post_residual_combines,
  haydn_post_target_combines,
]> {
  let CombineAllMethodName = "tryCombineAllImpl";
}

// Pre-legalizer combiner for Haydn.
// Shared generics + Pre-only intdiv/intrem + MULA64 fuse registry.
// Cast identities are TD-only (cast_combines); no C++ dual homes.
def HaydnPreLegalizerCombiner : GICombiner<
  "HaydnPreLegalizerCombinerImpl", [
  haydn_pre_generic_combines,
  haydn_pre_target_combines,
]> {
  let CombineAllMethodName = "tryCombineAllImpl";
}

//===----------------------------------------------------------------------===//
// Dual-home / residual inventory (T7.1–T7.2 closed for listed items)
//
// PreLegalizer C++ residual after T7:
//   KEEP (TD-registered match/apply only — no free-form switch):
//   - form_mula64 -> matchCombineMULA64 / applyCombineMULA64
//   DELETED (now TD-only via cast_combines / haydn_pre_generic_combines):
//   - trunc(sext/zext/anyext) identity C++
//   - redundant G_SEXT/G_ZEXT collapse C++
//   - G_AND/G_OR identity, shift-by-0, ptr_add immed chain,
//     constant_fold binops, intdiv/intrem strength reduction
//
// PostLegalizer C++ residual after T7:
//   KEEP (TD-registered match/apply only):
//   - form_agu_inc_mem -> matchCombineAGUIncMem / applyIncMem
//   - haydn_post_residual match/apply free functions
//     (mul_to_shift_add/sub deleted: 1-slot MULL vs SHL+ADD pessimization)
//   DELETED:
//   - sanitizeCastCopies post-legal repair path (producers fixed at Pre)
//   - free-form AGU switch after tryCombineAllImpl
//   - algebraic dual-home C++ (earlier T3 purge)
//===----------------------------------------------------------------------===//
