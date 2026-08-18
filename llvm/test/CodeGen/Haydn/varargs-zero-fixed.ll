; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs < %s | FileCheck %s
;
; Role: semantic — zero-fixed-argument variadics must spill register va_args.
; define i32 @f(...) has no fixed args, so FirstUnallocated starts at bank 0
; and R1–R7 / D0–D3 must be saved into the varargs save areas.

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)

define i32 @zero_fixed_i32(...) {
; CHECK-LABEL: zero_fixed_i32:
; Full GPR bank spill (R1–R7) — zero fixed args leave FirstUnallocated=0.
; CHECK:       st32 r1,
; CHECK:       st32 r2,
; CHECK:       st32 r3,
; CHECK:       st32 r4,
; CHECK:       st32 r5,
; CHECK:       st32 r6,
; CHECK:       st32 r7,
; DR bank spill as well.
; CHECK:       st64 d0,
; CHECK:       st64 d1,
; CHECK:       st64 d2,
; CHECK:       st64 d3,
entry:
  %ap = alloca i8, i32 32, align 8
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  ret i32 %v
}

define i64 @zero_fixed_i64(...) {
; CHECK-LABEL: zero_fixed_i64:
; CHECK:       st32 r1,
; CHECK:       st64 d0,
entry:
  %ap = alloca i8, i32 32, align 8
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i64
  call void @llvm.va_end(ptr %ap)
  ret i64 %v
}
