; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; REGRESSION TEST: CB-165 — a demoted hwloop whose trip physreg is REDEFINED
; by the (software-pipelined) loop body must not reload the stale TRIP value
; over the body's live-out def at the loop exit.
;
; Bug (pr51581-2 @ -O2; only the LAST element of each mod-by-constant store
; loop went bad): MachinePipeliner kept the loop-carried stage value in the
; same physreg the ZOL trip used (r5). The stack-counter demote path then
; unconditionally saved Prefer (the trip) into the demote-save FI in the
; preheader and reloaded it into r5 at the exit — c[N-1] received the raw
; trip value instead of the pipelined epilogue's a[N-1]-x*3.
;
; Fix law (HaydnHardwareLoops.cpp demote value-preserve): the save/restore
; pair exists only to return the value Prefer must carry OUT of the loop,
; and only the demote's own latch-scratch window can destroy it:
;   * latch scratch != Prefer (this shape): nothing installed touches
;     Prefer — no save, no restore (the old unconditional pair is what
;     miscompiled this case);
;   * latch scratch == Prefer, body redefines Prefer: save at latch end
;     (captures the body def), restore at exit;
;   * latch scratch == Prefer, body does not redefine: preheader trip save
;     + exit restore (the CB-162 shape, cb162-hwloop-demote-liveout-tripreg).
@a = external global [4096 x i32]
@c = external global [4096 x i32]

define void @cb165_pipelined_body_redefines_tripreg() {
entry:
  br label %for.body
for.cond.cleanup:
  ret void
for.body:
  %i.013 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %arrayidx = getelementptr inbounds i32, ptr @a, i32 %i.013
  %0 = load i32, ptr %arrayidx, align 4
  %conv = sext i32 %0 to i64
  %mul = mul nsw i64 %conv, 1431655766
  %shr = lshr i64 %mul, 32
  %conv1 = trunc i64 %shr to i32
  %shr3.neg = lshr i32 %0, 31
  %sub = add i32 %shr3.neg, %conv1
  %mul5.neg = mul i32 %sub, -3
  %sub6 = add i32 %mul5.neg, %0
  %arrayidx7 = getelementptr inbounds i32, ptr @c, i32 %i.013
  store i32 %sub6, ptr %arrayidx7, align 4
  %inc = add nuw nsw i32 %i.013, 1
  %exitcond.not = icmp eq i32 %inc, 4096
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body
}

; The demote keeps the software-counted latch on a stack-counter FI with a
; non-Prefer latch scratch (r6), and the loop body redefines the SET trip
; register r5 with the pipelined loop-carried value.
; CHECK-LABEL: cb165_pipelined_body_redefines_tripreg:
; latch countdown on the stack counter, not on the value register:
; CHECK: st32 {{r[0-9]+}}, sp, 0
; CHECK: bnez
; Exit: the epilogue store must consume the loop-computed value register
; DIRECTLY. The bug reloaded the stale trip first (ld32 into the value
; register from the demote-save slot, then the store) — nothing may load
; from the stack between the back-edge and that store.
; CHECK-NOT: ld32
; CHECK: s_sw_post_imm r5,
; CHECK: jalr
