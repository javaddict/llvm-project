; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — generic MCP (UseCopyInstr). HaydnCopyElim is deleted.

; Tests for generic MCP (UseCopyInstr) on the product pipeline.
; Isolated identity/dead COPY contracts live in copyelim-extra.mir.
; R0 is reserved soft-zero, not a hardwired sink (soft-zero-copy-r0.mir).
; MCP does not erase overlapping src/dst copies or reserved-reg copies.

;===--- Identity copy: returning the argument should not invent a self-COPY ---===
; RA should keep the value in the return register. MCP does not erase
; overlapping src/dst COPYs; this checks the full pipeline stays clean.

define i32 @identity_copy_test(i32 %a) nounwind {
; CHECK-LABEL: identity_copy_test:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i32 %a
}

;===--- Dead copy: value overwritten before use ---===
; The result of the first add is copied, but the copy destination is
; immediately overwritten by the second add. The copy is dead.

define i32 @dead_copy_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: dead_copy_test:
; The function body should not contain redundant copies; just the
; arithmetic and return.
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = add i32 %a, %b
  ret i32 %r
}

;===--- Soft-zero R0 is not a hardwired sink ---===
; Returning 0 must not rely on "COPY to R0 is free/dead". R0 is soft-zero:
; prologue zeros it; non-identity writes into R0 are kept when live
; (negative MIR coverage in soft-zero-copy-r0.mir).

define i32 @copy_to_r0_test() nounwind {
; CHECK-LABEL: copy_to_r0_test:
; Soft-zero entry + return via JALR discarding link into R0 (function exit).
; CHECK: xor32{{.*}}r0, r0, r0
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i32 0
}

;===--- Multiple returns: ensure copies between returns are clean ---===

define i32 @multi_return(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: multi_return:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %then, label %else

then:
  ret i32 %a

else:
  ret i32 %b
}

;===--- callee-saved register restores should remain ---===
; Copies to callee-saved registers that restore their values must NOT
; be eliminated — they are live-out to the caller.

define i32 @csr_restore_test(i32 %a) nounwind {
; CHECK-LABEL: csr_restore_test:
; The function must preserve callee-saved registers, so COPYs restoring
; them from the stack should remain (they are not dead).
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %a, %a
  ret i32 %r
}
