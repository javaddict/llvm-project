//===-- HaydnInstrGISel.td - Haydn GISel target pseudos ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn GlobalISel target generic-opcode definitions. Kept separate from
// HaydnGISel.td (which holds selection Pat patterns -- intentionally empty
// since Haydn selects via hand-written C++). This file follows the AArch64
// RISCV convention (*InstrGISel.td) for declaring target generic ops.
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

// G_MULA64 -- fused widening multiply-accumulate, formed EARLY by the
// pre-legalizer combiner from:
//
// %ea = G_SEXT %a(s32); both sources sign-extended
// %eb = G_SEXT %b(s32)
// %m = G_MUL <s64> %ea, %eb; single-use
// %acc' = G_ADD <s64> %m, %acc; (or G_ADD %acc, %m)
// => %acc' = G_MULA64 %acc, %ea, %eb
//
// rd(s64) = ra(s64) + rs1(s64) * rs2(s64), where the multiply is of the LOW
// 32-bit lane of each s64 source, widened to 64 -- i.e. MULA64_LL semantics
// (rtd = rtd + rsd1[31:0] * rsd2[31:0]). The G_SEXT places the value in the
// low lane, so generic fusion always selects MULA64_LL; lane variants
// LL/LH/HL/HH only arise from the intrinsic path (haydn_mula64_ss_hh etc.).
//
// Signed-signed only: both G_MUL operands must be G_SEXT of s32 (the `_ss_`
// accumulator variant). Unsigned/mixed (`_su_`/`_uu_`) have no accumulator
// form and are not fused here.
//
// Forming it pre-legalizer is mandatory: the legalizer replaces G_MUL<s64>
// with MUL64_LL (HaydnLegalizerInfo.cpp ~line 625), so this is the only pass
// that sees the G_MUL+G_ADD shape. Selected to Haydn::MULA64_LL (tied acc
// all operands DR64). See.
def G_MULA64 : HaydnGenericInstruction {
  let OutOperandList = (outs type0:$rd);
  let InOperandList = (ins type0:$ra, type0:$rs1, type0:$rs2);
  let hasSideEffects = 0;
  let isCommutable = 1;  // rs1 <-> rs2 commute
}

//===----------------------------------------------------------------------===//
// Pre/post-inc/dec memory (AIE-style gMIR). Formed by the post-legalizer
// combiner from G_LOAD/G_STORE + G_PTR_ADD (const offset). Selected to fused
// AGU writeback ops (S_LW_POST_IMM / D_LDW_* / ST*_POST / *_PRE_IMM).
//
//   POST: access at $base, then $ptr = $base + $offset
//   PRE:  $ptr = $base + $offset, then access at $ptr
//
// $offset is a scalar (usually G_CONSTANT); negative = decrement.
// type0 = pointer, type1 = offset scalar, type2 = data.
//===----------------------------------------------------------------------===//

// %val, %ptr = G_HAYDN_POSTINC_LOAD %base, %offset
def G_HAYDN_POSTINC_LOAD : HaydnGenericInstruction {
  let OutOperandList = (outs type2:$val, type0:$ptr);
  let InOperandList = (ins type0:$base, type1:$offset);
  let hasSideEffects = 0;
  let mayLoad = 1;
}

// %val, %ptr = G_HAYDN_PREINC_LOAD %base, %offset
def G_HAYDN_PREINC_LOAD : HaydnGenericInstruction {
  let OutOperandList = (outs type2:$val, type0:$ptr);
  let InOperandList = (ins type0:$base, type1:$offset);
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
