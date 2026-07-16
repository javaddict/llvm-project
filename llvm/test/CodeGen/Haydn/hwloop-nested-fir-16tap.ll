; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -stop-after=haydn-hwloops < %s | FileCheck %s
;
; REGRESSION TEST: nested-loop conversion — inner-loop latch explicit branch.
;
; Bug (before): when the inner loop of a nested pair converted to a
; hardware loop AND the inner latch was laid out LAST in the function (so its
; exit block — the outer latch — sat EARLIER in the layout), the post
; conversion inner latch had its conditional branch erased but no explicit
; branch inserted. The latch ended up with successor == outer-latch but no
; terminator reaching it, triggering:
;
; *** Bad machine code: MBB has unexpected successors which are not branch
; targets, fallthrough, EHPads, or inlineasm_br targets. ***
; basic block: %bb.N for.body4
;
; This is the fir-16tap shape: the clang-produced layout puts for.cond.cleanup
; (function exit) before the created inner preheader + inner body, making the
; inner body the LAST block. Its only successor after conversion is
; for.cond.cleanup3 (outer latch), which is NOT the layout fallthrough.
;
; Fix : after erasing the latch terminators and updating successors, if
; the ExitBB is not the layout fallthrough of the latch, insert an explicit
; unconditional `B ExitBB`.
;
; Test design: nested FIR with RUNTIME inner trip count (%n_taps) so the inner
; loop is NOT unrolled. The inner-loop guard (%cmp218) is hoisted to the outer
; preheader, and the inner preheader (for.body4.lr.ph) is a separate block
; this is the exact shape clang produces for `for (i...) { for (j=0; j<n_taps;
; j++)... }`, and it triggers the layout where the inner body is last. The
; inner loop converts to LoopStart/SET (sel=1). Outer may also convert when
; dual nesting is ON (default) if free-list + structure checks pass; the
; CHECK requires the inner conversion and verifier pass (explicit B).
;
; If this test regresses (the explicit B disappears), llc crashes with the
; MachineVerifier error above when -verify-machineinstrs is on.
;
; Decision reference: ~/haydn-plans/decisions/-hwloop-nested-explicit-branch.md

define void @fir_16tap(ptr noalias %y, ptr noalias %x, ptr noalias %h,
                       i32 %n_out, i32 %n_taps) nounwind {
; CHECK-LABEL: name: fir_16tap
; The inner loop MUST convert to a hardware loop (sel=1).
; CHECK: SET_HWLOOP
entry:
  %cmp21 = icmp sgt i32 %n_out, 0
  br i1 %cmp21, label %for.cond1.preheader.lr.ph, label %for.cond.cleanup

for.cond1.preheader.lr.ph:
  %cmp218 = icmp sgt i32 %n_taps, 0
  br label %for.cond1.preheader

for.cond1.preheader:
  %i.022 = phi i32 [ 0, %for.cond1.preheader.lr.ph ], [ %inc9, %for.cond.cleanup3 ]
  br i1 %cmp218, label %for.body4.lr.ph, label %for.cond.cleanup3

for.body4.lr.ph:
  %0 = getelementptr inbounds i32, ptr %x, i32 %i.022
  br label %for.body4

for.cond.cleanup:
  ret void

for.cond.cleanup3:
  %acc.0.lcssa = phi i32 [ 0, %for.cond1.preheader ], [ %add6, %for.body4 ]
  %arrayidx7 = getelementptr inbounds i32, ptr %y, i32 %i.022
  store i32 %acc.0.lcssa, ptr %arrayidx7, align 4
  %inc9 = add i32 %i.022, 1
  %exitcond23.not = icmp eq i32 %inc9, %n_out
  br i1 %exitcond23.not, label %for.cond.cleanup, label %for.cond1.preheader

for.body4:
  %j.020 = phi i32 [ 0, %for.body4.lr.ph ], [ %inc, %for.body4 ]
  %acc.019 = phi i32 [ 0, %for.body4.lr.ph ], [ %add6, %for.body4 ]
  %arrayidx = getelementptr inbounds i32, ptr %0, i32 %j.020
  %1 = load i32, ptr %arrayidx, align 4
  %arrayidx5 = getelementptr inbounds i32, ptr %h, i32 %j.020
  %2 = load i32, ptr %arrayidx5, align 4
  %mul = mul i32 %2, %1
  %add6 = add i32 %mul, %acc.019
  %inc = add i32 %j.020, 1
  %exitcond.not = icmp eq i32 %inc, %n_taps
  br i1 %exitcond.not, label %for.cond.cleanup3, label %for.body4
}
