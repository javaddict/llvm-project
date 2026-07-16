; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -O1 < %s | FileCheck %s

;
; REGRESSION TEST: HardwareLoops trip-count synthesis from a stack-spilled
; bound/bump register that shares its physreg with other constants.
;
; Bug : the abs-idiom body
; int s = x >> 31; int a = (x + s) ^ s; // branchless abs
; inside a 16-iteration counted loop forced the IV bound and bump to be spilled
; to stack slots. Post-RA the allocator reused ONE physreg ($r1) to materialize
; several constants in the function entry block:
; $r1 = ADDI32 $r0, 64; ST32 $r1, $r13, 0; limit slot = 64
; $r1 = ADDI32 $r0, 4; ST32 $r1, $r13, 8; bump slot = 4
; $r1 = ADDI32 $r0, 255; ST32 $r1, $r13, 4; mask slot = 255
; The Haydn Hardware Loop Detection pass resolved the stack-spilled bound via
; block-global findImmediateDef (returns the LAST def of $r1 = 255), so BOTH
; the limit and the bump resolved to 255 -> trip count = 255/255 = 1. The loop

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: main:
; CHECK: {{.}}

define dso_local i32 @main() local_unnamed_addr #0 {
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
