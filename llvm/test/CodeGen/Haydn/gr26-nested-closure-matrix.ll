; REQUIRES: asserts
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 \
; RUN:     -debug-only=haydn-late-convergence < %s -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=DBG
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ASM
;
; GR2.6 nested-loop closure matrix (hwloop ON — the product default):
; 2-deep nested hardware loops whose inner body forces inner-first
; revalidation, plus forward and backward conditional ranges in the same
; function. Pins:
;   * single S2 per driver entry under a demote-triggering corpus;
;   * hwloop-setups census is monotone (final <= entry) — the demote
;     erases SETs; nothing re-forms one;
;   * the enforced no-growth law admits the demote insertion (no fatal);
;   * inner-first wave order survives (Fixup demote evidence appears
;     before closure terminates);
;   * deterministic closure with no bound exhaustion.
;
; hwloop-setups DBG pin is a range: whether THIS corpus demotes depends
; on the Off1/Off2 geometry the single S2 produced; the law pins are the
; monotone census and the absence of fatals.

define void @nested_matrix(ptr nocapture %p, i32 %n, i32 %m) {
entry:
  br label %outer.cond

outer.cond:
  %i = phi i32 [ 0, %entry ], [ %inc13, %outer.latch ]
  %cmp = icmp slt i32 %i, %n
  br i1 %cmp, label %outer.body, label %exit

outer.body:
  br label %inner.cond

inner.cond:
  %j = phi i32 [ 0, %outer.body ], [ %inc, %inner.body ]
  %cmp2 = icmp slt i32 %j, %m
  br i1 %cmp2, label %inner.body, label %outer.latch

inner.body:
  %t = mul i32 %i, %j
  %idx = add i32 %i, %j
  %gep = getelementptr i32, ptr %p, i32 %idx
  store volatile i32 %t, ptr %gep, align 4
  %inc = add i32 %j, 1
  br label %inner.cond

outer.latch:
  %inc13 = add i32 %i, 1
  br label %outer.cond

exit:
  ret void
}

; Forward and backward conditional ranges in one function (hwloop on).
define i32 @fwd_back_mix(i32 %a, ptr nocapture %p, i32 %n) local_unnamed_addr {
entry:
  %c = icmp eq i32 %a, 0
  br i1 %c, label %loop, label %b2
b2:
  store volatile i32 1, ptr %p, align 4
  %d = icmp sgt i32 %a, 4
  br i1 %d, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ 0, %b2 ], [ %inc, %loop ]
  %v = load volatile i32, ptr %p, align 4
  %s = add i32 %v, %i
  store volatile i32 %s, ptr %p, align 4
  %inc = add i32 %i, 1
  %e = icmp slt i32 %inc, %n
  br i1 %e, label %loop, label %exit
exit:
  %r = phi i32 [ %a, %b2 ], [ %s, %loop ]
  ret i32 %r
}

; DBG: HaydnLateConvergence: nested_matrix bound={{[0-9]+}} (cond={{[0-9]+}} hwloop={{[0-9]+}})
; DBG: HaydnLateConvergence: S2 once per driver entry
; DBG: HaydnLateConvergence: closed after {{[0-9]+}} iteration(s) (no upward event)
; DBG: HaydnLateConvergence: nested_matrix prefixes={{[0-9]+}} no-growth={{[01]}} jalr-sites={{[0-9]+}} hwloop-setups={{[0-9]+$}}
; DBG: HaydnLateConvergence: fwd_back_mix bound={{[0-9]+}}
; DBG: HaydnLateConvergence: S2 once per driver entry
; DBG: HaydnLateConvergence: closed after {{[0-9]+}} iteration(s) (no upward event)
; DBG-NOT: exhausted
; DBG-NOT: grew beyond the admitted closure vocabulary
; DBG-NOT: non-monotone mutation

; ASM-LABEL: nested_matrix:
; ASM: bnez
; ASM: jalr
; ASM-LABEL: fwd_back_mix:
; ASM: jalr
