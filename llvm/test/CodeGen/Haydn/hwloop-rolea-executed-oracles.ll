; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops=0 < %s | FileCheck %s --check-prefix=HWOFF

; 2026-08-22 hwloop product-default flip rebaseline: default is now ON.
; DEFAULT pins set_hwloop_f2 formation with START/END geometry; HWOFF is
; the explicit-OFF soft residual.

; Role: semantic — Role-A executed iteration/memory/value oracle kernels.
; DEFAULT (product, ON) set_hwloop_f2 formation with START/END geometry.
; Product selector is innermost 0; HWLR is programmed only through SET
; (never free CSR 0x20-0x25). Off1/Off2 reloc is PC+(uimm<<2) via START/END
; labels. Freestanding BundleSim guest_exit=0 under flag ON/OFF for the same
; shapes (value oracles; store-fill + sum proves memory then value with
; set_hwloop under the ON default; HWOFF covers the disabled shape).

target triple = "haydn-unknown-elf"

define i32 @oracle_sum_runtime(ptr readonly %p, i32 %n) nounwind {
; DEFAULT-LABEL: oracle_sum_runtime:
; Setup floor: SET then intervening size-bearing parcels before BEGIN.
; DEFAULT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DEFAULT-NEXT:  {{.*}}nop
; DEFAULT-NOT:   csrw
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; Inclusive END strictly after START labels in emission order.
; DEFAULT:       jalr
;
; HWOFF-LABEL: oracle_sum_runtime:
; HWOFF-NOT:   set_hwloop
; HWOFF:       bnez
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

define i32 @oracle_store_then_sum(ptr %p, i32 %n) nounwind {
; DEFAULT-LABEL: oracle_store_then_sum:
; Memory then value: store loop arms SET, then sum loop arms SET.
; DEFAULT:       set_hwloop_f2 0,
; DEFAULT-NOT:   csrw
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       set_hwloop_f2 0,
; DEFAULT-NOT:   csrw
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       jalr
;
; HWOFF-LABEL: oracle_store_then_sum:
; HWOFF-NOT:   set_hwloop
; HWOFF:       jalr
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %store.loop, label %sum.guard
store.loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %store.loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %val = add i32 %i, 1
  store i32 %val, ptr %ge, align 4
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %store.loop, label %sum.guard
sum.guard:
  %cmp1 = icmp sgt i32 %n, 0
  br i1 %cmp1, label %sum.loop, label %exit
sum.loop:
  %j = phi i32 [ 0, %sum.guard ], [ %j.next, %sum.loop ]
  %s = phi i32 [ 0, %sum.guard ], [ %s.next, %sum.loop ]
  %ge2 = getelementptr inbounds i32, ptr %p, i32 %j
  %v = load i32, ptr %ge2, align 4
  %s.next = add i32 %s, %v
  %j.next = add i32 %j, 1
  %c2 = icmp slt i32 %j.next, %n
  br i1 %c2, label %sum.loop, label %exit
exit:
  %r = phi i32 [ 0, %sum.guard ], [ %s.next, %sum.loop ]
  ret i32 %r
}

define i32 @oracle_countdown_sum(ptr readonly %p, i32 %n) nounwind {
; DEFAULT-LABEL: oracle_countdown_sum:
; DEFAULT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DEFAULT-NOT:   csrw
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       jalr
;
; HWOFF-LABEL: oracle_countdown_sum:
; HWOFF-NOT:   set_hwloop
; HWOFF:       jalr
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit
loop:
  %i = phi i32 [ %n, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %idx = add i32 %i, -1
  %ge = getelementptr inbounds i32, ptr %p, i32 %idx
  %v = load i32, ptr %ge, align 4
  %s.next = add i32 %s, %v
  %i.next = add i32 %i, -1
  %c = icmp ne i32 %i.next, 0
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  ret i32 %r
}

define i32 @oracle_imm8_sum(ptr readonly %p) nounwind {
; DEFAULT-LABEL: oracle_imm8_sum:
; Fixed trip: SET + START/END geometry.
; DEFAULT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DEFAULT-NOT:   csrw
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       jalr
;
; HWOFF-LABEL: oracle_imm8_sum:
; HWOFF-NOT:   set_hwloop
; HWOFF:       jalr
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %s.next = add i32 %s, %v
  %i.next = add i32 %i, 1
  %c = icmp ult i32 %i.next, 8
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %s.next
}

; Boundary fixed trip=2 (smallest constant that forms Role A).
define i32 @oracle_trip2_sum(ptr readonly %p) nounwind {
; DEFAULT-LABEL: oracle_trip2_sum:
; DEFAULT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DEFAULT-NOT:   csrw
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       jalr
;
; HWOFF-LABEL: oracle_trip2_sum:
; HWOFF-NOT:   set_hwloop
; HWOFF:       jalr
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %s.next = add i32 %s, %v
  %i.next = add i32 %i, 1
  %c = icmp ult i32 %i.next, 2
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %s.next
}

; Memory geometry: copy src→dst then sum dst (iteration + memory + value).
define i32 @oracle_copy_sum(ptr readonly %src, ptr %dst, i32 %n) nounwind {
; DEFAULT-LABEL: oracle_copy_sum:
; DEFAULT:       set_hwloop_f2 0,
; DEFAULT-NOT:   csrw
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       set_hwloop_f2 0,
; DEFAULT-NOT:   csrw
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       jalr
;
; HWOFF-LABEL: oracle_copy_sum:
; HWOFF-NOT:   set_hwloop
; HWOFF:       jalr
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %copy.loop, label %exit
copy.loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %copy.loop ]
  %sp = getelementptr inbounds i32, ptr %src, i32 %i
  %dp = getelementptr inbounds i32, ptr %dst, i32 %i
  %v = load i32, ptr %sp, align 4
  store i32 %v, ptr %dp, align 4
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %copy.loop, label %sum.guard
sum.guard:
  br label %sum.loop
sum.loop:
  %j = phi i32 [ 0, %sum.guard ], [ %j.next, %sum.loop ]
  %s = phi i32 [ 0, %sum.guard ], [ %s.next, %sum.loop ]
  %ge = getelementptr inbounds i32, ptr %dst, i32 %j
  %w = load i32, ptr %ge, align 4
  %s.next = add i32 %s, %w
  %j.next = add i32 %j, 1
  %c2 = icmp slt i32 %j.next, %n
  br i1 %c2, label %sum.loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.next, %sum.loop ]
  ret i32 %r
}

; Measured multi-BB Role-A extension: innermost diamond, latch is the
; unique exit. Stores in both arms resist if-conversion. DEFAULT arms SET
; only; never a free HWLR CSR. Product default is ON.
define void @oracle_multibb_latch_stores(ptr %dst, ptr readonly %src, i32 %n) nounwind {
; DEFAULT-LABEL: oracle_multibb_latch_stores:
; DEFAULT:       set_hwloop_f2 0,
; DEFAULT-NOT:   csrw
; DEFAULT:       jalr
;
; HWOFF-LABEL: oracle_multibb_latch_stores:
; HWOFF-NOT:   set_hwloop
; HWOFF:       jalr
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %header, label %exit
header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %dp = phi ptr [ %dst, %entry ], [ %dp.next, %latch ]
  %v = load i32, ptr %sp, align 4
  %neg = icmp slt i32 %v, 0
  br i1 %neg, label %then, label %else
then:
  store i32 0, ptr %dp, align 4
  br label %latch
else:
  store i32 %v, ptr %dp, align 4
  br label %latch
latch:
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %dp.next = getelementptr inbounds i32, ptr %dp, i32 1
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %header, label %exit
exit:
  ret void
}
