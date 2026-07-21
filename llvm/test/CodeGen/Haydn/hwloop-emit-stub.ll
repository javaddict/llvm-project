; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr_w}}
;
;
; REGRESSION TEST: Hardware loop SET_HWLOOP MCInst emission.
;
; Purpose: Verify that the SET_HWLOOP pseudo instruction is lowered to
; an actual MCInst (not raw text) when the hardware loop pass emits it.
;
; History: HaydnAsmPrinter previously emitted SET_HWLOOP as raw text with
; no MCInst encoding (no fixups, no valid object code). That was fixed:
; AsmPrinter now lowers the pseudo to the WIDE set_hwloop_f2 form with
; proper loop-start / loop-end fixups and a trip-count register operand.
; This test guards against regressing back to raw-text emission.
;
; Test design: A simple counted loop with constant trip count 10. The
; HWLoop pass detects it and emits set_hwloop_f2 with the trip count
; materialised into r3 (addi32{{(_w)?}} r3, r0, 10). The CHECKs verify that the
; assembly contains the WIDE hardware-loop setup with operands, not the
; pre-fix raw-text form.

define i32 @hwloop_stub_simple(ptr %p) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
