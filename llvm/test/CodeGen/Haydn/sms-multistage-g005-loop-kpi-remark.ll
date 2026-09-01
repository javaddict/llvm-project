; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
; RUN: FileCheck %s --check-prefix=COUNT < %t.rmk

; G005 CANONICAL PER-LOOP II/NS REMARK (ultragoal story G005, 2026-08-23).
;
; System model: the post-RA multistage engine journals one outcome record
; per attempted loop into HaydnMachineFunctionInfo (the per-region Host
; dies at region exit); the shared post-RA host's finalizeSchedule seat
; then emits EXACTLY ONE canonical line per single-MBB loop of the
; function, in MachineFunction layout order. Peer: AIE emitLoopRemarks
; (AIEInterBlockScheduling.cpp:322-405) + emitPipelinerRemark; Haydn
; overlay: ZOL kernels (PLE terminator + preheader SET/LoopStart) are not
; CFG-visible loops and enter the census by that law; "II" carries AIE
; tooling semantics — the engine's II when it scheduled one, else the
; realized body parcel count ("body length as II").
;
; Canonical line shape (M2/M18 KPI input):
;   Schedule found II=<n> NS=<n> prologue=<n> parcels epilogue=<n> parcels
;   kind=<accepted|accepted-analysis|declined|not-candidate>
;   [seat=<last-reject-reason>] loop=bb.<N>
;
; Test design — four loop classes (G005 spec):
;   accepting     — dot-store kernel with llvm.loop.itercount.range MD:
;                   engine accepts (kind=accepted-analysis under
;                   analysis-only; MIR stays unmutated).
;   declining     — same shape without trip MD: engine candidates and
;                   declines fail-closed (seat vocabulary from the
;                   engine's reject reasons).
;   not_candidate — data-dependent exit (loop until loaded value < bound):
;                   no ZOL conversion, no +/-1 countdown bump; the engine
;                   never candidates it; II reports the body parcel count.
;   multi_mbb     — nested loop whose inner body spans TWO MBBs (head +
;                   body, conditional back-edge from body to head): no
;                   single-MBB loop exists in the function, so the census
;                   emits NOTHING for it (COUNT asserts exactly three
;                   canonical lines in the whole remark stream).
; II values are CAPTURED not pinned (noise rule); the contract is the
; kind vocabulary, per-loop uniqueness, layout ordering, and coexistence
; with the existing engine remark lines (ADDITIVE law — none changed).

target triple = "haydn-unknown-elf"

; Canonical line 1 (accepting): II parity with the engine's accept line.
; Existing engine accept line unchanged (no regression). Engine lines for
; each region precede ALL canonical lines (canonical batch is emitted at
; finalizeSchedule, after the last region) — RMK order below matches that
; emission order: engine accept pair, then the three canonical lines.
; RMK: accepted II=[[AII:[0-9]+]]
; RMK-SAME: measured-II=[[AII]]
; RMK: Schedule found II=[[AII]] NS=[[ANS:[0-9]+]] prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=accepted-analysis loop=bb.[[AL:[0-9]+]]
; Canonical line 2 (declining): reached search floor, seat vocabulary.
; RMK: Schedule found II=[[DII:[0-9]+]] NS=[[DNS:[0-9]+]] prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=declined seat=[[SEAT:[a-zA-Z-]+]]
; RMK-SAME: loop=bb.[[DL:[0-9]+]]
; Canonical line 3 (not-candidate): never attempted; body-length II.
; RMK: Schedule found II=[[NII:[0-9]+]] NS=0 prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=not-candidate loop=bb.[[NLBL:[0-9]+]]
; Exactly three canonical lines, in this (layout) order, none after.
; COUNT-COUNT-3: Schedule found
; COUNT-NOT: Schedule found

; ASM-LABEL: accepting:
; ASM: jalr
; ASM-LABEL: declining:
; ASM: jalr
; ASM-LABEL: not_candidate:
; ASM: jalr
; ASM-LABEL: multi_mbb:
; ASM: jalr

define void @accepting(ptr nocapture readonly %a, ptr nocapture readonly %b, ptr %dst, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %i = phi i32 [ 0, %pre ], [ %i.n, %body ]
  %s = phi i32 [ 0, %pre ], [ %s.n, %body ]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %pd = getelementptr inbounds i32, ptr %dst, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %m = mul i32 %va, %vb
  %s.n = add i32 %s, %m
  store i32 %s.n, ptr %pd, align 4
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %body, label %exit, !llvm.loop !0
exit:
  ret void
}

define i32 @declining(ptr nocapture readonly %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %j = phi i32 [ 0, %pre ], [ %j.n, %body ]
  %t = phi i32 [ 0, %pre ], [ %t.n, %body ]
  %q = getelementptr inbounds i32, ptr %p, i32 %j
  %u = load i32, ptr %q, align 4
  %t.n = add i32 %t, %u
  %j.n = add i32 %j, 1
  %cd = icmp ult i32 %j.n, %n
  br i1 %cd, label %body, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %t.n, %body ]
  ret i32 %r
}

define i32 @not_candidate(ptr nocapture readonly %p) {
entry:
  %e0 = load i32, ptr %p, align 4
  %pe = getelementptr inbounds i32, ptr %p, i32 1
  br label %body
body:
  %k = phi i32 [ 0, %entry ], [ %k.n, %body ]
  %acc = phi i32 [ %e0, %entry ], [ %acc.n, %body ]
  %kk = mul i32 %k, 3
  %ge = getelementptr inbounds i32, ptr %pe, i32 %kk
  %v = load i32, ptr %ge, align 4
  %acc.n = xor i32 %acc, %v
  %k.n = add i32 %k, 2
  %cp = icmp ult i32 %acc.n, %e0
  br i1 %cp, label %body, label %exit
exit:
  ret i32 %acc.n
}

define i32 @multi_mbb(ptr nocapture readonly %b, i32 %start) {
entry:
  br label %outer.head
outer.head:
  %x = phi i32 [ 0, %entry ], [ %x.n, %outer.latch ]
  %sum = phi i32 [ %start, %entry ], [ %sum.n, %outer.latch ]
  br label %inner.head
inner.head:
  %y = phi i32 [ 0, %outer.head ], [ %y.b, %inner.body ], [ %y.a, %inner.alt ]
  %even = icmp eq i32 %y, 4
  br i1 %even, label %inner.body, label %inner.alt
inner.body:
  %ld = getelementptr inbounds i32, ptr %b, i32 %y
  %lv = load i32, ptr %ld, align 4
  %sum.b = add i32 %sum, %lv
  %y.b = add i32 %y, 1
  %cb = icmp ult i32 %y.b, 8
  br i1 %cb, label %inner.head, label %outer.latch
inner.alt:
  %sum.a = add i32 %sum, %y
  %y.a = add i32 %y, 1
  %ca = icmp ult i32 %y.a, 8
  br i1 %ca, label %inner.head, label %outer.latch
outer.latch:
  %sum.n = phi i32 [ %sum.b, %inner.body ], [ %sum.a, %inner.alt ]
  %x.n = add i32 %x, 1
  %co = icmp ult i32 %x.n, 4
  br i1 %co, label %outer.head, label %exit
exit:
  ret i32 %sum.n
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
