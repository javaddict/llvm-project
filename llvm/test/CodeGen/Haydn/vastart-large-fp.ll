; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -frame-pointer=all < %s | FileCheck %s --check-prefix=FP
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s | FileCheck %s --check-prefix=OMIT

; Role: semantic — Large locals + va_start + frame pointer must compile (no hard-fatal on far FI).

; Large locals + va_start + frame pointer must compile (no hard-fatal on far FI).
; VASTART uses withPostRAScratch: free GPR first; spill only if none free
; (PostRAScratchFI is spill *home* only when scavenge finds no free GPR).
;
; Minimal C shape: volatile char pad[1024]; va_start; va_arg; under
; fno-omit-frame-pointer.

define i32 @sum_large_fp(i32 %n, ...) nounwind {
; FP-LABEL: sum_large_fp:
; Va_list field stores must appear (5-field init).
; FP: st32 {{r[0-9]+}}, {{r[0-9]+}}, 0
; FP: st32 {{r[0-9]+}}, {{r[0-9]+}}, 4
; FP: jalr
;
; OMIT-FP still compiles.
; OMIT-LABEL: sum_large_fp:
; OMIT: st32 {{r[0-9]+}}, {{r[0-9]+}}, 0
; OMIT: jalr
entry:
  %pad = alloca [1024 x i8], align 1
  %ap = alloca i8, align 4
  %p0 = getelementptr inbounds [1024 x i8], ptr %pad, i32 0, i32 0
  store volatile i8 1, ptr %p0, align 1
  call void @llvm.va_start(ptr %ap)
  %a = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  %v = load volatile i8, ptr %p0, align 1
  %vz = zext i8 %v to i32
  %r = add i32 %a, %vz
  ret i32 %r
}

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
