; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -filetype=obj -o %t.hwon.o < %s
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-CFG -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-PHI -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-TRIP -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-STAGE -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-LIVE -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-ALT -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-BUNDLE -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-LATE -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-ALLOC -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-SPLICE -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-COMMIT -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-TRIP -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-LIVE -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-ALT -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-META -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail -filetype=obj -o %t.o < %s
; RUN: cmp %t.hwon.o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=PF-CFG \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=REJECT < %t.rmk

; XFAIL: *
; Dual-ON rollback seats stay expected-fail until T3 independent
; SMS QUALIFY then T6 independent hardware-loop QUALIFY.
; Force-fail PF-CFG currently exhausts II (qualify-or-cut seated
; product-off) instead of emitting "preflight reject: PF-CFG-force".
; Post-qualification policy fold only; product defaults stay off.
; Do not treat this file as a default flip.

; Role: semantic — dual-ON PF-*/JM-* force-fail seats restore the
; hardware-loop-only object byte-for-byte. Product defaults stay OFF.
; Closed transaction QUALIFY names the forced seat and does not
; search-exhaust in place of the reject.

target triple = "haydn-unknown-elf"

; REJECT: preflight reject: PF-CFG-force
; REJECT-NOT: exhausted:

define i32 @runtime_trip_sum(ptr nocapture readonly %p, i32 %n) {
; ASM-LABEL: runtime_trip_sum:
; ASM:       set_hwloop_f2 0,
; ASM:       .LLhwloop_start
; ASM:       .LLhwloop_end
; ASM:       jalr
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
