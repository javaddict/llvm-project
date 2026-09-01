; REQUIRES: asserts
; RUN: rm -rf %t && split-file %s %t
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs < %t/near.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=NEAR
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 < %t/near.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=NEAR
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 \
; RUN:     -debug-only=haydn-late-convergence < %t/near.ll -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=DBG
; RUN: %python %t/gen_far.py > %t/far.ll
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 < %t/far.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=FAR
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 \
; RUN:     -debug-only=haydn-late-convergence < %t/far.ll -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=FARDBG
;
; W68.3R forward vs back range pins (exit-row). Distinct from a lone
; jalr epilogue: in-range forward is a PC-relative cond to a later
; label, in-range back is a PC-relative cond to an earlier loop
; header, and out-of-range (simm12) sites promote to LUI+ADDI+JALR
; inside the bounded loop with BR last. mattr=-hwloop keeps the
; software back-edge.

; Forward in-range: PC-relative cond to a later block. Not a LUI trampoline.
; NEAR-LABEL: fwd_near:
; NEAR: {{beqz|bnez|blt|bge}}{{.*}}.LBB0_
; NEAR-NOT: lui
; NEAR: jalr

; Backward in-range: PC-relative cond back to the loop header.
; NEAR-LABEL: back_near:
; NEAR: .LBB1_1:
; NEAR: {{bnez|beqz|blt|bge}}{{.*}}.LBB1_1
; NEAR-NOT: lui
; NEAR: jalr

; DBG: HaydnLateConvergence: fwd_near bound={{[0-9]+}}
; DBG: HaydnLateConvergence: closed after {{[0-9]+}} iteration(s) (no upward event)
; DBG: HaydnLateConvergence: back_near bound={{[0-9]+}}
; DBG: HaydnLateConvergence: closed after {{[0-9]+}} iteration(s) (no upward event)
; DBG-NOT: exhausted

; Out-of-range forward: the GR2.7 in-block long form — address
; materialization (LUI, then ADDI32) precedes the inverted near cond,
; then the JALR to the far target (the generic MachineVerifier forbids
; non-terminators after the first terminator, so the materialization
; comes first). Out-of-range back: the long-latch template on the latch
; (same LUI/ADDI before BEQZ law).
; FAR-LABEL: fwd_far:
; FAR: lui
; FAR: addi32
; FAR: jalr
; FAR-LABEL: back_far:
; FAR: lui
; FAR: addi32
; FAR: jalr

; FARDBG: HaydnLateConvergence: fwd_far bound={{[0-9]+}}
; FARDBG: HaydnLateConvergence: closed after {{[0-9]+}} iteration(s) (no upward event)
; FARDBG: HaydnLateConvergence: back_far bound={{[0-9]+}}
; FARDBG: HaydnLateConvergence: closed after {{[0-9]+}} iteration(s) (no upward event)
; FARDBG-NOT: exhausted

;--- near.ll
define i32 @fwd_near(i32 %a, ptr nocapture %p) {
entry:
  %c = icmp eq i32 %a, 0
  br i1 %c, label %tgt, label %other
other:
  store volatile i32 1, ptr %p, align 4
  br label %exit
tgt:
  store volatile i32 2, ptr %p, align 4
  br label %exit
exit:
  ret i32 %a
}

define i32 @back_near(ptr nocapture %p, i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %v = load volatile i32, ptr %p, align 4
  %s = add i32 %v, %i
  store volatile i32 %s, ptr %p, align 4
  %inc = add i32 %i, 1
  %c = icmp slt i32 %inc, %n
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %i
}

;--- gen_far.py
n = 400
print("define void @fwd_far(i32 %c, ptr %p) {")
print("entry:")
print("  %cmp = icmp eq i32 %c, 0")
print("  br i1 %cmp, label %far, label %pad")
print("pad:")
for i in range(n):
    print("  store volatile i32 %d, ptr %%p, align 4" % i)
print("  br label %far")
print("far:")
print("  ret void")
print("}")
print("define void @back_far(ptr %p, i32 %n) {")
print("entry:")
print("  br label %loop")
print("loop:")
print("  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]")
for i in range(n):
    print("  store volatile i32 %d, ptr %%p, align 4" % i)
print("  %inc = add i32 %i, 1")
print("  %c = icmp slt i32 %inc, %n")
print("  br i1 %c, label %loop, label %exit")
print("exit:")
print("  ret void")
print("}")
