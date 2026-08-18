; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-multistage-sms=false \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.off.rmk | FileCheck %s --check-prefix=OFF
; RUN: FileCheck %s --allow-empty --check-prefix=OFFRMK < %t.off.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms=false \
; RUN:   < %s | FileCheck %s --check-prefix=HWON
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-analysis-only \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.an.rmk | FileCheck %s --check-prefix=DUAL
; RUN: FileCheck %s --check-prefix=ANALYSIS-RMK < %t.an.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.commit.rmk | FileCheck %s --check-prefix=COMMIT
; RUN: FileCheck %s --check-prefix=COMMIT-RMK < %t.commit.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -filetype=obj -o %t.hwon.o < %s
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-CFG -filetype=obj -o %t.pfcfg.o < %s
; RUN: cmp %t.hwon.o %t.pfcfg.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-analysis-only \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.sms.rmk | FileCheck %s --check-prefix=SMSONLY
; RUN: FileCheck %s --allow-empty --check-prefix=SMSONLY-RMK < %t.sms.rmk

; XFAIL: *
; Dual-ON combined CFG seat stays expected-fail until T3 independent
; SMS QUALIFY (hardware loops off, parcels==II), then T6 independent
; SCEV-proven hwloop QUALIFY (multi-stage off), then this dual-ON
; preheader/BEGIN/END / latch-only overlay matrix. Product defaults
; stay off. Do not treat this file as a default flip.
; Analysis-only currently exhausts II instead of accepting.

; Role: semantic — combined CFG QUALIFY seat. Product defaults OFF.
; SCEV-proven path only; never post-RA rediscovery. Single-BB and
; innermost latch-only diamonds may arm one SET (sel=0). Multi-exit,
; early-exit, and nested-outer stay declined. Dual-ON must keep the
; same CFG/selector, compose prologue/kernel/epilogue with BEGIN/END,
; and never invent a free HWLR CSR write.

target triple = "haydn-unknown-elf"

; OFFRMK-NOT: accepted II=
; OFFRMK-NOT: MultiStageStageMBB
; SMSONLY-RMK-NOT: hwloop-combined=on
;
; ANALYSIS-RMK: accepted II=[[II:[0-9]+]] stages={{[2-9]|[1-9][0-9]+}}
; ANALYSIS-RMK-SAME: measured-II=[[II]]
; ANALYSIS-RMK: qualify parcels-per-iter=[[II]]
; ANALYSIS-RMK-SAME: searched-II=[[II]]
; ANALYSIS-RMK: hwloop-combined=on
; ANALYSIS-RMK-NOT: sequential (preflight)
;
; COMMIT-RMK: accepted II=[[CII:[0-9]+]] stages={{[2-9]|[1-9][0-9]+}}
; COMMIT-RMK-SAME: measured-II=[[CII]]
; COMMIT-RMK: stage-mbb prolog=
; COMMIT-RMK: peel-order=modulo-cycle
; COMMIT-RMK: hwloop-combined=on

define i32 @singlebb_preheader_geometry(ptr nocapture readonly %p, i32 %n) {
; OFF-LABEL: singlebb_preheader_geometry:
; OFF-NOT:   set_hwloop
; OFF-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; OFF:       jalr
;
; SMSONLY-LABEL: singlebb_preheader_geometry:
; SMSONLY-NOT:   set_hwloop
; SMSONLY-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; SMSONLY:       jalr
;
; HWON-LABEL: singlebb_preheader_geometry:
; HWON:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON-NOT:   set_hwloop_f2 1,
; HWON-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
;
; DUAL-LABEL: singlebb_preheader_geometry:
; DUAL:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DUAL-NOT:   set_hwloop_f2 1,
; DUAL-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DUAL:       .LLhwloop_start
; DUAL:       .LLhwloop_end
; DUAL:       jalr
;
; COMMIT-LABEL: singlebb_preheader_geometry:
; COMMIT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; COMMIT-NOT:   set_hwloop_f2 1,
; COMMIT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; COMMIT:       .LLhwloop_start
; COMMIT:       #<swps> stages={{[2-9]|[1-9][0-9]+}}
; COMMIT:       .LLhwloop_end
; COMMIT:       jalr
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [ 0, %pre ], [ %i.n, %loop ]
  %s = phi i32 [ 0, %pre ], [ %s.n, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %s.n = add i32 %s, %v
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  ret i32 %r
}

; Latch-only diamond: measured multi-BB Role A (AIE declines every
; multi-BB at AIEBaseTargetTransformInfo.cpp:317-320).
define void @latch_only_diamond(ptr %dst, ptr readonly %src, i32 %n) {
; OFF-LABEL: latch_only_diamond:
; OFF-NOT:   set_hwloop
; OFF-NOT:   csrw{{.*}} 0x2{{[0-5]}}
;
; HWON-LABEL: latch_only_diamond:
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   set_hwloop_f2 1,
; HWON-NOT:   set_hwloop{{.*}}set_hwloop
; HWON-NOT:   csrw{{.*}} 0x2{{[0-5]}}
;
; DUAL-LABEL: latch_only_diamond:
; DUAL:       set_hwloop_f2 0,
; DUAL-NOT:   set_hwloop_f2 1,
; DUAL-NOT:   set_hwloop{{.*}}set_hwloop
; DUAL-NOT:   csrw{{.*}} 0x2{{[0-5]}}
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %dp = phi ptr [ %dst, %entry ], [ %dp.next, %latch ]
  %v = load i32, ptr %sp, align 4
  %sign = icmp slt i32 %v, 0
  br i1 %sign, label %then, label %else
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
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret void
}

define i32 @multiexit_decline(ptr readonly %src, i32 %n, i32 %k) {
; OFF-LABEL: multiexit_decline:
; OFF-NOT:   set_hwloop
; OFF:       jalr
;
; HWON-LABEL: multiexit_decline:
; HWON-NOT:   set_hwloop
; HWON:       jalr
;
; DUAL-LABEL: multiexit_decline:
; DUAL-NOT:   set_hwloop
; DUAL:       jalr
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

; Nested outer stays soft; innermost single-BB may arm sel=0 only.
define i32 @nested_inner_only(ptr noalias %a, i32 %n, i32 %m) {
; OFF-LABEL: nested_inner_only:
; OFF-NOT:   set_hwloop
; OFF-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; OFF:       jalr
;
; HWON-LABEL: nested_inner_only:
; HWON:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON-NOT:   set_hwloop_f2 1,
; HWON-NOT:   set_hwloop_f2 2,
; HWON-NOT:   set_hwloop_f2 3,
; HWON-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
;
; DUAL-LABEL: nested_inner_only:
; DUAL:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DUAL-NOT:   set_hwloop_f2 1,
; DUAL-NOT:   set_hwloop_f2 2,
; DUAL-NOT:   set_hwloop_f2 3,
; DUAL-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DUAL:       .LLhwloop_start
; DUAL:       .LLhwloop_end
; DUAL:       jalr
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
