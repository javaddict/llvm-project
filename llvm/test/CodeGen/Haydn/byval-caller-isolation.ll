; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Role: semantic — byval caller isolation. Caller must spill a private stack
; copy of the aggregate payload, then pass that private pointer — not the
; original object pointer unchanged.

%struct.big = type { i32, i32, i32, i32, i32 }

declare i32 @sink_byval(ptr byval(%struct.big) %p)

define i32 @caller_byval_isolation(ptr %src) nounwind {
; CHECK-LABEL: caller_byval_isolation:
; Payload ld/st into the private frame (distinct from the LR spill st32 lr).
; CHECK-DAG: ld32 {{r[0-9]+}},
; CHECK-DAG: st32 {{r[0-9]+}},
; CHECK: jal
entry:
  %r = call i32 @sink_byval(ptr byval(%struct.big) %src)
  ret i32 %r
}
