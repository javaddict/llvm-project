; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; — land 0/-1 + add + store must stay correct without
; GenMux Pattern 2. EarlyIfConv may or may not convert (side blocks that
; load are not speculated); either a movt/movf or a branch+move of -1 is
; fine. What must not happen: load through the land register.

@a = external global i16
@out = external global i16
@flag = external global i32
@alt = external global i16
@ptr = external global ptr

; CHECK-LABEL: land_add_store:
define void @land_add_store() nounwind {
entry:
  %a0 = load i16, ptr @a
  %not = xor i16 %a0, -1
  %f = load i32, ptr @flag
  %tobool = icmp eq i32 %f, 0
  br i1 %tobool, label %cond.false, label %cond.true

cond.true:
  %v = load i16, ptr @alt
  %cmp = icmp eq i16 %v, 0
  br i1 %cmp, label %land.end, label %land.rhs

cond.false:
  %p = load ptr, ptr @ptr
  %pv = load i16, ptr %p
  %cmp2 = icmp eq i16 %pv, 0
  br i1 %cmp2, label %land.end, label %land.rhs

land.rhs:
  br label %land.end

land.end:
  %land = phi i16 [ 0, %cond.false ], [ 0, %cond.true ], [ -1, %land.rhs ]
  %sum = add i16 %land, %not
  store i16 %sum, ptr @out
  ret void
}

; Result is always land + ~a stored — integer path, not pointer.
; CHECK: add32
; CHECK: st16
