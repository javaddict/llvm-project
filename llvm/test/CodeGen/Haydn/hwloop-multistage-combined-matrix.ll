; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops < %s | FileCheck %s --check-prefix=HWON
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-analysis-only \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.an.rmk | FileCheck %s --check-prefix=ANALYSIS-ASM
; RUN: FileCheck %s --check-prefix=ANALYSIS-RMK < %t.an.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms < %s \
; RUN:   | FileCheck %s --check-prefix=DUAL
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -filetype=obj -o %t.hwon.o < %s
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-analysis-only -filetype=obj -o %t.dual-an.o < %s
; RUN: cmp %t.hwon.o %t.dual-an.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-CFG -filetype=obj -o %t.pfcfg.o < %s
; RUN: cmp %t.hwon.o %t.pfcfg.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-ALLOC -filetype=obj -o %t.jmalloc.o < %s
; RUN: cmp %t.hwon.o %t.jmalloc.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail -filetype=obj -o %t.ffall.o < %s
; RUN: cmp %t.hwon.o %t.ffall.o

; Role: semantic — one-artifact hwloop x multi-stage dual-ON matrix.
; Product defaults stay OFF. Independent qualify is not closed here.
; Forced dual-ON still forms SET on legal single-BB trips. Analysis-only
; and force-fail restore the hardware-loop-only object. Multi-stage may
; fail-closed after accept (no product default flip).

target triple = "haydn-unknown-elf"

; ANALYSIS-RMK: accepted II={{[0-9]+}} stages={{[2-9]|[1-9][0-9]+}}

define i32 @runtime_trip_sum(ptr nocapture readonly %p, i32 %n) {
; OFF-LABEL: runtime_trip_sum:
; OFF-NOT:   set_hwloop
; OFF-NOT:   #<swps> stages={{[2-9]|[1-9][0-9]+}}
; OFF:       jalr
;
; HWON-LABEL: runtime_trip_sum:
; HWON:       set_hwloop_f2 1, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON-NOT:   csrw
; HWON:       jalr
;
; ANALYSIS-ASM-LABEL: runtime_trip_sum:
; ANALYSIS-ASM:       set_hwloop_f2 1, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; ANALYSIS-ASM:       .LLhwloop_start
; ANALYSIS-ASM:       .LLhwloop_end
; ANALYSIS-ASM:       jalr
;
; Trip/CFG: retained SET on a dedicated preheader; selector stays {1}.
; Prologue/kernel/epilogue: START/END bound the body; legal return.
; Transaction/final object seats are the cmp RUN lines above.
; DUAL-LABEL: runtime_trip_sum:
; DUAL-NOT:   csrw
; DUAL:       set_hwloop_f2 1, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DUAL:       .LLhwloop_start
; DUAL:       .LLhwloop_end
; DUAL:       jalr
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
  %t0 = add i32 %v, 1
  %t1 = mul i32 %t0, 3
  %t2 = add i32 %t1, %v
  %t3 = xor i32 %t2, %s
  %s.n = add i32 %s, %t3
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  ret i32 %r
}

define i32 @const_trip_sum(ptr nocapture readonly %p) {
; OFF-LABEL: const_trip_sum:
; OFF-NOT:   set_hwloop
; OFF:       jalr
;
; HWON-LABEL: const_trip_sum:
; HWON:       set_hwloop_f2 1,
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
;
; ANALYSIS-ASM-LABEL: const_trip_sum:
; ANALYSIS-ASM:       set_hwloop_f2 1,
; ANALYSIS-ASM:       .LLhwloop_start
; ANALYSIS-ASM:       jalr
;
; DUAL-LABEL: const_trip_sum:
; DUAL:       set_hwloop_f2 1,
; DUAL:       .LLhwloop_start
; DUAL:       .LLhwloop_end
; DUAL:       jalr
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.n, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %s.n = add i32 %s, %v
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, 16
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %s.n
}

declare void @side_effect(i32)
define void @call_reject(i32 %n) {
; OFF-LABEL: call_reject:
; OFF-NOT:   set_hwloop
; OFF:       jalr
;
; HWON-LABEL: call_reject:
; HWON-NOT:   set_hwloop
; HWON:       jalr
;
; ANALYSIS-ASM-LABEL: call_reject:
; ANALYSIS-ASM-NOT:   set_hwloop
; ANALYSIS-ASM:       jalr
;
; DUAL-LABEL: call_reject:
; DUAL-NOT:   set_hwloop
; DUAL:       jalr
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
; OFF-LABEL: multibb_decline:
; OFF-NOT:   set_hwloop
; OFF:       jalr
;
; HWON-LABEL: multibb_decline:
; HWON-NOT:   set_hwloop
; HWON:       jalr
;
; ANALYSIS-ASM-LABEL: multibb_decline:
; ANALYSIS-ASM-NOT:   set_hwloop
; ANALYSIS-ASM:       jalr
;
; DUAL-LABEL: multibb_decline:
; DUAL-NOT:   set_hwloop
; DUAL:       jalr
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

define i32 @zero_trip_decline(ptr %p) {
; OFF-LABEL: zero_trip_decline:
; OFF-NOT:   set_hwloop
; OFF:       jalr
;
; HWON-LABEL: zero_trip_decline:
; HWON-NOT:   set_hwloop
; HWON:       jalr
;
; ANALYSIS-ASM-LABEL: zero_trip_decline:
; ANALYSIS-ASM-NOT:   set_hwloop
; ANALYSIS-ASM:       jalr
;
; DUAL-LABEL: zero_trip_decline:
; DUAL-NOT:   set_hwloop
; DUAL:       jalr
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
