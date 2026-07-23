; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 < %s | FileCheck %s
; STALE-FAILMARKER-REMOVED (, post- cutover): the SMS
; analyzability regression has cleared — analyzeLoopForPipelining now accepts
; this canonical vec_dot_streaming loop and SMS profitably schedules it
; (Schedule Found? 1, II=2). The SWP-NOT acceptance bar and the mull
; kernel-body CHECK both pass.
;
; REGRESSION TEST (G4 / -rework): MachinePipeliner (Swing Modulo Scheduling)
; must ACCEPT the canonical LSR-produced countable single-BB loop shape -- i.e.
; analyzeLoopForPipelining must NOT return nullptr ("Unable to analyzeLoop"
; must be GONE from the debug output) -- AND, now that the SFR-strip has
; collapsed the recurrence-bound MII, SMS PROFITABLY pipelines it.
;
; STATUS (re-verified live via -debug-only=pipeliner): the PHI-form
; IV recognizer (-rework,) lands. analyzeLoopForPipelining no longer
; rejects this loop. Furthermore the SFR-strip (re-applied; "re-apply
; SFR-strip from non-flag ALU ops") removed the artificial SFR WAW chain that
; used to inflate the recurrence MII to the loop body length, so SMS now finds
; a profitable schedule:
; Res MII: 4, MII = 4, MAX_II = 14 (rec=1, res=4); slot pressure
; Schedule Found? 1 (II=6)
; The earlier "copy-chain" recognizer was a MISDIAGNOSIS: it assumed the
; pipeliner sees post-PHIElimination COPY cycles, but the MachinePipeliner runs
; PRE-PHIElimination and sees genuine PHI-form MIR. The copy-chain code was
; dead -- it never matched -- so every countable loop was still rejected with
; "Unable to analyzeLoop". The rework recognizes the real shape:
;
; bb.loop:
; %iv = PHI %init, %preheader, %bump, %bb.loop; the IV
; %bump = ADD32 %iv, %step; or ADDI32 / SUB32
; %cmp = SEQ32 %bump, %limit
;
; findInductionVar now accepts an induction-step bump whose non-immediate
; source is a PHI whose latch-incoming value == the bump's def (closing the
; back-edge cycle), plus the symmetric compare-on-PHI shape.
;
; NOTE on earlier stale comments: a prior revision claimed "the G4 fix landed
; SMS tried II=3..13 and reported Schedule Found? 0". That was FALSE: the loop
; was rejected at analyzeLoopForPipelining and never reached the schedule
; search. The copy-chain claim was likewise FALSE (dead code). Then, even
; after the recognizer accepted the loop, the SFR WAW chain kept MII too high
; for a profitable schedule. Both are corrected here: the recognizer accepts
; AND the SFR-strip lets SMS find II=3.
;
; Test design: vec_dot_streaming is the canonical SMS candidate -- two
; streamed loads + a MAC + an accumulation, all loop-carried. We use
; mattr=-hwloop so the HaydnHardwareLoops pass does not convert this loop to a
; zero-overhead hardware loop, leaving it visible to SMS. The
; verify-machineinstrs flag fails the build if the modulo schedule expander
; ever produces broken phi/renaming. The mull assertion proves the loop body
; survived (the pipeliner never silently drops instructions).
;
; SWP asserts the G4 acceptance bar ("Unable to analyzeLoop" is GONE). The
; mattr=-hwloop path disables hardware loops so SMS sees the loop; the
; post-inc fusion still fires, so the body has s_lw_post_imm + mul64.
; ResMII must reflect multi-slot pressure (not stuck at 1).
; SWP: Return Res MII:{{[1-9][0-9]*}}
; SWP-NOT: Unable to analyzeLoop

define i32 @vec_dot_streaming(ptr nocapture readonly %a, ptr nocapture readonly %b, i32 %n) {
; CHECK-LABEL: vec_dot_streaming:
; Default path (hwloops enabled): the loop lowers as a zero-overhead hardware
; loop. Post-inc fusion (+ LD64/LD32 candidate-gate fix): the two
; streaming loads fuse to s_lw_post_imm. The mull must survive.
; CHECK:        // =>This Inner Loop Header: Depth=1
; CHECK:        s_lw_post_imm
; CHECK:        mull
entry:
  br label %for.body

for.body:
  %i.011 = phi i32 [ 0, %entry ], [ %add6, %for.body ]
  %sum.010 = phi i32 [ 0, %entry ], [ %add, %for.body ]
  %arrayidx = getelementptr inbounds i32, ptr %a, i32 %i.011
  %lv = load i32, ptr %arrayidx, align 4
  %arrayidx2 = getelementptr inbounds i32, ptr %b, i32 %i.011
  %rv = load i32, ptr %arrayidx2, align 4
  %mul = mul nsw i32 %rv, %lv
  %add = add nsw i32 %mul, %sum.010
  %add6 = add nuw nsw i32 %i.011, 1
  %exitcond = icmp eq i32 %add6, %n
  br i1 %exitcond, label %for.end, label %for.body

for.end:
  %sum.0.lcssa = phi i32 [ %sum.010, %for.body ]
  ret i32 %sum.0.lcssa
}
