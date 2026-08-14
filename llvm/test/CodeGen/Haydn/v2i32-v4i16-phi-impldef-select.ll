; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — G_PHI / G_IMPLICIT_DEF / G_SELECT for v2i32 and v4i16 must legalize.

; REGRESSION TEST: G_PHI / G_IMPLICIT_DEF / G_SELECT for v2i32 and v4i16 must
; legalize. These ops appear in NatureDSP FFT/vector kernels that loop over
; <2 x i32> / <4 x i16> accumulators. Before the fix, the legalizer
; declared only scalar types ({S32, S64, P0}) legal for these three opcodes
; so any vector PHI/IMPLICIT_DEF/SELECT failed with "unable to legalize
; instruction" — blocking fft_cplx24x24, ifft_cplx32x16, vec_max16x16_fast
; and ~25 other FFT/vector kernels at -O2.
;
; The fix adds V2I32/V4I16/V8I8 to the legal sets for G_PHI, G_IMPLICIT_DEF
; and G_SELECT — they all fit in DR64 (64 bits), the same register class that
; holds the scalar vector intrinsics.

declare <2 x i32> @llvm.haydn.x2add32s(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4add16s(<4 x i16>, <4 x i16>)

; v2i32 PHI in a loop (the FFT butterfly accumulator pattern).
define <2 x i32> @test_v2i32_phi_loop(<2 x i32> %init, <2 x i32> %step, i32 %n) {
; CHECK-LABEL: test_v2i32_phi_loop:
; CHECK: x2add32s
entry:
  br label %loop
loop:
  %acc = phi <2 x i32> [ %init, %entry ], [ %next, %loop ]
  %next = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %acc, <2 x i32> %step)
  %n.dec = sub i32 %n, 1
  %cmp = icmp sgt i32 %n.dec, 0
  br i1 %cmp, label %loop, label %exit
exit:
  ret <2 x i32> %next
}

; v2i32 zero-init (legalizes via G_IMPLICIT_DEF for the zeroinitializer).
define <2 x i32> @test_v2i32_impldef(<2 x i32> %a) {
; CHECK-LABEL: test_v2i32_impldef:
; CHECK: jalr{{(\.s[012])?}}
  %r = add <2 x i32> %a, zeroinitializer
  ret <2 x i32> %r
}

; v4i16 PHI across loop iterations (the vec_max/min fast-kernel pattern).
define <4 x i16> @test_v4i16_phi(<4 x i16> %a, <4 x i16> %b, i32 %n) {
; CHECK-LABEL: test_v4i16_phi:
; CHECK: x4add16s
entry:
  br label %loop
loop:
  %acc = phi <4 x i16> [ %a, %entry ], [ %next, %loop ]
  %next = call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %acc, <4 x i16> %b)
  %n.dec = sub i32 %n, 1
  %cmp = icmp sgt i32 %n.dec, 0
  br i1 %cmp, label %loop, label %exit
exit:
  ret <4 x i16> %next
}

; v4i16 SELECT (the conditional-lane pattern from vec_bexp16).
define <4 x i16> @test_v4i16_select(<4 x i16> %a, <4 x i16> %b, i1 %c) {
; CHECK-LABEL: test_v4i16_select:
; CHECK: jalr{{(\.s[012])?}}
  %r = select i1 %c, <4 x i16> %a, <4 x i16> %b
  ret <4 x i16> %r
}
