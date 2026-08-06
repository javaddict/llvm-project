; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -O2 -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — setup-gap geometry (cycle-primary; bytes from product parcel).
;
; Geometry (golden Format E product law):
;   - Strict END: BEGIN < END (END = last body cycle; body parcels >= 3)
;   - SetupIssueDistance = 3  (Cycle(BEGIN) - Cycle(SET) >= 3)
;   - InterveningCycles  = 2  (Following size-bearing parcels after SET)
;   - MinSetupBytes = InterveningCycles × productParcelBytes (not a free freeze)
; Do not collapse this pair into the ambiguous phrase "t-3" alone.
;
; Loop body is intentionally ≥ MinBodyBundles so hardware form is kept; the
; check focuses on the SET→BEGIN intervening gap (two size-bearing parcels).

define i32 @short_hwloop(i32 %n, ptr %p, ptr %q) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %gq = getelementptr inbounds i32, ptr %q, i32 %i
  %w = load i32, ptr %gq, align 4
  %sum = add i32 %v, %w
  %acc.next = add i32 %acc, %sum
  %i.next = add i32 %i, 1
  %cont = icmp slt i32 %i.next, %n
  br i1 %cont, label %loop, label %exit

exit:
  %r = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  ret i32 %r
}

; CHECK: set_hwloop{{(_f2)?}}{{(_w)?}}
; Two size-bearing parcels after SET (InterveningCycles=2 → BEGIN at distance 3).
; CHECK-NEXT: {
; CHECK-NEXT: {
; CHECK: LLhwloop_start
; Body parcels BEGIN..END inclusive >= 3 (product MinBodyBundles).
; CHECK: LLhwloop_end
