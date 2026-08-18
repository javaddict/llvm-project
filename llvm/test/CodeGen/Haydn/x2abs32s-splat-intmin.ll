; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s

; Role: semantic — G_BUILD_VECTOR / splat of INT_MIN into v2i32 must dual-lane pack both 0x80000000 halves — not SEXT32T64 (which yields 0xFFFFFFFF80000000).

; G_BUILD_VECTOR / splat of INT_MIN into v2i32 must dual-lane pack both
; 0x80000000 halves — not SEXT32T64 (which yields 0xFFFFFFFF80000000).
; PostSelectOptimize used to fold MOV_GPR_TO_DR64 x,x → SEXT, breaking
; __haydn_x2abs32s(splat(INT_MIN)) (BundleSim intrin_x2simd exit 6).

declare <2 x i32> @llvm.haydn.x2abs32s(<2 x i32>)

define i32 @x2abs_splat_intmin() {
; CHECK-LABEL: x2abs_splat_intmin:
; CHECK: // %bb.0:
; Must not sign-extend a single word into the DR.
; CHECK-NOT: sext32t64
; CHECK: x2abs32s
; CHECK: jalr{{.*}}lr
entry:
  %0 = call <2 x i32> @llvm.haydn.x2abs32s(<2 x i32> <i32 -2147483648, i32 -2147483648>)
  %1 = bitcast <2 x i32> %0 to i64
  ; 0x7fffffff7fffffff
  %eq = icmp eq i64 %1, 9223372034707292159
  %r = select i1 %eq, i32 0, i32 6
  ret i32 %r
}
