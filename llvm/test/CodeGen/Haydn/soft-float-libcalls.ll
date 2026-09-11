; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Soft-float surface for Haydn (no FPU): * arith/cmp/convert → compiler-rt (__addsf3, __eqsf2, …).

; Soft-float surface for Haydn (no FPU):
;   * arith/cmp/convert → compiler-rt (__addsf3, __eqsf2, …)
;   * floor/minnum      → libm (floorf/fminf); compile ok, link needs libm
;   * copysign          → generic .lower (AND/OR), no copysign call
;   * minimum/maximum*  → mapped to fminnum/fmaxnum → fminf/fmaxf
;   * is.fpclass        → generic .lower (integer bit tests), no libcall

; float add -> __addsf3

define float @fadd(float %a, float %b) {
; CHECK-LABEL: fadd:
; CHECK: lui{{.*}}__addsf3
; CHECK: addi32{{.*}}__addsf3
; CHECK: jalr{{.*}}lr
  %r = fadd float %a, %b
  ret float %r
}

; double mul -> __muldf3
define double @dmul(double %a, double %b) {
; CHECK-LABEL: dmul:
; CHECK: lui{{.*}}__muldf3
; CHECK: addi32{{.*}}__muldf3
; CHECK: jalr{{.*}}lr
  %r = fmul double %a, %b
  ret double %r
}

; double->float trunc -> __truncdfsf2
define float @d2f(double %a) {
; CHECK-LABEL: d2f:
; CHECK: lui{{.*}}__truncdfsf2
; CHECK: addi32{{.*}}__truncdfsf2
; CHECK: jalr{{.*}}lr
  %r = fptrunc double %a to float
  ret float %r
}

; float->int -> __fixsfsi
define i32 @f2i(float %a) {
; CHECK-LABEL: f2i:
; CHECK: lui{{.*}}__fixsfsi
; CHECK: addi32{{.*}}__fixsfsi
; CHECK: jalr{{.*}}lr
  %r = fptosi float %a to i32
  ret i32 %r
}

; float compare eq -> __eqsf2
define i1 @feq(float %a, float %b) {
; CHECK-LABEL: feq:
; CHECK: lui{{.*}}__eqsf2
; CHECK: addi32{{.*}}__eqsf2
; CHECK: jalr{{.*}}lr
  %r = fcmp oeq float %a, %b
  ret i1 %r
}

; copysign: bit-trick (AND + OR), NO copysign libcall
define float @fcopysign(float %a, float %b) {
; CHECK-LABEL: fcopysign:
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}copysign
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__copysign
; CHECK: and32
; CHECK: or32
  %r = call float @llvm.copysign.f32(float %a, float %b)
  ret float %r
}

; floor: floorf libcall
define float @ffloor(float %a) {
; CHECK-LABEL: ffloor:
; CHECK: lui{{.*}}floorf
; CHECK: addi32{{.*}}floorf
; CHECK: jalr{{.*}}lr
  %r = call float @llvm.floor.f32(float %a)
  ret float %r
}

; fminnum: fminf libcall
define float @fmin(float %a, float %b) {
; CHECK-LABEL: fmin:
; CHECK: lui{{.*}}fminf
; CHECK: addi32{{.*}}fminf
; CHECK: jalr{{.*}}lr
  %r = call float @llvm.minnum.f32(float %a, float %b)
  ret float %r
}

; fmaxnum: fmaxf libcall
define float @fmax(float %a, float %b) {
; CHECK-LABEL: fmax:
; CHECK: lui{{.*}}fmaxf
; CHECK: addi32{{.*}}fmaxf
; CHECK: jalr{{.*}}lr
  %r = call float @llvm.maxnum.f32(float %a, float %b)
  ret float %r
}

; llvm.minimum → fminnum path → fminf (no legalizer crash)
define float @fminimum(float %a, float %b) {
; CHECK-LABEL: fminimum:
; CHECK: lui{{.*}}fminf
; CHECK: addi32{{.*}}fminf
; CHECK: jalr{{.*}}lr
  %r = call float @llvm.minimum.f32(float %a, float %b)
  ret float %r
}

; llvm.maximum → fmaxf
define float @fmaximum(float %a, float %b) {
; CHECK-LABEL: fmaximum:
; CHECK: lui{{.*}}fmaxf
; CHECK: addi32{{.*}}fmaxf
; CHECK: jalr{{.*}}lr
  %r = call float @llvm.maximum.f32(float %a, float %b)
  ret float %r
}

; is.fpclass: integer bit tests, no runtime call
define i1 @fisnan(float %a) {
; CHECK-LABEL: fisnan:
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}isnan
; CHECK: jalr
  %r = call i1 @llvm.is.fpclass.f32(float %a, i32 3)
  ret i1 %r
}

; double copysign: AND64/OR64, no libcall
define double @dcopysign(double %a, double %b) {
; CHECK-LABEL: dcopysign:
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}copysign
; CHECK: and64
; CHECK: or64
  %r = call double @llvm.copysign.f64(double %a, double %b)
  ret double %r
}

declare float @llvm.copysign.f32(float, float)
declare double @llvm.copysign.f64(double, double)
declare float @llvm.floor.f32(float)
declare float @llvm.minnum.f32(float, float)
declare float @llvm.maxnum.f32(float, float)
declare float @llvm.minimum.f32(float, float)
declare float @llvm.maximum.f32(float, float)
declare i1 @llvm.is.fpclass.f32(float, i32)
