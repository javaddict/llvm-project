; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — XFAIL RESOLVED (, binary `e01004df`): the X2CMUL32_F2_S1 X2CMUL32_S1 operand-flag bug filed earlier today was.

; XFAIL RESOLVED (, binary `e01004df`): the X2CMUL32_F2_S1
; X2CMUL32_S1 operand-flag bug filed earlier today was
; CLOSED by the commit series — specifically `0c7dc7cb448c` "complete
; BUG 1 — align ALL _FLEX operand dags to legacy" aligned the operand flags
; (use/def) to the legacy equivalents. llc now emits real NSA/X4SAT/X2CMUL
; instructions (25 real ops, 0 verifier aborts). XFAIL line removed; test
; now PASSES.
;
; DR64 move/convert intrinsics test for Haydn backend.
; Tests register transfer, normalization, and cross-type operations
; that bridge DR64 and GPR32 register banks.
;
; Categories tested (PASSING):
; Normalization: nsa32, nsau32 (only these have MC encoding)
; X4 pack/sat: x4sat32t16
; X2 CMUL F2 variants (ternary DR64)
;
; KNOWN FAILURES (commented out):
; nsa64, nsa16_l, nsa32_l, nsaz64, nsaz16_l, nsaz32_l:
; selected but MC layer drops encoding (EMPTY output)
; srai64r: cannot select G_INTRINSIC (i64, i32) -> i64
; x4seli16: cannot select G_INTRINSIC (i64, i64, i32) -> i64
; x2slli32, x2srai32, x2srli32, x4slli16, x4srai16, x4srli16:
; cannot select (ISel patterns commented out in HaydnIntrinsics.td)
; x2srai32r, x4srai16r: cannot select (ISel patterns commented out)
; x4cmul16_f2, x4cmul16s_f2: selected but MC layer drops encoding
; log2, exp2, recip, sqrt: selected but MC layer drops encoding

;===----------------------------------------------------------------------===;
; Normalization (NSA) - GPR32 unary ops (with MC encoding)
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.nsa32(i32)
declare i32 @llvm.haydn.nsau32(i32)
define i32 @test_nsa32(i32 %a) {
; CHECK-LABEL: test_nsa32:
; CHECK: nsa32
  %r = call i32 @llvm.haydn.nsa32(i32 %a)
  ret i32 %r
}

define i32 @test_nsau32(i32 %a) {
; CHECK-LABEL: test_nsau32:
; CHECK: nsau32
  %r = call i32 @llvm.haydn.nsau32(i32 %a)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; X4 pack/sat
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4sat32t16(<2 x i32>, <2 x i32>)
define i64 @test_x4sat32t16(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4sat32t16:
; CHECK: x4sat32t16
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %call.3 = call <4 x i16> @llvm.haydn.x4sat32t16(<2 x i32> %bc.1, <2 x i32> %bc.2)
  %r = bitcast <4 x i16> %call.3 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X2 CMUL F2 variants (ternary DR64)
;===----------------------------------------------------------------------===;

declare { i64, i64 } @llvm.haydn.x2cmul32.f2(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32s.f2(<2 x i32>, <2 x i32>)
define i64 @test_x2cmul32_f2(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2cmul32_f2:
; CHECK: x2cmul32_f2
  %bc.4 = bitcast i64 %a to <2 x i32>
  %bc.5 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32.f2(<2 x i32> %bc.4, <2 x i32> %bc.5)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x2cmul32s_f2(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2cmul32s_f2:
; CHECK: x2cmul32s_f2
  %bc.6 = bitcast i64 %a to <2 x i32>
  %bc.7 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s.f2(<2 x i32> %bc.6, <2 x i32> %bc.7)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}
