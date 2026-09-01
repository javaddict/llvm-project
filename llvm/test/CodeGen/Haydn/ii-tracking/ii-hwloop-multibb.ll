; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:   -haydn-enable-hwloops=0 < %s | FileCheck %s --check-prefix=HWOFF

; 2026-08-22 hwloop product-default flip rebaseline: default is now ON.

; Role: semantic — under the product default (ON since 2026-08-22) an
; innermost single-latch/single-exit diamond forms a Role-A hardware loop
; (measured expand pinned by hwloop-multibb.ll). HWOFF keeps the
; explicit-OFF software back-edge shape.

define void @ii_hwloop_multibb(ptr %dst, ptr readonly %src, i32 %n) {
; DEFAULT-LABEL: ii_hwloop_multibb:
; DEFAULT:       set_hwloop_f2 0,
; Software back-edge under OFF (form may be fused blt_w or slt+bnez).
; HWOFF-LABEL: ii_hwloop_multibb:
; HWOFF-NOT:   set_hwloop
; HWOFF:       {{blt|bnez|beqz}}
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
  br label %latch

else:
  br label %latch

latch:
  %out = phi i32 [ 0, %then ], [ %v, %else ]
  store i32 %out, ptr %dp
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %dp.next = getelementptr inbounds i32, ptr %dp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}
