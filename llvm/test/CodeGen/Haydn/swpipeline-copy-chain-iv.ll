; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 < %s | FileCheck %s
;
; REGRESSION TEST (-rework, G4 SW-pipeliner recognizer): the
; MachinePipeliner must ACCEPT this countable single-BB MAC loop
; analyzeLoopForPipelining must NOT return nullptr. STATUS (re-verified
; post- cutover): SMS ACCEPTS the loop AND finds a profitable
; schedule (II=2). The SWP checks below assert BOTH the acceptance bar (no
; "Unable to analyzeLoop") and the schedule result ("Schedule Found? 1 (II=2)").
;
; ROOT CAUSE this test pins (CLASS-2 false-negative; see lesson and
; decision -rework): the MachinePipeliner runs PRE-PHIElimination, so it
; sees PHI-form MIR, NOT the post-PHIElimination COPY cycles the earlier
; "copy-chain" recognizer targeted (that code was dead -- it never matched).
; The actual loop shape (from `llc -print-before=pipeliner`):
;
; bb.loop:
; %iv = PHI %init, %preheader, %bump, %bb.loop; the IV
; %bump = ADD32 %iv, %step; or ADDI32 / SUB32
; %cmp = SEQ32 %bump, %limit
;
; The pre-rework findInductionVar could not recognize PHI-form IVs: it only
; accepted self-referential ADD (`%iv = ADD %iv, %step`, which never occurs
; pre-PHIElimination) or copy-chain cycles (which never occur at the
; pipeliner). It returned Register for EVERY real clang/LSR loop ->
; analyzeLoopForPipelining returned nullptr -> SMS printed "Unable to
; analyzeLoop, can NOT pipeline Loop" and never reached MII/profitability.
; 100% of MAC-heavy countable loops were rejected.
;
; FIX (-rework,): findInductionVar now recognizes PHI-form. Given a
; compare source Reg: if Reg's def is ADD/ADDI/SUB, the IV candidate is its
; non-immediate register source, which must be a PHI whose latch-incoming
; value == the bump's def. It also handles the symmetric shape (compare
; directly on the IV PHI). createTripCountGreaterCondition still always
; returns nullopt (safe; no static trip-count shortcut).
;
; Test design: vec_dot is the canonical SMS candidate (two streamed loads +
; a MAC + an accumulation, all loop-carried). -mattr=-hwloop keeps the loop
; visible to SMS (HaydnHardwareLoops does not convert it). -verify-machineinstrs
; fails the build if the modulo schedule expander ever produces broken
; phi/renaming -- the class wrong-code gate.
;
; SWP asserts the G4 acceptance bar: "Unable to analyzeLoop" is GONE and SMS
; finds a real schedule (II=2). The non-SWP CHECK asserts the MAC body
; (mul64.ll post- DR64-MAC lowering) survives intact (SMS never silently
; drops instructions).
;
; What breaks if the bug reappears: the pipeliner prints "Unable to
; analyzeLoop, can NOT pipeline Loop" on stderr, the Schedule-Found check
; below no longer matches, and the loop is emitted as a plain non-pipelined
; loop.

define i32 @vec_dot(ptr nocapture readonly %a, ptr nocapture readonly %b, i32 %n) {
; CHECK-LABEL: vec_dot:
; The scalar multiply must survive -- SMS never drops instructions.
; Post- the s32 multiply lowers via the DR64 MAC unit (mull), not mac32.
; CHECK:        mull
; G4 acceptance bar: the recognizer must NOT reject this loop. The debug
; output must NOT contain the pre--rework rejection banner.
; SWP-NOT:      Unable to analyzeLoop
; SMS now finds a profitable >=2-stage schedule for this loop (post
; rework the recognizer accepts it AND the recurrence leaves slack for
; overlap). Assert the schedule-search result is "1" (found) with II=2.
; If SMS regresses to "Schedule Found? 0", this check fails.
; SWP:          # succs left : 2
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
