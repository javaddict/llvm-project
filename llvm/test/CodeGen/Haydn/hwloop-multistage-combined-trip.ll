; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-multistage-sms=false \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.off.rmk | FileCheck %s --check-prefix=OFF
; RUN: FileCheck %s --allow-empty --check-prefix=OFFRMK < %t.off.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms=false \
; RUN:   < %s | FileCheck %s --check-prefix=HWON
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms=false \
; RUN:   -filetype=obj -o %t.hwon.o < %s
; RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.hwon.o | \
; RUN:   FileCheck %s --check-prefix=OBJ
; RUN: llvm-readobj -r %t.hwon.o | FileCheck %s --check-prefix=RELOC
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-analysis-only \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.an.rmk | FileCheck %s --check-prefix=DUAL
; RUN: FileCheck %s --check-prefix=ANALYSIS-RMK < %t.an.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-analysis-only -filetype=obj -o %t.dual-an.o < %s
; RUN: cmp %t.hwon.o %t.dual-an.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-TRIP -filetype=obj -o %t.pftrip.o < %s
; RUN: cmp %t.hwon.o %t.pftrip.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-analysis-only \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.sms.rmk | FileCheck %s --check-prefix=SMSONLY
; RUN: FileCheck %s --allow-empty --check-prefix=SMSONLY-RMK < %t.sms.rmk

; XFAIL: *
; Dual-ON combined trip seat stays expected-fail until T3 independent
; SMS QUALIFY (hardware loops off, parcels==II), then T6 independent
; SCEV-proven hwloop QUALIFY (multi-stage off), then this dual-ON
; retained-trip / COUNT / selector matrix. Product defaults stay
; off. Do not treat this file as a default flip.
; Analysis-only currently exhausts II instead of accepting.

; Role: semantic — combined trip QUALIFY seat. Product defaults OFF.
; SCEV-proven path only; never post-RA rediscovery. HWON arms
; set_hwloop_f2 sel=0 on legal runtime/const trips. Zero-trip / call
; stay declined. Dual-ON must keep the same trip/selector, accept
; peel-adjusted COUNT with parcels==II, and never invent a free
; HWLR CSR write. Off1/Off2 resolve in-object.

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

define i32 @runtime_trip_sum(ptr nocapture readonly %p, i32 %n) {
; OFF-LABEL: runtime_trip_sum:
; OFF-NOT:   set_hwloop
; OFF-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; OFF:       jalr
;
; SMSONLY-LABEL: runtime_trip_sum:
; SMSONLY-NOT:   set_hwloop
; SMSONLY-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; SMSONLY:       jalr
;
; HWON-LABEL: runtime_trip_sum:
; HWON:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON-NOT:   set_hwloop_f2 1,
; HWON-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
;
; DUAL-LABEL: runtime_trip_sum:
; DUAL:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DUAL-NOT:   set_hwloop_f2 1,
; DUAL-NOT:   csrw{{.*}} 0x2{{[0-5]}}
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
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   set_hwloop_f2 1,
; HWON-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
;
; DUAL-LABEL: const_trip_sum:
; DUAL:       set_hwloop_f2 0,
; DUAL-NOT:   set_hwloop_f2 1,
; DUAL-NOT:   csrw{{.*}} 0x2{{[0-5]}}
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
  br i1 %c, label %loop, label %exit, !llvm.loop !0
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

define i32 @zero_trip_decline(ptr %p) {
; OFF-LABEL: zero_trip_decline:
; OFF-NOT:   set_hwloop
; OFF:       jalr
;
; HWON-LABEL: zero_trip_decline:
; HWON-NOT:   set_hwloop
; HWON:       jalr
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

; OBJ:      set_hwloop_f2
; OBJ-NOT:  csrw
; RELOC-NOT: R_HAYDN_BranchSImm16
; RELOC-NOT: HWLoopOffset

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.unroll.disable"}
