; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s 2>&1 | FileCheck %s

; REGRESSION TEST: G_TRUNC s64 -> s1 must extract BIT 0 into a clean 0/1 GPR32
; (MOVE32_DR_L then AND32 with 1), not alias the s1 Dst to the DR64 Src.
;
; Bug (/): s64->s1 was not legal at all ("unable to legalize G_TRUNC
; s64->s1"). The naive fix — declare it legal and let the sub-32-bit branch
; replaceRegWith(Dst s1, Src s64) — was wrong: it left the DR64 bank on the s1
; result (leaking into ST32/MOVT32 GPR32 slots -> "Illegal virtual register")
; and G_BRCOND tests non-zero (not bit 0), so `trunc i64 2 to i1` mis-branches.
; The selector now extracts bit 0 into a real 0/1 GPR32 (AND with 1), correct
; for both G_BRCOND (non-zero) and MOVT32 (bit-0) consumers.
;
; The -verify-machineinstrs on the RUN line is the DR64-leak probe; the two
; functions below pin the bit-0 semantics (2 -> false, 3 -> true) for any future
; runtime check.

; trunc i64 2 to i1 -> bit0 = 0 -> select picks the false value (10).
; CHECK-LABEL: trunc_s64_2_to_i1:
define i32 @trunc_s64_2_to_i1() nounwind {
  %c = trunc i64 2 to i1            ; 2 & 1 = 0 -> false
  %r = select i1 %c, i32 20, i32 10
  ret i32 %r                        ; expected 10
}

; trunc i64 3 to i1 -> bit0 = 1 -> select picks the true value (20).
; CHECK-LABEL: trunc_s64_3_to_i1:
define i32 @trunc_s64_3_to_i1() nounwind {
  %c = trunc i64 3 to i1            ; 3 & 1 = 1 -> true
  %r = select i1 %c, i32 20, i32 10
  ret i32 %r                        ; expected 20
}

; Used in a branch (G_BRCOND, non-zero test).
; CHECK-LABEL: trunc_s64_branch:
define i32 @trunc_s64_branch(i64 %v) nounwind {
entry:
  %c = trunc i64 %v to i1
  br i1 %c, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}
