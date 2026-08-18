; RUN: opt -mtriple=haydn-unknown-elf -passes=loop-unroll -stats \
; RUN:   -disable-output < %s 2>&1 | FileCheck %s --check-prefix=STATS
; RUN: opt -mtriple=haydn-unknown-elf -passes=loop-unroll \
; RUN:   -haydn-prefer-swp-over-unroll=999 -stats -disable-output < %s 2>&1 | \
; RUN:   FileCheck %s --check-prefix=NODFER
; REQUIRES: asserts
;
; Role: semantic — TTI unroll↔SMS guard (AIE aie-prefer-swp-over-unroll
; analog, AIEBaseTargetTransformInfo.cpp:72-73 / :204-208) plus
; getPreferredAddressingMode = AMK_PostIndexed (Hexagon
; HexagonTargetTransformInfo.cpp:104-106).
;
; SCEV-constant trip at or above the default floor (9) stays rolled so
; software pipelining can measure the original loop. Runtime / small
; constant trips still densify. Flag 999 disables the floor (never defer).
; Defer runs before densify eligibility so BaseT Partial/Runtime cannot
; still unroll a reserved loop.

target triple = "haydn-unknown-elf"

define void @memcpy_runtime(ptr noalias %dst, ptr noalias readonly %src, i32 %n) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr inbounds i32, ptr %src, i32 %i
  %dp = getelementptr inbounds i32, ptr %dst, i32 %i
  %v = load i32, ptr %sp, align 4
  store i32 %v, ptr %dp, align 4
  %i.next = add nuw nsw i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

define void @memcpy_const16(ptr noalias %dst, ptr noalias readonly %src) nounwind {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr inbounds i32, ptr %src, i32 %i
  %dp = getelementptr inbounds i32, ptr %dst, i32 %i
  %v = load i32, ptr %sp, align 4
  store i32 %v, ptr %dp, align 4
  %i.next = add nuw nsw i32 %i, 1
  %c = icmp slt i32 %i.next, 16
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

define i32 @mac_const4(ptr readonly %a, ptr readonly %b) nounwind {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %ap = getelementptr inbounds i32, ptr %a, i32 %i
  %bp = getelementptr inbounds i32, ptr %b, i32 %i
  %va = load i32, ptr %ap, align 4
  %vb = load i32, ptr %bp, align 4
  %m = mul i32 %va, %vb
  %acc.next = add i32 %acc, %m
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, 4
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %acc.next
}

define i32 @mac_const16(ptr readonly %a, ptr readonly %b) nounwind {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %ap = getelementptr inbounds i32, ptr %a, i32 %i
  %bp = getelementptr inbounds i32, ptr %b, i32 %i
  %va = load i32, ptr %ap, align 4
  %vb = load i32, ptr %bp, align 4
  %m = mul i32 %va, %vb
  %acc.next = add i32 %acc, %m
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, 16
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %acc.next
}

; STATS-DAG: {{[1-9][0-9]*}} haydn-tti {{.*}}short streams given densify Partial/Runtime unroll
; STATS-DAG: {{[1-9][0-9]*}} haydn-tti {{.*}}densify streams left rolled for software pipelining

; NODFER-DAG: {{[1-9][0-9]*}} haydn-tti {{.*}}short streams given densify Partial/Runtime unroll
; NODFER-NOT: densify streams left rolled for software pipelining
