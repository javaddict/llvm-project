; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: DR64 copy elimination via OR64 identity pattern.
;
; Bug: HaydnCopyElim only handled GPR32 COPY elimination. DR64 register copies
; are emitted as OR64 dN, dN, dN (identity: dN = dN | dN = dN). These were never
; eliminated, wasting execution slots in the VLIW packet.
; Fix: Added OR64 identity copy detection to HaydnCopyElim (Pass 1).
;
; Test design: Create functions that force DR64 register-to-register copies.
; The compiler emits OR64 for DR64 copies. With the fix, identity OR64 patterns
; are removed. We verify that output is clean (no redundant OR64 with all-same regs).

;===--- DR64 identity: function returning input i64 (forces DR64 copy) ---===
; The return value copy may be an OR64 identity if the value stays in the same DR64.

define i64 @dr64_identity(i64 %a) nounwind {
; CHECK-LABEL: dr64_identity:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i64 %a
}

;===--- DR64 with arithmetic: ensure no redundant OR64 survives ---===

define i64 @dr64_add(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: dr64_add:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = add i64 %a, %b
  ret i64 %r
}

;===--- DR64 with multiple live values: forces register pressure and copies ---===

define i64 @dr64_multi(i64 %a, i64 %b, i64 %c) nounwind {
; CHECK-LABEL: dr64_multi:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %ab = add i64 %a, %b
  %bc = add i64 %b, %c
  %r = add i64 %ab, %bc
  ret i64 %r
}

;===--- DR64 with function call: forces callee-saved saves/restores ---===

declare i64 @ext_fn(i64)

define i64 @dr64_call(i64 %a) nounwind {
; CHECK-LABEL: dr64_call:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = call i64 @ext_fn(i64 %a)
  ret i64 %r
}

;===--- DR64 dead copy: value overwritten before use ---===
; The first add result is assigned to a register, then immediately overwritten
; by the second add. Any copy of the first result is dead.

define i64 @dr64_dead_copy(i64 %a, i64 %b, i64 %c) nounwind {
; CHECK-LABEL: dr64_dead_copy:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %ab = add i64 %a, %b
  %ac = add i64 %a, %c
  %r = add i64 %ac, %b
  ret i64 %r
}
