//===-- HaydnInstrGISel.td - Haydn GISel target pseudos ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn GlobalISel target generic-opcode definitions. Kept separate from
// HaydnGISel.td (which holds selection Pat patterns for regular 1:1 maps).
// This file follows the AArch64/RISCV convention (*InstrGISel.td) for
// declaring target generic ops (G_MULA64*, G_HAYDN_MUL64_WIDEN*, G_HAYDN_*INC_*).
//
// A target generic op has isPreIselOpcode = true, so the selector routes it
// past the "already selected" early-return (gated on the MCID::PreIselOpcode
// FLAG, not the opcode-range test -- see / c690d431) to the custom C++
// switch in HaydnInstructionSelector::select. RegBankSelect assigns banks by
// the type-driven default (HaydnRegisterBankInfo::getInstrMapping).
//
//===----------------------------------------------------------------------===//

class HaydnGenericInstruction : GenericInstruction {
  let Namespace = "Haydn";
}

// G_MULA64 / G_MULA64U -- fused widening multiply-accumulate, formed EARLY
// by the pre-legalizer combiner from:
//
// %ea = G_SEXT|G_ZEXT %a(s32)
// %eb = G_SEXT|G_ZEXT %b(s32)   (same extension kind on both)
// %m = G_MUL <s64> %ea, %eb; single-use
// %acc' = G_ADD <s64> %m, %acc; (or G_ADD %acc, %m)
// => %acc' = G_MULA64  %acc, %ea, %eb   (SEXT x SEXT -> MULA64_LL)
// => %acc' = G_MULA64U %acc, %ea, %eb   (ZEXT/ANYEXT x ZEXT/ANYEXT -> MULA64_ULUL)
//
// rd(s64) = ra(s64) + low32(rs1) * low32(rs2), with signedness matching the
// extension kind. Low-lane only: G_SEXT/G_ZEXT place the s32 in the low lane.
// High-lane (LH/HL/HH) and mixed-sign (LUL/ULL/…) only arise from the
// intrinsic path (haydn_mula64_ss_hh / haydn_mula64_uu_ull / etc.).
//
// ISA naming trap: MULA64_ULL is unsigned×SIGNED (rs2 sign-extended), NOT
// fully unsigned. Fully-unsigned low×low is MULA64_ULUL — matches the
// legalizer's zext×zext → MUL64_ULUL policy (mul-widen-i32-to-i64-native.ll).
//
// Forming pre-legalizer is mandatory: the legalizer replaces G_MUL<s64> with
// G_HAYDN_MUL64_WIDEN{,U}, so this is the only pass that sees the G_MUL+G_ADD
// shape. Selected to Haydn::MULA64_LL / MULA64_ULUL (tied acc, all DR64).
// formMACs / PostSelect MAC densify is FATED — do not revive.
def G_MULA64 : HaydnGenericInstruction {
  let OutOperandList = (outs type0:$rd);
  let InOperandList = (ins type0:$ra, type0:$rs1, type0:$rs2);
  let hasSideEffects = 0;
  let isCommutable = 1;  // rs1 <-> rs2 commute
}

// Unsigned×unsigned low-lane MAC (Wave T5.1). See G_MULA64 comment block.
def G_MULA64U : HaydnGenericInstruction {
  let OutOperandList = (outs type0:$rd);
  let InOperandList = (ins type0:$ra, type0:$rs1, type0:$rs2);
  let hasSideEffects = 0;
  let isCommutable = 1;  // rs1 <-> rs2 commute
}

// G_HAYDN_MUL64_WIDEN / G_HAYDN_MUL64_WIDENU -- 32x32->64 widening multiply.
// The legalizer rewrites G_MUL<s64> when both operands are G_SEXT / G_ZEXT /
// G_ANYEXT of s32 (AIE G_AIE_* shape: target-generic, then select):
//   SEXT x SEXT                 -> G_HAYDN_MUL64_WIDEN  -> MUL64_LL
//   ZEXT/ANYEXT x ZEXT/ANYEXT   -> G_HAYDN_MUL64_WIDENU -> MUL64_ULUL
// True 64x64 G_MUL is a schoolbook of three unsigned widening muls.
// Legalizer must not construct Haydn::MUL64_*. Selection of these opcodes
// is the C++ selector residual (same home as G_MULA64*); no ISD node, so
// HaydnGISel.td Pat cannot match them.
def G_HAYDN_MUL64_WIDEN : HaydnGenericInstruction {
  let OutOperandList = (outs type0:$rd);
  let InOperandList = (ins type0:$rs1, type0:$rs2);
  let hasSideEffects = 0;
  let isCommutable = 1;
}

def G_HAYDN_MUL64_WIDENU : HaydnGenericInstruction {
  let OutOperandList = (outs type0:$rd);
  let InOperandList = (ins type0:$rs1, type0:$rs2);
  let hasSideEffects = 0;
  let isCommutable = 1;
}

//===----------------------------------------------------------------------===//
// Pre/post-inc/dec memory (AIE-style gMIR). Formed by the post-legalizer
// combiner from G_LOAD/G_ZEXTLOAD/G_SEXTLOAD/G_STORE + G_PTR_ADD.
// Selected to fused AGU writeback ops (S_LW_POST_IMM / D_LDW_* / ST*_POST /
// *_PRE_IMM; byte/half: S_LBU/S_LBS / S_LHWU/S_LHWS).
//
//   POST: access at $base, then $ptr = $base + $offset
//   PRE:  $ptr = $base + $offset, then access at $ptr
//
// $offset is a scalar (usually G_CONSTANT); negative = decrement.
// $is_sext is 1 when formed from G_SEXTLOAD (select S_LBS/S_LHWS for 1/2-byte),
// 0 from G_LOAD/G_ZEXTLOAD (S_LBU/S_LHWU). Ignored for 4/8-byte loads.
// type0 = pointer, type1 = offset scalar, type2 = data.
//===----------------------------------------------------------------------===//

// %val, %ptr = G_HAYDN_POSTINC_LOAD %base, %offset, imm:$is_sext
def G_HAYDN_POSTINC_LOAD : HaydnGenericInstruction {
  let OutOperandList = (outs type2:$val, type0:$ptr);
  let InOperandList = (ins type0:$base, type1:$offset, i32imm:$is_sext);
  let hasSideEffects = 0;
  let mayLoad = 1;
}

// %val, %ptr = G_HAYDN_PREINC_LOAD %base, %offset, imm:$is_sext
def G_HAYDN_PREINC_LOAD : HaydnGenericInstruction {
  let OutOperandList = (outs type2:$val, type0:$ptr);
  let InOperandList = (ins type0:$base, type1:$offset, i32imm:$is_sext);
  let hasSideEffects = 0;
  let mayLoad = 1;
}

// %ptr = G_HAYDN_POSTINC_STORE %val, %base, %offset
def G_HAYDN_POSTINC_STORE : HaydnGenericInstruction {
  let OutOperandList = (outs type0:$ptr);
  let InOperandList = (ins type2:$val, type0:$base, type1:$offset);
  let hasSideEffects = 0;
  let mayStore = 1;
}

// %ptr = G_HAYDN_PREINC_STORE %val, %base, %offset
def G_HAYDN_PREINC_STORE : HaydnGenericInstruction {
  let OutOperandList = (outs type0:$ptr);
  let InOperandList = (ins type2:$val, type0:$base, type1:$offset);
  let hasSideEffects = 0;
  let mayStore = 1;
}
