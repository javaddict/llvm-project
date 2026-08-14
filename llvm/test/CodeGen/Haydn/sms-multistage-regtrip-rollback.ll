; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -filetype=obj -o %t.hwon.o < %s
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-TRIP -filetype=obj -o %t.tr.o < %s
; RUN: cmp %t.hwon.o %t.tr.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-COMMIT -filetype=obj -o %t.cm.o < %s
; RUN: cmp %t.hwon.o %t.cm.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-LIVE -filetype=obj -o %t.lv.o < %s
; RUN: cmp %t.hwon.o %t.lv.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-ALT -filetype=obj -o %t.al.o < %s
; RUN: cmp %t.hwon.o %t.al.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-META -filetype=obj -o %t.mt.o < %s
; RUN: cmp %t.hwon.o %t.mt.o
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-TRIP \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.tr.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=TRIP < %t.tr.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-LIVE \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s >/dev/null 2>%t.lv.rmk
; RUN: FileCheck %s --check-prefix=LIVE < %t.lv.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-ALT \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s >/dev/null 2>%t.al.rmk
; RUN: FileCheck %s --check-prefix=ALT < %t.al.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -haydn-multistage-sms-force-fail-seat=JM-META \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s >/dev/null 2>%t.mt.rmk
; RUN: FileCheck %s --check-prefix=META < %t.mt.rmk
;
; REGRESSION TEST: F4 register-operand rollback + F5 JM byte-identity.
;
; Bug: rematerializeAddImmForUse setReg's a scavenged dest into the
; SET_HWLOOP_REG trip use. The multi-stage snapshot restored opcode/imm/MBB
; but not register operands, so JM-TRIP/JM-COMMIT rollback left a dangling
; scavenged register. JM-COMMIT also fired at splice (before exact commit),
; and JM-LIVE/ALT/META shared one seam / one reason string.
;
; Fix: snapshot clones are the full operand oracle (registers included);
; JM-COMMIT force-fail is after exact commit; JM-LIVE/ALT/META are distinct
; seats. Product default stays OFF.
;
; Test design: runtime trip (reg-trip SET). Object cmp vs hwloop-only
; baseline. If remat escapes restore, cmp fails. If JM-COMMIT is still
; merged with JM-SPLICE, the COMMIT object may match for the wrong reason
; — TRIP still exercises remat.
; Dual-ON analysis may accept then fail PF-LIVE before journal seats;
; that preflight reject preserves the ordinary baseline (object cmp above).

target triple = "haydn-unknown-elf"

; TRIP: {{preflight reject: PF-LIVE|rollback to ordinary baseline \(JM-TRIP-force\)}}
; LIVE: {{preflight reject: PF-LIVE|rollback to ordinary baseline \(JM-LIVE-force\)}}
; LIVE-NOT: JM-ALT-force
; LIVE-NOT: JM-META-force
; ALT: {{preflight reject: PF-LIVE|rollback to ordinary baseline \(JM-ALT-force\)}}
; ALT-NOT: JM-LIVE-force
; ALT-NOT: JM-META-force
; META: {{preflight reject: PF-LIVE|rollback to ordinary baseline \(JM-META-force\)}}
; META-NOT: JM-LIVE-force
; META-NOT: JM-ALT-force

define i32 @runtime_trip_sum(ptr nocapture readonly %p, i32 %n) {
; ASM-LABEL: runtime_trip_sum:
; ASM:       set_hwloop_f2 1,
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
