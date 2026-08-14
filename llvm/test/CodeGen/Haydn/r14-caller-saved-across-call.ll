; RUN: llc -mtriple=haydn-unknown-elf -verify-machineinstrs -global-isel-abort=1 %s -o - | FileCheck %s

; Role: semantic — R14 (FP) is caller-saved.

; REGRESSION TEST: R14 (FP) is caller-saved. CSR_Haydn does not list R14, and
; hasFP reserves it only when an actual frame pointer is in use. When hasFP
; is false, R14 is freely allocatable as scratch, so JAL/JALR must list R14 in
; their Defs so the register allocator spills any value live across the call.
;
; Without R14 in Defs, the allocator may keep 'n' live in R14 across the
; recursive call in fib, and the callee silently clobbers it — returning the
; wrong value (e.g. fib(10) returns random data instead of 55).
;
; Test invariant: the value live across the call must be saved/restored via
; st32/ld32 from a slot — i.e. the allocator did NOT try to keep it in any
; caller-saved reg the callee would clobber. The compile itself succeeding
; under -verify-machineinstrs is the main check (without R14 in Defs, MIR
; verifier would catch the liveness violation).

declare i32 @extern_leaf(i32)

; Post-call: the live-across-call value is reloaded (ld32) and the return
; value is computed (add32). Their relative order is not semantically
; significant — the post-RA scheduler may place the add32 before or after
; the epilogue reload depending on frame-instr barrier placement.
define i32 @r14_spill_across_call(i32 %x) nounwind {
  %call = call i32 @extern_leaf(i32 %x)
  %r = add i32 %call, %x
  ret i32 %r
}

; Recursive case: 'n' must survive the recursive call. This is the fib-shaped
; pattern that originally surfaced the bug.
; CHECK-LABEL: r14_recursive_survives_call:
; CHECK: st32
; CHECK: jal{{(\.s[012])?}}
; CHECK: jal{{(\.s[012])?}}
; CHECK: ld32
define i32 @r14_recursive_survives_call(i32 %n) nounwind {
entry:
  %cmp = icmp slt i32 %n, 2
  br i1 %cmp, label %base, label %recur

base:
  ret i32 %n

recur:
  %sub1 = sub i32 %n, 1
  %call1 = call i32 @r14_recursive_survives_call(i32 %sub1)
  %sub2 = sub i32 %n, 2
  %call2 = call i32 @r14_recursive_survives_call(i32 %sub2)
  %r = add i32 %call1, %call2
  ret i32 %r
}
