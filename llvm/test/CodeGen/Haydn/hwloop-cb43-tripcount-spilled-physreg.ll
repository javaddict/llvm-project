; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -O1 < %s | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -O1 -haydn-enable-hwloops=0 < %s | FileCheck %s --check-prefix=HWOFF

; 2026-08-22 hwloop product-default flip rebaseline: default is now ON.

; Role: semantic — SCEV-proven trip must stay 16 when nearby constants
; share physregs (not collapse to 1 via last-def of a later 255 mask).
; Product default (ON since 2026-08-22) arms both counted loops as
; Role-A SET sel=0 with the trip materialised as 16; HWOFF is the
; software counted residual. Never free HWLR CSR. Never late physical
; rediscovery of the trip from the spilled last-def.

define dso_local i32 @main() local_unnamed_addr #0 {
; DEFAULT-LABEL: main:
; Trip 16 is materialised once and reused by both SETs (not 1 via last-def).
; DEFAULT:       addi32 {{.*}}, 16
; DEFAULT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DEFAULT-NOT:   set_hwloop_f2 1,
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       jalr
;
; HWOFF-LABEL: main:
; HWOFF-NOT:   set_hwloop
; HWOFF-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; HWOFF:       bnez
; HWOFF:       bnez
; HWOFF:       jalr
entry:
  %v = alloca [16 x i32], align 4
  br label %for.body

for.body:                                         ; preds = %entry, %for.body
  %i.019 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %arrayidx = getelementptr inbounds [16 x i32], ptr %v, i32 %i.019
  store i32 %i.019, ptr %arrayidx, align 4
  %inc = add nuw nsw i32 %i.019, 1
  %exitcond.not = icmp eq i32 %inc, 16
  br i1 %exitcond.not, label %for.body5, label %for.body

for.body5:                                        ; preds = %for.body, %for.body5
  %L1.021 = phi i32 [ %add7, %for.body5 ], [ 0, %for.body ]
  %i1.020 = phi i32 [ %inc9, %for.body5 ], [ 0, %for.body ]
  %arrayidx6 = getelementptr inbounds [16 x i32], ptr %v, i32 %i1.020
  %0 = load i32, ptr %arrayidx6, align 4
  %shr = ashr i32 %0, 31
  %add = add i32 %0, %shr
  %xor = xor i32 %add, %shr
  %add7 = add nuw nsw i32 %xor, %L1.021
  %inc9 = add nuw nsw i32 %i1.020, 1
  %exitcond22.not = icmp eq i32 %inc9, 16
  br i1 %exitcond22.not, label %for.cond.cleanup4, label %for.body5

for.cond.cleanup4:                                ; preds = %for.body5
  %and = and i32 %add7, 255
  ret i32 %and
}

attributes #0 = { nounwind "target-features"="+hwloop" }
