; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -global-isel-abort=1 -verify-machineinstrs -O2 \
; RUN:     -debug-only=pipeliner < %s -o %t.s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: FileCheck %s --check-prefix=ASM < %t.s
; REQUIRES: asserts

; Role: Option A / peer-law residual — ZOL multi-stage never expands pre-RA.
; Constant trip keeps MinTripCount high enough to pass the ZOL MinTC gate so
; StageCount>1 containment is the reject surface (closes inverted ZOL
; multi-stage gate). Product multi-stage is post-RA only. Geometry floors
; are metrics-only here. Soft residual path (flag off) is sms-pli-*/sms-multistage-*.

; SWP: SMS-HANDOFF: metrics-only freeze
; SWP-DAG: Schedule Found? 1
; SWP-DAG: SMS-SHOULDUSE: reject multi-stage stages={{[2-9]|[1-9][0-9]+}} II={{[0-9]+}} (pre-RA StageCount>1 containment; post-RA multi-stage only)
; SWP-DAG: ZOL: geometry floors MinBodyBundles=3 SetupIssueDistance=3 InterveningCycles=2
; SWP-NOT: SMS-SHOULDUSE: accept multi-stage durable
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
