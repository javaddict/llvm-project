; RUN: llc -haydn-enable-ldst-opt=true -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -O2 -enable-pipeliner=false < %s \
; RUN:     | FileCheck %s --check-prefix=OFF
; RUN: llc -haydn-enable-ldst-opt=true -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -O2 -enable-pipeliner=false -haydn-enable-post-pipeliner < %s \
; RUN:     | FileCheck %s --check-prefix=ON
; RUN: llc -haydn-enable-ldst-opt=true -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -O2 -enable-pipeliner=false -haydn-enable-post-pipeliner \
; RUN:     -debug-only=haydn-post-pipeliner < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=DBG
;
; HaydnPostPipeliner Stage-0 (default OFF;). ON needs explicit flag.
;
; Fixture: single-BB ZOL chain loop (load + three dependent adds of a
; runtime %k + sum). Pre-RA SMS is disabled so post-RA PostPipeliner owns
; the multi-stage opportunity.
;
; OFF (default / no -haydn-enable-post-pipeliner): sequential body (II≈5)
; empty preheader after SET_HWLOOP (only setup nops), no epilogue peel in exit.
;
; ON (-haydn-enable-post-pipeliner): multi-stage materialize
; * prologue clones in preheader (load + early chain adds)
; * denser kernel (II=3 vs 5)
; * epilogue clones in exit
; Debug path must log Success with NStages >= 2.

; OFF-LABEL: chain_loop:
; OFF: set_hwloop
; OFF: // =>This Inner Loop Header: Depth=1
; Preheader after SET has only setup nops — no peeled load/add.
; OFF-NOT: s_lw_post_imm
; OFF: s_lw_post_imm
; OFF: add32
; OFF: add32
; OFF: add32
; OFF: add32
; Exit has no epilogue chain peels (just epilogue glue / return).
; OFF-LABEL: // %exit
; OFF-NOT: add32
; OFF: jalr

; ON-LABEL: chain_loop:
; ON: set_hwloop
; Prologue peel into preheader (before the hwloop body label).
; ON: s_lw_post_imm
; ON: add32
; ON: // =>This Inner Loop Header: Depth=1
; Kernel still carries the steady-state mix.
; ON: s_lw_post_imm
; ON: add32
; Epilogue peel into exit (extra adds after the loop end label).
; ON-LABEL: // %exit
; ON: add32
; ON: jalr

; DBG: HaydnPostPipeliner: Success II={{[0-9]+}} NStages={{[2-9]}}

define i32 @chain_loop(i32* nocapture readonly %a, i32 %n, i32 %k) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
exit.loopexit:
  %s.lcssa = phi i32 [ %s4, %loop ]
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.lcssa, %exit.loopexit ]
  ret i32 %r
loop:
  %i = phi i32 [ 0, %pre ], [ %inext, %loop ]
  %s = phi i32 [ 0, %pre ], [ %s4, %loop ]
  %p = getelementptr inbounds i32, i32* %a, i32 %i
  %v = load i32, i32* %p, align 4
  %s1 = add i32 %v, %k
  %s2 = add i32 %s1, %k
  %s3 = add i32 %s2, %k
  %s4 = add i32 %s, %s3
  %inext = add nuw nsw i32 %i, 1
  %cond = icmp eq i32 %inext, %n
  br i1 %cond, label %exit.loopexit, label %loop
}
