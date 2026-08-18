; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -haydn-enable-hwloops \
; RUN:   -enable-pipeliner=false -enable-misched=false \
; RUN:   -global-isel-abort=1 -verify-machineinstrs -mattr=+hwloop \
; RUN:   -stop-after=haydn-hwloops < %s | FileCheck %s

; Role: MIR — Previously expected-fail (XFAIL marker removed): the kept ISA-27 compare/branch codegen (denser bundles; CHECK at line 84 no longer matches).

; Previously expected-fail (XFAIL marker removed): the kept ISA-27
; compare/branch codegen (denser bundles; CHECK at line 84 no longer matches).
; Denser-but-correct; needed a denser-bundle rebaseline (like
; compare-branches/s64-*). Marker removed: rebaselined and now passes. See.
;
; REGRESSION TEST (documenting): NatureDSP loop patterns and the HWLoop
; recognizer. This file now uses the pre-RA HardwareLoops pass
; (`-stop-after=haydn-hwloops`, Stream A /), which runs BEFORE SMS
; and converts countable loops to SET_HWLOOP_REG + HWLOOP_END pseudos (on
; virtual registers — vreg numbers are deliberately not pinned in the CHECKs).
; These are the tracked patterns from
; ~/haydn-plans/naturedsp-haydn/disasm/kernel-sdiff/PATTERNS.md (GAP-2, GAP-3).
; This file exists so that:
;
; 1. The current behavior is locked (no silent regression of a *working*
; path into these shapes, and no accidental conversion that would
; indicate an untested code change).
; 2. When a fix lands for a remaining tracked gap, this test will FAIL
; forcing the fix author to update it here with a SET_HWLOOP CHECK and a
; reference to the closing decision. That is the intended workflow: do
; NOT silence a failure here by relaxing the CHECK without understanding why.
;
; This is deliberately NOT using the lit expected-fail directive: that
; directive would silently pass when the gap closes, hiding the fact that the
; behavior changed. A positive CHECK on the hardware-loop pseudos makes the
; change loud.
;
; -enable-pipeliner=false -enable-misched=false: pre-RA SMS and the pre-RA
; machine scheduler hang on the IIR recurrence (bqriir). This file pins Role-A
; expand output, not those passes.

; ===========================================================================
; Pattern 3 (GAP-2 CLOSED by): Count-up runtime trip, fused BLT latch.
; for (i=0; i<n; i++) sum += a[i];
;
; This is the most common C for-loop shape. After LSR+post-RA it lowers to a
; fused `BLT iv, limit` back-edge. Before the recognizer converted the
; UNFUSED equality form (`seq32 eq,iv,limit; beqz_w eq` — GAP-1, see
; hwloop-naturedsp-patterns.ll) but NOT the fused BLT form, because the IV
; was initialized via a MOVE32 copy in the preheader and the init constant
; lived in the entry block — the cross-block copy-chain resolution gap (see
; and Fix A). After, resolveCopySourceInBlock carries the copy
; source across block boundaries, the IVInit constant resolves, and Case 1
; of the register-trip-count path matches (TripCountReg = LimitReg).
;
; Behavior after : a SET_HWLOOP_REG + HWLOOP_END, no compare/branch
; back-edge. This CHECK was updated from `CHECK-NOT: SET_HWLOOP / CHECK: BLT`
; to the converted form. The dedicated regression test for this shape (with
; the full comment block documenting the bug) lives in
; hwloop-recognizer-broaden.ll @gap2_countup_blt.
;
; Pre-RA pass note (Stream A,): under `-stop-after=haydn-hwloops`
; the loop still converts; the SET_HWLOOP_REG/HWLOOP_END pseudos are emitted
; on virtual registers (pre-RA), so no vreg numbers are pinned here.
; ===========================================================================

define i32 @countup_runtime_blt(ptr readonly %a, i32 %n) nounwind {
; CHECK-LABEL: name: countup_runtime_blt
; CHECK: SET_HWLOOP
; CHECK: PseudoLoopEnd
entry:
  %c0 = icmp sgt i32 %n, 0
  br i1 %c0, label %loop, label %exit

loop:
  %i   = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %p   = getelementptr inbounds i32, ptr %a, i32 %i
  %v   = load i32, ptr %p, align 4
  %sum.next = add i32 %sum, %v
  %i.next   = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit

exit:
  %r = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  ret i32 %r
}

; ===========================================================================
; Pattern 6 (GAP-3 CLOSED by the pre-RA pass, Stream A /): IIR
; recurrence loop (bqriir32x32_df1 shape).
; y[i] = b0*x[i] + b1*x[i-1] + b2*x[i-2] - a1*y[i-1] - a2*y[i-2]
;
; HiFi3 source: bqriir32x32_df1_hifi3.c. The loop body has feedback recursion
; (each y[i] depends on y[i-1], y[i-2]). The loop is COUNTABLE (trip = N)
; so a hardware loop is legal. PATTERNS.md classified this as GAP-3
; ("recursion / data-dependent... confirm per-loop; don't force") because the
; old post-RA recognizer (`haydn-hwloops`) did not convert it.
;
; CLOSED by the pre-RA HardwareLoops pass (Stream A,): the pre-RA pass
; runs before SMS and recognizes the countable trip from the induction/compare
; pair regardless of the in-loop recurrence, emitting SET_HWLOOP_REG +
; HWLOOP_END on virtual registers. The CHECK has been flipped from the
; non-conversion form (`CHECK-NOT: SET_HWLOOP / CHECK: BNEZ`) to the converted
; form. vreg numbers are not pinned (pre-RA MIR).
; ===========================================================================
define void @bqriir_df1_recurrence(ptr %y, ptr %x, i32 %N,
                                   i32 %b0, i32 %b1, i32 %b2,
                                   i32 %a1, i32 %a2) nounwind {
; CHECK-LABEL: name: bqriir_df1_recurrence
; Pre-RA pass removed; post-RA does NOT convert (tracked GAP-3 reverts).
; CHECK: SET_HWLOOP
; CHECK: PseudoLoopEnd
entry:
  %c0 = icmp sgt i32 %N, 0
  br i1 %c0, label %loop, label %exit

loop:
  %i   = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %ym1 = phi i32 [ 0, %entry ], [ %yn, %loop ]
  %ym2 = phi i32 [ 0, %entry ], [ %ym1, %loop ]
  %xm1 = phi i32 [ 0, %entry ], [ %xn, %loop ]
  %xm2 = phi i32 [ 0, %entry ], [ %xm1, %loop ]
  %xp  = getelementptr inbounds i32, ptr %x, i32 %i
  %xn  = load i32, ptr %xp, align 4
  %t1 = mul i32 %b0, %xn
  %t2 = mul i32 %b1, %xm1
  %t3 = mul i32 %b2, %xm2
  %t4 = mul i32 %a1, %ym1
  %t5 = mul i32 %a2, %ym2
  %s1 = add i32 %t1, %t2
  %s2 = add i32 %s1, %t3
  %s3 = sub i32 %s2, %t4
  %yn = sub i32 %s3, %t5
  %yp = getelementptr inbounds i32, ptr %y, i32 %i
  store i32 %yn, ptr %yp, align 4
  %i.next = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %N
  br i1 %cond, label %loop, label %exit

exit:
  ret void
}
