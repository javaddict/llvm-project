; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:   -mattr=+hwloop -stop-before=haydn-finalize-mi-bundles < %s | \
; RUN:   FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -stop-before=haydn-finalize-mi-bundles < %s | \
; RUN:   FileCheck %s --check-prefix=HWON

; Role: semantic — multi-BB Role-A CFG extension (measured latch-only diamond).
; Product default OFF: no set_hwloop. HWON forms Role A on an innermost
; single-latch/single-exit diamond (never post-RA rediscovery). Multi-exit
; stays declined. Combined SMS+hwloop and default-ON stay out of scope.

target triple = "haydn-unknown-elf"

; Multi-BB body with stores in both arms — resists if-conversion.
; Innermost single-latch/single-exit diamond is the measured SCEV/CFG
; overlay of AIE's all-multi-BB decline. Product default stays OFF.
define void @multibb_side_effect_stores(ptr %dst, ptr readonly %src, i32 %n) nounwind {
; DEFAULT-LABEL: name: multibb_side_effect_stores
; DEFAULT-NOT:   SET_HWLOOP
;
; HWON-LABEL: name: multibb_side_effect_stores
; HWON:       SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %dp = phi ptr [ %dst, %entry ], [ %dp.next, %latch ]
  %v = load i32, ptr %sp
  %sign = icmp slt i32 %v, 0
  br i1 %sign, label %then, label %else

then:
  store i32 0, ptr %dp
  br label %latch

else:
  store i32 %v, ptr %dp
  br label %latch

latch:
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %dp.next = getelementptr inbounds i32, ptr %dp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

; Two-way branch with arithmetic in each arm. Latch-only overlay (or
; if-converted single-BB) may arm one Role-A SET under HWON. DEFAULT stays
; soft. Never two SETs.
define i32 @multibb_arith_arms(ptr readonly %src, i32 %n, i32 %k) nounwind {
; DEFAULT-LABEL: name: multibb_arith_arms
; DEFAULT-NOT:   SET_HWLOOP
;
; HWON-LABEL: name: multibb_arith_arms
; HWON:       SET_HWLOOP
; HWON-NOT:   SET_HWLOOP{{.*}}SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %latch ]
  %v = load i32, ptr %sp
  %sign = icmp slt i32 %v, 0
  br i1 %sign, label %then, label %else

then:
  %neg = sub i32 0, %v
  %scaled_then = mul i32 %neg, %k
  br label %latch

else:
  %scaled_else = mul i32 %v, %k
  br label %latch

latch:
  %scaled = phi i32 [ %scaled_then, %then ], [ %scaled_else, %else ]
  %acc.next = add i32 %acc, %scaled
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %acc.next
}

; Multi-exit stays declined (early exit != latch). Soft under HWON.
define i32 @multibb_early_exit(ptr readonly %src, i32 %n, i32 %k) nounwind {
; DEFAULT-LABEL: name: multibb_early_exit
; DEFAULT-NOT:   SET_HWLOOP
;
; HWON-LABEL: name: multibb_early_exit
; HWON-NOT:   SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %v = load i32, ptr %sp
  %hit = icmp eq i32 %v, %k
  br i1 %hit, label %early, label %latch

latch:
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

early:
  ret i32 %v

exit:
  ret i32 0
}
