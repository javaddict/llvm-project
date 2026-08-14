//===-- HaydnLegalizerInfo.cpp ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// This file implements the targeting of the MachineLegalizer class for Haydn.
//===----------------------------------------------------------------------===//

#include "HaydnLegalizerInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;
using namespace LegalityPredicates;
using namespace LegalizeMutations;
using namespace llvm::MIPatternMatch;

#define DEBUG_TYPE "haydn-legalizer"

HaydnLegalizerInfo::HaydnLegalizerInfo(const HaydnSubtarget &ST) {
  using namespace TargetOpcode;

  // NOLINTBEGIN(readability-identifier-naming)
  const LLT S1 = LLT::scalar(1);      // NOLINT
  const LLT S8 = LLT::scalar(8);      // NOLINT
  const LLT S16 = LLT::scalar(16);    // NOLINT
  const LLT S32 = LLT::scalar(32);    // NOLINT
  const LLT S64 = LLT::scalar(64);    // NOLINT
  const LLT S128 = LLT::scalar(128);  // NOLINT
  const LLT P0 = LLT::pointer(0, 32); // NOLINT
  // NOLINTEND(readability-identifier-naming)

  //===--------------------------------------------------------------------===
  // Integer Arithmetic
  //===--------------------------------------------------------------------===
  // s32 add/sub are natively legal; s64 add/sub use DR64 instructions (ADD64/SUB64).
  // SIMD types: v2i32, v4i16, v8i8 (all 64-bit, fit in DR64)
  const LLT V2I32 = LLT::fixed_vector(2, 32);  // NOLINT
  const LLT V4I16 = LLT::fixed_vector(4, 16);  // NOLINT
  const LLT V8I8 = LLT::fixed_vector(8, 8);    // NOLINT
  const LLT V4I8 = LLT::fixed_vector(4, 8);    // residual SLP (not native)
  const LLT V2I16 = LLT::fixed_vector(2, 16);  // residual SLP
  // CB-130: vectors wider than 64 bits (e.g. <4 x s32>) are out of product
  // SIMD scope — scalarize to s32/s64 ops. Haydn native SIMD is 64-bit only
  // (v2i32 / v4i16 / v8i8 in DR64).
  auto ScalarizeWideVec = [](unsigned TypeIdx = 0) {
    return [=](const LegalityQuery &Query) {
      const LLT Ty = Query.Types[TypeIdx];
      return Ty.isVector() && Ty.getSizeInBits() > 64;
    };
  };

  getActionDefinitionsBuilder({G_ADD, G_SUB})
      .legalFor({S32, S64, V2I32, V4I16})
      .scalarizeIf(ScalarizeWideVec(0), 0)
      // Residual non-native vectors (e.g. v2i16 from SLP/CoreMark after
 // registers v4i16/v2i32) — scalarize, do not leave illegal.
      .scalarize(0)
      .minScalar(0, S32)
      .maxScalar(0, S64)
      .widenScalarToNextPow2(0);

  // G_MUL elementwise wrap:
  //   s32   — MULL (golden MAC GRR low-half product)
  //   v2i32 — X2MULPL32 (low 32 of each dual 32x32 product)
  //   v4i16 — NOT X4MUL16 (2-dest DSP mul). Scalarize; true X4MUL16 only via
  //           llvm.haydn.x4mul16 / haydn_x4mul16.
  // s64: custom G_HAYDN_MUL64_WIDEN{,U} (widen 32x32->64) or schoolbook
  // partials — never __muldi3. Use customFor for s64, not legalFor/libcallFor
  // (legalFor would leave G_MUL for a selector that has no s64 Pat;
  // libcallFor fails Haydn call arg splitting).
  getActionDefinitionsBuilder(G_MUL)
      .legalFor({S32, V2I32})
      .customFor({S64})
      .scalarizeIf(ScalarizeWideVec(0), 0)
      .minScalar(0, S32)
      .maxScalar(0, S64)
      .widenScalarToNextPow2(0)
      .scalarize(0);

  // Haydn has no native division/remainder - use libcalls.
  getActionDefinitionsBuilder({G_SDIV, G_UDIV, G_SREM, G_UREM, G_SDIVREM, G_UDIVREM})
      .libcallFor({S32, S64})
      .scalarizeIf(ScalarizeWideVec(0), 0)
      // pr60960 f3: v4s8 G_UDIV — not a legal SIMD shape; scalarize residual.
      .scalarize(0)
      .minScalar(0, S32)
      .maxScalar(0, S64);

  // G_UMULH — unsigned multiply high.
  // s32: MULUUH (legal). s64: custom schoolbook. s8/s16/s24: custom widen.
  getActionDefinitionsBuilder(G_UMULH)
      .legalFor({S32})
      .customFor({S8, S16, LLT::scalar(24), S64})
      .scalarizeIf(ScalarizeWideVec(0), 0);

  // G_SMULH — signed multiply high.
  // s32: MULSSH (legal). s64: generic lower. s8/s16/s24: custom widen.
  getActionDefinitionsBuilder(G_SMULH)
      .legalFor({S32})
      .customFor({S8, S16, LLT::scalar(24)})
      .scalarizeIf(ScalarizeWideVec(0), 0)
      .lowerFor({S64});
  //===--------------------------------------------------------------------===
  // Bitwise Logic
  //===--------------------------------------------------------------------===
  // SIMD: G_AND, G_OR, G_XOR are legal for v2i32 and v4i16 (both 64-bit DR64
  // lanes): a bitwise on the packed lane IS the 64-bit DR64 bitwise op
  // (AND64/OR64/XOR64). The selector (HaydnInstructionSelector.cpp:1447-1508)
  // keys purely on DstTy.getSizeInBits==64 -> DR64RegClass, so v4i16 routes
  // to the same op as v2i32/s64. For s1/s8/s16 scalars, widen to s32 (Haydn
  // has no native sub-32-bit bitwise ops). Fixes the fft_cplx16x16_hifi3
  // "unable to legalize G_OR <4 x s16>" abort. V8I8 is deliberately
  // omitted here to match G_ADD/G_SUB/G_MUL/G_SHL, which list V4I16 but not
  // V8I8 (no proven v8i8 arithmetic path).
  getActionDefinitionsBuilder({G_AND, G_OR, G_XOR})
      .legalFor({S32, S64, V2I32, V4I16})
      .scalarizeIf(ScalarizeWideVec(0), 0)
      // Residual vectors (v2i16 etc.) before scalar widen/clamp.
      .scalarize(0)
      // pr79737-1: s72 bitfield RMW (`and` mask to clear field e). Widen to
      // next pow2 (s128) BEFORE clampScalar max=s64. clamp-first half-splits
      // s72 via G_EXTRACT s64+s8 and remerges as illegal 9×s8 G_MERGE_VALUES.
      // Order matches AArch64/RISC-V and the shift rule for pr79737-2.
      .widenScalarToNextPow2(0)
      .clampScalar(0, S32, S64);

  //===--------------------------------------------------------------------===
  // Shifts
  //===--------------------------------------------------------------------===
  // 32-bit and 64-bit shifts are legal (selector handles s64 decomposition).
  // SIMD: v2i32 shifts use X2SLL32/X2SRL32/X2SRA32.
  // SIMD: v4i16 shifts use X4SLL16/X4SRL16/X4SRA16.
  // Both use the full vector type for data and amount; the selector extracts
  // the scalar shift amount from element 0 and passes it as GPR32.
  getActionDefinitionsBuilder({G_SHL, G_LSHR, G_ASHR})
      .legalFor({{S32, S32}, {S64, S32}, {V2I32, V2I32}, {V4I16, V4I16}})
      .scalarizeIf(ScalarizeWideVec(0), 0)
      // pr60960: v4s8 (32-bit) is under the 64-bit wide-vector gate but not a
      // legal SIMD shift shape — scalarize residual vectors (not just >64b).
      .scalarize(0)
      .minScalar(0, S32)
      // pr79737-2: s72 bitfield shifts — widen to s128 before maxScalar can
      // half-split s72 into illegal s36 G_UNMERGE_VALUES.
      .widenScalarIf(
          [](const LegalityQuery &Query) {
            return Query.Types[0].isScalar() &&
                   Query.Types[0].getSizeInBits() > 64 &&
                   Query.Types[0].getSizeInBits() < 128;
          },
          changeTo(0, S128))
      .maxScalar(0, S64)
      .clampScalar(1, S32, S32)
      .widenScalarToNextPow2(0);

  //===--------------------------------------------------------------------===
  // Comparison & Select
  //===--------------------------------------------------------------------===
  // G_ICMP: s1 result is legal (IRTranslator default). Post-ISA-27, scalar
  // Haydn compares (SLT32/SLTU32/SEQ32) write a GPR32 holding 0/1 — NOT SFR or
  // flags. The selector (HaydnInstructionSelector.cpp:622-1003) materialises
  // that GPR bool and feeds it directly to G_BRCOND / G_SELECT's condition
  // operand (and hence MOVT32 $rs2 — the gpr-as-bool contract). Only the 64-bit
  // compare decomposition uses extra GPR AND/OR combines; no SFR is involved in
  // either path.
  //
  // Non-power-of-2 scalar widths (s61/s63 from bitfields/yarpgen) fall between
  // S32 and S64, so clampScalar alone does nothing and legalize fails.
  // Widen to the next power-of-2 first (AArch64/RISC-V pattern), then clamp.
  // LegalizerHelper::widenScalar(G_ICMP) chooses SEXT vs ZEXT from the
  // predicate, so signedness is preserved.
  getActionDefinitionsBuilder(G_ICMP)
      .legalFor({{S1, S32}, {S1, S64}, {S1, P0}, {S32, S32}, {S32, P0}})
      // CB-130: compare of wide vectors → scalarize element type (idx 1).
      .scalarizeIf(ScalarizeWideVec(1), 1)
 // Residual SLP vectors (v2i16/v4i8/…) after — not native SIMD.
      .scalarize(1)
      .widenScalarToNextPow2(1)
      .clampScalar(1, S32, S64);

  getActionDefinitionsBuilder(G_SELECT)
      .legalFor({{S32, S1}, {S64, S1}, {P0, S1}, {V2I32, S1}, {V4I16, S1}, {V8I8, S1}})
      // CB-130: vector-select of wide SIMD (v4s32 / v4s1 mask).
      .scalarizeIf(ScalarizeWideVec(0), 0)
      // Residual: v4i8 + vector-cond G_SELECT from SLP (ssad/usad torture).
      .scalarize(0)
      .clampScalar(0, S32, S64)
      .widenScalarToNextPow2(0);

  //===--------------------------------------------------------------------===
  // Control Flow
  //===--------------------------------------------------------------------===
  getActionDefinitionsBuilder(G_BRCOND).legalFor({S1});
  getActionDefinitionsBuilder(G_BR).alwaysLegal();

  //===--------------------------------------------------------------------===
  // PHI
  //===--------------------------------------------------------------------===
  getActionDefinitionsBuilder(G_PHI)
      .legalFor({S32, S64, P0, V2I32, V4I16, V8I8, V4I8, V2I16})
      .scalarizeIf(ScalarizeWideVec(0), 0)
 .scalarize(0) // residual e.g. odd vectors ( SLP)
      .clampScalar(0, S32, S64)
      .widenScalarToNextPow2(0);

  //===--------------------------------------------------------------------===
  // Type Conversions
  //===--------------------------------------------------------------------===
  // s1→s64 extensions: declared legal here; the selector decomposes them
  // into s1→s32 (AND32 with 1) then s32→s64 (MOV_GPR_TO_DR64 with R0).
  // G_SEXT s1→s64 uses ASR32 for sign-extension then MOV_GPR_TO_DR64.
  // s1→s64 extensions: declared legal here; the selector decomposes them
  // into s1→s32 (AND32 with 1) then s32→s64 (MOV_GPR_TO_DR64 with R0).
  // G_SEXT s1→s64 uses ASR32 for sign-extension then MOV_GPR_TO_DR64.
  // Extensions to a narrow result (s8/s16) are custom-lowered in
  // legalizeCustom (widen result to s32 via the legal s{1,8,16}->s32 path
  // then G_TRUNC to s8/s16). We cannot use clampScalar(0, S32, S64) here
  // because the generic LegalizerHelper::widenScalar for G_[SZ]EXT widens
  // the SOURCE register, not the destination — leaving the narrow result
  // type unchanged and "unable to legalize". Fixes s1->s16 (LC3
  // attack_detector_fx), s8->s16 (LC3 al_fec), and s1->s8 (LC3 ari_codec).
  // NOTE: {S8, S1} must NOT appear in legalFor even though the G_ZEXT
  // selector has a fast path for s1->s8. legalFor takes precedence over
  // customFor, so listing it Legal forces G_SEXT s1->s8 straight to the
  // selector — which has no (8,1) case for G_SEXT (only G_ZEXT) -> "cannot
  // select". Keeping it only in customFor routes all three ext ops' s1->s8
  // (and any narrow-result extension) through the widen-to-s32 + G_TRUNC path
  // in legalizeCustom, which is correct for SEXT/ZEXT/ANYEXT alike.
  // s128 destination extensions (s64 -> s128) are custom-lowered in
  // legalizeCustom to G_MERGE_VALUES <src>, <zero|sext-high>. This is the
  // feeding extension for the i128 multiply that IR instcombine
  // AggressiveInstCombine forms from a schoolbook 64x64->128 split at -O2
  // The generic narrowScalar of G_MUL s128 already decomposes the
  // 128-bit multiply itself into s64 G_UNMERGE_VALUES + G_MUL/G_UMULH +
  // G_MERGE_VALUES; only the feeding s64->s128 extension lacked a rule.
  // See /.
  // s32←sN zext/sext: custom → AND / SHL+LSHR / SHL+ASHR for selectImpl RI Pats.
  // s64←sN stays legal so post-legalizer redundant-ext combine can collapse
  // double zext/sext to s64 (isLegal gate); selector residual emits ANDI/MOV.
  // s64←s32 legal (MOV / SEXT_GPR32_TO_DR64).
  getActionDefinitionsBuilder({G_SEXT, G_ZEXT})
      .legalFor({{S64, S32}, {S64, S8}, {S64, S16}, {S64, S1}})
      .customFor({{S32, S1}, {S32, S8}, {S32, S16},
                  {S16, S1}, {S16, S8}, {S8, S1}, {S8, S8}, {S16, S16},
                  {S128, S64}})
      .customIf([](const LegalityQuery &Query) {
        const LLT DstTy = Query.Types[0];
        const LLT SrcTy = Query.Types[1];
        return DstTy.isScalar() && SrcTy.isScalar() &&
               DstTy.getSizeInBits() == 128 && SrcTy.getSizeInBits() < 128 &&
               SrcTy.getSizeInBits() != 64;
      })
      .scalarize(0)
      // Non-pow2 / residual →s32: rewrite via custom legalize.
      .customIf([](const LegalityQuery &Query) {
        const LLT DstTy = Query.Types[0];
        const LLT SrcTy = Query.Types[1];
        if (!DstTy.isScalar() || !SrcTy.isScalar())
          return false;
        const unsigned DstBits = DstTy.getSizeInBits();
        const unsigned SrcBits = SrcTy.getSizeInBits();
        if (DstBits != 32)
          return false;
        return SrcBits >= 1 && SrcBits < 32;
      })
      // Residual s64←sN (non-pow2 bitfields): keep legal for combiner/select.
      .legalIf([](const LegalityQuery &Query) {
        const LLT DstTy = Query.Types[0];
        const LLT SrcTy = Query.Types[1];
        if (!DstTy.isScalar() || !SrcTy.isScalar())
          return false;
        return DstTy.getSizeInBits() == 64 && SrcTy.getSizeInBits() >= 1 &&
               SrcTy.getSizeInBits() < 64;
      });

  // ANYEXT: bank identity (selector replaceRegWith / MOV); no mask rewrite.
  getActionDefinitionsBuilder(G_ANYEXT)
      .legalFor({{S32, S1}, {S32, S8}, {S32, S16}, {S64, S32},
                 {S64, S8}, {S64, S16}, {S64, S1}})
      .customFor({{S16, S1}, {S16, S8}, {S8, S1}, {S8, S8}, {S16, S16},
                  {S128, S64}})
      .customIf([](const LegalityQuery &Query) {
        const LLT DstTy = Query.Types[0];
        const LLT SrcTy = Query.Types[1];
        return DstTy.isScalar() && SrcTy.isScalar() &&
               DstTy.getSizeInBits() == 128 && SrcTy.getSizeInBits() < 128 &&
               SrcTy.getSizeInBits() != 64;
      })
      .scalarize(0)
      .legalIf([](const LegalityQuery &Query) {
        const LLT DstTy = Query.Types[0];
        const LLT SrcTy = Query.Types[1];
        if (!DstTy.isScalar() || !SrcTy.isScalar())
          return false;
        const unsigned DstBits = DstTy.getSizeInBits();
        const unsigned SrcBits = SrcTy.getSizeInBits();
        if (DstBits != 32 && DstBits != 64)
          return false;
        return SrcBits >= 1 && SrcBits < DstBits;
      });

  // G_TRUNC: scalar truncations are legal. <4 x s32> -> <4 x s16> is
  // custom-lowered to avoid needing a register class for 128-bit <4 x s32>.
  const LLT V4I32 = LLT::fixed_vector(4, 32);  // NOLINT
  // Trunc-to-s1 rule (all legal source widths): the result must be a clean
  // 0/1 GPR32 (selector ANDs with 1; s64 also MOVE32_DR_L first). Needed
  // for BOTH G_BRCOND (non-zero test) and MOVT32 (bit-0 test). A bare
  // replaceRegWith(Dst s1, Src) leaks high bits into BRCOND and (for s64)
  // the DR64 bank into GPR32 slots.
  //
  // {S1, S16} was missing → legalizer abort on `G_TRUNC s16→s1` (
  // yarpgen seed 2542). Distinct from (G_ICMP non-pow2): here both
  // types are pow2; the gap is simply an incomplete legalFor matrix for the
  // s1-result column. Closed lattice: s1 trunc legal from every scalar
  // width we otherwise allow as a GPR/DR source (s8/s16/s32/s64).
  //
  // residual (seed 2896): G_TRUNC to *non-pow2* result widths
  // (s24 from 23-bit signed bitfields, also s12/s31/…) is not in the
  // legalFor matrix and is not handled by generic widenScalar (G_TRUNC
  // type-0 widen is UnableToLegalize). Treat remaining scalar truncs as
  // legal bit-subset artifacts on GPR/DR (AArch64/AMDGPU alwaysLegal
  // pattern). Selector: s1 still ANDs bit0; s64 extracts MOVE32_DR_L;
  // other sub-register widths are replaceRegWith no-ops. Companion
  // ZEXT/SEXT legalIf above masks/sign-extends non-pow2 sources.
  getActionDefinitionsBuilder(G_TRUNC)
      // trunc→s1 stays legal: G_SELECT/G_BRCOND require s1 cond type.
      // Selector isolates bit0 via ANDI32 (cannot rewrite to s32 here).
      .legalFor({{S1, S32}, {S1, S16}, {S1, S8}, {S1, S64},
                 {S8, S16}, {S16, S32}, {S8, S32},
                 {S32, S64}, {S16, S64}, {S8, S64}})
      .customFor({{V4I16, V4I32}})
      .scalarize(0) // residual vector trunc e.g. v2i32→v2i16
      .legalIf([](const LegalityQuery &Query) {
        const LLT DstTy = Query.Types[0];
        const LLT SrcTy = Query.Types[1];
        return DstTy.isScalar() && SrcTy.isScalar() &&
               DstTy.getSizeInBits() < SrcTy.getSizeInBits();
      });

  getActionDefinitionsBuilder(G_BITCAST)
      .legalFor({{S32, S32}, {S64, S64},
                 {V2I32, V4I16}, {V4I16, V2I32},
                 {V2I32, S64}, {S64, V2I32},
                 {V4I16, S64}, {S64, V4I16},
                 {V2I32, V8I8}, {V8I8, V2I32},
                 {V4I16, V8I8}, {V8I8, V4I16},
                 {S64, V8I8}, {V8I8, S64},
                 // Residual SLP v4i8 pack lives in one s32 GPR.
                 {V4I8, S32}, {S32, V4I8},
                 {V2I16, S32}, {S32, V2I16}})
      // Wide / mismatched vectors (pr70903 <32 x s8>↔<4 x s64>): generic lower.
      .lower();

  //===--------------------------------------------------------------------===
  // Multi-value / Composite
  //===--------------------------------------------------------------------===
  // {narrow, wide} covers G_UNMERGE wide→narrow and G_MERGE narrow→wide
  // (and the swapped pair covers the opposite opcode). s64↔s128 needed for
  // i72 bitfield legalization (pr79737-2 anyext/load split).
  // Merge/unmerge split so clampMaxNumElements applies only to the vector
  // type index (UNMERGE src = type 1; MERGE dst = type 0). Combined rules
  // wrongly clamped scalar operands and asserted in fewerElementsVectorMerge.
  getActionDefinitionsBuilder(G_MERGE_VALUES)
      .legalFor({{S32, S64}, {S64, S32}, {S64, S128}, {S128, S64},
                 {S32, S16}, {S16, S32},
                 {S32, V2I32}, {S16, V4I16}, {S8, V8I8},
                 {S8, V4I8}, {S16, V2I16},
                 {S16, S64}, {S64, S16}})
      // pr79737-1: G_MERGE_VALUES 9×s8 → s72 bitfield pack.
      .customIf([](const LegalityQuery &Query) {
        return Query.Types[0].isScalar() &&
               !isPowerOf2_32(Query.Types[0].getSizeInBits());
      })
      .lower();

  getActionDefinitionsBuilder(G_UNMERGE_VALUES)
      .legalFor({{S32, S64}, {S64, S32}, {S64, S128}, {S128, S64},
                 {S32, S16}, {S16, S32},
                 {S32, V2I32}, {S16, V4I16}, {S8, V8I8},
                 {S8, V4I8}, {S16, V2I16},
                 {S16, S64}, {S64, S16}})
      // Wide residual SLP: fewer-elements only when unmerging *to scalars*
      // (type0 scalar). Intermediate unmerge-to-v2 pieces must not re-enter
      // clamp (NarrowTy==DstTy → UnableToLegalize hang/fail).
      .fewerElementsIf(
          [=](const LegalityQuery &Query) {
            return Query.Types[0].isScalar() && Query.Types[1].isFixedVector() &&
                   Query.Types[1].getElementType() == S32 &&
                   Query.Types[1].getNumElements() > 2;
          },
          [=](const LegalityQuery &Query) {
            (void)Query;
            return std::make_pair(1, V2I32);
          })
      .fewerElementsIf(
          [=](const LegalityQuery &Query) {
            return Query.Types[0].isScalar() && Query.Types[1].isFixedVector() &&
                   Query.Types[1].getElementType() == S16 &&
                   Query.Types[1].getNumElements() > 4;
          },
          [=](const LegalityQuery &Query) {
            (void)Query;
            return std::make_pair(1, V4I16);
          })
      .fewerElementsIf(
          [=](const LegalityQuery &Query) {
            return Query.Types[0].isScalar() && Query.Types[1].isFixedVector() &&
                   Query.Types[1].getElementType() == S8 &&
                   Query.Types[1].getNumElements() > 8;
          },
          [=](const LegalityQuery &Query) {
            (void)Query;
            return std::make_pair(1, V8I8);
          })
      // Intermediate fewer-elements artifact: unmerge v2N → N-element vectors
      // (e.g. v4s32 → 2×v2s32). Lower via bitcast to legal scalar unmerge.
      .customIf([](const LegalityQuery &Query) {
        return Query.Types[0].isVector() && Query.Types[1].isVector() &&
               Query.Types[1].getSizeInBits() > 64 &&
               Query.Types[1].getSizeInBits() % Query.Types[0].getSizeInBits() ==
                   0;
      })
      .lower();

  getActionDefinitionsBuilder({G_INSERT, G_EXTRACT})
      .legalFor({{S32, S32}, {S64, S64}})
      .immIdx(0);  // Immediate index 0 is the offset/position

  //===--------------------------------------------------------------------===
  // Memory Operations
  //===--------------------------------------------------------------------===
  // Peer: AIE2LegalizerInfo + RISCVLegalizerInfo (!enableUnalignedScalarMem).
  //
  // Only *naturally aligned* scalar mem ops are legal (AlignInBits == mem
  // width). BundleSim golden faults S_LW/S_LHW*/D_LDW on misaligned EA
  // so unaligned / non-pow2 (e.g. i24 bitfield RMW, align-1 i32) must lower
  // via LegalizerHelper::lowerLoad/Store, not fall through as "legal" s32.
  //
  // Extending/truncating rows (s32←s8/s16) cover lowerLoad high halves and
  // match AIE/RISCV ExtLoad MemDesc. s64 min-align stays 32 (ABI i64:32);
  // ISel still splits LD64 when MMO align < 8 (D_LDW needs 8). DR SIMD mem
  // is MemDesc-legal only at align 64 (see vector rows below).
  getActionDefinitionsBuilder({G_LOAD, G_STORE})
      .legalForTypesWithMemDesc({{S8, P0, S8, 8},
                                 {S16, P0, S16, 16},
                                 {S32, P0, S32, 32},
                                 {S64, P0, S64, 32},
                                 {P0, P0, P0, 32},
                                 // Anyext load / trunc store after splits.
                                 {S16, P0, S8, 8},
                                 {S32, P0, S8, 8},
                                 {S32, P0, S16, 16},
                                 // DR SIMD mem (v2i32/v4i16/v8i8): ABI is
                                 // v64:32 so natural IR align is 4. ISel splits
                                 // LD64→LD32×2 when MMO align < 8 (LD32 needs
                                 // align 4). Min AlignInBits=32 keeps align-4
                                 // vectors legal; align-2 residual SLP/LV
                                 // (coremark matrix_add_const, yarpgen seed1
                                 // store <4 x i16> into align-2 struct) must
                                 // scalarize — type-only legality used to
                                 // select ST32/LD32 and MEMORY_FAULT.
                                 {V2I32, P0, V2I32, 32},
                                 {V4I16, P0, V4I16, 32},
                                 {V8I8, P0, V8I8, 32}})
      // CB-130: <4 x s32> (128-bit) mem — scalarize to s32 loads/stores.
      .scalarizeIf(ScalarizeWideVec(0), 0)
      // pr60960: v4s8 stack spill/reload is 32-bit, under the wide-vector
      // gate, and not a legal SIMD mem shape — scalarize residual vectors.
      // Also underaligned (<4) v2i32/v4i16/v8i8 mem from SLP/LV — scalarize
      // to element ops that respect MMO align.
      .scalarize(0)
      .minScalar(0, S8)
      // CB-126 / pr79737-2: clamp extending load / trunc store results when
      // MMO type differs. lowerLoad of s72 emits s128 = G_LOAD/ZEXTLOAD of
      // s64 — must narrow to s64 (not s32; result must be ≥ mem width), then
      // anyext s64→s128. s64 anyext from s8/s16 still narrows to s32.
      .narrowScalarIf(
          [](const LegalityQuery &Query) {
            return Query.Types[0].isScalar() && !Query.MMODescrs.empty() &&
                   Query.Types[0] != Query.MMODescrs[0].MemoryTy &&
                   Query.Types[0].getSizeInBits() > 64;
          },
          changeTo(0, S64))
      .narrowScalarIf(
          [](const LegalityQuery &Query) {
            return Query.Types[0].isScalar() && !Query.MMODescrs.empty() &&
                   Query.Types[0] != Query.MMODescrs[0].MemoryTy &&
                   Query.Types[0].getSizeInBits() > 32 &&
                   Query.MMODescrs[0].MemoryTy.getSizeInBits() <= 32;
          },
          changeTo(0, S32))
      // CB-126 residual: G_STORE s32 value into s64 MMO (after non-pow2
      // split) — generic lower returns UnableToLegalize. Custom: match
      // value width to mem width and rewrite MMO.
      //
      // Mem width must be a whole byte (>= 8): sub-byte mem (`store i1`,
      // MemoryTy s1) would otherwise livelock — minScalar(0, S8) widens the
      // value to s8, custom truncs it back to s1 and rewrites the MMO to s1,
      // forever (unbounded vreg/MMO growth, no diagnostic). Sub-byte mem
      // belongs to lowerIfMemSizeNotByteSizePow2() below.
      .customIf(
          [](const LegalityQuery &Query) {
            return Query.Opcode == TargetOpcode::G_STORE &&
                   Query.Types[0].isScalar() && !Query.MMODescrs.empty() &&
                   Query.Types[0] != Query.MMODescrs[0].MemoryTy &&
                   Query.MMODescrs[0].MemoryTy.isScalar() &&
                   Query.MMODescrs[0].MemoryTy.getSizeInBits() >= 8 &&
                   isPowerOf2_32(Query.MMODescrs[0].MemoryTy.getSizeInBits());
          })
      // Non-pow2 mem (i40/i72 bitfields) MUST lower before widenScalar, or
      // widen turns G_STORE s72 into G_STORE s128 + G_ANYEXT s72→s128 (abort).
      .lowerIfMemSizeNotByteSizePow2()
      .widenScalarToNextPow2(0, /*Min=*/8)
      .lower();

  getActionDefinitionsBuilder(G_PTR_ADD)
      .legalFor({{P0, S32}});

  getActionDefinitionsBuilder(G_GLOBAL_VALUE).legalFor({P0});
  getActionDefinitionsBuilder(G_FRAME_INDEX).legalFor({P0});
  getActionDefinitionsBuilder(G_CONSTANT_POOL).legalFor({P0});

  //===--------------------------------------------------------------------===
  // Pointer Conversions
  //===--------------------------------------------------------------------===
  // G_INTTOPTR: s32 → p0; G_PTRTOINT: p0 → s32
  // For s64, the selector will decompose these operations.
  // clampScalar: pr17252 does `ptrtoint to i8` — widen result to s32 then
  // artifact-trunc (generic lower cannot PTRTOINT to s8).
  getActionDefinitionsBuilder({G_INTTOPTR, G_PTRTOINT})
      .legalFor({{P0, S32}, {S32, P0}, {P0, S64}, {S64, P0}})
      .clampScalar(0, S32, S64);

  //===--------------------------------------------------------------------===
  // Constants and Undef
  //===--------------------------------------------------------------------===
  // s32/s64/p0 constants are legal; selector handles materialization.
  // Types wider than 64 (s72 bitfield containers from pr79737-2) must NOT
  // hit maxScalar→s64: that narrows via insertParts into a G_MERGE of
  // leftovers, so later G_ANYEXT s72→s128 cannot constant-fold and fails.
  // Custom-split to s64 halves (merged s128 + trunc back) instead.
  // Only 65..128-bit custom: >128 (e.g. s640 from odd vector bitcasts) falls
  // through to clampScalar→s64 (APInt::zext(128) asserts if width > 128).
  getActionDefinitionsBuilder(G_CONSTANT)
      .legalFor({S1, S8, S16, S32, S64, P0})
      .customIf([](const LegalityQuery &Query) {
        if (!Query.Types[0].isScalar())
          return false;
        unsigned B = Query.Types[0].getSizeInBits();
        return B > 64 && B <= 128;
      })
      .clampScalar(0, S32, S64)
      .widenScalarToNextPow2(0);

  getActionDefinitionsBuilder(G_IMPLICIT_DEF)
      .legalFor({S32, S64, P0, V2I32, V4I16, V8I8, V4I8, V2I16})
      .scalarizeIf(ScalarizeWideVec(0), 0)
      .scalarize(0) // residual e.g. odd vectors
      .clampScalar(0, S32, S64)
      .widenScalarToNextPow2(0);

  //===--------------------------------------------------------------------===
  // Floating-point — soft-float (no FPU; values live as IEEE bits in GPR/DR)
  //===--------------------------------------------------------------------===
  // Closed contract: every advertised op either has a registered RTLIB impl
  // in HaydnSubtarget::initLibcallLoweringInfo, a generic .lower that does
  // not need a libcall, or .unsupported() (diagnostic, not ICE).
  //   * arith / libm / converts → .libcallFor (compiler-rt / libm)
  //   * fneg / fabs / copysign  → generic .lower (sign-bit integer tricks)
  //   * fconstant              → custom bitcast→G_CONSTANT (not constpool)
  //   * minimum/maximum*       → custom → fminnum/fmaxnum libcall
  //   * G_FMAD                 → generic .lower (fmul+fadd); no RTLIB
  //   * constrained/strict FP  → unsupported (no FP env)
  // Generic LegalizerHelper has no libcall cases for fneg/fabs/copysign;
  // .lower is the canonical soft-float path (do not reimplement in custom).
  getActionDefinitionsBuilder({
      G_FADD, G_FSUB, G_FMUL, G_FDIV, G_FREM,
      G_FMA, G_FSQRT,
      G_FCOS, G_FSIN, G_FEXP, G_FLOG, G_FLOG2, G_FLOG10,
      G_FPOWI, G_FPOW,
      G_FCEIL, G_FFLOOR, G_FRINT, G_FNEARBYINT,
      G_FMINNUM, G_FMAXNUM,
      G_INTRINSIC_TRUNC, G_INTRINSIC_ROUND, G_INTRINSIC_ROUNDEVEN,
  })
      // Soft-float scalars only. Vectors (e.g. v4f32) have no libcall form —
      // scalarize then libcall per lane. Leftover widths (f16/f128) fail
      // closed instead of advertising a nameless libcall.
      .libcallFor({S32, S64})
      .scalarizeIf(ScalarizeWideVec(0), 0)
      .scalarize(0)
      .unsupported();

  // G_FMAD has no RTLIB mapping (LegalizerHelper::libcall default-fails).
  // Generic lower rewrites to G_FMUL+G_FADD, which then libcall.
  getActionDefinitionsBuilder(G_FMAD)
      .lowerFor({S32, S64})
      .scalarize(0)
      .unsupported();

  // Constrained FP: LegalizerHelper::libcall has no STRICT cases, and Haydn
  // has no FP environment. Fail closed rather than ICE.
  getActionDefinitionsBuilder({
      G_STRICT_FADD, G_STRICT_FSUB, G_STRICT_FMUL, G_STRICT_FDIV,
      G_STRICT_FREM, G_STRICT_FSQRT, G_STRICT_FMA, G_STRICT_FLDEXP,
  }).unsupported();

  // FP ↔ int / FP size conversions are multi-type. Narrow integer results
  // (s16 = G_FPTOUI s64) must clamp to s32 before libcall; same for sitofp
  // sources. fptrunc/fpext: f32↔f64 are compiler-rt libcalls. f16 uses a
  // custom integer-bit ABI (Haydn CC has no half); fp128/same-size fail
  // closed. Do NOT clampScalar f16→s32 before the libcall check — that was
  // the s32←s32 G_FPEXT ICE (T-DSP10).
  getActionDefinitionsBuilder({G_FPTOSI, G_FPTOUI})
      .libcallForCartesianProduct({S32, S64}, {S32, S64})
      .clampScalar(0, S32, S64)
      .clampScalar(1, S32, S64)
      .scalarize(0);
  getActionDefinitionsBuilder({G_SITOFP, G_UITOFP})
      .libcallForCartesianProduct({S32, S64}, {S32, S64})
      .clampScalar(0, S32, S64)
      .clampScalar(1, S32, S64)
      .scalarize(0);
  getActionDefinitionsBuilder({G_FPTRUNC, G_FPEXT})
      .libcallFor({{S32, S64}, {S64, S32}})
      .customFor({{S32, S16}, {S16, S32}, {S64, S16}, {S16, S64}})
      .unsupportedIf([](const LegalityQuery &Query) {
        if (Query.Types.size() < 2)
          return true;
        // Same-size is not an extension/truncation.
        return Query.Types[0] == Query.Types[1];
      })
      .unsupported();

  // Sign-bit ops: use generic lower (XOR/AND/copysign graft). Same code as
  // RISCVLegalizerInfo softfloat — no target custom needed.
  getActionDefinitionsBuilder({G_FNEG, G_FABS})
      .lowerFor({S32, S64})
      .scalarize(0)
      .unsupported();
  getActionDefinitionsBuilder(G_FCOPYSIGN)
      .lowerFor({{S32, S32}, {S64, S64}})
      .scalarize(0)
      .unsupported();

  // llvm.minimum/maximum and *num: no reliable baremetal libcall (and generic
  // .lower of *num emits G_FCANONICALIZE we do not select). Map to
  // fminnum/fmaxnum libcalls (fminf/fmaxf). NaN-signaling is not tracked.
  getActionDefinitionsBuilder({G_FMINIMUM, G_FMAXIMUM,
                               G_FMINIMUMNUM, G_FMAXIMUMNUM})
      .customFor({S32, S64})
      .scalarize(0)
      .unsupported();

  // Bitcast IEEE bits → G_CONSTANT in the float-typed vreg. Generic .lower
  // would load from the constant pool; custom matches RISC-V softfloat.
  getActionDefinitionsBuilder(G_FCONSTANT)
      .customFor({S16, S32, S64})
      .unsupported();

  // G_FCMP: result is s1, so it cannot share the bulk {S32,S64} libcall rule.
  getActionDefinitionsBuilder(G_FCMP)
      .libcallFor({{S1, S32}, {S1, S64}})
      // Residual vector FCMP (complex-5 v2f32 SLP) — scalarize lanes first.
      .scalarize(1)
      .unsupported();

  //===--------------------------------------------------------------------===
  // Atomics — all lower (no native atomics)
  //===--------------------------------------------------------------------===
  // G_ATOMICRMW_* and G_ATOMIC_CMPXCHG have 2 type indices: [value, ptr]
  getActionDefinitionsBuilder({
      G_ATOMICRMW_XCHG, G_ATOMICRMW_ADD, G_ATOMICRMW_SUB,
      G_ATOMICRMW_AND, G_ATOMICRMW_OR, G_ATOMICRMW_XOR,
      G_ATOMICRMW_MAX, G_ATOMICRMW_MIN,
      G_ATOMICRMW_UMAX, G_ATOMICRMW_UMIN,
      G_ATOMIC_CMPXCHG,
      G_ATOMICRMW_NAND, G_ATOMICRMW_FADD, G_ATOMICRMW_FSUB,
      G_ATOMICRMW_FMAX, G_ATOMICRMW_FMIN,
      G_ATOMICRMW_FMAXIMUM, G_ATOMICRMW_FMINIMUM,
      G_ATOMICRMW_UINC_WRAP, G_ATOMICRMW_UDEC_WRAP,
      G_ATOMICRMW_USUB_COND, G_ATOMICRMW_USUB_SAT,
  }).lowerFor({{S32, P0}, {S64, P0}});

  // G_ATOMIC_CMPXCHG_WITH_SUCCESS has 3 type indices: [val, old, ptr]
  getActionDefinitionsBuilder(G_ATOMIC_CMPXCHG_WITH_SUCCESS)
      .lowerIf(all(typeInSet(0, {S32, S64}), typeIs(1, S1), typeIs(2, P0)));

  //===--------------------------------------------------------------------===
  // Varargs
  //===--------------------------------------------------------------------===
  // G_VASTART / G_VAARG: custom at the legalizer (RISCV legalizeVAStart
  // 812, AArch64 legalizeVaArg 2158). Do NOT leave them legal for a
  // post-RA ExpandPseudos VAARG_* / VASTART pseudo.
  getActionDefinitionsBuilder(G_VASTART).customFor({P0});

  // Two-bank varargs ABI: i64/f64 -> DR cursor __vr_top/__vr_offs; else
  // GPR cursor __gr_top/__gr_offs; per-bank overflow to __stack.
  // Product ABI advertises only scalar/pointer 32- and 64-bit va_arg forms.
  // Vectors, i128, and other widths must fail closed. Sub-32 integer
  // scalars widen to s32 (C default argument promotions).
  getActionDefinitionsBuilder(G_VAARG)
      .customFor({S32, S64, P0})
      .widenScalarIf(
          [](const LegalityQuery &Query) {
            const LLT Ty = Query.Types[0];
            return Ty.isScalar() && Ty.getSizeInBits() < 32;
          },
          changeTo(0, S32))
      .unsupportedIf([](const LegalityQuery &Query) {
        const LLT Ty = Query.Types[0];
        if (Ty.isVector())
          return true;
        if (Ty.isPointer())
          return false;
        if (!Ty.isScalar())
          return true;
        const unsigned Sz = Ty.getSizeInBits();
        return Sz != 32 && Sz != 64;
      })
      .unsupported();

  //===--------------------------------------------------------------------===
  // Extended load/store — peer AIE2LegalizerInfo / RISCV ExtLoadActions
  //===--------------------------------------------------------------------===
  getActionDefinitionsBuilder({G_SEXTLOAD, G_ZEXTLOAD})
      .legalForTypesWithMemDesc({{S32, P0, S8, 8},
                                 {S32, P0, S16, 16},
                                 {S16, P0, S8, 8}})
      // CB-126 residual: non-pow2 load lower emits G_ZEXTLOAD s32←s32 (same
      // size). Generic lowerLoad returns UnableToLegalize for that shape
      // (treats it as "aligned pow2 that needs unaligned split"). Rewrite
      // same-size extload → plain G_LOAD (pr52979/pr57344/pr58570).
      .customIf(
          [](const LegalityQuery &Query) {
            return !Query.MMODescrs.empty() && Query.Types[0].isScalar() &&
                   Query.Types[0] == Query.MMODescrs[0].MemoryTy;
          })
      // pr79737-2: lowerLoad(s72) emits s128 = G_ZEXTLOAD (s64) — narrow to
      // s64 then anyext (must not collapse to s32; result ≥ mem width).
      .narrowScalarIf(
          [](const LegalityQuery &Query) {
            return Query.Types[0].isScalar() && !Query.MMODescrs.empty() &&
                   Query.Types[0] != Query.MMODescrs[0].MemoryTy &&
                   Query.Types[0].getSizeInBits() > 64;
          },
          changeTo(0, S64))
      // CB-126: s64 = G_ZEXTLOAD/G_SEXTLOAD of s8/s16 — narrow result to s32
      // then anyext (same class as G_LOAD anyext).
      .narrowScalarIf(
          [](const LegalityQuery &Query) {
            return Query.Types[0].isScalar() && !Query.MMODescrs.empty() &&
                   Query.Types[0] != Query.MMODescrs[0].MemoryTy &&
                   Query.Types[0].getSizeInBits() > 32 &&
                   Query.MMODescrs[0].MemoryTy.getSizeInBits() <= 32;
          },
          changeTo(0, S32))
      .widenScalarToNextPow2(0, /*Min=*/8)
      .lowerIfMemSizeNotByteSizePow2()
      .lower();

  getActionDefinitionsBuilder({G_INDEXED_LOAD, G_INDEXED_SEXTLOAD,
                               G_INDEXED_ZEXTLOAD, G_INDEXED_STORE})
      .lowerFor({{S32, P0}});

  //===--------------------------------------------------------------------===
  // Intrinsics — always legal (GISel handles them)
  //===--------------------------------------------------------------------===
  getActionDefinitionsBuilder({G_INTRINSIC, G_INTRINSIC_W_SIDE_EFFECTS,
                               G_INTRINSIC_CONVERGENT,
                               G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS})
      .alwaysLegal();

  //===--------------------------------------------------------------------===
  // Misc scalar ops — lower with type coverage
  //===--------------------------------------------------------------------===
  // 1 type idx, 0 imm idx
  // G_FSHL/G_FSHR have all operands + result at the same type idx, but the
  // generic LegalizerHelper::widenScalar for funnel-shift only handles
  // pow-of-2 widening where the helper can rebuild the op. Listing s16
  // explicitly in lowerFor lets the selector take it directly; the generic
  // selector materializes it via shifts/or after the s16 is promoted to s32
  // (no native funnel-shift instruction).
  // Bitcount ops: lower via the generic lowerBitCount, which ASSUMES
  // DstTy == SrcTy. The previous `.minScalar(0, S16)` widened ONLY the result
  // (the legalizer's widenScalarDst runs first, then seeing TypeIdx-0 legal it
  // falls through to Lower before ever widening the source) -> lowerBitCount
  // built s8 intermediates under an s16 destination -> `validateShiftOp`
  // assertionx) crashed). Fix: lower
  // s8 DIRECTLY with no widening so DstTy == SrcTy == s8; lowerBitCount then
  // builds s8 intermediates that Haydn's generic s8->s32 widening legalizes.
  getActionDefinitionsBuilder({
      G_CTLZ, G_CTTZ, G_CTPOP,
      G_CTLZ_ZERO_UNDEF, G_CTTZ_ZERO_UNDEF,
  })
      .lowerFor({S8, S16, S32, S64})
      .maxScalar(0, S64);

  // G_ABS — native ABS32 (GPR) / ABS64 (DR64). Non-saturating matches
  // llvm.abs (INT_MIN stays INT_MIN). ABS32S/ABS64S are sat-only intrinsics.
  // Pats in HaydnGISel.td; selectImpl owns selection (no C++ residual).
  // Residual SLP vectors (cb44 v2i32 llvm.abs): .lower() expands via
  // lowerAbsToAddXor to vector ASHR/ADD/XOR (legal for v2i32). Avoid bare
  // .scalarize(0) on G_ABS — fewerElements→G_BUILD_VECTOR rebuild can assert
  // in fewerElementsVectorMerge when NarrowTy is scalar.
  getActionDefinitionsBuilder(G_ABS)
      .legalFor({S32, S64})
      .minScalar(0, S32)
      .maxScalar(0, S64)
      .lower();

  // G_FSHL/G_FSHR: lower s8 directly (pr56866). minScalar(0,S16) alone is not
  // enough — funnel-shift widenScalar only rebuilds pow2 shapes reliably, and
  // leaving s8 unlisted aborts with "unable to legalize G_FSHL s8". Mirror
  // the bitcount path: lowerFor includes S8 so DstTy==SrcTy for lower.
  getActionDefinitionsBuilder({G_FSHL, G_FSHR})
      .lowerFor({S8, S16, S32, S64})
      // Residual vector funnel shifts (pr56866 v4i8) — scalarize then lower.
      .scalarize(0)
      .maxScalar(0, S64);

  // G_SMULO / G_UMULO: lower to G_MUL + G_SMULH/G_UMULH + overflow icmp.
  // Clang emits non-pow2 widths for mixed-sign __builtin_*_mul_overflow
  // (e.g. gcc-torture pr89434 at -O0: llvm.smul.with.overflow.i33 for an
  // unsigned i32 store of a signed multiply). lowerFor({S16,S32,S64}) alone
  // left s33 with no action → "unable to legalize G_SMULO". Mirror AArch64:
  // widen odd widths to the next power of two (min s32), clamp to {s32,s64},
  // then lower. s32 SMULH is native (MULSSH/MULUUH); s64 SMULH/UMULH already
  // have legalizer paths (lowerFor / custom schoolbook).
  getActionDefinitionsBuilder({G_SMULO, G_UMULO})
      .widenScalarToNextPow2(0, /*Min=*/32)
      .clampScalar(0, S32, S64)
      .lower();

  getActionDefinitionsBuilder({
      G_UADDO, G_USUBO,
      G_SADDO, G_SSUBO, G_UADDE, G_USUBE, G_SADDE, G_SSUBE,
      G_UADDSAT, G_SADDSAT, G_USUBSAT, G_SSUBSAT,
      G_USHLSAT, G_SSHLSAT,
      G_ROTL, G_ROTR,
      G_SBFX, G_UBFX,
      G_FPTOSI_SAT, G_FPTOUI_SAT,
      G_CONSTANT_FOLD_BARRIER,
      G_LROUND, G_LLROUND,
      G_FFREXP, G_FLDEXP,
  }).lowerFor({S16, S32, S64})
    .minScalar(0, S16)
    .maxScalar(0, S64);

  // G_BSWAP / G_BITREVERSE on s64 are custom-lowered in legalizeCustom.
  // The generic LegalizerHelper::lowerBswap computes its byte masks as
  // `0xFF << (i*8)` in SIGNED int arithmetic (LegalizerHelper.cpp:9716); for
  // i=3 this is signed overflow -> -16777216, which APInt(64,...) sign
  // extends to 0xFFFFFFFF_FF000000 instead of the correct 0x00000000_FF000000.
  // The wrong mask drops the innermost byte pair (bytes 3,4) of the bswap, so
  // 64-bit bitreverse (which is bswap + intra-byte swaps) miscompiles whenever
  // those bytes differ between the halves -- (6-stage SWAR bitreverse).
  // Custom lowering below rebuilds the masks as uint64_t. Per constraint #0
  // (never modify upstream) this lives here, not in LegalizerHelper.cpp.
  getActionDefinitionsBuilder({G_BSWAP, G_BITREVERSE})
      .lowerFor({S16, S32})
      .customFor({S64})
      // Residual SLP vectors (v2i16 G_BSWAP in pr52760) — scalarize then lower.
      .scalarize(0)
      .minScalar(0, S16)
      .maxScalar(0, S64);

  // G_SMIN/G_SMAX — native MAX32/MIN32 (s32) and MAX64/MIN64 (s64, signed).
  // ISA MAX64/MIN64 are signed int64 compares; Pats in HaydnGISel.td.
  getActionDefinitionsBuilder({G_SMIN, G_SMAX})
      .legalFor({S32, S64})
      // Residual SLP vectors (v4i8 etc.) — scalarize before min/max scalar.
      .scalarize(0)
      .minScalar(0, S32)
      .maxScalar(0, S64);

  // G_UMIN/G_UMAX — native MAXU32/MINU32 for s32 only. No MAXU64/MINU64 in
  // ISA; s64 lowers to select/icmp expansion (hi/lo decomposition).
  getActionDefinitionsBuilder({G_UMIN, G_UMAX})
      .legalFor({S32})
      .lowerFor({S64})
      // Residual SLP vectors (v4i8 G_UMAX in pr69691) — scalarize then lower.
      .scalarize(0)
      .minScalar(0, S32)
      .maxScalar(0, S64);

  // G_FREEZE is a no-op — always legal for any type.
  getActionDefinitionsBuilder(G_FREEZE).alwaysLegal();

  // 1 type idx, 1 imm idx (scale/width)
  getActionDefinitionsBuilder({
      G_SEXT_INREG,
      G_SMULFIX, G_SMULFIXSAT, G_UMULFIX, G_UMULFIXSAT,
      G_SDIVFIX, G_UDIVFIX, G_SDIVFIXSAT, G_UDIVFIXSAT,
  }).lowerFor({S32, S64}).immIdx(0);

  // 2 type idx ops
  getActionDefinitionsBuilder({G_SCMP, G_UCMP})
      .lowerFor({{S32, S32}, {S64, S64}});

  getActionDefinitionsBuilder(G_PTRMASK).alwaysLegal();

  getActionDefinitionsBuilder(G_ADDRSPACE_CAST)
      .legalFor({{P0, P0}});

  // Memory intrinsics — use libcalls for baremetal MVB
  // Haydn has no native memcpy/memmove/memset/bzero instructions
  // G_MEMCPY/G_MEMMOVE: (dst:p0, src:p0, len:s32, isvolatile:imm)
  // G_MEMSET: (dst:p0, val:s8, len:s32, isvolatile:imm)
  // G_BZERO: (dst:p0, len:s32, isvolatile:imm)
  // Use.libcall without arguments to handle all type combinations
  getActionDefinitionsBuilder({G_MEMCPY, G_MEMMOVE, G_MEMSET}).libcall();

  getActionDefinitionsBuilder(G_MEMCPY_INLINE).lower();

  getActionDefinitionsBuilder(G_BZERO).libcall();

  // Traps / fences have no type indices — legalFor({S32,P0}) asserts in
  // Legalizer (ArrayRef index OOB). Same class as the old G_TRAP bug.
  // Match AArch64/AMDGPU: alwaysLegal; selector emits RET (traps) or
  // MEMBARRIER (fence — compiler barrier; Haydn is single-core baremetal).
  getActionDefinitionsBuilder(
      {G_TRAP, G_DEBUGTRAP, G_UBSANTRAP, G_FENCE, G_INVOKE_REGION_START})
      .alwaysLegal();

  // G_PREFETCH has mixed typed/imm operands — legalFor({S32,P0}) mis-indexes
  // types and can assert in getAction (getReg on imm). Select as nop (erase).
  getActionDefinitionsBuilder(G_PREFETCH).alwaysLegal();

  // Control/misc with type idx (pointers / i32)
  getActionDefinitionsBuilder({
      G_BLOCK_ADDR, G_JUMP_TABLE, G_BRINDIRECT, G_BRJT,
      G_READ_REGISTER, G_WRITE_REGISTER,
      G_READCYCLECOUNTER, G_READSTEADYCOUNTER,
      G_STACKSAVE, G_STACKRESTORE,
  }).legalFor({S32, P0});

  // T-ABI2: G_DYN_STACKALLOC is custom-lowered (RISCV
  // RISCVLegalizerInfo.cpp:512-513 .lower() + LegalizerHelper
  // getDynStackAllocTargetPtr). Size is rounded to StackAlign(8) and the
  // new SP is masked with -8 so alloca(n) with n%8≠0 cannot leave
  // SP≡4 mod 8 (HaydnCallingConv MEMORY_FAULT). Align > 8 is fail-closed
  // (no BP / no ANDI32 negative mask). Do not leave this legal for the
  // selector: that path discarded the align operand and did not re-round.
  getActionDefinitionsBuilder(G_DYN_STACKALLOC)
      .customFor({{P0, S32}})
      .clampScalar(1, S32, S32)
      .unsupported();

  // FP misc. trunc/round/roundeven libcall with the arith group above.
  // lrint/llrint generic-lower to frint + fptosi (frint is a libcall).
  // fptrunc_round has no Haydn/RTLIB story.
  getActionDefinitionsBuilder({
      G_INTRINSIC_LRINT, G_INTRINSIC_LLRINT,
  }).lowerFor({S32, S64}).unsupported();
  getActionDefinitionsBuilder(G_INTRINSIC_FPTRUNC_ROUND).unsupported();

  // Soft-float: no FP class hardware. Was .legalFor but nothing selects
  // G_IS_FPCLASS → "cannot select" (divsc3/mulsc3 via crt_isnan/crt_isinf).
  // Generic lower turns it into integer bit tests on the IEEE bit-pattern
  // (same as AArch64 .lower() / RISCV softfloat .lowerFor).
  // Bare .lower() on leftover widths hits getFltSemanticForLLT
  // llvm_unreachable (s80/non-IEEE) — that was the unfiled crash that
  // disabled 31 libm entrypoints. Only s32/s64 are product IEEE; else
  // diagnostic.
  getActionDefinitionsBuilder(G_IS_FPCLASS)
      .lowerFor({{S1, S32}, {S1, S64}})
      .unsupported();

  // FP environment
  getActionDefinitionsBuilder({
      G_GET_FPENV, G_SET_FPENV, G_RESET_FPENV,
      G_GET_FPMODE, G_SET_FPMODE, G_RESET_FPMODE,
      G_GET_ROUNDING, G_SET_ROUNDING,
  }).lowerFor({S32});

  // Trunc with saturation (2 type idx)
  getActionDefinitionsBuilder({
      G_TRUNC_SSAT_S, G_TRUNC_SSAT_U, G_TRUNC_USAT_U,
  }).lowerFor({{S16, S32}, {S8, S32}, {S16, S64}, {S8, S64}});

  //===--------------------------------------------------------------------===
  // SIMD Vector ops — legal for supported types
  //===--------------------------------------------------------------------===
  // SIMD types: v2i32, v4i16, v8i8 (all 64-bit, fit in DR64)
  // These are mapped to DR64 register bank and use SIMD instructions
  // (V2I32, V4I16, V8I8 already defined above)
  //
  // Note: SIMD arithmetic and bitwise ops are already legalized above
  // combined with scalar types to avoid duplicate alias issues.

  // G_BUILD_VECTOR: only v2i32 is natively legal (selects to MOV_GPR_TO_DR64
  // REG_SEQUENCE-style merge). Multi-element packs (v4i16, v8i8) are custom
  // lowered to scalar pack + legal v2i32 build + bitcast — AIE-shaped
  // (legalize first; ISel stays a thin 2-way merge). Leaving v4i16 "legal"
  // forced the selector to emit half-selected G_* or ad-hoc SLL/OR sequences.
  getActionDefinitionsBuilder(G_BUILD_VECTOR)
      .legalFor({{V2I32, S32}})
      .customFor({{V4I16, S16}, {V8I8, S8}, {V4I8, S8}, {V2I16, S16}})
      // Residual SLP s1 builds (G_ICMP vector scalarize rebuilds v2s1 from
      // scalar s1 lanes; cb44 abs+select SLP). Do NOT clampMaxNumElements(S1,1)
      // — fewerElementsVectorMerge asserts when NarrowTy is scalar. Custom
      // rewrites remaining G_UNMERGE uses to the element regs and drops the
      // build (artifact-combine may not run before legalize re-visits).
      .customIf([](const LegalityQuery &Q) {
        return Q.Types[0].isVector() &&
               Q.Types[0].getElementType().getSizeInBits() == 1;
      })
      // Residual SLP builds (v4s32/v16s32/…): fewer-elements down to a
      // legal native shape. Bare .lower() is UnableToLegalize for BUILD_VECTOR
      // and the artifact-retry loop hangs the legalizer (pr28982a @ -O2).
      .clampMaxNumElements(0, S32, 2)
      .clampMaxNumElements(0, S16, 4)
      .clampMaxNumElements(0, S8, 8)
      .lower();

  //===--------------------------------------------------------------------===
  // SIMD Vector reductions — custom for supported types
  //===--------------------------------------------------------------------===
  // G_VECREDUCE_ADD on v2i32/v4i16 is expanded in legalizeCustom to scalar
  // extract+add, but the selector can match these patterns to X2HADD32_L
  // X4HADD16_L for optimal codegen.
  getActionDefinitionsBuilder(G_VECREDUCE_ADD)
      .customFor({{S32, V2I32}, {S16, V4I16}})
      .lowerFor({S32, S64});

  // G_EXTRACT_VECTOR_ELT: custom for v2i32 and v4i16.
  // Expanded in legalizeCustom to G_UNMERGE_VALUES + optional shift.
  getActionDefinitionsBuilder(G_EXTRACT_VECTOR_ELT)
      .customFor({{S32, V2I32}, {S16, V4I16}})
      // Residual SLP: fewer-elements on the source vector first so generic
      // lower does not unmerge a v16 and re-create extracts (legalizer hang).
      .clampMaxNumElements(1, S32, 2)
      .clampMaxNumElements(1, S16, 4)
      .clampMaxNumElements(1, S8, 8)
      .clampMaxNumElements(1, S1, 1)
      .lower();

  // G_INSERT_VECTOR_ELT: custom for v2i32 and v4i16.
  // Expanded in legalizeCustom to G_UNMERGE_VALUES + shift/mask/merge.
  getActionDefinitionsBuilder(G_INSERT_VECTOR_ELT)
      .customFor({{V2I32, S32}, {V4I16, S16}})
      .clampMaxNumElements(0, S32, 2)
      .clampMaxNumElements(0, S16, 4)
      .clampMaxNumElements(0, S8, 8)
      .clampMaxNumElements(0, S1, 1)
      .lower();

  //===--------------------------------------------------------------------===
  // G_SHUFFLE_VECTOR — custom for v2i32 (scalarize via extract+build)
  //===--------------------------------------------------------------------===
  // The LLVM "expand-reductions" pass converts llvm.vector.reduce.add.v2i32
  // into shufflevector + add + extractelement at the IR level, before GISel.
  // We never see G_VECREDUCE_ADD; we see G_SHUFFLE_VECTOR instead. Custom
  // scalarize v2i32 shuffles by extracting elements and rebuilding the vector.
  getActionDefinitionsBuilder(G_SHUFFLE_VECTOR)
      // All shuffles go through custom: 64-bit v2i32 keeps bitcast-unmerge;
      // residual SLP (v16s1 from v4s1, etc.) extract+build. Bare .lower()
      // thrash-hangs on wide masks (pr28982a).
      .customIf([](const LegalityQuery &Query) {
        return Query.Types[0].isVector();
      })
      .lower();

  //===--------------------------------------------------------------------===
  // Vector ops — unsupported (non-SIMD vector types)
  //===--------------------------------------------------------------------===
  // G_CONCAT_VECTORS: residual SLP fewer-elements produces concat of native
  // 64-bit pieces (e.g. 8×v2s32 → v16s32). Clamp dest to a native 64-bit
  // shape so the wide concat is split; bare .lowerFor({S32,S64}) is
  // UnableToLegalize and artifact-retries forever (pr28982a hang).
  getActionDefinitionsBuilder(G_CONCAT_VECTORS)
      .clampMaxNumElements(0, S32, 2)
      .clampMaxNumElements(0, S16, 4)
      .clampMaxNumElements(0, S8, 8)
      .clampMaxNumElements(0, S1, 1)
      .lower();

  getActionDefinitionsBuilder({
      G_BUILD_VECTOR_TRUNC,
      G_INSERT_SUBVECTOR, G_EXTRACT_SUBVECTOR,
      G_SPLAT_VECTOR, G_STEP_VECTOR, G_VSCALE,
      G_VECREDUCE_MUL,
      G_VECREDUCE_AND, G_VECREDUCE_OR, G_VECREDUCE_XOR,
      G_VECREDUCE_SMAX, G_VECREDUCE_SMIN,
      G_VECREDUCE_UMAX, G_VECREDUCE_UMIN,
      G_VECREDUCE_FADD, G_VECREDUCE_FMUL,
      G_VECREDUCE_FMAX, G_VECREDUCE_FMIN,
      G_VECREDUCE_FMAXIMUM, G_VECREDUCE_FMINIMUM,
      G_VECREDUCE_SEQ_FADD, G_VECREDUCE_SEQ_FMUL,
      G_VECTOR_COMPRESS,
  }).lowerFor({S32, S64});

  getActionDefinitionsBuilder(G_PTRAUTH_GLOBAL_VALUE).unsupported();
}

// Lower a true 64x64->64 multiply (G_MUL <s64> with at least one non-widened
// operand) to a schoolbook of generic G_MUL <s64> partials. See the G_MUL
// block in legalizeCustom for the derivation.
// Decomposition (result mod 2^64):
// aLo, aHi = unmerge a; bLo, bHi = unmerge b (each half s32)
// LL = (zext aLo) * (zext bLo) -- low x low, full 64-bit UNSIGNED product
// LH = (zext aLo) * (zext bHi) -- low x high
// HL = (zext aHi) * (zext bLo) -- high x low
// result = LL + ((LH + HL) << 32)
// (aHi*bHi contributes only to bits >= 64, dropped by the mod-2^64 result.)
// The three inner G_MUL <s64> ops each have two zext-of-s32 operands, so when
// the legalizer re-visits them they take the widening-multiply branch and
// become G_HAYDN_MUL64_WIDENU. The cross and LL partials MUST be unsigned
// because G_MUL <s64> is signless at the IR level -- the low 64 bits of the
// product are the same whether the operands are interpreted as signed or
// unsigned, but only the UNSIGNED 32x32->64 widening produces the correct
// high-32 bits of each partial (a SIGNED widening sign-extends the wrong
// half when bit 31 of a partial operand is set). The previous sext +
// signed-widening path corrupted the high 32 bits of every round's products
// (MurmurHash3 finalizer on uint64_t). The zext/unmerge/shl/add ops are
// already handled by the Haydn selector.
static void lowerMul64Schoolbook(MachineIRBuilder &MIB, MachineRegisterInfo &MRI,
                                 MachineInstr &MI) {
  using namespace TargetOpcode;
  const LLT S32 = LLT::scalar(32);
  const LLT S64 = LLT::scalar(64);

  Register DstReg = MI.getOperand(0).getReg();
  Register Src0 = MI.getOperand(1).getReg();
  Register Src1 = MI.getOperand(2).getReg();

  // Split each 64-bit operand into low/high 32-bit halves.
  Register ALo = MRI.createGenericVirtualRegister(S32);
  Register AHi = MRI.createGenericVirtualRegister(S32);
  MIB.buildUnmerge({ALo, AHi}, Src0);

  Register BLo = MRI.createGenericVirtualRegister(S32);
  Register BHi = MRI.createGenericVirtualRegister(S32);
  MIB.buildUnmerge({BLo, BHi}, Src1);

  // Zero-extend each half to 64 bits so the inner G_MUL <s64> ops hit the
  // widening-multiply branch (both zext-of-s32) and become
  // G_HAYDN_MUL64_WIDENU. Zero extension is REQUIRED here, not sign
  // extension: G_MUL <s64> is signless, and only the unsigned 32x32->64
  // widening produces the correct high 32 bits of each partial product. With
  // sign extension the previous path used signed widening, which
  // sign-extends each operand's wrong half and corrupts any product whose
  // partials have bit 31 set in their low-32 operands.
  auto ZextTo64 = [&](Register R) {
    Register Ext = MRI.createGenericVirtualRegister(S64);
    MIB.buildZExt(Ext, R);
    return Ext;
  };
  Register ALo64 = ZextTo64(ALo);
  Register AHi64 = ZextTo64(AHi);
  Register BLo64 = ZextTo64(BLo);
  Register BHi64 = ZextTo64(BHi);

  // Three 32x32->64 widening partial products. Each becomes
  // G_HAYDN_MUL64_WIDENU.
  Register LL = MRI.createGenericVirtualRegister(S64);
  MIB.buildMul(LL, ALo64, BLo64);
  Register LH = MRI.createGenericVirtualRegister(S64);
  MIB.buildMul(LH, ALo64, BHi64);
  Register HL = MRI.createGenericVirtualRegister(S64);
  MIB.buildMul(HL, AHi64, BLo64);

  // Sum the cross partials and shift into the high-half position, then add
  // the low product. result = LL + ((LH + HL) << 32).
  Register Cross = MRI.createGenericVirtualRegister(S64);
  MIB.buildAdd(Cross, LH, HL);

  Register ShiftAmt = MRI.createGenericVirtualRegister(S32);
  MIB.buildConstant(ShiftAmt, 32);
  Register CrossSh = MRI.createGenericVirtualRegister(S64);
  MIB.buildShl(CrossSh, Cross, ShiftAmt);

  MIB.buildAdd(DstReg, LL, CrossSh);
  MI.eraseFromParent();
}

/// IEEE half ↔ f32/f64 via compiler-rt. Haydn CC has no f16; compiler-rt
/// without COMPILER_RT_HAS_FLOAT16 uses uint16_t, which Haydn passes/returns
/// in a GPR32. Form the libcall with i32/f32/f64 types so CC_Haydn can assign
/// them. Do not invent rounding: the named helpers are the golden symbols.
static bool legalizeHalfConvertLibcall(LegalizerHelper &Helper, MachineInstr &MI,
                                       LostDebugLocObserver &LocObserver) {
  using namespace TargetOpcode;
  MachineIRBuilder &MIB = Helper.MIRBuilder;
  MachineRegisterInfo &MRI = *MIB.getMRI();
  MIB.setInstrAndDebugLoc(MI);

  const Register Dst = MI.getOperand(0).getReg();
  const Register Src = MI.getOperand(1).getReg();
  const unsigned DstBits = MRI.getType(Dst).getSizeInBits();
  const unsigned SrcBits = MRI.getType(Src).getSizeInBits();
  LLVMContext &Ctx = MI.getMF()->getFunction().getContext();
  const LLT S32 = LLT::scalar(32);

  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  Type *ArgTy = nullptr;
  Type *RetTy = nullptr;
  Register ArgReg = Src;
  Register RetReg = Dst;
  bool TruncRetToHalf = false;

  if (MI.getOpcode() == G_FPEXT && SrcBits == 16) {
    // uint16_t ABI: zero-extend the half bits into GPR32 (high 16 = 0).
    ArgReg = MIB.buildZExt(S32, Src).getReg(0);
    ArgTy = Type::getInt32Ty(Ctx);
    if (DstBits == 32) {
      LC = RTLIB::FPEXT_F16_F32;
      RetTy = Type::getFloatTy(Ctx);
    } else if (DstBits == 64) {
      LC = RTLIB::FPEXT_F16_F64;
      RetTy = Type::getDoubleTy(Ctx);
    }
  } else if (MI.getOpcode() == G_FPTRUNC && DstBits == 16) {
    TruncRetToHalf = true;
    RetReg = MRI.createGenericVirtualRegister(S32);
    RetTy = Type::getInt32Ty(Ctx);
    if (SrcBits == 32) {
      LC = RTLIB::FPROUND_F32_F16;
      ArgTy = Type::getFloatTy(Ctx);
    } else if (SrcBits == 64) {
      LC = RTLIB::FPROUND_F64_F16;
      ArgTy = Type::getDoubleTy(Ctx);
    }
  }

  if (LC == RTLIB::UNKNOWN_LIBCALL || !ArgTy || !RetTy)
    return false;

  const LegalizerHelper::LegalizeResult Status =
      createLibcall(MIB, LC, {RetReg, RetTy, 0}, {{ArgReg, ArgTy, 0}},
                    LocObserver, &MI);
  if (Status != LegalizerHelper::Legalized)
    return false;
  if (TruncRetToHalf)
    MIB.buildTrunc(Dst, RetReg);
  MI.eraseFromParent();
  return true;
}

namespace {

// Structured va_list is 5×i32: __stack, __gr_top, __vr_top, __gr_offs,
// __vr_offs. Offsets must match CallLowering saveVarArgRegisters.
constexpr int kVaStackOff = 0;
constexpr int kVaGrTopOff = 4;
constexpr int kVaVrTopOff = 8;
constexpr int kVaGrOffsOff = 12;
constexpr int kVaVrOffsOff = 16;
constexpr int64_t kVaStackStep = 8;

Register vaListFieldPtr(MachineIRBuilder &MIB, Register List, int ByteOff,
                        LLT PtrTy, LLT S32) {
  if (ByteOff == 0)
    return List;
  auto Off = MIB.buildConstant(S32, ByteOff);
  return MIB.buildPtrAdd(PtrTy, List, Off).getReg(0);
}

MachineMemOperand *vaListMMO(MachineFunction &MF, MachineMemOperand::Flags F,
                             LLT MemTy, Align A, int ByteOff) {
  return MF.getMachineMemOperand(MachinePointerInfo().getWithOffset(ByteOff), F,
                                 MemTy, A);
}

} // namespace

bool HaydnLegalizerInfo::legalizeIntrinsic(LegalizerHelper &Helper,
                                           MachineInstr &MI) const {
  MachineIRBuilder &MIB = Helper.MIRBuilder;
  switch (cast<GIntrinsic>(MI).getIntrinsicID()) {
  default:
    return true;
  case Intrinsic::vaend:
    MI.eraseFromParent();
    return true;
  case Intrinsic::vacopy: {
    // AArch64LegalizerInfo.cpp:1706 copies the va_list object; Haydn's list
    // is 5×i32 (getVaListSizeInBits). Copy word-by-word so each G_LOAD/G_STORE
    // is already legal (s160 is not).
    MachineFunction &MF = *MI.getMF();
    const LLT S32 = LLT::scalar(32);
    const LLT PtrTy = LLT::pointer(0, 32);
    Register DstLst = MI.getOperand(1).getReg();
    Register SrcLst = MI.getOperand(2).getReg();
    MIB.setInstrAndDebugLoc(MI);
    for (unsigned W = 0; W < 5; ++W) {
      const int ByteOff = static_cast<int>(W * 4);
      Register SrcAddr = vaListFieldPtr(MIB, SrcLst, ByteOff, PtrTy, S32);
      Register DstAddr = vaListFieldPtr(MIB, DstLst, ByteOff, PtrTy, S32);
      auto Tmp = MIB.buildLoad(
          S32, SrcAddr,
          *vaListMMO(MF, MachineMemOperand::MOLoad, S32, Align(4), ByteOff));
      MIB.buildStore(
          Tmp, DstAddr,
          *vaListMMO(MF, MachineMemOperand::MOStore, S32, Align(4), ByteOff));
    }
    MI.eraseFromParent();
    return true;
  }
  }
}

bool HaydnLegalizerInfo::legalizeVAStart(LegalizerHelper &Helper,
                                         MachineInstr &MI) const {
  // RISCVLegalizerInfo.cpp:812 — store save-area addresses into va_list.
  assert(MI.getOpcode() == TargetOpcode::G_VASTART);
  MachineIRBuilder &MIB = Helper.MIRBuilder;
  MachineFunction &MF = *MI.getMF();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  MIB.setInstrAndDebugLoc(MI);
  if (!FuncInfo->hasVarArgsSaveAreas()) {
    MI.eraseFromParent();
    return true;
  }

  const LLT PtrTy = LLT::pointer(0, 32);
  const LLT S32 = LLT::scalar(32);
  Register List = MI.getOperand(0).getReg();
  const int GprFI = FuncInfo->getVarArgsGprFI();
  const int DrFI = FuncInfo->getVarArgsDrFI();
  const int StackFI = FuncInfo->getVarArgsStackFI();
  const int GprSize = FuncInfo->getVarArgsGprSize();
  const int DrSize = FuncInfo->getVarArgsDrSize();

  auto StorePtrField = [&](int FI, int64_t Extra, int FieldOff) {
    Register Addr = MIB.buildFrameIndex(PtrTy, FI).getReg(0);
    if (Extra != 0) {
      auto Off = MIB.buildConstant(S32, Extra);
      Addr = MIB.buildPtrAdd(PtrTy, Addr, Off).getReg(0);
    }
    Register FieldPtr = vaListFieldPtr(MIB, List, FieldOff, PtrTy, S32);
    MIB.buildStore(Addr, FieldPtr,
                   *vaListMMO(MF, MachineMemOperand::MOStore, PtrTy, Align(4),
                              FieldOff));
  };
  auto StoreOffsField = [&](int BankSize, int FieldOff) {
    auto Imm = MIB.buildConstant(S32, -BankSize);
    Register FieldPtr = vaListFieldPtr(MIB, List, FieldOff, PtrTy, S32);
    MIB.buildStore(Imm, FieldPtr,
                   *vaListMMO(MF, MachineMemOperand::MOStore, S32, Align(4),
                              FieldOff));
  };

  StorePtrField(StackFI, /*Extra=*/0, kVaStackOff);
  StorePtrField(GprFI, GprSize, kVaGrTopOff);
  StorePtrField(DrFI, DrSize, kVaVrTopOff);
  StoreOffsField(GprSize, kVaGrOffsOff);
  StoreOffsField(DrSize, kVaVrOffsOff);
  MI.eraseFromParent();
  return true;
}

bool HaydnLegalizerInfo::legalizeVAArg(LegalizerHelper &Helper,
                                       MachineInstr &MI) const {
  // AArch64LegalizerInfo.cpp:2158 — GISel va_arg is straight-line (no CFG).
  // Haydn adds GPR/DR bank select plus overflow onto __stack via G_SELECT.
  MachineIRBuilder &MIB = Helper.MIRBuilder;
  MachineRegisterInfo &MRI = *MIB.getMRI();
  MachineFunction &MF = *MI.getMF();

  Register Dst = MI.getOperand(0).getReg();
  Register List = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(Dst);
  const LLT PtrTy = LLT::pointer(0, 32);
  const LLT S32 = LLT::scalar(32);
  const LLT S1 = LLT::scalar(1);

  if (MI.getNumOperands() >= 3 && MI.getOperand(2).isImm()) {
    const int64_t VaAlign = MI.getOperand(2).getImm();
    if (VaAlign <= 0 || VaAlign > 8 || (VaAlign & (VaAlign - 1)) != 0)
      return false;
  }

  const bool IsI64 = DstTy.isScalar() && DstTy.getSizeInBits() == 64;
  const int TopOff = IsI64 ? kVaVrTopOff : kVaGrTopOff;
  const int OffsOff = IsI64 ? kVaVrOffsOff : kVaGrOffsOff;
  const int64_t RegStep = IsI64 ? 8 : 4;
  const Align ValAlign = IsI64 ? Align(8) : Align(4);

  MIB.setInstrAndDebugLoc(MI);

  auto LoadPtrField = [&](int FieldOff) {
    Register FieldPtr = vaListFieldPtr(MIB, List, FieldOff, PtrTy, S32);
    return MIB
        .buildLoad(PtrTy, FieldPtr,
                   *vaListMMO(MF, MachineMemOperand::MOLoad, PtrTy, Align(4),
                              FieldOff))
        .getReg(0);
  };
  auto LoadOffsField = [&](int FieldOff) {
    Register FieldPtr = vaListFieldPtr(MIB, List, FieldOff, PtrTy, S32);
    return MIB
        .buildLoad(S32, FieldPtr,
                   *vaListMMO(MF, MachineMemOperand::MOLoad, S32, Align(4),
                              FieldOff))
        .getReg(0);
  };

  Register CurOff = LoadOffsField(OffsOff);
  Register Top = LoadPtrField(TopOff);
  Register StackPtr = LoadPtrField(kVaStackOff);
  auto Tentative = MIB.buildAdd(S32, CurOff, MIB.buildConstant(S32, RegStep));
  Register UseStack =
      MIB.buildICmp(CmpInst::ICMP_SGT, S1, Tentative,
                    MIB.buildConstant(S32, 0))
          .getReg(0);

  Register RegAddr = MIB.buildPtrAdd(PtrTy, Top, CurOff).getReg(0);
  Register Addr =
      MIB.buildSelect(PtrTy, UseStack, StackPtr, RegAddr).getReg(0);
  MIB.buildLoad(Dst, Addr,
                *vaListMMO(MF, MachineMemOperand::MOLoad, DstTy, ValAlign,
                           /*ByteOff=*/0));

  auto NewStack =
      MIB.buildPtrAdd(PtrTy, StackPtr, MIB.buildConstant(S32, kVaStackStep));
  auto NewOffs = MIB.buildAdd(S32, CurOff, MIB.buildConstant(S32, RegStep));
  Register StoreStack =
      MIB.buildSelect(PtrTy, UseStack, NewStack, StackPtr).getReg(0);
  Register StoreOffs =
      MIB.buildSelect(S32, UseStack, CurOff, NewOffs).getReg(0);
  MIB.buildStore(StoreStack, vaListFieldPtr(MIB, List, kVaStackOff, PtrTy, S32),
                 *vaListMMO(MF, MachineMemOperand::MOStore, PtrTy, Align(4),
                            kVaStackOff));
  MIB.buildStore(StoreOffs, vaListFieldPtr(MIB, List, OffsOff, PtrTy, S32),
                 *vaListMMO(MF, MachineMemOperand::MOStore, S32, Align(4),
                            OffsOff));
  MI.eraseFromParent();
  return true;
}

bool HaydnLegalizerInfo::legalizeCustom(LegalizerHelper &Helper,
                                        MachineInstr &MI,
                                        LostDebugLocObserver &LocObserver) const {
  using namespace TargetOpcode;
  MachineIRBuilder &MIB = Helper.MIRBuilder;
  MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();

  if (MI.getOpcode() == G_VASTART)
    return legalizeVAStart(Helper, MI);
  if (MI.getOpcode() == G_VAARG)
    return legalizeVAArg(Helper, MI);

  //===--------------------------------------------------------------------===
  // G_MERGE_VALUES → non-power-of-2 result (pr79737-1: 9×s8 → s72).
  // Build next-pow2 accumulator with zext+shl+or, then trunc.
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == G_MERGE_VALUES) {
    Register Dst = MI.getOperand(0).getReg();
    LLT DstTy = MRI.getType(Dst);
    if (!DstTy.isScalar() || isPowerOf2_32(DstTy.getSizeInBits()))
      return false;
    const unsigned DstBits = DstTy.getSizeInBits();
    const unsigned WideBits = PowerOf2Ceil(DstBits);
    const LLT WideTy = LLT::scalar(WideBits);
    const LLT S32 = LLT::scalar(32);
    MIB.setInstrAndDebugLoc(MI);
    Register Acc = MIB.buildConstant(WideTy, 0).getReg(0);
    unsigned Offset = 0;
    for (unsigned OpIdx = 1, E = MI.getNumOperands(); OpIdx < E; ++OpIdx) {
      Register Part = MI.getOperand(OpIdx).getReg();
      LLT PartTy = MRI.getType(Part);
      if (!PartTy.isScalar()) {
        // Should not happen for pr79737-1 path; abort cleanly.
        return false;
      }
      const unsigned PartBits = PartTy.getSizeInBits();
      Register Ext = MIB.buildZExt(WideTy, Part).getReg(0);
      if (Offset != 0) {
        auto ShAmt = MIB.buildConstant(S32, Offset);
        Ext = MIB.buildShl(WideTy, Ext, ShAmt).getReg(0);
      }
      Acc = MIB.buildOr(WideTy, Acc, Ext).getReg(0);
      Offset += PartBits;
    }
    if (WideBits == DstBits)
      MIB.buildCopy(Dst, Acc);
    else
      MIB.buildTrunc(Dst, Acc);
    MI.eraseFromParent();
    return true;
  }

  //===--------------------------------------------------------------------===
  // G_CONSTANT wider than s64 (s72 bitfield containers). Split into two s64
  // halves, merge to s128, trunc back to the original width so consumers see
  // a trunc(s128) that G_ANYEXT can unmerge (pr79737-2).
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == G_CONSTANT) {
    Register Dst = MI.getOperand(0).getReg();
    LLT DstTy = MRI.getType(Dst);
    unsigned Bits = DstTy.isScalar() ? DstTy.getSizeInBits() : 0;
    // Custom path is only for 65..128-bit scalars (see builder rule above).
    if (!DstTy.isScalar() || Bits <= 64 || Bits > 128)
      return false;
    MIB.setInstrAndDebugLoc(MI);
    const LLT S64 = LLT::scalar(64);
    const LLT S128 = LLT::scalar(128);
    // zext only when the ConstantInt is narrower than 128; never zext-down.
    APInt Raw = MI.getOperand(1).getCImm()->getValue();
    APInt Val = Raw.getBitWidth() < 128 ? Raw.zext(128)
               : Raw.getBitWidth() > 128 ? Raw.trunc(128)
                                         : Raw;
    auto Lo = MIB.buildConstant(S64, Val.trunc(64));
    auto Hi = MIB.buildConstant(S64, Val.lshr(64).trunc(64));
    Register Wide = MRI.createGenericVirtualRegister(S128);
    MIB.buildMergeLikeInstr(Wide, {Lo.getReg(0), Hi.getReg(0)});
    if (Bits == 128)
      MIB.buildCopy(Dst, Wide);
    else
      MIB.buildTrunc(Dst, Wide);
    MI.eraseFromParent();
    return true;
  }

  //===--------------------------------------------------------------------===
  // Same-size G_ZEXTLOAD / G_SEXTLOAD → G_LOAD (CB-126 residual).
  // Non-pow2 mem lower emits s32 = G_ZEXTLOAD (load s32); generic lowerLoad
  // then UnableToLegalize. Plain load is legal for s32/s32.
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == G_ZEXTLOAD || MI.getOpcode() == G_SEXTLOAD) {
    auto &LoadMI = cast<GExtLoad>(MI);
    Register Dst = LoadMI.getDstReg();
    Register Ptr = LoadMI.getPointerReg();
    LLT DstTy = MRI.getType(Dst);
    LLT MemTy = LoadMI.getMMO().getMemoryType();
    if (DstTy == MemTy) {
      MIB.setInstrAndDebugLoc(MI);
      MIB.buildLoad(Dst, Ptr, LoadMI.getMMO());
      MI.eraseFromParent();
      return true;
    }
    return false;
  }

  //===--------------------------------------------------------------------===
  // G_STORE value-width ≠ MMO mem-width (CB-126 residual, pr79737-2).
  // After i72 split: G_STORE s32 :: (store s64). Anyext/trunc to match mem,
  // rewrite MMO size so the store is a natural legal pair.
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == G_STORE) {
    auto &StoreMI = cast<GStore>(MI);
    Register Val = StoreMI.getValueReg();
    Register Ptr = StoreMI.getPointerReg();
    LLT ValTy = MRI.getType(Val);
    MachineMemOperand &MMO = StoreMI.getMMO();
    LLT MemTy = MMO.getMemoryType();
    if (!ValTy.isScalar() || !MemTy.isScalar() || ValTy == MemTy)
      return false;

    MIB.setInstrAndDebugLoc(MI);
    MachineFunction &MF = *MI.getMF();
    Register StoreVal = Val;
    if (ValTy.getSizeInBits() < MemTy.getSizeInBits())
      StoreVal = MIB.buildAnyExt(MemTy, Val).getReg(0);
    else
      StoreVal = MIB.buildTrunc(MemTy, Val).getReg(0);

    MachineMemOperand *NewMMO =
        MF.getMachineMemOperand(&MMO, MMO.getPointerInfo(), MemTy);
    MIB.buildStore(StoreVal, Ptr, *NewMMO);
    MI.eraseFromParent();
    return true;
  }

  //===--------------------------------------------------------------------===
  // Narrow-result extensions (s1/s8 source -> s8/s16 result). The generic
  // widenScalar helper for G_[SZ]EXT/G_ANYEXT widens the SOURCE register
  // leaving the result type unchanged — so clampScalar(0, S32, S64) cannot
  // promote a narrow result. We handle the narrow-result case explicitly by
  // widening the result to s32 (legal path), then truncating back to the
  // requested narrow type. The selector materialises the s32 result then
  // narrows it into the destination sub-register slot.
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == G_ZEXT || MI.getOpcode() == G_SEXT ||
      MI.getOpcode() == G_ANYEXT) {
    Register DstReg = MI.getOperand(0).getReg();
    LLT DstTy = MRI.getType(DstReg);
    Register SrcReg = MI.getOperand(1).getReg();
    LLT SrcTy = MRI.getType(SrcReg);

    // Phase A (Pat-first): rewrite sN→s32 zext/sext to generic AND / SHL+LSHR /
    // SHL+ASHR with G_CONSTANT so selectImpl RI Pats fold immediates. Avoids
    // C++ emitALUImm residual for the common width matrix.
    //
    // Sub-32 sources may still carry LLT s8/s16/s1; G_AND/G_SHL require a
    // common scalar type. Widen via G_ANYEXT (next legalizer pass turns that
    // into a bank-identity copy on GPR32).
    if (DstTy.isScalar() && SrcTy.isScalar() &&
        DstTy.getSizeInBits() == 32 && SrcTy.getSizeInBits() < 32 &&
        SrcTy.getSizeInBits() >= 1) {
      const unsigned SrcBits = SrcTy.getSizeInBits();
      const LLT S32 = LLT::scalar(32);
      MIB.setInstrAndDebugLoc(MI);
      // Widen LLT to s32 for AND/SHL (anyext is legal → selector identity).
      auto Wide =
          (SrcBits == 32) ? SrcReg : MIB.buildAnyExt(S32, SrcReg).getReg(0);
      if (MI.getOpcode() == G_ZEXT) {
        if (SrcBits <= 20) {
          auto Mask = MIB.buildConstant(S32, (1u << SrcBits) - 1);
          MIB.buildAnd(DstReg, Wide, Mask);
        } else {
          const unsigned Sh = 32 - SrcBits;
          auto ShAmt = MIB.buildConstant(S32, Sh);
          auto Tmp = MIB.buildShl(S32, Wide, ShAmt);
          MIB.buildLShr(DstReg, Tmp, ShAmt);
        }
        MI.eraseFromParent();
        return true;
      }
      // G_SEXT
      const unsigned Sh = 32 - SrcBits;
      auto ShAmt = MIB.buildConstant(S32, Sh);
      auto Tmp = MIB.buildShl(S32, Wide, ShAmt);
      MIB.buildAShr(DstReg, Tmp, ShAmt);
      MI.eraseFromParent();
      return true;
    }

    // sN→s64 is legal (selector residual ANDI/MOV or SEXT chain). Do not
    // custom-chain here: post-legalizer redundant-ext combine needs isLegal.

    // s64 -> s128 extension. IR-level instcombine / AggressiveInst
    // Combine folds an inline 64x64->128 schoolbook multiply into
    // `zext i64 X to i128; zext i64 Y to i128; mul nuw i128 %x, %y`.
    // The generic narrowScalar of G_MUL s128 then unmerges both operands
    // back to s64 halves and re-multiplies via s64 G_MUL/G_UMULH -- i.e. the
    // multiply itself was already handled. What was MISSING was the rule for
    // this feeding s64->s128 extension (the legalizer aborted on the G_ZEXT
    // before ever reaching the G_MUL). Lower it to G_MERGE_VALUES so the s128
    // value exists as two s64 halves the rest of the pipeline can chew on:
    // G_ZEXT/G_ANYEXT s64 -> s128 : merge <src>, <zero> (high 64 = 0)
    // G_SEXT s64 -> s128 : merge <src>, <sext-high> (sign-extend)
    // The G_ASHR-by-63 sign-extend-high idiom is canonical (matches
    // SelectionDAG soft-float targets). /.
    if (DstTy.isScalar() && DstTy.getSizeInBits() == 128) {
      const LLT S64 = LLT::scalar(64);
      const LLT S32 = LLT::scalar(32);
      Register Src = MI.getOperand(1).getReg();
      LLT SrcTy = MRI.getType(Src);
      if (!SrcTy.isScalar())
        return false;
      const unsigned SrcBits = SrcTy.getSizeInBits();
      if (SrcBits >= 128)
        return false;

      // SrcBits in (64, 128): build s128 as merge(lo s64, hi s64).
      // NEVER G_STORE the non-pow2 source — lowerStore(s72) re-emits
      // G_ANYEXT s72→s128 and that stack path infinite-loops (pr79737-2).
      if (SrcBits > 64) {
        MIB.setInstrAndDebugLoc(MI);
        const unsigned HiBits = SrcBits - 64; // e.g. 8 for s72
        Register Lo64 = MRI.createGenericVirtualRegister(S64);
        Register Hi64 = MRI.createGenericVirtualRegister(S64);
        bool HaveHalves = false;

        // Constant: split the APInt (store i72 C; lowerStore anyexts C).
        if (auto MaybeCst = getIConstantVRegValWithLookThrough(Src, MRI)) {
          APInt V = MaybeCst->Value.zext(128);
          MIB.buildConstant(Lo64, V.trunc(64));
          MIB.buildConstant(Hi64, V.lshr(64).trunc(64));
          HaveHalves = true;
        } else if (MachineInstr *Def = MRI.getVRegDef(Src)) {
          // Trunc of s128 (typical after lowerLoad): unmerge the wide value.
          if (Def->getOpcode() == G_TRUNC) {
            Register Wide = Def->getOperand(1).getReg();
            LLT WideTy = MRI.getType(Wide);
            if (WideTy.isScalar() && WideTy.getSizeInBits() == 128) {
              auto U = MIB.buildUnmerge(S64, Wide);
              MIB.buildCopy(Lo64, U.getReg(0));
              MIB.buildCopy(Hi64, U.getReg(1));
              HaveHalves = true;
            } else if (WideTy.isScalar() && WideTy.getSizeInBits() > 128) {
              auto ShAmt = MIB.buildConstant(S32, 64);
              MIB.buildTrunc(Lo64, Wide);
              auto HiW = MIB.buildLShr(WideTy, Wide, ShAmt);
              MIB.buildTrunc(Hi64, HiW);
              HaveHalves = true;
            }
          } else if (Def->getOpcode() == G_LOAD ||
                     Def->getOpcode() == G_SEXTLOAD ||
                     Def->getOpcode() == G_ZEXTLOAD) {
            // Re-load as legal s64 + high fragment (no non-pow2 mem op).
            auto &LoadMI = cast<GAnyLoad>(*Def);
            MachineMemOperand &OldMMO = LoadMI.getMMO();
            if (OldMMO.getMemoryType().getSizeInBits() == SrcBits) {
              MachineFunction &MF = *MI.getMF();
              Register Ptr = LoadMI.getPointerReg();
              const LLT P0 = LLT::pointer(0, 32);
              MachineMemOperand *LoMMO =
                  MF.getMachineMemOperand(&OldMMO, 0, 8);
              auto LoLd = MIB.buildLoad(S64, Ptr, *LoMMO);
              MIB.buildCopy(Lo64, LoLd.getReg(0));
              auto Off = MIB.buildConstant(S32, 8);
              auto PtrHi = MIB.buildPtrAdd(P0, Ptr, Off);
              unsigned HiBytes = (HiBits + 7) / 8;
              MachineMemOperand *HiMMO =
                  MF.getMachineMemOperand(&OldMMO, 8, HiBytes);
              // High fragment is always zero-extended into the s64 hi half;
              // whole-value G_SEXT is applied below via s64 sign-extend.
              auto HiLd =
                  MIB.buildLoadInstr(G_ZEXTLOAD, S64, PtrHi, *HiMMO);
              MIB.buildCopy(Hi64, HiLd.getReg(0));
              HaveHalves = true;
            }
          }
        }

        if (!HaveHalves) {
          // Last resort: low half only. High bits of the source are lost —
          // prefer a hard failure over the old non-pow2 stack store loop.
          return false;
        }

        // ZEXT: clear bits above the original source width in the hi half.
        if (MI.getOpcode() == G_ZEXT && HiBits < 64) {
          auto Mask = MIB.buildConstant(
              S64, (APInt::getAllOnes(HiBits)).zext(64));
          Register Masked = MRI.createGenericVirtualRegister(S64);
          MIB.buildAnd(Masked, Hi64, Mask);
          Hi64 = Masked;
        }

        // SEXT: sign-extend from bit (SrcBits-1), which lives in Hi64.
        // Use only s64 shifts (s128 shifts are not legal on Haydn).
        if (MI.getOpcode() == G_SEXT) {
          unsigned Sh = 64 - HiBits;
          auto ShAmt = MIB.buildConstant(S32, Sh);
          Register T = MRI.createGenericVirtualRegister(S64);
          MIB.buildShl(T, Hi64, ShAmt);
          Register SExtHi = MRI.createGenericVirtualRegister(S64);
          MIB.buildAShr(SExtHi, T, ShAmt);
          Hi64 = SExtHi;
        }

        MIB.buildMergeLikeInstr(DstReg, {Lo64, Hi64});
        MI.eraseFromParent();
        return true;
      }

      Register Lo64 = Src;
      if (SrcBits < 64)
        Lo64 = MIB.buildInstr(MI.getOpcode(), {S64}, {Src}).getReg(0);

      Register Hi64 = MRI.createGenericVirtualRegister(S64);
      if (MI.getOpcode() == G_SEXT) {
        Register ShAmt63 = MRI.createGenericVirtualRegister(S32);
        MIB.buildConstant(ShAmt63, 63);
        MIB.buildAShr(Hi64, Lo64, ShAmt63);
      } else {
        MIB.buildConstant(Hi64, 0);
      }
      MIB.buildMergeLikeInstr(DstReg, {Lo64, Hi64});
      MI.eraseFromParent();
      return true;
    }

    if (DstTy.isScalar() &&
        (DstTy.getSizeInBits() == 8 || DstTy.getSizeInBits() == 16)) {
      Register WideDst = MRI.createGenericVirtualRegister(LLT::scalar(32));
      Register Src = MI.getOperand(1).getReg();
      MIB.buildInstr(MI.getOpcode(), {WideDst}, {Src});
      MIB.buildTrunc(DstReg, WideDst);
      MI.eraseFromParent();
      return true;
    }
  }

  //===--------------------------------------------------------------------===
  // G_SMULH / G_UMULH — narrow types (s8/s16). Widening operands with the
  // matching extension (sext for SMULH, zext for UMULH) to s32, computing the
  // s32 multiply, and extracting the relevant high bits yields the correct
  // narrow high-multiply result. The generic minScalar/widenScalar path does
  // not sign-extend operands, so it mis-compiles the SIGNED case; we do it
  // explicitly here. Fixes yarpgen seeds 1, 6, 10.
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == G_SMULH || MI.getOpcode() == G_UMULH) {
    Register DstReg = MI.getOperand(0).getReg();
    LLT DstTy = MRI.getType(DstReg);
    unsigned DstBits = DstTy.getSizeInBits();
    // Only handle NARROW types (s8/s16/s24) here. s64 UMULH falls through to
    // its dedicated schoolbook handler below; s32 is lowered generically (not
    // custom). s24 comes from 23-bit signed bitfields.
    if (DstBits == 8 || DstBits == 16 || DstBits == 24) {
      const LLT S32 = LLT::scalar(32);
      Register Src0 = MI.getOperand(1).getReg();
      Register Src1 = MI.getOperand(2).getReg();

      // Extend both operands to s32 with the matching extension kind.
      Register E0 = MRI.createGenericVirtualRegister(S32);
      Register E1 = MRI.createGenericVirtualRegister(S32);
      if (MI.getOpcode() == G_SMULH) {
        MIB.buildSExt(E0, Src0);
        MIB.buildSExt(E1, Src1);
      } else {
        MIB.buildZExt(E0, Src0);
        MIB.buildZExt(E1, Src1);
      }

      // Full 32x32 product; extract bits [DstBits, 2*DstBits) by shifting.
      Register Prod = MRI.createGenericVirtualRegister(S32);
      MIB.buildMul(Prod, E0, E1);

      Register ShiftAmt = MRI.createGenericVirtualRegister(S32);
      MIB.buildConstant(ShiftAmt, DstBits);
      Register Shifted = MRI.createGenericVirtualRegister(S32);
      if (MI.getOpcode() == G_SMULH)
        MIB.buildAShr(Shifted, Prod, ShiftAmt);
      else
        MIB.buildLShr(Shifted, Prod, ShiftAmt);

      MIB.buildTrunc(DstReg, Shifted);
      MI.eraseFromParent();
      return true;
    }
    // non-narrow: fall through to the G_UMULH s64 handler below (or the
    // generic s32 lower path, which isn't custom).
  }

  //===--------------------------------------------------------------------===
  // G_BSWAP / G_BITREVERSE s64 -- custom lowering. The generic
  // LegalizerHelper::lowerBswap computes byte masks as `0xFF << (i*8)` in
  // signed int (LegalizerHelper.cpp:9716); for i=3 this sign-extends to
  // 0xFFFFFFFF_FF000000 instead of 0x00000000_FF000000, dropping the
  // innermost byte pair of the bswap and corrupting any 64-bit bitreverse
  // whose bytes 3 and 4 differ. We rebuild the sequence here with
  // uint64_t masks. G_BITREVERSE lowers via G_BSWAP + the standard
  // Swap4/Swap2/Swap1 intra-byte stages (whose 0xF0../0xCC../0xAA.. masks are
  // already correct as they fit in 31 bits).
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == G_BSWAP || MI.getOpcode() == G_BITREVERSE) {
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg = MI.getOperand(1).getReg();
    LLT Ty = MRI.getType(DstReg);
    if (Ty.getSizeInBits() == 64) {
      // Byte-swap (8-byte reversal) with unsigned byte masks. Mirrors
      // LegalizerHelper::lowerBswap but uses uint64_t masks so the i=3 mask
      // is 0x00000000FF000000, not the signed-overflow 0xFFFFFFFFFF000000.
      auto ShiftAmt0 = MIB.buildConstant(Ty, uint64_t(56));
      auto LoShl = MIB.buildShl(Ty, SrcReg, ShiftAmt0);
      auto HiLshr = MIB.buildLShr(Ty, SrcReg, ShiftAmt0);
      auto Bswap = MIB.buildOr(Ty, HiLshr, LoShl);
      for (unsigned I = 1; I < 4; ++I) {
        uint64_t MaskVal = 0xFFULL << (I * 8);   // unsigned: no sign extension
        uint64_t ShiftVal = 56 - 16 * I;
        auto MaskC = MIB.buildConstant(Ty, MaskVal);
        auto ShiftC = MIB.buildConstant(Ty, ShiftVal);
        auto LoByte = MIB.buildAnd(Ty, SrcReg, MaskC);
        auto LoShlI = MIB.buildShl(Ty, LoByte, ShiftC);
        Bswap = MIB.buildOr(Ty, Bswap, LoShlI);
        auto SrcShr = MIB.buildLShr(Ty, SrcReg, ShiftC);
        auto HiShr = MIB.buildAnd(Ty, SrcShr, MaskC);
        Bswap = MIB.buildOr(Ty, Bswap, HiShr);
      }

      // G_BSWAP is done; G_BITREVERSE continues with intra-byte swaps. The
      // swap masks (0xF0.., 0xCC.., 0xAA..) are all <= 0x7FFFFFFF per byte
      // and splatted across 8 bytes, so they are positive when built as
      // APInt(64, splat) -- no signed-overflow pitfall, identical to the
      // generic SwapN lowering.
      if (MI.getOpcode() == G_BSWAP) {
        Bswap.getInstr()->getOperand(0).setReg(DstReg);
      } else {
        const LLT S64 = Ty;
        auto SwapN = [&MIB, &S64](MachineInstrBuilder Src, unsigned N,
                                  uint64_t Mask) -> MachineInstrBuilder {
          auto CN = MIB.buildConstant(S64, uint64_t(N));
          auto CMask = MIB.buildConstant(S64, Mask);
          auto LHS = MIB.buildLShr(S64, MIB.buildAnd(S64, Src, CMask), CN);
          auto RHS = MIB.buildAnd(S64, MIB.buildShl(S64, Src, CN), CMask);
          return MIB.buildOr(S64, LHS, RHS);
        };
        auto S4 = SwapN(Bswap, 4, 0xF0F0F0F0F0F0F0F0ULL);
        auto S2 = SwapN(S4, 2, 0xCCCCCCCCCCCCCCCCULL);
        auto S1 = SwapN(S2, 1, 0xAAAAAAAAAAAAAAAAULL);
        S1.getInstr()->getOperand(0).setReg(DstReg);
      }
      MI.eraseFromParent();
      return true;
    }
    // s16/s32 fall through to the generic lowerFor path (no signed-overflow
    // issue: the i=3 mask only exists for 8-byte bswap = s64).
    return false;
  }

  //===--------------------------------------------------------------------===
  // G_MUL s64 -- widening 32x32->64 becomes G_HAYDN_MUL64_WIDEN (signed) or
  // G_HAYDN_MUL64_WIDENU (unsigned) by operand extension kind; true 64x64
  // lowers to a schoolbook of three unsigned widening muls. No __muldi3.
  //===--------------------------------------------------------------------===
  // Haydn has no single 64x64->64 multiply. The MUL64_* family computes each
  // 32x32->64 partial natively, but the legalizer emits target-generic
  // G_HAYDN_MUL64_WIDEN{,U} (AIE G_AIE_* shape; select later). Peer:
  // AIELegalizerHelper.cpp:1519-1531 builds G_AIE_BROADCAST_VECTOR;
  // RISCVLegalizerInfo.cpp:444-447 uses libcallFor, never a target MUL.
  // For `(int64_t)(int32_t)a * b` (G_SEXT of s32) emit G_HAYDN_MUL64_WIDEN;
  // for `(uint64_t)(uint32_t)a * b` (G_ZEXT/G_ANYEXT) emit
  // G_HAYDN_MUL64_WIDENU. True 64x64 (at least one operand not a widened
  // s32) is schoolbook of three G_HAYDN_MUL64_WIDENU partials + add + shl.
  // customFor, not libcallFor: Haydn call lowering does not split libcall
  // args from LLVM Type*.
  if (MI.getOpcode() == G_MUL) {
    Register DstReg = MI.getOperand(0).getReg();
    Register Src0 = MI.getOperand(1).getReg();
    Register Src1 = MI.getOperand(2).getReg();
    LLT DstTy = MRI.getType(DstReg);

    if (DstTy.getSizeInBits() == 64) {
      // Widening 32x32->64 multiply: both operands trace to a widened s32
      // (`(int64_t)(int32_t)a * b` or `(uint64_t)(uint32_t)a * b`).
      //
      // Sign vs unsigned: MUL64_LL is signed x signed 32x32->64; fully
      // unsigned low×low is MUL64_ULUL (MUL64_ULL is unsigned x SIGNED).
      // A ZERO-extended operand with bit 31 set needs the unsigned generic
      // (G_HAYDN_MUL64_WIDENU); signed widening sign-extends the wrong half.
      auto ExtKindOfS32 = [&](Register R, unsigned &ExtOp) -> bool {
        ExtOp = 0;
        if (!R.isVirtual())
          return false;
        MachineInstr *Def = MRI.getVRegDef(R);
        if (!Def)
          return false;
        unsigned Op = Def->getOpcode();
        if (Op != TargetOpcode::G_SEXT && Op != TargetOpcode::G_ZEXT &&
            Op != TargetOpcode::G_ANYEXT)
          return false;
        Register ExtSrc = Def->getOperand(1).getReg();
        if (!ExtSrc.isVirtual() || !MRI.getType(ExtSrc).isValid())
          return false;
        if (MRI.getType(ExtSrc).getSizeInBits() != 32)
          return false;
        ExtOp = Op;
        return true;
      };
      unsigned Ext0, Ext1;
      bool Widened0 = ExtKindOfS32(Src0, Ext0);
      bool Widened1 = ExtKindOfS32(Src1, Ext1);

      if (Widened0 && Widened1) {
        // SEXT x SEXT -> G_HAYDN_MUL64_WIDEN. ZEXT/ANYEXT x ZEXT/ANYEXT ->
        // G_HAYDN_MUL64_WIDENU. Mixed signedness is not a single widening
        // multiply; use the schoolbook path below.
        bool BothSigned = (Ext0 == TargetOpcode::G_SEXT &&
                           Ext1 == TargetOpcode::G_SEXT);
        bool BothUnsigned =
            (Ext0 == TargetOpcode::G_ZEXT || Ext0 == TargetOpcode::G_ANYEXT) &&
            (Ext1 == TargetOpcode::G_ZEXT || Ext1 == TargetOpcode::G_ANYEXT);
        if (BothSigned || BothUnsigned) {
          unsigned MulOpc = BothSigned ? Haydn::G_HAYDN_MUL64_WIDEN
                                       : Haydn::G_HAYDN_MUL64_WIDENU;
          MIB.buildInstr(MulOpc, {DstReg}, {Src0, Src1});
          MI.eraseFromParent();
          return true;
        }
      }

      // True 64x64 multiply (at least one operand is not a widened s32).
      // Schoolbook of generic G_MIR; inner G_MUL <s64> of zext-of-s32
      // re-enter as G_HAYDN_MUL64_WIDENU:
      //
      // aLo, aHi = G_UNMERGE_VALUES a (2 x s32)
      // bLo, bHi = G_UNMERGE_VALUES b (2 x s32)
      // LL/LH/HL = (zext half) * (zext half) -> G_HAYDN_MUL64_WIDENU
      // result = LL + ((LH + HL) << 32) (mod 2^64)
      //
      // HH (aHi*bHi) only contributes to bits >= 64. See
      // mul-i64-native-all-shapes.ll.
      lowerMul64Schoolbook(MIB, MRI, MI);
      return true;
    }
    return false;
  }

  //===--------------------------------------------------------------------===
  // G_UMULH s64 — high 64 bits of unsigned 64x64 multiply, via schoolbook
  // G_MUL of zext halves (each becomes G_HAYDN_MUL64_WIDENU). Required by
  // the generic PreLegalizerCombiner's udiv-by-constant strength reduction,
  // which expands `udiv X, K` to `(UMULH X, magic) >> shift`.
  //
  // Formula (a, b are s64):
  // aLo, aHi = unmerge a (each s32, zero-extended for the inner mul)
  // bLo, bHi = unmerge b
  // LL = (zext aLo) * (zext bLo) -> bits 0-63
  // LH = (zext aLo) * (zext bHi) -> bits 32-95
  // HL = (zext aHi) * (zext bLo) -> bits 32-95
  // HH = (zext aHi) * (zext bHi) -> bits 64-127 (low 64 of HH span)
  // UMULH = HH + ((LH + HL + (LL >> 32)) >> 32) (mod 2^64)
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == G_UMULH) {
    Register DstReg = MI.getOperand(0).getReg();
    LLT DstTy = MRI.getType(DstReg);
    if (DstTy.getSizeInBits() != 64)
      return false;

    const LLT S32 = LLT::scalar(32);
    const LLT S64 = LLT::scalar(64);

    Register Src0 = MI.getOperand(1).getReg();
    Register Src1 = MI.getOperand(2).getReg();

    Register ALo = MRI.createGenericVirtualRegister(S32);
    Register AHi = MRI.createGenericVirtualRegister(S32);
    MIB.buildUnmerge({ALo, AHi}, Src0);
    Register BLo = MRI.createGenericVirtualRegister(S32);
    Register BHi = MRI.createGenericVirtualRegister(S32);
    MIB.buildUnmerge({BLo, BHi}, Src1);

    // Zero-extend each half to s64 so inner G_MUL ops hit the widening path
    // (G_HAYDN_MUL64_WIDENU). Zero extension is correct for UMULH.
    auto ZextTo64 = [&](Register R) {
      Register Ext = MRI.createGenericVirtualRegister(S64);
      MIB.buildZExt(Ext, R);
      return Ext;
    };
    Register ALo64 = ZextTo64(ALo);
    Register AHi64 = ZextTo64(AHi);
    Register BLo64 = ZextTo64(BLo);
    Register BHi64 = ZextTo64(BHi);

    // Four 32x32->64 widening partial products. Each becomes
    // G_HAYDN_MUL64_WIDENU.
    Register LL = MRI.createGenericVirtualRegister(S64);
    MIB.buildMul(LL, ALo64, BLo64);
    Register LH = MRI.createGenericVirtualRegister(S64);
    MIB.buildMul(LH, ALo64, BHi64);
    Register HL = MRI.createGenericVirtualRegister(S64);
    MIB.buildMul(HL, AHi64, BLo64);
    Register HH = MRI.createGenericVirtualRegister(S64);
    MIB.buildMul(HH, AHi64, BHi64);

    // Schoolbook 128-bit multiply with carry propagation into HH. The mid sum
    // (LH + HL + (LL>>32)) can carry out of bit 63 (LH,HL are each up to
    // (2^32-1)^2 ~= 2^64); computing it mod 2^64 silently drops that carry
    // which represents 2^96 in the full product and MUST reach HH. Splitting
    // each cross-partial into low-32/high-32 keeps the carry explicit
    // (canonical compiler-rt/RISC-V LowerUMULH form):
    // Mid = (LH & MASK32) + (HL & MASK32) + (LL >> 32) / <= 33 bits
    // UMULH = HH + (LH >> 32) + (HL >> 32) + (Mid >> 32)
    Register ShAmt32 = MRI.createGenericVirtualRegister(S32);
    MIB.buildConstant(ShAmt32, 32);
    Register Mask32 = MRI.createGenericVirtualRegister(S64);
    MIB.buildConstant(Mask32, APInt(64, 0xFFFFFFFFu));

    Register LLHi = MRI.createGenericVirtualRegister(S64);
    MIB.buildLShr(LLHi, LL, ShAmt32);
    Register LHLo = MRI.createGenericVirtualRegister(S64);
    MIB.buildAnd(LHLo, LH, Mask32);
    Register HLLo = MRI.createGenericVirtualRegister(S64);
    MIB.buildAnd(HLLo, HL, Mask32);
    Register LHHi = MRI.createGenericVirtualRegister(S64);
    MIB.buildLShr(LHHi, LH, ShAmt32);
    Register HLHi = MRI.createGenericVirtualRegister(S64);
    MIB.buildLShr(HLHi, HL, ShAmt32);

    // Mid = LHLo + HLLo + LLHi. Chain through fresh vregs (one def each).
    Register MidA = MRI.createGenericVirtualRegister(S64);
    MIB.buildAdd(MidA, LHLo, HLLo);
    Register Mid = MRI.createGenericVirtualRegister(S64);
    MIB.buildAdd(Mid, MidA, LLHi);

    // UMULH = HH + LHHi + HLHi + (Mid >> 32). (Mid>>32) is the recovered carry.
    Register MidCarry = MRI.createGenericVirtualRegister(S64);
    MIB.buildLShr(MidCarry, Mid, ShAmt32);
    Register HHA = MRI.createGenericVirtualRegister(S64);
    MIB.buildAdd(HHA, HH, LHHi);
    Register HHB = MRI.createGenericVirtualRegister(S64);
    MIB.buildAdd(HHB, HHA, HLHi);
    MIB.buildAdd(DstReg, HHB, MidCarry);
    MI.eraseFromParent();
    return true;
  }

  // IEEE half convert: integer-bit libcall ABI (see legalizeHalfConvertLibcall).
  if (MI.getOpcode() == G_FPEXT || MI.getOpcode() == G_FPTRUNC)
    return legalizeHalfConvertLibcall(Helper, MI, LocObserver);

  // Soft-float G_FCONSTANT: bitcast IEEE bits into G_CONSTANT (not constpool).
  if (MI.getOpcode() == G_FCONSTANT) {
    const APFloat &FVal = MI.getOperand(1).getFPImm()->getValueAPF();
    Register DstReg = MI.getOperand(0).getReg();
    MIB.buildConstant(DstReg, FVal.bitcastToAPInt());
    MI.eraseFromParent();
    return true;
  }

  // Soft-float minimum/maximum* → fminnum/fmaxnum (libcall to fminf/fmaxf).
  if (MI.getOpcode() == G_FMINIMUM || MI.getOpcode() == G_FMAXIMUM ||
      MI.getOpcode() == G_FMINIMUMNUM || MI.getOpcode() == G_FMAXIMUMNUM) {
    Register DstReg = MI.getOperand(0).getReg();
    bool IsMin = (MI.getOpcode() == G_FMINIMUM ||
                  MI.getOpcode() == G_FMINIMUMNUM);
    MIB.buildInstr(IsMin ? G_FMINNUM : G_FMAXNUM, {DstReg},
                   {MI.getOperand(1).getReg(), MI.getOperand(2).getReg()});
    MI.eraseFromParent();
    return true;
  }

  //===--------------------------------------------------------------------===
  // G_BUILD_VECTOR — pack multi-element SIMD into legal v2i32 + bitcast.
  //===--------------------------------------------------------------------===
  // AIE custom-legalizes multi-lane builds (vpush / pack) and only selects
  // simple 2-way merges. Mirror that: emit scalar pack as gMIR, then a legal
  // G_BUILD_VECTOR <2 x s32> that ISel turns into MOV_GPR_TO_DR64.
  if (MI.getOpcode() == G_BUILD_VECTOR) {
    Register Dst = MI.getOperand(0).getReg();
    LLT DstTy = MRI.getType(Dst);
    const LLT S32 = LLT::scalar(32);
    const LLT V2S32 = LLT::fixed_vector(2, 32);

    // Residual <N x s1> builds from G_ICMP vector scalarize. Every remaining
    // use must be G_UNMERGE_VALUES of the same width; forward element regs and
    // drop the build. Avoids clampMaxNumElements(S1,1) → fewerElements assert.
    // Use Observer when rewriting so CSE maps stay consistent for the
    // post-legalizer combiner.
    if (DstTy.isVector() && DstTy.getElementType().getSizeInBits() == 1) {
      const unsigned NumElts = DstTy.getNumElements();
      if (MI.getNumOperands() != NumElts + 1)
        return false;
      SmallVector<Register, 8> Elts;
      Elts.reserve(NumElts);
      for (unsigned I = 0; I < NumElts; ++I)
        Elts.push_back(MI.getOperand(I + 1).getReg());

      SmallVector<MachineInstr *, 4> Unmerges;
      for (MachineInstr &Use : MRI.use_instructions(Dst)) {
        if (Use.getOpcode() != TargetOpcode::G_UNMERGE_VALUES ||
            Use.getNumOperands() != NumElts + 1)
          return false;
        Unmerges.push_back(&Use);
      }
      GISelChangeObserver &Observer = Helper.Observer;
      for (MachineInstr *U : Unmerges) {
        for (unsigned I = 0; I < NumElts; ++I) {
          Register From = U->getOperand(I).getReg();
          Register To = Elts[I];
          Observer.changingAllUsesOfReg(MRI, From);
          if (MRI.constrainRegAttrs(To, From))
            MRI.replaceRegWith(From, To);
          else
            MIB.buildCopy(From, To);
          Observer.finishedChangingAllUsesOfReg();
        }
        Observer.erasingInstr(*U);
        U->eraseFromParent();
      }
      Observer.erasingInstr(MI);
      MI.eraseFromParent();
      return true;
    }

    auto packTwo16 = [&](Register Lo16, Register Hi16) -> Register {
      Register LoW = MRI.createGenericVirtualRegister(S32);
      Register HiW = MRI.createGenericVirtualRegister(S32);
      MIB.buildAnyExt(LoW, Lo16);
      MIB.buildAnyExt(HiW, Hi16);
      Register Mask16 = MRI.createGenericVirtualRegister(S32);
      MIB.buildConstant(Mask16, 0xFFFF);
      Register Shift16 = MRI.createGenericVirtualRegister(S32);
      MIB.buildConstant(Shift16, 16);
      Register LoM = MRI.createGenericVirtualRegister(S32);
      MIB.buildAnd(LoM, LoW, Mask16);
      Register HiS = MRI.createGenericVirtualRegister(S32);
      MIB.buildShl(HiS, HiW, Shift16);
      Register Packed = MRI.createGenericVirtualRegister(S32);
      MIB.buildOr(Packed, LoM, HiS);
      return Packed;
    };

    auto packFour8 = [&](Register E0, Register E1, Register E2,
                         Register E3) -> Register {
      Register W0 = MRI.createGenericVirtualRegister(S32);
      Register W1 = MRI.createGenericVirtualRegister(S32);
      Register W2 = MRI.createGenericVirtualRegister(S32);
      Register W3 = MRI.createGenericVirtualRegister(S32);
      MIB.buildAnyExt(W0, E0);
      MIB.buildAnyExt(W1, E1);
      MIB.buildAnyExt(W2, E2);
      MIB.buildAnyExt(W3, E3);
      Register M8 = MRI.createGenericVirtualRegister(S32);
      MIB.buildConstant(M8, 0xFF);
      Register S8 = MRI.createGenericVirtualRegister(S32);
      MIB.buildConstant(S8, 8);
      Register S16 = MRI.createGenericVirtualRegister(S32);
      MIB.buildConstant(S16, 16);
      Register S24 = MRI.createGenericVirtualRegister(S32);
      MIB.buildConstant(S24, 24);
      auto maskShift = [&](Register W, unsigned ShAmt) -> Register {
        Register M = MRI.createGenericVirtualRegister(S32);
        MIB.buildAnd(M, W, M8);
        if (ShAmt == 0)
          return M;
        Register AmtReg = ShAmt == 8 ? S8 : (ShAmt == 16 ? S16 : S24);
        Register S = MRI.createGenericVirtualRegister(S32);
        MIB.buildShl(S, M, AmtReg);
        return S;
      };
      Register P0 = maskShift(W0, 0);
      Register P1 = maskShift(W1, 8);
      Register P2 = maskShift(W2, 16);
      Register P3 = maskShift(W3, 24);
      Register T0 = MRI.createGenericVirtualRegister(S32);
      MIB.buildOr(T0, P0, P1);
      Register T1 = MRI.createGenericVirtualRegister(S32);
      MIB.buildOr(T1, P2, P3);
      Register Packed = MRI.createGenericVirtualRegister(S32);
      MIB.buildOr(Packed, T0, T1);
      return Packed;
    };

    if (DstTy == LLT::fixed_vector(4, 16) && MI.getNumOperands() == 5) {
      Register Lo32 =
          packTwo16(MI.getOperand(1).getReg(), MI.getOperand(2).getReg());
      Register Hi32 =
          packTwo16(MI.getOperand(3).getReg(), MI.getOperand(4).getReg());
      Register V2 = MRI.createGenericVirtualRegister(V2S32);
      MIB.buildBuildVector(V2, {Lo32, Hi32});
      MIB.buildBitcast(Dst, V2);
      MI.eraseFromParent();
      return true;
    }

    if (DstTy == LLT::fixed_vector(8, 8) && MI.getNumOperands() == 9) {
      Register Lo32 = packFour8(MI.getOperand(1).getReg(),
                                MI.getOperand(2).getReg(),
                                MI.getOperand(3).getReg(),
                                MI.getOperand(4).getReg());
      Register Hi32 = packFour8(MI.getOperand(5).getReg(),
                                MI.getOperand(6).getReg(),
                                MI.getOperand(7).getReg(),
                                MI.getOperand(8).getReg());
      Register V2 = MRI.createGenericVirtualRegister(V2S32);
      MIB.buildBuildVector(V2, {Lo32, Hi32});
      MIB.buildBitcast(Dst, V2);
      MI.eraseFromParent();
      return true;
    }

    // Residual SLP v4i8 (32-bit): pack four s8 into one s32 + bitcast.
    if (DstTy == LLT::fixed_vector(4, 8) && MI.getNumOperands() == 5) {
      Register Packed = packFour8(MI.getOperand(1).getReg(),
                                  MI.getOperand(2).getReg(),
                                  MI.getOperand(3).getReg(),
                                  MI.getOperand(4).getReg());
      MIB.buildBitcast(Dst, Packed);
      MI.eraseFromParent();
      return true;
    }

    // Residual SLP v2i16 (32-bit): pack two s16 into one s32 + bitcast.
    if (DstTy == LLT::fixed_vector(2, 16) && MI.getNumOperands() == 3) {
      Register Packed =
          packTwo16(MI.getOperand(1).getReg(), MI.getOperand(2).getReg());
      MIB.buildBitcast(Dst, Packed);
      MI.eraseFromParent();
      return true;
    }

    return false;
  }

  if (MI.getOpcode() == G_TRUNC) {
    Register Dst = MI.getOperand(0).getReg();
    Register Src = MI.getOperand(1).getReg();
    LLT DstTy = MRI.getType(Dst);
    LLT SrcTy = MRI.getType(Src);

    // Handle <4 x s32> -> <4 x s16> truncation: the IRTranslator creates this
    // pattern when building <4 x i16> vectors from s32 function arguments.
    // %wide:_(<4 x s32>) = G_BUILD_VECTOR %a, %b, %c, %d
    // %vec:_(<4 x s16>) = G_TRUNC %wide
    // Pack s32 elements into two s32 values (4 x i16 packed into 64-bit DR64):
    // Lo32 = (a & 0xFFFF) | (b << 16)
    // Hi32 = (c & 0xFFFF) | (d << 16)
    // Then build <2 x s32> and bitcast to <4 x s16>.
    if (DstTy == LLT::fixed_vector(4, 16) &&
        SrcTy == LLT::fixed_vector(4, 32)) {
      MachineInstr *BuildVec = MRI.getVRegDef(Src);
      if (BuildVec && BuildVec->getOpcode() == G_BUILD_VECTOR &&
          BuildVec->getNumOperands() == 5) {
        const LLT S32 = LLT::scalar(32);
        Register Elem0 = BuildVec->getOperand(1).getReg();
        Register Elem1 = BuildVec->getOperand(2).getReg();
        Register Elem2 = BuildVec->getOperand(3).getReg();
        Register Elem3 = BuildVec->getOperand(4).getReg();

        // Constant 0xFFFF for masking and 16 for shift amount
        Register Mask16 = MRI.createGenericVirtualRegister(S32);
        MIB.buildConstant(Mask16, 0xFFFF);
        Register Shift16 = MRI.createGenericVirtualRegister(S32);
        MIB.buildConstant(Shift16, 16);

        // Lo32 = (Elem0 & 0xFFFF) | (Elem1 << 16)
        Register E0Masked = MRI.createGenericVirtualRegister(S32);
        MIB.buildAnd(E0Masked, Elem0, Mask16);
        Register E1Shifted = MRI.createGenericVirtualRegister(S32);
        MIB.buildShl(E1Shifted, Elem1, Shift16);
        Register Lo32 = MRI.createGenericVirtualRegister(S32);
        MIB.buildOr(Lo32, E0Masked, E1Shifted);

        // Hi32 = (Elem2 & 0xFFFF) | (Elem3 << 16)
        Register E2Masked = MRI.createGenericVirtualRegister(S32);
        MIB.buildAnd(E2Masked, Elem2, Mask16);
        Register E3Shifted = MRI.createGenericVirtualRegister(S32);
        MIB.buildShl(E3Shifted, Elem3, Shift16);
        Register Hi32 = MRI.createGenericVirtualRegister(S32);
        MIB.buildOr(Hi32, E2Masked, E3Shifted);

        // Build <2 x s32> = G_BUILD_VECTOR Lo32, Hi32
        // This is legal and will be selected to DR64.
        // Then bitcast <2 x s32> to <4 x s16> (same 64-bit register).
        const LLT V2S32 = LLT::fixed_vector(2, 32);
        Register V2 = MRI.createGenericVirtualRegister(V2S32);
        MIB.buildBuildVector(V2, {Lo32, Hi32});
        MIB.buildBitcast(Dst, V2);

        // Erase the G_TRUNC. The dead G_BUILD_VECTOR <4 x s32> will be
        // cleaned up by the legalizer's dead instruction elimination pass.
        MI.eraseFromParent();

        return true;
      }
    }
  }

  //===--------------------------------------------------------------------===
  // G_VECREDUCE_ADD — manually scalarize to avoid G_SHUFFLE_VECTOR.
  //===--------------------------------------------------------------------===
  // The legalizer table marks G_VECREDUCE_ADD as customFor({S32, V2I32}
  // {S16, V4I16}). We cannot use fewerElementsVectorReductions because it
  // generates G_SHUFFLE_VECTOR which Haydn does not support. Instead, we
  // manually split the vector via G_UNMERGE_VALUES and build a scalar add
  // tree. For v2i32 this produces:
  // %lo, %hi = G_UNMERGE_VALUES %vec
  // %sum = G_ADD %lo, %hi
  // The PostSelectOptimize pass will then recognize the MOV_DR64_TO_GPR +
  // ADD32 pattern and replace it with X2HADD32_L.
  if (MI.getOpcode() == G_VECREDUCE_ADD) {
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcVecReg = MI.getOperand(1).getReg();
    LLT DstTy = MRI.getType(DstReg);
    LLT SrcVecTy = MRI.getType(SrcVecReg);

    if (!SrcVecTy.isVector())
      return false;

    unsigned NumElems = SrcVecTy.getNumElements();
    LLT ElemTy = SrcVecTy.getElementType();

    // Extract all elements via G_UNMERGE_VALUES.
    SmallVector<Register, 4> Elems;
    if (NumElems == 2 && ElemTy.getSizeInBits() == 32) {
      // v2i32: unmerge into 2 x s32.
      Register Lo = MRI.createGenericVirtualRegister(ElemTy);
      Register Hi = MRI.createGenericVirtualRegister(ElemTy);
      MIB.buildUnmerge({Lo, Hi}, SrcVecReg);
      Elems.push_back(Lo);
      Elems.push_back(Hi);
    } else if (NumElems == 4 && ElemTy.getSizeInBits() == 16) {
      // v4i16: unmerge into 4 x s16.
      for (unsigned I = 0; I < 4; ++I) {
        Register E = MRI.createGenericVirtualRegister(ElemTy);
        Elems.push_back(E);
      }
      MIB.buildUnmerge(Elems, SrcVecReg);
    } else {
      return false;
    }

    // Build a balanced add tree.
    Register Accum = Elems[0];
    for (unsigned I = 1; I < Elems.size(); ++I) {
      Register Next = MRI.createGenericVirtualRegister(DstTy);
      MIB.buildAdd(Next, Accum, Elems[I]);
      Accum = Next;
    }

    MIB.buildCopy(DstReg, Accum);
    MI.eraseFromParent();
    return true;
  }

  //===--------------------------------------------------------------------===
  // G_EXTRACT_VECTOR_ELT / G_INSERT_VECTOR_ELT
  //===--------------------------------------------------------------------===
  // For v2i32/v4i16 types, the generic lowerExtractInsertVectorElt generates
  // G_UNMERGE_VALUES on vector types which the selector mishandles. Instead
  // we bitcast to s64 and use G_UNMERGE_VALUES s64->2xs32 (properly selected).
  if (MI.getOpcode() == G_EXTRACT_VECTOR_ELT) {
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcVec = MI.getOperand(1).getReg();
    Register IdxReg = MI.getOperand(2).getReg();
    LLT SrcVecTy = MRI.getType(SrcVec);

    // For 64-bit vector types, handle via bitcast to s64.
    if (SrcVecTy.isVector() && SrcVecTy.getSizeInBits() == 64) {
      int64_t IdxVal;
      if (mi_match(IdxReg, MRI, m_ICst(IdxVal)) &&
          IdxVal >= 0 &&
          static_cast<unsigned>(IdxVal) < SrcVecTy.getNumElements()) {
        const LLT S64 = LLT::scalar(64);
        LLT ElemTy = SrcVecTy.getElementType();
        unsigned NumElems = SrcVecTy.getNumElements();

        // Bitcast vector to s64, then unmerge to scalars.
        Register VecS64 = MRI.createGenericVirtualRegister(S64);
        MIB.buildBitcast(VecS64, SrcVec);
        SmallVector<Register, 4> Elems;
        for (unsigned I = 0; I < NumElems; ++I)
          Elems.push_back(MRI.createGenericVirtualRegister(ElemTy));
        MIB.buildUnmerge(Elems, VecS64);
        MIB.buildCopy(DstReg, Elems[IdxVal]);
        MI.eraseFromParent();
        return true;
      }
    }

    // Do NOT call lowerExtractInsertVectorElt on residual wide vectors:
    // it unmerges the whole vector to feed one extract, which re-creates
    // extracts (legalizer hang). Wide extracts must hit clampMaxNumElements
    // fewer-elements first; signal unable so the legalizer applies rules.
    if (SrcVecTy.isVector() && SrcVecTy.getSizeInBits() > 64)
      return false;

    // Fall back to generic lowering for other types / variable indices.
    return Helper.lowerExtractInsertVectorElt(MI) ==
           LegalizerHelper::Legalized;
  }

  if (MI.getOpcode() == G_INSERT_VECTOR_ELT) {
    return Helper.lowerExtractInsertVectorElt(MI) ==
           LegalizerHelper::Legalized;
  }

  //===--------------------------------------------------------------------===
  // G_UNMERGE_VALUES of wide residual vectors into smaller vectors
  // (fewer-elements intermediate: v4s32 → 2×v2s32, v16s32 → 8×v2s32, …).
  // Avoid bitcast-to-s512 thrash: build each part from element extracts.
  // EXTRACT/BUILD of residual vectors are clamped to native 64-bit shapes.
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == TargetOpcode::G_UNMERGE_VALUES) {
    const unsigned NumDefs = MI.getNumOperands() - 1;
    Register SrcReg = MI.getOperand(NumDefs).getReg();
    LLT SrcTy = MRI.getType(SrcReg);
    LLT DstTy = MRI.getType(MI.getOperand(0).getReg());
    if (!SrcTy.isVector() || !DstTy.isVector())
      return false;
    if (SrcTy.getElementType() != DstTy.getElementType())
      return false;
    if (SrcTy.getSizeInBits() <= 64)
      return false;
    if (NumDefs * DstTy.getNumElements() != SrcTy.getNumElements())
      return false;

    MIB.setInstrAndDebugLoc(MI);
    const LLT S32 = LLT::scalar(32);
    const LLT EltTy = DstTy.getElementType();
    const unsigned EltsPerPart = DstTy.getNumElements();
    for (unsigned P = 0; P < NumDefs; ++P) {
      SmallVector<Register, 8> Elts;
      Elts.reserve(EltsPerPart);
      for (unsigned E = 0; E < EltsPerPart; ++E) {
        auto Idx = MIB.buildConstant(S32, P * EltsPerPart + E);
        Elts.push_back(
            MIB.buildExtractVectorElement(EltTy, SrcReg, Idx).getReg(0));
      }
      MIB.buildBuildVector(MI.getOperand(P).getReg(), Elts);
    }
    MI.eraseFromParent();
    return true;
  }

  //===--------------------------------------------------------------------===
  // G_SHUFFLE_VECTOR — scalarize any residual size via extract + build.
  // Native 64-bit v2i32 keeps the bitcast-to-s64 path; wider/mismatched
  // SLP shuffles (v16s1 from v4s1, etc.) use extractelement.
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == G_SHUFFLE_VECTOR) {
    Register DstReg = MI.getOperand(0).getReg();
    Register Src1Reg = MI.getOperand(1).getReg();
    LLT DstTy = MRI.getType(DstReg);
    LLT SrcTy = MRI.getType(Src1Reg);

    if (!SrcTy.isVector() || !DstTy.isVector())
      return false;

    unsigned NumSrcElems = SrcTy.getNumElements();
    LLT ElemTy = SrcTy.getElementType();
    if (DstTy.getElementType() != ElemTy)
      return false;
    unsigned NumDstElems = DstTy.getNumElements();
    const LLT S32 = LLT::scalar(32);
    ArrayRef<int> Mask = MI.getOperand(3).getShuffleMask();

    auto extractLane = [&](Register Vec, unsigned Lane) -> Register {
      // Prefer unmerge of 64-bit native vectors (selector-friendly).
      if (MRI.getType(Vec).getSizeInBits() == 64 &&
          MRI.getType(Vec).getNumElements() == NumSrcElems &&
          NumSrcElems <= 4) {
        // Fall through to extract; bitcast path only for pure v2i32 identity
        // shapes handled below.
      }
      auto Idx = MIB.buildConstant(S32, Lane);
      return MIB.buildExtractVectorElement(ElemTy, Vec, Idx).getReg(0);
    };

    // Fast path: same-shape 64-bit vectors via s64 bitcast + unmerge.
    if (SrcTy == DstTy && SrcTy.getSizeInBits() == 64) {
      const LLT S64 = LLT::scalar(64);
      Register Src1S64 = MRI.createGenericVirtualRegister(S64);
      MIB.buildBitcast(Src1S64, Src1Reg);
      SmallVector<Register, 4> Src1Elems;
      for (unsigned I = 0; I < NumSrcElems; ++I)
        Src1Elems.push_back(MRI.createGenericVirtualRegister(ElemTy));
      MIB.buildUnmerge(Src1Elems, Src1S64);

      SmallVector<Register, 4> Src2Elems;
      bool NeedsSrc2 = llvm::any_of(Mask, [NumSrcElems](int M) {
        return M >= 0 && static_cast<unsigned>(M) >= NumSrcElems;
      });
      if (NeedsSrc2) {
        Register Src2Reg = MI.getOperand(2).getReg();
        Register Src2S64 = MRI.createGenericVirtualRegister(S64);
        MIB.buildBitcast(Src2S64, Src2Reg);
        for (unsigned I = 0; I < NumSrcElems; ++I)
          Src2Elems.push_back(MRI.createGenericVirtualRegister(ElemTy));
        MIB.buildUnmerge(Src2Elems, Src2S64);
      }

      SmallVector<Register, 4> ResultElems;
      for (unsigned I = 0; I < NumDstElems; ++I) {
        int MaskVal = Mask.size() > I ? Mask[I] : -1;
        if (MaskVal < 0) {
          Register UndefReg = MRI.createGenericVirtualRegister(ElemTy);
          MIB.buildUndef(UndefReg);
          ResultElems.push_back(UndefReg);
        } else if (static_cast<unsigned>(MaskVal) < NumSrcElems) {
          ResultElems.push_back(Src1Elems[MaskVal]);
        } else if (static_cast<unsigned>(MaskVal) < 2 * NumSrcElems &&
                   !Src2Elems.empty()) {
          ResultElems.push_back(
              Src2Elems[static_cast<unsigned>(MaskVal) - NumSrcElems]);
        } else {
          return false;
        }
      }
      MIB.buildBuildVector(DstReg, ResultElems);
      MI.eraseFromParent();
      return true;
    }

    // Residual SLP (including mismatched src/dst lengths, e.g. v4s1→v16s1).
    SmallVector<Register, 16> ResultElems;
    Register Src2Reg = MI.getOperand(2).getReg();
    for (unsigned I = 0; I < NumDstElems; ++I) {
      int MaskVal = Mask.size() > I ? Mask[I] : -1;
      if (MaskVal < 0) {
        Register UndefReg = MRI.createGenericVirtualRegister(ElemTy);
        MIB.buildUndef(UndefReg);
        ResultElems.push_back(UndefReg);
      } else if (static_cast<unsigned>(MaskVal) < NumSrcElems) {
        ResultElems.push_back(extractLane(Src1Reg, MaskVal));
      } else if (static_cast<unsigned>(MaskVal) < 2 * NumSrcElems) {
        ResultElems.push_back(
            extractLane(Src2Reg, static_cast<unsigned>(MaskVal) - NumSrcElems));
      } else {
        return false;
      }
    }
    MIB.buildBuildVector(DstReg, ResultElems);
    MI.eraseFromParent();
    return true;
  }

  //===--------------------------------------------------------------------===
  // G_DYN_STACKALLOC — T-ABI2. Round size to StackAlign(8) and mask SP
  // with -8. Peer: LegalizerHelper::getDynStackAllocTargetPtr
  // (LegalizerHelper.cpp:9234) used by RISCV .lower()
  // (RISCVLegalizerInfo.cpp:512-513). TLI stack-save register is unset
  // on Haydn (ISelLowering is out of scope); use architectural SP R13.
  // Alignment > StackAlign is fail-closed (no BP, ANDI32 is uimm20 ZEXT).
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == G_DYN_STACKALLOC) {
    MachineFunction &MF = *MI.getMF();
    const Align StackAlign =
        MF.getSubtarget().getFrameLowering()->getStackAlign();
    const Align Alignment = assumeAligned(MI.getOperand(2).getImm());
    if (Alignment > StackAlign)
      return false;

    Register Dst = MI.getOperand(0).getReg();
    Register Size = MI.getOperand(1).getReg();
    const LLT PtrTy = MRI.getType(Dst);
    const LLT SizeTy = MRI.getType(Size);

    MIB.setInstrAndDebugLoc(MI);

    // Round size up to StackAlign so SP never lands at ≡4 mod 8
    // (HaydnCallingConv MEMORY_FAULT). Idempotent if IRTranslator already
    // rounded (IRTranslator.cpp:3216-3225).
    auto Pad = MIB.buildConstant(SizeTy, StackAlign.value() - 1);
    auto Rounded = MIB.buildAdd(SizeTy, Size, Pad, MachineInstr::NoUWrap);
    auto SizeMask = MIB.buildConstant(
        SizeTy, static_cast<int64_t>(~(uint64_t)(StackAlign.value() - 1)));
    Register AlignedSize = MIB.buildAnd(SizeTy, Rounded, SizeMask).getReg(0);

    Register NewSP = Helper.getDynStackAllocTargetPtr(Haydn::R13, AlignedSize,
                                                      StackAlign, PtrTy);
    MIB.buildCopy(Haydn::R13, NewSP);
    MIB.buildCopy(Dst, NewSP);
    MI.eraseFromParent();
    return true;
  }

  return false;
}
