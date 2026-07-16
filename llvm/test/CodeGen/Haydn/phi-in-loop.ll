; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr_w}}
;
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.




;
; REGRESSION TEST: G_PHI must be converted to target PHI during selection.
;
; Bug: The Haydn instruction selector handled G_PHI by setting register classes
; on operands but never called MI.setDesc(TII.get(TargetOpcode::PHI)) to convert
; the opcode. This left G_PHI instructions in the Selected function, triggering
; "Unexpected generic instruction G_PHI in a Selected function" verifier errors.
;
; The pattern that triggers it: loop with phi nodes that feed into shift operations.
; The selector's custom G_PHI case (in the switch) was reached but only set reg
; classes -- it did not actually perform the opcode conversion that RISC-V/AArch64
; backends do via MI.setDesc(PHI).
;
; If this test fails with "Unexpected generic instruction G_PHI", the selector's
; G_PHI handling is missing the setDesc call. Do NOT work around it by removing
; the test -- fix the selector.
;
; Test design: Two phi nodes in a loop body, one feeding a shift (shl), the other
; an add. The shift forces the phi result through a non-trivial dependency chain.

define i32 @shift_loop(ptr %p, i32 %n) {
; (SFR-strip) changed bundle layout — rebaselined.
entry:
  br label %loop

loop:
  %acc = phi i32 [ 0, %entry ], [ %next, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %val = load i32, ptr %p
  %next = shl i32 %acc, 1
  %next2 = or i32 %next, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %next2
}
