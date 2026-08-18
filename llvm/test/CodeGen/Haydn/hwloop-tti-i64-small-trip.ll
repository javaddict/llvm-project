; RUN: opt -passes=hardware-loops -mtriple=haydn-unknown-elf \
; RUN:   -stats -S -o /dev/null < %s 2>&1 | FileCheck %s --check-prefix=STATS
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s --check-prefix=DEFAULT
; REQUIRES: asserts
; REQUIRES: haydn-registered-target

; REGRESSION TEST: TTI hwloop trip gate must use the HWLR_COUNT *value* bound.
;
; Bug: HaydnTTI::isHardwareLoopProfitable rejected any trip whose SCEV
; APInt bit width was > 32. That is the SCEV *type* width, not the value.
; An i64 induction with a constant trip of 100 was declined even though
; 100 fits in golden 32-bit HWLR_COUNT. A trip whose unsigned range max
; may exceed 0xFFFFFFFF must still be declined.
;
; Fix: reject only when getUnsignedRangeMax(TripCountSCEV).ugt(0xFFFFFFFF).
; If the type-width proxy returns, this i64-trip-100 kernel is declined at
; TTI (zero accepted stats) even though the value is legal.
;
; Test design: TTI acceptance is the contract (NumHWLoopAccepted == 1).
; Generic HardwareLoops may still skip converting an i64 ExitCount into an
; i32 CountType; that is a later-layer residual, not this TTI value gate.
; DEFAULT pins that this file does not flip the product hwloop default.

; STATS: 1 haydn-tti {{.*}}Role-A hardware-loop candidates accepted

; i64 IV, constant trip 100 — previously declined for type width.
define i32 @i64_small_const_trip(ptr %p) {
; DEFAULT-LABEL: i64_small_const_trip:
; DEFAULT-NOT: set_hwloop
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %v = load i32, ptr %p, align 4
  %s.next = add i32 %s, %v
  %i.next = add nuw i64 %i, 1
  %c = icmp ult i64 %i.next, 100
  br i1 %c, label %loop, label %exit

exit:
  ret i32 %s.next
}

; i64 IV, constant trip 2^32 — unsigned max exceeds HWLR_COUNT; stay declined.
define i32 @i64_overflow_const_trip(ptr %p) {
; DEFAULT-LABEL: i64_overflow_const_trip:
; DEFAULT-NOT: set_hwloop
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %v = load i32, ptr %p, align 4
  %s.next = add i32 %s, %v
  %i.next = add nuw i64 %i, 1
  %c = icmp ult i64 %i.next, 4294967296
  br i1 %c, label %loop, label %exit

exit:
  ret i32 %s.next
}
