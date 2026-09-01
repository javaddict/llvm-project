; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; REGRESSION TEST (G008, 2026-08-23): cross-iteration memory disambiguation
; on the two-copy graph (AIE aliasAcrossVirtualUnrolls port).
;
; System model: the two-copy graph's memory chain edges were built from
; STATELESS same-iteration AA (MachineInstr::mayAlias), so a store and a
; load/store on the SAME recurrent pointer-phi stream at different constant
; offsets kept a false cross-copy (loop-carried) edge whenever same-iteration
; AA could not see the iteration axis at all. The G008 law proves the pair
; disjoint at EVERY iteration distance d>=1 (windows advance in lockstep by
; the phi byte stride S; separation sep(d)=dOff+d*S is strictly increasing,
; so the all-distances check is the exact two-case law: sep(1)>=Wa, or at
; the first clearing distance d0 the last sub-clearing separation jumps
; clean: sep(d0-1)<=-Wb) and drops the provably-false edge at the same
; two-copy seam pruneTwoCopySeamArtifacts owns. Fail-closed: distinct bases,
; unknown offsets, unknown widths, or
; non-positive stride keep the conservative edge.
;
; Test design — four fixtures, one file:
;   stream_k1   (a) store x[i], load x[i+1] on ONE stream (two-arg form so
;               LSR keeps the preheader clean — the preheader-real-instr
;               candidacy gap is OUT of scope here, owner topics/scheduling).
;               BEFORE G008: edges=4, II search [2,4] declined ii-exhaustion.
;   stream_k4   (a') store x[i], load x[i+4] — wider separation, same law.
;   two_noalias (b) distinct-object streams: IR AA already resolves
;               same-iteration; no cross-copy memory edge existed BEFORE or
;               AFTER (pure control; kind/edges unchanged by G008).
;   stream_rk   (c) SAME object, two streams with RUNTIME-relative bases:
;               disjointness at any distance is unprovable → the
;               conservative cross-copy memory edge MUST stay (edges=5
;               mem=1 before AND after; fail-closed control).
;
; The G006 corpus noise rule applies: II/edges are CAPTURED variables where
; the value is free to improve; the (a) edges= count is the pinned contract
; (must DROP vs the pre-G008 value — the same-phi stream pairs (store@-16,
; load@0) and (store,store-next) are provably disjoint at every distance
; d>=1: sep(d) = 16 + 4d >= 20 > 4, and tiling sep(1)=4 >= width).

target triple = "haydn-unknown-elf"

; (a) the II-floor streaming shape: load x[i+1] (delayed stream), store
; x[i]. Pre-G008 the false cross-copy store→load chain edge kept the body
; serialized across iterations.
define void @stream_k1(ptr noalias %x0, ptr noalias %x1, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %p = phi ptr [%x0, %pre],[%p.n, %body]
  %i = phi i32 [0, %pre],[%i.n, %body]
  %v = load i32, ptr %p, align 4
  %w = xor i32 %v, 1
  %q = getelementptr inbounds i32, ptr %p, i32 -1
  store i32 %w, ptr %q, align 4
  %p.n = getelementptr inbounds i32, ptr %p, i32 1
  %i.n = add nuw nsw i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %body, label %exit, !llvm.loop !0
exit:
  ret void
}

; (a') wider separation: load x[i+4], store x[i] (K=4 >= width 4).
define void @stream_k4(ptr noalias %xhead, ptr noalias %xtail, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %p = phi ptr [%xhead, %pre],[%p.n, %body]
  %i = phi i32 [0, %pre],[%i.n, %body]
  %v = load i32, ptr %p, align 4
  %w = xor i32 %v, 1
  %q = getelementptr inbounds i32, ptr %p, i32 -4
  store i32 %w, ptr %q, align 4
  %p.n = getelementptr inbounds i32, ptr %p, i32 1
  %i.n = add nuw nsw i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %body, label %exit, !llvm.loop !0
exit:
  ret void
}

; (b) distinct-object control (cb148 class): two noalias bases.
define void @two_noalias(ptr noalias %dst, ptr noalias %src, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %pd = phi ptr [%dst, %pre],[%pd.n, %body]
  %ps = phi ptr [%src, %pre],[%ps.n, %body]
  %i = phi i32 [0, %pre],[%i.n, %body]
  %v = load i32, ptr %ps, align 4
  %w = xor i32 %v, 1
  store i32 %w, ptr %pd, align 4
  %pd.n = getelementptr inbounds i32, ptr %pd, i32 1
  %ps.n = getelementptr inbounds i32, ptr %ps, i32 1
  %i.n = add nuw nsw i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %body, label %exit, !llvm.loop !0
exit:
  ret void
}

; (c) fail-closed control: ONE object (no noalias claim between the bases),
; two streams whose relative offset is a runtime value. Nothing proves
; disjointness at any distance; the conservative edge must survive G008.
define void @stream_rk(ptr %x, ptr %xk, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %p = phi ptr [%x, %pre],[%p.n, %body]
  %q = phi ptr [%xk, %pre],[%q.n, %body]
  %i = phi i32 [0, %pre],[%i.n, %body]
  %v = load i32, ptr %q, align 4
  %w = xor i32 %v, 1
  store i32 %w, ptr %p, align 4
  %p.n = getelementptr inbounds i32, ptr %p, i32 1
  %q.n = getelementptr inbounds i32, ptr %q, i32 1
  %i.n = add nuw nsw i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %body, label %exit, !llvm.loop !0
exit:
  ret void
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}

; (a)/(a') CONTRACT: the provably-false same-stream cross-copy memory edges
; are gone. Before G008 both stream fixtures read edges=4 (captured on the
; pre-change build, 2026-08-23). After: edges=3 — the register recurrence
; only; the pruned edge is the store(i)→load(i+d) chain edge over the same
; phi stream (sep(d)=4+4d >= 8 > width). Both fixtures share the identical
; count (same law, different K — K enters only through the provable side).
; kind stays declined (the format seat, not memory, governs accept here —
; this fixture pins the DEPENDENCE contract, not the schedule). Emission
; order is PER FUNCTION: its lcd engine line(s) then that function's
; canonical G005 line (k1, k4, two_noalias, stream_rk) — blocks below
; mirror the true order.
; RMK: lcd two-iteration RecMII={{[0-9]+}} edges=3 mem=0 ResMII={{[0-9]+}}
; RMK: Schedule found II={{[0-9]+}} NS={{[0-9]+}} prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=declined seat=ii-exhaustion
; RMK-SAME: loop=bb.{{[0-9]+}}.body
; RMK: lcd two-iteration RecMII={{[0-9]+}} edges=3 mem=0 ResMII={{[0-9]+}}
; RMK: Schedule found II={{[0-9]+}} NS={{[0-9]+}} prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=declined seat=ii-exhaustion
; RMK-SAME: loop=bb.{{[0-9]+}}.body
; (b) control: unchanged shape (accepted; no cross-copy memory edge ever).
; RMK: lcd two-iteration RecMII={{[0-9]+}} edges={{[0-9]+}}
; RMK-SAME: mem={{[0-9]+}} ResMII={{[0-9]+}}
; RMK: lcd two-iteration RecMII={{[0-9]+}} edges={{[0-9]+}}
; RMK-SAME: mem={{[0-9]+}} stages={{[0-9]+}}
; RMK: Schedule found II=[[BII:[0-9]+]] NS=[[BNS:[0-9]+]] prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=accepted loop=bb.{{[0-9]+}}.body
; (c) CONTRACT: fail-closed — runtime-relative streams keep the memory LCD.
; Before G008: edges=5 mem=1; after G008 the same values (nothing proven).
; RMK: lcd two-iteration RecMII={{[0-9]+}} edges=5 mem=1
; RMK-SAME: ResMII={{[0-9]+}}
; RMK: lcd two-iteration RecMII={{[0-9]+}} edges=5 mem=1
; RMK-SAME: stages={{[0-9]+}}
; RMK: Schedule found II=[[CII:[0-9]+]] NS=[[CNS:[0-9]+]] prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=accepted loop=bb.{{[0-9]+}}.body

; ASM: stream_k1:
; ASM: jalr
; ASM: stream_k4:
; ASM: jalr
; ASM: two_noalias:
; ASM: jalr
; ASM: stream_rk:
; ASM: jalr
