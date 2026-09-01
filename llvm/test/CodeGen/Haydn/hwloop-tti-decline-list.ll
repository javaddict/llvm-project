; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 \
; RUN:   -mattr=+hwloop -debug-only=haydn-tti < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s
; REQUIRES: asserts
; REQUIRES: haydn-registered-target

; REGRESSION TEST (2026-08-22, topics/hwloop PM2 realignment): the AIE
; decline-list port must gate Haydn hardware-loop acceptance on ops that
; WILL lower to libcalls on the soft-float path — at IR, pre-ISel, the
; same pipeline point AIE uses (AIEBaseTargetTransformInfo.cpp:91-121
; isLoweredToCall / :123-182 isAllowedInZOL).
;
; Bug class this pins: before the port, TTI accepted loops with fadd /
; fcmp / fpext / sdiv / memcpy bodies; soft-float legalization then
; inserted __addsf3-style JALs into the MachineIR body, and the loop had
; to be rescued by the formation-time preflight DEMOTE (the
; demote-late-call class, with its counter-ownership hazards).
;
; Haydn overlay laws pinned here (HaydnLegalizerInfo.cpp:618-725):
;   * f32 AND f64 arith/cmp/converts decline (no FPU at any width — AIE
;     keeps f32 because it HAS an f32 unit; Haydn does not).
;   * f32 loads/stores stay ALLOWED (plain GPR/DR moves).
;   * int div/rem declines (libcall).
;   * i64 mul stays ALLOWED (custom widen/schoolbook — never __muldi3,
;     unlike AIE's >32-bit mul decline).
;   * memcpy intrinsic declines.
;
; Test design: debug-stream pins (same shape as
; hwloop-tti-trip-value-gate.ll). The integer control arm must ACCEPT
; (guards against over-tightening); every decline arm must print the
; decline-list debug line. Query multiplicity varies, so only presence
; is pinned, not counts.

; CONTROL — pure integer MAC stream: must still ACCEPT.
define void @int_mac(ptr %p, ptr %q, i32 %n) {
; CHECK: Haydn HWLoop(IR): accepted innermost ZOL candidate
entry:
  br label %body

body:
  %i = phi i64 [ 0, %entry ], [ %next, %body ]
  %pi = getelementptr i32, ptr %p, i64 %i
  %qi = getelementptr i32, ptr %q, i64 %i
  %a = load i32, ptr %pi
  %b = load i32, ptr %qi
  %m = mul i64 %i, 3
  %s = add i32 %a, %b
  store i32 %s, ptr %pi
  %next = add i64 %i, 1
  %cmp = icmp slt i64 %next, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

; f32 arith in body — soft-float libcall: DECLINE.
define void @f32_arith(ptr %p, ptr %q, i32 %n) {
; CHECK: Haydn HWLoop(IR): body op lowers to a call — declined
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %pi = getelementptr float, ptr %p, i32 %i
  %qi = getelementptr float, ptr %q, i32 %i
  %a = load float, ptr %pi
  %b = load float, ptr %qi
  %s = fadd float %a, %b
  store float %s, ptr %pi
  %inc = add i32 %i, 1
  %cmp = icmp ult i32 %inc, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

; f64 arith in body — soft-float libcall: DECLINE (AIE also declines doubles).
define void @f64_arith(ptr %p, ptr %q, i32 %n) {
; CHECK: Haydn HWLoop(IR): body op lowers to a call — declined
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %pi = getelementptr double, ptr %p, i32 %i
  %qi = getelementptr double, ptr %q, i32 %i
  %a = load double, ptr %pi
  %b = load double, ptr %qi
  %s = fsub double %a, %b
  store double %s, ptr %pi
  %inc = add i32 %i, 1
  %cmp = icmp ult i32 %inc, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

; f32 compare feeding the exit — __lesf2-style libcall: DECLINE.
define void @f32_cmp(ptr %p, ptr %q, i32 %n) {
; CHECK: Haydn HWLoop(IR): body op lowers to a call — declined
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %pi = getelementptr float, ptr %p, i32 %i
  %qi = getelementptr float, ptr %q, i32 %i
  %a = load float, ptr %pi
  %b = load float, ptr %qi
  %fc = fcmp olt float %a, %b
  %inc = add i32 %i, 1
  %tc = icmp ult i32 %inc, 100
  %cmp = and i1 %fc, %tc
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

; fptrunc f64->f32 in body — compiler-rt conversion libcall: DECLINE.
define void @fptrunc_body(ptr %p, ptr %q, i32 %n) {
; CHECK: Haydn HWLoop(IR): body op lowers to a call — declined
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %pi = getelementptr double, ptr %p, i32 %i
  %qi = getelementptr float, ptr %q, i32 %i
  %a = load double, ptr %pi
  %t = fptrunc double %a to float
  store float %t, ptr %qi
  %inc = add i32 %i, 1
  %cmp = icmp ult i32 %inc, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

; sitofp in body — conversion libcall: DECLINE.
define void @sitofp_body(ptr %p, i32 %n) {
; CHECK: Haydn HWLoop(IR): body op lowers to a call — declined
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %pi = getelementptr float, ptr %p, i32 %i
  %t = sitofp i32 %i to float
  store float %t, ptr %pi
  %inc = add i32 %i, 1
  %cmp = icmp ult i32 %inc, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

; integer sdiv in body — __divsi3 libcall: DECLINE.
define void @int_div(ptr %p, ptr %q, i32 %n) {
; CHECK: Haydn HWLoop(IR): body op lowers to a call — declined
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %qi = getelementptr i32, ptr %q, i32 %i
  %a = load i32, ptr %pi
  %b = load i32, ptr %qi
  %d = sdiv i32 %a, %b
  store i32 %d, ptr %pi
  %inc = add i32 %i, 1
  %cmp = icmp ult i32 %inc, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

; memcpy intrinsic in body — lowers to a call in general shape: DECLINE.
define void @memcpy_body(ptr %p, ptr %q, i32 %n) {
; CHECK: Haydn HWLoop(IR): body op lowers to a call — declined
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %qi = getelementptr i32, ptr %q, i32 %i
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %pi, ptr align 4 %qi, i32 16, i1 false)
  %inc = add i32 %i, 1
  %cmp = icmp ult i32 %inc, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1)

; Generic FP-math intrinsic (llvm.sqrt) in body — G_FSQRT is in the bulk
; libcall set (HaydnLegalizerInfo.cpp:631-646): DECLINE. Pins the
; fail-closed intrinsic policy added with the decline-list port: only
; llvm.haydn.* target intrinsics (inline ISel patterns — see
; dsp-intrinsic-e2e.ll / quad-mac-coissue.ll, which now ARM) and no-code
; annotations pass; every unlisted intrinsic declines.
define void @sqrt_intrinsic_body(ptr %p, ptr %q, i32 %n) {
; CHECK: Haydn HWLoop(IR): body op lowers to a call — declined
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %pi = getelementptr float, ptr %p, i32 %i
  %qi = getelementptr float, ptr %q, i32 %i
  %a = load float, ptr %qi
  %s = call float @llvm.sqrt.f32(float %a)
  store float %s, ptr %pi
  %inc = add i32 %i, 1
  %cmp = icmp ult i32 %inc, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

declare float @llvm.sqrt.f32(float)

; Target intrinsic in body — selects INLINE (mul64.ll ISel pattern,
; IntrinsicsHaydn.td): must still ACCEPT. The AIE-shaped call scan
; exists precisely so inline DSP kernels keep their ZOL seat.
define void @haydn_intrinsic_body(ptr %p, ptr %q, i32 %n) {
; CHECK: Haydn HWLoop(IR): accepted innermost ZOL candidate
entry:
  br label %body

body:
  %i = phi i64 [ 0, %entry ], [ %inc, %body ]
  %pi = getelementptr i64, ptr %p, i64 %i
  %qi = getelementptr i64, ptr %q, i64 %i
  %a = load i64, ptr %pi
  %b = load i64, ptr %qi
  %av = bitcast i64 %a to <2 x i32>
  %bv = bitcast i64 %b to <2 x i32>
  %m = call i64 @llvm.haydn.mul64.ss.ll(<2 x i32> %av, <2 x i32> %bv)
  store i64 %m, ptr %pi
  %inc = add i64 %i, 1
  %cmp = icmp slt i64 %inc, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

declare i64 @llvm.haydn.mul64.ss.ll(<2 x i32>, <2 x i32>)

; f32 LOAD/STORE only (moves, no FP arith): must still ACCEPT — pins that
; the decline list does not over-reach into plain FP memory streams.
define void @f32_ldst_only(ptr %p, ptr %q, i32 %n) {
; CHECK: Haydn HWLoop(IR): accepted innermost ZOL candidate
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %pi = getelementptr float, ptr %p, i32 %i
  %qi = getelementptr float, ptr %q, i32 %i
  %a = load float, ptr %qi
  store float %a, ptr %pi
  %inc = add i32 %i, 1
  %cmp = icmp ult i32 %inc, 100
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}
