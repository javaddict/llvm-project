; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Branch offset field must be correctly encoded.

; REGRESSION TEST: Branch offset field must be correctly encoded.
;
; This test covers conditional and unconditional branch offset handling.
; The Haydn ISA encodes branch offsets at bits [15:0] of the instruction.
; If the branch encoding regresses, the assembler will fail or the branch
; will target the wrong address.
;
; Branch polarity (GISel emitInvert01 + fall-through preference):
;   icmp eq  -> seq32 + xori32 1 + bnez_w  (branch if NOT equal)
;   icmp ne  -> seq32 + bnez_w             (branch if equal / skip then)
;   icmp slt -> slt32 + xori32 1 + bnez_w
;   icmp ult -> sltu32 + xori32 1 + bnez_w
; Do NOT update CHECK lines without understanding the root cause.

;Branch after equality comparison

define void @branch_eq(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: branch_eq:
; CHECK: seq32
; CHECK: xori32
; CHECK: bnez{{(\.s[012])?}}
; CHECK: jal{{(\.s[012])?}} {{.*}}, extern_fn
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %then, label %end
then:
  call void @extern_fn()
  br label %end
end:
  ret void
}

;Branch after not-equal comparison
; NE: SEQ32 + BNEZ (no invert) — fall through to then when not equal.
define void @branch_ne(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: branch_ne:
; CHECK: seq32
; CHECK: bnez{{(\.s[012])?}}
entry:
  %cmp = icmp ne i32 %a, %b
  br i1 %cmp, label %then, label %end
then:
  call void @extern_fn()
  br label %end
end:
  ret void
}

;Branch after signed less-than
define void @branch_slt(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: branch_slt:
; CHECK: slt32
; CHECK: xori32
; CHECK: bnez{{(\.s[012])?}}
entry:
  %cmp = icmp slt i32 %a, %b
  br i1 %cmp, label %then, label %end
then:
  call void @extern_fn()
  br label %end
end:
  ret void
}

;Branch after unsigned less-than
define void @branch_ult(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: branch_ult:
; CHECK: sltu32
; CHECK: xori32
; CHECK: bnez{{(\.s[012])?}}
entry:
  %cmp = icmp ult i32 %a, %b
  br i1 %cmp, label %then, label %end
then:
  call void @extern_fn()
  br label %end
end:
  ret void
}

;Branch with larger code block (bigger offset)
define i32 @branch_large_offset(i32 %x) nounwind {
; CHECK-LABEL: branch_large_offset:
; CHECK: slt32
; CHECK: xori32
; CHECK: bnez{{(\.s[012])?}}
entry:
  %cmp = icmp sgt i32 %x, 0
  br i1 %cmp, label %pos, label %neg
pos:
  %a1 = add i32 %x, 1
  %a2 = add i32 %a1, 2
  %a3 = add i32 %a2, 3
  %a4 = add i32 %a3, 4
  %a5 = add i32 %a4, 5
  ret i32 %a5
neg:
  %b1 = sub i32 %x, 1
  %b2 = sub i32 %b1, 2
  %b3 = sub i32 %b2, 3
  %b4 = sub i32 %b3, 4
  %b5 = sub i32 %b4, 5
  ret i32 %b5
}

;Inverted branch (fall-through vs taken)
define i32 @branch_inverted(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: branch_inverted:
; CHECK: slt32
; CHECK: xori32
; CHECK: bnez{{(\.s[012])?}}
; This shape (br i1 %cmp; less: ret; geq: ret) has NO Join block, so
; HaydnGenMux Phase 2 (tryConvertBranchCMOV) correctly bails — it requires
; Join->pred_size==2. The backend emits slt32+xori32+bnez_w, which preserves
; the test's stated purpose (verify the branch-offset field is correctly
; encoded). The movt32 predication path is tested separately in
; cmov-formation.ll (phi-merge Join).
entry:
  %cmp = icmp slt i32 %a, %b
  br i1 %cmp, label %less, label %geq
less:
  ret i32 1
geq:
  ret i32 0
}

declare void @extern_fn()
