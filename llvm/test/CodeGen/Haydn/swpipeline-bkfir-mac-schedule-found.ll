; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -stop-after=pipeliner -debug-only=pipeliner \
; RUN:     < %s 2>&1 | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -stop-after=pipeliner \
; RUN:     -haydn-enable-multistage-sms=false < %s \
; RUN:     | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms=false \
; RUN:     < %s | FileCheck %s --check-prefix=ORD-ASM
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.postra.rmk | FileCheck %s --check-prefix=POSTRA-ASM
; RUN: FileCheck %s --check-prefix=POSTRA < %t.postra.rmk
; RUN: not grep -q 'accepted II=' %t.postra.rmk || FileCheck %s --check-prefix=POSTRA-ACC < %t.postra.rmk
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.mat.rmk | FileCheck %s --check-prefix=MAT-ASM
; RUN: FileCheck %s --check-prefix=MAT < %t.mat.rmk
; RUN: not grep -q 'accepted II=' %t.mat.rmk || FileCheck %s --check-prefix=POSTRA-ACC < %t.mat.rmk

; Role: semantic — NatureDSP bkfir32x32 MAC hot-loop shape: dual coef loads + 4x4
; acc-MAC chains + dual circular-buffer sample loads. Positive SMS schedule-found
; pin: SMS pipelines this hot MAC loop (no scalar fallback). Requires MAC
; acc->acc latency 1, load->acc-MAC latency 2, S2-first pickSlot, ADDI32 (not
; ADDI32_W), and MachinePipeliner computeNodeOrder pred_L filtered by NodeSet.
;
; Hang containment: generic SMS arms still stop after the pipeliner so
; "Schedule Found?" stays independent of RA. ORD-ASM / POSTRA / MAT run
; through RA + post-RA (T4 hang-root is capped). Same-artifact QUALIFY is
; hwloops OFF: ordinary list-schedule+commit completes; SMS analysis and
; materialize either exhaust/reject (recorded capped-reject QoR on this
; dense MAC body) or accept with parcels-per-iter == searched II.
; Product multi-stage stays OFF.
;
; Why this is contract-only (no brittle bundle body): the D999 no-forwarding
; fix changes SMS placement to call the operand-aware MI overload of
; ResourceCycle (llvm/lib/CodeGen/MachinePipeliner.cpp). calculateResMIIDFA
; shares that MI overload, so ResMII — and therefore the starting II and the
; exact kernel bundle layout — may shift vs the pre-D999 schedule even for loops
; with no live intra-bundle RAW, because the greedy FuncUnit-order packing now
; accounts for no-forwarding spacing. The previous per-bundle CHECKs were
; auto-generated against the pre-D999 schedule and are no longer reliable. The
; durable contract is that SMS FINDS a schedule for this loop (schedule-limited,
; not rejected) and emits a non-empty kernel. Regenerate exact II/bundles with
; utils/update_llc_test_checks.py after a coordinator build if a precision pin
; is wanted again.

; SWP contract (single function): SMS pipelines this loop. The pipeliner
; -debug-only trace (including "Schedule Found? 1") is emitted BEFORE the
; asm, so it is matched here up front rather than after ASM-LABEL.
; SWP: Schedule Found? 1
; SWP-NOT: Unable to analyzeLoop, can NOT pipeline Loop

; ASM: name:{{[ 	]+}}bkfir_mac_hot
; ASM: RET
;
; ORD-ASM: bkfir_mac_hot:
; ORD-ASM: jalr
; ORD-ASM-NOT: set_hwloop
; ORD-ASM-NOT: #<swps>
;
; POSTRA-ASM: bkfir_mac_hot:
; POSTRA-ASM: jalr
; POSTRA: resource-bias=slot-windows
; POSTRA: {{accepted II=|exhausted:|rejected:}}
; POSTRA: qualify-or-cut
; POSTRA: product-off
; POSTRA: nat-ipc=measured-miss
; POSTRA: no-competitive-ipc
; POSTRA: no-stage0-ib-pp
; POSTRA: hwloop-combined=off
; POSTRA-NOT: sequential (preflight)
;
; MAT-ASM: bkfir_mac_hot:
; MAT-ASM: jalr
; MAT: {{accepted II=|exhausted:|rejected:|preflight reject:}}
; MAT: qualify-or-cut
; MAT: product-off
; MAT: nat-ipc=measured-miss
; MAT-NOT: sequential (preflight)
;
; POSTRA-ACC: accepted II=[[II:[0-9]+]]
; POSTRA-ACC-SAME: measured-II=[[II]]
; POSTRA-ACC: qualify parcels-per-iter=[[II]]
; POSTRA-ACC-SAME: searched-II=[[II]]

define void @bkfir_mac_hot(
    ptr nocapture readonly %C,
    ptr nocapture readonly %D0,
    ptr nocapture readonly %D1,
    ptr nocapture %Q,
    i32 %M4) {
entry:
  %cmp = icmp sgt i32 %M4, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %pc = phi ptr [ %C, %entry ], [ %pc.n, %loop ]
  %q0 = phi i64 [ 0, %entry ], [ %q0.n, %loop ]
  %q1 = phi i64 [ 0, %entry ], [ %q1.n, %loop ]
  %q2 = phi i64 [ 0, %entry ], [ %q2.n, %loop ]
  %q3 = phi i64 [ 0, %entry ], [ %q3.n, %loop ]
  %d01 = phi i64 [ 0, %entry ], [ %d45, %loop ]
  %d12 = phi i64 [ 0, %entry ], [ %d56, %loop ]

  %d23 = load i64, ptr %D0, align 8
  %d45 = load i64, ptr %D0, align 8
  %d34 = load i64, ptr %D1, align 8
  %d56 = load i64, ptr %D1, align 8

  %c0 = load i64, ptr %pc, align 4
  %pc1 = getelementptr inbounds i64, ptr %pc, i32 1
  %c1 = load i64, ptr %pc1, align 4
  %pc.n = getelementptr inbounds i64, ptr %pc, i32 2

  %bc.1 = bitcast i64 %d01 to <2 x i32>
  %bc.2 = bitcast i64 %c0 to <2 x i32>
  %q0.a = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q0, <2 x i32> %bc.1, <2 x i32> %bc.2)
  %bc.3 = bitcast i64 %d01 to <2 x i32>
  %bc.4 = bitcast i64 %c0 to <2 x i32>
  %q0.b = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q0.a, <2 x i32> %bc.3, <2 x i32> %bc.4)
  %bc.5 = bitcast i64 %d23 to <2 x i32>
  %bc.6 = bitcast i64 %c1 to <2 x i32>
  %q0.c = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q0.b, <2 x i32> %bc.5, <2 x i32> %bc.6)
  %bc.7 = bitcast i64 %d23 to <2 x i32>
  %bc.8 = bitcast i64 %c1 to <2 x i32>
  %q0.n = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q0.c, <2 x i32> %bc.7, <2 x i32> %bc.8)

  %bc.9 = bitcast i64 %d12 to <2 x i32>
  %bc.10 = bitcast i64 %c0 to <2 x i32>
  %q1.a = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q1, <2 x i32> %bc.9, <2 x i32> %bc.10)
  %bc.11 = bitcast i64 %d12 to <2 x i32>
  %bc.12 = bitcast i64 %c0 to <2 x i32>
  %q1.b = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q1.a, <2 x i32> %bc.11, <2 x i32> %bc.12)
  %bc.13 = bitcast i64 %d34 to <2 x i32>
  %bc.14 = bitcast i64 %c1 to <2 x i32>
  %q1.c = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q1.b, <2 x i32> %bc.13, <2 x i32> %bc.14)
  %bc.15 = bitcast i64 %d34 to <2 x i32>
  %bc.16 = bitcast i64 %c1 to <2 x i32>
  %q1.n = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q1.c, <2 x i32> %bc.15, <2 x i32> %bc.16)

  %bc.17 = bitcast i64 %d23 to <2 x i32>
  %bc.18 = bitcast i64 %c0 to <2 x i32>
  %q2.a = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q2, <2 x i32> %bc.17, <2 x i32> %bc.18)
  %bc.19 = bitcast i64 %d23 to <2 x i32>
  %bc.20 = bitcast i64 %c0 to <2 x i32>
  %q2.b = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q2.a, <2 x i32> %bc.19, <2 x i32> %bc.20)
  %bc.21 = bitcast i64 %d45 to <2 x i32>
  %bc.22 = bitcast i64 %c1 to <2 x i32>
  %q2.c = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q2.b, <2 x i32> %bc.21, <2 x i32> %bc.22)
  %bc.23 = bitcast i64 %d45 to <2 x i32>
  %bc.24 = bitcast i64 %c1 to <2 x i32>
  %q2.n = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q2.c, <2 x i32> %bc.23, <2 x i32> %bc.24)

  %bc.25 = bitcast i64 %d34 to <2 x i32>
  %bc.26 = bitcast i64 %c0 to <2 x i32>
  %q3.a = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q3, <2 x i32> %bc.25, <2 x i32> %bc.26)
  %bc.27 = bitcast i64 %d34 to <2 x i32>
  %bc.28 = bitcast i64 %c0 to <2 x i32>
  %q3.b = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q3.a, <2 x i32> %bc.27, <2 x i32> %bc.28)
  %bc.29 = bitcast i64 %d56 to <2 x i32>
  %bc.30 = bitcast i64 %c1 to <2 x i32>
  %q3.c = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q3.b, <2 x i32> %bc.29, <2 x i32> %bc.30)
  %bc.31 = bitcast i64 %d56 to <2 x i32>
  %bc.32 = bitcast i64 %c1 to <2 x i32>
  %q3.n = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q3.c, <2 x i32> %bc.31, <2 x i32> %bc.32)

  %i.next = add nuw nsw i32 %i, 1
  %done = icmp eq i32 %i.next, %M4
  br i1 %done, label %exit, label %loop

exit:
  %r0 = phi i64 [ 0, %entry ], [ %q0.n, %loop ]
  %r1 = phi i64 [ 0, %entry ], [ %q1.n, %loop ]
  %r2 = phi i64 [ 0, %entry ], [ %q2.n, %loop ]
  %r3 = phi i64 [ 0, %entry ], [ %q3.n, %loop ]
  store i64 %r0, ptr %Q, align 8
  %Q1 = getelementptr inbounds i64, ptr %Q, i32 1
  store i64 %r1, ptr %Q1, align 8
  %Q2 = getelementptr inbounds i64, ptr %Q, i32 2
  store i64 %r2, ptr %Q2, align 8
  %Q3 = getelementptr inbounds i64, ptr %Q, i32 3
  store i64 %r3, ptr %Q3, align 8
  ret void
}

declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, <2 x i32>, <2 x i32>)
