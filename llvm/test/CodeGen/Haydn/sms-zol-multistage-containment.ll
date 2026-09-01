; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -global-isel-abort=1 -verify-machineinstrs -O2 \
; RUN:     -debug-only=pipeliner < %s -o %t.s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: FileCheck %s --check-prefix=ASM < %t.s
; REQUIRES: asserts

; Role: W68.1 LIFT pin — ZOL multi-stage expands on the generic pre-RA
; MachinePipeliner at product defaults. Constant trip 16 gives the static
; MinTripCount guard (16 > PrologueCount), the classic expander peels
; prologue/kernel/epilog, and adjustTripCount edits LoopStart $adj. The
; asm still carries no #<swps> annotation pre-RA and no durable freeze
; crosses RA (D493); the F41 knob at 1 restores the historic refuse
; (pinned by the CONTAINED arm of sms-f41-containment-product-pin.ll).

; SWP: SMS-HANDOFF: coverage ok
; SWP-DAG: Schedule Found? 1
; SWP-DAG: SMS-SHOULDUSE: accept stages={{[2-9]}} II={{[0-9]+}} (metrics-only; bare logical MIs; proven counted residual; no pre-RA cycle groups; product containment (PPS-3 bound))
; SWP-NOT: SMS-SHOULDUSE: reject multi-stage
; SWP-NOT: SMS-HANDOFF: materialize done groups={{[1-9][0-9]*}}

; ASM-LABEL: zol_mac_body:
; ASM: set_hwloop_f2
; ASM-NOT: #<swps> stages={{[2-9]|[1-9][0-9]+}}
; ASM: jalr

define i32 @zol_mac_body(ptr nocapture readonly %a, ptr nocapture readonly %b) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %mul = mul i32 %va, %vb
  %acc.next = add i32 %acc, %mul
  %i.next = add nuw i32 %i, 1
  %cond = icmp ult i32 %i.next, 16
  br i1 %cond, label %loop, label %exit
exit:
  ret i32 %acc.next
}
