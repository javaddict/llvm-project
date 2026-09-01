; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops=0 < %s | FileCheck %s --check-prefix=HWOFF
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -filetype=obj -o %t.o < %s

; 2026-08-22 hwloop product-default flip rebaseline: default is now ON.

; Role: semantic — product programs HWLR only through SET_HWLOOP with a
; product selector. Free CSR invent for HWLR_BEGIN/END/COUNT is unavailable.
; DEFAULT (ON since 2026-08-22) arms set_hwloop_f2 but must never emit
; free CSR writes (csrw) to program loop state; HWOFF stays soft.
; filetype=obj proves the supported Off1/Off2 reloc encodes on the
; matching artifact.

target triple = "haydn-unknown-elf"

define i32 @csr_setonly_sum(ptr readonly %p, i32 %n) nounwind {
; DEFAULT-LABEL: csr_setonly_sum:
; DEFAULT:       set_hwloop_f2 0,
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       jalr
;
; HWOFF-LABEL: csr_setonly_sum:
; HWOFF-NOT:   set_hwloop
; HWOFF-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; HWOFF:       jalr
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %s.next = add i32 %s, %v
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  ret i32 %r
}
