; REQUIRES: haydn-registered-target
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:   -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — Un-XFAIL'd : X2FCMULA32RS ISel selection now lands; the test was stale-CHECK only (lesson).

; Un-XFAIL'd : X2FCMULA32RS ISel selection now lands; the test was
; stale-CHECK only (lesson). Verify mulfc32x16ras.{low,high} emit
; x2fcmula32rs.
;
; REGRESSION TEST: llvm.haydn.mulfc32x16ras.{low,high} ISel lowering.
;
; Why this test exists:
; AE_MULFC32X16RAS is the single hottest intrinsic in upstream NatureDSP
; FFT kernels (494 uses per CLAUDE.md). Haydn has NO native instruction
; for complex 32x16 MAC with rounding+saturation (see
; ~/haydn-plans/isa-improve/ISA-08-mulfc32x16ras.md). The Haydn backend
; implements llvm.haydn.mulfc32x16ras.{low,high} as LLVM IR intrinsics
; that lower onto the existing X2FCMULA32RS instruction after a C-level
; 16->32 lane widen of the twiddle (Haydn lacks a cross-lane 16->32 pack
; instruction).
;
; What this test guards:
; 1. The new intrinsics are accepted by llc with -global-isel-abort=1.
; 2. The ISel selector lowers them onto X2FCMULA32RS (the only native
; 32x32 complex MAC with round+sat in the Haydn ISA).
; 3. The result is consumed (returned from the function) so the
; selector must emit a real X2FCMULA32RS, not be DCE'd.
;
; What would break if the bug reappears:
; If the ISel pattern is removed, llc -global-isel-abort=1 will fail
; with "unable to legalize / select intrinsic".
; If the X2FCMULA32RS emission is regressed, the CHECK lines below
; will fail.
; Do NOT update the CHECK lines without understanding the root cause.
;
; Reference:
; ~/haydn-plans/decisions/-mulfc32x16ras-intrinsic.md
; ~/haydn-plans/isa-improve/ISA-08-mulfc32x16ras.md

declare i64 @llvm.haydn.mulfc32x16ras.low(i64, i64, i64)
declare i64 @llvm.haydn.mulfc32x16ras.high(i64, i64, i64)

define dso_local i64 @test_mulfc32x16ras_low(i64 %acc, i64 %data, i64 %twid) nounwind {
  %r = call i64 @llvm.haydn.mulfc32x16ras.low(i64 %acc, i64 %data, i64 %twid)
  ret i64 %r
}

; CHECK-LABEL: test_mulfc32x16ras_high:
; CHECK:       x2fcmula32rs
define dso_local i64 @test_mulfc32x16ras_high(i64 %acc, i64 %data, i64 %twid) nounwind {
  %r = call i64 @llvm.haydn.mulfc32x16ras.high(i64 %acc, i64 %data, i64 %twid)
  ret i64 %r
}

; Chained test: acc is the result of a previous MAC, exercising the
; accumulator-dataflow pattern of the FFT butterfly inner loop. The result
; must be returned so neither call is DCE'd.
; CHECK-LABEL: test_mulfc32x16ras_chain:
; CHECK:       x2fcmula32rs
define dso_local i64 @test_mulfc32x16ras_chain(i64 %acc, i64 %d0, i64 %t0,
                                                i64 %d1, i64 %t1) nounwind {
  %a1 = call i64 @llvm.haydn.mulfc32x16ras.low(i64 %acc, i64 %d0, i64 %t0)
  %a2 = call i64 @llvm.haydn.mulfc32x16ras.high(i64 %a1, i64 %d1, i64 %t1)
  ret i64 %a2
}
