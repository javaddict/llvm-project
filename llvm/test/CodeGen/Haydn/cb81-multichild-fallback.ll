; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; REGRESSION TEST: — graceful pure-tblgen fallback for unbaked
; multi-child bundles (prior revision).
;
; Bug: the slot-OR MCCodeEmitter (-E4d, row-auction retired) FATAL-CRASHED
; `clang -c` / `llc` at -O1/-O2 on any function whose codegen produces a
; multi-child bundle that HaydnSlotVariantFinalizer can't fully bake to
; per-slot _M0 variants. Two crash sites:
; 1. encodeBundleSlotOR asserted `(Bits & 0xF) == 0b0011` on an EW_64Bit
; child that isn't a Mode-0 slot variant.
; 2. encodeBundle report_fatal_error'd on the unbaked multi-child bundle
; (no fallback after the row-auction retirement).
; The proven trigger (compiler-rt __udivmoddi4): an `lshr i64 X, amt` lowers
; to SRL64 (DR64 def) plus a MOVE32_DR_L companion (GPR32 extract), and that
; companion has no _M0 variant in the finalizer's coverage set — the bundle
; stays generic and the OLD encoder hit report_fatal_error (exit 134).
;
; Fix (46ad979c): encodeBundleSlotOR returns false (not assert) when a
; child's encoded bits lack the Mode-0 pattern 0011; encodeBundle emits an
; unbaked multi-child bundle as sequential standalone parcels via
; encodeSingleInstruction (pure-tblgen getBinaryCodeForInstr), mirroring the
; existing 48-bit-child split. LLVM_DEBUG logs the coverage gap.
;
; Test design: each function combines an i64 shift (lowers to SRL64/SRA64
; SLL64 with a DR64 destination) with a companion scalar op, in a loop body
; that the packetizer bundles. The companion + cross-bank DR64→GPR32 extract
; (MOVE32_DR_L, no _M0 variant) is precisely the case the finalizer can't
; fully bake. The KEY regression guard is the RUN line itself: if
; regresses (assert or report_fatal_error returns), llc crashes with a
; non-zero exit code and the test fails. The CHECK lines additionally pin
; the shift mnemonic so the IR keeps producing SRL64/SRA64/SLL64 (if the
; selector regresses to a decomposition, the bundle shape changes and this
; test no longer exercises the fallback — surfacing that drift).
;
; What breaks if regresses:
; Pre-fix behavior returns: llc aborts with "Haydn pure-tblgen emit:
; bundle not encodable via encodeBundleSlotOR (unbaked/compact-format
; child)" (exit 134). RUN line fails.
; If the assertion at encodeBundleSlotOR returns: "Mode-0 slot-OR child
; must carry pattern bits 0011" abort.
;
; References:
; OPEN-COMPILER-BUGS.md (FIXED)
; llvm/lib/Target/Haydn/MCTargetDesc/HaydnMCCodeEmitter.cpp
; (encodeBundle fallback, encodeBundleSlotOR return-on-gap)
; compiler-rt/lib/builtins/udivmoddi4.c (original crash repro)

; CHECK-LABEL: test_lshr_i64_in_loop:
; CHECK: srl64
; An i64 logical right shift in a packetizable loop body. SRL64 produces a
; DR64 destination; when the result feeds back into an i32 accumulator the
; selector inserts MOVE32_DR_L (cross-bank extract, no _M0 variant) → the
; multi-child bundle hits the fallback path. If regresses, llc
; crashes on this function.
define i64 @test_lshr_i64_in_loop(i64 %x, i32 %n, i64 %sh) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i64 [ %x, %entry ], [ %acc.shr, %loop ]
  %acc.shr = lshr i64 %acc, %sh
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  ret i64 %acc.shr
}

; CHECK-LABEL: test_ashr_i64_in_loop:
; CHECK: sra64
; Same shape with arithmetic shift — SRA64 (DR64 dest) + companion ops.
define i64 @test_ashr_i64_in_loop(i64 %x, i32 %n, i64 %sh) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i64 [ %x, %entry ], [ %acc.shr, %loop ]
  %acc.shr = ashr i64 %acc, %sh
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  ret i64 %acc.shr
}

; CHECK-LABEL: test_shl_i64_in_loop:
; CHECK: sll64
; Left-shift variant — SLL64 lowers (i64, i64)->i64 with DR64 dest. A
; scalar add companion in the same loop body forces a multi-child bundle.
define i64 @test_shl_i64_in_loop(i64 %x, i32 %n, i64 %sh) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i64 [ %x, %entry ], [ %acc.shl, %loop ]
  %acc.shl = shl i64 %acc, %sh
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  ret i64 %acc.shl
}
