; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops < %s | FileCheck %s --check-prefix=HWON

; Role: semantic — Role-A executed iteration/memory/value oracle kernels.
; DEFAULT OFF soft residual; HWON set_hwloop_f2 formation with START/END geometry.
; Freestanding BundleSim guest_exit=0 under flag ON/OFF for the same shapes
; (value oracles; store-fill + sum proves memory then value under HWON with
; set_hwloop only when enabled). Final CSR/reloc geometry remains open.

target triple = "haydn-unknown-elf"

define i32 @oracle_sum_runtime(ptr readonly %p, i32 %n) nounwind {
; DEFAULT-LABEL: oracle_sum_runtime:
; DEFAULT-NOT:   set_hwloop
; DEFAULT:       bnez
; DEFAULT:       jalr
;
; HWON-LABEL: oracle_sum_runtime:
; Setup floor: SET then intervening size-bearing parcels before BEGIN.
; HWON:       set_hwloop_f2 1, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON-NEXT:  {{.*}}nop
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; Inclusive END strictly after START labels in emission order.
; HWON:       jalr
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
; DEFAULT-NOT:   set_hwloop
; DEFAULT:       jalr
;
; HWON-LABEL: oracle_store_then_sum:
; Memory then value: store loop arms SET, then sum loop arms SET.
; HWON:       set_hwloop_f2
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       set_hwloop_f2
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
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
; DEFAULT-NOT:   set_hwloop
; DEFAULT:       jalr
;
; HWON-LABEL: oracle_countdown_sum:
; HWON:       set_hwloop_f2 1, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
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
; DEFAULT-NOT:   set_hwloop
; DEFAULT:       jalr
;
; HWON-LABEL: oracle_imm8_sum:
; Fixed trip: SET + START/END geometry.
; HWON:       set_hwloop_f2 1, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
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

; Boundary fixed trip=2 (smallest constant that forms Role A under HWON).
define i32 @oracle_trip2_sum(ptr readonly %p) nounwind {
; DEFAULT-LABEL: oracle_trip2_sum:
; DEFAULT-NOT:   set_hwloop
; DEFAULT:       jalr
;
; HWON-LABEL: oracle_trip2_sum:
; HWON:       set_hwloop_f2 1, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
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
; DEFAULT-NOT:   set_hwloop
; DEFAULT:       jalr
;
; HWON-LABEL: oracle_copy_sum:
; HWON:       set_hwloop_f2
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       set_hwloop_f2
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
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
