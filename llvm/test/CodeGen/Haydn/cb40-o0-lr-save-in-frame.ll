; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s | FileCheck %s

; Role: semantic — — at -O0 the prologue must save LR (R15) at a frame-INTERNAL offset, NOT below the just-decremented SP.

; REGRESSION TEST: — at -O0 the prologue must save LR (R15) at a
; frame-INTERNAL offset, NOT below the just-decremented SP.
;
; Bug: emitPrologue addressed each CSR slot off the FrameReg returned by
; getFrameIndexReference. When hasFP is true (forced here by alloca), that
; returns FP-relative NEGATIVE offsets (e.g. -4), and (before the fix) the
; store base was SP — so the LR save became `st32 lr, sp, -4` AFTER
; `subi32 sp,sp,N`, i.e. 4 bytes BELOW the new SP, inside the callee's frame.
; The callee's first local then aliased and clobbered the saved LR, so on
; return JALR jumped to garbage -> crt0 re-entered main -> NOEXIT.
;
; Fix (the real layer): at prologue CSR-save time the frame pointer is NOT yet
; established (FP is materialized AFTER the CSR stores — the.s showed
; `st32 lr, fp, -4` preceding `addi32 fp, sp, N`), so CSR slots must be
; addressed SP-relative. Once FP exists, FP == SP + StackSize, hence
; [FP+off] == [SP+StackSize+off]; the CSR loops fold the stack size into the
; offset for the hasFP case and use SP (R13) as the base.
;
; Test design: caller uses alloca (forces hasFP=true) and is non-leaf (the
; call forces the R15/LR save). The surgical probe is CHECK-NOT on
; `st32 {{lr|r15}}, {{sp|r13}}, -<K>` — the exact below-SP store the bug
; emitted. If the bug regresses, that line reappears in the prologue.

declare void @use(ptr)

define i32 @caller(i32 %n) nounwind {
; CHECK-LABEL: caller:
; Flex cutover: subi32 -> subi32 (slot-suffixed Flex form).
; CHECK:       subi32{{.*}} sp, sp,
; LR must NOT be stored below the decremented SP (the bug signature).
; CHECK-NOT:   st32 {{lr|r15}}, {{sp|r13}}, -{{[0-9]+}}
; CHECK:       jal{{(\.s[012])?}}
entry:
  %v = alloca i32, i32 %n        ; forces hasFP = true
  call void @use(ptr %v)         ; non-leaf -> forces LR (R15) save
  %r = load i32, ptr %v
  ret i32 %r
}
