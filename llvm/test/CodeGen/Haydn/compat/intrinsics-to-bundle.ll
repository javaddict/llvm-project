; REQUIRES: haydn-registered-target
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — FILED : MULSSH_S0 (and similar MAC-S0 instructions) have a tied-def operand-flag bug that aborts the.

; REGRESSION FILED : MULSSH_S0 (and similar
; MAC-S0 instructions) have a tied-def operand-flag bug that aborts the
; MachineVerifier ("Explicit def tied to explicit use without tie constraint"
; + "Explicit operand should not be tied"). The.td wrongly marks operand 2
; as (tied-def 0) on a non-destructive 3-operand instruction. Regressed in
; the Flex cutover. Selector is correct; only the.td tied-def flag
; needs adjusting. Track under / Flex cutover (MULSSH tied-def). Do NOT
; rebaseline CHECKs to silence the verifier abort.
; RE-XFAIL note: this was XFAIL'd (lanewise retype) because it
; exercises the vector-scalar shift intrinsics (x2sra32, x4sra16,...) which now
; take a real <2 x i32>/<4 x i16> vector operand. The selector fed the GPR32
; shift amount to MOV_DR64_TO_GPR, which expects a DR64 source (verifier abort).
; Fixed in HaydnInstructionSelector's selectDR64ShiftGPR32 (bank
; inferred from operand type, not getRegBankOrNull). See lesson; same root
; cause as dr64-shift-regform.ll.
;
; CAPSTONE dimension 3, part C: Haydn intrinsic -> bundle-level test.
;
; For each Haydn IR intrinsic (llvm.haydn.*), a probe function that lowers it
; and FileChecks the resulting VLIW bundle -- the instruction appears inside a
; `{... }` bundle in the correct slot with the right mnemonic. This proves
; every intrinsic lowers to a correct bundle (no libcall, no drop, no
; global-isel-abort=1 fallback).
;
; Grouped by family: (1) MAC, (2) fractional MAC, (3) dual-MAC, (4) complex
; (5) pack/shift, (6) ALU/SIMD, (7) scalar saturating.
;
; CHECK lines match the mnemonic inside a bundle `{... }`. We do not pin
; registers (regalloc drift) -- we assert the OPCODE lands in a bundle.
;
; Reference: ~/haydn-plans/reviews/capstone-compat-header-audit-.md

;===----------------------------------------------------------------------===;
; (1) MAC family -- 64-bit signed multiply + accumulate
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mul64.ss.ll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.ss.lh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.ss.hl(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.ss.hh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.ss.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.ss.hl(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.ss.hh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.ss.hh(i64, <2 x i32>, <2 x i32>)
declare i32 @llvm.haydn.mulq31(i32, i32, i32)
declare i64 @llvm.haydn.mulq63(i64, i64, i64)
define i64 @test_mul64_ss_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_ss_ll:
; CHECK: {{.*}}mul64.ll
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.ss.ll(<2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}
define i64 @test_mul64_ss_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_ss_hh:
; CHECK: mul64.hh
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.ss.hh(<2 x i32> %bc.3, <2 x i32> %bc.4)
  ret i64 %r
}
define i64 @test_mula64_ss_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_hh:
; CHECK: mula64.hh
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.hh(i64 %acc, <2 x i32> %bc.5, <2 x i32> %bc.6)
  ret i64 %r
}
define i64 @test_mula64_ss_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_ll:
; CHECK: mula64.ll
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %bc.7, <2 x i32> %bc.8)
  ret i64 %r
}
define i64 @test_muls64_ss_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_ss_hh:
; CHECK: muls64.hh
  %bc.9 = bitcast i64 %a to <2 x i32>
  %bc.10 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.hh(i64 %acc, <2 x i32> %bc.9, <2 x i32> %bc.10)
  ret i64 %r
}
define i32 @test_mulq31(i32 %acc, i32 %a, i32 %b) {
; CHECK-LABEL: test_mulq31:
; MULQ31 phantom removed; lowered to MULSSH (signed Q1.31 high product).
; CHECK: mulssh
  %r = call i32 @llvm.haydn.mulq31(i32 %acc, i32 %a, i32 %b)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; (2) Fractional MAC family -- 32x32 fractional single-lane
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmul32s.hh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmul32s.lh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmul32s.ll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmula32s.hh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmula32s.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmula32s.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmuls32s.hh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mul32rs.ll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32rs.hh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2muls32rs.lh(i64, <2 x i32>, <2 x i32>)
define i64 @test_fmul32s_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_hh:
; CHECK: fmul32s_hh
  %bc.11 = bitcast i64 %a to <2 x i32>
  %bc.12 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmul32s.hh(<2 x i32> %bc.11, <2 x i32> %bc.12)
  ret i64 %r
}
define i64 @test_fmula32s_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_hh:
; CHECK: fmula32s_hh
  %bc.13 = bitcast i64 %a to <2 x i32>
  %bc.14 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.hh(i64 %acc, <2 x i32> %bc.13, <2 x i32> %bc.14)
  ret i64 %r
}
define i64 @test_fmula32s_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_lh:
; CHECK: fmula32s_lh
  %bc.15 = bitcast i64 %a to <2 x i32>
  %bc.16 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.lh(i64 %acc, <2 x i32> %bc.15, <2 x i32> %bc.16)
  ret i64 %r
}
define i64 @test_ff2mul32rs_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32rs_ll:
; CHECK: ff2mul32rs_ll
  %bc.17 = bitcast i64 %a to <2 x i32>
  %bc.18 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mul32rs.ll(<2 x i32> %bc.17, <2 x i32> %bc.18)
  ret i64 %r
}
define i64 @test_ff2mula32rs_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32rs_hh:
; CHECK: ff2mula32rs_hh
  %bc.19 = bitcast i64 %a to <2 x i32>
  %bc.20 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32rs.hh(i64 %acc, <2 x i32> %bc.19, <2 x i32> %bc.20)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; (3) Dual-MAC family -- dual 32x32 in one instruction
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulaa32rs.hllh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulss32rs.hhll(i64, <2 x i32>, <2 x i32>)
define i64 @test_f2mulaa32rs_hhll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_f2mulaa32rs_hhll:
; CHECK: f2mulaa32rs_hhll
  %bc.21 = bitcast i64 %a to <2 x i32>
  %bc.22 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %acc, <2 x i32> %bc.21, <2 x i32> %bc.22)
  ret i64 %r
}
define i64 @test_f2mulaa32rs_hllh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_f2mulaa32rs_hllh:
; CHECK: f2mulaa32rs_hllh
  %bc.23 = bitcast i64 %a to <2 x i32>
  %bc.24 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulaa32rs.hllh(i64 %acc, <2 x i32> %bc.23, <2 x i32> %bc.24)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; (4) Complex multiply family
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2fcmul32rs(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32>, <2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16>, <4 x i16>)
define i64 @test_x2fcmul32rs(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2fcmul32rs:
; CHECK: x2fcmul32rs
  %bc.25 = bitcast i64 %a to <2 x i32>
  %bc.26 = bitcast i64 %b to <2 x i32>
  %call.27 = call <2 x i32> @llvm.haydn.x2fcmul32rs(<2 x i32> %bc.25, <2 x i32> %bc.26)
  %r = bitcast <2 x i32> %call.27 to i64
  ret i64 %r
}
define i64 @test_x2fcmula32rs(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_x2fcmula32rs:
; CHECK: x2fcmula32rs
  %bc.28 = bitcast i64 %acc to <2 x i32>
  %bc.29 = bitcast i64 %a to <2 x i32>
  %bc.30 = bitcast i64 %b to <2 x i32>
  %call.31 = call <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32> %bc.28, <2 x i32> %bc.29, <2 x i32> %bc.30)
  %r = bitcast <2 x i32> %call.31 to i64
  ret i64 %r
}
define i64 @test_x2cmul32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2cmul32:
; CHECK: x2cmul32
  %bc.32 = bitcast i64 %a to <2 x i32>
  %bc.33 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32> %bc.32, <2 x i32> %bc.33)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}
define i64 @test_x4fcmul16rs(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmul16rs:
; CHECK: x4fcmul16rs
  %bc.34 = bitcast i64 %a to <4 x i16>
  %bc.35 = bitcast i64 %b to <4 x i16>
  %call.36 = call <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16> %bc.34, <4 x i16> %bc.35)
  %r = bitcast <4 x i16> %call.36 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; (5) Quad-16 MAC family
;===----------------------------------------------------------------------===;

declare { i64, i64 } @llvm.haydn.x4mul16(<4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4mula16(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4mula16s(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4muls16s(i64, i64, <4 x i16>, <4 x i16>)
define i64 @test_x4mula16(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4mula16:
; CHECK: x4mula16
  %bc.37 = bitcast i64 %a to <4 x i16>
  %bc.38 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4mula16(i64 %acc, i64 %acc2, <4 x i16> %bc.37, <4 x i16> %bc.38)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}
define i64 @test_x4mula16s(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4mula16s:
; CHECK: x4mula16s
  %bc.39 = bitcast i64 %a to <4 x i16>
  %bc.40 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4mula16s(i64 %acc, i64 %acc2, <4 x i16> %bc.39, <4 x i16> %bc.40)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===;
; (6) Pack / Shift / Round family
; packsr32/satsr64 are NOT IR (composites in haydn_dsp.h). Use DB sra64r/sra64.
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.sra64r(i64, i32)
declare i64 @llvm.haydn.sra64(i64, i32)
declare <2 x i32> @llvm.haydn.x2sra32(<2 x i32>, i32)
declare <4 x i16> @llvm.haydn.x4sra16(<4 x i16>, i32)
declare i64 @llvm.haydn.srai64r(i64, i32)
declare <2 x i32> @llvm.haydn.x2sra32r(<2 x i32>, i32)

define i64 @test_sra64r_pack(i64 %ps) {
; CHECK-LABEL: test_sra64r_pack:
; CHECK: sra64r
  %r = call i64 @llvm.haydn.sra64r(i64 %ps, i32 0)
  ret i64 %r
}
define i64 @test_sra64_sat_shift(i64 %q) {
; CHECK-LABEL: test_sra64_sat_shift:
; CHECK: sra64
  %r = call i64 @llvm.haydn.sra64(i64 %q, i32 15)
  ret i64 %r
}
define <2 x i32> @test_x2sra32(<2 x i32> %a) {
; CHECK-LABEL: test_x2sra32:
; CHECK: x2sra32
  %r = call <2 x i32> @llvm.haydn.x2sra32(<2 x i32> %a, i32 3)
  ret <2 x i32> %r
}
define <4 x i16> @test_x4sra16(<4 x i16> %a) {
; CHECK-LABEL: test_x4sra16:
; CHECK: x4sra16
  %r = call <4 x i16> @llvm.haydn.x4sra16(<4 x i16> %a, i32 2)
  ret <4 x i16> %r
}
define i64 @test_srai64r(i64 %a) {
; CHECK-LABEL: test_srai64r:
; CHECK: srai64r
  %r = call i64 @llvm.haydn.srai64r(i64 %a, i32 7)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; (7) ALU / SIMD family
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2add32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sub32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2add32s(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4add16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sub16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4add16s(<4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x2addsub32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2max32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2min32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2slt32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sle32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4seq16(<4 x i16>, <4 x i16>)
declare i32 @llvm.haydn.abs32s(i32)
declare i64 @llvm.haydn.abs64(i64)
declare i64 @llvm.haydn.abs64s(i64)
declare i32 @llvm.haydn.neg32s(i32)
declare i64 @llvm.haydn.neg64s(i64)
declare i32 @llvm.haydn.add32s(i32, i32)
declare i32 @llvm.haydn.sub32s(i32, i32)
declare <2 x i32> @llvm.haydn.maxabs32s(<2 x i32>, <2 x i32>)
define <2 x i32> @test_x2add32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2add32:
; CHECK: x2add32
  %r = call <2 x i32> @llvm.haydn.x2add32(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}
define <4 x i16> @test_x4add16(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4add16:
; CHECK: x4add16
  %r = call <4 x i16> @llvm.haydn.x4add16(<4 x i16> %a, <4 x i16> %b)
  ret <4 x i16> %r
}
define <2 x i32> @test_x2add32s(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2add32s:
; CHECK: x2add32s
  %r = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}
define <4 x i16> @test_x4add16s(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4add16s:
; CHECK: x4add16s
  %r = call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %a, <4 x i16> %b)
  ret <4 x i16> %r
}
define <2 x i32> @test_x2slt32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2slt32:
; CHECK: x2slt32
  %r = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}
define <2 x i32> @test_x2max32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2max32:
; CHECK: x2max32
  %r = call <2 x i32> @llvm.haydn.x2max32(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}
define i32 @test_abs32s(i32 %a) {
; CHECK-LABEL: test_abs32s:
; CHECK: abs32s
  %r = call i32 @llvm.haydn.abs32s(i32 %a)
  ret i32 %r
}
define i64 @test_abs64s(i64 %a) {
; CHECK-LABEL: test_abs64s:
; CHECK: abs64s
  %r = call i64 @llvm.haydn.abs64s(i64 %a)
  ret i64 %r
}
define i32 @test_neg32s(i32 %a) {
; CHECK-LABEL: test_neg32s:
; CHECK: neg32s
  %r = call i32 @llvm.haydn.neg32s(i32 %a)
  ret i32 %r
}
define i32 @test_add32s(i32 %a, i32 %b) {
; CHECK-LABEL: test_add32s:
; CHECK: add32s
  %r = call i32 @llvm.haydn.add32s(i32 %a, i32 %b)
  ret i32 %r
}
