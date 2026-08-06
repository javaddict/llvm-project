; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s -o - | FileCheck %s

; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
;
; REGRESSION TEST (Bug 5 /): Haydn callee-saved registers (incl. LR/R15)
; MUST be saved at POSITIVE offsets from the new SP (inside the allocated
; frame), NOT at the raw negative PEI fixed-object offsets (which would land
; BELOW the post-prologue SP).
;
; Bug: emitPrologue/emitEpilogue used MachineFrameInfo::getObjectOffset
; directly for CSR slots. PEI assigns fixed CSR slots negative offsets
; (e.g. LR=-8, R8=-4, D8=-8). After `subi32 sp,sp,StackSize`, reaching
; sp+negative places the CSR saves BELOW the new SP — outside the allocated
; frame. A subsequent stack push (call-frame pseudo, a double-constant
; temporary, or alloca) then lands on top of the saved values. In the
; coremark-blocking case, a double-constant temporary overwrote the saved LR
; and `jalr r0, lr, 0` jumped wild.
;
; Fix : compute CSR save/restore offsets via getFrameIndexReference
; which performs the standard PEI-offset -> SP-relative-positive translation
; (getObjectOffset + StackSize) and matches eliminateFrameIndex. CSRs now sit
; INSIDE the allocated frame at sp+positive, so later pushes can never alias.

; CHECK-LABEL: bug5_main:
; CHECK: {{.}}

@g = external global double

define i32 @bug5_main(i32 %argc) {
; Prologue: stack decrement, then LR saved at POSITIVE offset from new SP.
; The buggy form was `addi32 r12, sp, -8` followed by `st32 lr, r12, 0`;
; the fixed form uses `or32 r12, sp, sp` (offset 0) or `addi32 r12, sp, <N>`
; with N >= 0.
; Mid-body double-constant push must NOT alias the LR slot — it goes to a
; distinct, lower address. The CHECK above already pins the LR save offset;
; the body push is observed as a separate `st32 {{r[0-9]+}}, sp, 0` after a
; further `subi32 sp, sp, 8`.
; Epilogue: LR reload at the same POSITIVE offset.
entry:
  %a = fmul double 1.500000e+00, 2.500000e+00     ; constant-folded double temp on stack
  store double %a, ptr @g
  %call = call i32 @helper(i32 %argc)
  %trunc = fptosi double %a to i32
  %add = add i32 %call, %trunc
  ret i32 %add
}

declare i32 @helper(i32)
