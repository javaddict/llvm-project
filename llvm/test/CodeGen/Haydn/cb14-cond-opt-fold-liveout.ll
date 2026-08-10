; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s

; Role: semantic — — HaydnConditionOptimizer::foldCmpBranch must NOT fold a CMP+branch when the CMP destination register is live-out of the.

; REGRESSION TEST: — HaydnConditionOptimizer::foldCmpBranch must NOT
; fold a CMP+branch when the CMP destination register is live-out of the
; basic block (read by a successor block).
;
; Bug: foldCmpBranch verified deadness only with isRegDeadAfter, which scanned
; for uses strictly WITHIN the same block. When the CMP destination was also
; read by a SUCCESSOR block (e.g. a "found" flag computed in a search and
; returned after the search), the fold would delete the defining CMP. The
; destination register then retained whatever stale value it held before the
; CMP, and the successor read that stale value — corrupting the result.
; In the original e2e_ds_bst_iter benchmark the iterative search's per-key
; found-flags are summed after the search loops; deleting the SEQ32 that
; produced each flag left the flag register holding a key-array address, so
; sum_found evaluated to 7 instead of 27.
;
; Test design: a 3-step linear search over a small global array, looking for
; the value 7 (which lives at index 1). Each step is its own block; each
; block's SEQ32 (eq_i = arr[i]==7) feeds BOTH a conditional branch AND the
; `found` phi in the return block. The LAST step's SEQ32 destination is
; live-out to the return block, so foldCmpBranch must decline the fold.
;
; PASS criterion: the SEQ32 in the last search step (// %step2) must survive
; in the output, and its destination register must reach the AND32 in the
; found block. Before the fix, foldCmpBranch collapsed that SEQ32+BNEZ into
; a bare BNE and deleted the SEQ32; the AND32 then read a stale register
; returning 0 even when the value was found.

@arr = global [4 x i32] [i32 3, i32 7, i32 1, i32 9]

define i32 @cb14_fold_liveout_seq() {
; CHECK-LABEL: cb14_fold_liveout_seq:
; All three search steps must emit a SEQ32 whose destination register is
; live-out to the found block. foldCmpBranch must NOT delete any of them.
; We assert that each step's block contains a seq32; if foldCmpBranch wrongly
; folded the live-out comparison, the corresponding seq32 would vanish and
; the AND32 in the found block would read a stale register.
; CHECK: // %bb.0:                               // %entry
; CHECK: seq32
; CHECK: // %bb.1:                               // %step1
; CHECK: seq32
; CHECK: // %bb.2:                               // %step2
; CHECK: seq32
; CHECK: // %found
; CHECK: {{and32|andi32}}
entry:
  %a0addr = getelementptr [4 x i32], ptr @arr, i32 0, i32 0
  %a0 = load i32, ptr %a0addr
  %eq0 = icmp eq i32 %a0, 7
  br i1 %eq0, label %found, label %step1

step1:
  %a1addr = getelementptr [4 x i32], ptr @arr, i32 0, i32 1
  %a1 = load i32, ptr %a1addr
  %eq1 = icmp eq i32 %a1, 7
  br i1 %eq1, label %found, label %step2

step2:
  %a2addr = getelementptr [4 x i32], ptr @arr, i32 0, i32 2
  %a2 = load i32, ptr %a2addr
  %eq2 = icmp eq i32 %a2, 7
  br i1 %eq2, label %found, label %notfound

found:
  %phi = phi i1 [ %eq0, %entry ], [ %eq1, %step1 ], [ %eq2, %step2 ]
  %zext = zext i1 %phi to i32
  ret i32 %zext

notfound:
  ret i32 0
}
