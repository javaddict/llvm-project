; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — (GAP-4 multibb): a multi-BB loop must NOT be lowered to a hardware loop (ZOL) via the IR-level pass.

; REGRESSION TEST (GAP-4 multibb): a multi-BB loop must NOT be lowered to a
; hardware loop (ZOL) via the IR-level pass.
;
; the AsmPrinter's LoopStart handler assumes the ZOL body is a
; single basic block — it registers the HWLR_END label on `LoopBody` and emits
; it at that block's last real instruction. For a multi-BB loop `LoopBody`
; resolves to the header, whose only instruction is a terminator, so the END
; label is never emitted -> llvm-mc "Undefined temporary symbol.LLhwloop_end0"
; build abort. So isHardwareLoopProfitable now rejects any multi-BB loop
; (HaydnTargetTransformInfo.cpp); this loop correctly stays a SOFTWARE loop
; (compare-and-branch back-edge). The post-RA recognizer may still form a
; hwloop for multi-BB loops it can correctly bound (single latch, placeable
; END); this clamp shape is not one of them.
;
; Asserting NO set_hwloop here guards the fix: if a future change re-opens
; the multi-BB IR-ZOL path, this test fails (and the build would abort).

define void @ii_hwloop_multibb(ptr %dst, ptr readonly %src, i32 %n) {
; CHECK-LABEL: ii_hwloop_multibb:
; CHECK-NOT:   set_hwloop
; Software back-edge (form may be fused blt_w or slt+bnez — either is fine).
; CHECK:       {{blt|bnez|beqz}}
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %dp = phi ptr [ %dst, %entry ], [ %dp.next, %latch ]
  %v = load i32, ptr %sp
  %sign = icmp slt i32 %v, 0
  br i1 %sign, label %then, label %else

then:
  br label %latch

else:
  br label %latch

latch:
  %out = phi i32 [ 0, %then ], [ %v, %else ]
  store i32 %out, ptr %dp
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %dp.next = getelementptr inbounds i32, ptr %dp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}
