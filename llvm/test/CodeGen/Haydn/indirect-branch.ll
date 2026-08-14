; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s

; Role: semantic — G_BRINDIRECT (indirect branch / computed goto) must select to JALR, and G_BLOCK_ADDR (block address materialization) must select to.

; REGRESSION TEST: G_BRINDIRECT (indirect branch / computed goto) must select
; to JALR, and G_BLOCK_ADDR (block address materialization) must select to
; LOAD_ADDR (expanded to LUI + ADDI32 with HI20/LO16 fixups).
;
; Bug: computed goto (goto *ptr) failed with "cannot select: G_BRINDIRECT"
; because the instruction selector had no case for G_BRINDIRECT.
; Additionally, the blockaddress operator (&&label) failed with "cannot select:
; G_BLOCK_ADDR" because the selector had no case for it either.
;
; Fix: Added G_BRINDIRECT -> JALR R0, %src, 0 (R0 as dest discards return
; address, making it a pure jump). Added G_BLOCK_ADDR -> LOAD_ADDR pseudo
; with block address operand. Extended HaydnAsmPrinter to handle block
; addresses in LOAD_ADDR expansion (LUI + ADDI32 with HI20/LO16 fixups).
;
; If either selection regresses, llc will crash with "cannot select".

define void @computed_goto(i32 %idx) {
; CHECK-LABEL: computed_goto:
; Check that JALR is emitted for the indirect branch
; CHECK: jalr{{(\.s[012])?}}
entry:
  %target = alloca ptr
  store ptr blockaddress(@computed_goto, %label1), ptr %target
  %cmp = icmp eq i32 %idx, 1
  br i1 %cmp, label %setl2, label %dojump

setl2:
  store ptr blockaddress(@computed_goto, %label2), ptr %target
  br label %dojump

dojump:
  %addr = load ptr, ptr %target
  indirectbr ptr %addr, [label %label1, label %label2]

label1:
  ret void

label2:
  ret void
}

; Simpler test: direct computed goto with a single target.
define void @simple_indirect_br(ptr %target) {
; CHECK-LABEL: simple_indirect_br:
; CHECK: jalr{{(\.s[012])?}}
entry:
  indirectbr ptr %target, [label %dst]

dst:
  ret void
}
