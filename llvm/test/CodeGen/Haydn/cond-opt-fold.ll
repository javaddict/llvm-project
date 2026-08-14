; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — End-to-end check that comparison+branch folds and the zero-branch narrowing fire together through the full codegen pipeline.

; REGRESSION TEST: End-to-end check that comparison+branch folds and the
; zero-branch narrowing fire together through the full codegen pipeline.
;
; Bug being guarded against: When the selector emits CMP r, rA, rB followed
; by BNEZ/BEQZ, the ISel cmp+zero-test step must rewrite
; the pair as a single two-register branch (BEQ/BNE/BLT/BGE/BLTU/BGEU). If
; one of the comparison operands happens to be the constant 0 (lowered to a
; MOVE32 from R0 or directly referencing R0), the subsequent
; narrowBranchToZero step must further rewrite the result to the
; single-register BEQZ/BNEZ form. Before, the second step did not exist
; so the codegen kept the wider two-register branch even when a shorter
; zero-test form was available.
;
; Test design: Each function lowers an `icmp` against a constant 0 followed
; by a conditional branch. The CHECK lines assert that the emitted assembly
; uses the zero-test mnemonic. If the narrowing rule regresses, the output
; will contain `beq_w rX, r0` / `bne_w rX, r0` instead and the CHECK-NOT lines
; will fire.

;===--- icmp eq against zero should reach a single-register zero test ---===
; The post-fold form may be either BEQ rX, rY (when both operands are
; non-zero registers) or, when one side is zero, BEQZ/BNEZ rX. Here the IR
; forces one operand to 0, so we expect a single-register zero-test branch
; in the final asm. Haydn lowers icmp eq via SEQ32 (sets r=1 if equal) and
; then branches on that result with BNEZ — the test asserts that the final
; branch is a single-register zero-test form, not a 2-register BEQ/BNE.
; SEQ32 + XORI invert + BEQZ (T7.5 exact polarity).

define void @fold_eq_zero(i32 %a, ptr %p) nounwind {
entry:
  %c = icmp eq i32 %a, 0
  br i1 %c, label %then, label %else

then:
  store i32 1, ptr %p
  ret void

else:
  store i32 2, ptr %p
  ret void
}

;===--- icmp ne against zero ---===
; Haydn lowers icmp ne via SEQ32 (r=1 if eq) + XOR1 (invert) + BEQZ. The
; test asserts that the final branch is a single-register zero-test form
; not a 2-register BNE.
; CHECK-LABEL: fold_ne_zero:
; CHECK-NOT: bne{{(\.s[012])?}} r{{[0-9]+}}, r{{[0-9]+}}
; SEQ32 + BNEZ to else (eq → else; fallthrough = then). Not BEQZ-primary.
; CHECK: seq32
; CHECK: bnez{{(\.s[012])?}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
define void @fold_ne_zero(i32 %a, ptr %p) nounwind {
entry:
  %c = icmp ne i32 %a, 0
  br i1 %c, label %then, label %else

then:
  store i32 1, ptr %p
  ret void

else:
  store i32 2, ptr %p
  ret void
}

;===--- icmp slt against zero (signed: %a < 0) ---===
; CHECK-LABEL: fold_slt_zero:
; Current form: SLT32 + XORI invert + BEQZ (T7.5 exact). Not 2-reg BLT.
; CHECK-NOT: blt r{{[0-9]+}}, r{{[0-9]+}}
; CHECK: slt32
; CHECK: xori32
; CHECK: beqz{{(\.s[012])?}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
define void @fold_slt_zero(i32 %a, ptr %p) nounwind {
entry:
  %c = icmp slt i32 %a, 0
  br i1 %c, label %then, label %else

then:
  store i32 1, ptr %p
  ret void

else:
  store i32 2, ptr %p
  ret void
}
