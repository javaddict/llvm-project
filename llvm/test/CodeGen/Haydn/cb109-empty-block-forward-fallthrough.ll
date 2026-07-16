; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; HaydnCFGOptimizer::forwardEmptyBlocks must not leave a predecessor
; with a fallthrough terminator when the empty block is erased and the
; forwarded target is not the post-erase layout successor.
;
; Shape (yarpgen seed 2288 reduced):
; pred: one-way cond to cold, fallthrough into empty
; empty: only `b target` (unconditional)
; other: sits between empty and target in layout (so after erase, pred
; would fall into other, not target)
;
; Without the fix, pred keeps a one-way BEQZ and CFG edge to target while
; layout-next becomes other → MachineBlockPlacement::updateTerminator asserts
; isSuccessor(PreviousLayoutSuccessor).

@g = external global i32
@h = external global i32

define i32 @cb109_forward_fallthrough(i32 %x) nounwind {
entry:
  %c = icmp eq i32 %x, 0
  br i1 %c, label %cold, label %empty

; Fallthrough predecessor of empty (layout: entry → empty when cold is
; placed later). Force cold far away via a side-effecting call.
empty:
  br label %target

other:
  store volatile i32 7, ptr @g
  br label %target

target:
  %v = load volatile i32, ptr @h
  ret i32 %v

cold:
  store volatile i32 9, ptr @g
  ; Keep cold large enough that branch-prob placement prefers other orders.
  store volatile i32 9, ptr @g
  store volatile i32 9, ptr @g
  br label %target
}

; CHECK-LABEL: cb109_forward_fallthrough:
; Must compile without updateTerminator assert and return.
; CHECK: jalr_w{{(\.s[012])?}}
