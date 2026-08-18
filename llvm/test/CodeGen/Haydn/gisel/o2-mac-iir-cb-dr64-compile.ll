; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:     -stop-after=prologepilog -verify-machineinstrs -o - %s | FileCheck %s
;
; Role: compile — default -O2 (pre-RA MachineScheduler + greedy ON) of a
; mixed MAC / IIR / circular-buffer / DR64 body must terminate through
; PEI. These families historically hung in pre-RA list / greedy. Emit
; CompletionState is a later owner; this pin is finish, not bundle QUALIFY.
;
; CHECK-LABEL: name: mac_dr64
; CHECK-LABEL: name: iir_df1
; CHECK-LABEL: name: cb_dr64

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)
declare ptr @llvm.haydn.sdw.cb.imm(i64, ptr, i32, i32)

define i64 @mac_dr64(i64 %acc, i64 %a, i64 %b) nounwind {
  %va = bitcast i64 %a to <2 x i32>
  %vb = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %va, <2 x i32> %vb)
  ret i64 %r
}

define i32 @iir_df1(i32 %b0, i32 %xn, i32 %b1, i32 %x1, i32 %a1, i32 %y1) nounwind {
  %p0 = mul i32 %b0, %xn
  %p1 = mul i32 %b1, %x1
  %s = add i32 %p0, %p1
  %fb = mul i32 %a1, %y1
  %y = sub i32 %s, %fb
  ret i32 %y
}

define i64 @cb_dr64(ptr %p, i64 %v) nounwind {
  %ld = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %p, i32 0, i32 1)
  %x = extractvalue { i64, ptr } %ld, 0
  %np = extractvalue { i64, ptr } %ld, 1
  %s = add i64 %x, %v
  %q = call ptr @llvm.haydn.sdw.cb.imm(i64 %s, ptr %np, i32 0, i32 1)
  ret i64 %s
}
