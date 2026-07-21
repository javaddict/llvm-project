; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; REGRESSION TEST: full soft-float for Haydn.
;
; Haydn has no FPU: every f32 lives in a GPR32, every f64 in a DR64, as the
; IEEE bit-pattern. The GISel legalizer lowers every FP op to a runtime libcall
; (compiler-rt builtins __addsf3/__adddf3/...), with three exception classes
; that previously CRASHED ("unable to legalize instruction") and are fixed in
;
; 1. G_FCOPYSIGN — no case in LegalizerHelper::libcall; now custom
; bit-trick-lowered (sign-bit graft via AND/OR). Must NOT emit a copysign
; libcall. If this regresses, llc aborts "unable to legalize G_FCOPYSIGN".
; 2. G_FFLOOR/G_FCEIL/G_FRINT/G_FMINNUM/G_FMAXNUM — cases exist but the
; libcall impls were unset for the baremetal triple (getLibcallName null)
; > crash; now bound to floorf/ceilf/rintf/fminf/fmaxf (clean libm
; link-error, not a compile crash).
;
; Calls lower to `jal_w lr, <symbol>` inside VLIW bundles.

; float add -> __addsf3
define float @fadd(float %a, float %b) {
; CHECK-LABEL: fadd:
; CHECK: jal_w{{(\.s[012])?}}	lr, __addsf3
  %r = fadd float %a, %b
  ret float %r
}

; double mul -> __muldf3
define double @dmul(double %a, double %b) {
; CHECK-LABEL: dmul:
; CHECK: jal_w{{(\.s[012])?}}	lr, __muldf3
  %r = fmul double %a, %b
  ret double %r
}

; double->float trunc -> __truncdfsf2
define float @d2f(double %a) {
; CHECK-LABEL: d2f:
; CHECK: jal_w{{(\.s[012])?}}	lr, __truncdfsf2
  %r = fptrunc double %a to float
  ret float %r
}

; float->int -> __fixsfsi (must truncate, not round —)
define i32 @f2i(float %a) {
; CHECK-LABEL: f2i:
; CHECK: jal_w{{(\.s[012])?}}	lr, __fixsfsi
  %r = fptosi float %a to i32
  ret i32 %r
}

; float compare eq -> __eqsf2
define i1 @feq(float %a, float %b) {
; CHECK-LABEL: feq:
; CHECK: jal_w{{(\.s[012])?}}	lr, __eqsf2
  %r = fcmp oeq float %a, %b
  ret i1 %r
}

; copysign: bit-trick (AND + OR), NO copysign libcall (fix #1)
define float @fcopysign(float %a, float %b) {
; CHECK-LABEL: fcopysign:
; CHECK-NOT: jal_w{{(\.s[012])?}}	lr, copysign
; CHECK-NOT: jal_w{{(\.s[012])?}}	lr, __copysign
; CHECK: and32
; CHECK: or32
  %r = call float @llvm.copysign.f32(float %a, float %b)
  ret float %r
}

; floor: emits floorf libcall, does NOT crash (fix #2)
define float @ffloor(float %a) {
; CHECK-LABEL: ffloor:
; CHECK: jal_w{{(\.s[012])?}}	lr, floorf
  %r = call float @llvm.floor.f32(float %a)
  ret float %r
}

; fmin: emits fminf libcall, does NOT crash (fix #2)
define float @fmin(float %a, float %b) {
; CHECK-LABEL: fmin:
; CHECK: jal_w{{(\.s[012])?}}	lr, fminf
  %r = call float @llvm.minnum.f32(float %a, float %b)
  ret float %r
}

declare float @llvm.copysign.f32(float, float)
declare float @llvm.floor.f32(float)
declare float @llvm.minnum.f32(float, float)
