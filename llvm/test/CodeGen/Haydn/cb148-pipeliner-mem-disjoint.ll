; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s -o /dev/null
; REQUIRES: haydn-registered-target
;
; NOTE on this base: the abort described below was measured on the pre-merge
; haydn line, where the <4 x i16> inner loop legalized into several halfword
; accesses. This base keeps the vector in DR registers (x4add16 + word
; accesses), so the assert shape is not currently reachable here; the test
; stays as the compile-success guard for whenever legalization or the SMS
; gates shift the shape back into hasLoopCarriedMemDep's cheap path.

; CB-148: clang aborted compiling CoreMark. This is `matrix_add_const` from
; core_matrix.c, reduced to the IR the backend actually receives — the inner
; loop vectorized to <4 x i16>, which legalizes into several halfword accesses
; off one base at different constant offsets.
;
; MachinePipeliner's hasLoopCarriedMemDep takes a cheap path when two accesses
; share a base register and the lower offset comes first, and asserts that the
; target agrees they are trivially disjoint:
;
;   assert(TII->areMemAccessesTriviallyDisjoint(SrcMI, DstMI) &&
;          "What happened to the chain edge?")
;
; TargetInstrInfo's default answers false for every pair, so a target that
; enables the swing pipeliner without overriding it aborts the compiler the
; first time a loop reaches that path. Haydn enables it at O2+ and had no
; override.
;
; There is no wrong-output symptom to check for — the compiler either survives
; this function or it does not — so the test asserts nothing beyond compiling.
; Verified to abort without the override rather than assumed: a hand-written
; i32 loop did NOT reproduce it and would have been a vacuous regression test.
; The halfword read-modify-write and the vectorization are both load-bearing.

define dso_local void @matrix_add_const(i32 %N, ptr captures(none) %A,
                                        i16 signext %val) {
entry:
  %cmp17.not = icmp eq i32 %N, 0
  br i1 %cmp17.not, label %for.end9, label %for.cond1.preheader.preheader

for.cond1.preheader.preheader:
  %min.iters.check = icmp ult i32 %N, 4
  %n.vec = and i32 %N, -4
  %broadcast.splatinsert = insertelement <4 x i16> poison, i16 %val, i64 0
  %broadcast.splat = shufflevector <4 x i16> %broadcast.splatinsert,
                                   <4 x i16> poison, <4 x i32> zeroinitializer
  %cmp.n = icmp eq i32 %N, %n.vec
  br label %for.cond1.preheader

for.cond1.preheader:
  %i.018 = phi i32 [ %inc8, %for.inc7 ], [ 0, %for.cond1.preheader.preheader ]
  %mul = mul i32 %i.018, %N
  %0 = getelementptr i16, ptr %A, i32 %mul
  br i1 %min.iters.check, label %for.body3.preheader, label %vector.body

vector.body:
  %index = phi i32 [ %index.next, %vector.body ], [ 0, %for.cond1.preheader ]
  %1 = getelementptr i16, ptr %0, i32 %index
  %wide.load = load <4 x i16>, ptr %1, align 2
  %2 = add <4 x i16> %wide.load, %broadcast.splat
  store <4 x i16> %2, ptr %1, align 2
  %index.next = add nuw i32 %index, 4
  %3 = icmp eq i32 %index.next, %n.vec
  br i1 %3, label %middle.block, label %vector.body

middle.block:
  br i1 %cmp.n, label %for.inc7, label %for.body3.preheader

for.body3.preheader:
  %j.016.ph = phi i32 [ 0, %for.cond1.preheader ], [ %n.vec, %middle.block ]
  br label %for.body3

for.body3:
  %j.016 = phi i32 [ %inc, %for.body3 ], [ %j.016.ph, %for.body3.preheader ]
  %arrayidx = getelementptr i16, ptr %0, i32 %j.016
  %4 = load i16, ptr %arrayidx, align 2
  %add5 = add i16 %4, %val
  store i16 %add5, ptr %arrayidx, align 2
  %inc = add nuw i32 %j.016, 1
  %exitcond.not = icmp eq i32 %inc, %N
  br i1 %exitcond.not, label %for.inc7, label %for.body3

for.inc7:
  %inc8 = add nuw i32 %i.018, 1
  %exitcond19.not = icmp eq i32 %inc8, %N
  br i1 %exitcond19.not, label %for.end9, label %for.cond1.preheader

for.end9:
  ret void
}
