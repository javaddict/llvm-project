; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s

; REGRESSION TEST: i64 (s64) constant materialization must NOT move SP mid-function.
;
; Bug: LOADI64 (G_CONSTANT s64) expanded once per i64 compare-constant to a
; DYNAMIC stack temp via
;     $r13 = SUBI32 $r13, 8
;     $r1  = ADDI32_W $r0, <lo>; ST32 $r1, $r13, 0
;     $r1  = ADDI32_W $r0, <hi>; ST32 $r1, $r13, 1
;     $dN  = LD64 $r13, 0
;     $r13 = ADDI32_W $r13, 8
; Moving SP mid-function broke SP-relative fixed-object addressing. In
; mac_mula64_all the hoisted i64 compare-constant 979 was stored at sp=BASE-16
; but loaded at sp=BASE-8 (8-byte SP shift across the transient) -> wrong value
; -> guest exited 11 instead of 0. BundleSim faithfully ran the emitted code;
; the emitted addressing was unsound.
;
; Fix (closed rule): LOADI64 never emits a dynamic SP adjustment. Constants
; sign/zero-extendable from one GPR32 half (Hi==0 zero-extend, or Hi==-1 with
; Lo<0 sign-extend -- covering every small i64 compare-constant such as 979)
; materialise REGISTER-ONLY: SEXT_GPR32_TO_DR64 (+ <<32; >>32 logical for the
; zero-extend case). General both-halves-nonzero constants pack via a per-
; function fixed DR64PackFI (stable FP/SP base, addressed through
; getFrameIndexReference). No memory and no SP motion for the common case.
;
; Test design: a leaf comparing an incoming i64 against 979 (Hi==0 ->
; register-only zero-extend path). This leaf still has a prologue
; `subi32 sp,sp,8` / epilogue `addi32 sp,sp,8`: that is the BASELINE emergency
; scavenger slot (processFunctionBeforeFrameFinalized reserves one 4-byte EFI,
; 8-aligned, for every function with a RegScavenger -- deliberate, "same
; insurance as ARM without a free IP"). It is NOT the forbidden transient.
; The regression guard is therefore SCOPED with CHECK-NOT between positive
; anchors: exactly one prologue subi and one epilogue addi, with NO second
; (dynamic) subi/addi around the constant materialisation, plus the positive
; register-only path (sext32t64 + slli64 + srli64). If the bug regresses, a
; second subi/addi pair appears around the pack and the scoped CHECK-NOT
; directives fail.

define i32 @i64_cmp_const_979(i64 %x) nounwind {
; CHECK-LABEL: i64_cmp_const_979:
; CHECK: subi32 sp, sp, 8
; CHECK-NOT: subi32{{.*}} sp, sp, 8
; CHECK: sext32t64 d{{[0-9]+}}, r{{[0-9]+}}
; CHECK: slli64 d{{[0-9]+}}, d{{[0-9]+}}, 32
; CHECK: srli64 d{{[0-9]+}}, d{{[0-9]+}}, 32
; CHECK-NOT: addi32{{.*}} sp, sp, 8
; CHECK: addi32 sp, sp, 8
entry:
  %c = icmp eq i64 %x, 979
  %r = zext i1 %c to i32
  ret i32 %r
}
