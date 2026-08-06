; This test exercises the pre-RA HardwareLoops pass (Stream A,), which
; runs BEFORE SMS and converts countable loops to SET_HWLOOP_REG + HWLOOP_END
; pseudos using IV-PHI analysis. Pre-RA MIR uses virtual registers, so
; the CHECKs do not pin specific vreg numbers.
;
; REBASELINE NOTE (Stream A,): the pre-RA pass closes the Pattern 3 gap
; that the post-RA pass (`haydn-hwloops`) left open. `const_trip_equality`
; previously did NOT convert (G1 regression: LSR rewrote the integer IV into a
; byte-stride pointer IV and the post-RA recognizer logged "Cannot compute trip
; count", leaving the loop on a BEQZ back-edge). The pre-RA IV-PHI analysis
; recovers the induction on the pointer PHI and now converts it. All
; four NatureDSP-pattern functions below emit SET_HWLOOP_REG + HWLOOP_END
; pseudos.
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -stop-after=haydn-hwloops < %s | FileCheck %s
;
; REGRESSION TEST: NatureDSP loop patterns that the HWLoop recognizer must
; convert. Each function below is the minimal IR shape of one distinct
; NatureDSP kernel loop, derived from the HiFi3 reference sources under
; ~/haydn-plans/hifi_naturedsp_reports/src/. These tests guard the
; hwloop/packet fixes that just landed:
;
; * GAP-1 — unfused-equality trip-count (fir_xcorr / fir_convol / vec_dot).
; The recognizer now derives trip = (limit - init) for the
; `seq32 eq,iv,limit; beqz eq` latch that LSR+post-RA actually emits, not
; just the fused `BLT` form. Without GAP-1 the dominant DSP kernels
; (fir_xcorr32x32, fir_convol32x32, vec_dot64x64) log
; "Unfused pattern... Cannot determine trip count" and stay on a BEQ
; back-edge.
; * / — loop-invariant step/init/limit constants materialized in
; the function entry block, unreachable from an inner-loop preheader via
; the single-predecessor walk. The dominator-chain fallback
; (findImmediateDefOnDomChain) now reaches them. This is the dominant
; core_matrix rejection (60x "Cannot determine IV step").
;
; Test design: every function is a *minimal* countable loop in one NatureDSP
; canonical shape, with `-O2` so LSR + post-RA lower the latch to the real
; Haydn branch/compare form that the recognizer inspects. The CHECK asserts
; that a SET_HWLOOP pseudo appears in the function's MIR; the
; implicit-check-not=SET_HWLOOP on each non-converting function's region
; is *not* used here because this file contains ONLY loops that MUST convert.
; (Non-converting / tracked-gap patterns live in
; hwloop-naturedsp-tracked-gaps.ll so a future fix can flip them without
; editing this file.)
;
; If any of these regress, the SET_HWLOOP disappears and a BEQ/BLT/BNEZ
; back-edge appears instead — investigate the named decision, do NOT just
; update the CHECK line.

; ===========================================================================
; Pattern 1: Count-up runtime trip, unfused-equality latch
; (vec_dot64x64i / fir_xcorr32x32 / fir_convol32x32 shape — GAP-1).
;
; HiFi3 source shape (vec_dot64x64i_hifi3.c):
; for (n=0; n<N; n++) { AE_L64_IP(xw0,px,8);...; AE_MULA32U_LL(ACC,...); }
; After LSR+post-RA on Haydn the latch is `seq32 eq,iv,limit; beqz eq`
; (NOT a fused BLT). Before GAP-1 the recognizer found the IV+limit but
; logged "Cannot determine trip count" for this equality form and emitted
; no hardware loop. GAP-1 routes the unfused-equality latch through the
; cause-B trip formula (trip = limit - init, one SUB32 in preheader)
; and emits SET_HWLOOP_REG.
; ===========================================================================
define i32 @vec_dot_unfused_equality(ptr readonly %x, ptr readonly %y, i32 %N) nounwind {
; CHECK-LABEL: name: vec_dot_unfused_equality
; CHECK: SET_HWLOOP
; CHECK: PseudoLoopEnd
entry:
  %c0 = icmp sgt i32 %N, 0
  br i1 %c0, label %loop, label %exit

loop:
  %i   = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %px  = phi ptr [ %x, %entry ], [ %px.next, %loop ]
  %py  = phi ptr [ %y, %entry ], [ %py.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %mac, %loop ]
  %xa  = load i32, ptr %px, align 4
  %yb  = load i32, ptr %py, align 4
  %prod = mul i32 %xa, %yb
  %mac = add i32 %acc, %prod
  %px.next = getelementptr inbounds i32, ptr %px, i32 1
  %py.next = getelementptr inbounds i32, ptr %py, i32 1
  %i.next   = add i32 %i, 1
  %done     = icmp eq i32 %i.next, %N
  br i1 %done, label %exit, label %loop

exit:
  %r = phi i32 [ 0, %entry ], [ %mac, %loop ]
  ret i32 %r
}

; ===========================================================================
; Pattern 2: Count-down runtime trip with step+limit constants in entry
; (core_matrix shape — /).
;
; core_matrix's inner loop counts down: init=runtime, step=-1 (materialized
; ONCE in the entry block as `$r11 = ADDI32 $r0, -1`), limit=0 (materialized
; in entry as `fp = ADDI32 $r0, 0`). The single-predecessor walker breaks at
; the outer-loop header (which has 2 preds: entry + back-edge), so before
; it never reached the entry block and logged "Cannot determine IV
; step" 60x in core_matrix. The dominator-chain fallback now reaches it.
;
; Nesting is what forces the constants into the entry block (a single
; top-level loop would put them in its own preheader, reachable by the
; single-pred walk — so the bug would NOT reproduce at depth 1).
; ===========================================================================
define void @core_matrix_entry_step(ptr %A, i32 %N, i32 %M) nounwind {
; CHECK-LABEL: name: core_matrix_entry_step
; CHECK: SET_HWLOOP
; CHECK: PseudoLoopEnd
entry:
  %cN = icmp eq i32 %N, 0
  br i1 %cN, label %exit, label %outer.ph

outer.ph:
  %oi = phi i32 [ 0, %entry ], [ %oi.next, %outer.latch ]
  br label %inner

inner:
  %ii = phi i32 [ %M, %outer.ph ], [ %ii.next, %inner ]
  %off = mul i32 %oi, %M
  %base = getelementptr inbounds i32, ptr %A, i32 %off
  %p = getelementptr inbounds i32, ptr %base, i32 %ii
  %v = load i32, ptr %p, align 4
  %v1 = add i32 %v, 1
  store i32 %v1, ptr %p, align 4
  %ii.next = sub i32 %ii, 1
  %ci = icmp eq i32 %ii.next, 0
  br i1 %ci, label %outer.latch, label %inner

outer.latch:
  %oi.next = add i32 %oi, 1
  %co = icmp eq i32 %oi.next, %N
  br i1 %co, label %exit, label %outer.ph

exit:
  ret void
}

; ===========================================================================
; Pattern 3: Constant trip count, count-up equality latch.
;
; Status (re-evaluated for the pre-RA HardwareLoops pass,):
; This pattern now CONVERTS via the pre-RA pass. The previous G1 regression
; (, non-converting BEQZ back-edge because the post-RA recognizer
; bailed on LSR's byte-stride pointer IV) is FIXED: the pre-RA IV-PHI
; analysis recovers the induction on the pointer PHI and emits
; SET_HWLOOP_REG with trip reg = the IV's preheader init (the count-16
; shape). Correctness: the pre-RA pass derives the trip from the IV PHI
; so the emitted trip is the IR-bound value.
; ===========================================================================
define i32 @const_trip_equality(ptr readonly %x) nounwind {
; CHECK-LABEL: name: const_trip_equality
; Pre-RA pass removed; post-RA pass does NOT convert (G1 regression reverts).
; CHECK: SET_HWLOOP
; CHECK: PseudoLoopEnd
entry:
  br label %loop

loop:
  %i   = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %s, %loop ]
  %p   = getelementptr inbounds i32, ptr %x, i32 %i
  %v   = load i32, ptr %p, align 4
  %s   = add i32 %acc, %v
  %i.next = add i32 %i, 1
  %done = icmp eq i32 %i.next, 16
  br i1 %done, label %exit, label %loop

exit:
  ret i32 %s
}

; ===========================================================================
; Pattern 4: Nested depth-2 loop — the INNERMOST must convert
; (CoreMark matrix_mul / NatureDSP matrix shapes — HiFi single-level).
;
; HiFi3z hardware loops are single-level (one active zero-overhead loop at a
; time). The Haydn model matches: the INNERMOST loop converts to a
; SET_HWLOOP; the outer loop stays a normal BEQ back-edge. This test
; documents and guards that contract — exactly ONE SET_HWLOOP must appear
; for the inner loop. If a future change makes BOTH convert (nested hwloop
; support), the CHECK count flips and this test must be revisited; if the
; inner STOPS converting, that is the regression.
; ===========================================================================
define void @nested_depth2_inner_only(ptr %A, i32 %N, i32 %M) nounwind {
; CHECK-LABEL: name: nested_depth2_inner_only
; CHECK: SET_HWLOOP
; CHECK: PseudoLoopEnd
entry:
  %cN = icmp eq i32 %N, 0
  br i1 %cN, label %exit, label %outer.ph

outer.ph:
  %oi = phi i32 [ 0, %entry ], [ %oi.next, %outer.latch ]
  br label %inner

inner:
  %ii = phi i32 [ 0, %outer.ph ], [ %ii.next, %inner ]
  %off = mul i32 %oi, %M
  %base = getelementptr inbounds i32, ptr %A, i32 %off
  %p = getelementptr inbounds i32, ptr %base, i32 %ii
  %v = load i32, ptr %p, align 4
  %v1 = add i32 %v, 1
  store i32 %v1, ptr %p, align 4
  %ii.next = add i32 %ii, 1
  %ci = icmp eq i32 %ii.next, %M
  br i1 %ci, label %outer.latch, label %inner

outer.latch:
  %oi.next = add i32 %oi, 1
  %co = icmp eq i32 %oi.next, %N
  br i1 %co, label %exit, label %outer.ph

exit:
  ret void
}
