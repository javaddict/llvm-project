; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms=false \
; RUN:   < %s | FileCheck %s --check-prefix=HWON

; Role: semantic — independent Role-A qualification while product
; defaults stay OFF. HWON force-enables hardware loops with multi-stage
; SMS explicitly OFF. Combined dual-ON and default-ON stay out of scope.

define i32 @const_trip(ptr %p) {
; DEFAULT-LABEL: const_trip:
; DEFAULT-NOT: set_hwloop
; DEFAULT: bnez
; HWON-LABEL: const_trip:
; Inner product selector only; never a free CSR program of HWLR.
; HWON: set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON-NOT: csrw
; HWON-NOT: bnez
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.n, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  %v = load i32, ptr %p
  %s.n = add i32 %s, %v
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, 8
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %s.n
}

define i32 @runtime_trip(ptr %p, i32 %n) {
; DEFAULT-LABEL: runtime_trip:
; DEFAULT-NOT: set_hwloop
; HWON-LABEL: runtime_trip:
; HWON: set_hwloop_f2 0,
; HWON-NOT: csrw
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [ 0, %pre ], [ %i.n, %loop ]
  %s = phi i32 [ 0, %pre ], [ %s.n, %loop ]
  %v = load i32, ptr %p
  %s.n = add i32 %s, %v
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  ret i32 %r
}

declare void @side_effect(i32)
define void @call_reject(i32 %n) {
; DEFAULT-LABEL: call_reject:
; DEFAULT-NOT: set_hwloop
; HWON-LABEL: call_reject:
; HWON-NOT: set_hwloop
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.n, %loop ]
  call void @side_effect(i32 %i)
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

define i32 @multibb_decline(ptr %p, ptr %q, i32 %n) {
; DEFAULT-LABEL: multibb_decline:
; DEFAULT-NOT: set_hwloop
; HWON-LABEL: multibb_decline:
; Latch-only multi-BB is the measured SCEV/CFG extension: HWON arms one
; Role-A SET. Never two SETs.
; HWON: set_hwloop_f2 0,
; HWON-NOT: set_hwloop{{.*}}set_hwloop
entry:
  br label %header
header:
  %i = phi i32 [ 0, %entry ], [ %i.n, %latch ]
  %s = phi i32 [ 0, %entry ], [ %s.n, %latch ]
  %c.if = icmp eq i32 %i, 0
  br i1 %c.if, label %then, label %else
then:
  %vt = load i32, ptr %p
  br label %latch
else:
  %ve = load i32, ptr %q
  br label %latch
latch:
  %vx = phi i32 [ %vt, %then ], [ %ve, %else ]
  %s.n = add i32 %s, %vx
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %header, label %exit
exit:
  ret i32 %s.n
}
; Nest matrix: multi-BB outer remains soft; single-BB inner may form Role A.
define i32 @nested_inner_only(ptr noalias %a, i32 %n, i32 %m) {
; DEFAULT-LABEL: nested_inner_only:
; DEFAULT-NOT: set_hwloop
; DEFAULT: jalr
; HWON-LABEL: nested_inner_only:
; HWON: set_hwloop_f2 0,
; HWON-NOT: csrw
; HWON: jalr
entry:
  %cmp.n = icmp sgt i32 %n, 0
  br i1 %cmp.n, label %outer.preheader, label %exit
outer.preheader:
  %cmp.m = icmp sgt i32 %m, 0
  br label %outer.header
outer.header:
  %i = phi i32 [ 0, %outer.preheader ], [ %i.next, %outer.latch ]
  %s = phi i32 [ 0, %outer.preheader ], [ %s.inner, %outer.latch ]
  br i1 %cmp.m, label %inner.preheader, label %outer.latch
inner.preheader:
  br label %inner.body
inner.body:
  %j = phi i32 [ 0, %inner.preheader ], [ %j.next, %inner.body ]
  %si = phi i32 [ %s, %inner.preheader ], [ %si.acc, %inner.body ]
  %idx = add i32 %i, %j
  %p = getelementptr inbounds i32, ptr %a, i32 %idx
  %v = load i32, ptr %p, align 4
  %si.acc = add i32 %si, %v
  %j.next = add i32 %j, 1
  %c.j = icmp eq i32 %j.next, %m
  br i1 %c.j, label %outer.latch, label %inner.body
outer.latch:
  %s.inner = phi i32 [ %s, %outer.header ], [ %si.acc, %inner.body ]
  %i.next = add i32 %i, 1
  %c.i = icmp eq i32 %i.next, %n
  br i1 %c.i, label %exit, label %outer.header
exit:
  %r = phi i32 [ 0, %entry ], [ %s.inner, %outer.latch ]
  ret i32 %r
}

; Zero-trip constant never arms a selector (TTI purity seat).
define i32 @zero_trip_decline(ptr %p) {
; DEFAULT-LABEL: zero_trip_decline:
; DEFAULT-NOT: set_hwloop
; HWON-LABEL: zero_trip_decline:
; HWON-NOT: set_hwloop
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.n, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  %v = load i32, ptr %p
  %s.n = add i32 %s, %v
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, 0
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %s.n
}

; Constant trip=1 stays soft at formation (upstream declines BETC=0 shape);
; MinCount=1 is still legal for runtime COUNT via SET when armed.
define i32 @trip1_const_soft(ptr %p) {
; DEFAULT-LABEL: trip1_const_soft:
; DEFAULT-NOT: set_hwloop
; HWON-LABEL: trip1_const_soft:
; HWON-NOT: set_hwloop
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.n, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  %v = load i32, ptr %p
  %s.n = add i32 %s, %v
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, 1
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %s.n
}

; Constant trip=2 is the small fixed-trip Role-A floor that does arm SET.
define i32 @trip2_const_form(ptr %p) {
; DEFAULT-LABEL: trip2_const_form:
; DEFAULT-NOT: set_hwloop
; HWON-LABEL: trip2_const_form:
; HWON: set_hwloop_f2 0,
; HWON-NOT: csrw
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.n, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  %v = load i32, ptr %p
  %s.n = add i32 %s, %v
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, 2
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %s.n
}

; Innermost latch-only diamond with stores (resists if-conversion).
; Measured SCEV/CFG overlay of AIE's all-multi-BB decline. Never a free
; HWLR CSR.
define void @multibb_latch_stores(ptr %dst, ptr readonly %src, i32 %n) {
; DEFAULT-LABEL: multibb_latch_stores:
; DEFAULT-NOT: set_hwloop
; HWON-LABEL: multibb_latch_stores:
; HWON: set_hwloop_f2 0,
; HWON-NOT: csrw
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %header, label %exit
header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %dp = phi ptr [ %dst, %entry ], [ %dp.next, %latch ]
  %v = load i32, ptr %sp, align 4
  %neg = icmp slt i32 %v, 0
  br i1 %neg, label %then, label %else
then:
  store i32 0, ptr %dp, align 4
  br label %latch
else:
  store i32 %v, ptr %dp, align 4
  br label %latch
latch:
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %dp.next = getelementptr inbounds i32, ptr %dp, i32 1
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %header, label %exit
exit:
  ret void
}

define i32 @multiexit_decline(ptr readonly %src, i32 %n, i32 %k) {
; DEFAULT-LABEL: multiexit_decline:
; DEFAULT-NOT: set_hwloop
; HWON-LABEL: multiexit_decline:
; HWON-NOT: set_hwloop
entry:
  br label %header
header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %v = load i32, ptr %sp, align 4
  %hit = icmp eq i32 %v, %k
  br i1 %hit, label %early, label %latch
latch:
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %header, label %exit
early:
  ret i32 %v
exit:
  ret i32 0
}
