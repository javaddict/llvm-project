; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -O2 \
; RUN:   -global-isel-abort=1 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
;
; REGRESSION TEST (2026-08-22, demote-first law; reworked same day for the
; topics/hwloop PM2 peer-verified realignment): the embench
; nsichneu / fir-f32 / lms class — a counted loop whose body ends up
; containing a call — must compile cleanly through FULL llc at default
; flags and never arm a hardware loop it cannot honor.
;
; Two arms, two pipeline points for the same law:
;   * @call_in_body: the call is visible in IR -> Haydn TTI
;     isHardwareLoopProfitable declines the loop outright (no seat armed).
;     Unchanged by the realignment.
;   * @softfloat_loop: the IR has NO call, but the body is fadd-on-f32 —
;     a WILL-lower-to-libcall op on Haydn's soft-float path. Before the
;     realignment, TTI accepted and armed the seat, soft-float
;     legalization then inserted __addsf3 JALs into the MachineIR body,
;     and preflight had to DEMOTE (with the isSoundDemoteCounter law
;     guarding the counter against call-clobber regmasks). After the PM2
;     AIE decline-list port (isAllowedInHwLoopBody: f32/f64 arith
;     declines — HaydnLegalizerInfo.cpp libcallFor {S32,S64}), TTI
;     declines at the same pipeline point AIE uses (IR, pre-ISel): NO
;     seat is armed at all, the loop compiles as a plain software loop,
;     and the __addsf3 call is just an ordinary call in an ordinary loop.
;     The preflight demote-for-calls path is thereby unreachable from IR
;     (kept only as the Off1/Off2 layout-range sink and hand-MIR
;     defense-in-depth).
;
; Contract under test (both arms): rc=0, NO set_hwloop emitted, a bnez
; -style counted back-edge present. The softfloat arm now additionally
; pins that acceptance never happened upstream: there is no
; SET_HWLOOP pseudo surviving post-ISel at all.

define dso_local void @call_in_body(ptr nocapture %p, i32 %n) nounwind {
; CHECK-LABEL: call_in_body:
; CHECK-NOT: set_hwloop
; CHECK: .LBB0_1:
; CHECK: jal
; CHECK: bnez {{r[0-9]+}}, .LBB0_1
; CHECK-NOT: set_hwloop
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %v = load i32, ptr %pi
  tail call void @sink(i32 %v)
  %inc = add i32 %i, 1
  %cmp = icmp ult i32 %inc, %n
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}

declare void @sink(i32)

define dso_local void @softfloat_loop(ptr nocapture %p, ptr nocapture readonly %q, i32 %n) nounwind {
; CHECK-LABEL: softfloat_loop:
; CHECK-NOT: set_hwloop
; CHECK: .LBB1_1:
; The fadd body lowers to __addsf3; the loop is a plain software loop
; (TTI decline), so the call sits in an ordinary bnez back-edge loop.
; CHECK: jal {{.*}}__addsf3
; CHECK: bnez {{r[0-9]+}}, .LBB1_1
; CHECK-NOT: set_hwloop
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
  %cmp = icmp ult i32 %inc, %n
  br i1 %cmp, label %body, label %exit

exit:
  ret void
}
