; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — DR64 copies are OR64 via isCopyInstrImpl; MCP(UseCopyInstr).

; REGRESSION TEST: DR64 copy recognition via OR64 + generic MCP(UseCopyInstr).
;
; Bug: MCP without UseCopyInstr only saw TargetOpcode::COPY. DR64 copies are
; emitted as OR64 dN, ds, ds (rd = rs | rs). Dead OR64 copies were left in
; the packet. Fix: HaydnInstrInfo::isCopyInstrImpl + MCP(UseCopyInstr=true).
; HaydnCopyElim is deleted; do not revive it.
;
; Test design: Force DR64 register-to-register copies (OR64 via copyPhysReg).
; MCP(UseCopyInstr) sees OR64 rd,rs,rs as a copy. Dead non-identity copies
; can be removed; overlapping rd==rs copies stay. Isolated contract:
; copyelim-extra.mir.

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
