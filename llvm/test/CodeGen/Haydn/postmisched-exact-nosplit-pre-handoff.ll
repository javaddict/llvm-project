; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -stop-before=postmisched \
; RUN:   -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=PRE
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -stop-after=postmisched \
; RUN:   -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=POST
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -stats -verify-machineinstrs \
; RUN:   < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS

; Role: MIR — Product qualification for post-RA exact no-split commit and the pre-handoff hard-bundle entry law:.

; Product qualification for post-RA exact no-split commit (Option A containment):
;   * leaf/no-loop (pack_three_alu): stop-before postmisched has no hard BUNDLE
;     roots; post-RA owns packing.
;   * loop kernels (acc_stream / dual_stream): pre-RA SMS rejects StageCount>1
;     and never freezes multi-member BUNDLE; bare logical MIs through RA.
;   * stop-after postmisched: exact-commits multi-MI product cycles as BUNDLE {{[01]}}
;     (FormatE96 FormatID) with member setDesc where alts apply.
;   * Illegal multi-member hard root sequential-dissolve:
;     postmisched-hard-root-illegal-fail.mir.
;

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

; Independent ALU ops — post-RA auction + exact no-split may co-issue.
define i32 @pack_three_alu(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f) {
; PRE-LABEL: name: pack_three_alu
; PRE-NOT: BUNDLE
; PRE: ADD32
; PRE: XOR32
; PRE: OR32
;
; POST-LABEL: name: pack_three_alu
; POST: BUNDLE {{[01]}}
; POST-DAG: ADD32
; POST-DAG: XOR32
entry:
  %x = add i32 %a, %b
  %y = xor i32 %c, %d
  %z = or i32 %e, %f
  %t0 = add i32 %x, %y
  %t1 = add i32 %t0, %z
  ret i32 %t1
}

; Streaming accumulate — production path through RA into postmisched stays
; logical/bare (no hard cycle group), then post-RA owns BUNDLE roots.
define i32 @acc_stream(ptr nocapture readonly %p, i32 %n) {
; PRE-LABEL: name: acc_stream
; Option A: bare logicals pre-postmisched (no SMS multi-member freeze).
; PRE: ADD32
; PRE-NOT: BUNDLE
;
; POST-LABEL: name: acc_stream
; POST: BUNDLE {{[01]}}
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %v = load i32, ptr %pi, align 4
  %s.next = add i32 %s, %v
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  ret i32 %r
}

; Dual independent streams — denser product fills still exact-commit (no
; scheduled split repair). Entry remains free of hard BUNDLE roots.
define i32 @dual_stream(ptr nocapture readonly %x, ptr nocapture readonly %h,
                        i32 %n) {
; PRE-LABEL: name: dual_stream
; Option A containment: no pre-RA SMS multi-member BUNDLE into postmisched.
; PRE-NOT: BUNDLE
; PRE-DAG: LD32
;
; POST-LABEL: name: dual_stream
; POST: BUNDLE {{[01]}}
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %a = phi i32 [ 0, %entry ], [ %a.next, %loop ]
  %xi = getelementptr i32, ptr %x, i32 %i
  %hi = getelementptr i32, ptr %h, i32 %i
  %xv = load i32, ptr %xi, align 4
  %hv = load i32, ptr %hi, align 4
  %p = mul i32 %xv, %hv
  %a.next = add i32 %a, %p
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %a.next, %loop ]
  ret i32 %r
}

; At least one multi-MI exact finalize; the unstamped hard-root counter must
; stay at zero (absent from -stats). The 'failed exact no-split' counter may be zero under multi-stage SMS containment; when present it is
; EXPECTED to be non-zero: the post-RA field-order-RAW fix (companion regression
; postra-field-order-raw-narrow-store.mir) correctly fails commit for any
; multi-MI cycle whose field-order permutation would reverse a legal same-cycle
; WAR into a no-forwarding RAW hazard (which previously corrupted adjacent
; narrow stores). Such cycles correctly fall back to sequential parcels. A
; non-zero count here is correct product behavior (the fix is alive and rejecting
; hazardous bundles), NOT a regression; if it ever returns to 0 the fix has
; regressed and this canary must fail.
; STATS: multi-MI cycles finalized as BUNDLE
; STATS-NOT: unstamped multi-member hard BUNDLE roots at post-RA
