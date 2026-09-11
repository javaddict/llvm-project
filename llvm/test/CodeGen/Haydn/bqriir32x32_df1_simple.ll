; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — BiQuad IIR filter (Direct Form 1), 32x32-bit fixed-point (portable C version).

; BiQuad IIR filter (Direct Form 1), 32x32-bit fixed-point (portable C version).
; Pre-compiled from bqriir32x32_df1_simple.c with:
; clang -target haydn-unknown-elf -S -emit-llvm -O2
;
; Algorithm: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
; Fixed-point: Q31 data, Q30 coefficients, Q17.46 accumulator (64-bit).
;
; This test exercises the full Clang -> LLVM -> Haydn CodeGen pipeline for a
; real DSP workload: 32x32->64 multiply, 64-bit accumulation, 64-bit right
; shift, array access, and loop control flow.
;
; Original source: NatureDSP Signal Library, bqriir32x32_df1_hifi3.c.

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

%struct.bqriir32_df1_state_t = type { i32, i32, i32, i32, i32, i32, i32, i32, i32 }

define dso_local void @bqriir32x32_df1_process(ptr noundef %st, ptr noundef writeonly %r, ptr noundef readonly %x, i32 noundef %N) local_unnamed_addr #0 {
; CHECK-LABEL: bqriir32x32_df1_process:
entry:
  %sx06 = getelementptr inbounds nuw i8, ptr %st, i32 20
  %0 = load i32, ptr %sx06, align 4
  %sx17 = getelementptr inbounds nuw i8, ptr %st, i32 24
  %1 = load i32, ptr %sx17, align 4
  %sy08 = getelementptr inbounds nuw i8, ptr %st, i32 28
  %2 = load i32, ptr %sy08, align 4
  %sy19 = getelementptr inbounds nuw i8, ptr %st, i32 32
  %3 = load i32, ptr %sy19, align 4
  %cmp54 = icmp sgt i32 %N, 0
  br i1 %cmp54, label %for.body.lr.ph, label %for.cond.cleanup

for.body.lr.ph:
  %a25 = getelementptr inbounds nuw i8, ptr %st, i32 16
  %4 = load i32, ptr %a25, align 4
  %a14 = getelementptr inbounds nuw i8, ptr %st, i32 12
  %5 = load i32, ptr %a14, align 4
  %b23 = getelementptr inbounds nuw i8, ptr %st, i32 8
  %6 = load i32, ptr %b23, align 4
  %b12 = getelementptr inbounds nuw i8, ptr %st, i32 4
  %7 = load i32, ptr %b12, align 4
  %8 = load i32, ptr %st, align 4
  %conv = sext i32 %8 to i64
  %conv11 = sext i32 %7 to i64
  %conv14 = sext i32 %6 to i64
  %conv18 = sext i32 %5 to i64
  %conv21 = sext i32 %4 to i64
  br label %for.body

for.cond.cleanup:
  %sy1.0.lcssa = phi i32 [ %3, %entry ], [ %sy0.057, %for.body ]
  %sy0.0.lcssa = phi i32 [ %2, %entry ], [ %conv25, %for.body ]
  %sx1.0.lcssa = phi i32 [ %1, %entry ], [ %sx0.059, %for.body ]
  %sx0.0.lcssa = phi i32 [ %0, %entry ], [ %9, %for.body ]
  store i32 %sx0.0.lcssa, ptr %sx06, align 4
  store i32 %sx1.0.lcssa, ptr %sx17, align 4
  store i32 %sy0.0.lcssa, ptr %sy08, align 4
  store i32 %sy1.0.lcssa, ptr %sy19, align 4
  ret void

for.body:
  %sx0.059 = phi i32 [ %0, %for.body.lr.ph ], [ %9, %for.body ]
  %sx1.058 = phi i32 [ %1, %for.body.lr.ph ], [ %sx0.059, %for.body ]
  %sy0.057 = phi i32 [ %2, %for.body.lr.ph ], [ %conv25, %for.body ]
  %i.056 = phi i32 [ 0, %for.body.lr.ph ], [ %inc, %for.body ]
  %sy1.055 = phi i32 [ %3, %for.body.lr.ph ], [ %sy0.057, %for.body ]
  %arrayidx = getelementptr inbounds nuw i32, ptr %x, i32 %i.056
  %9 = load i32, ptr %arrayidx, align 4
  %conv10 = sext i32 %9 to i64
  %mul = mul nsw i64 %conv10, %conv
  %conv12 = sext i32 %sx0.059 to i64
  %mul13 = mul nsw i64 %conv12, %conv11
  %conv15 = sext i32 %sx1.058 to i64
  %mul16 = mul nsw i64 %conv15, %conv14
  %conv19 = sext i32 %sy0.057 to i64
  %conv22 = sext i32 %sy1.055 to i64
  %10 = mul nsw i64 %conv21, %conv22
  %11 = mul nsw i64 %conv18, %conv19
  %12 = add i64 %11, %10
  %add = add i64 %mul13, %mul16
  %add17 = sub i64 %add, %12
  %sub24 = add i64 %add17, %mul
  %shr = lshr i64 %sub24, 30
  %conv25 = trunc i64 %shr to i32
  %arrayidx26 = getelementptr inbounds nuw i32, ptr %r, i32 %i.056
  store i32 %conv25, ptr %arrayidx26, align 4
  %inc = add nuw nsw i32 %i.056, 1
  %exitcond.not = icmp eq i32 %inc, %N
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body
}

; 64-bit multiply chains select as mul64.ll / mula64.ll or the widened
; mul64.ulul expansion (GISel TD-first / formMACs burn-down). Kernel still
; uses a 64-bit subtract, loads/stores, and returns. CHECK-DAG keeps the
; test robust to scheduling changes.
; CHECK-DAG: mul{{a?64\.(ll|ulul)}}
; CHECK-DAG: sub64
; CHECK-DAG: ld32
; CHECK-DAG: st32
; CHECK-DAG: jalr{{(\.s[012])?}}

define dso_local void @bqriir32x32_df1_cascade(ptr noundef %sections, ptr noundef %r, ptr noundef readonly %x, i32 noundef %N, i32 noundef %M) local_unnamed_addr #0 {
; CHECK-LABEL: bqriir32x32_df1_cascade:
; CHECK: lui{{.*}}bqriir32x32_df1_process
; CHECK: addi32{{.*}}bqriir32x32_df1_process
; CHECK: jalr
entry:
  %cmp4 = icmp sgt i32 %M, 0
  br i1 %cmp4, label %for.body.preheader, label %for.cond.cleanup

for.body.preheader:
  tail call void @bqriir32x32_df1_process(ptr noundef %sections, ptr noundef %r, ptr noundef %x, i32 noundef %N)
  %exitcond.peel.not = icmp eq i32 %M, 1
  br i1 %exitcond.peel.not, label %for.cond.cleanup, label %for.body

for.cond.cleanup:
  ret void

for.body:
  %m.06 = phi i32 [ %inc, %for.body ], [ 1, %for.body.preheader ]
  %arrayidx = getelementptr inbounds nuw %struct.bqriir32_df1_state_t, ptr %sections, i32 %m.06
  tail call void @bqriir32x32_df1_process(ptr noundef nonnull %arrayidx, ptr noundef %r, ptr noundef %r, i32 noundef %N)
  %inc = add nuw nsw i32 %m.06, 1
  %exitcond.not = icmp eq i32 %inc, %M
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body
}

attributes #0 = { noinline nounwind "frame-pointer"="all" "no-trapping-math"="true" }
