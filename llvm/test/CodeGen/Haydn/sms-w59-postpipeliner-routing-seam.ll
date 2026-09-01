; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=+hwloop \
; RUN:     -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:     -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -O2 -debug-only=pipeliner < %s -o %t.def.s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: FileCheck %s --check-prefix=ASM < %t.def.s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=+hwloop \
; RUN:     -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:     -global-isel-abort=1 -verify-machineinstrs -O2 \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:     -o %t.dual.s 2>%t.dual.rmk
; RUN: FileCheck %s --check-prefix=DUAL-RMK < %t.dual.rmk
; REQUIRES: asserts

; REGRESSION TEST (W59 / GOALS W59) — AIE preferPostPipeliner loop-routing
; seam port: ONE policy deciding, per loop, which pipelining engine owns it
; (pre-RA generic SMS StageCount==1 only vs post-RA HaydnMultiStageSMS).
;
; Bug class guarded: before W59 there was NO routing policy — only a seed
; comment (HaydnPipelinerLoopInfo.cpp:263-266). The two engines could
; contend or both skip; defer-class loops were rejected by bare containment
; with no routing evidence, and tooling could not tell "rejected because
; post-RA owns this loop" from "rejected for profitability".
;
; Port shape (AIEBasePipelinerLoopInfo.cpp:770-805):
;   defer when ZOL && MinTripCount>1 && (NS>3 && II<11, or II<4 &&
;   max SDep latency >= II). Haydn adaptation law: pre-RA NEVER accepts
;   multi-stage (PIPE-20/D493), so both AIE consumers collapse to the ONE
;   decline-AND-DEFER veto in shouldUseSchedule; the handshake is
;   remark-only (no state crosses RA — the post-RA host re-analyzes).
;
; Test design (explicit flags, default-independent; bodies verified at the
; current binary to schedule exactly as pinned):
;   * w59_defer_lowii: constant-trip (16) single-load accumulator. Pre-RA
;     finds NS=2 II=2; LoadLatency=2 >= II=2 fires the latency arm →
;     SWP pins the W59-DEFER line (and NOT the bare containment line:
;     routing owns this reject now). Dual-ON run pins the post-RA host
;     accepting the SAME loop (accepted remark with stages>=1) — the
;     defer actually routes.
;   * w59_nodefer_ii3: dual-load MAC, NS=2 II=3. max latency (2) < II(3)
;     and NS<=3 → NO defer; the bare containment line still owns the
;     reject (unchanged pre-W59 behavior — the seam must not grow).
;   * w59_nodefer_mintc: runtime trip, MinTripCount=0 → the ZOL MinTC
;     gate (upstream of the seam) still owns the reject; W59 never sees
;     it (post-RA can do nothing with unknown trip either... it fails
;     closed there too, but that is the host's own preflight).
;   * w59_nodefer_singlestage: NS=1 II=1 → single-stage reject unchanged
;     (upstream of the seam; boundary NS==1 never defers).
; The NS arm (NS>3 && II<11) is organically unreachable today (pre-RA II
; search converges to NS=2; upstream SwpMaxStages=3) — its polarity is
; pinned in the pure-predicate unit test
; (HaydnHazardRecognizerTest.cpp PreRASMSSMSDeferToPostRAMultiStageRouting).

; W59 RETIRED (W68.1): with ZOL multi-stage owned by the generic pre-RA
; MachinePipeliner there is one engine and no routing — the W59-DEFER line
; must never appear again. Every ZOL disposition is now a plain
; shouldUseSchedule decision: guarded multi-stage ACCEPTS (w59_defer_lowii,
; static MinTripCount 16), the MinTC gate still refuses unknown trip
; (w59_nodefer_mintc), and single-stage still refuses (no overlap).
; SWP: Schedule Found? 1 (II=2)
; SWP: SMS-SHOULDUSE: accept stages=2 II=2 (metrics-only; bare logical MIs; proven counted residual; no pre-RA cycle groups; product containment (PPS-3 bound))
; SWP: Schedule Found? 1 (II=3)
; SWP: SMS-SHOULDUSE: accept stages=2 II=3 (metrics-only; bare logical MIs; proven counted residual; no pre-RA cycle groups; product containment (PPS-3 bound))
; SWP: Schedule Found? 1 (II=2)
; SWP: ZOL: reject SMS (MaxStageCount=1 MinTripCount=0)
; SWP: Schedule Found? 1 (II=1)
; SWP: ZOL: rejecting single-stage schedule
; SWP-NOT: W59-DEFER

; ASM-LABEL: w59_defer_lowii:
; ASM-NOT: #<swps> stages=
; ASM: jalr

; DUAL-RMK: accepted II=[[II:[0-9]+]] stages={{[1-9][0-9]*}}
; DUAL-RMK-SAME: measured-II=[[II]]

define i32 @w59_defer_lowii(ptr nocapture readonly %p) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.n, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %s.n = add i32 %s, %v
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, 16
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  ret i32 %s.n
}

define i32 @w59_nodefer_ii3(ptr nocapture readonly %a, ptr nocapture readonly %b) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %mul = mul i32 %va, %vb
  %acc.next = add i32 %acc, %mul
  %i.next = add nuw i32 %i, 1
  %cond = icmp ult i32 %i.next, 16
  br i1 %cond, label %loop, label %exit
exit:
  ret i32 %acc.next
}

define i32 @w59_nodefer_mintc(ptr nocapture readonly %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [ 0, %pre ], [ %i.n, %loop ]
  %s = phi i32 [ 0, %pre ], [ %s.n, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %s.n = add i32 %s, %v
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  %r = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  ret i32 %r
}

define i32 @w59_nodefer_singlestage(ptr nocapture readonly %a) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %a, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %mul = mul i32 %va, %vb
  %i.next = add nuw i32 %i, 1
  %cond = icmp ult i32 %i.next, 16
  br i1 %cond, label %loop, label %exit
exit:
  ret i32 %mul
}

!0 = !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
