; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -stop-before=haydn-finalize-mi-bundles \
; RUN:     < %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -stop-after=machine-cp < %s -o - \
; RUN:     | FileCheck %s --check-prefix=MCP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -stop-before=haydn-finalize-mi-bundles \
; RUN:     -debug-only=pipeliner < %s -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; REQUIRES: asserts

; Role: Option C containment — fill_nn LCG does not freeze stages=2 hard BUNDLE.
; Freeze-era pin expected multi-stage SWPS + BUNDLE+COPY; deleted pre-RA cycle
; identity. Product multi-stage is post-RA only.
;
; NStages==1 is a legal kernel-only / pre-RA bare-logical schedule. This body
; finds stages=1 (no overlap). Pre-RA still rejects StageCount>1 before
; mutation; StageCount==1 is accepted as bare logical MIs (no durable
; BUNDLE+COPY). Target-rejected is the old StageCount>1 arm — do not require
; it here.
;
; HC#0 II-retry (2026-08-28): shouldUseSchedule runs inside the II
; search; accept lines precede "Schedule Found? 1".

; SWP: SMS-SHOULDUSE: accept stages=1 II={{[0-9]+}}
; SWP: Schedule Found? 1
; SWP-NOT: SMS-SHOULDUSE: accept multi-stage durable
; SWP-NOT: SMS-HANDOFF: materialize done groups={{[1-9][0-9]*}}

; ASM: name: fill_nn
; ASM: MULL
; ASM: SEQ32
; ASM: BEQZ

; MCP-LABEL: name: fill_nn
; 2026-08-19: MULL untied per golden (rt = rs1*rs2 non-destructive; ISS
; CC_G_GG_M) — the tied-form COPY-before-MULL is gone; MULL consumes regs
; directly.
; MCP: $r{{[0-9]+}} = MULL{{.*}}$r{{[0-9]+}}{{.*}}$r{{[0-9]+}}
; MCP-NOT: COPY{{.*}}= COPY{{.*}}
; MCP-NOT: BUNDLE{{.*}}{

define dso_local void @fill_nn(ptr noundef writeonly captures(none) %C,
                               i32 noundef %n,
                               ptr noundef captures(none) %s_io) local_unnamed_addr {
entry:
  %0 = load i32, ptr %s_io, align 4
  %cmp9.not = icmp eq i32 %n, 0
  br i1 %cmp9.not, label %for.cond.cleanup, label %for.body.preheader
for.body.preheader:
  %mul = mul i32 %n, %n
  %umax = tail call i32 @llvm.umax.i32(i32 %mul, i32 1)
  br label %for.body
for.cond.cleanup:
  %s.0.lcssa = phi i32 [ %0, %entry ], [ %add, %for.body ]
  store i32 %s.0.lcssa, ptr %s_io, align 4
  ret void
for.body:
  %k.011 = phi i32 [ %inc, %for.body ], [ 0, %for.body.preheader ]
  %s.010 = phi i32 [ %add, %for.body ], [ %0, %for.body.preheader ]
  %mul1 = mul i32 %s.010, 1664525
  %add = add i32 %mul1, 1013904223
  %conv2 = ashr i32 %add, 16
  %arrayidx = getelementptr inbounds nuw i32, ptr %C, i32 %k.011
  store i32 %conv2, ptr %arrayidx, align 4
  %inc = add nuw nsw i32 %k.011, 1
  %exitcond.not = icmp eq i32 %inc, %umax
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body
}
declare i32 @llvm.umax.i32(i32, i32)
