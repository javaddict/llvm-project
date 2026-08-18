; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-multistage-sms=false < %s | \
; RUN:   FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms=false \
; RUN:   < %s | FileCheck %s --check-prefix=HWON
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms=false \
; RUN:   -filetype=obj -o %t.o < %s
; RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | \
; RUN:   FileCheck %s --check-prefix=OBJ
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC
;
; Role: semantic — independent SCEV-proven QUALIFY while product default
; stays OFF and multi-stage SMS is explicitly OFF. CSR / selector / reloc /
; fault seats on one artifact.
; Off path: no set_hwloop and no csrw to unpublished HWLR window.
; On path: SET_HWLOOP_F2 sel=0 only; START before END; no free CSR program.
; Object: encoded set_hwloop_f2 with resolved Off1/Off2 immediates; no
; csrw 0x20-0x25. Local Off1/Off2 resolve in-object (PC+(uimm<<2)).
; Reloc: never the retired HWLoopOffset alias. Ordinary branch relocs
; may still use R_HAYDN_BranchSImm16 — do not ban that kind.
; Fault: call / zero-trip / early-exit stay declined.
; Small legal constant trip may arm Role A; trip=0 never does.
; Multi-BB: innermost single-latch/single-exit diamond may arm Role A
; (measured SCEV/CFG overlay of AIE's all-multi-BB decline).

target triple = "haydn-unknown-elf"

define i32 @qualify_runtime_sum(ptr readonly %p, i32 %n) nounwind {
; DEFAULT-LABEL: qualify_runtime_sum:
; DEFAULT-NOT:   set_hwloop
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DEFAULT:       jalr
;
; HWON-LABEL: qualify_runtime_sum:
; Setup floor: SET then intervening size-bearing parcels before BEGIN.
; HWON:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON-NEXT:  {{.*}}nop
; HWON-NOT:   set_hwloop_f2 1,
; HWON-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; Inclusive END strictly after START labels in emission order.
; HWON:       jalr
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %s.next = add i32 %s, %v
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  ret i32 %r
}

; Innermost latch-only diamond: measured multi-BB Role A under HWON.
define void @qualify_multibb_latch(ptr %dst, ptr readonly %src, i32 %n) nounwind {
; DEFAULT-LABEL: qualify_multibb_latch:
; DEFAULT-NOT:   set_hwloop
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
;
; HWON-LABEL: qualify_multibb_latch:
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   set_hwloop_f2 1,
; HWON-NOT:   csrw{{.*}} 0x2{{[0-5]}}
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

declare void @side_effect(i32)
define void @qualify_call_fault(i32 %n) nounwind {
; DEFAULT-LABEL: qualify_call_fault:
; DEFAULT-NOT: set_hwloop
; HWON-LABEL: qualify_call_fault:
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

define void @qualify_zero_trip_fault(ptr %p) nounwind {
; DEFAULT-LABEL: qualify_zero_trip_fault:
; DEFAULT-NOT: set_hwloop
; HWON-LABEL: qualify_zero_trip_fault:
; HWON-NOT: set_hwloop
entry:
  br i1 false, label %loop, label %exit
loop:
  store i32 0, ptr %p
  br i1 false, label %loop, label %exit
exit:
  ret void
}

; Small legal constant trip (COUNT >= MinCount) may arm Role A.
; Trip=1 is typically unrolled or left soft; trip=4 stays a real ZOL body.
define void @qualify_trip_const4(ptr %p) nounwind {
; DEFAULT-LABEL: qualify_trip_const4:
; DEFAULT-NOT:   set_hwloop
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; HWON-LABEL: qualify_trip_const4:
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   set_hwloop_f2 1,
; HWON-NOT:   csrw{{.*}} 0x2{{[0-5]}}
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  store i32 0, ptr %p, align 4
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, 4
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  ret void
}

; Nesting: multi-BB outer stays soft; innermost single-BB may arm sel=0
; only. Never a second SET, never sel=1/2/3, never free HWLR CSR.
; AIE AIEBaseTargetTransformInfo.cpp:317-320 declines every multi-BB;
; this is the measured inner-only overlay (never Role B rediscovery).
define i32 @qualify_nested_inner_only(ptr noalias %a, i32 %n, i32 %m) nounwind {
; DEFAULT-LABEL: qualify_nested_inner_only:
; DEFAULT-NOT:   set_hwloop
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DEFAULT:       jalr
;
; HWON-LABEL: qualify_nested_inner_only:
; HWON:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON-NOT:   set_hwloop_f2 1,
; HWON-NOT:   set_hwloop_f2 2,
; HWON-NOT:   set_hwloop_f2 3,
; HWON-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
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

; Early-exit != latch: declined (never late physical rediscovery).
define i32 @qualify_early_exit_fault(ptr readonly %src, i32 %n, i32 %k) nounwind {
; DEFAULT-LABEL: qualify_early_exit_fault:
; DEFAULT-NOT: set_hwloop
; HWON-LABEL: qualify_early_exit_fault:
; HWON-NOT: set_hwloop
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %v = load i32, ptr %sp, align 4
  %hit = icmp eq i32 %v, %k
  br i1 %hit, label %early, label %latch
latch:
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
early:
  ret i32 %v
exit:
  ret i32 0
}

; Matching-artifact object: encoded set_hwloop_f2 (resolved Off1/Off2),
; no unpublished HWLR CSR write, never the retired HWLoopOffset alias.
; Local Off1/Off2 resolve in AsmBackend (PC+(uimm<<2)); ordinary
; R_HAYDN_BranchSImm16 on soft edges is allowed.

; OBJ:      set_hwloop_f2
; OBJ-NOT:  csrw
; RELOC-NOT: HWLoopOffset

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.unroll.disable"}
