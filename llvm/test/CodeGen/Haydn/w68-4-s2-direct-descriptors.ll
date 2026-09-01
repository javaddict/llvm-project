; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=haydn-latency-stalls -verify-machineinstrs < %s | FileCheck %s

; Role: MIR — W68.4 latency/bake seat split. PostMachineScheduler keeps
; remaining bare singles LOGICAL: LatencyStalls charges dest windows from
; the logical Desc's published itinerary (ST32_POST Slot1_LD [2]; the
; member row is [1]) — baking at the scheduler would erase the exposed-
; pipeline stall before the stall authority runs (stack-align regression
; class). The identity bake (closed singleton ProductDefaultRowID E2,
; AIEMachineScheduler.cpp:1126-1132 peer) runs at the END of
; haydn-latency-stalls, so at this stop point every bare single is a
; generated member and construction-only Finalize has no tryAdd chooser.

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

define i32 @one_add(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: name: one_add
; Closed singleton settles on the E2 member, not the public logical.
; CHECK: ADD32_E2_
; CHECK-NOT: {{= ADD32 }}
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @independent_adds(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
; CHECK-LABEL: name: independent_adds
; Packed or sequential, each ADD is a generated member before Finalize.
; CHECK-DAG: ADD32_E{{[23]}}_
; CHECK-NOT: {{= ADD32 }}
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  %r = xor i32 %x, %y
  ret i32 %r
}

; Catalog alias ST32_POST owns AlternateInsts on S_SW_POST_IMM. Closed
; singleton bake must productSolve that alias (same as multi-MI
; exactSolveProductOpcodes), not leave a freeze residual logical.
define void @stream_store(ptr %p, i32 %n, i32 %v) nounwind {
; CHECK-LABEL: name: stream_store
; CHECK: S_SW_POST_IMM_E
; CHECK-NOT: {{= ST32_POST }}
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %q = phi ptr [ %p, %entry ], [ %q.next, %loop ]
  store i32 %v, ptr %q, align 4
  %q.next = getelementptr inbounds i32, ptr %q, i32 1
  %i.next = add nsw i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %end
end:
  ret void
}
