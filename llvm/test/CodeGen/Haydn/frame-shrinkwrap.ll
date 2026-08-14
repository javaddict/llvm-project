; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — CFI for SP adjustment must co-locate with the SP decrement (emitPrologue).

; REGRESSION: CFI for SP adjustment must co-locate with the SP decrement
; (emitPrologue). Shrink-wrapping may still place the prologue in the entry
; block for this shape (M7 opt not required for correctness); the invariant
; is that.cfi_def_cfa_offset is never orphaned from subi32 sp.
;
; Fast early-exit + late path with CSR-heavy calls.

declare i32 @getValue()
declare i32 @consume(i32, i32, i32, i32, i32, i32, i32)

define i32 @shrink_wrap_cfi_test(i32 %cond) {
; CHECK-LABEL: shrink_wrap_cfi_test:
; CHECK: .cfi_startproc
; CHECK: subi32{{.*}}sp
; CHECK: .cfi_def_cfa_offset
; CHECK: jal{{.*}}getValue
; CHECK: jalr{{(\.s[012])?}}
entry:
  %cmp = icmp eq i32 %cond, 0
  br i1 %cmp, label %early_exit, label %late_path

early_exit:
  ret i32 42

late_path:
  %v1 = call i32 @getValue()
  %v2 = call i32 @getValue()
  %v3 = call i32 @getValue()
  %v4 = call i32 @getValue()
  %v5 = call i32 @getValue()
  %r1 = call i32 @consume(i32 %v1, i32 %v2, i32 %v3, i32 %v4, i32 %v5, i32 0, i32 0)
  ret i32 %r1
}
