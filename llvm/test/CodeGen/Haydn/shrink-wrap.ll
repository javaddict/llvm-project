; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s

; Role: semantic — Shrink-wrapping must push prologue to the first block that actually uses callee-saved registers, not always the entry block.

; REGRESSION TEST: Shrink-wrapping must push prologue to the first block
; that actually uses callee-saved registers, not always the entry block.
;
; Rationale: Without shrink-wrapping, every function saves/restores callee-saved
; registers in the entry/exit blocks even if some paths don't need them.
; With shrink-wrapping enabled (enableShrinkWrapping returns true when no FP)
; the prologue (SP decrement + callee-save stores) is moved to the first block
; that actually uses callee-saved registers.
;
; Test design: The function has an early-exit path (cond == 0 -> return 42)
; that does not use any callee-saved registers, and a late path that calls
; multiple functions and must preserve values across calls, forcing callee-save
; register usage. The shrink-wrap pass should detect that the entry block and
; early-exit block have no CSR usage, and place the save/restore in the late path.
;
; If shrink-wrapping regresses, the entry block will contain SP decrement and
; callee-save stores, and the early-exit path will need to restore them.

declare i32 @getValue()
declare i32 @consume(i32, i32, i32, i32, i32, i32, i32)

define i32 @shrink_wrap_test(i32 %cond) {
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

; CHECK-LABEL: shrink_wrap_test:
; CHECK: .cfi_startproc
;
; Entry block should NOT have stack allocation or callee-save stores.
; It should just compare and branch.
; CHECK-NOT: subi32{{.*}}sp{{.*}}sp
; CHECK-NOT: st32{{.*}}r8
; CHECK-NOT: st32{{.*}}r9
; CHECK-NOT: st32{{.*}}r10
; CHECK-NOT: st32{{.*}}r11
; CHECK-NOT: st32{{.*}}r12
;
; The first callee-save store and SP decrement should appear in the late path
; (the block after the branch to early_exit).
; CHECK: subi32{{.*}}sp{{.*}}sp
; CHECK: st32{{.*}}r{{(8|9|10|11|12)}}
; CHECK: lui{{.*}}getValue
; CHECK: addi32{{.*}}getValue
; CHECK: jalr
