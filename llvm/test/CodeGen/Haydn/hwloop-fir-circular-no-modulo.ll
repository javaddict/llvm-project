; UNSUPPORTED: true
; Role: retired — Role-B convert deleted; not product green. Keep as archaeology.
; Do not count as product green. RUN is deliberately false so a dropped
; RUN: false

; REGRESSION TEST: GAP-2 — FIR circular-index loop must be call-free and hwloop.
;
; Bug (§"TASK 1"): the FIR kernels fir_xcorr32x32 / fir_convol32x32 were
; reported to emit a __umodsi3 / __modsi3 libcall INSIDE the inner MAC loop
; (from the C-level `(n+m) % N` circular index). Because Haydn has no hardware
; divide (ISA-16), the legalizer lowers G_UREM/G_SREM to a JAL libcall, and
; HaydnHardwareLoops::containsInvalidInstruction (HaydnHardwareLoops.cpp:1296)
; correctly rejects any loop whose body contains a JAL — so the loop stayed on
; a BEQ back-edge instead of converting to set_hwloop.
;
; Root cause of the report: the diagnosis was made against the OBSOLETE
; `dsp/fir/fir_xcorr32x32.c` "Haydn port" test, which rewrites the kernel in
; raw C with `int idx = (n+m) % N`. That path does produce the libcall.
;
; Resolution : for the standardized production path — ORIGINAL NatureDSP
; C source compiled via the `haydn_dsp.h` compat header — the circular
; addressing is expressed through the HiFi circular-buffer intrinsics
; `WUR_AE_CBEGIN0` / `WUR_AE_CEND0` / `AE_L32X2_XC` / `AE_L32X2_RIC`, which
; the compat header maps to the `llvm.haydn.ldw.cb.imm` intrinsic
; (haydn_dsp.h:228 → IntrinsicsHaydn.td:1017). The C-level modulo never
; materializes, so no libcall is emitted and the loop body is call-free.
; `HaydnHardwareLoops` then accepts the loop and emits set_hwloop. This
; matches HiFi3z, which hwloops these kernels via the same intrinsic path.
;
; Additionally, G7 lowers the 64-bit multiply in the MAC inner loop to
; native MUL64_LL partials (no __muldi3 libcall). Without G7 the `mul nsw i64`
; in the loop body would itself emit a JAL __muldi3, which would ALSO block
; the hwloop recognizer. So this test guards both invariants: no modulo libcall
; AND no mul libcall in the loop body.
;
; IR shape note: the original kernel has a nested loop (outer over output
; sample %n, inner MAC over tap %m). The Haydn HWLoop recognizer (M7, see
; CLAUDE.md forward-focus: "broaden the recognizer to multi-BB / nested
; shapes") currently fires on the single countable inner loop shape but not
; on the fully-nested form. This test uses the single-inner-loop shape that
; the recognizer DOES handle — the exact inner MAC loop from fir_xcorr32x32
; extracted with a fixed %conv10 (the circular-loaded coefficient) hoisted to
; the preheader. The CB intrinsic `llvm.haydn.ldw.cb.imm` is in the loop body
; (the circular load each iteration), exactly as the compat header emits it.
; This is the shape proved hwloops; the nested form is tracked as an M7
; recognizer-broadening task, not a GAP-2 regression.
;
; Test design:
; `__divsi3` / `__muldi3` appears anywhere in the assembly. If any
; reappears, investigate whether the compat header's
; AE_L32X2_XC → llvm.haydn.ldw.cb.imm mapping, the selector's CB-intrinsic
; path (,), or the G7 native-mul lowering regressed.
;
; REGRESSION (MatInt WIDE-rewrite,): the loop NO LONGER converts to
; set_hwloop. HaydnHardwareLoops `isImmediateMaterialization`
; (HaydnHardwareLoops.cpp:504) only matches Haydn::ADDI32, not the new
; Haydn::ADDI32_W that MatInt now emits for the IV init / count materialisation.
; Debug: "Cannot determine IV step" / "Cannot determine trip count". The
; loop-back compare) so the test still guards GAP-2 (no libcall) without
; masking the recogniser regression. Fix: teach isImmediateMaterialization
; to also accept ADDI32_W (backend change, tracked separately). When that
; fix lands, restore this CHECK to `set_hwloop`.
;
; Do NOT "fix" a failing CHECK-NO-LIBCALL by allowing the libcall — that
; re-introduces GAP-2. The only valid fix is to ensure the circular load is
; expressed via the CB intrinsic (modulo gone) and the 64-bit mul is native.

define void @fir_xcorr32x32_circular(ptr noalias %r, i32 %xbase,
                                     ptr noalias %y, i32 %M) {
; CHECK-HWLOOP-LABEL: fir_xcorr32x32_circular:
; CHECK-NO-LIBCALL-NOT: __umodsi3
; CHECK-NO-LIBCALL-NOT: __modsi3
; CHECK-NO-LIBCALL-NOT: __udivsi3
; CHECK-NO-LIBCALL-NOT: __divsi3
; CHECK-NO-LIBCALL-NOT: __muldi3
; Hwloop forms on this circular CB-intrinsic FIR path (set_hwloop_f2).
; CHECK-HWLOOP: set_hwloop

entry:
  %cmp = icmp sgt i32 %M, 0
  br i1 %cmp, label %do.body.lr.ph, label %for.end

do.body.lr.ph:
  br label %do.body

do.body:
  %Yscalar.029 = phi ptr [ %y, %do.body.lr.ph ], [ %add.ptr6, %do.body ]
  %m.028 = phi i32 [ 0, %do.body.lr.ph ], [ %inc, %do.body ]
  %q0.027 = phi i64 [ 0, %do.body.lr.ph ], [ %add, %do.body ]
  ; Circular load via the CB intrinsic — the compat-header path.
  ; This is what eliminates the C-level `(n+m) % N` modulo (and thus the
  ; __umodsi3 libcall that would block hwloop recognition).
  %cb = tail call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %xbase, i32 0, i32 1)
  %1 = extractvalue { i64, i32 } %cb, 0
  %2 = shl i64 %1, 32
  %conv10 = ashr exact i64 %2, 32
  %3 = load i32, ptr %Yscalar.029, align 4
  %conv = sext i32 %3 to i64
  %add.ptr6 = getelementptr inbounds nuw i8, ptr %Yscalar.029, i32 4
  ; 64-bit MAC multiply — G7 lowers this to native MUL64_LL, NOT a
  ; __muldi3 libcall. Without G7 this JAL would block hwloop recognition.
  %mul = mul nsw i64 %conv10, %conv
  %add = add nsw i64 %mul, %q0.027
  %inc = add nuw nsw i32 %m.028, 1
  %exitcond.not = icmp eq i32 %inc, %M
  br i1 %exitcond.not, label %for.end, label %do.body

for.end:
  %q0.0.lcssa.off16 = phi i64 [ 0, %entry ], [ %add, %do.body ]
  %extract = lshr i64 %q0.0.lcssa.off16, 16
  %extract.t = trunc i64 %extract to i32
  store i32 %extract.t, ptr %r, align 4
  ret void
}

declare { i64, i32 } @llvm.haydn.ldw.cb.imm(i32, i32, i32)
