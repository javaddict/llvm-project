; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s

; Role: semantic — frame-destroy SP restore is a post-RA scheduling barrier.

; REGRESSION TEST: frame-setup / frame-destroy instructions (which modify the
; stack pointer R13) MUST be post-RA scheduling barriers.
;
; Bug: HaydnInstrInfo::isSchedulingBoundary overrode the base
; TargetInstrInfo predicate and dropped the inherited
; modifiesRegister(getStackPointerRegisterToSaveRestore) rule. The post-RA
; scheduler then treated the epilogue `ADDI32 $sp, <N>` as an ordinary ALU
; op — dependency-independent of the surrounding callee-save stores — and
; co-issued it with prologue stores, restoring SP MID-FUNCTION. Every
; subsequent sp-relative address then resolved against the caller's SP, not
; the callee frame. For variadic functions this broke the VASTART va_list
; field materializations (`addi32 rN, sp, <off>`), which read garbage past
; the frame, so va_arg returned 0 at -O1/-O2.
;
; Fix: isSchedulingBoundary returns true for any MI carrying FrameSetup or
; FrameDestroy, pinning the SP-modifying prologue/epilogue at the block edges.
;
; Test design: a variadic function whose va_arg feeds an external sink (so the
; optimizer cannot constant-fold it away) forces a real VASTART expansion and
; sp-relative va_list field stores. The epilogue SP-restore must NOT share a
; bundle with a prologue callee-save store. If the bug regresses, the frame
; destroy `addi32{{(_w)?}}... sp, sp` is co-issued with a `st32` spill and the

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)
declare void @sink(i32)

define void @varargs_use(i32 %n, ...) nounwind {
; CHECK-LABEL: varargs_use:
; Prologue SP adjust + LR spill, then a real call so the epilogue cannot fold
; away. Extra addi32 / save-store swap is legal; the contract is that
; frame-destroy SP restore is not co-issued with a store.
; CHECK: subi32 sp, sp, 72
; CHECK: st32 lr, sp,
; CHECK: jal lr, sink
; CHECK: ld32 lr, sp,
; CHECK-NOT: { {{.*}}st32{{.*}}addi32{{(_w)?}}{{.*}}sp, sp
; CHECK-NOT: { {{.*}}addi32{{(_w)?}}{{.*}}sp, sp{{.*}}st32
; CHECK: { {{.*}}addi32{{(_w)?}}{{.*}}sp, sp, 72 }
; CHECK: jalr r0, lr, 0
  %ap = alloca i8, align 8
  call void @llvm.va_start.p0(ptr %ap)
  %v = va_arg ptr %ap, i32
  call void @sink(i32 %v)
  call void @llvm.va_end.p0(ptr %ap)
  ret void
}
; The epilogue SP-restore must be present...
; and must NOT be co-issued (same bundle line) with a prologue spill store.
