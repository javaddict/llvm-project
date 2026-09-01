; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s

; REGRESSION TEST: same-ready-cycle MAC twins must coissue two-per-bundle.
;
; Bug (quad4acc.c shape: four independent haydn_fmulaa32x16 accumulators in
; one loop): the post-RA scheduler grouped the four FMULAA32X16 into
; same-ready-cycle pairs correctly, but canCoissueProductCycle REJECTED every
; group before the rematch arm could run:
;
;   if (cycleHasMixedFormatEModes(Instrs))   // early reject — E3+E2 member mix
;     return false;
;   ...
;   if (auto Mixed = resolveMixedMemberCycleOnce(...))  // the REMATCHER —
;                                                      // UNREACHABLE for the
;                                                      // mixed input it exists
;                                                      // to fix
;
; materializeMultiOpcodeInstrs bakes each MI's preferred member independently
; (one H3_L2 onto the E3 row MAC1, the other onto the E2 row MAC1 — a
; row-MIXED and unit-DUPLICATED set). The early mixed-mode gate rejected that
; input outright, so resolveMixedMemberCycleOnce (peel members to logicals via
; productSolveLogicalOpcode, then exactSolveProductOpcodes onto one coherent
; row) never ran. Every group then fell through to the singleton wrap: 4
; one-op bundles instead of 2 two-MAC bundles. Fixed by applying the hard
; mixed-row reject only to the POST-rematch member set in all three commit
; surfaces (canCoissueProductCycle / commitExactMultiMIProductCycle /
; commitExactHardRootProductCycle — one law, not a second path).
;
; FMULAA32X16 twins exist on both rows: H1_L0 has E2_E0_MAC0 + E2_E1_MAC1
; (E2 row, both MAC units) and E3_E1_MAC0 + E3_E2_MAC1 (E3 row), so the
; rematch binds each pair onto one row with distinct MAC units.
;
; Test design: two h3.l2 + two h1.l0 accumulators over independent DR64
; accumulators. After the fix each same-ready MAC pair commits as ONE bundle
; with two fmulaa32x16 on distinct MAC units. If the early-reject returns,
; each MAC becomes its own bundle (singletons) and the MAC0+MAC1 CHECKs fail.

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

define void @quad(ptr %out, i32 %n) {
entry:
  %cmp26 = icmp sgt i32 %n, 0
  br i1 %cmp26, label %for.body, label %for.cond.cleanup

for.cond.cleanup:
  %q1.0.lcssa = phi i64 [ 0, %entry ], [ %1, %for.body ]
  %q2.0.lcssa = phi i64 [ 0, %entry ], [ %2, %for.body ]
  %q3.0.lcssa = phi i64 [ 0, %entry ], [ %3, %for.body ]
  %q0.0.lcssa = phi i64 [ 0, %entry ], [ %0, %for.body ]
  store i64 %q0.0.lcssa, ptr %out, align 4
  %arrayidx8 = getelementptr inbounds nuw i8, ptr %out, i32 8
  store i64 %q1.0.lcssa, ptr %arrayidx8, align 4
  %arrayidx9 = getelementptr inbounds nuw i8, ptr %out, i32 16
  store i64 %q2.0.lcssa, ptr %arrayidx9, align 4
  %arrayidx10 = getelementptr inbounds nuw i8, ptr %out, i32 24
  store i64 %q3.0.lcssa, ptr %arrayidx10, align 4
  ret void

for.body:
  %i.035 = phi i32 [ %inc, %for.body ], [ 0, %entry ]
  %q0.034 = phi i64 [ %0, %for.body ], [ 0, %entry ]
  %d23o.033 = phi i64 [ %add6, %for.body ], [ 4, %entry ]
  %d23e.032 = phi i64 [ %add5, %for.body ], [ 3, %entry ]
  %d01o.031 = phi i64 [ %add4, %for.body ], [ 2, %entry ]
  %d01e.030 = phi i64 [ %add, %for.body ], [ 1, %entry ]
  %q3.029 = phi i64 [ %3, %for.body ], [ 0, %entry ]
  %q2.028 = phi i64 [ %2, %for.body ], [ 0, %entry ]
  %q1.027 = phi i64 [ %1, %for.body ], [ 0, %entry ]
  %0 = tail call i64 @llvm.haydn.fmulaa32x16.h3.l2(i64 %q0.034, i64 %d01e.030, i64 5)
  %1 = tail call i64 @llvm.haydn.fmulaa32x16.h3.l2(i64 %q1.027, i64 %d01o.031, i64 5)
  %2 = tail call i64 @llvm.haydn.fmulaa32x16.h1.l0(i64 %q2.028, i64 %d23e.032, i64 5)
  %3 = tail call i64 @llvm.haydn.fmulaa32x16.h1.l0(i64 %q3.029, i64 %d23o.033, i64 5)
  %add = add nuw nsw i64 %d01e.030, 1
  %add4 = add nuw nsw i64 %d01o.031, 1
  %add5 = add nuw nsw i64 %d23e.032, 1
  %add6 = add nuw nsw i64 %d23o.033, 1
  %inc = add nuw nsw i32 %i.035, 1
  %exitcond.not = icmp eq i32 %inc, %n
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body
}

declare i64 @llvm.haydn.fmulaa32x16.h3.l2(i64, i64, i64)
declare i64 @llvm.haydn.fmulaa32x16.h1.l0(i64, i64, i64)

; The loop body must materialize the four MACs as TWO multi-MAC bundles (the
; h3.l2 pair and the h1.l0 pair), each bundle seating its two fmulaa32x16 on
; the two distinct MAC units of one coherent Format E row (user-facing asm
; prints logical mnemonics; unit distinctness is the solver's golden
; unit-injectivity law and is pinned by the HaydnTests unit test).
;
; CHECK-LABEL: quad:
; CHECK: // =>This Inner Loop Header: Depth=1
; REBASELINED 2026-08-22 (topics/hwloop PM2 AIE decline-list port): the
; body's calls are llvm.haydn.fmulaa32x16.* target intrinsics (inline
; ISel patterns), so TTI now arms a hardware loop and the AsmPrinter
; emits the ZOL start label (+ its "Label of block must be emitted"
; comment) between the header comment and the first body parcel. The
; two-parcel MAC seating contract below is unchanged.
; CHECK: Label of block must be emitted
; CHECK: [[HWL:.LLhwloop_start[0-9]+]]:
; First bundle: two h3.l2 MACs in ONE parcel.
; CHECK-NEXT: { {{.*}}fmulaa32x16.h3.l2 {{.*}}; {{.*}}fmulaa32x16.h3.l2
; Second bundle: two h1.l0 MACs in ONE parcel (a third op may share it).
; CHECK-NEXT: { {{.*}}fmulaa32x16.h1.l0 {{.*}}fmulaa32x16.h1.l0
; The two parcels above carry all four MACs: no fmulaa32x16 may appear on
; any later line (a singleton wrap would print one MAC per parcel line).
; CHECK-NOT: fmulaa32x16
