; RUN: llc -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.an.rmk | FileCheck %s --check-prefix=ANALYSIS-ASM
; RUN: FileCheck %s --check-prefix=ANALYSIS-RMK < %t.an.rmk
; RUN: llc -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=PF-CFG < %s \
; RUN:   | FileCheck %s --check-prefix=FORCE
; RUN: llc -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=JM-ALLOC < %s \
; RUN:   | FileCheck %s --check-prefix=FORCE
;
; G-MULTISTAGE-QUALIFY slice (hardware loops OFF): soft-count latency chain may
; accept, reject, or exhaust the II window; product default remains OFF;
; PF/JM force-fail seats must retain a legal ordinary epilogue (no crash /
; no half-mutation). Exhaustion is a reject class (no feasible II).
;
; OFF-LABEL: soft_store_chain:
; OFF: jalr
; ANALYSIS-ASM-LABEL: soft_store_chain:
; ANALYSIS-ASM: jalr
; ANALYSIS-RMK: {{accepted II=|rejected:|exhausted:}}
; FORCE-LABEL: soft_store_chain:
; FORCE: jalr

define void @soft_store_chain(ptr nocapture writeonly %dst,
                              ptr nocapture readonly %src, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %i = phi i32 [ 0, %pre ], [ %inext, %body ]
  %p = getelementptr inbounds i32, ptr %src, i32 %i
  %x = load i32, ptr %p, align 4
  %y = add i32 %x, 1
  %z = mul i32 %y, 3
  %w = add i32 %z, %x
  %q = getelementptr inbounds i32, ptr %dst, i32 %i
  store i32 %w, ptr %q, align 4
  %inext = add nuw nsw i32 %i, 1
  %cond = icmp eq i32 %inext, %n
  br i1 %cond, label %exit, label %body
exit:
  ret void
}
