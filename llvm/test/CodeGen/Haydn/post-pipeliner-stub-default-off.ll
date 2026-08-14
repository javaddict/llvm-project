; UNSUPPORTED: true
; Role: retired — Stage-0 PostPipeliner deleted; not product coverage.
; Do not count as product green. RUN is deliberately false so a dropped
; RUN: false

; Stage-0 PostPipeliner default OFF. ON needs explicit flag.
; Both paths must remain verifier-clean.
;
; See post-pipeliner-stage0.ll for the multi-stage mutation lit test
; (SMS disabled, longer chain).

; OFF-LABEL: add_loop:
; OFF: jalr
; ON-LABEL: add_loop:
; ON: jalr

define i32 @add_loop(i32* nocapture readonly %a, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %for.body.preheader, label %exit
for.body.preheader:
  br label %for.body
exit.loopexit:
  %sum.lcssa = phi i32 [ %add, %for.body ]
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %sum.lcssa, %exit.loopexit ]
  ret i32 %r
for.body:
  %i = phi i32 [ %i.next, %for.body ], [ 0, %for.body.preheader ]
  %sum = phi i32 [ %add, %for.body ], [ 0, %for.body.preheader ]
  %p = getelementptr inbounds i32, i32* %a, i32 %i
  %v = load i32, i32* %p, align 4
  %add = add i32 %sum, %v
  %i.next = add nuw nsw i32 %i, 1
  %cond = icmp eq i32 %i.next, %n
  br i1 %cond, label %exit.loopexit, label %for.body
}
