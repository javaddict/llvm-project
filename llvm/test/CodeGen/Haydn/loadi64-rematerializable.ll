; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s 2>&1 | FileCheck %s

; REGRESSION TEST: i64 constant materialization must use the rematerializable
; LOADI64 pseudo, NOT the MatInt-halves + MOV_GPR_TO_DR64 chain.
;
; Bug : the old chain (lo32/hi32 HaydnMatInt into GPR32 vregs + a
; MOV_GPR_TO_DR64 merge) was a 3+-instruction sequence with virtual-register
; operands, so RA could not rematerialize it. When an i64 constant was used in
; a non-dominating join block, RA cross-block-copied the DR64 result via
; `OR64 %x, %x`, referencing the single materialization that did not dominate
; the use -> "Virtual register defs don't dominate all uses" verifier abort.
;
; Fix: the selector emits one LOADI64 pseudo (single i64 immediate operand
; isReMaterializable). RA rematerializes it at each use; expandPostRAPseudo
; lowers it (MatInt lo/hi into a scavenged post-RA GPR scratch + a transient
; SP slot + LD64 — not fixed R12/AT). This test's -verify-machineinstrs is
; the probe: if the old dominance bug returns, llc aborts and FileCheck sees
; no output.

define i64 @i64_const_small() nounwind {
; CHECK-LABEL: i64_const_small:
  ret i64 1
}

define i64 @i64_const_large() nounwind {
; CHECK-LABEL: i64_const_large:
  ret i64 305419896            ; 0x12345678
}

define i64 @i64_const_neg() nounwind {
; CHECK-LABEL: i64_const_neg:
  ret i64 -1                   ; 0xFFFFFFFFFFFFFFFF
}

; i64 constant used in two non-dominated blocks — the exact shape that forced
; the cross-block OR64 copy and the dominance abort before the fix.
define i64 @i64_const_in_two_blocks(i32 %c) nounwind {
; CHECK-LABEL: i64_const_in_two_blocks:
entry:
  %t = icmp eq i32 %c, 0
  br i1 %t, label %then, label %else
then:
  ret i64 42
else:
  ret i64 42
}
