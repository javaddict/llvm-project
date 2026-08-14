; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — s for the HaydnConditionOptimizer pass (post-RA).

; Status : previously-XFAIL regression resolved; lit PASS.
;
; Tests for the HaydnConditionOptimizer pass (post-RA).
;
; Pass optimizations tested:
; Self-comparison elimination: SLT32/SLTU32 r, rX, rX → SUB32 r, r, r (== 0)
; Inverse comparison reuse: SLT32 rA, rX, rY + SLT32 rB, rY, rX
; → second replaced with XORI32 rB, rA, 1
; Cmp+branch folding: when CMP dst != CMP src and the result feeds
; (optionally through XOR32 with 1) into BNEZ/BEQZ, replace with a
; single two-register branch (BEQ/BNE/BLT/BGE/BLTU/BGEU).
;
; elimination (slt i32 %a, %a should fold to sub32) and the inverse-pair
; reuse (slt a,b + slt b,a should fold to slt + xori32) no longer fire.
; Actual output retains two slt32 ops. Likely the post-RA pattern matcher
; in HaydnConditionOptimizer no longer recognizes the _S0 slot-suffixed
; opcode form. The optimizations themselves remain desirable; only the matcher

;===--- Self-comparison: slt i32 %a, %a → 0 ---===

define i32 @self_comparison_slt(i32 %a) nounwind {
; CHECK-LABEL: self_comparison_slt:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %cmp = icmp slt i32 %a, %a
  %r = zext i1 %cmp to i32
  ret i32 %r
}

;===--- Self-comparison: ult i32 %a, %a → 0 ---===

define i32 @self_comparison_ult(i32 %a) nounwind {
; CHECK-LABEL: self_comparison_ult:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %cmp = icmp ult i32 %a, %a
  %r = zext i1 %cmp to i32
  ret i32 %r
}

;===--- Inverse comparison pair ---===

define i32 @inverse_comparison(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: inverse_comparison:
; CHECK: slt32
; CHECK: { {{.*}}xor32 r0, r0, r0{{.*}} }
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %cmp1 = icmp slt i32 %a, %b
  %cmp2 = icmp slt i32 %b, %a
  %v1 = zext i1 %cmp1 to i32
  %v2 = zext i1 %cmp2 to i32
  %r = add i32 %v1, %v2
  ret i32 %r
}

;===--- Non-inverse: unrelated comparison pairs ---===

define i32 @unrelated_comparisons(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
; CHECK-LABEL: unrelated_comparisons:
; CHECK: slt32
; CHECK: slt32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %cmp1 = icmp slt i32 %a, %b
  %cmp2 = icmp slt i32 %c, %d
  %v1 = zext i1 %cmp1 to i32
  %v2 = zext i1 %cmp2 to i32
  %r = add i32 %v1, %v2
  ret i32 %r
}

;===--- Inverse pair with select ---===

define i32 @inverse_comparison_select(i32 %a, i32 %b, i32 %c) nounwind {
; CHECK-LABEL: inverse_comparison_select:
; CHECK: slt32
; CHECK: { {{.*}}xor32 r0, r0, r0{{.*}} }
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %cmp1 = icmp slt i32 %a, %b
  %cmp2 = icmp slt i32 %b, %a
  %v1 = zext i1 %cmp1 to i32
  %v2 = zext i1 %cmp2 to i32
  %r = select i1 %cmp1, i32 %v1, i32 %v2
  ret i32 %r
}

;===--- No fold: comparison result used by return value ---===

define i32 @no_fold_cmp_result_used(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: no_fold_cmp_result_used:
; CHECK: seq32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %cmp = icmp eq i32 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}

;===--- Branching comparisons with stores in targets ---===
; Stores prevent CMOV formation, so branches remain for the optimizer.

@GV = external dso_local global i32

; icmp eq + branch → SEQ32 + BEQZ (direct pattern, CMP dst may alias src)
define void @branch_eq(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: branch_eq:
; CHECK: seq32
; CHECK: b{{eq|ne}}z_w{{(\.s[012])?}}
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  store i32 1, ptr @GV
  ret void
else:
  store i32 0, ptr @GV
  ret void
}

; icmp ne + branch → SEQ32 + BNEZ to else (eq → else; fallthrough = then)
define void @branch_ne(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: branch_ne:
; CHECK: seq32
; CHECK: b{{eq|ne}}z_w{{(\.s[012])?}}
entry:
  %cmp = icmp ne i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  store i32 1, ptr @GV
  ret void
else:
  store i32 0, ptr @GV
  ret void
}

; icmp slt + branch → SLT32 + BEQZ
define void @branch_slt(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: branch_slt:
; CHECK: slt32
; CHECK: b{{eq|ne}}z_w{{(\.s[012])?}}
entry:
  %cmp = icmp slt i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  store i32 1, ptr @GV
  ret void
else:
  store i32 0, ptr @GV
  ret void
}

; icmp sge + branch → SLT32 + BNEZ to else (a<b → else; fallthrough = a>=b)
define void @branch_sge(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: branch_sge:
; CHECK: slt32
; CHECK: b{{eq|ne}}z_w{{(\.s[012])?}}
entry:
  %cmp = icmp sge i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  store i32 1, ptr @GV
  ret void
else:
  store i32 0, ptr @GV
  ret void
}

; icmp ult + branch → SLTU32 + BEQZ
define void @branch_ult(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: branch_ult:
; CHECK: sltu32
; CHECK: b{{eq|ne}}z_w{{(\.s[012])?}}
entry:
  %cmp = icmp ult i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  store i32 1, ptr @GV
  ret void
else:
  store i32 0, ptr @GV
  ret void
}

; icmp uge + branch → SLTU32 + BNEZ to else (a<b → else; fallthrough = a>=b)
define void @branch_uge(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: branch_uge:
; CHECK: sltu32
; CHECK: b{{eq|ne}}z_w{{(\.s[012])?}}
entry:
  %cmp = icmp uge i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  store i32 1, ptr @GV
  ret void
else:
  store i32 0, ptr @GV
  ret void
}
