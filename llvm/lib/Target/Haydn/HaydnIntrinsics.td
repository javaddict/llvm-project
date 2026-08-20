//===-- HaydnIntrinsics.td - Haydn Intrinsic ISel Patterns ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ISel patterns mapping llvm.haydn.* intrinsics to Haydn machine instructions.
// These patterns are used by the tablegen-generated GlobalISel match table.
//
// IMPORTANT: The operand order in each Pat must match the instruction definition
// in HaydnInstrInfo.td / HaydnInstrInfoManual.td exactly.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 32x32->64 Multiply (mul64.ll) — i32 x i32 -> i64
// This intrinsic takes i32 operands but the instruction uses DR64 registers.
// Selection is handled by custom C++ in HaydnInstructionSelector::selectIntrinsic
// because the type cross (GPR32 -> DR64) requires SEXT32 + MOV_GPR_TO_DR64.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 64-bit Multiply (MUL64) — binary DR64 instructions
// Mapping: intrinsic suffix -> instruction suffix
// ss_ll -> LL, ss_lh -> LH, ss_hl -> HL, ss_hh -> HH
// su_lul -> LUL, su_ulh -> ULH, su_uhl -> UHL, su_uhh -> UHH
// us_luh -> LUH, us_uhuh -> UHUH, us_hul -> HUL, us_uhul -> UHUL
// uu_uluh -> ULUH, uu_ulul -> ULUL, uu_ull -> ULL, uu_ulh -> ULH
//===----------------------------------------------------------------------===//

// Signed-signed
def : Pat<(int_haydn_mul64_ss_ll DR64:$a, DR64:$b),  (MUL64_LL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_ss_lh DR64:$a, DR64:$b),  (MUL64_LH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_ss_hl DR64:$a, DR64:$b),  (MUL64_HL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_ss_hh DR64:$a, DR64:$b),  (MUL64_HH DR64:$a, DR64:$b)>;

// Signed-unsigned
def : Pat<(int_haydn_mul64_su_lul DR64:$a, DR64:$b), (MUL64_LUL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_su_ulh DR64:$a, DR64:$b), (MUL64_ULH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_su_uhl DR64:$a, DR64:$b), (MUL64_UHL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_su_uhh DR64:$a, DR64:$b), (MUL64_UHH DR64:$a, DR64:$b)>;

// Unsigned-signed
def : Pat<(int_haydn_mul64_us_luh DR64:$a, DR64:$b),  (MUL64_LUH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_us_uhuh DR64:$a, DR64:$b), (MUL64_UHUH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_us_hul DR64:$a, DR64:$b),  (MUL64_HUL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_us_uhul DR64:$a, DR64:$b), (MUL64_UHUL DR64:$a, DR64:$b)>;

// Unsigned-unsigned
def : Pat<(int_haydn_mul64_uu_uluh DR64:$a, DR64:$b), (MUL64_ULUH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_uu_ulul DR64:$a, DR64:$b), (MUL64_ULUL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_uu_ull DR64:$a, DR64:$b),  (MUL64_ULL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_uu_ulh DR64:$a, DR64:$b),  (MUL64_ULH DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Accumulate/Subtract families (MULA64/MULS64/MULAS64/MULSS64).
//
// These instructions are FmtALU64Acc tied-def accumulators : the silicon
// op is `OPC rtd, rsd1, rsd2` where rtd is both source (accumulator) and
// destination (DB behavior: `rtd = rtd ± (...)`). They CANNOT be expressed as
// 2-operand Pats — the accumulator input is a tied-def that GISel Pats can't
// represent. The signedness-tagged intrinsics (mula64_su_*, _us_*, _uu_* and
// the muls64/mulas64/mulss64 equivalents) are wired via `selectAccMAC` switch
// cases in HaydnInstructionSelector.cpp.
//
// The 2-operand Pats that were here (targeting MULA64_LUL/ULH/... etc.) were
// stale: they fed 2 operands to what are now 3-input instructions, and they
// dropped the accumulator (the silent-miscompute class — see).
// Removed in; the correct selectAccMAC wiring for the full signedness
// matrix lands in Workstream A step A2.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Saturating Arithmetic
// Header composites satsr64 / packsr32 are not Pats (not golden encodings).
// *r/*rs rounding bias is 1<<(shift-1) then ASR; *s sat-left is header-soft.
// X4MULA16S is four-lane sat 16x16->32 (vec_dot16 dest-typed AE_MULAF16X4SS).
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_add32s GPR32:$a, GPR32:$b), (ADD32S GPR32:$a, GPR32:$b)>;
def : Pat<(int_haydn_sub32s GPR32:$a, GPR32:$b), (SUB32S GPR32:$a, GPR32:$b)>;
def : Pat<(int_haydn_add64s DR64:$a, DR64:$b),   (ADD64S DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_sub64s DR64:$a, DR64:$b),   (SUB64S DR64:$a, DR64:$b)>;

// llvm.sadd.sat / llvm.ssub.sat → golden signed-sat encodings.
// AIE lowers these (AIE2LegalizerInfo.cpp:288-291). Haydn overlay matches
// HexagonPatterns.td:1570 A2_addsat / RISCVInstrInfoXqci.td:1548 QC_ADDSAT.
def : Pat<(i32 (saddsat GPR32:$a, GPR32:$b)), (ADD32S GPR32:$a, GPR32:$b)>;
def : Pat<(i32 (ssubsat GPR32:$a, GPR32:$b)), (SUB32S GPR32:$a, GPR32:$b)>;
def : Pat<(i64 (saddsat DR64:$a, DR64:$b)), (ADD64S DR64:$a, DR64:$b)>;
def : Pat<(i64 (ssubsat DR64:$a, DR64:$b)), (SUB64S DR64:$a, DR64:$b)>;
def : Pat<(v2i32 (saddsat DR64:$a, DR64:$b)), (X2ADD32S DR64:$a, DR64:$b)>;
def : Pat<(v2i32 (ssubsat DR64:$a, DR64:$b)), (X2SUB32S DR64:$a, DR64:$b)>;
def : Pat<(v4i16 (saddsat DR64:$a, DR64:$b)), (X4ADD16S DR64:$a, DR64:$b)>;
def : Pat<(v4i16 (ssubsat DR64:$a, DR64:$b)), (X4SUB16S DR64:$a, DR64:$b)>;

// ABS32S uses FmtALU32 with 2 GPR32 ins (rs1 is source, rs2 unused)
def : Pat<(int_haydn_abs32s GPR32:$a),  (ABS32S GPR32:$a, GPR32:$a)>;
def : Pat<(int_haydn_neg32s GPR32:$a),  (NEG32S GPR32:$a)>;
def : Pat<(int_haydn_abs64s DR64:$a),   (ABS64S DR64:$a)>;
def : Pat<(int_haydn_neg64s DR64:$a),   (NEG64S DR64:$a)>;
def : Pat<(int_haydn_abs64 DR64:$a),    (ABS64 DR64:$a)>;
def : Pat<(int_haydn_neg64 DR64:$a),    (NEG64 DR64:$a)>;

//===----------------------------------------------------------------------===//
// Fractional Multiply
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_fmul32s_ll DR64:$a, DR64:$b), (FMUL32S_LL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul32s_lh DR64:$a, DR64:$b), (FMUL32S_LH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul32s_hh DR64:$a, DR64:$b), (FMUL32S_HH DR64:$a, DR64:$b)>;

// FmtALU64Acc tied-def: acc is $rd_in. GISel selectAccMAC is the live path;
// SDAG/GISel Pats kept for the full LL/LH/HH matrix (was only FMULS32S_LH).
def : Pat<(int_haydn_fmula32s_ll DR64:$acc, DR64:$a, DR64:$b),
          (FMULA32S_LL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmula32s_lh DR64:$acc, DR64:$a, DR64:$b),
          (FMULA32S_LH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmula32s_hh DR64:$acc, DR64:$a, DR64:$b),
          (FMULA32S_HH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls32s_ll DR64:$acc, DR64:$a, DR64:$b),
          (FMULS32S_LL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls32s_lh DR64:$acc, DR64:$a, DR64:$b),
          (FMULS32S_LH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls32s_hh DR64:$acc, DR64:$a, DR64:$b),
          (FMULS32S_HH DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// FF2 Fractional Multiply with Symmetric Rounding (F2MUL32RS)
// Maps to F2MUL* instructions in HaydnInstrInfoManual.td.
// Naming: FF2 intrinsic -> F2MUL* instruction (the "F" prefix is dropped in
// the ISA; "FF2" = "Fractional Format-2" in the LLVM intrinsic namespace).
//
// The FF2* intrinsic family maps by name to the FF2* instruction family
// (single product per lane) — NOT the dual-product F2MULAA32RS_*/F2MULSS32RS_*
// family. Verified against haydn_instruction_db.json:
// FF2MUL32RS_LH (line 4429): temp = LL×HH; rtd = r(temp) — one product
// FF2MULA32RS_LH (line 4678): temp = LL×HH; rtd += r(temp) — one product, accumulate
// F2MULAA32RS_HHLL (line 3083): two products (HH×HH + LL×LL) — dual MAC
// Earlier mappings routed ff2mul32rs_* to F2MULAA32RS_* which doubled the
// work and changed the result; corrected per codex review
// (m6-codex-destructive-acc-lanemap.md §2(ii) / m6-codex-fmul16-header-readiness.md).
//===----------------------------------------------------------------------===//

// FF2MUL32RS — saturating single-product fractional mul with rounding
def : Pat<(int_haydn_ff2mul32rs_ll DR64:$a, DR64:$b), (FF2MUL32RS_LL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2mul32rs_lh DR64:$a, DR64:$b), (FF2MUL32RS_LH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2mul32rs_hh DR64:$a, DR64:$b), (FF2MUL32RS_HH DR64:$a, DR64:$b)>;

// FF2MULA32RS / FF2MULS32RS — saturating single-product acc twins of
// FF2MUL32RS. GISel selectAccMAC is the live path; 3-operand Pats match
// the ff2mula32r / ff2muls32r tied-def shape (acc is $rd_in).
def : Pat<(int_haydn_ff2mula32rs_ll DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULA32RS_LL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2mula32rs_lh DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULA32RS_LH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2mula32rs_hh DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULA32RS_HH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2muls32rs_ll DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULS32RS_LL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2muls32rs_lh DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULS32RS_LH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2muls32rs_hh DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULS32RS_HH DR64:$acc, DR64:$a, DR64:$b)>;



// FF2MUL32R — non-saturating single-product fractional multiply with rounding.
// Maps 1:1 to FF2MUL32R_* (one product per call) — NOT the dual-product
// F2MULAA32R_* (which sums HH*HH + LL*LL). Verified against DB Behaviors
// (FF2MUL32R_LL: rsd1[31:00]*rsd2[31:00], single) and debated with codex
// GISel selector cases in HaydnInstructionSelector.cpp are the live
// path; these Pats are kept consistent for any SDAG/future path.
def : Pat<(int_haydn_ff2mul32r_ll DR64:$a, DR64:$b), (FF2MUL32R_LL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2mul32r_lh DR64:$a, DR64:$b), (FF2MUL32R_LH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2mul32r_hh DR64:$a, DR64:$b), (FF2MUL32R_HH DR64:$a, DR64:$b)>;

// FF2MULA32R — non-saturating single-product fractional multiply-accumulate
// with rounding. DB FF2MULA32R_*: rtd = rtd + single_product (tied-def
// accumulate). These Pats are SDAG-only (GlobalISel is mandated per
// AGENTS.md hard-constraint #1; the live selection path is the selector
// case in HaydnInstructionSelector.cpp). Kept consistent with the
// FmtALU64Acc tied-def shape (acc -> $rd_in). See.
def : Pat<(int_haydn_ff2mula32r_ll DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULA32R_LL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2mula32r_lh DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULA32R_LH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2mula32r_hh DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULA32R_HH DR64:$acc, DR64:$a, DR64:$b)>;

// FF2MULS32R — non-saturating single-product fractional multiply-subtract
// with rounding. DB FF2MULS32R_*: rtd = rtd - single_product (tied-def
// accumulate) — NOT the dual F2MULSS32R_* (subtracts HH*HH + LL*LL).
// Single-product routing fixed in; tied-def arity reconciled in.
def : Pat<(int_haydn_ff2muls32r_ll DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULS32R_LL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2muls32r_lh DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULS32R_LH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_ff2muls32r_hh DR64:$acc, DR64:$a, DR64:$b),
          (FF2MULS32R_HH DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// 32-bit Multiply (high half)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_mull GPR32:$a, GPR32:$b),   (MULL GPR32:$a, GPR32:$b)>;
def : Pat<(int_haydn_mulssh GPR32:$a, GPR32:$b), (MULSSH GPR32:$a, GPR32:$b)>;
def : Pat<(int_haydn_mulsuh GPR32:$a, GPR32:$b), (MULSUH GPR32:$a, GPR32:$b)>;
def : Pat<(int_haydn_muluuh GPR32:$a, GPR32:$b), (MULUUH GPR32:$a, GPR32:$b)>;

//===----------------------------------------------------------------------===//
// Q-format Multiply/Accumulate (ternary GPR32/DR64)
//
// MULQ31/MACQ31/MULQ63 instruction defs were REMOVED from
// HaydnInstrInfo.td (phantom — not in the ISA DB). The int_haydn_mulq31
// macq31/mulq63 intrinsics remain for source/builtin compatibility and are
// lowered in HaydnInstructionSelector.cpp (case haydn_mulq31/macq31/mulq63)
// to real ISA sequences (MULSSH, MULSSH+ADD32, MUL64_LL). No Pat<> here
// the C++ selector handles the lowering.
//===----------------------------------------------------------------------===//

// MAC32 is not a DB opcode (MULA* targets DR64). haydn_mac32 lowers to
// MULL + ADD32 in the C++ selector.

//===----------------------------------------------------------------------===//
// SIMD X2 Operations (dual 32-bit)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x2add32s DR64:$a, DR64:$b), (X2ADD32S DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2sub32s DR64:$a, DR64:$b), (X2SUB32S DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2addsub32s DR64:$a, DR64:$b), (X2ADDSUB32S DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// SIMD X4 Operations (quad 16-bit)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x4add16s DR64:$a, DR64:$b), (X4ADD16S DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4sub16s DR64:$a, DR64:$b), (X4SUB16S DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// SIMD MAC (ternary DR64)
//===----------------------------------------------------------------------===//

// 2-dest SIMD MAC Pats removed. These ops are TRUE 2-output (golden
// DR_Write_Port:[rtd1,rtd2]) selected via the GlobalIsel C++ selector
// (selectSimdMacAcc2Dest in HaydnInstructionSelector.cpp), not SDAG Pats.
// The old ternary Pats are invalid post-2-dest (intrinsic now [i64,i64];
// instruction now (outs rtd1,rtd2),(ins rta1,rta2,rsd1,rsd2)). Mirrors the
// X4FCMULA16RS precedent (line ~258). GlobalIsel-only — no SDAG path.
// def : Pat<(int_haydn_x2mula32 DR64:$a, DR64:$b, DR64:$c), (X2MULA32 DR64:$a, DR64:$b, DR64:$c)>;
// def : Pat<(int_haydn_x2muls32 DR64:$a, DR64:$b, DR64:$c), (X2MULS32 DR64:$a, DR64:$b, DR64:$c)>;
// def : Pat<(int_haydn_x4mula16 DR64:$a, DR64:$b, DR64:$c), (X4MULA16 DR64:$a, DR64:$b, DR64:$c)>;
// def : Pat<(int_haydn_x4muls16 DR64:$a, DR64:$b, DR64:$c), (X4MULS16 DR64:$a, DR64:$b, DR64:$c)>;
// def : Pat<(int_haydn_x4mula16s DR64:$a, DR64:$b, DR64:$c), (X4MULA16S DR64:$a, DR64:$b, DR64:$c)>;
// def : Pat<(int_haydn_x4muls16s DR64:$a, DR64:$b, DR64:$c), (X4MULS16S DR64:$a, DR64:$b, DR64:$c)>;

//===----------------------------------------------------------------------===//
// Transcendental Functions
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_log2 GPR32:$a),  (LOG2 GPR32:$a)>;
def : Pat<(int_haydn_exp2 GPR32:$a),  (EXP2 GPR32:$a)>;
def : Pat<(int_haydn_recip GPR32:$a), (RECIP GPR32:$a)>;
def : Pat<(int_haydn_sqrt GPR32:$a),  (SQRT GPR32:$a)>;

//===----------------------------------------------------------------------===//
// Normalization (NSA)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_nsa32 GPR32:$a),    (NSA32 GPR32:$a)>;
def : Pat<(int_haydn_nsau32 GPR32:$a),   (NSAU32 GPR32:$a)>;
// R_GD: DR64 source (match FormatsALU64 members / logical dag).
def : Pat<(int_haydn_nsa64 DR64:$a),    (NSA64 DR64:$a)>;
def : Pat<(int_haydn_nsa16_l DR64:$a),  (NSA16_L DR64:$a)>;
def : Pat<(int_haydn_nsa32_l DR64:$a),  (NSA32_L DR64:$a)>;
def : Pat<(int_haydn_nsaz64 DR64:$a),   (NSAZ64 DR64:$a)>;
def : Pat<(int_haydn_nsaz16_l DR64:$a), (NSAZ16_L DR64:$a)>;
def : Pat<(int_haydn_nsaz32_l DR64:$a), (NSAZ32_L DR64:$a)>;

//===----------------------------------------------------------------------===//
// MULSA32/MULSS32 — Dual 32-bit multiply-add/sub, tied accumulator.
// Golden Constraints "$rd1 = $rd1_in". GISel live path is selectAccMAC;
// these Pats keep the three-operand acc form for the match table.
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_mulsa32_hhll DR64:$acc, DR64:$a, DR64:$b), (MULSA32_HHLL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulsa32_hllh DR64:$acc, DR64:$a, DR64:$b), (MULSA32_HLLH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulss32_hhll DR64:$acc, DR64:$a, DR64:$b), (MULSS32_HHLL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulss32_hllh DR64:$acc, DR64:$a, DR64:$b), (MULSS32_HLLH DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 2: LC3 BASOP 16x16 fractional multiply (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_fmul16_hs00 DR64:$a, DR64:$b), (FMUL16_HS00 DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 2: IIR biquad fused dual MAC (binary DR64)
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// Wave 2: Complex multiply (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x4fcmul16rs DR64:$a, DR64:$b), (X4FCMUL16RS DR64:$a, DR64:$b)>;
// X4FCMULA16RS/RSS Pats removed — these are tied-def accumulators
// selected via selectAccMAC in the hand-coded switch, not a 2-src Pat.
def : Pat<(int_haydn_x4fcmul16rss DR64:$a, DR64:$b), (X4FCMUL16RSS DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 3: 32-bit complex multiply (2-dest DR64: real->rtd1, imag->rtd2)
// Pats removed — 2-dest ops selected via selectSimdMac2Dest (C++ GISel
// selector), not SDAG Pats. Old ternary Pats invalid post-2-dest.
//===----------------------------------------------------------------------===//

// def : Pat<(int_haydn_x2cmul32 DR64:$a, DR64:$b, DR64:$c), (X2CMUL32 DR64:$a, DR64:$b, DR64:$c)>;
// def : Pat<(int_haydn_x2cmul32s DR64:$a, DR64:$b, DR64:$c), (X2CMUL32S DR64:$a, DR64:$b, DR64:$c)>;

//===----------------------------------------------------------------------===//
// Wave 3: SFR Flag Register Predication
// Compare→SFR→conditional-move pattern for SIMD predication.
//
// Compare ops: binary DR64 → sets SFR flags per lane.
// The.td instruction has (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2).
// $rd is a passthrough (returns first operand); the real effect is
// writing to SFR (implicit def).
//
// Move ops: ternary DR64 → conditional select per lane based on SFR.
// Intrinsic: dst = mov(src_false, src_true) — src_false is kept when
// the condition is not met, src_true is selected when condition is met.
// The.td instruction has (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2).
// We map intrinsic(src_false, src_true) → X2MOVT32(src_false, src_true).
//===----------------------------------------------------------------------===//

// X2/X4 SFR compare + cmov: GISel selects via C++ (selectBinary). TableGen
// Pats are intentionally omitted — Defs/Uses=[SFR] with hasSideEffects=0
// fails "Pattern doesn't match hasSideEffects = 0" (same ISA-27 constraint
// that deferred explicit SFR modeling). Selection coverage:
// haydn_x2{seq,slt,sle}32 / haydn_x2mov{t,f}32
// haydn_x4{seq,slt,sle}16 / haydn_x4mov{t,f}16
// in HaydnInstructionSelector.cpp.

//===----------------------------------------------------------------------===//
// Wave 4: X2 SIMD fractional multiply (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x2fmul32rs  DR64:$a, DR64:$b), (X2FMUL32RS  DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmul32rss DR64:$a, DR64:$b), (X2FMUL32RSS DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmul32ts  DR64:$a, DR64:$b), (X2FMUL32TS  DR64:$a, DR64:$b)>;

def : Pat<(int_haydn_x2fmula32rs DR64:$acc, DR64:$a, DR64:$b), (X2FMULA32RS DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmula32rss DR64:$acc, DR64:$a, DR64:$b), (X2FMULA32RSS DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmula32ts DR64:$acc, DR64:$a, DR64:$b), (X2FMULA32TS DR64:$acc, DR64:$a, DR64:$b)>;

def : Pat<(int_haydn_x2fmuls32rs DR64:$acc, DR64:$a, DR64:$b), (X2FMULS32RS DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmuls32rss DR64:$acc, DR64:$a, DR64:$b), (X2FMULS32RSS DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmuls32ts DR64:$acc, DR64:$a, DR64:$b), (X2FMULS32TS DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4: X4 SIMD fractional multiply (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x4fmul16rs  DR64:$a, DR64:$b), (X4FMUL16RS  DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4fmul16rss DR64:$a, DR64:$b), (X4FMUL16RSS DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4fmul16ts  DR64:$a, DR64:$b), (X4FMUL16TS  DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4: X2/X4 shift with rounding (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x2frsst32 DR64:$a, DR64:$b), (X2FRSST32 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2frst32  DR64:$a, DR64:$b), (X2FRST32  DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4frsst16 DR64:$a, DR64:$b), (X4FRSST16 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4frst16  DR64:$a, DR64:$b), (X4FRST16  DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4: FMUL16_HS remaining variants (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_fmul16_hs01 DR64:$a, DR64:$b), (FMUL16_HS01 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_hs02 DR64:$a, DR64:$b), (FMUL16_HS02 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_hs03 DR64:$a, DR64:$b), (FMUL16_HS03 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_hs11 DR64:$a, DR64:$b), (FMUL16_HS11 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_hs12 DR64:$a, DR64:$b), (FMUL16_HS12 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_hs13 DR64:$a, DR64:$b), (FMUL16_HS13 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_hs22 DR64:$a, DR64:$b), (FMUL16_HS22 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_hs23 DR64:$a, DR64:$b), (FMUL16_HS23 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_hs33 DR64:$a, DR64:$b), (FMUL16_HS33 DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4: FMUL16_LS variants (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_fmul16_ls00 DR64:$a, DR64:$b), (FMUL16_LS00 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_ls01 DR64:$a, DR64:$b), (FMUL16_LS01 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_ls02 DR64:$a, DR64:$b), (FMUL16_LS02 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_ls03 DR64:$a, DR64:$b), (FMUL16_LS03 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_ls11 DR64:$a, DR64:$b), (FMUL16_LS11 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_ls12 DR64:$a, DR64:$b), (FMUL16_LS12 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_ls13 DR64:$a, DR64:$b), (FMUL16_LS13 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_ls22 DR64:$a, DR64:$b), (FMUL16_LS22 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_ls23 DR64:$a, DR64:$b), (FMUL16_LS23 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul16_ls33 DR64:$a, DR64:$b), (FMUL16_LS33 DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4: FMULAA16 HS/LS MAC variants (binary DR64)
//===----------------------------------------------------------------------===//

// Ternary Acc (FmtALU64Acc): GISel selectAccMAC is the live path; SDAG Pat
// kept consistent with the tied-def shape. HS_11_00 is the two-lane dual
// product (lanes 1+0); default NatureDSP vec_dot16 uses dest-typed
// X4MULA16S instead (2-dest, C++ selector only).
def : Pat<(int_haydn_fmulaa16_hs_11_00 DR64:$acc, DR64:$a, DR64:$b), (FMULAA16_HS_11_00 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulaa16_hs_13_02 DR64:$acc, DR64:$a, DR64:$b), (FMULAA16_HS_13_02 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulaa16_hs_33_22 DR64:$acc, DR64:$a, DR64:$b),
          (FMULAA16_HS_33_22 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulaa16_ls_11_00 DR64:$acc, DR64:$a, DR64:$b), (FMULAA16_LS_11_00 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulaa16_ls_13_02 DR64:$acc, DR64:$a, DR64:$b), (FMULAA16_LS_13_02 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulaa16_ls_33_22 DR64:$acc, DR64:$a, DR64:$b), (FMULAA16_LS_33_22 DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4: FMULSS16 HS/LS MSU variants (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_fmulss16_hs_11_00 DR64:$acc, DR64:$a, DR64:$b), (FMULSS16_HS_11_00 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulss16_hs_13_02 DR64:$acc, DR64:$a, DR64:$b), (FMULSS16_HS_13_02 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulss16_hs_33_22 DR64:$acc, DR64:$a, DR64:$b), (FMULSS16_HS_33_22 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulss16_ls_11_00 DR64:$acc, DR64:$a, DR64:$b), (FMULSS16_LS_11_00 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulss16_ls_13_02 DR64:$acc, DR64:$a, DR64:$b), (FMULSS16_LS_13_02 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulss16_ls_33_22 DR64:$acc, DR64:$a, DR64:$b), (FMULSS16_LS_33_22 DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4: F2MUL zero-accumulator variants (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_f2mulas32rs_hhll DR64:$acc, DR64:$a, DR64:$b), (F2MULAS32RS_HHLL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulas32rs_hllh DR64:$acc, DR64:$a, DR64:$b), (F2MULAS32RS_HLLH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulsa32rs_hhll DR64:$acc, DR64:$a, DR64:$b), (F2MULSA32RS_HHLL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulsa32rs_hllh DR64:$acc, DR64:$a, DR64:$b), (F2MULSA32RS_HLLH DR64:$acc, DR64:$a, DR64:$b)>;

def : Pat<(int_haydn_f2mulas32r_hhll DR64:$acc, DR64:$a, DR64:$b), (F2MULAS32R_HHLL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulas32r_hllh DR64:$acc, DR64:$a, DR64:$b), (F2MULAS32R_HLLH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulsa32r_hhll DR64:$acc, DR64:$a, DR64:$b), (F2MULSA32R_HHLL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulsa32r_hllh DR64:$acc, DR64:$a, DR64:$b), (F2MULSA32R_HLLH DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 2: X2/X4 SIMD Unary ops (unary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x2abs32   DR64:$a), (X2ABS32   DR64:$a)>;
def : Pat<(int_haydn_x2abs32s  DR64:$a), (X2ABS32S  DR64:$a)>;
def : Pat<(int_haydn_x2neg32   DR64:$a), (X2NEG32   DR64:$a)>;
def : Pat<(int_haydn_x2neg32s  DR64:$a), (X2NEG32S  DR64:$a)>;
def : Pat<(int_haydn_x2neg32_l DR64:$a), (X2NEG32_L DR64:$a)>;
def : Pat<(int_haydn_x2neg32s_l DR64:$a), (X2NEG32S_L DR64:$a)>;
def : Pat<(int_haydn_x2swap32  DR64:$a), (X2SWAP32  DR64:$a)>;
def : Pat<(int_haydn_x2mjswap32  DR64:$a), (X2MJSWAP32  DR64:$a)>;
def : Pat<(int_haydn_x2mjswap32s DR64:$a), (X2MJSWAP32S DR64:$a)>;

def : Pat<(int_haydn_x4abs16   DR64:$a), (X4ABS16   DR64:$a)>;
def : Pat<(int_haydn_x4abs16s  DR64:$a), (X4ABS16S  DR64:$a)>;
def : Pat<(int_haydn_x4neg16   DR64:$a), (X4NEG16   DR64:$a)>;
def : Pat<(int_haydn_x4neg16s  DR64:$a), (X4NEG16S  DR64:$a)>;
def : Pat<(int_haydn_x4swap16  DR64:$a), (X4SWAP16  DR64:$a)>;
def : Pat<(int_haydn_x4mjswap16  DR64:$a), (X4MJSWAP16  DR64:$a)>;
def : Pat<(int_haydn_x4mjswap16s DR64:$a), (X4MJSWAP16S DR64:$a)>;
def : Pat<(int_haydn_x4conj16  DR64:$a), (X4CONJ16  DR64:$a)>;
def : Pat<(int_haydn_x4conj16s DR64:$a), (X4CONJ16S DR64:$a)>;
def : Pat<(int_haydn_x4energy16 DR64:$a), (X4ENERGY16 DR64:$a)>;
// Unary X4CMUL16* — logical bases; post-RA setDesc → S1/S2 members.
def : Pat<(int_haydn_x4cmul16   DR64:$a), (X4CMUL16   DR64:$a)>;
def : Pat<(int_haydn_x4cmul16s  DR64:$a), (X4CMUL16S  DR64:$a)>;
def : Pat<(int_haydn_x4cmul16s_f2 DR64:$a), (X4CMUL16S_F2 DR64:$a)>;
def : Pat<(int_haydn_x4cmul16_f2  DR64:$a), (X4CMUL16_F2  DR64:$a)>;

// X2/X4 binary (DR64 binary)
def : Pat<(int_haydn_x2max32   DR64:$a, DR64:$b), (X2MAX32   DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2min32   DR64:$a, DR64:$b), (X2MIN32   DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2clamp32 DR64:$v, DR64:$lo, DR64:$hi), (X2CLAMP32 DR64:$v, DR64:$lo, DR64:$hi)>;
def : Pat<(int_haydn_x4max16   DR64:$a, DR64:$b), (X4MAX16   DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4min16   DR64:$a, DR64:$b), (X4MIN16   DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4clamp16 DR64:$v, DR64:$lo, DR64:$hi), (X4CLAMP16 DR64:$v, DR64:$lo, DR64:$hi)>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 2: Shuffle/Pack ops
//===----------------------------------------------------------------------===//

// X2SEL32 lane variants (binary DR64)
def : Pat<(int_haydn_x2sel32_hh DR64:$a, DR64:$b), (X2SEL32_HH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2sel32_hl DR64:$a, DR64:$b), (X2SEL32_HL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2sel32_lh DR64:$a, DR64:$b), (X2SEL32_LH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2sel32_ll DR64:$a, DR64:$b), (X2SEL32_LL DR64:$a, DR64:$b)>;

// X2ADDSUB32 / X2SUBADD32 variants (binary DR64)
// Note: int_haydn_x2addsub32s already mapped above in SIMD X2 section.
def : Pat<(int_haydn_x2addsub32       DR64:$a, DR64:$b), (X2ADDSUB32       DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2addsub32_hllh  DR64:$a, DR64:$b), (X2ADDSUB32_HLLH  DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2addsub32s_hllh DR64:$a, DR64:$b), (X2ADDSUB32S_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2subadd32       DR64:$a, DR64:$b), (X2SUBADD32       DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2subadd32s      DR64:$a, DR64:$b), (X2SUBADD32S      DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2subadd32_hllh  DR64:$a, DR64:$b), (X2SUBADD32_HLLH  DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2subadd32s_hllh DR64:$a, DR64:$b), (X2SUBADD32S_HLLH DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 2: X4 Complex Multiply variants (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x4cmul16s_h  DR64:$a, DR64:$b), (X4CMUL16S_H  DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4cmul16s_l  DR64:$a, DR64:$b), (X4CMUL16S_L  DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4cmula16s_h DR64:$acc, DR64:$a, DR64:$b), (X4CMULA16S_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4cmula16s_l DR64:$acc, DR64:$a, DR64:$b), (X4CMULA16S_L DR64:$acc, DR64:$a, DR64:$b)>;

def : Pat<(int_haydn_x4cjmul16s_h  DR64:$a, DR64:$b), (X4CJMUL16S_H  DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4cjmul16s_l  DR64:$a, DR64:$b), (X4CJMUL16S_L  DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4cjmula16s_h DR64:$acc, DR64:$a, DR64:$b), (X4CJMULA16S_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4cjmula16s_l DR64:$acc, DR64:$a, DR64:$b), (X4CJMULA16S_L DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 2: 64-bit scalar ops
//===----------------------------------------------------------------------===//

// Unary DR64
def : Pat<(int_haydn_not64 DR64:$a), (NOT64 DR64:$a)>;
// SEQ64: GISel via selectUnary (Defs=[SFR] blocks tablegen Pat).

// Binary DR64
def : Pat<(int_haydn_max64   DR64:$a, DR64:$b), (MAX64   DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_min64   DR64:$a, DR64:$b), (MIN64   DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_add64_h DR64:$a, DR64:$b), (ADD64_H DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_add64_l DR64:$a, DR64:$b), (ADD64_L DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_sub64_h DR64:$a, DR64:$b), (SUB64_H DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_sub64_l DR64:$a, DR64:$b), (SUB64_L DR64:$a, DR64:$b)>;

// SLL64: now DR64 binary (i64, i32 shift -> i64) per ISA DB fix.
// The intrinsic takes i64 data + i32 shift; selection handled by C++ selector.


// SRA64/SRL64: cross-type (i64 accum, i32 shift -> i32 result)
// Selection handled by custom C++ due to mixed register classes.

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: SMULA16 — Signed 16-bit MAC with lane selection (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_smula16_00 DR64:$acc, DR64:$a, DR64:$b), (SMULA16_00 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16_10 DR64:$acc, DR64:$a, DR64:$b), (SMULA16_10 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16_11 DR64:$acc, DR64:$a, DR64:$b), (SMULA16_11 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16_20 DR64:$acc, DR64:$a, DR64:$b), (SMULA16_20 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16_21 DR64:$acc, DR64:$a, DR64:$b), (SMULA16_21 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16_22 DR64:$acc, DR64:$a, DR64:$b), (SMULA16_22 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16_30 DR64:$acc, DR64:$a, DR64:$b), (SMULA16_30 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16_31 DR64:$acc, DR64:$a, DR64:$b), (SMULA16_31 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16_32 DR64:$acc, DR64:$a, DR64:$b), (SMULA16_32 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16_33 DR64:$acc, DR64:$a, DR64:$b), (SMULA16_33 DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: SMULA16S — Signed 16-bit saturating MAC (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_smula16s_00 DR64:$acc, DR64:$a, DR64:$b), (SMULA16S_00 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16s_10 DR64:$acc, DR64:$a, DR64:$b), (SMULA16S_10 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16s_11 DR64:$acc, DR64:$a, DR64:$b), (SMULA16S_11 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16s_20 DR64:$acc, DR64:$a, DR64:$b), (SMULA16S_20 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16s_21 DR64:$acc, DR64:$a, DR64:$b), (SMULA16S_21 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16s_22 DR64:$acc, DR64:$a, DR64:$b), (SMULA16S_22 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16s_30 DR64:$acc, DR64:$a, DR64:$b), (SMULA16S_30 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16s_31 DR64:$acc, DR64:$a, DR64:$b), (SMULA16S_31 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16s_32 DR64:$acc, DR64:$a, DR64:$b), (SMULA16S_32 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smula16s_33 DR64:$acc, DR64:$a, DR64:$b), (SMULA16S_33 DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: SMULS16 — Signed 16-bit multiply-subtract (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_smuls16_00 DR64:$acc, DR64:$a, DR64:$b), (SMULS16_00 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16_10 DR64:$acc, DR64:$a, DR64:$b), (SMULS16_10 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16_11 DR64:$acc, DR64:$a, DR64:$b), (SMULS16_11 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16_20 DR64:$acc, DR64:$a, DR64:$b), (SMULS16_20 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16_21 DR64:$acc, DR64:$a, DR64:$b), (SMULS16_21 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16_22 DR64:$acc, DR64:$a, DR64:$b), (SMULS16_22 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16_30 DR64:$acc, DR64:$a, DR64:$b), (SMULS16_30 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16_31 DR64:$acc, DR64:$a, DR64:$b), (SMULS16_31 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16_32 DR64:$acc, DR64:$a, DR64:$b), (SMULS16_32 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16_33 DR64:$acc, DR64:$a, DR64:$b), (SMULS16_33 DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: SMULS16S — Signed 16-bit saturating multiply-subtract (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_smuls16s_00 DR64:$acc, DR64:$a, DR64:$b), (SMULS16S_00 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16s_10 DR64:$acc, DR64:$a, DR64:$b), (SMULS16S_10 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16s_11 DR64:$acc, DR64:$a, DR64:$b), (SMULS16S_11 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16s_20 DR64:$acc, DR64:$a, DR64:$b), (SMULS16S_20 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16s_21 DR64:$acc, DR64:$a, DR64:$b), (SMULS16S_21 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16s_22 DR64:$acc, DR64:$a, DR64:$b), (SMULS16S_22 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16s_30 DR64:$acc, DR64:$a, DR64:$b), (SMULS16S_30 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16s_31 DR64:$acc, DR64:$a, DR64:$b), (SMULS16S_31 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16s_32 DR64:$acc, DR64:$a, DR64:$b), (SMULS16S_32 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_smuls16s_33 DR64:$acc, DR64:$a, DR64:$b), (SMULS16S_33 DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: FMULS16 — Fractional 16-bit multiply-subtract (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_fmuls16_hs00 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_HS00 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_hs01 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_HS01 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_hs02 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_HS02 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_hs03 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_HS03 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_hs11 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_HS11 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_hs12 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_HS12 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_hs13 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_HS13 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_hs22 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_HS22 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_hs23 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_HS23 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_hs33 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_HS33 DR64:$acc, DR64:$a, DR64:$b)>;

def : Pat<(int_haydn_fmuls16_ls00 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_LS00 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_ls01 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_LS01 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_ls02 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_LS02 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_ls03 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_LS03 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_ls11 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_LS11 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_ls12 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_LS12 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_ls13 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_LS13 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_ls22 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_LS22 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_ls23 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_LS23 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls16_ls33 DR64:$acc, DR64:$a, DR64:$b), (FMULS16_LS33 DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: FMULAS32S/FMULSA32S — Saturating fractional add-subtract MAC
// Ternary (acc, a, b): DB shows these read rtd as accumulator. Reconciled
// from 2-operand to 3-operand as part of A1 full (mirrors).
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_fmulas32s_hhll DR64:$acc, DR64:$a, DR64:$b), (FMULAS32S_HHLL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulas32s_hllh DR64:$acc, DR64:$a, DR64:$b), (FMULAS32S_HLLH DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulsa32s_hhll DR64:$acc, DR64:$a, DR64:$b), (FMULSA32S_HHLL DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulsa32s_hllh DR64:$acc, DR64:$a, DR64:$b), (FMULSA32S_HLLH DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: SRA64R — Cross-type shift with rounding
// Selection handled by custom C++ due to mixed register classes.

//===----------------------------------------------------------------------===//
// Wave 5: Scalar 64-bit SFR Compare -> SFR (binary DR64)
//===----------------------------------------------------------------------===//

// SLT64, SLE64, MOVT64, MOVF64: These instructions take only 1 explicit
// DR64 operand (the other "operand" is the implicit SFR condition register).
// Handled by custom C++ in HaydnInstructionSelector — do NOT add Pat patterns
// here because the operand counts do not match the intrinsic signatures.

//===----------------------------------------------------------------------===//
// Wave 5: SFR Register Transfer
// MOVESFR2GPR / MOVEGPR2SFR / ZERO_SFR are handled by custom C++ selector
// because they have no operands or different register classes.
// (MOVESFR2GPR: no explicit source operands, reads SFR implicitly)
// (MOVEGPR2SFR: GPR32 source, no explicit dest, writes SFR implicitly)
// (ZERO_SFR: no operands at all)

//===----------------------------------------------------------------------===//
// Circular Buffer Load/Store (CBR)
// All handled by custom C++ in HaydnInstructionSelector because:
// CB loads have tied operands (base ptr read+write) + cbr_sel immediate
// CB stores have void return + cbr_sel immediate
// The cbr_sel immediate cannot be expressed via standard Pat operand types
// Instructions: D_LDW_CB_IMM, D_LDW_CB_REG, D_SDW_CB_IMM, D_SDW_CB_REG

//===----------------------------------------------------------------------===//
// Bit-Reversed Addressing Load/Store
// All handled by custom C++ in HaydnInstructionSelector because:
// BREV stores have tied operands (base ptr read+write)
// Immediate variants require constant extraction for stride
// Instructions: D_LDW_BREV_IMM/REG, S_LW_BREV_IMM/REG
// D_SDW_BREV_IMM/REG, S_SW_BREV_IMM/REG

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD non-saturating arithmetic (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x2add32 DR64:$a, DR64:$b), (X2ADD32 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2sub32 DR64:$a, DR64:$b), (X2SUB32 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4add16 DR64:$a, DR64:$b), (X4ADD16 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4sub16 DR64:$a, DR64:$b), (X4SUB16 DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 5: X2 SIMD HLLH cross-lane variants (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x2add32_hllh  DR64:$a, DR64:$b), (X2ADD32_HLLH  DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2add32s_hllh DR64:$a, DR64:$b), (X2ADD32S_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2sub32_hllh  DR64:$a, DR64:$b), (X2SUB32_HLLH  DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2sub32s_hllh DR64:$a, DR64:$b), (X2SUB32S_HLLH DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD register shifts (binary DR64)
//===----------------------------------------------------------------------===//

// Shift amount is GPR32 (HaydnInstrInfo.td X2SLL32 / Auto X4SLL16).
// Intrinsics take a vector second operand; GISel selectDR64ShiftGPR32 is
// the live path. No DR64/DR64 Pat — that would be a type lie.

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD shift with rounding (binary DR64)
//===----------------------------------------------------------------------===//

// X2SRA32R / X4SRA16R also take GPR32 shift amount. Same C++ selector.

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD horizontal reductions (unary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x2hadd32_h DR64:$a), (X2HADD32_H DR64:$a)>;
def : Pat<(int_haydn_x2hadd32s_h DR64:$a), (X2HADD32S_H DR64:$a)>;
def : Pat<(int_haydn_x2hadd32_l DR64:$a), (X2HADD32_L DR64:$a)>;
def : Pat<(int_haydn_x2hadd32s_l DR64:$a), (X2HADD32S_L DR64:$a)>;
def : Pat<(int_haydn_x4hadd16_h DR64:$a), (X4HADD16_H DR64:$a)>;
def : Pat<(int_haydn_x4hadd16_l DR64:$a), (X4HADD16_L DR64:$a)>;
// R_GD: GPR32 dest / DR64 source. GISel selectUnaryR_GD; no DR64-dest Pat.

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD dot product (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x2dot32 DR64:$a, DR64:$b), (X2DOT32 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x4dot16 DR64:$a, DR64:$b), (X4DOT16 DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD multiply (2-dest DR64)
//===----------------------------------------------------------------------===//

// X2MUL32 / X4MUL16 are FmtALU64_2Dest. GISel selectSimdMac2Dest only.

//===----------------------------------------------------------------------===//
// Wave 5: X2 SIMD multiply-pair variants (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x2mulph32 DR64:$a, DR64:$b), (X2MULPH32 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2mulpl32 DR64:$a, DR64:$b), (X2MULPL32 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2mulaph32 DR64:$acc, DR64:$a, DR64:$b), (X2MULAPH32 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2mulapl32 DR64:$acc, DR64:$a, DR64:$b), (X2MULAPL32 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2mulsph32 DR64:$acc, DR64:$a, DR64:$b), (X2MULSPH32 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2mulspl32 DR64:$acc, DR64:$a, DR64:$b), (X2MULSPL32 DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 5: X2 32-bit complex fractional multiply (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x2fcmul32rs DR64:$a, DR64:$b), (X2FCMUL32RS DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fcmul32rss DR64:$a, DR64:$b), (X2FCMUL32RSS DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fcmula32rs DR64:$acc, DR64:$a, DR64:$b),
          (X2FCMULA32RS DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fcmula32rss DR64:$acc, DR64:$a, DR64:$b),
          (X2FCMULA32RSS DR64:$acc, DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 5: X2 CMUL F2 variants (2-dest DR64)
//===----------------------------------------------------------------------===//

// X2CMUL32_F2 / X2CMUL32S_F2 are FmtMAC2Dest. Public AE wrappers stay
// fail-closed in haydn_dsp.h. GISel selectSimdMac2Dest only.

//===----------------------------------------------------------------------===//
// Wave 5: X2 FF2 shift variants (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x2ff2rsst32 DR64:$a, DR64:$b), (X2FF2RSST32 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2ff2rst32 DR64:$a, DR64:$b), (X2FF2RST32 DR64:$a, DR64:$b)>;

//===----------------------------------------------------------------------===//
// Wave 5: X4 FF2MUL variants (2-dest DR64)
//===----------------------------------------------------------------------===//

// X4FF2MUL16S / MULA / MULS are 2-dest. GISel only.

//===----------------------------------------------------------------------===//
// Wave 5: X4 pack/sat (binary DR64)
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_x4sat32t16 DR64:$a, DR64:$b), (X4SAT32T16 DR64:$a, DR64:$b)>;

// Immediate shift and SELI16 intrinsics use custom C++ selection
// (constant extraction from i32 immediate operand).

//===----------------------------------------------------------------------===//
// Phase 3A : 61 binary intrinsics wired via Pat (60 DR64 + 1 GPR32).
// These are pure-product / zero-accumulate ops (no accumulator read per DB).
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_f2mulzaa32r_hhll DR64:$a, DR64:$b), (F2MULZAA32R_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzaa32r_hllh DR64:$a, DR64:$b), (F2MULZAA32R_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzaa32rs_hhll DR64:$a, DR64:$b), (F2MULZAA32RS_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzaa32rs_hllh DR64:$a, DR64:$b), (F2MULZAA32RS_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzas32r_hhll DR64:$a, DR64:$b), (F2MULZAS32R_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzas32r_hllh DR64:$a, DR64:$b), (F2MULZAS32R_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzas32rs_hhll DR64:$a, DR64:$b), (F2MULZAS32RS_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzas32rs_hllh DR64:$a, DR64:$b), (F2MULZAS32RS_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzsa32r_hhll DR64:$a, DR64:$b), (F2MULZSA32R_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzsa32r_hllh DR64:$a, DR64:$b), (F2MULZSA32R_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzsa32rs_hhll DR64:$a, DR64:$b), (F2MULZSA32RS_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzsa32rs_hllh DR64:$a, DR64:$b), (F2MULZSA32RS_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzss32r_hhll DR64:$a, DR64:$b), (F2MULZSS32R_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzss32r_hllh DR64:$a, DR64:$b), (F2MULZSS32R_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzss32rs_hhll DR64:$a, DR64:$b), (F2MULZSS32RS_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_f2mulzss32rs_hllh DR64:$a, DR64:$b), (F2MULZSS32RS_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa16_hs_11_00 DR64:$a, DR64:$b), (FMULZAA16_HS_11_00 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa16_hs_13_02 DR64:$a, DR64:$b), (FMULZAA16_HS_13_02 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa16_hs_33_22 DR64:$a, DR64:$b), (FMULZAA16_HS_33_22 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa16_ls_11_00 DR64:$a, DR64:$b), (FMULZAA16_LS_11_00 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa16_ls_13_02 DR64:$a, DR64:$b), (FMULZAA16_LS_13_02 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa16_ls_33_22 DR64:$a, DR64:$b), (FMULZAA16_LS_33_22 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa32s_hhll DR64:$a, DR64:$b), (FMULZAA32S_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa32s_hllh DR64:$a, DR64:$b), (FMULZAA32S_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzas32s_hhll DR64:$a, DR64:$b), (FMULZAS32S_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzas32s_hllh DR64:$a, DR64:$b), (FMULZAS32S_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzsa32s_hhll DR64:$a, DR64:$b), (FMULZSA32S_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzsa32s_hllh DR64:$a, DR64:$b), (FMULZSA32S_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzss16_hs_11_00 DR64:$a, DR64:$b), (FMULZSS16_HS_11_00 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzss16_hs_13_02 DR64:$a, DR64:$b), (FMULZSS16_HS_13_02 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzss16_hs_33_22 DR64:$a, DR64:$b), (FMULZSS16_HS_33_22 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzss16_ls_11_00 DR64:$a, DR64:$b), (FMULZSS16_LS_11_00 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzss16_ls_13_02 DR64:$a, DR64:$b), (FMULZSS16_LS_13_02 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzss16_ls_33_22 DR64:$a, DR64:$b), (FMULZSS16_LS_33_22 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzss32s_hhll DR64:$a, DR64:$b), (FMULZSS32S_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzss32s_hllh DR64:$a, DR64:$b), (FMULZSS32S_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul16zaq DR64:$a, DR64:$b), (MUL16ZAQ DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_hh DR64:$a, DR64:$b), (MUL64_HH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_hl DR64:$a, DR64:$b), (MUL64_HL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_huh DR64:$a, DR64:$b), (MUL64_HUH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_hul DR64:$a, DR64:$b), (MUL64_HUL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_lh DR64:$a, DR64:$b), (MUL64_LH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_luh DR64:$a, DR64:$b), (MUL64_LUH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_lul DR64:$a, DR64:$b), (MUL64_LUL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_uhh DR64:$a, DR64:$b), (MUL64_UHH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_uhl DR64:$a, DR64:$b), (MUL64_UHL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_uhuh DR64:$a, DR64:$b), (MUL64_UHUH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_uhul DR64:$a, DR64:$b), (MUL64_UHUL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_ulh DR64:$a, DR64:$b), (MUL64_ULH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_ull DR64:$a, DR64:$b), (MUL64_ULL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_uluh DR64:$a, DR64:$b), (MUL64_ULUH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul64_ulul DR64:$a, DR64:$b), (MUL64_ULUL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzaa32_hhll DR64:$a, DR64:$b), (MULZAA32_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzaa32_hllh DR64:$a, DR64:$b), (MULZAA32_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzas32_hhll DR64:$a, DR64:$b), (MULZAS32_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzas32_hllh DR64:$a, DR64:$b), (MULZAS32_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzsa32_hhll DR64:$a, DR64:$b), (MULZSA32_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzsa32_hllh DR64:$a, DR64:$b), (MULZSA32_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzss32_hhll DR64:$a, DR64:$b), (MULZSS32_HHLL DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzss32_hllh DR64:$a, DR64:$b), (MULZSS32_HLLH DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_brev32 GPR32:$a, GPR32:$b), (BREV32 GPR32:$a, GPR32:$b)>;

//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// 32X16 multiply/MAC family — golden v2_1 (2026-08-18). Logical defs live
// in HaydnInstrInfoGolden.td.inc; accumulate forms are golden-tied
// ((outs $rd1), (ins $rd1_in, $rd2, $rd3), Constraints "$rd1 = $rd1_in").
//===----------------------------------------------------------------------===//

def : Pat<(int_haydn_fmul32x16_h0 DR64:$a, DR64:$b), (FMUL32X16_H0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul32x16_h1 DR64:$a, DR64:$b), (FMUL32X16_H1 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul32x16_h2 DR64:$a, DR64:$b), (FMUL32X16_H2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul32x16_h3 DR64:$a, DR64:$b), (FMUL32X16_H3 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul32x16_l0 DR64:$a, DR64:$b), (FMUL32X16_L0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul32x16_l1 DR64:$a, DR64:$b), (FMUL32X16_L1 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul32x16_l2 DR64:$a, DR64:$b), (FMUL32X16_L2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmul32x16_l3 DR64:$a, DR64:$b), (FMUL32X16_L3 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa32x16_h0_l1 DR64:$a, DR64:$b), (FMULZAA32X16_H0_L1 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa32x16_h1_l0 DR64:$a, DR64:$b), (FMULZAA32X16_H1_L0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa32x16_h2_l3 DR64:$a, DR64:$b), (FMULZAA32X16_H2_L3 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzaa32x16_h3_l2 DR64:$a, DR64:$b), (FMULZAA32X16_H3_L2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzas32x16_h1_l0 DR64:$a, DR64:$b), (FMULZAS32X16_H1_L0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzas32x16_h3_l2 DR64:$a, DR64:$b), (FMULZAS32X16_H3_L2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzsa32x16_h1_l0 DR64:$a, DR64:$b), (FMULZSA32X16_H1_L0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzsa32x16_h3_l2 DR64:$a, DR64:$b), (FMULZSA32X16_H3_L2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzss32x16_h1_l0 DR64:$a, DR64:$b), (FMULZSS32X16_H1_L0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulzss32x16_h3_l2 DR64:$a, DR64:$b), (FMULZSS32X16_H3_L2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul32x16_h0 DR64:$a, DR64:$b), (MUL32X16_H0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul32x16_h1 DR64:$a, DR64:$b), (MUL32X16_H1 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul32x16_h2 DR64:$a, DR64:$b), (MUL32X16_H2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul32x16_h3 DR64:$a, DR64:$b), (MUL32X16_H3 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul32x16_l0 DR64:$a, DR64:$b), (MUL32X16_L0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul32x16_l1 DR64:$a, DR64:$b), (MUL32X16_L1 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul32x16_l2 DR64:$a, DR64:$b), (MUL32X16_L2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mul32x16_l3 DR64:$a, DR64:$b), (MUL32X16_L3 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzaa32x16_h0_l1 DR64:$a, DR64:$b), (MULZAA32X16_H0_L1 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzaa32x16_h1_l0 DR64:$a, DR64:$b), (MULZAA32X16_H1_L0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzaa32x16_h2_l3 DR64:$a, DR64:$b), (MULZAA32X16_H2_L3 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzaa32x16_h3_l2 DR64:$a, DR64:$b), (MULZAA32X16_H3_L2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzas32x16_h1_l0 DR64:$a, DR64:$b), (MULZAS32X16_H1_L0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzas32x16_h3_l2 DR64:$a, DR64:$b), (MULZAS32X16_H3_L2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzsa32x16_h1_l0 DR64:$a, DR64:$b), (MULZSA32X16_H1_L0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzsa32x16_h3_l2 DR64:$a, DR64:$b), (MULZSA32X16_H3_L2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzss32x16_h1_l0 DR64:$a, DR64:$b), (MULZSS32X16_H1_L0 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulzss32x16_h3_l2 DR64:$a, DR64:$b), (MULZSS32X16_H3_L2 DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2cmul32x16_h DR64:$a, DR64:$b), (X2CMUL32X16_H DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2cmul32x16_l DR64:$a, DR64:$b), (X2CMUL32X16_L DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fcmul32x16rs_h DR64:$a, DR64:$b), (X2FCMUL32X16RS_H DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fcmul32x16rs_l DR64:$a, DR64:$b), (X2FCMUL32X16RS_L DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmul32x16rss_h DR64:$a, DR64:$b), (X2FMUL32X16RSS_H DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmul32x16rss_l DR64:$a, DR64:$b), (X2FMUL32X16RSS_L DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmul32x16rs_h DR64:$a, DR64:$b), (X2FMUL32X16RS_H DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmul32x16rs_l DR64:$a, DR64:$b), (X2FMUL32X16RS_L DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmul32x16ts_h DR64:$a, DR64:$b), (X2FMUL32X16TS_H DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmul32x16ts_l DR64:$a, DR64:$b), (X2FMUL32X16TS_L DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2mul32x16_h DR64:$a, DR64:$b), (X2MUL32X16_H DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2mul32x16_l DR64:$a, DR64:$b), (X2MUL32X16_L DR64:$a, DR64:$b)>;

def : Pat<(int_haydn_fmula32x16_h0 DR64:$acc, DR64:$a, DR64:$b), (FMULA32X16_H0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmula32x16_h1 DR64:$acc, DR64:$a, DR64:$b), (FMULA32X16_H1 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmula32x16_h2 DR64:$acc, DR64:$a, DR64:$b), (FMULA32X16_H2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmula32x16_h3 DR64:$acc, DR64:$a, DR64:$b), (FMULA32X16_H3 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmula32x16_l0 DR64:$acc, DR64:$a, DR64:$b), (FMULA32X16_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmula32x16_l1 DR64:$acc, DR64:$a, DR64:$b), (FMULA32X16_L1 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmula32x16_l2 DR64:$acc, DR64:$a, DR64:$b), (FMULA32X16_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmula32x16_l3 DR64:$acc, DR64:$a, DR64:$b), (FMULA32X16_L3 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulaa32x16_h0_l1 DR64:$acc, DR64:$a, DR64:$b), (FMULAA32X16_H0_L1 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulaa32x16_h1_l0 DR64:$acc, DR64:$a, DR64:$b), (FMULAA32X16_H1_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulaa32x16_h2_l3 DR64:$acc, DR64:$a, DR64:$b), (FMULAA32X16_H2_L3 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulaa32x16_h3_l2 DR64:$acc, DR64:$a, DR64:$b), (FMULAA32X16_H3_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulas32x16_h1_l0 DR64:$acc, DR64:$a, DR64:$b), (FMULAS32X16_H1_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulas32x16_h3_l2 DR64:$acc, DR64:$a, DR64:$b), (FMULAS32X16_H3_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls32x16_h0 DR64:$acc, DR64:$a, DR64:$b), (FMULS32X16_H0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls32x16_h1 DR64:$acc, DR64:$a, DR64:$b), (FMULS32X16_H1 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls32x16_h2 DR64:$acc, DR64:$a, DR64:$b), (FMULS32X16_H2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls32x16_h3 DR64:$acc, DR64:$a, DR64:$b), (FMULS32X16_H3 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls32x16_l0 DR64:$acc, DR64:$a, DR64:$b), (FMULS32X16_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls32x16_l1 DR64:$acc, DR64:$a, DR64:$b), (FMULS32X16_L1 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls32x16_l2 DR64:$acc, DR64:$a, DR64:$b), (FMULS32X16_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmuls32x16_l3 DR64:$acc, DR64:$a, DR64:$b), (FMULS32X16_L3 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulsa32x16_h1_l0 DR64:$acc, DR64:$a, DR64:$b), (FMULSA32X16_H1_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulsa32x16_h3_l2 DR64:$acc, DR64:$a, DR64:$b), (FMULSA32X16_H3_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulss32x16_h1_l0 DR64:$acc, DR64:$a, DR64:$b), (FMULSS32X16_H1_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_fmulss32x16_h3_l2 DR64:$acc, DR64:$a, DR64:$b), (FMULSS32X16_H3_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mula32x16_h0 DR64:$acc, DR64:$a, DR64:$b), (MULA32X16_H0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mula32x16_h1 DR64:$acc, DR64:$a, DR64:$b), (MULA32X16_H1 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mula32x16_h2 DR64:$acc, DR64:$a, DR64:$b), (MULA32X16_H2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mula32x16_h3 DR64:$acc, DR64:$a, DR64:$b), (MULA32X16_H3 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mula32x16_l0 DR64:$acc, DR64:$a, DR64:$b), (MULA32X16_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mula32x16_l1 DR64:$acc, DR64:$a, DR64:$b), (MULA32X16_L1 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mula32x16_l2 DR64:$acc, DR64:$a, DR64:$b), (MULA32X16_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mula32x16_l3 DR64:$acc, DR64:$a, DR64:$b), (MULA32X16_L3 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulaa32x16_h0_l1 DR64:$acc, DR64:$a, DR64:$b), (MULAA32X16_H0_L1 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulaa32x16_h1_l0 DR64:$acc, DR64:$a, DR64:$b), (MULAA32X16_H1_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulaa32x16_h2_l3 DR64:$acc, DR64:$a, DR64:$b), (MULAA32X16_H2_L3 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulaa32x16_h3_l2 DR64:$acc, DR64:$a, DR64:$b), (MULAA32X16_H3_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulas32x16_h1_l0 DR64:$acc, DR64:$a, DR64:$b), (MULAS32X16_H1_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulas32x16_h3_l2 DR64:$acc, DR64:$a, DR64:$b), (MULAS32X16_H3_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_muls32x16_h0 DR64:$acc, DR64:$a, DR64:$b), (MULS32X16_H0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_muls32x16_h1 DR64:$acc, DR64:$a, DR64:$b), (MULS32X16_H1 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_muls32x16_h2 DR64:$acc, DR64:$a, DR64:$b), (MULS32X16_H2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_muls32x16_h3 DR64:$acc, DR64:$a, DR64:$b), (MULS32X16_H3 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_muls32x16_l0 DR64:$acc, DR64:$a, DR64:$b), (MULS32X16_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_muls32x16_l1 DR64:$acc, DR64:$a, DR64:$b), (MULS32X16_L1 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_muls32x16_l2 DR64:$acc, DR64:$a, DR64:$b), (MULS32X16_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_muls32x16_l3 DR64:$acc, DR64:$a, DR64:$b), (MULS32X16_L3 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulsa32x16_h1_l0 DR64:$acc, DR64:$a, DR64:$b), (MULSA32X16_H1_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulsa32x16_h3_l2 DR64:$acc, DR64:$a, DR64:$b), (MULSA32X16_H3_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulss32x16_h1_l0 DR64:$acc, DR64:$a, DR64:$b), (MULSS32X16_H1_L0 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_mulss32x16_h3_l2 DR64:$acc, DR64:$a, DR64:$b), (MULSS32X16_H3_L2 DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2cmula32x16_h DR64:$acc, DR64:$a, DR64:$b), (X2CMULA32X16_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2cmula32x16_l DR64:$acc, DR64:$a, DR64:$b), (X2CMULA32X16_L DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fcmula32x16rs_h DR64:$acc, DR64:$a, DR64:$b), (X2FCMULA32X16RS_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fcmula32x16rs_l DR64:$acc, DR64:$a, DR64:$b), (X2FCMULA32X16RS_L DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmula32x16rss_h DR64:$acc, DR64:$a, DR64:$b), (X2FMULA32X16RSS_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmula32x16rss_l DR64:$acc, DR64:$a, DR64:$b), (X2FMULA32X16RSS_L DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmula32x16rs_h DR64:$acc, DR64:$a, DR64:$b), (X2FMULA32X16RS_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmula32x16rs_l DR64:$acc, DR64:$a, DR64:$b), (X2FMULA32X16RS_L DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmula32x16ts_h DR64:$acc, DR64:$a, DR64:$b), (X2FMULA32X16TS_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmula32x16ts_l DR64:$acc, DR64:$a, DR64:$b), (X2FMULA32X16TS_L DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmuls32x16rss_h DR64:$acc, DR64:$a, DR64:$b), (X2FMULS32X16RSS_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmuls32x16rss_l DR64:$acc, DR64:$a, DR64:$b), (X2FMULS32X16RSS_L DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmuls32x16rs_h DR64:$acc, DR64:$a, DR64:$b), (X2FMULS32X16RS_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmuls32x16rs_l DR64:$acc, DR64:$a, DR64:$b), (X2FMULS32X16RS_L DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmuls32x16ts_h DR64:$acc, DR64:$a, DR64:$b), (X2FMULS32X16TS_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2fmuls32x16ts_l DR64:$acc, DR64:$a, DR64:$b), (X2FMULS32X16TS_L DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2mula32x16_h DR64:$acc, DR64:$a, DR64:$b), (X2MULA32X16_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2mula32x16_l DR64:$acc, DR64:$a, DR64:$b), (X2MULA32X16_L DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2muls32x16_h DR64:$acc, DR64:$a, DR64:$b), (X2MULS32X16_H DR64:$acc, DR64:$a, DR64:$b)>;
def : Pat<(int_haydn_x2muls32x16_l DR64:$acc, DR64:$a, DR64:$b), (X2MULS32X16_L DR64:$acc, DR64:$a, DR64:$b)>;
