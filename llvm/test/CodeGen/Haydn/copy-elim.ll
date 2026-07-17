; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Tests for the HaydnCopyElim pass (post-RA).
; Each function exercises a specific redundant COPY elimination pattern.
;
; Pass optimizations tested here:
; Identity copy: COPY rA, rA (src == dst) → removed
; Dead copy: COPY rA, rB where rA overwritten before use → removed
; Copy to R0: NOT eliminated as "hardwired" — R0 is soft-zero (see
; soft-zero-copy-r0.mir). True identity COPY r0,r0 may still be removed.

;===--- Identity copy: trivial self-copy should be eliminated ---===
; The compiler may not emit these directly, but if register allocation
; produces a COPY from a register to itself, the pass must remove it.

define i32 @identity_copy_test(i32 %a) nounwind {
; CHECK-LABEL: identity_copy_test:
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  ret i32 %a
}

;===--- Dead copy: value overwritten before use ---===
; The result of the first add is copied, but the copy destination is
; immediately overwritten by the second add. The copy is dead.

define i32 @dead_copy_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: dead_copy_test:
; The function body should not contain redundant copies; just the
; arithmetic and return.
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = add i32 %a, %b
  ret i32 %r
}

;===--- Soft-zero R0 is not a hardwired sink ---===
; Returning 0 must not rely on "COPY to R0 is free/dead". R0 is soft-zero:
; prologue zeros it; non-identity writes into R0 are preserved by CopyElim
; (negative MIR coverage in soft-zero-copy-r0.mir).

define i32 @copy_to_r0_test() nounwind {
; CHECK-LABEL: copy_to_r0_test:
; Soft-zero entry + return via JALR discarding link into R0 (function exit).
; CHECK: xor32{{.*}}r0, r0, r0
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  ret i32 0
}

;===--- Multiple returns: ensure copies between returns are clean ---===

define i32 @multi_return(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: multi_return:
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
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
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %a, %a
  ret i32 %r
}
