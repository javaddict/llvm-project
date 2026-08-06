; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -O2 -debug-only=pipeliner < %s 2>&1 | FileCheck %s --check-prefix=SWP

; Role: semantic — SMS must ANALYZE a canonical countable loop compiled at DEFAULT -O2 (no -mattr=-hwloop).

; REGRESSION TEST : SMS must ANALYZE a canonical countable loop compiled
; at DEFAULT -O2 (no -mattr=-hwloop). The IR-level HardwareLoops pass runs
; before IRTranslator, so every countable single-BB loop arrives at the pre-RA
; pipeliner ALREADY in ZOL form -- LoopStart (preheader) + PseudoLoopEnd (latch)
; with the original icmp+br latch REMOVED. analyzeLoopForPipelining must
; recognize this ZOL shape (not return nullptr).
;
; Bug: EnableZOLPipelining defaulted to false ("careful validation" gate).
; The naive analyzeCountableLoop path only matches icmp+br terminators, which
; are gone post-hardware-loops; with ZOL pipelining off, SMS printed
; "Unable to analyzeLoop, can NOT pipeline Loop" for 100% of real loops and
; returned nullptr. This contradicted ("classic SMS running PRE-RA on ZOL
; form") and gated the LD64 SWPS unblock from paying off.
;
; Fix: flip EnableZOLPipelining default ON. The ZOL PipelinerLoopInfo
; (shouldIgnoreForPipelining, shouldUseSchedule rejecting StageCount<=1
; adjustTripCount editing LoopStart's $adj, no-guard createTripCountGreaterCondition)
; is complete and AIE-faithful; the +358-bloat regression is now gated
; inside shouldUseSchedule.
;
; Test design: simplest countable dot-product loop, IV 0->n step 1, exit on
; add.i == n. Compiled WITHOUT -mattr=-hwloop so it is ZOL form at SMS. If the
; fix regresses, llc prints "Unable to analyzeLoop" and the SWP-NOT fails.
;
; NOTE: this test does NOT assert a profitable multi-stage schedule (that needs
; the LD64 slot-polymorphic unblock on a 2-load streaming loop). It pins
; the ANALYZABILITY contract: analyzeLoopForPipelining must not reject the ZOL
; form. Whether SMS then finds a profitable II depends on the loop body's
; recurrence/resource MII (a separate concern tracked by the -mattr=-hwloop
; naive-path swpipeline-*.ll tests).

; SWP-NOT: Unable to analyzeLoop

define i32 @dot(i32* nocapture readonly %a, i32* nocapture readonly %b, i32 %n) {
entry:
  %cmp4 = icmp sgt i32 %n, 0
  br i1 %cmp4, label %for.body.preheader, label %for.cond.cleanup
for.body.preheader:
  br label %for.body
for.cond.cleanup.loopexit:
  %add.lcssa = phi i32 [ %add, %for.body ]
  br label %for.cond.cleanup
for.cond.cleanup:
  %sum.0.lcssa = phi i32 [ 0, %entry ], [ %add.lcssa, %for.cond.cleanup.loopexit ]
  ret i32 %sum.0.lcssa
for.body:
  %i.06 = phi i32 [ %add.i, %for.body ], [ 0, %for.body.preheader ]
  %sum.05 = phi i32 [ %add, %for.body ], [ 0, %for.body.preheader ]
  %idxprom = zext i32 %i.06 to i64
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %idxprom
  %0 = load i32, i32* %arrayidx, align 4
  %arrayidx2 = getelementptr inbounds i32, i32* %b, i64 %idxprom
  %1 = load i32, i32* %arrayidx2, align 4
  %mul = mul nsw i32 %1, %0
  %add = add nsw i32 %mul, %sum.05
  %add.i = add nuw nsw i32 %i.06, 1
  %exitcond = icmp eq i32 %add.i, %n
  br i1 %exitcond, label %for.cond.cleanup.loopexit, label %for.body
}
