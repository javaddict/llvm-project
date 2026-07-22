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
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"

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
  getActionDefinitionsBuilder({G_ADD, G_SUB})
      .legalFor({S32, S64, V2I32, V4I16})
      .minScalar(0, S32)
      .maxScalar(0, S64)
      .widenScalarToNextPow2(0);

  // s32 mul selects to the real MUL64_LL + bank-move sequence (Haydn has no
  // native GPR32 multiply; removed the invented MUL32); s64 mul is
  // custom-lowered in legalizeCustom to native MUL64_LL (widening 32x32->64)
  // or a native schoolbook sequence of MUL64_LL partials (true 64x64) -- never
  // a __muldi3 libcall.
  // We use customFor, NOT legalFor or libcallFor: legalFor would route G_MUL
  // <s64> to the selector's LIBCALL_MUL64 fallback; libcallFor fails because
  // Haydn's call lowering doesn't handle the LLVM Type*-based arg splitting
  // for libcalls correctly.
  // G_MUL elementwise wrap:
  //   s32  — MUL64_LL low half (no native GPR32 mul)
  //   v2i32 — X2MULPL32 (golden: low 32 of each dual 32x32 product)
  //   v4i16 — NOT X4MUL16 (that is 2-dest 16x16->32 DSP mul). Scalarize
  //           to s16 then minScalar->s32. True ISA X4MUL16 is only via
  //           llvm.haydn.x4mul16 / haydn_x4mul16 (2-result dpair).
  getActionDefinitionsBuilder(G_MUL)
      .legalFor({S32, V2I32})
      .customFor({S64})
      .minScalar(0, S32)
      .maxScalar(0, S64)
      .widenScalarToNextPow2(0)
      .scalarize(0);

  // Haydn has no native division/remainder - use libcalls
  getActionDefinitionsBuilder({G_SDIV, G_UDIV, G_SREM, G_UREM, G_SDIVREM, G_UDIVREM})
      .libcallFor({S32, S64})
      .minScalar(0, S32)
      .maxScalar(0, S64);

  // G_UMULH — unsigned multiply high.
  // s64: custom (native MUL64_LL schoolbook, see legalizeCustom)
  // s32: lower (generic decomposition)
  // s8/s16/s24: custom (zero-extend operands to s32, MUL s32, LShr by DstBits
  // truncate). See legalizeCustom. s24 arises from 23-bit signed
  // bitfields (int member : 23 -> load i24). Fixes "unable to
  // legalize G_UMULH s8/s16/s24" (yarpgen seeds 10, 92).
  getActionDefinitionsBuilder(G_UMULH)
      .customFor({S8, S16, LLT::scalar(24), S64})
      .lowerFor({S32});

  // G_SMULH — signed multiply high. s32/s64 lower via the generic path;
  // narrow s8/s16/s24 are custom-lowered in legalizeCustom (sign-extend
  // operands to s32, MUL s32, AShr by DstBits, truncate). s24 arises from
  // 23-bit signed bitfields. Fixes G_SMULH s8/s16/s24 (yarpgen seeds 1, 6, 92).
  getActionDefinitionsBuilder(G_SMULH)
      .customFor({S8, S16, LLT::scalar(24)})
      .lowerFor({S32, S64});

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
      .clampScalar(0, S32, S64)
      .widenScalarToNextPow2(0);

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
      .minScalar(0, S32)
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
      .widenScalarToNextPow2(1)
      .clampScalar(1, S32, S64);

  getActionDefinitionsBuilder(G_SELECT)
      .legalFor({{S32, S1}, {S64, S1}, {P0, S1}, {V2I32, S1}, {V4I16, S1}, {V8I8, S1}})
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
      .legalFor({S32, S64, P0, V2I32, V4I16, V8I8})
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
  const LLT S128 = LLT::scalar(128);  // NOLINT(readability-identifier-naming)
  // s128 destination extensions (s64 -> s128) are custom-lowered in
  // legalizeCustom to G_MERGE_VALUES <src>, <zero|sext-high>. This is the
  // feeding extension for the i128 multiply that IR instcombine
  // AggressiveInstCombine forms from a schoolbook 64x64->128 split at -O2
  // The generic narrowScalar of G_MUL s128 already decomposes the
  // 128-bit multiply itself into s64 G_UNMERGE_VALUES + G_MUL/G_UMULH +
  // G_MERGE_VALUES; only the feeding s64->s128 extension lacked a rule.
  // See /.
  // Closed rule for extensions TO s32/s64 FROM any narrower scalar (incl.
  // non-pow2 s12/s24/s31 bitfield widths). The selector masks (ZEXT/ANYEXT)
  // or shift-sign-extends (SEXT) by SrcBits — not a hardcoded {1,8,16} set.
  // Narrow *results* (s8/s16) stay customFor below (cannot clampScalar dest).
  // legalIf is ordered after legalFor/customFor so those rules still win.
  getActionDefinitionsBuilder({G_SEXT, G_ZEXT, G_ANYEXT})
      .legalFor({{S32, S1}, {S32, S8}, {S32, S16}, {S64, S32},
                 {S64, S8}, {S64, S16}, {S64, S1}})
      .customFor({{S16, S1}, {S16, S8}, {S8, S1}, {S8, S8}, {S16, S16},
                  {S128, S64}})
      .legalIf([](const LegalityQuery &Query) {
        const LLT DstTy = Query.Types[0];
        const LLT SrcTy = Query.Types[1];
        if (!DstTy.isScalar() || !SrcTy.isScalar())
          return false;
        const unsigned DstBits = DstTy.getSizeInBits();
        const unsigned SrcBits = SrcTy.getSizeInBits();
        // Only full-register destinations; narrow results stay custom.
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
      .legalFor({{S1, S32}, {S1, S16}, {S1, S8}, {S1, S64},
                 {S8, S16}, {S16, S32}, {S8, S32},
                 {S32, S64}, {S16, S64}, {S8, S64}})
      .customFor({{V4I16, V4I32}})
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
                 {S64, V8I8}, {V8I8, S64}});

  //===--------------------------------------------------------------------===
  // Multi-value / Composite
  //===--------------------------------------------------------------------===
  getActionDefinitionsBuilder({G_MERGE_VALUES, G_UNMERGE_VALUES})
      .legalFor({{S32, S64}, {S64, S32}, {S32, V2I32}, {S16, V4I16},
                 {S16, S64}, {S64, S16}});

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
  // ISel still splits LD64 when MMO align < 8 (D_LDW needs 8). Vectors keep
  // type-only legality (unchanged). Selector keys opcode on MMO size.
  getActionDefinitionsBuilder({G_LOAD, G_STORE})
      .legalForTypesWithMemDesc({{S8, P0, S8, 8},
                                 {S16, P0, S16, 16},
                                 {S32, P0, S32, 32},
                                 {S64, P0, S64, 32},
                                 {P0, P0, P0, 32},
                                 // Anyext load / trunc store after splits.
                                 {S16, P0, S8, 8},
                                 {S32, P0, S8, 8},
                                 {S32, P0, S16, 16}})
      .legalFor({{V2I32, P0}, {V4I16, P0}, {V8I8, P0}})
      .minScalar(0, S8)
      .widenScalarToNextPow2(0, /*Min=*/8)
      .lowerIfMemSizeNotByteSizePow2()
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
  // For s64, the selector will decompose these operations
  getActionDefinitionsBuilder({G_INTTOPTR, G_PTRTOINT})
      .legalFor({{P0, S32}, {S32, P0}, {P0, S64}, {S64, P0}});

  //===--------------------------------------------------------------------===
  // Constants and Undef
  //===--------------------------------------------------------------------===
  // s32/s64/p0 constants are legal; selector handles materialization
  getActionDefinitionsBuilder(G_CONSTANT)
      .legalFor({S1, S8, S16, S32, S64, P0})
      .clampScalar(0, S32, S64)
      .widenScalarToNextPow2(0);

  getActionDefinitionsBuilder(G_IMPLICIT_DEF)
      .legalFor({S32, S64, P0, V2I32, V4I16, V8I8})
      .clampScalar(0, S32, S64)
      .widenScalarToNextPow2(0);

  //===--------------------------------------------------------------------===
  // Floating-point — all libcall (soft-float)
  //===--------------------------------------------------------------------===
  // G_FNEG / G_FABS are deliberately NOT in the bulk.libcallFor block below.
  // The upstream generic libcall path (LegalizerHelper::libcall in
  // llvm/lib/CodeGen/GlobalISel/LegalizerHelper.cpp) has NO case for G_FNEG or
  // G_FABS — they fall through to `default: return UnableToLegalize`, so
  // declaring `.libcallFor` for them makes the legalizer abort with
  // "unable to legalize instruction: %.._(s32) = G_FNEG/G_FABS". There is no
  // __negsf2 libcall (negation is a sign-bit flip), and although FABS_F32
  // FABS_F64 exist in RuntimeLibcalls.td, the generic libcall path does not
  // dispatch to them. The canonical soft-float lowering for both — used by
  // SelectionDAG on RISC-V / other soft-float targets — is the integer
  // bit-trick on the IEEE-754 bit-pattern: fneg = XOR with the sign bit
  // (0x8000...0), fabs = AND with the magnitude mask (0x7FFF...F). Since
  // Haydn stores every float in a GPR/DR64 as its bit-cast integer, we
  // custom-lower these to G_XOR / G_AND on the integer representation
  // (see legalizeCustom).
  getActionDefinitionsBuilder({
      G_FADD, G_FSUB, G_FMUL, G_FDIV, G_FREM,
      G_FMA, G_FMAD, G_FSQRT,
      G_FCOS, G_FSIN, G_FEXP, G_FLOG, G_FLOG2, G_FLOG10,
      G_FPOWI, G_FPOW,
      G_FCEIL, G_FFLOOR, G_FRINT, G_FNEARBYINT,
      G_FMINNUM, G_FMAXNUM,
      G_FPTRUNC, G_FPEXT,
      G_FPTOSI, G_FPTOUI, G_SITOFP, G_UITOFP,
      G_STRICT_FADD, G_STRICT_FSUB, G_STRICT_FMUL, G_STRICT_FDIV,
      G_STRICT_FREM, G_STRICT_FSQRT, G_STRICT_FMA, G_STRICT_FLDEXP,
  }).libcallFor({S32, S64});

  // G_FNEG / G_FABS — soft-float integer bit-manipulation lowering. See the
  // long comment above for why these cannot use.libcallFor, and the
  // G_FNEG / G_FABS case in legalizeCustom for the actual lowering.
  //
  // G_FCOPYSIGN is ALSO custom-lowered with an integer bit-trick (sign-bit
  // graft: result = (x & 0x7FFF..F) | (y & 0x8000..0)). Like FNEG/FABS, the
  // upstream generic libcall path (LegalizerHelper::libcall) has NO case for
  // G_FCOPYSIGN, so.libcallFor would abort with "unable to legalize". The
  // bit-trick is the canonical, IEEE-correct soft-float copysign (NaN sign
  // handled, since NaN carries a sign bit) and needs no runtime symbol..
  //
  // G_FMINIMUM / G_FMAXIMUM (llvm.minimum/maximum) and G_FMINIMUMNUM
  // G_FMAXIMUMNUM likewise cannot use the generic libcall path: FMINIMUM
  // FMAXIMUM have NO case in LegalizerHelper::libcall, and FMINIMUMNUM
  // FMAXIMUMNUM have a case but no RuntimeLibcallImpl in the.td (so
  // getLibcallName returns null). All four are custom-mapped to
  // G_FMINNUM/G_FMAXNUM (see legalizeCustom), which then take the libcall path.
  // C frontends emit FMINNUM (via __builtin_fmin), never these, so this mapping
  // only matters for IR using the intrinsics directly. The only difference is
  // NaN-signaling, which baremetal soft-float does not track.
  getActionDefinitionsBuilder({G_FNEG, G_FABS, G_FCOPYSIGN,
                               G_FMINIMUM, G_FMAXIMUM,
                               G_FMINIMUMNUM, G_FMAXIMUMNUM})
      .customFor({S32, S64});

  // G_FCONSTANT — soft-float constant materialization. A constant is not a
  // runtime libcall (libcallFor would crash the legalizer with "unable to
  // legalize instruction: G_FCONSTANT float 2.5"). Custom-lower by bitcasting
  // the float's bit-pattern to its integer representation and emitting a
  // G_CONSTANT of that integer bit-pattern in the float-typed destination vreg.
  // The selector materializes the integer constant and treats the result as a
  // float (since Haydn has no FPU, every float value lives in a GPR/DR64 as its
  // bit-cast integer). This mirrors RISC-V soft-float G_FCONSTANT lowering
  // (RISCVLegalizerInfo::legalizeCustom, case G_FCONSTANT).
  getActionDefinitionsBuilder(G_FCONSTANT)
      .customFor({S32, S64});

  // G_FCMP has a distinct type signature: {s1 result, s32/s64 operands}.
  // It cannot share the bulk.libcallFor({S32, S64}) rule above because that
  // rule checks type index 0 (the result), which is s1 for comparisons
  // not S32 or S64. Without this separate rule, G_FCMP fails to legalize
  // with "unable to legalize instruction".
  getActionDefinitionsBuilder(G_FCMP)
      .libcallFor({{S1, S32}, {S1, S64}});

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
  // G_VASTART: single p0 argument (va_list pointer)
  getActionDefinitionsBuilder(G_VASTART).legalFor({P0});

  // G_VAARG: kept LEGAL and selected by a custom handler in
  // HaydnInstructionSelector. The legalizer must NOT lower it to
  // generic G_LOAD/G_PTR_ADD: the two-bank varargs ABI needs bank-selection
  // (i64/f64 -> DR cursor __vr_top/__vr_offs via LD64_S1; else -> GPR cursor
  // __gr_top/__gr_offs via LD32) with per-bank overflow to __stack, which the
  // generic single-cursor lowering cannot express. The custom selector emits
  // concrete Haydn ops (LD32/LD64_S1/ST32/SUB32/ADDI32) and constrains each.
  getActionDefinitionsBuilder(G_VAARG).alwaysLegal();

  //===--------------------------------------------------------------------===
  // Extended load/store — peer AIE2LegalizerInfo / RISCV ExtLoadActions
  //===--------------------------------------------------------------------===
  getActionDefinitionsBuilder({G_SEXTLOAD, G_ZEXTLOAD})
      .legalForTypesWithMemDesc({{S32, P0, S8, 8},
                                 {S32, P0, S16, 16},
                                 {S16, P0, S8, 8}})
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

  getActionDefinitionsBuilder({
      G_ABS,
      G_UADDO, G_USUBO, G_SMULO, G_UMULO,
      G_SADDO, G_SSUBO, G_UADDE, G_USUBE, G_SADDE, G_SSUBE,
      G_UADDSAT, G_SADDSAT, G_USUBSAT, G_SSUBSAT,
      G_USHLSAT, G_SSHLSAT,
      G_ROTL, G_ROTR, G_FSHL, G_FSHR,
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
      .minScalar(0, S16)
      .maxScalar(0, S64);

  // G_SMIN/G_SMAX/G_UMIN/G_UMAX — native MAX32/MIN32/MAXU32/MINU32 for s32.
  // s64 is lowered to the s32 ops via hi/lo decomposition.
  getActionDefinitionsBuilder({G_SMIN, G_SMAX, G_UMIN, G_UMAX})
      .legalFor({S32})
      .lowerFor({S64})
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

  // Control/misc with type idx (pointers / i32)
  getActionDefinitionsBuilder({
      G_PREFETCH,
      G_BLOCK_ADDR, G_JUMP_TABLE, G_BRINDIRECT, G_BRJT,
      G_DYN_STACKALLOC,
      G_READ_REGISTER, G_WRITE_REGISTER,
      G_READCYCLECOUNTER, G_READSTEADYCOUNTER,
      G_STACKSAVE, G_STACKRESTORE,
  }).legalFor({S32, P0});

  // FP misc
  getActionDefinitionsBuilder({
      G_INTRINSIC_TRUNC, G_INTRINSIC_ROUND, G_INTRINSIC_ROUNDEVEN,
      G_INTRINSIC_LRINT, G_INTRINSIC_LLRINT,
      G_INTRINSIC_FPTRUNC_ROUND,
  }).lowerFor({S32, S64});

  getActionDefinitionsBuilder(G_IS_FPCLASS)
      .legalFor({{S1, S32}, {S1, S64}});

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
      .customFor({{V4I16, S16}, {V8I8, S8}})
      .lowerFor({S32, S64});

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
      .lowerFor({S32, S64});

  // G_INSERT_VECTOR_ELT: custom for v2i32 and v4i16.
  // Expanded in legalizeCustom to G_UNMERGE_VALUES + shift/mask/merge.
  getActionDefinitionsBuilder(G_INSERT_VECTOR_ELT)
      .customFor({{V2I32, S32}, {V4I16, S16}})
      .lowerFor({S32, S64});

  //===--------------------------------------------------------------------===
  // G_SHUFFLE_VECTOR — custom for v2i32 (scalarize via extract+build)
  //===--------------------------------------------------------------------===
  // The LLVM "expand-reductions" pass converts llvm.vector.reduce.add.v2i32
  // into shufflevector + add + extractelement at the IR level, before GISel.
  // We never see G_VECREDUCE_ADD; we see G_SHUFFLE_VECTOR instead. Custom
  // scalarize v2i32 shuffles by extracting elements and rebuilding the vector.
  getActionDefinitionsBuilder(G_SHUFFLE_VECTOR)
      .customFor({{V2I32, V2I32}})
      .lowerFor({S32, S64});

  //===--------------------------------------------------------------------===
  // Vector ops — unsupported (non-SIMD vector types)
  //===--------------------------------------------------------------------===
  getActionDefinitionsBuilder({
      G_BUILD_VECTOR_TRUNC,
      G_CONCAT_VECTORS,
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

  getActionDefinitionsBuilder(G_PTRAUTH_GLOBAL_VALUE).legalFor({P0});
}

// Lower a true 64x64->64 multiply (G_MUL <s64> with at least one non-widened
// operand) to a native schoolbook sequence using the MUL64_* partial-product
// family. See the G_MUL block in legalizeCustom for the derivation.
// Decomposition (result mod 2^64):
// aLo, aHi = unmerge a; bLo, bHi = unmerge b (each half s32)
// LL = (zext aLo) * (zext bLo) -- low x low, full 64-bit UNSIGNED product
// LH = (zext aLo) * (zext bHi) -- low x high
// HL = (zext aHi) * (zext bLo) -- high x low
// result = LL + ((LH + HL) << 32)
// (aHi*bHi contributes only to bits >= 64, dropped by the mod-2^64 result.)
// The three inner G_MUL <s64> ops each have two zext-of-s32 operands, so when
// the legalizer re-visits them they take the widening-multiply branch and
// lower to a single MUL64_ULL (unsigned x unsigned). The cross and LL partials
// MUST be unsigned because G_MUL <s64> is signless at the IR level -- the low
// 64 bits of the product are the same whether the operands are interpreted as
// signed or unsigned, but only the UNSIGNED 32x32->64 widening produces the
// correct high-32 bits of each partial (a SIGNED widening sign-extends the
// wrong half when bit 31 of a partial operand is set). (MurmurHash3
// finalizer on uint64_t) was the trigger: the previous sext + MUL64_LL path
// corrupted the high 32 bits of every round's products. The zext/unmerge
// shl/add ops are all already handled by the Haydn selector.
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
  // widening-multiply branch (both zext-of-s32) and lower to MUL64_ULL
  // (unsigned x unsigned). Zero extension is REQUIRED here, not sign
  // extension: G_MUL <s64> is signless, and only the unsigned 32x32->64
  // widening produces the correct high 32 bits of each partial product. With
  // sign extension the previous code selected MUL64_LL (signed x signed)
  // which sign-extends each operand's wrong half and corrupts any product
  // whose partials have bit 31 set in their low-32 operands.
  auto ZextTo64 = [&](Register R) {
    Register Ext = MRI.createGenericVirtualRegister(S64);
    MIB.buildZExt(Ext, R);
    return Ext;
  };
  Register ALo64 = ZextTo64(ALo);
  Register AHi64 = ZextTo64(AHi);
  Register BLo64 = ZextTo64(BLo);
  Register BHi64 = ZextTo64(BHi);

  // Three 32x32->64 widening partial products. Each lowers to MUL64_ULL.
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

bool HaydnLegalizerInfo::legalizeCustom(LegalizerHelper &Helper,
                                        MachineInstr &MI,
                                        LostDebugLocObserver &LocObserver) const {
  using namespace TargetOpcode;
  MachineIRBuilder &MIB = Helper.MIRBuilder;
  MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();

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
      Register Src = MI.getOperand(1).getReg();
      // Source must be s64; any other source width would need a different
      // decomposition. The legalizer table only declares customFor for
      // {{S128, S64}}, so this is the only shape that reaches here.
      if (MRI.getType(Src).getSizeInBits() != 64)
        return false;

      Register Hi64 = MRI.createGenericVirtualRegister(S64);
      if (MI.getOpcode() == G_SEXT) {
        // sign-extend-high: Hi64 = ASHR Src, 63
        Register ShAmt63 = MRI.createGenericVirtualRegister(LLT::scalar(32));
        MIB.buildConstant(ShAmt63, 63);
        MIB.buildAShr(Hi64, Src, ShAmt63);
      } else {
        // G_ZEXT / G_ANYEXT: high half is zero.
        MIB.buildConstant(Hi64, 0);
      }
      MIB.buildMergeLikeInstr(DstReg, {Src, Hi64});
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
  // G_MUL s64 -- widening 32x32->64 selects MUL64_LL (signed) or MUL64_ULL
  // (unsigned) by operand extension kind; true 64x64 lowers to a native
  // schoolbook sequence of MUL64_ULL (unsigned) partial products (
  // No __muldi3.
  //===--------------------------------------------------------------------===
  // Haydn has no single 64x64->64 multiply, but the MUL64_* family computes
  // each 32x32->64 partial product natively: MUL64_LL = signed x signed
  // (spec slot1_mac_opcode_table.md:59), MUL64_ULL = unsigned x unsigned
  // (:66). For the common C pattern `(int64_t)(int32_t)a * b` (operands are
  // G_SEXT of s32 values) we emit MUL64_LL directly; for `(uint64_t)(uint32_t)
  // a * b` (G_ZEXT) we emit MUL64_ULL. This avoids the LIBCALL_MUL64 ->
  // __muldi3 path that has no runtime stub and crashes at runtime (
  // bug #19). The legalizer declares G_MUL s64 as customFor({S64}) so this
  // runs. For true 64x64 multiply (at least one operand is NOT a widened
  // s32), we lower to a native schoolbook sequence of three MUL64_ULL
  // partials + add + shl, again avoiding the libcall path.
  // We use customFor instead of libcallFor because the generic GISel libcall
  // path fails for this target (Haydn's call lowering doesn't handle the
  // LLVM Type*-based arg splitting correctly for libcalls).
  if (MI.getOpcode() == G_MUL) {
    Register DstReg = MI.getOperand(0).getReg();
    Register Src0 = MI.getOperand(1).getReg();
    Register Src1 = MI.getOperand(2).getReg();
    LLT DstTy = MRI.getType(DstReg);

    if (DstTy.getSizeInBits() == 64) {
      const auto &TRI = *MI.getMF()->getSubtarget().getRegisterInfo();
      const auto &TII = *MI.getMF()->getSubtarget().getInstrInfo();
      auto &RBI = *MI.getMF()->getSubtarget().getRegBankInfo();

      // Widening 32x32->64 multiply: lower to a native MUL64 widening op when
      // both operands trace to a widened s32 value (the `(int64_t)(int32_t)a * b`
      // shape for SIGNED widening, or `(uint64_t)(uint32_t)a * b` for UNSIGNED).
      // This kills the LIBCALL_MUL64 -> __muldi3 path that has no runtime stub
      // and crashes at runtime (jal resolves to ELF symbol index 0). See.
      //
      // Sign vs unsigned matters: MUL64_LL is signed x signed 32x32->64 (spec
      // §MAC, slot1_mac_opcode_table.md:59), MUL64_ULL is unsigned x unsigned
      // (slot1_mac_opcode_table.md:66). For a sign-extended s32 operand both
      // give the correct low-32-bits-influenced 64-bit product ONLY when the
      // operand's sign bit (bit 31) is clear, OR when both operands are signed
      // (the signed product matches the signed widening multiply). But for a
      // ZERO-extended (unsigned) operand whose value has bit 31 set, MUL64_LL
      // sign-extends the wrong half and produces the wrong high 32 bits -- the
      // `uint32_t * uint32_t -> uint64_t` widening must use MUL64_ULL.
      // (MurmurHash3 finalizer `x *= K` on uint64_t) was the trigger: the
      // schoolbook path sext'd the operands, so all three partials selected
      // MUL64_LL and corrupted the high 32 bits of each round's result.
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
        // Select the partial-product opcode matching the operand extension
        // kind. SEXT (signed widening) -> MUL64_LL (signed x signed). ZEXT or
        // ANYEXT (unsigned widening) -> MUL64_ULL (unsigned x unsigned). Mixed
        // signedness cannot arise from a single widening multiply; fall back to
        // the schoolbook path below if it ever did.
        bool BothSigned = (Ext0 == TargetOpcode::G_SEXT &&
                           Ext1 == TargetOpcode::G_SEXT);
        bool BothUnsigned =
            (Ext0 == TargetOpcode::G_ZEXT || Ext0 == TargetOpcode::G_ANYEXT) &&
            (Ext1 == TargetOpcode::G_ZEXT || Ext1 == TargetOpcode::G_ANYEXT);
        if (BothSigned || BothUnsigned) {
          // CSE profiles include regclass/bank. Never raw
          // RBI.constrainGenericRegister on existing vregs while Legalizer CSE
          // is live — it desyncs CSEMap (Combiner assert on G_SEXT/G_ZEXT).
          // constrainSelectedInstRegOperands uses MF.getObserver() (AIE /
          // upstream Utils.cpp pattern).
          // MUL64_ULL is unsigned x SIGNED per the ISA (rs2 sign-extended);
          // fully-unsigned low×low is MUL64_ULUL.
          unsigned MulOpc = BothSigned ? Haydn::MUL64_LL : Haydn::MUL64_ULUL;
          MachineInstr *NewMI =
              MIB.buildInstr(MulOpc, {DstReg}, {Src0, Src1});
          constrainSelectedInstRegOperands(*NewMI, TII, TRI, RBI);
          MI.eraseFromParent();
          return true;
        }
      }

      // True 64x64 multiply (at least one operand is not a widened s32).
      // Haydn has no single 64x64->64 multiply, but the MUL64_* family computes
      // each 32x32->64 partial product natively (MUL64_LL = aLo*bLo etc.). We
      // decompose G_MUL <s64> into the schoolbook form using generic G_MIR ops
      // that the selector already lowers to native instructions:
      //
      // aLo, aHi = G_UNMERGE_VALUES a (2 x s32)
      // bLo, bHi = G_UNMERGE_VALUES b (2 x s32)
      // LL = (sext aLo) * (sext bLo) -> MUL64_LL (widening path)
      // LH = (sext aLo) * (sext bHi) -> MUL64_LL (widening path)
      // HL = (sext aHi) * (sext bLo) -> MUL64_LL (widening path)
      // result = LL + ((LH + HL) << 32) (mod 2^64)
      //
      // The HH partial (aHi*bHi) only contributes to bits >= 64 and is dropped
      // by the mod-2^64 result. The three inner G_MUL <s64> ops each have two
      // sext-of-s32 operands, so they re-enter this handler via the
      // IsWidenedFromS32 branch above and lower to a single MUL64_LL -- no
      // recursion, no libcall. This eliminates the LIBCALL_MUL64 -> __muldi3
      // path that had no runtime stub. See mul-i64-native-all-shapes.ll.
      lowerMul64Schoolbook(MIB, MRI, MI);
      return true;
    }
    return false;
  }

  //===--------------------------------------------------------------------===
  // G_UMULH s64 — high 64 bits of unsigned 64x64 multiply, via native
  // MUL64_LL partial products (schoolbook). Required by the generic
  // PreLegalizerCombiner's udiv-by-constant strength reduction, which
  // expands `udiv X, K` to `(UMULH X, magic) >> shift`.
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
    // (MUL64_LL). Zero extension is correct for UMULH.
    auto ZextTo64 = [&](Register R) {
      Register Ext = MRI.createGenericVirtualRegister(S64);
      MIB.buildZExt(Ext, R);
      return Ext;
    };
    Register ALo64 = ZextTo64(ALo);
    Register AHi64 = ZextTo64(AHi);
    Register BLo64 = ZextTo64(BLo);
    Register BHi64 = ZextTo64(BHi);

    // Four 32x32->64 widening partial products. Each lowers to MUL64_LL.
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

  //===--------------------------------------------------------------------===
  // G_FCONSTANT — soft-float constant materialization via bitcast-to-int.
  //===--------------------------------------------------------------------===
  // Haydn has no FPU, so every float value is stored in a GPR/DR64 as its
  // bit-cast integer. Materialize the float constant by extracting its APInt
  // bit-pattern (FVal.bitcastToAPInt) and emitting a G_CONSTANT with that
  // integer value into the original float-typed destination vreg. The
  // register-bank/selector then treats the result like any other integer
  // constant. This is the standard soft-float G_FCONSTANT idiom (matches
  // RISCVLegalizerInfo::legalizeCustom case G_FCONSTANT). See.
  if (MI.getOpcode() == G_FCONSTANT) {
    const APFloat &FVal = MI.getOperand(1).getFPImm()->getValueAPF();
    Register DstReg = MI.getOperand(0).getReg();
    MIB.buildConstant(DstReg, FVal.bitcastToAPInt());
    MI.eraseFromParent();
    return true;
  }

  //===--------------------------------------------------------------------===
  // G_FNEG / G_FABS — soft-float integer bit-manipulation lowering.
  //===--------------------------------------------------------------------===
  // Haydn has no FPU, so every float value lives in a GPR/DR64 as its IEEE-754
  // bit-cast integer. The standard soft-float idiom (matches SelectionDAG
  // ISel lowering for ISD::FNEG / ISD::FABS on RISC-V and other soft-float
  // targets) is to manipulate the bit-pattern directly:
  // fneg(x) = bitcast<x->iN> XOR 0x8000...0 (flip the sign bit)
  // fabs(x) = bitcast<x->iN> AND 0x7FFF...F (clear the sign bit)
  // The destination vreg is float-typed but lives in the same storage as an
  // integer; we emit the bit-trick into it directly. No libcall is involved
  // (no __negsf2 exists; FABS libcall is not in the upstream GISel libcall
  // dispatch — see LegalizerHelper::libcall). See,.
  if (MI.getOpcode() == G_FNEG || MI.getOpcode() == G_FABS) {
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg = MI.getOperand(1).getReg();
    LLT DstTy = MRI.getType(DstReg);
    unsigned BitWidth = DstTy.getSizeInBits();

    // Build the integer mask: sign-bit-only (XOR) for fneg, all-magnitude
    // (AND) for fabs. For a 32-bit float: 0x80000000 (sign) / 0x7FFFFFFF
    // (magnitude). For a 64-bit double: 0x8000000000000000
    // 0x7FFFFFFFFFFFFFFF. APInt handles the wide-constant construction.
    APInt Mask;
    if (MI.getOpcode() == G_FNEG)
      Mask = APInt::getSignMask(BitWidth);
    else
      Mask = APInt::getBitsSet(BitWidth, 0, BitWidth - 1);

    Register MaskReg = MRI.createGenericVirtualRegister(DstTy);
    MIB.buildConstant(MaskReg, Mask);

    if (MI.getOpcode() == G_FNEG)
      MIB.buildXor(DstReg, SrcReg, MaskReg);
    else
      MIB.buildAnd(DstReg, SrcReg, MaskReg);

    MI.eraseFromParent();
    return true;
  }

  //===--------------------------------------------------------------------===
  // G_FCOPYSIGN — soft-float sign-bit graft (no runtime symbol)..
  //===--------------------------------------------------------------------===
  // copysign(x, y) = keep x's magnitude, take y's sign:
  // result = (x & 0x7FFF...F) | (y & 0x8000...0)
  // Like FNEG/FABS this is the canonical integer bit-trick on the IEEE-754
  // bit-pattern (NaN sign is handled correctly since NaN carries a sign bit).
  // No libcall: G_FCOPYSIGN has no case in LegalizerHelper::libcall.
  if (MI.getOpcode() == G_FCOPYSIGN) {
    Register DstReg = MI.getOperand(0).getReg();
    Register XReg = MI.getOperand(1).getReg();  // magnitude source
    Register YReg = MI.getOperand(2).getReg();  // sign source
    LLT DstTy = MRI.getType(DstReg);
    unsigned BitWidth = DstTy.getSizeInBits();

    APInt MagMask = APInt::getBitsSet(BitWidth, 0, BitWidth - 1);  // 0x7FFF..F
    APInt SignMask = APInt::getSignMask(BitWidth);                 // 0x8000..0

    Register MagMaskReg = MRI.createGenericVirtualRegister(DstTy);
    Register SignMaskReg = MRI.createGenericVirtualRegister(DstTy);
    MIB.buildConstant(MagMaskReg, MagMask);
    MIB.buildConstant(SignMaskReg, SignMask);

    Register MagPart = MRI.createGenericVirtualRegister(DstTy);
    Register SignPart = MRI.createGenericVirtualRegister(DstTy);
    MIB.buildAnd(MagPart, XReg, MagMaskReg);
    MIB.buildAnd(SignPart, YReg, SignMaskReg);
    MIB.buildOr(DstReg, MagPart, SignPart);

    MI.eraseFromParent();
    return true;
  }

  // G_FMINIMUM / G_FMAXIMUM / G_FMINIMUMNUM / G_FMAXIMUMNUM — map to
  // G_FMINNUM / G_FMAXNUM..
  // FMINIMUM/FMAXIMUM have no case in the generic libcall switch, and
  // FMINIMUMNUM/FMAXIMUMNUM have no RuntimeLibcallImpl (getLibcallName null).
  // All four are mapped to FMINNUM/FMAXNUM, which take the standard libcall
  // path. C frontends emit FMINNUM (__builtin_fmin), never these; the only
  // difference is NaN-signaling, which baremetal soft-float does not track.
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

    // Fall back to generic lowering for other types / variable indices.
    return Helper.lowerExtractInsertVectorElt(MI) ==
           LegalizerHelper::Legalized;
  }

  if (MI.getOpcode() == G_INSERT_VECTOR_ELT) {
    return Helper.lowerExtractInsertVectorElt(MI) ==
           LegalizerHelper::Legalized;
  }

  //===--------------------------------------------------------------------===
  // G_SHUFFLE_VECTOR — lower for v2i32 by bitcasting to s64 and using
  // G_UNMERGE_VALUES s64 (not v2i32) to extract elements. This avoids
  // the selector bug with G_UNMERGE_VALUES on vector types.
  //===--------------------------------------------------------------------===
  if (MI.getOpcode() == G_SHUFFLE_VECTOR) {
    Register DstReg = MI.getOperand(0).getReg();
    Register Src1Reg = MI.getOperand(1).getReg();
    LLT DstTy = MRI.getType(DstReg);
    LLT SrcTy = MRI.getType(Src1Reg);

    if (!SrcTy.isVector() || SrcTy != DstTy)
      return false;
    if (SrcTy.getSizeInBits() != 64)
      return false;

    unsigned NumSrcElems = SrcTy.getNumElements();
    LLT ElemTy = SrcTy.getElementType();
    unsigned NumDstElems = DstTy.getNumElements();
    const LLT S64 = LLT::scalar(64);

    // Extract source 1 elements via bitcast to s64 + G_UNMERGE_VALUES s64.
    Register Src1S64 = MRI.createGenericVirtualRegister(S64);
    MIB.buildBitcast(Src1S64, Src1Reg);
    SmallVector<Register, 4> Src1Elems;
    for (unsigned I = 0; I < NumSrcElems; ++I)
      Src1Elems.push_back(MRI.createGenericVirtualRegister(ElemTy));
    MIB.buildUnmerge(Src1Elems, Src1S64);

    // Extract source 2 elements if any mask entry references them.
    SmallVector<Register, 4> Src2Elems;
    ArrayRef<int> Mask = MI.getOperand(3).getShuffleMask();
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

    // Build result elements per the mask.
    SmallVector<Register, 4> ResultElems;
    for (unsigned I = 0; I < NumDstElems; ++I) {
      int MaskVal = Mask.size() > I ? Mask[I] : -1;
      if (MaskVal < 0) {
        // Undef lane — use G_IMPLICIT_DEF to avoid a false dependency on any
        // source element. Substituting Src1Elems[0] created a data dependency
        // on element 0 of the source vector, which the scheduler / register
        // allocator cannot elide.
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

  return false;
}
