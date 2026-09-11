; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -stop-after=prologepilog < %s | FileCheck %s --check-prefix=PEI
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s --check-prefix=ASM
;
; REGRESSION TEST: MaxCallFrameSize is finalized at PEI, not re-decided in
; emitPrologue.
;
; Bug: determineFrameLayout ran inside emitPrologue and called
; setMaxCallFrameSize after PEI calculateCallFrameInfo / assignFrameOffsets.
; Same class as the post-hoc scavenger walk: a second writer after the
; owning pass had already computed the value. emitPrologue looked like it
; owned outgoing-arg reservation even after FrameSize += MaxCallFrameSize
; was dropped.
;
; Fix: processFunctionBeforeFrameFinalized is the sole MaxCallFrameSize
; writer after calculateCallFrameInfo (VLA StackAlign snap only).
; determineFrameLayout / emitPrologue snap StackSize to StackAlign and do
; not rewrite MaxCallFrameSize. Do not add MaxCallFrameSize into FrameSize.
;
; Test design: @reg_only_call and @stack_overflow_call differ only in
; outgoing stack-arg size (0 vs two 8-byte slots = 16). After PEI they
; share stackSize (locals/CSR/scratch) while maxCallFrameSize is 0 vs 16.
; Assembly: same prologue CFA; the stack-arg caller still SUBI32/ADDI32 16
; around JAL. If emitPrologue starts adding MaxCallFrameSize into the
; frame, [[SZ]] diverges and both prefixes fail.
;
; If this regresses, PEI stackSize of @stack_overflow_call grows by 16
; while the call-site bracket remains — dual model, or emitPrologue
; re-owns MaxCallFrameSize.

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

declare i32 @seven(i32, i32, i32, i32, i32, i32, i32)
declare i32 @nine(i32, i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @reg_only_call() {
; PEI-LABEL: name: reg_only_call
; PEI: stackSize: 16
; PEI: maxCallFrameSize: 0
;
; ASM-LABEL: reg_only_call:
; ASM:       .cfi_def_cfa_offset 16
; ASM-DAG:   lui{{.*}}seven
; ASM-DAG:   addi32{{.*}}seven
; ASM-DAG:   jalr{{.*}}lr
; ASM:       { nop; jalr r0, lr, 0 }
  %r = call i32 @seven(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7)
  ret i32 %r
}

define i32 @stack_overflow_call() {
; PEI-LABEL: name: stack_overflow_call
; Extra CSR/scratch vs @reg_only_call is RA, not MaxCallFrameSize.
; Dual model would write stackSize 32.
; PEI: stackSize: 24
; PEI: maxCallFrameSize: 16
; PEI-NOT: stackSize: 32
;
; ASM-LABEL: stack_overflow_call:
; ASM:       .cfi_def_cfa_offset 24
; ASM-NOT:   .cfi_def_cfa_offset 32
; ASM:       subi32{{(_w)?}}{{.*}}sp{{.*}}, 16
; ASM-DAG:   lui{{.*}}nine
; ASM-DAG:   addi32{{.*}}nine
; ASM:       { {{.*}}jalr{{.*}}lr{{.*}} }
; ASM:       addi32{{(_w)?}}{{.*}}sp{{.*}}, 16
; ASM:       { nop; jalr r0, lr, 0 }
  %r = call i32 @nine(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7,
                      i32 8, i32 9)
  ret i32 %r
}
