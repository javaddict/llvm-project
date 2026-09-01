; 2026-08-22 SMS product-default flip rebaseline: SMS default is ON, so the
; OFF arm pins the explicit no-SMS contract (-haydn-enable-multistage-sms=0)
; and every rollback-identity baseline (hwon) is built with SMS explicitly
; OFF — force-fail restores the hardware-loop-only object, not the default
; dual object. DUAL arms stay explicitly flagged (now redundant with the
; default but kept as the forced-evidence shape).
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops=false -haydn-enable-multistage-sms=0 \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.off.rmk | FileCheck %s --check-prefix=OFF
; RUN: FileCheck %s --allow-empty --check-prefix=OFFRMK < %t.off.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms=0 \
; RUN:   < %s | FileCheck %s --check-prefix=HWON
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-analysis-only \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.an.rmk | FileCheck %s --check-prefix=ANALYSIS-ASM
; RUN: FileCheck %s --check-prefix=ANALYSIS-RMK < %t.an.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.dual.rmk | FileCheck %s --check-prefix=DUAL
; RUN: FileCheck %s --check-prefix=DUAL-RMK < %t.dual.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms=0 \
; RUN:   -filetype=obj -o %t.hwon.o < %s
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
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-analysis-only \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.sms.rmk | FileCheck %s --check-prefix=SMSONLY
; RUN: FileCheck %s --allow-empty --check-prefix=SMSONLY-RMK < %t.sms.rmk

; 2026-08-22 G004 dual-ON qualification LANDED: XFAIL removed. The dual
; arms accept and materialize (II parity: qualify parcels-per-iter ==
; searched-II == AchievedII; stage-mbb prolog/epilog peels; inclusive
; END). Independent T3/T6 qualification evidence plus this matrix now
; hold; product DEFAULTS for -haydn-enable-multistage-sms stay OFF (this
; file forces the flags explicitly — it is not a default flip).

; Role: semantic — one-artifact dual-ON qualify matrix.
; Product defaults stay OFF. Independent qualify is not closed here.
; Forced dual-ON still forms SET on legal single-BB trips and on the
; measured latch-only multi-BB overlay. Call / zero-trip / multi-exit
; stay declined. Analysis-only and force-fail restore the hardware-loop
; object when the engine does not commit. Closed seats require accept
; with parcels-per-iter == searched II, stages>=2 peels, and no free
; HWLR CSR write.

target triple = "haydn-unknown-elf"

; Product default: +hwloop attr is not a policy flip.
; OFFRMK-NOT: accepted II=
; OFFRMK-NOT: MultiStageStageMBB
; OFFRMK-NOT: qualify-or-cut
;
; SMS force-ON alone never forms SET and never claims combined-on.
; SMSONLY-RMK-NOT: hwloop-combined=on
;
; Closed dual-ON analysis: searched II is realized, stages>=2, combined on.
; ANALYSIS-RMK: accepted II=[[II:[0-9]+]] stages={{[2-9]|[1-9][0-9]+}}
; ANALYSIS-RMK-SAME: measured-II=[[II]]
; ANALYSIS-RMK: qualify parcels-per-iter=[[II]]
; ANALYSIS-RMK-SAME: searched-II=[[II]]
; ANALYSIS-RMK: hwloop-combined=on
; ANALYSIS-RMK-NOT: sequential (preflight)
;
; Closed dual-ON commit: peel MBBs + swps stages>=2 under the same SET.
; DUAL-RMK: accepted II=[[DII:[0-9]+]] stages={{[2-9]|[1-9][0-9]+}}
; DUAL-RMK-SAME: measured-II=[[DII]]
; DUAL-RMK: qualify parcels-per-iter=[[DII]]
; DUAL-RMK-SAME: searched-II=[[DII]]
; DUAL-RMK: hwloop-combined=on
; DUAL-RMK: stage-mbb prolog=
; DUAL-RMK: peel-order=modulo-cycle

define i32 @runtime_trip_sum(ptr nocapture readonly %p, i32 %n) {
; OFF-LABEL: runtime_trip_sum:
; OFF-NOT:   set_hwloop
; OFF-NOT:   #<swps> stages={{[2-9]|[1-9][0-9]+}}
; OFF:       jalr
;
; SMSONLY-LABEL: runtime_trip_sum:
; SMSONLY-NOT:   set_hwloop
; SMSONLY-NOT:   #<swps> stages={{[2-9]|[1-9][0-9]+}}
; SMSONLY:       jalr
;
; HWON-LABEL: runtime_trip_sum:
; HWON:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON-NOT:   csrw
; HWON-NOT:   #<swps> stages={{[2-9]|[1-9][0-9]+}}
; HWON:       jalr
;
; ANALYSIS-ASM-LABEL: runtime_trip_sum:
; ANALYSIS-ASM:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; ANALYSIS-ASM:       .LLhwloop_start
; ANALYSIS-ASM:       .LLhwloop_end
; ANALYSIS-ASM-NOT:   csrw
; ANALYSIS-ASM:       jalr
;
; Trip: retained SCEV trip/setup; selector stays 0; no HWLR CSR write.
; CFG: dedicated preheader owns setup; START/END bound the body.
; Prologue: setup at/before BEGIN; peels do not break the setup floor.
; Kernel: active-loop body is the multi-stage kernel; END is last parcel.
; Epilogue: drain live-outs; late fixup/demote stays fail-closed.
; Transaction/final-oracle: cmp RUN lines (analysis-only + PF/JM).
; DUAL-LABEL: runtime_trip_sum:
; DUAL-NOT:   csrw
; DUAL:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; 2026-08-22 G004 rebind: the SWPS annotation block prints before
; HWLR_BEGIN (AsmPrinter emits comments at the kernel MBB head), and
; HWLR_END is the INCLUSIVE address of the last body parcel — the label
; sits immediately before that final parcel, which still executes every
; iteration inside [BEGIN, END].
; DUAL:       #<swps> stages={{[2-9]|[1-9][0-9]+}}
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
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  %r = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  ret i32 %r
}

; 2026-08-22 G004: runtime-trip loops need the AIE-shaped min-trip floor
; (llvm.loop.itercount.range) for multi-stage acceptance — peel depth
; NStages-1 executes real iterations, so an unbounded runtime trip fails
; closed exactly like AIE PostPipeliner candidates without min-trip MD
; (AIEPostPipeliner.cpp:154-163 reads the MD floor; no MD -> reject).
!0 = !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}

define i32 @const_trip_sum(ptr nocapture readonly %p) {
; OFF-LABEL: const_trip_sum:
; OFF-NOT:   set_hwloop
; OFF:       jalr
;
; HWON-LABEL: const_trip_sum:
; HWON:       set_hwloop_f2 0,
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
;
; ANALYSIS-ASM-LABEL: const_trip_sum:
; ANALYSIS-ASM:       set_hwloop_f2 0,
; ANALYSIS-ASM:       .LLhwloop_start
; ANALYSIS-ASM:       jalr
;
; DUAL-LABEL: const_trip_sum:
; DUAL:       set_hwloop_f2 0,
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
; Latch-only overlay arms one Role-A SET. Never two SETs, never CSR.
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   set_hwloop{{.*}}set_hwloop
; HWON-NOT:   csrw
; HWON:       jalr
;
; ANALYSIS-ASM-LABEL: multibb_decline:
; ANALYSIS-ASM-NOT:   set_hwloop{{.*}}set_hwloop
; ANALYSIS-ASM-NOT:   csrw
; ANALYSIS-ASM:       jalr
;
; DUAL-LABEL: multibb_decline:
; DUAL-NOT:   set_hwloop{{.*}}set_hwloop
; DUAL-NOT:   csrw
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

define i32 @multiexit_decline(ptr readonly %src, i32 %n, i32 %k) {
; OFF-LABEL: multiexit_decline:
; OFF-NOT:   set_hwloop
; OFF:       jalr
;
; HWON-LABEL: multiexit_decline:
; HWON-NOT:   set_hwloop
; HWON:       jalr
;
; ANALYSIS-ASM-LABEL: multiexit_decline:
; ANALYSIS-ASM-NOT:   set_hwloop
; ANALYSIS-ASM:       jalr
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
