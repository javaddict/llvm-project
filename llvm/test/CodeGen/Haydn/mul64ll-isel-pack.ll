; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; REGRESSION: i64 = sext(i32) * sext(i32) ISel-packs as sext32t64 x2 + mul64.ll
; (not loadi32/sra32 sign-mask chain, not __muldi3 libcall).

define i64 @mul64_ll_isel_pack_sext_sext(i32 %a, i32 %b) {
; CHECK-LABEL: mul64_ll_isel_pack_sext_sext:
; CHECK-NOT: __muldi3
; CHECK-NOT: loadi32
; CHECK-NOT: sra32
; CHECK-DAG: sext32t64
; CHECK-DAG: sext32t64
; CHECK: mul64.ll
  %aa = sext i32 %a to i64
  %bb = sext i32 %b to i64
  %m = mul i64 %aa, %bb
  ret i64 %m
}

define i64 @mul64_ll_isel_pack_in_loop(i32 %n, i32* nocapture readonly %p, i32 %c) {
; CHECK-LABEL: mul64_ll_isel_pack_in_loop:
; CHECK-DAG: sext32t64
; PostLegalizer formMACs burn-down: loop MAC may lower as mul64.ll / mula64.ll
; or the widened mul64.ulul expansion (still no __muldi3 / sra sign-mask).
; CHECK: mul{{a?64\.(ll|ulul)}}
; CHECK-NOT: sra32
; CHECK-NOT: loadi32
entry:
  %cmp9 = icmp sgt i32 %n, 0
  %cext = sext i32 %c to i64
  br i1 %cmp9, label %for.body, label %for.cond.cleanup

for.body:
  %i = phi i32 [ %inc, %for.body ], [ 0, %entry ]
  %acc = phi i64 [ %add, %for.body ], [ 0, %entry ]
  %arrayidx = getelementptr inbounds i32, i32* %p, i32 %i
  %0 = load i32, i32* %arrayidx, align 4
  %conv = sext i32 %0 to i64
  %mul = mul nsw i64 %conv, %cext
  %add = add nsw i64 %mul, %acc
  %inc = add nuw nsw i32 %i, 1
  %exitcond = icmp eq i32 %inc, %n
  br i1 %exitcond, label %for.cond.cleanup, label %for.body

for.cond.cleanup:
  %acc.lcssa = phi i64 [ 0, %entry ], [ %add, %for.body ]
  ret i64 %acc.lcssa
}
