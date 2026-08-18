; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=MEM < %t.rmk
;
; REGRESSION TEST: may-alias store→next-iter load is a two-copy memory LCD.
;
; Bug: the multi-stage DAG modeled only Anti/Output/Data physreg edges.
; An aliasing store in iteration i and load in iteration i+1 could be
; overlapped at an illegal II. Memory Order edges were intra-region only.
;
; Fix: clone the body twice and call buildSchedGraph so store→next-iter
; load is a forward Order edge (AIE NCopies=2). LCD facts fold into
; NodeInfo Earliest/Latest/LCDLatest placement windows. Product default
; remains OFF; hardware loops OFF.
;
; Test design: in-place load/add/store of one pointer plus a countdown IV.
; If the two-copy graph is dropped, RecMII/edges/mem go to 0 or the old
; "memory LCD unmodeled" reject returns.
;
; ASM-LABEL: mem_lcd_inplace:
; ASM: jalr
; MEM: lcd two-iteration RecMII={{[1-9][0-9]*}} edges={{[1-9][0-9]*}} mem={{[1-9][0-9]*}}
; MEM: lcd-as-windows
; MEM-NOT: memory LCD unmodeled

define void @mem_lcd_inplace(ptr nocapture %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  ; Keep a dedicated preheader (empty `pre` folds into entry, which then has
  ; two successors and fails PF-CFG / isCandidate). The store is outside the
  ; kernel; the loop-carried recurrence is still load/store of %p.
  %mark = getelementptr inbounds i32, ptr %p, i32 %n
  store i32 0, ptr %mark, align 4
  br label %body
body:
  %i = phi i32 [ %n, %pre ], [ %inext, %body ]
  %v = load i32, ptr %p, align 4
  %w = add i32 %v, 1
  store i32 %w, ptr %p, align 4
  %inext = add nsw i32 %i, -1
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body
exit:
  ret void
}
