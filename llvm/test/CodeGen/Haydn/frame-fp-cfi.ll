; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:   --force-dwarf-frame-section -filetype=obj -o - < %s \
; RUN:   | llvm-dwarfdump -debug-frame - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=FRAME
;
; REGRESSION TEST: FP-frame CFA is FP+0, not FP+StackSize.
;
; Bug: emitPrologue set FP = SP + StackSize (incoming SP) then emitted
; DW_CFA_def_cfa_register(FP) after .cfi_def_cfa_offset StackSize.
; DW_CFA_def_cfa_register keeps the prior offset, so CFA became
; FP + StackSize = incoming_SP + StackSize — wrong by the whole frame.
; Every CFA-relative .cfi_offset in an FP frame then resolved to garbage.
; frame-alloca.ll and unwind-frame-chain.ll pinned the broken assembly.
;
; Correct DWARF (RISCV/LoongArch shape): after FP equals incoming SP,
; emit DW_CFA_def_cfa(FP, 0) so CFA = FP + 0 = incoming_SP.
; .cfi_offset stays incoming-SP-relative (getObjectOffset, negative).
;
; If this regresses to .cfi_def_cfa_register fp, unwinders compute CFA
; StackSize bytes past the incoming SP.

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

declare void @use(i32)

; VLA forces hasFP. Prologue: subi sp,N; save fp; fp = sp+N; CFA = fp+0.
define i32 @fp_vla_cfa(i32 %n) {
; ASM-LABEL: fp_vla_cfa:
; ASM:       .cfi_def_cfa_offset
; ASM:       addi32{{(_w)?}} fp, sp,
; ASM-NEXT:  .cfi_def_cfa {{fp|r14}}, 0
; ASM-NOT:   .cfi_def_cfa_register
; ASM:       .cfi_offset {{fp|r14}}, -
; ASM:       .cfi_def_cfa {{sp|r13}}, 0
entry:
  %a = alloca i32, i32 %n
  store i32 1, ptr %a
  %v = load i32, ptr %a
  ret i32 %v
}

; frame-pointer=all with a call: FP+LR saves, still CFA = fp+0.
define i32 @fp_call_cfa(i32 %x) "frame-pointer"="all" {
; ASM-LABEL: fp_call_cfa:
; ASM:       .cfi_def_cfa_offset
; ASM:       addi32{{(_w)?}} fp, sp,
; ASM-NEXT:  .cfi_def_cfa {{fp|r14}}, 0
; ASM-NOT:   .cfi_def_cfa_register
; ASM:       .cfi_offset {{fp|lr|r14|r15}}, -
; ASM:       jal
; ASM:       .cfi_def_cfa {{sp|r13}}, 0
entry:
  call void @use(i32 %x)
  ret i32 %x
}

; Object-file FDE must encode DW_CFA_def_cfa R14+0, not def_cfa_register.
; FRAME: DW_CFA_def_cfa: R14 +0
; FRAME-NOT: DW_CFA_def_cfa_register
