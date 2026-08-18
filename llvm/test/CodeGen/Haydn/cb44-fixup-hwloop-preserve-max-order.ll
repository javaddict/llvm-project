; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s

; Role: semantic — residual / FixupHwLoops tryShortenStartOffset must not reverse preheader reduction order when moving MIs before SET_HWLOOP.

; residual / FixupHwLoops tryShortenStartOffset must not reverse
; preheader reduction order when moving MIs before SET_HWLOOP.
;
; Bug: repeatedly splicing the *last* post-SET MI before SET reversed
; MAX32 chains so the loop live-in held an intermediate max (sim exit 138
; vs host 145). Fix: move the *first* post-SET MI so relative order holds.
;
; This is the full unrolled argmax + proximity count from
; benchmarks/compiler_bugs/cb44_o2_stale_cond_max_reduce.c (noinline datav).

define hidden i32 @datav(i32 noundef %i) local_unnamed_addr {
entry:
  %mul = mul nsw i32 %i, 17
  %shl = shl i32 %i, 3
  %xor = xor i32 %mul, %shl
  %and = and i32 %xor, 31
  %sub = add nsw i32 %and, -16
  ret i32 %sub
}

define dso_local i32 @main() local_unnamed_addr {
; CHECK-LABEL: main:
; Must exit with host golden low8 (145), not a reversed-max residual.
; We only check codegen still produces a hwloop or software loop and returns
; via jalr; the BundleSim case is the numeric gate.
; CHECK: jalr{{.*}}lr
entry:
  %data = alloca [16 x i32], align 4
  br label %for.body

for.body:
  %i.0 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %call = call i32 @datav(i32 noundef %i.0)
  %arrayidx = getelementptr inbounds [16 x i32], ptr %data, i32 0, i32 %i.0
  store i32 %call, ptr %arrayidx, align 4
  %inc = add nuw nsw i32 %i.0, 1
  %exitcond = icmp eq i32 %inc, 16
  br i1 %exitcond, label %for.body7.preheader, label %for.body

for.body7.preheader:
  %0 = load i32, ptr %data, align 4
  br label %for.body7

for.body7:
  %i4.0 = phi i32 [ %inc12, %for.body7 ], [ 1, %for.body7.preheader ]
  %mxIdx.0 = phi i32 [ %i4.0.mxIdx.0, %for.body7 ], [ 0, %for.body7.preheader ]
  %mx.0 = phi i32 [ %cond, %for.body7 ], [ %0, %for.body7.preheader ]
  %arrayidx8 = getelementptr inbounds [16 x i32], ptr %data, i32 0, i32 %i4.0
  %1 = load i32, ptr %arrayidx8, align 4
  %cmp9 = icmp sgt i32 %1, %mx.0
  %cond = select i1 %cmp9, i32 %1, i32 %mx.0
  %i4.0.mxIdx.0 = select i1 %cmp9, i32 %i4.0, i32 %mxIdx.0
  %inc12 = add nuw nsw i32 %i4.0, 1
  %exitcond43 = icmp eq i32 %inc12, 16
  br i1 %exitcond43, label %for.body17, label %for.body7

for.body17:
  %lsr.iv = phi ptr [ %scevgep, %for.body17 ], [ %data, %for.body7 ]
  %i13.0 = phi i32 [ %inc26, %for.body17 ], [ 0, %for.body7 ]
  %sum.0 = phi i32 [ %sum.1, %for.body17 ], [ 0, %for.body7 ]
  %cnt.0 = phi i32 [ %cnt.1, %for.body17 ], [ 0, %for.body7 ]
  %2 = load i32, ptr %lsr.iv, align 4
  %sub18 = sub nsw i32 %2, %cond
  %3 = call i32 @llvm.abs.i32(i32 %sub18, i1 true)
  %cmp19 = icmp ult i32 %3, 3
  %inc23 = zext i1 %cmp19 to i32
  %cnt.1 = add nuw nsw i32 %cnt.0, %inc23
  %add = select i1 %cmp19, i32 %i13.0, i32 0
  %sum.1 = add nuw nsw i32 %add, %sum.0
  %inc26 = add nuw nsw i32 %i13.0, 1
  %scevgep = getelementptr i8, ptr %lsr.iv, i32 4
  %exitcond.not = icmp eq i32 %inc26, 16
  br i1 %exitcond.not, label %for.cond.cleanup16, label %for.body17

for.cond.cleanup16:
  %mul28 = mul nsw i32 %cond, 3
  %mul29 = mul nsw i32 %i4.0.mxIdx.0, 5
  %add30 = add nsw i32 %mul28, %mul29
  %mul31 = mul nuw nsw i32 %cnt.1, 7
  %add32 = add nsw i32 %add30, %mul31
  %add33 = add nsw i32 %add32, %sum.1
  %and34 = and i32 %add33, 255
  ret i32 %and34
}

declare i32 @llvm.abs.i32(i32, i1 immarg)
