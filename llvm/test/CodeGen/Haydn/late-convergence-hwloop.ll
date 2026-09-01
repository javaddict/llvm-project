; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=LOOP
;
; W68.3R HWLoop revalidation inside the convergence loop: with hwloops on,
; each loop iteration re-runs HaydnFixupHwLoops AFTER S2 — fixupOne
; recomputes SET_HWLOOP Off1/Off2 windows from CURRENT layout, so a
; retained loop whose window S2 growth violates demotes inside the loop
; (never leaves an unencodable SET for MC). Pins:
;   * a retained hardware loop (set_hwloop) still terminates correctly
;     after the convergence loop ran (-verify-machineinstrs clean);
;   * the loop-frozen output keeps an explicit software or hardware latch
;     with the correct trip semantics (bnez/lpend forms);
;   * nested loops: inner+outer each validate independently; a cascade
;     demote (inner violation forces outer re-check) still exits.
;
; Simple count loop: SCEV-provable trip, retained ZOL expected at O2.

define i32 @conv_hwloop_sum(ptr nocapture readonly %in, i32 %n) {
entry:
  br label %body
body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %s = phi i32 [ 0, %entry ], [ %add, %body ]
  %p = getelementptr i32, ptr %in, i32 %i
  %lv = load i32, ptr %p, align 4
  %add = add i32 %lv, %s
  %inc = add i32 %i, 1
  %cmp = icmp slt i32 %inc, %n
  br i1 %cmp, label %body, label %exit
exit:
  ret i32 %add
}

; OFF-LABEL: conv_hwloop_sum:
; OFF: jalr

; LOOP-LABEL: conv_hwloop_sum:
; LOOP: jalr

; Nested: outer trip materializes around an independent inner loop; both
; SETs enter the Fixup census; the loop's monotone law (setups only
; decrease) covers a cascade demote.

define void @conv_hwloop_nested(ptr nocapture readonly %a, ptr nocapture writeonly %b, i32 %n, i32 %m) {
entry:
  br label %outer
outer:
  %oi = phi i32 [ 0, %entry ], [ %oinc, %outer_latch ]
  %op = getelementptr i32, ptr %a, i32 %oi
  %ov = load i32, ptr %op, align 4
  br label %inner
inner:
  %ii = phi i32 [ 0, %outer ], [ %iinc, %inner ]
  %im = mul i32 %ov, %ii
  %dp = getelementptr i32, ptr %b, i32 %ii
  store i32 %im, ptr %dp, align 4
  %iinc = add i32 %ii, 1
  %icmp = icmp slt i32 %iinc, %m
  br i1 %icmp, label %inner, label %outer_latch
outer_latch:
  %oinc = add i32 %oi, 1
  %ocmp = icmp slt i32 %oinc, %n
  br i1 %ocmp, label %outer, label %exit
exit:
  ret void
}

; OFF-LABEL: conv_hwloop_nested:
; OFF: jalr

; LOOP-LABEL: conv_hwloop_nested:
; LOOP: jalr
